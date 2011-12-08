/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006 Thorsten Lockert
 *
 * Written by Thorsten Lockert <tholo@trollphone.org>
 *
 * Funding provided by Troll Phone Networks AS
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
 * \brief DNS Support for Asterisk
 *
 * \author Thorsten Lockert <tholo@trollphone.org>
 *
 * \par Reference
 * - DNR SRV records http://www.ietf.org/rfc/rfc2782.txt
 *
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/network.h"
#include <arpa/nameser.h>	/* res_* functions */
#include <resolv.h>

#include "asterisk/channel.h"
#include "asterisk/dns.h"
#include "asterisk/endian.h"

#define MAX_SIZE 4096

#ifdef __PDP_ENDIAN
#if __BYTE_ORDER == __PDP_ENDIAN
#define DETERMINED_BYTE_ORDER __LITTLE_ENDIAN
#endif
#endif
#if __BYTE_ORDER == __BIG_ENDIAN
#define DETERMINED_BYTE_ORDER __BIG_ENDIAN
#endif
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define DETERMINED_BYTE_ORDER __LITTLE_ENDIAN
#endif

/* The dns_HEADER structure definition below originated
   in the arpa/nameser.h header file distributed with ISC
   BIND, which contains the following copyright and license
   notices:

 * ++Copyright++ 1983, 1989, 1993
 * -
 * Copyright (c) 1983, 1989, 1993
 *    The Regents of the University of California.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * 	This product includes software developed by the University of
 * 	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

typedef struct {
	unsigned	id:16;          /*!< query identification number */
#if DETERMINED_BYTE_ORDER == __BIG_ENDIAN
			/* fields in third byte */
	unsigned	qr:1;           /*!< response flag */
	unsigned	opcode:4;       /*!< purpose of message */
	unsigned	aa:1;           /*!< authoritive answer */
	unsigned	tc:1;           /*!< truncated message */
	unsigned	rd:1;           /*!< recursion desired */
			/* fields in fourth byte */
	unsigned	ra:1;           /*!< recursion available */
	unsigned	unused:1;       /*!< unused bits (MBZ as of 4.9.3a3) */
	unsigned	ad:1;           /*!< authentic data from named */
	unsigned	cd:1;           /*!< checking disabled by resolver */
	unsigned	rcode:4;        /*!< response code */
#endif
#if DETERMINED_BYTE_ORDER == __LITTLE_ENDIAN
			/* fields in third byte */
	unsigned	rd:1;           /*!< recursion desired */
	unsigned	tc:1;           /*!< truncated message */
	unsigned	aa:1;           /*!< authoritive answer */
	unsigned	opcode:4;       /*!< purpose of message */
	unsigned	qr:1;           /*!< response flag */
			/* fields in fourth byte */
	unsigned	rcode:4;        /*!< response code */
	unsigned	cd:1;           /*!< checking disabled by resolver */
	unsigned	ad:1;           /*!< authentic data from named */
	unsigned	unused:1;       /*!< unused bits (MBZ as of 4.9.3a3) */
	unsigned	ra:1;           /*!< recursion available */
#endif
			/* remaining bytes */
	unsigned	qdcount:16;     /*!< number of question entries */
	unsigned	ancount:16;     /*!< number of answer entries */
	unsigned	nscount:16;     /*!< number of authority entries */
	unsigned	arcount:16;     /*!< number of resource entries */
} dns_HEADER;

struct dn_answer {
	unsigned short rtype;
	unsigned short class;
	unsigned int ttl;
	unsigned short size;
} __attribute__((__packed__));

static int skip_name(unsigned char *s, int len)
{
	int x = 0;

	while (x < len) {
		if (*s == '\0') {
			s++;
			x++;
			break;
		}
		if ((*s & 0xc0) == 0xc0) {
			s += 2;
			x += 2;
			break;
		}
		x += *s + 1;
		s += *s + 1;
	}
	if (x >= len)
		return -1;
	return x;
}

/*! \brief Parse DNS lookup result, call callback */
static int dns_parse_answer(void *context,
	int class, int type, unsigned char *answer, int len,
	int (*callback)(void *context, unsigned char *answer, int len, unsigned char *fullanswer))
{
	unsigned char *fullanswer = answer;
	struct dn_answer *ans;
	dns_HEADER *h;
	int ret = 0;
	int res;
	int x;

	h = (dns_HEADER *)answer;
	answer += sizeof(dns_HEADER);
	len -= sizeof(dns_HEADER);

	for (x = 0; x < ntohs(h->qdcount); x++) {
		if ((res = skip_name(answer, len)) < 0) {
			ast_log(LOG_WARNING, "Couldn't skip over name\n");
			return -1;
		}
		answer += res + 4;	/* Skip name and QCODE / QCLASS */
		len -= res + 4;
		if (len < 0) {
			ast_log(LOG_WARNING, "Strange query size\n");
			return -1;
		}
	}

	for (x = 0; x < ntohs(h->ancount); x++) {
		if ((res = skip_name(answer, len)) < 0) {
			ast_log(LOG_WARNING, "Failed skipping name\n");
			return -1;
		}
		answer += res;
		len -= res;
		ans = (struct dn_answer *)answer;
		answer += sizeof(struct dn_answer);
		len -= sizeof(struct dn_answer);
		if (len < 0) {
			ast_log(LOG_WARNING, "Strange result size\n");
			return -1;
		}
		if (len < 0) {
			ast_log(LOG_WARNING, "Length exceeds frame\n");
			return -1;
		}

		if (ntohs(ans->class) == class && ntohs(ans->rtype) == type) {
			if (callback) {
				if ((res = callback(context, answer, ntohs(ans->size), fullanswer)) < 0) {
					ast_log(LOG_WARNING, "Failed to parse result\n");
					return -1;
				}
				ret = 1;
			}
		}
		answer += ntohs(ans->size);
		len -= ntohs(ans->size);
	}
	return ret;
}

#ifndef HAVE_RES_NINIT
AST_MUTEX_DEFINE_STATIC(res_lock);
#endif

/*! \brief Lookup record in DNS 
\note Asterisk DNS is synchronus at this time. This means that if your DNS does
not work properly, Asterisk might not start properly or a channel may lock.
*/
int ast_search_dns(void *context,
	   const char *dname, int class, int type,
	   int (*callback)(void *context, unsigned char *answer, int len, unsigned char *fullanswer))
{
#ifdef HAVE_RES_NINIT
	struct __res_state dnsstate;
#endif
	unsigned char answer[MAX_SIZE];
	int res, ret = -1;

#ifdef HAVE_RES_NINIT
	memset(&dnsstate, 0, sizeof(dnsstate));
	res_ninit(&dnsstate);
	res = res_nsearch(&dnsstate, dname, class, type, answer, sizeof(answer));
#else
	ast_mutex_lock(&res_lock);
	res_init();
	res = res_search(dname, class, type, answer, sizeof(answer));
#endif
	if (res > 0) {
		if ((res = dns_parse_answer(context, class, type, answer, res, callback)) < 0) {
			ast_log(LOG_WARNING, "DNS Parse error for %s\n", dname);
			ret = -1;
		} else if (res == 0) {
			ast_debug(1, "No matches found in DNS for %s\n", dname);
			ret = 0;
		} else
			ret = 1;
	}
#ifdef HAVE_RES_NINIT
#ifdef HAVE_RES_NDESTROY
	res_ndestroy(&dnsstate);
#else
	res_nclose(&dnsstate);
#endif
#else
#ifdef HAVE_RES_CLOSE
	res_close();
#endif
	ast_mutex_unlock(&res_lock);
#endif

	return ret;
}
