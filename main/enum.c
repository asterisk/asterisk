/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Funding provided by nic.at
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
 * \brief ENUM Support for Asterisk
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg Funding provided by nic.at
 *
 * \par Enum standards
 *
 * - NAPTR records: http://ietf.nri.reston.va.us/rfc/rfc2915.txt
 * - DNS SRV records: http://www.ietf.org/rfc/rfc2782.txt
 * - ENUM http://www.ietf.org/rfc/rfc3761.txt
 * - ENUM for H.323: http://www.ietf.org/rfc/rfc3762.txt
 * - ENUM SIP: http://www.ietf.org/rfc/rfc3764.txt
 * - IANA ENUM Services: http://www.iana.org/assignments/enum-services
 *
 * - I-ENUM:
 *   http://tools.ietf.org/wg/enum/draft-ietf-enum-combined/
 *   http://tools.ietf.org/wg/enum/draft-ietf-enum-branch-location-record/
 *
 * \par Possible improvement
 * \todo Implement a caching mechanism for multile enum lookups
 * - See https://issues.asterisk.org/view.php?id=6739
 * \todo The service type selection needs to be redone.
 */

/*! \li \ref enum.c uses the configuration file \ref enum.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page enum.conf enum.conf
 * \verbinclude enum.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#ifdef __APPLE__
#include <arpa/nameser_compat.h>
#endif
#include <resolv.h>
#include <ctype.h>
#include <regex.h>

#include "asterisk/enum.h"
#include "asterisk/dns.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"

#ifdef __APPLE__
#undef T_NAPTR
#define T_NAPTR 35
#endif

#ifdef __APPLE__
#undef T_TXT
#define T_TXT 16
#endif

static char ienum_branchlabel[32] = "i";
/* how to do infrastructure enum branch location resolution? */
#define ENUMLOOKUP_BLR_CC       0
#define ENUMLOOKUP_BLR_TXT      1
#define ENUMLOOKUP_BLR_EBL      2
static int ebl_alg = ENUMLOOKUP_BLR_CC;

/* EBL record provisional type code */
#define T_EBL      65300

AST_MUTEX_DEFINE_STATIC(enumlock);

/*! \brief Determine the length of a country code when given an E.164 string */
/*
 * Input: E.164 number w/o leading +
 *
 * Output: number of digits in the country code
 *	   0 on invalid number
 *
 * Algorithm:
 *   3 digits is the default length of a country code.
 *   country codes 1 and 7 are a single digit.
 *   the following country codes are two digits: 20, 27, 30-34, 36, 39,
 *     40, 41, 43-49, 51-58, 60-66, 81, 82, 84, 86, 90-95, 98.
 */
static int cclen(const char *number)
{
	int cc;
	char digits[3] = "";

	if (!number || (strlen(number) < 3)) {
		return 0;
	}

	strncpy(digits, number, 2);

	if (!sscanf(digits, "%30d", &cc)) {
		return 0;
	}

	if (cc / 10 == 1 || cc / 10 == 7)
		return 1;

	if (cc == 20 || cc == 27 || (cc >= 30 && cc <= 34) || cc == 36 ||
	    cc == 39 || cc == 40 || cc == 41 || (cc >= 40 && cc <= 41) ||
	    (cc >= 43 && cc <= 49) || (cc >= 51 && cc <= 58) ||
	    (cc >= 60 && cc <= 66) || cc == 81 || cc == 82 || cc == 84 ||
	    cc == 86 || (cc >= 90 && cc <= 95) || cc == 98) {
		return 2;
	}

	return 3;
}

struct txt_context {
	char txt[1024];		/* TXT record in TXT lookup */
	int txtlen;		/* Length */
};

/*! \brief Callback for TXT record lookup, /ol version */
static int txt_callback(void *context, unsigned char *answer, int len, unsigned char *fullanswer)
{
	struct txt_context *c = context;
	unsigned int i;

	c->txt[0] = 0;	/* default to empty */
	c->txtlen = 0;

	if (answer == NULL) {
		return 0;
	}

	/* RFC1035:
	 *
	 * <character-string> is a single length octet followed by that number of characters.
	 * TXT-DATA        One or more <character-string>s.
	 *
	 * We only take the first string here.
	 */

	i = *answer++;
	len -= 1;

	if (i > len) {	/* illegal packet */
		ast_log(LOG_WARNING, "txt_callback: malformed TXT record.\n");
		return 0;
	}

	if (i >= sizeof(c->txt)) {	/* too long? */
		ast_log(LOG_WARNING, "txt_callback: TXT record too long.\n");
		i = sizeof(c->txt) - 1;
	}

	ast_copy_string(c->txt, (char *)answer, i + 1);  /* this handles the \0 termination */
	c->txtlen = i;

	return 1;
}

/*! \brief Determine the branch location record as stored in a TXT record */
/*
 * Input: CC code
 *
 * Output: number of digits in the number before the i-enum branch
 *
 * Algorithm:  Build <ienum_branchlabel>.c.c.<suffix> and look for a TXT lookup.
 *		Return atoi(TXT-record).
 *		Return -1 on not found.
 *
 */
static int blr_txt(const char *cc, const char *suffix)
{
	struct txt_context context;
	char domain[128] = "";
	char *p1, *p2;
	int ret;

	ast_mutex_lock(&enumlock);

	ast_verb(4, "blr_txt()  cc='%s', suffix='%s', c_bl='%s'\n", cc, suffix, ienum_branchlabel);

	if (sizeof(domain) < (strlen(cc) * 2 + strlen(ienum_branchlabel) + strlen(suffix) + 2)) {
		ast_mutex_unlock(&enumlock);
		ast_log(LOG_WARNING, "ERROR: string sizing in blr_txt.\n");
		return -1;
	}

	p1 = domain + snprintf(domain, sizeof(domain), "%s.", ienum_branchlabel);
	ast_mutex_unlock(&enumlock);

	for (p2 = (char *) cc + strlen(cc) - 1; p2 >= cc; p2--) {
		if (isdigit(*p2)) {
			*p1++ = *p2;
			*p1++ = '.';
		}
	}
	strcat(p1, suffix);

	ast_verb(4, "blr_txt() FQDN for TXT record: %s, cc was %s\n", domain, cc);

	ret = ast_search_dns(&context, domain, C_IN, T_TXT, txt_callback);

	if (ret > 0) {
		ret = atoi(context.txt);

		if ((ret >= 0) && (ret < 20)) {
			ast_verb(3, "blr_txt() BLR TXT record for %s is %d (apex: %s)\n", cc, ret, suffix);
			return ret;
		}
	}

	ast_verb(3, "blr_txt() BLR TXT record for %s not found (apex: %s)\n", cc, suffix);

	return -1;
}

struct ebl_context {
	unsigned char pos;
	char separator[256];		/* label to insert */
	int sep_len;			/* Length */
	char apex[256];			/* new Apex */
	int apex_len;			/* Length */
};

/*! \brief Callback for EBL record lookup */
static int ebl_callback(void *context, unsigned char *answer, int len, unsigned char *fullanswer)
{
	struct ebl_context *c = context;
	int i;

	c->pos = 0;	/* default to empty */
	c->separator[0] = 0;
	c->sep_len = 0;
	c->apex[0] = 0;
	c->apex_len = 0;

	if (answer == NULL) {
		return 0;
	}

	/* draft-lendl-enum-branch-location-record-00
	 *
	 *      0  1  2  3  4  5  6  7
	 *    +--+--+--+--+--+--+--+--+
	 *    |       POSITION        |
	 *    +--+--+--+--+--+--+--+--+
	 *    /       SEPARATOR       /
	 *    +--+--+--+--+--+--+--+--+
	 *    /         APEX          /
	 *    +--+--+--+--+--+--+--+--+
	 *
	 *  where POSITION is a single byte, SEPARATOR is a <character-string>
	 *  and APEX is a <domain-name>.
	 *
	 */

	c->pos = *answer++;
	len -= 1;

	if ((c->pos > 15) || len < 2) {	/* illegal packet */
		ast_log(LOG_WARNING, "ebl_callback: malformed EBL record.\n");
		return 0;
	}

	i = *answer++;
	len -= 1;
	if (i > len) {	/* illegal packet */
		ast_log(LOG_WARNING, "ebl_callback: malformed EBL record.\n");
		return 0;
	}

	ast_copy_string(c->separator, (char *)answer, i + 1);
	c->sep_len = i;

	answer += i;
	len -= i;

	if ((i = dn_expand((unsigned char *)fullanswer, (unsigned char *)answer + len,
				(unsigned char *)answer, c->apex, sizeof(c->apex) - 1)) < 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return 0;
	}
	c->apex[i] = 0;
	c->apex_len = i;

	return 1;
}

/*! \brief Evaluate the I-ENUM branch as stored in an EBL record */
/*
 * Input: CC code
 *
 * Output: number of digits in the number before the i-enum branch
 *
 * Algorithm:  Build <ienum_branchlabel>.c.c.<suffix> and look for an EBL record
 *		Return pos and fill in separator and apex.
 *		Return -1 on not found.
 *
 */
static int blr_ebl(const char *cc, const char *suffix, char *separator, int sep_len, char* apex, int apex_len)
{
	struct ebl_context context;
	char domain[128] = "";
	char *p1,*p2;
	int ret;

	ast_mutex_lock(&enumlock);

	ast_verb(4, "blr_ebl()  cc='%s', suffix='%s', c_bl='%s'\n", cc, suffix, ienum_branchlabel);

	if (sizeof(domain) < (strlen(cc) * 2 + strlen(ienum_branchlabel) + strlen(suffix) + 2)) {
		ast_mutex_unlock(&enumlock);
		ast_log(LOG_WARNING, "ERROR: string sizing in blr_EBL.\n");
		return -1;
	}

	p1 = domain + snprintf(domain, sizeof(domain), "%s.", ienum_branchlabel);
	ast_mutex_unlock(&enumlock);

	for (p2 = (char *) cc + strlen(cc) - 1; p2 >= cc; p2--) {
		if (isdigit(*p2)) {
			*p1++ = *p2;
			*p1++ = '.';
		}
	}
	strcat(p1, suffix);

	ast_verb(4, "blr_ebl() FQDN for EBL record: %s, cc was %s\n", domain, cc);

	ret = ast_search_dns(&context, domain, C_IN, T_EBL, ebl_callback);
	if (ret > 0) {
		ret = context.pos;

		if ((ret >= 0) && (ret < 20)) {
			ast_verb(3, "blr_txt() BLR EBL record for %s is %d/%s/%s)\n", cc, ret, context.separator, context.apex);
			ast_copy_string(separator, context.separator, sep_len);
			ast_copy_string(apex, context.apex, apex_len);
			return ret;
		}
	}
	ast_verb(3, "blr_txt() BLR EBL record for %s not found (apex: %s)\n", cc, suffix);
	return -1;
}

/*! \brief Parse NAPTR record information elements */
static unsigned int parse_ie(char *data, unsigned int maxdatalen, unsigned char *src, unsigned int srclen)
{
	unsigned int len, olen;

	len = olen = (unsigned int) src[0];
	src++;
	srclen--;

	if (len > srclen) {
		ast_log(LOG_WARNING, "ENUM parsing failed: Wanted %u characters, got %u\n", len, srclen);
		return -1;
	}

	if (len > maxdatalen)
		len = maxdatalen;
	memcpy(data, src, len);

	return olen + 1;
}

/*! \brief Parse DNS NAPTR record used in ENUM ---*/
static int parse_naptr(unsigned char *dst, int dstsize, char *tech, int techsize, unsigned char *answer, int len, unsigned char *naptrinput)
{
	char tech_return[80];
	char *oanswer = (char *)answer;
	char flags[512] = "";
	char services[512] = "";
	char *p;
	char regexp[512] = "";
	char repl[512] = "";
	char tempdst[512] = "";
	char errbuff[512] = "";
	char delim;
	char *delim2;
	char *pattern, *subst, *d;
	int res;
	int regexp_len, rc;
	static const int max_bt = 10; /* max num of regexp backreference allowed, must remain 10 to guarantee a valid backreference index */
	int size, matchindex; /* size is the size of the backreference sub. */
	size_t d_len = sizeof(tempdst) - 1;
	regex_t preg;
	regmatch_t pmatch[max_bt];

	tech_return[0] = '\0';
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

	if ((res = dn_expand((unsigned char *)oanswer, (unsigned char *)answer + len, (unsigned char *)answer, repl, sizeof(repl) - 1)) < 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	}

	ast_debug(3, "NAPTR input='%s', flags='%s', services='%s', regexp='%s', repl='%s'\n",
		naptrinput, flags, services, regexp, repl);


	if (tolower(flags[0]) != 'u') {
		ast_log(LOG_WARNING, "NAPTR Flag must be 'U' or 'u'.\n");
		return -1;
	}

	p = strstr(services, "e2u+");
	if (p == NULL)
		p = strstr(services, "E2U+");
	if (p){
		p = p + 4;
		if (strchr(p, ':')){
			p = strchr(p, ':') + 1;
		}
		ast_copy_string(tech_return, p, sizeof(tech_return));
	} else {

		p = strstr(services, "+e2u");
		if (p == NULL)
			p = strstr(services, "+E2U");
		if (p) {
			*p = 0;
			p = strchr(services, ':');
			if (p)
				*p = 0;
			ast_copy_string(tech_return, services, sizeof(tech_return));
		}
	}

	regexp_len = strlen(regexp);
	if (regexp_len < 7) {
		ast_log(LOG_WARNING, "Regex too short to be meaningful.\n");
		return -1;
	}

	/* this takes the first character of the regexp (which is a delimiter)
	 * and uses that character to find the index of the second delimiter */
	delim = regexp[0];
	delim2 = strchr(regexp + 1, delim);
	if ((delim2 == NULL) || (regexp[regexp_len - 1] != delim)) {  /* is the second delimiter found, and is the end of the regexp a delimiter */
		ast_log(LOG_WARNING, "Regex delimiter error (on \"%s\").\n", regexp);
		return -1;
	} else if (strchr((delim2 + 1), delim) == NULL) { /* if the second delimiter is found, make sure there is a third instance.  this could be the end one instead of the middle */
		ast_log(LOG_WARNING, "Regex delimiter error (on \"%s\").\n", regexp);
		return -1;
	}
	pattern = regexp + 1;   /* pattern is the regex without the begining and ending delimiter */
	*delim2 = 0;    /* zero out the middle delimiter */
	subst   = delim2 + 1; /* dst substring is everything after the second delimiter. */
	regexp[regexp_len - 1] = 0; /* zero out the last delimiter */

/*
 * now do the regex wizardry.
 */

	if (regcomp(&preg, pattern, REG_EXTENDED | REG_NEWLINE)) {
		ast_log(LOG_WARNING, "NAPTR Regex compilation error (regex = \"%s\").\n", regexp);
		return -1;
	}

	if (preg.re_nsub > ARRAY_LEN(pmatch)) {
		ast_log(LOG_WARNING, "NAPTR Regex compilation error: too many subs.\n");
		regfree(&preg);
		return -1;
	}
	/* pmatch is an array containing the substring indexes for the regex backreference sub.
	 * max_bt is the maximum number of backreferences allowed to be stored in pmatch */
	if ((rc = regexec(&preg, (char *) naptrinput, max_bt, pmatch, 0))) {
		regerror(rc, &preg, errbuff, sizeof(errbuff));
		ast_log(LOG_WARNING, "NAPTR Regex match failed. Reason: %s\n", errbuff);
		regfree(&preg);
		return -1;
	}
	regfree(&preg);

	d = tempdst;
	d_len--;

	/* perform the backreference sub. Search the subst for backreferences,
	 * when a backreference is found, retrieve the backreferences number.
	 * use the backreference number as an index for pmatch to retrieve the
	 * beginning and ending indexes of the substring to insert as the backreference.
	 * if no backreference is found, continue copying the subst into tempdst */
	while (*subst && (d_len > 0)) {
		if ((subst[0] == '\\') && isdigit(subst[1])) { /* is this character the beginning of a backreference */
			matchindex = (int) (subst[1] - '0');
			if (matchindex >= ARRAY_LEN(pmatch)) {
				ast_log(LOG_WARNING, "Error during regex substitution. Invalid pmatch index.\n");
				return -1;
			}
			/* pmatch len is 10. we are garanteed a single char 0-9 is a valid index */
			size = pmatch[matchindex].rm_eo - pmatch[matchindex].rm_so;
			if (size > d_len) {
				ast_log(LOG_WARNING, "Not enough space during NAPTR regex substitution.\n");
				return -1;
			}
			/* are the pmatch indexes valid for the input length */
			if ((strlen((char *) naptrinput) >= pmatch[matchindex].rm_eo) && (pmatch[matchindex].rm_so <= pmatch[matchindex].rm_eo)) {
				memcpy(d, (naptrinput + (int) pmatch[matchindex].rm_so), size);  /* copy input substring into backreference marker */
				d_len -= size;
				subst += 2;  /* skip over backreference characters to next valid character */
				d += size;
			} else {
				ast_log(LOG_WARNING, "Error during regex substitution. Invalid backreference index.\n");
				return -1;
			}
		} else if (isprint(*subst)) {
			*d++ = *subst++;
			d_len--;
		} else {
			ast_log(LOG_WARNING, "Error during regex substitution.\n");
			return -1;
		}
	}
	*d = 0;
	ast_copy_string((char *) dst, tempdst, dstsize);
	dst[dstsize - 1] = '\0';

	if (*tech != '\0'){ /* check if it is requested NAPTR */
		if (!strncasecmp(tech, "ALL", techsize)){
			return 0; /* return or count any RR */
		}
		if (!strncasecmp(tech_return, tech, sizeof(tech_return) < techsize ? sizeof(tech_return): techsize)){
			ast_copy_string(tech, tech_return, techsize);
			return 0; /* we got our RR */
		} else { /* go to the next RR in the DNS answer */
			return 1;
		}
	}

	/* tech was not specified, return first parsed RR */
	ast_copy_string(tech, tech_return, techsize);

	return 0;
}

/* do not return requested value, just count RRs and return thei number in dst */
#define ENUMLOOKUP_OPTIONS_COUNT       1
/* do an ISN style lookup */
#define ENUMLOOKUP_OPTIONS_ISN		2
/* do a infrastructure ENUM lookup */
#define ENUMLOOKUP_OPTIONS_IENUM	4
/* do a direct DNS lookup: no reversal */
#define ENUMLOOKUP_OPTIONS_DIRECT	8

/*! \brief Callback from ENUM lookup function */
static int enum_callback(void *context, unsigned char *answer, int len, unsigned char *fullanswer)
{
	struct enum_context *c = context;
	void *p = NULL;
	int res;

	res = parse_naptr((unsigned char *)c->dst, c->dstlen, c->tech, c->techlen, answer, len, (unsigned char *)c->naptrinput);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to parse naptr\n");
		return -1;
	} else if ((res == 0) && !ast_strlen_zero(c->dst)) { /* ok, we got needed NAPTR */
		if (c->options & ENUMLOOKUP_OPTIONS_COUNT) { /* counting RRs */
			c->count++;
			snprintf(c->dst, c->dstlen, "%d", c->count);
		} else  {
			if ((p = ast_realloc(c->naptr_rrs, sizeof(*c->naptr_rrs) * (c->naptr_rrs_count + 1)))) {
				c->naptr_rrs = p;
				memcpy(&c->naptr_rrs[c->naptr_rrs_count].naptr, answer, sizeof(c->naptr_rrs->naptr));
				c->naptr_rrs[c->naptr_rrs_count].result = ast_strdup(c->dst);
				c->naptr_rrs[c->naptr_rrs_count].tech = ast_strdup(c->tech);
				c->naptr_rrs[c->naptr_rrs_count].sort_pos = c->naptr_rrs_count;
				c->naptr_rrs_count++;
			}
			c->dst[0] = 0;
		}
		return 0;
	}

	return 0;
}

/* ENUM lookup */
int ast_get_enum(struct ast_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen, char* suffix, char* options, unsigned int record, struct enum_context **argcontext)
{
	struct enum_context *context;
	char tmp[512];
	char domain[256];
	char left[128];
	char middle[128];
	char naptrinput[128];
	char apex[128] = "";
	int ret = -1;
	/* for ISN rewrite */
	char *p1 = NULL;
	char *p2 = NULL;
	char *p3 = NULL;
	int k = 0;
	int i = 0;
	int z = 0;
	int spaceleft = 0;
	struct timeval time_start, time_end;

	if (ast_strlen_zero(suffix)) {
		ast_log(LOG_WARNING, "ast_get_enum need a suffix parameter now.\n");
		return -1;
	}

	ast_debug(2, "num='%s', tech='%s', suffix='%s', options='%s', record=%u\n", number, tech, suffix, options, record);

/*
  We don't need that any more, that "n" preceding the number has been replaced by a flag
  in the options paramter.
	ast_copy_string(naptrinput, number, sizeof(naptrinput));
*/
/*
 * The "number" parameter includes a leading '+' if it's a full E.164 number (and not ISN)
 * We need to preserve that as the regex inside NAPTRs expect the +.
 *
 * But for the domain generation, the '+' is a nuissance, so we get rid of it.
*/
	ast_copy_string(naptrinput, number[0] == 'n' ? number + 1 : number, sizeof(naptrinput));
	if (number[0] == '+') {
		number++;
	}

	if (!(context = ast_calloc(1, sizeof(*context)))) {
		return -1;
	}

	if ((p3 = strchr(naptrinput, '*'))) {
		*p3='\0';
	}

	context->naptrinput = naptrinput;	/* The number */
	context->dst = dst;			/* Return string */
	context->dstlen = dstlen;
	context->tech = tech;
	context->techlen = techlen;
	context->options = 0;
	context->position = record > 0 ? record : 1;
	context->count = 0;
	context->naptr_rrs = NULL;
	context->naptr_rrs_count = 0;

	/*
	 * Process options:
	 *
	 *	c	Return count, not URI
	 *	i	Use infrastructure ENUM
	 *	s	Do ISN transformation
	 *	d	Direct DNS query: no reversing.
	 *
	 */
	if (options != NULL) {
		if (strchr(options,'s')) {
			context->options |= ENUMLOOKUP_OPTIONS_ISN;
		} else if (strchr(options,'i')) {
			context->options |= ENUMLOOKUP_OPTIONS_IENUM;
		} else if (strchr(options,'d')) {
			context->options |= ENUMLOOKUP_OPTIONS_DIRECT;
		}
		if (strchr(options,'c')) {
			context->options |= ENUMLOOKUP_OPTIONS_COUNT;
		}
		if (strchr(number,'*')) {
			context->options |= ENUMLOOKUP_OPTIONS_ISN;
		}
	}
	ast_debug(2, "ENUM options(%s): pos=%d, options='%d'\n", options, context->position, context->options);
	ast_debug(1, "n='%s', tech='%s', suffix='%s', options='%d', record='%d'\n",
			number, tech, suffix, context->options, context->position);

	/*
	 * This code does more than simple RFC3261 ENUM. All these rewriting
	 * schemes have in common that they build the FQDN for the NAPTR lookup
	 * by concatenating
	 *    - a number which needs be flipped and "."-seperated 	(left)
	 *    - some fixed string					(middle)
	 *    - an Apex.						(apex)
	 *
	 * The RFC3261 ENUM is: left=full number, middle="", apex=from args.
	 * ISN:  number = "middle*left", apex=from args
	 * I-ENUM: EBL parameters build the split, can change apex
	 * Direct: left="", middle=argument, apex=from args
	 *
	 */

	/* default: the whole number will be flipped, no middle domain component */
	ast_copy_string(left, number, sizeof(left));
	middle[0] = '\0';
	/*
	 * I-ENUM can change the apex, thus we copy it
	 */
	ast_copy_string(apex, suffix, sizeof(apex));
	/* ISN rewrite */
	if ((context->options & ENUMLOOKUP_OPTIONS_ISN) && (p1 = strchr(number, '*'))) {
		*p1++ = '\0';
		ast_copy_string(left, number, sizeof(left));
		ast_copy_string(middle, p1, sizeof(middle) - 1);
		strcat(middle, ".");
		ast_debug(2, "ISN ENUM: left=%s, middle='%s'\n", left, middle);
	/* Direct DNS lookup rewrite */
	} else if (context->options & ENUMLOOKUP_OPTIONS_DIRECT) {
		left[0] = 0; /* nothing to flip around */
		ast_copy_string(middle, number, sizeof(middle) - 1);
		strcat(middle, ".");
		ast_debug(2, "DIRECT ENUM:  middle='%s'\n", middle);
	/* Infrastructure ENUM rewrite */
	} else if (context->options & ENUMLOOKUP_OPTIONS_IENUM) {
		int sdl = 0;
		char cc[8];
		char sep[256], n_apex[256];
		int cc_len = cclen(number);
		sdl = cc_len;
		ast_mutex_lock(&enumlock);
		ast_copy_string(sep, ienum_branchlabel, sizeof(sep)); /* default */
		ast_mutex_unlock(&enumlock);

		switch (ebl_alg) {
		case ENUMLOOKUP_BLR_EBL:
			ast_copy_string(cc, number, cc_len); /* cclen() never returns more than 3 */
			sdl = blr_ebl(cc, suffix, sep, sizeof(sep) - 1, n_apex, sizeof(n_apex) - 1);

			if (sdl >= 0) {
				ast_copy_string(apex, n_apex, sizeof(apex));
				ast_debug(2, "EBL ENUM: sep=%s, apex='%s'\n", sep, n_apex);
			} else {
				sdl = cc_len;
			}
			break;
		case ENUMLOOKUP_BLR_TXT:
			ast_copy_string(cc, number, cc_len); /* cclen() never returns more than 3 */
			sdl = blr_txt(cc, suffix);

			if (sdl < 0) {
				sdl = cc_len;
			}
			break;

		case ENUMLOOKUP_BLR_CC:	/* BLR is at the country-code level */
		default:
			sdl = cc_len;
			break;
		}

		if (sdl > strlen(number)) {	/* Number too short for this sdl? */
			ast_log(LOG_WARNING, "I-ENUM: subdomain location %d behind number %s\n", sdl, number);
			ast_free(context);
			return 0;
		}
		ast_copy_string(left, number + sdl, sizeof(left));

		ast_mutex_lock(&enumlock);
		ast_copy_string(middle, sep, sizeof(middle) - 1);
		strcat(middle, ".");
		ast_mutex_unlock(&enumlock);

		/* check the space we need for middle */
		if ((sdl * 2 + strlen(middle) + 2) > sizeof(middle)) {
			ast_log(LOG_WARNING, "ast_get_enum: not enough space for I-ENUM rewrite.\n");
			ast_free(context);
			return -1;
		}

		p1 = middle + strlen(middle);
		for (p2 = (char *) number + sdl - 1; p2 >= number; p2--) {
			if (isdigit(*p2)) {
				*p1++ = *p2;
				*p1++ = '.';
			}
		}
		*p1 = '\0';

		ast_debug(2, "I-ENUM: cclen=%d, left=%s, middle='%s', apex='%s'\n", cc_len, left, middle, apex);
	}

	if (strlen(left) * 2 + 2 > sizeof(domain)) {
		ast_log(LOG_WARNING, "string to long in ast_get_enum\n");
		ast_free(context);
		return -1;
	}

	/* flip left into domain */
	p1 = domain;
	for (p2 = left + strlen(left); p2 >= left; p2--) {
		if (isdigit(*p2)) {
			*p1++ = *p2;
			*p1++ = '.';
		}
	}
	*p1 = '\0';

	if (chan && ast_autoservice_start(chan) < 0) {
		ast_free(context);
		return -1;
	}

	spaceleft = sizeof(tmp) - 2;
	ast_copy_string(tmp, domain, spaceleft);
	spaceleft -= strlen(domain);

	if (*middle) {
		strncat(tmp, middle, spaceleft);
		spaceleft -= strlen(middle);
	}

	strncat(tmp,apex,spaceleft);
	time_start = ast_tvnow();
	ret = ast_search_dns(context, tmp, C_IN, T_NAPTR, enum_callback);
	time_end = ast_tvnow();

	ast_debug(2, "profiling: %s, %s, %" PRIi64 " ms\n",
			(ret == 0) ? "OK" : "FAIL", tmp, ast_tvdiff_ms(time_end, time_start));

	if (ret < 0) {
		ast_debug(1, "No such number found: %s (%s)\n", tmp, strerror(errno));
		context->naptr_rrs_count = -1;
		strcpy(dst, "0");
		ret = 0;
	}

	if (context->naptr_rrs_count >= context->position && ! (context->options & ENUMLOOKUP_OPTIONS_COUNT)) {
		/* sort array by NAPTR order/preference */
		for (k = 0; k < context->naptr_rrs_count; k++) {
			for (i = 0; i < context->naptr_rrs_count; i++) {
				/* use order first and then preference to compare */
				if ((ntohs(context->naptr_rrs[k].naptr.order) < ntohs(context->naptr_rrs[i].naptr.order)
				     && context->naptr_rrs[k].sort_pos > context->naptr_rrs[i].sort_pos)
				     || (ntohs(context->naptr_rrs[k].naptr.order) > ntohs(context->naptr_rrs[i].naptr.order)
				     && context->naptr_rrs[k].sort_pos < context->naptr_rrs[i].sort_pos)) {
					z = context->naptr_rrs[k].sort_pos;
					context->naptr_rrs[k].sort_pos = context->naptr_rrs[i].sort_pos;
					context->naptr_rrs[i].sort_pos = z;
					continue;
				}
				if (ntohs(context->naptr_rrs[k].naptr.order) == ntohs(context->naptr_rrs[i].naptr.order)) {
					if ((ntohs(context->naptr_rrs[k].naptr.pref) < ntohs(context->naptr_rrs[i].naptr.pref)
					     && context->naptr_rrs[k].sort_pos > context->naptr_rrs[i].sort_pos)
					     || (ntohs(context->naptr_rrs[k].naptr.pref) > ntohs(context->naptr_rrs[i].naptr.pref)
					     && context->naptr_rrs[k].sort_pos < context->naptr_rrs[i].sort_pos)) {
						z = context->naptr_rrs[k].sort_pos;
						context->naptr_rrs[k].sort_pos = context->naptr_rrs[i].sort_pos;
						context->naptr_rrs[i].sort_pos = z;
					}
				}
			}
		}
		for (k = 0; k < context->naptr_rrs_count; k++) {
			if (context->naptr_rrs[k].sort_pos == context->position - 1) {
				ast_copy_string(context->dst, context->naptr_rrs[k].result, dstlen);
				ast_copy_string(context->tech, context->naptr_rrs[k].tech, techlen);
				break;
			}
		}
	} else if (!(context->options & ENUMLOOKUP_OPTIONS_COUNT)) {
		context->dst[0] = 0;
	} else if ((context->options & ENUMLOOKUP_OPTIONS_COUNT)) {
		snprintf(context->dst, context->dstlen, "%d", context->naptr_rrs_count + context->count);
	}

	if (chan) {
		ret |= ast_autoservice_stop(chan);
	}

	if (!argcontext) {
		for (k = 0; k < context->naptr_rrs_count; k++) {
			ast_free(context->naptr_rrs[k].result);
			ast_free(context->naptr_rrs[k].tech);
		}
		ast_free(context->naptr_rrs);
		ast_free(context);
	} else {
		*argcontext = context;
	}

	return ret;
}

int ast_get_txt(struct ast_channel *chan, const char *number, char *txt, int txtlen, char *suffix)
{
	struct txt_context context;
	char tmp[259 + 512];
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;

	ast_debug(4, "ast_get_txt: Number = '%s', suffix = '%s'\n", number, suffix);

	if (chan && ast_autoservice_start(chan) < 0) {
		return -1;
	}

	if (pos > 128) {
		pos = 128;
	}

	while (pos >= 0) {
		if (isdigit(number[pos])) {
			tmp[newpos++] = number[pos];
			tmp[newpos++] = '.';
		}
		pos--;
	}

	ast_copy_string(&tmp[newpos], suffix, sizeof(tmp) - newpos);

	if (ret < 0) {
		ast_debug(2, "No such number found in ENUM: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	} else {
		ast_copy_string(txt, context.txt, txtlen);
	}
	if (chan) {
		ret |= ast_autoservice_stop(chan);
	}
	return ret;
}

/*! \brief Initialize the ENUM support subsystem */
static int private_enum_init(int reload)
{
	struct ast_config *cfg;
	const char *string;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if ((cfg = ast_config_load2("enum.conf", "enum", config_flags)) == CONFIG_STATUS_FILEUNCHANGED)
		return 0;
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	/* Destroy existing list */
	ast_mutex_lock(&enumlock);
	if (cfg) {
		if ((string = ast_variable_retrieve(cfg, "ienum", "branchlabel"))) {
			ast_copy_string(ienum_branchlabel, string, sizeof(ienum_branchlabel));
		}

		if ((string = ast_variable_retrieve(cfg, "ienum", "ebl_alg"))) {
			ebl_alg = ENUMLOOKUP_BLR_CC; /* default */

			if (!strcasecmp(string, "txt"))
				ebl_alg = ENUMLOOKUP_BLR_TXT;
			else if (!strcasecmp(string, "ebl"))
				ebl_alg = ENUMLOOKUP_BLR_EBL;
			else if (!strcasecmp(string, "cc"))
				ebl_alg = ENUMLOOKUP_BLR_CC;
			else
				ast_log(LOG_WARNING, "No valid parameter for ienum/ebl_alg.\n");
		}
		ast_config_destroy(cfg);
	}
	ast_mutex_unlock(&enumlock);
	return 0;
}

int ast_enum_init(void)
{
	return private_enum_init(0);
}

int ast_enum_reload(void)
{
	return private_enum_init(1);
}
