/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

/*!	\file enum.h
	\brief DNS and ENUM functions
*/

#ifndef _ASTERISK_ENUM_H
#define _ASTERISK_ENUM_H

#include "asterisk/channel.h"

/*! \brief Lookup entry in ENUM Returns 1 if found, 0 if not found, -1 on hangup
	\param chan	Channel
   \param number   E164 number with or without the leading +
	\param location Number returned	(or SIP uri)
	\param maxloc	Max length
   \param technology     Technology (from url scheme in response)
                       You can set it to get particular answer RR, if there are many techs in DNS response, example: "sip"
                       If you need any record, then set it to empty string
   \param maxtech  Max length
   \param suffix   Zone suffix (if is NULL then use enum.conf 'search' variable)
   \param options  Options ('c' to count number of NAPTR RR, or number - the position of required RR in the answer list
*/
extern int ast_get_enum(struct ast_channel *chan, const char *number, char *location, int maxloc, char *technology, int maxtech, char* suffix, char* options);

/*!	\brief Lookup DNS TXT record (used by app TXTCIDnum
	\param chan	Channel
   \param number   E164 number with or without the leading +
	\param location	Number returned	(or SIP uri)
	\param maxloc	Max length of number
	\param technology 	Technology (not used in TXT records)
	\param maxtech	Max length
	\param txt	Text string (return value)
	\param maxtxt	Max length of "txt"
*/
extern int ast_get_txt(struct ast_channel *chan, const char *number, char *location, int maxloc, char *technology, int maxtech, char *txt, int maxtxt);

extern int ast_enum_init(void);
extern int ast_enum_reload(void);

#endif /* _ASTERISK_ENUM_H */
