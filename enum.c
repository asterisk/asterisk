/*
 * ENUM Support for Asterisk
 *
 * Copyright (C) 2003-2005, Digium, inc
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Funding provided by nic.at
 *
 * Distributed under the terms of the GNU GPL
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#if __APPLE_CC__ >= 1495
#include <arpa/nameser_compat.h>
#endif
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <unistd.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/enum.h"
#include "asterisk/dns.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"

#ifdef __APPLE__
#undef T_NAPTR
#define T_NAPTR 35
#endif

#ifdef __APPLE__
#undef T_TXT
#define T_TXT 16
#endif

/* The IETF Enum standard root, managed by the ITU */
#define TOPLEV "e164.arpa."

/* Linked list from config file */
static struct enum_search {
	char toplev[80];
	struct enum_search *next;
} *toplevs;

static int enumver = 0;

AST_MUTEX_DEFINE_STATIC(enumlock);

struct naptr {
	unsigned short order;
	unsigned short pref;
} __attribute__ ((__packed__));

/*--- parse_ie: Parse NAPTR record information elements */
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

/*--- parse_naptr: Parse DNS NAPTR record used in ENUM ---*/
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


	dst[0] = '\0';
	
	if (len < sizeof(struct naptr)) {
		ast_log(LOG_WARNING, "NAPTR record length too short\n");
		return -1;
	}
	answer += sizeof(struct naptr);
	len -= sizeof(struct naptr);
	if ((res = parse_ie(flags, sizeof(flags) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get flags from NAPTR record\n");
		return -1; 
	} else { 
		answer += res; 
		len -= res; 
	}
	if ((res = parse_ie(services, sizeof(services) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get services from NAPTR record\n");
		return -1; 
	} else { 
		answer += res; 
		len -= res; 
	}
	if ((res = parse_ie(regexp, sizeof(regexp) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get regexp from NAPTR record\n");
		return -1; 
	} else { 
		answer += res; 
		len -= res; 
	}
	if ((res = dn_expand(oanswer,answer + len,answer, repl, sizeof(repl) - 1)) < 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	} 

	if (option_debug > 2)	/* Advanced NAPTR debugging */
		ast_log(LOG_DEBUG, "NAPTR input='%s', flags='%s', services='%s', regexp='%s', repl='%s'\n",
			naptrinput, flags, services, regexp, repl);

	if (tolower(flags[0]) != 'u') {
		ast_log(LOG_WARNING, "NAPTR Flag must be 'U' or 'u'.\n");
		return -1;
	}

	if ((!strncasecmp(services, "e2u+sip", 7)) || 
	    (!strncasecmp(services, "sip+e2u", 7))) {
		strncpy(tech, "sip", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+h323", 8)) || 
	    (!strncasecmp(services, "h323+e2u", 8))) {
		strncpy(tech, "h323", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+x-iax2", 10)) || 
	    (!strncasecmp(services, "e2u+iax2", 8)) ||
	    (!strncasecmp(services, "iax2+e2u", 8))) {
		strncpy(tech, "iax2", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+x-iax", 9)) ||
	    (!strncasecmp(services, "e2u+iax", 7)) ||
	    (!strncasecmp(services, "iax+e2u", 7))) {
		strncpy(tech, "iax", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+tel", 7)) || 
	    (!strncasecmp(services, "tel+e2u", 7))) {
		strncpy(tech, "tel", techsize -1); 
	} else if (!strncasecmp(services, "e2u+voice:", 10)) {
		strncpy(tech, services+10, techsize -1); 
	} else {
		ast_log(LOG_DEBUG, 
		"Services must be e2u+${tech}, ${tech}+e2u, or e2u+voice: where $tech is from (sip, h323, tel, iax, iax2). \n");
		return 0;
	}

	/* DEDBUGGING STUB
	strncpy(regexp, "!^\\+43(.*)$!\\1@bla.fasel!", sizeof(regexp) - 1);
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
		ast_log(LOG_WARNING, "NAPTR Regex compilation error (regex = \"%s\").\n",regexp);
		return -1;
	}

	if (preg.re_nsub > 9) {
		ast_log(LOG_WARNING, "NAPTR Regex compilation error: too many subs.\n");
		regfree(&preg);
		return -1;
	}

	if (regexec(&preg, naptrinput, 9, pmatch, 0)) {
		ast_log(LOG_WARNING, "NAPTR Regex match failed.\n");
		regfree(&preg);
		return -1;
	}
	regfree(&preg);

	d = temp; 
	d_len--; 
	while( *subst && (d_len > 0) ) {
		if ((subst[0] == '\\') && isdigit(subst[1]) && (pmatch[subst[1]-'0'].rm_so != -1)) {
			backref = subst[1]-'0';
			size = pmatch[backref].rm_eo - pmatch[backref].rm_so;
			if (size > d_len) {
				ast_log(LOG_WARNING, "Not enough space during NAPTR regex substitution.\n");
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
	strncpy(dst, temp, dstsize - 1);
	dst[dstsize - 1] = '\0';
	return 0;
}

struct enum_context {
	char *dst;	/* Destination part of URL from ENUM */
	int dstlen;	/* Length */
	char *tech;	/* Technology (from URL scheme) */
	int techlen;	/* Length */
	char *txt;	/* TXT record in TXT lookup */
	int txtlen;	/* Length */
	char *naptrinput;	/* The number to lookup */
};

/*--- txt_callback: Callback for TXT record lookup */
static int txt_callback(void *context, u_char *answer, int len, u_char *fullanswer)
{
	struct enum_context *c = (struct enum_context *)context;
#if 0
	printf("ENUMTXT Called\n");
#endif

	if (answer == NULL) {
		c->txt = NULL;
		c->txtlen = 0;
		return 0;
	}

	/* skip over first byte, as for some reason it's a vertical tab character */
	answer += 1;
	len -= 1;

	/* answer is not null-terminated, but should be */
	/* this is safe to do, as answer has extra bytes on the end we can 
           safely overwrite with a null */
	answer[len] = '\0';
	/* now increment len so that len includes the null, so that we can
	   compare apples to apples */
	len +=1;

	/* finally, copy the answer into c->txt */
	strncpy(c->txt, answer, len < c->txtlen ? len-1 : (c->txtlen)-1);
	
	/* just to be safe, let's make sure c->txt is null terminated */
	c->txt[(c->txtlen)-1] = '\0';

	return 1;
}

/*--- enum_callback: Callback from ENUM lookup function */
static int enum_callback(void *context, u_char *answer, int len, u_char *fullanswer)
{
	struct enum_context *c = (struct enum_context *)context;

	if (parse_naptr(c->dst, c->dstlen, c->tech, c->techlen, answer, len, c->naptrinput)) {
		ast_log(LOG_WARNING, "Failed to parse naptr :(\n");
		return -1;
	}

	if (!ast_strlen_zero(c->dst))
		return 1;

	return 0;
}

/*--- ast_get_enum: ENUM lookup */
int ast_get_enum(struct ast_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen)
{
	struct enum_context context;
	char tmp[259 + 80];
	char naptrinput[80] = "+";
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;

	strncat(naptrinput, number, sizeof(naptrinput) - 2);

	context.naptrinput = naptrinput;	/* The number */
	context.dst = dst;			/* Return string */
	context.dstlen = dstlen;
	context.tech = tech;			/* Return string */
	context.techlen = techlen;

	if (pos > 128)
		pos = 128;
	while(pos >= 0) {
		tmp[newpos++] = number[pos--];
		tmp[newpos++] = '.';
	}
	
	if (chan && ast_autoservice_start(chan) < 0)
		return -1;

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
			strncpy(tmp + newpos, s->toplev, sizeof(tmp) - newpos - 1);
		}
		ast_mutex_unlock(&enumlock);
		if (!s)
			break;
		ret = ast_search_dns(&context, tmp, C_IN, T_NAPTR, enum_callback);
		if (ret > 0)
			break;
	}
	if (ret < 0) {
		ast_log(LOG_DEBUG, "No such number found: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	}
	if (chan)
		ret |= ast_autoservice_stop(chan);
	return ret;
}

/*--- ast_get_txt: Get TXT record from DNS. 
	Really has nothing to do with enum, but anyway...
 */
int ast_get_txt(struct ast_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen, char *txt, int txtlen)
{
	struct enum_context context;
	char tmp[259 + 80];
	char naptrinput[80] = "+";
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;

	strncat(naptrinput, number, sizeof(naptrinput) - 2);

	context.naptrinput = naptrinput;
	context.dst = dst;
	context.dstlen = dstlen;
	context.tech = tech;
	context.techlen = techlen;
	context.txt = txt;
	context.txtlen = txtlen;

	if (pos > 128)
		pos = 128;
	while(pos >= 0) {
		tmp[newpos++] = number[pos--];
		tmp[newpos++] = '.';
	}
	
	if (chan && ast_autoservice_start(chan) < 0)
		return -1;

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
			strncpy(tmp + newpos, s->toplev, sizeof(tmp) - newpos - 1);
		}
		ast_mutex_unlock(&enumlock);
		if (!s)
			break;

		ret = ast_search_dns(&context, tmp, C_IN, T_TXT, txt_callback);
		if (ret > 0)
			break;
	}
	if (ret < 0) {
		ast_log(LOG_DEBUG, "No such number found: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	}
	if (chan)
		ret |= ast_autoservice_stop(chan);
	return ret;
}

/*--- enum_newtoplev: Add enum tree to linked list ---*/
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

/*--- ast_enum_init: Initialize the ENUM support subsystem */
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
	cfg = ast_config_load("enum.conf");
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
		ast_config_destroy(cfg);
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
