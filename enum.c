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
#include <ctype.h>
#include <regex.h>


#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/enum.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>

#define MAX_SIZE 4096

#define TOPLEV "e164.arpa."

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

static struct enum_search {
	char toplev[80];
	struct enum_search *next;
} *toplevs;

static int enumver = 0;

static ast_mutex_t enumlock = AST_MUTEX_INITIALIZER;

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

struct dn_answer {
	unsigned short rtype;
	unsigned short class;
	unsigned int ttl;
	unsigned short size;
} __attribute__ ((__packed__));

struct naptr {
	unsigned short order;
	unsigned short pref;
} __attribute__ ((__packed__));

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

static int parse_naptr(unsigned char *dst, int dstsize, char *tech, int techsize, unsigned char *answer, int len, char *naptrinput)
{
	unsigned char *oanswer = answer;
	unsigned char flags[80] = "";
	unsigned char services[80] = "";
	unsigned char regexp[80] = "";
	unsigned char repl[80] = "";
	unsigned char temp[80] = "";
	unsigned char delim;
	unsigned char *delim2;
	unsigned char *pattern, *subst, *d;
	int res;
	int regexp_len, size, backref;
	int d_len = sizeof(temp) - 1;
	regex_t preg;
	regmatch_t pmatch[9];

	
	if (len < sizeof(struct naptr)) {
		printf("Length too short\n");
		return -1;
	}
	answer += sizeof(struct naptr);
	len -= sizeof(struct naptr);
	if ((res = parse_ie(flags, sizeof(flags) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get flags\n");
		return -1; 
	} else { answer += res; len -= res; }
	if ((res = parse_ie(services, sizeof(services) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get services\n");
		return -1; 
	} else { answer += res; len -= res; }
	if ((res = parse_ie(regexp, sizeof(regexp) - 1, answer, len)) < 0)
		return -1; else { answer += res; len -= res; }
	if ((res = dn_expand(oanswer,answer + len,answer, repl, sizeof(repl) - 1)) < 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	} 

#if 0
	printf("Input: %s\n", naptrinput);
	printf("Flags: %s\n", flags);
	printf("Services: %s\n", services);
	printf("Regexp: %s\n", regexp);
	printf("Repl: %s\n", repl);
#endif

	if (tolower(flags[0]) != 'u') {
		ast_log(LOG_WARNING, "Flag must be 'U' or 'u'.\n");
		return -1;
	}

	if ((!strncasecmp(services, "e2u+sip", 7)) || 
	    (!strncasecmp(services, "sip+e2u", 7))) {
		strncpy(tech, "sip", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+h323", 7)) || 
	    (!strncasecmp(services, "h323+e2u", 7))) {
		strncpy(tech, "h323", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+iax", 7)) || 
	    (!strncasecmp(services, "iax+e2u", 7))) {
		strncpy(tech, "iax", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+iax2", 7)) || 
	    (!strncasecmp(services, "iax2+e2u", 7))) {
		strncpy(tech, "iax2", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+tel", 7)) || 
	    (!strncasecmp(services, "tel+e2u", 7))) {
		strncpy(tech, "tel", techsize -1); 
	} else if (strncasecmp(services, "e2u+voice:", 10)) {
		ast_log(LOG_WARNING, "Services must be e2u+sip, sip+e2u, e2u+h323, h323+e2u, e2u+iax, iax+e2u, e2u+iax2, iax2+e2u, e2u+tel, tel+e2u or e2u+voice:\n");
		return -1;
	}

	/* DEDBUGGING STUB
	strcpy(regexp, "!^\\+43(.*)$!\\1@bla.fasel!");
	*/

	regexp_len = strlen(regexp);
	if (regexp_len < 7) {
		ast_log(LOG_WARNING, "Regex too short to be meaningful.\n");
		return -1;
	} 


	delim = regexp[0];
	delim2 = strchr(regexp + 1, delim);
	if ((delim2 == NULL) || (regexp[regexp_len-1] != delim)) {
		ast_log(LOG_WARNING, "Regex delimiter error (on \"%s\").\n",regexp);
		return -1;
	}

	pattern = regexp + 1;
	*delim2 = 0;
	subst   = delim2 + 1;
	regexp[regexp_len-1] = 0;

#if 0
	printf("Pattern: %s\n", pattern);
	printf("Subst: %s\n", subst);
#endif

/*
 * now do the regex wizardry.
 */

	if (regcomp(&preg, pattern, REG_EXTENDED | REG_NEWLINE)) {
		ast_log(LOG_WARNING, "Regex compilation error (regex = \"%s\").\n",regexp);
		return -1;
	}

	if (preg.re_nsub > 9) {
		ast_log(LOG_WARNING, "Regex compilation error: too many subs.\n");
		regfree(&preg);
		return -1;
	}

	if (regexec(&preg, naptrinput, 9, pmatch, 0)) {
		ast_log(LOG_WARNING, "Regex match failed.\n");
		regfree(&preg);
		return -1;
	}
	regfree(&preg);

	d = temp; d_len--; 
	while( *subst && (d_len > 0) ) {
		if ((subst[0] == '\\') && isdigit(subst[1]) && (pmatch[subst[1]-'0'].rm_so != -1)) {
			backref = subst[1]-'0';
			size = pmatch[backref].rm_eo - pmatch[backref].rm_so;
			if (size > d_len) {
				ast_log(LOG_WARNING, "Not enough space during regex substitution.\n");
				return -1;
				}
			memcpy(d, naptrinput + pmatch[backref].rm_so, size);
			d += size;
			d_len -= size;
			subst += 2;
		} else if (isprint(*subst)) {
			*d++ = *subst++;
			d_len--;
		} else {
			ast_log(LOG_WARNING, "Error during regex substitution.\n");
			return -1;
		}
	}
	*d = 0;
	strncpy(dst, temp, dstsize);
	d = strchr(services, ':');
	if (d) 
		strncpy(tech, d+1, techsize -1); 
	return 0;
}

static int parse_answer(unsigned char *dst, int dstlen, unsigned char *tech, int techlen, unsigned char *answer, int len, char *naptrinput)
{
	/*
	 * This function is influenced by "ser" the SIP router.
	 */
	int x;
	int res;
	dns_HEADER *h;
	struct dn_answer *ans;
	dst[0] = '\0';
	tech[0] = '\0';
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
	printf("Length remaining: %d\n", len);
	printf("Answer count: %d\n", ntohs(h->ancount));
	printf("Looking for %d/%d\n", C_IN, T_NAPTR);
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
		printf("Type: %d, class: %d, ttl: %d, length: %d\n", ntohs(ans->rtype), ntohs(ans->class),
			ntohl(ans->ttl), ntohs(ans->size));
#endif			
		len -= ntohs(ans->size);
		if (len < 0) {
			ast_log(LOG_WARNING, "Length exceeds frame\n");
			return -1;
		}
		if ((ntohs(ans->class) == C_IN) && (ntohs(ans->rtype) == T_NAPTR)) {
			if (parse_naptr(dst, dstlen, tech, techlen, answer, ntohs(ans->size), naptrinput))
				ast_log(LOG_WARNING, "Failed to parse naptr :(\n");
			if (strlen(dst))
				return 0;
		}
		answer += ntohs(ans->size);
	}
	return 0;
}

int ast_get_enum(struct ast_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen)
{
	unsigned char answer[MAX_SIZE];
	char tmp[259 + 80];
	char naptrinput[80] = "+";
	int pos = strlen(number) - 1;
	int newpos=0;
	int res = -1;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;
	struct __res_state enumstate;
	res_ninit(&enumstate);	
	if (chan && ast_autoservice_start(chan) < 0)
		return -1;

	strncat(naptrinput, number, sizeof(naptrinput) - 2);

	if (pos > 128)
		pos = 128;
	while(pos >= 0) {
		tmp[newpos++] = number[pos--];
		tmp[newpos++] = '.';
	}
#if 0
	printf("Looking for '%s'\n", tmp);
#endif	
	
	for(;;) {
		ast_mutex_lock(&enumlock);
		if (version != enumver) {
			/* Ooh, a reload... */
			s = toplevs;
			version = enumver;
		} else {
			s = s->next;
		}
		if (s) {
			strcpy(tmp + newpos, s->toplev);
		}
		ast_mutex_unlock(&enumlock);
		if (!s)
			break;
		res = res_nsearch(&enumstate, tmp, C_IN, T_NAPTR, answer, sizeof(answer));
		if (res > 0)
			break;
	}
	if (res > 0) {
		if ((res = parse_answer(dst, dstlen, tech, techlen, answer, res, naptrinput))) {
			ast_log(LOG_WARNING, "Parse error returned %d\n", res);
			ret = 0;
		} else {
			ast_log(LOG_DEBUG, "Found technology '%s', destination '%s'\n", tech, dst);
			ret = 1;
		}
	} else {
		ast_log(LOG_DEBUG, "No such number found: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	}
	if (chan)
		ret |= ast_autoservice_stop(chan);
	res_nclose(&enumstate);
	return ret;
}

static struct enum_search *enum_newtoplev(char *s)
{
	struct enum_search *tmp;
	tmp = malloc(sizeof(struct enum_search));
	if (tmp) {
		memset(tmp, 0, sizeof(struct enum_search));
		strncpy(tmp->toplev, s, sizeof(tmp->toplev) - 1);
	}
	return tmp;
}

int ast_enum_init(void)
{
	struct ast_config *cfg;
	struct enum_search *s, *sl;
	struct ast_variable *v;

	/* Destroy existing list */
	ast_mutex_lock(&enumlock);
	s = toplevs;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
	toplevs = NULL;
	cfg = ast_load("enum.conf");
	if (cfg) {
		sl = NULL;
		v = ast_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "search")) {
				s = enum_newtoplev(v->value);
				if (s) {
					if (sl)
						sl->next = s;
					else
						toplevs = s;
					sl = s;
				}
			}
			v = v->next;
		}
		ast_destroy(cfg);
	} else {
		toplevs = enum_newtoplev(TOPLEV);
	}
	enumver++;
	ast_mutex_unlock(&enumlock);
	return 0;
}

int ast_enum_reload(void)
{
	return ast_enum_init();
}
