/*
 * ENUM Support for Asterisk
 *
 * Copyright (C) 2003 Digium
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Funding provided by nic.at
 *
 * Distributed under the terms of the GNU GPL
 *
 */

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <errno.h>

#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/srv.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>

#define MAX_SIZE 4096

typedef struct {
	unsigned	id :16;		/* query identification number */
#if BYTE_ORDER == BIG_ENDIAN
			/* fields in third byte */
	unsigned	qr: 1;		/* response flag */
	unsigned	opcode: 4;	/* purpose of message */
	unsigned	aa: 1;		/* authoritive answer */
	unsigned	tc: 1;		/* truncated message */
	unsigned	rd: 1;		/* recursion desired */
			/* fields in fourth byte */
	unsigned	ra: 1;		/* recursion available */
	unsigned	unused :1;	/* unused bits (MBZ as of 4.9.3a3) */
	unsigned	ad: 1;		/* authentic data from named */
	unsigned	cd: 1;		/* checking disabled by resolver */
	unsigned	rcode :4;	/* response code */
#endif
#if BYTE_ORDER == LITTLE_ENDIAN || BYTE_ORDER == PDP_ENDIAN
			/* fields in third byte */
	unsigned	rd :1;		/* recursion desired */
	unsigned	tc :1;		/* truncated message */
	unsigned	aa :1;		/* authoritive answer */
	unsigned	opcode :4;	/* purpose of message */
	unsigned	qr :1;		/* response flag */
			/* fields in fourth byte */
	unsigned	rcode :4;	/* response code */
	unsigned	cd: 1;		/* checking disabled by resolver */
	unsigned	ad: 1;		/* authentic data from named */
	unsigned	unused :1;	/* unused bits (MBZ as of 4.9.3a3) */
	unsigned	ra :1;		/* recursion available */
#endif
			/* remaining bytes */
	unsigned	qdcount :16;	/* number of question entries */
	unsigned	ancount :16;	/* number of answer entries */
	unsigned	nscount :16;	/* number of authority entries */
	unsigned	arcount :16;	/* number of resource entries */
} dns_HEADER;

struct dn_answer {
	unsigned short rtype;
	unsigned short class;
	unsigned int ttl;
	unsigned short size;
} __attribute__ ((__packed__));

struct srv {
	unsigned short priority;
	unsigned short weight;
	unsigned short portnum;
} __attribute__ ((__packed__));

static int skip_name(unsigned char *s, int len)
{
	/* Shamelessly take from SER */
	int x = 0;
	while(x < len) {
		if (!*s) {
			s++;
			x++;
			break;
		}
		if (((*s) & 0xc0) == 0xc0) {
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

static int parse_ie(unsigned char *data, int maxdatalen, unsigned char *src, int srclen)
{
	int len, olen;
	len = olen = (int)src[0];
	src++;
	srclen--;
	if (len > srclen) {
		ast_log(LOG_WARNING, "Want %d, got %d\n", len, srclen);
		return -1;
	}
	if (len > maxdatalen)
		len = maxdatalen;
	memcpy(data, src, len);
	return olen + 1;
}

static int parse_srv(unsigned char *host, int hostlen, int *portno, unsigned char *answer, int len, unsigned char *msg)
{
	int res = 0;
	struct srv *srv = (struct srv *)answer;
	char repl[256] = "";

	if (len < sizeof(struct srv)) {
		printf("Length too short\n");
		return -1;
	}
	answer += sizeof(struct srv);
	len -= sizeof(struct srv);

	if ((res = dn_expand(msg,answer + len,answer, repl, sizeof(repl) - 1)) < 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	}
	if (res && strcmp(repl, ".")) {
		ast_verbose( VERBOSE_PREFIX_3 "parse_srv: SRV mapped to host %s, port %d\n", repl, ntohs(srv->portnum));
		if (host) {
			strncpy(host, repl, hostlen - 2);
			host[hostlen-1] = '\0';
		}
		if (portno)
			*portno = ntohs(srv->portnum);
		return(0);
	}
	return(-1);
}

static int parse_answer(unsigned char *host, int hostlen, int *port, char *answer, int len)
{
	/*
	 * This function is influenced by "ser" the SIP router.
	 */
	int x;
	int res;
	dns_HEADER *h;
	struct dn_answer *ans;
/*	int lastlit = 1; */
	char *oanswer = answer;
	host[0] = '\0';
	*port = 0;

#if 0
	for (x=0;x<len;x++) {
		if ((answer[x] < 32) || (answer[x] > 127)) {
			if (lastlit)
				printf("\"");
			printf(" 0x%02x", answer[x]);
			lastlit = 0;
		} else {
			if (!lastlit) 
				printf(" \"");
			printf("%c", answer[x]);
			lastlit = 1;
		}
	}
	printf("\n");
#endif	
	h = (dns_HEADER *)answer;
	/* Skip over DNS header */
	answer += sizeof(dns_HEADER);
	len -= sizeof(dns_HEADER);
#if 0
	printf("Query count: %d\n", ntohs(h->qdcount));
#endif
	for (x=0;x<ntohs(h->qdcount);x++) {
		if ((res = skip_name(answer, len)) < 0) {
			ast_log(LOG_WARNING, "Couldn't skip over name\n");
			return -1;
		}
		answer += res;
		len -= res;
		answer += 4; 	/* Skip QCODE / QCLASS */
		len -= 4;
		if (len < 0) {
			ast_log(LOG_WARNING, "Strange query size\n");
			return -1;
		}
	}
#if 0
	printf("Length remaining: %d, already at %04x\n", len, 0x2a + (answer - oanswer));
	printf("Answer count: %d\n", ntohs(h->ancount));
	printf("Looking for %d/%d\n", C_IN, T_SRV);
	printf("Answer so far: %02x %02x %02x %02x %02x %02x\n", answer[0], answer[1], answer[2], answer[3], answer[4], answer[5]);
#endif
	for (x=0;x<ntohs(h->ancount);x++) {
		if ((res = skip_name(answer, len)) < 0) {
			ast_log(LOG_WARNING, "Failed to skip name :(\n");
			return -1;
		}
		answer += res;
		len -= res;
		ans = (struct dn_answer *)answer;
		answer += sizeof(struct dn_answer);
		len -= sizeof(struct dn_answer);
		if (len < 0)
			return -1;
#if 0
		printf("Type: %d (%04x), class: %d (%04x), ttl: %d (%08x), length: %d (%04x)\n", ntohs(ans->rtype), ntohs(ans->rtype), ntohs(ans->class), ntohs(ans->class),
			ntohl(ans->ttl), ntohl(ans->ttl), ntohs(ans->size), ntohs(ans->size));
#endif			
		len -= ntohs(ans->size);
		if (len < 0) {
			ast_log(LOG_WARNING, "Length exceeds frame by %d\n", -len);
			return -1;
		}
		if ((ntohs(ans->class) == C_IN) && (ntohs(ans->rtype) == T_SRV)) {

			if (parse_srv(host, hostlen, port, answer, ntohs(ans->size),oanswer))
				ast_log(LOG_WARNING, "Failed to parse srv :(\n");

			if (strlen(host))
				return 0;
		}
		answer += ntohs(ans->size);
	}
	return 0;
}

int ast_get_srv(struct ast_channel *chan, char *host, int hostlen, int *port, const char *service)
{
	int res;
	int ret = -1;
	struct __res_state srvstate;
	char answer[MAX_SIZE];

	if (*port)
		*port = 0;
	res_ninit(&srvstate);	
	if (chan && ast_autoservice_start(chan) < 0)
		return -1;
	res = res_nsearch(&srvstate, service, C_IN, T_SRV, answer, sizeof(answer));
	if (res > 0) {
		if ((res = parse_answer(host, hostlen, port, answer, res))) {
			ast_log(LOG_WARNING, "Parse error returned %d\n", res);
			ret = 0;
		} else {
			ast_log(LOG_DEBUG, "Found host '%s', port '%d'\n", host, *port);
			ret = 1;
		}
	} else {
		ast_log(LOG_DEBUG, "No such service found: %s (%s)\n", service, strerror(errno));
		ret = 0;
	}
	if (chan)
		ret |= ast_autoservice_stop(chan);
	res_nclose(&srvstate);
	return ret;
}
