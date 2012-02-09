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

/* wrapper macro to tell whether t points to one of the sip_tech descriptors */
#define IS_SIP_TECH(t)  ((t) == &sip_tech || (t) == &sip_tech_info)

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


/*! \brief Convert SIP hangup causes to Asterisk hangup causes */
int hangup_sip2cause(int cause);

/*! \brief Convert Asterisk hangup causes to SIP codes
\verbatim
 Possible values from causes.h
        AST_CAUSE_NOTDEFINED    AST_CAUSE_NORMAL        AST_CAUSE_BUSY
        AST_CAUSE_FAILURE       AST_CAUSE_CONGESTION    AST_CAUSE_UNALLOCATED

	In addition to these, a lot of PRI codes is defined in causes.h
	...should we take care of them too ?

	Quote RFC 3398

   ISUP Cause value                        SIP response
   ----------------                        ------------
   1  unallocated number                   404 Not Found
   2  no route to network                  404 Not found
   3  no route to destination              404 Not found
   16 normal call clearing                 --- (*)
   17 user busy                            486 Busy here
   18 no user responding                   408 Request Timeout
   19 no answer from the user              480 Temporarily unavailable
   20 subscriber absent                    480 Temporarily unavailable
   21 call rejected                        403 Forbidden (+)
   22 number changed (w/o diagnostic)      410 Gone
   22 number changed (w/ diagnostic)       301 Moved Permanently
   23 redirection to new destination       410 Gone
   26 non-selected user clearing           404 Not Found (=)
   27 destination out of order             502 Bad Gateway
   28 address incomplete                   484 Address incomplete
   29 facility rejected                    501 Not implemented
   31 normal unspecified                   480 Temporarily unavailable
\endverbatim
*/
const char *hangup_cause2sip(int cause);

/*! \brief Return a string describing the force_rport value for the given flags */
const char *force_rport_string(struct ast_flags *flags);

/*! \brief Return a string describing the comedia value for the given flags */
const char *comedia_string(struct ast_flags *flags);

#endif
