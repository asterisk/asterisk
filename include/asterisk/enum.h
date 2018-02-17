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

struct naptr {
	unsigned short order;
	unsigned short pref;
} __attribute__((__packed__));

struct enum_naptr_rr {
	struct naptr naptr; /*!< order and preference of RR */
	char *result;       /*!< result of naptr parsing,e.g.: tel:+5553 */
	char *tech;         /*!< Technology (from URL scheme) */
	int sort_pos;       /*!< sort position */
};

struct enum_context {
	char *dst;                       /*!< Destination part of URL from ENUM */
	int dstlen;                      /*!< Length */
	char *tech;                      /*!< Technology (from URL scheme) */
	int techlen;                     /*!< Length */
	char *txt;                       /*!< TXT record in TXT lookup */
	int txtlen;                      /*!< Length */
	char *naptrinput;                /*!< The number to lookup */
	int position;                    /*!< specifies position of required RR */
	int count;                       /*!< used as counter for RRs */
	int options;                     /*!< options , see ENUMLOOKUP_OPTIONS_* defined above */
	struct enum_naptr_rr *naptr_rrs; /*!< array of parsed NAPTR RRs */
	int naptr_rrs_count;             /*!< Size of array naptr_rrs */
};


/*! \brief Lookup entry in ENUM
	\param chan	Channel
	\param number   E164 number with or without the leading +
	\param location Number returned	(or SIP uri)
	\param maxloc	Max length
	\param technology Technology (from url scheme in response)
                       You can set it to get particular answer RR, if there are many techs in DNS response, example: "sip"
                       If you need any record, then set it to "ALL" string
	\param maxtech  Max length
	\param suffix   Zone suffix (WARNING: No defaults here any more)
	\param options  Options
				'c'    - Count number of NAPTR RR
				number - Position of the requested RR in the answer list
				'u'    - Full URI return (does not strip URI scheme)
				'i'    - Infrastructure ENUM lookup
				's'    - ISN based lookup
				'd'    - Direct DNS query
	\param record   The position of required RR in the answer list
	\param argcontext   Argument for caching results into an enum_context pointer (NULL is used for not caching)
	\retval 1 if found
	\retval 0 if not found
	\retval -1 on hangup
*/
int ast_get_enum(struct ast_channel *chan, const char *number, char *location, int maxloc, char *technology,
		int maxtech, char* suffix, char* options, unsigned int record, struct enum_context **argcontext);

/*!	\brief Lookup DNS TXT record (used by app TXTCIDnum)
 *
 * Really has nothing to do with enum, but anyway...
 * Actually, there is now an internet-draft which describes how callerID should
 * be stored in ENUM domains: draft-ietf-enum-cnam-04.txt
 * The algorithm implemented here will thus be obsolete soon.
 *
 * \param chan	Channel
 * \param number   E164 number with or without the leading +
 * \param txt	Text string (return value)
 * \param maxtxt	Max length of "txt"
 * \param suffix	Zone suffix
 * \version 1.6.1 new suffix parameter to take into account caller ids that aren't in e164.arpa
 * \version 1.6.1 removed parameters location, maxloc, technology, maxtech as all the information
 * is stored the txt string
 */
int ast_get_txt(struct ast_channel *chan, const char *number, char *txt, int maxtxt, char *suffix);

#endif /* _ASTERISK_ENUM_H */
