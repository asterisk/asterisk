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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/network.h"
#include <arpa/nameser.h>	/* res_* functions */
#include <resolv.h>

#include "asterisk/channel.h"
#include "asterisk/dns.h"
#include "asterisk/endian.h"

/*! \brief The maximum size permitted for the answer from the DNS server */
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

#ifndef HAVE_RES_NINIT
AST_MUTEX_DEFINE_STATIC(res_lock);
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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
	unsigned	aa:1;           /*!< authoritative answer */
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
	unsigned	aa:1;           /*!< authoritative answer */
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
	unsigned short rtype;       /*!< The resource record type. */
	unsigned short class;       /*!< The resource record class. */
	unsigned int ttl;           /*!< The resource record time to live. */
	unsigned short size;        /*!< The resource record size. */
} __attribute__((__packed__));

/*!
 * \brief Tries to find the position of the next field in the DNS response.
 *
 * \internal
 *
 * \param  s    A char pointer to the current frame in the DNS response.
 * \param  len  The remaining available length of the DNS response.
 *
 * \retval The position of the next field
 * \retval -1 if there are no remaining fields
 */
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

	/* If we are out of room to search, return failure. */
	if (x >= len) {
		return AST_DNS_SEARCH_FAILURE;
	}

	/* Return the value for the current position in the DNS response. This is the start
	position of the next field. */
	return x;
}

/*!
 * \brief Advances the position of the DNS response pointer by the size of the current field.
 *
 * \internal
 *
 * \param  dns_response   A pointer to a char pointer to the current field in the DNS response.
 * \param  remaining_len  The remaining available length in the DNS response to search.
 * \param  field_size     A positive value representing the size of the current field
                          pointed to by the dns_response parameter.
 *
 * \retval The remaining length in the DNS response
 * \retval -1 there are no frames remaining in the DNS response
 */
static int dns_advance_field(unsigned char **dns_response, int remaining_len, int field_size)
{
	if (dns_response == NULL || field_size < 0 || remaining_len < field_size) {
		return AST_DNS_SEARCH_FAILURE;
	}

	*dns_response += field_size;
	remaining_len -= field_size;

	return remaining_len;
}

#ifndef HAVE_RES_NINIT
/*!
 * \brief Handles the DNS search if the system has RES_INIT.
 *
 * \internal
 *
 * \param  dname             Domain name to lookup (host, SRV domain, TXT record name).
 * \param  rr_class          Record Class (see "man res_search").
 * \param  rr_type           Record type (see "man res_search").
 * \param  dns_response      The full DNS response.
 * \param  dns_response_len  The length of the full DNS response.
 *
 * \retval The length of the DNS response
 * \retval -1 on search failure
 */
static int dns_search_res(const char *dname, int rr_class, int rr_type,
	unsigned char *dns_response, int dns_response_len)
{

	int ret = AST_DNS_SEARCH_FAILURE;

	ast_mutex_lock(&res_lock);
	res_init();
	ret = res_search(dname,
	                 rr_class,
	                 rr_type,
	                 dns_response,
	                 dns_response_len);

#ifdef HAVE_RES_CLOSE
	res_close();
#endif

	ast_mutex_unlock(&res_lock);

	return ret;
}
#else
/*!
 * \brief Handles the DNS search if the system has RES_NINIT.
 *
 * \internal
 *
 * \param  dname             Domain name to lookup (host, SRV domain, TXT record name).
 * \param  rr_class          Record Class (see "man res_search").
 * \param  rr_type           Record type (see "man res_search").
 * \param  dns_response      The full DNS response.
 * \param  dns_response_len  The length of the full DNS response.
 *
 * \retval The length of the DNS response
 * \retval -1 on search failure
 */
static int dns_search_res(const char *dname, int rr_class, int rr_type,
	unsigned char *dns_response, int dns_response_len)
{

	int ret = AST_DNS_SEARCH_FAILURE;
	struct __res_state dns_state;

	memset(&dns_state, 0, sizeof(dns_state));
	res_ninit(&dns_state);
	ret = res_nsearch(&dns_state,
	                  dname,
	                  rr_class,
	                  rr_type,
	                  dns_response,
	                  dns_response_len);

#ifdef HAVE_RES_NDESTROY
	res_ndestroy(&dns_state);
#else
	res_nclose(&dns_state);
#endif

	return ret;
}
#endif

/*!
 * \brief Parse DNS lookup result, call callback
 *
 * \internal
 *
 * \param  context   Void pointer containing data to use in the callback functions.
 * \param  dname     Domain name to lookup (host, SRV domain, TXT record name).
 * \param  class     Record Class (see "man res_search").
 * \param  type      Record type (see "man res_search").
 * \param  answer    The full DNS response.
 * \param  len       The length of the full DNS response.
 * \param  callback  Callback function for handling the discovered resource records from the DNS search.
 *
 * \retval -1 on search failure
 * \retval  0 on no records found
 * \retval  1 on success
 */
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
			ast_log(LOG_WARNING, "Length of DNS answer exceeds frame\n");
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

/*!
 * \brief Extended version of the DNS Parsing function.
 *
 * \details Parses the DNS lookup result and notifies the observer of each discovered
 *          resource record with the provided callback.
 *
 * \internal
 *
 * \param  context           Void pointer containing data to use in the callback functions.
 * \param  dname             Domain name to lookup (host, SRV domain, TXT record name).
 * \param  rr_class          Record Class (see "man res_search").
 * \param  rr_type           Record type (see "man res_search").
 * \param  answer            The full DNS response.
 * \param  answer_len        The length of the full DNS response.
 * \param  response_handler  Callback function for handling the DNS response.
 * \param  record_handler    Callback function for handling the discovered resource records from the DNS search.
 *
 * \retval -1 on search failure
 * \retval  0 on no records found
 * \retval  1 on success
 */
static int dns_parse_answer_ex(void *context, int rr_class, int rr_type, unsigned char *answer, int answer_len,
	int (*response_handler)(void *context, unsigned char *dns_response, int dns_response_len, int rcode),
	int (*record_handler)(void *context, unsigned char *record, int record_len, int ttl))
{
	unsigned char *dns_response = answer;
	dns_HEADER *dns_header = (dns_HEADER *)answer;

	struct dn_answer *ans;
	int res, x, pos, dns_response_len, ret;

	dns_response_len = answer_len;
	ret = AST_DNS_SEARCH_NO_RECORDS;

	/* Invoke the response_handler callback to notify the observer of the raw DNS response */
	response_handler(context, dns_response, dns_response_len, ntohs(dns_header->rcode));

	/* Verify there is something to parse */
	if (answer_len == 0) {
		return ret;
	}

	/* Try advancing the cursor for the dns header */
	if ((pos = dns_advance_field(&answer, answer_len, sizeof(dns_HEADER))) < 0) {
		ast_log(LOG_WARNING, "Length of DNS answer exceeds available search frames\n");
		return AST_DNS_SEARCH_FAILURE;
	}

	/* Skip domain name and QCODE / QCLASS */
	for (x = 0; x < ntohs(dns_header->qdcount); x++) {
		if ((res = skip_name(answer, pos)) < 0) {
			ast_log(LOG_WARNING, "Failed skipping name\n");
			return AST_DNS_SEARCH_FAILURE;
		}

		/* Try advancing the cursor for the name and QCODE / QCLASS fields */
		if ((pos = dns_advance_field(&answer, pos, res + 4)) < 0) {
			return AST_DNS_SEARCH_FAILURE;
		}
	}

	/* Extract the individual records */
	for (x = 0; x < ntohs(dns_header->ancount); x++) {
		if ((res = skip_name(answer, pos)) < 0) {
			ast_log(LOG_WARNING, "Failed skipping name\n");
			return AST_DNS_SEARCH_FAILURE;
		}

		/* Try advancing the cursor to the current record */
		if ((pos = dns_advance_field(&answer, pos, res)) < 0) {
			ast_log(LOG_WARNING, "Length of DNS answer exceeds available search frames\n");
			return AST_DNS_SEARCH_FAILURE;
		}

		/* Cast the current value for the answer pointer as a dn_answer struct */
		ans = (struct dn_answer *) answer;

		/* Try advancing the cursor to the end of the current record  */
		if ((pos = dns_advance_field(&answer, pos, sizeof(struct dn_answer)))  < 0) {
			ast_log(LOG_WARNING, "Length of DNS answer exceeds available search frames\n");
			return AST_DNS_SEARCH_FAILURE;
		}

		/* Skip over the records that do not have the same resource record class and type we care about */
		if (ntohs(ans->class) == rr_class && ntohs(ans->rtype) == rr_type) {
			/* Invoke the record handler callback to deliver the discovered record */
			record_handler(context, answer, ntohs(ans->size), ntohl(ans->ttl));
			/*At least one record was found */
			ret = AST_DNS_SEARCH_SUCCESS;
		}

		/* Try and update the field to the next record, but ignore any errors that come
		 * back because this may be the end of the line. */
		pos = dns_advance_field(&answer, pos, ntohs(ans->size));
	}

	return ret;
}

/*!
 * \brief Lookup record in DNS
 *
 * \note Asterisk DNS is synchronus at this time. This means that if your DNS does not
 *       work properly, Asterisk might not start properly or a channel may lock.
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

enum ast_dns_search_result ast_search_dns_ex(void *context, const char *dname, int rr_class, int rr_type,
	   int (*response_handler)(void *context, unsigned char *dns_response, int dns_response_len, int rcode),
	   int (*record_handler)(void *context, unsigned char *record, int record_len, int ttl))
{
	int ret, dns_response_len;
	unsigned char dns_response[MAX_SIZE];

	/* Assert that the callbacks are not NULL */
	ast_assert(response_handler != NULL);
	ast_assert(record_handler != NULL);

	/* Try the DNS search. */
	dns_response_len = dns_search_res(dname,
	                                  rr_class,
	                                  rr_type,
	                                  dns_response,
	                                  sizeof(dns_response));

	if (dns_response_len < 0) {
		ast_debug(1, "DNS search failed for %s\n", dname);
		response_handler(context, (unsigned char *)"", 0, NXDOMAIN);
		return AST_DNS_SEARCH_FAILURE;
	}

	/* Parse records from DNS response */
	ret = dns_parse_answer_ex(context,
	                          rr_class,
	                          rr_type,
	                          dns_response,
	                          dns_response_len,
	                          response_handler,
	                          record_handler);

	/* Handle the return code from parsing the DNS response */
	if (ret == AST_DNS_SEARCH_FAILURE) {
		/* Parsing Error */
		ast_log(LOG_WARNING, "DNS Parse error for %s\n", dname);
	} else if (ret == AST_DNS_SEARCH_NO_RECORDS) {
		/* No results found */
		ast_debug(1, "DNS search yielded no results for %s\n", dname);
	}

	return ret;
}

struct ao2_container *ast_dns_get_nameservers(void)
{
#ifdef HAVE_RES_NINIT
	struct __res_state dnsstate;
#endif
	struct __res_state *state;
	struct ao2_container *nameservers;
	int i;

	nameservers = ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 3);
	if (!nameservers) {
		return NULL;
	}

#ifdef HAVE_RES_NINIT
	memset(&dnsstate, 0, sizeof(dnsstate));
	res_ninit(&dnsstate);
	state = &dnsstate;
#else
	ast_mutex_lock(&res_lock);
	res_init();
	state = &_res;
#endif

	for (i = 0; i < state->nscount; i++) {
		char addr[INET6_ADDRSTRLEN];
		const char *addrp = NULL;

		/* glibc sets sin_family to 0 when the nameserver is an IPv6 address */
		if (state->nsaddr_list[i].sin_family) {
			addrp = inet_ntop(AF_INET, &state->nsaddr_list[i].sin_addr, addr, sizeof(addr));
#if defined(HAVE_RES_NINIT) && defined(HAVE_STRUCT___RES_STATE__U__EXT_NSADDRS)
		} else if (state->_u._ext.nsaddrs[i]) {
			addrp = inet_ntop(AF_INET6, &state->_u._ext.nsaddrs[i]->sin6_addr, addr, sizeof(addr));
#endif
		}

		if (addrp) {
			ast_debug(1, "Discovered nameserver: %s\n", addrp);
			ast_str_container_add(nameservers, addrp);
		}
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

	return nameservers;
}
