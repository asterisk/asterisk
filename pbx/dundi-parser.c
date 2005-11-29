/*
 * Distributed Universal Number Discovery (DUNDi)
 *
 * Copyright (C) 2004 - 2005, Digium Inc.
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * This program is Free Software distributed under the terms of
 * of the GNU General Public License.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/frame.h"
#include "asterisk/utils.h"
#include "asterisk/dundi.h"
#include "dundi-parser.h"
#include "asterisk/dundi.h"

static void internaloutput(const char *str)
{
	fputs(str, stdout);
}

static void internalerror(const char *str)
{
	fprintf(stderr, "WARNING: %s", str);
}

static void (*outputf)(const char *str) = internaloutput;
static void (*errorf)(const char *str) = internalerror;

char *dundi_eid_to_str(char *s, int maxlen, dundi_eid *eid)
{
	int x;
	char *os = s;
	if (maxlen < 18) {
		if (s && (maxlen > 0))
			*s = '\0';
	} else {
		for (x=0;x<5;x++) {
			sprintf(s, "%02x:", eid->eid[x]);
			s += 3;
		}
		sprintf(s, "%02x", eid->eid[5]);
	}
	return os;
}

char *dundi_eid_to_str_short(char *s, int maxlen, dundi_eid *eid)
{
	int x;
	char *os = s;
	if (maxlen < 13) {
		if (s && (maxlen > 0))
			*s = '\0';
	} else {
		for (x=0;x<6;x++) {
			sprintf(s, "%02X", eid->eid[x]);
			s += 2;
		}
	}
	return os;
}

int dundi_str_to_eid(dundi_eid *eid, char *s)
{
	unsigned int eid_int[6];
	int x;
	if (sscanf(s, "%x:%x:%x:%x:%x:%x", &eid_int[0], &eid_int[1], &eid_int[2],
		 &eid_int[3], &eid_int[4], &eid_int[5]) != 6)
		 	return -1;
	for (x=0;x<6;x++)
		eid->eid[x] = eid_int[x];
	return 0;
}

int dundi_str_short_to_eid(dundi_eid *eid, char *s)
{
	unsigned int eid_int[6];
	int x;
	if (sscanf(s, "%2x%2x%2x%2x%2x%2x", &eid_int[0], &eid_int[1], &eid_int[2],
		 &eid_int[3], &eid_int[4], &eid_int[5]) != 6)
		 	return -1;
	for (x=0;x<6;x++)
		eid->eid[x] = eid_int[x];
	return 0;
}

int dundi_eid_zero(dundi_eid *eid)
{
	int x;
	for (x=0;x<sizeof(eid->eid) / sizeof(eid->eid[0]);x++)
		if (eid->eid[x]) return 0;
	return 1;
}

int dundi_eid_cmp(dundi_eid *eid1, dundi_eid *eid2)
{
	return memcmp(eid1, eid2, sizeof(dundi_eid));
}

static void dump_string(char *output, int maxlen, void *value, int len)
{
	maxlen--;
	if (maxlen > len)
		maxlen = len;
	strncpy(output,value, maxlen);
	output[maxlen] = '\0';
}

static void dump_cbypass(char *output, int maxlen, void *value, int len)
{
	maxlen--;
	strncpy(output, "Bypass Caches", maxlen);
	output[maxlen] = '\0';
}

static void dump_eid(char *output, int maxlen, void *value, int len)
{
	if (len == 6)
		dundi_eid_to_str(output, maxlen, (dundi_eid *)value);
	else
		snprintf(output, maxlen, "Invalid EID len %d", len);
}

char *dundi_hint2str(char *buf, int bufsiz, int flags)
{
	strcpy(buf, "");
	buf[bufsiz-1] = '\0';
	if (flags & DUNDI_HINT_TTL_EXPIRED) {
		strncat(buf, "TTLEXPIRED|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_HINT_DONT_ASK) {
		strncat(buf, "DONTASK|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_HINT_UNAFFECTED) {
		strncat(buf, "UNAFFECTED|", bufsiz - strlen(buf) - 1);
	}
	/* Get rid of trailing | */
	if (ast_strlen_zero(buf))
		strcpy(buf, "NONE|");
	buf[strlen(buf)-1] = '\0';
	return buf;
}

static void dump_hint(char *output, int maxlen, void *value, int len)
{
	unsigned short flags;
	char tmp[512];
	char tmp2[256];
	if (len < 2) {
		strncpy(output, "<invalid contents>", maxlen);
		return;
	}
	memcpy(&flags, value, sizeof(flags));
	flags = ntohs(flags);
	memset(tmp, 0, sizeof(tmp));
	dundi_hint2str(tmp2, sizeof(tmp2), flags);
	snprintf(tmp, sizeof(tmp), "[%s] ", tmp2);
	memcpy(tmp + strlen(tmp), value + 2, len - 2);
	strncpy(output, tmp, maxlen - 1);
}

static void dump_cause(char *output, int maxlen, void *value, int len)
{
	static char *causes[] = {
		"SUCCESS",
		"GENERAL",
		"DYNAMIC",
		"NOAUTH" ,
		};
	char tmp[256];
	char tmp2[256];
	int mlen;
	unsigned char cause;
	if (len < 1) {
		strncpy(output, "<invalid contents>", maxlen);
		return;
	}
	cause = *((unsigned char *)value);
	memset(tmp2, 0, sizeof(tmp2));
	mlen = len - 1;
	if (mlen > 255)
		mlen = 255;
	memcpy(tmp2, value + 1, mlen);
	if (cause < sizeof(causes) / sizeof(causes[0])) {
		if (len > 1)
			snprintf(tmp, sizeof(tmp), "%s: %s", causes[cause], tmp2);
		else
			snprintf(tmp, sizeof(tmp), "%s", causes[cause]);
	} else {
		if (len > 1)
			snprintf(tmp, sizeof(tmp), "%d: %s", cause, tmp2);
		else
			snprintf(tmp, sizeof(tmp), "%d", cause);
	}
	
	strncpy(output,tmp, maxlen);
	output[maxlen] = '\0';
}

static void dump_int(char *output, int maxlen, void *value, int len)
{
	if (len == (int)sizeof(unsigned int))
		snprintf(output, maxlen, "%lu", (unsigned long)ntohl(*((unsigned int *)value)));
	else
		snprintf(output, maxlen, "Invalid INT");
}

static void dump_short(char *output, int maxlen, void *value, int len)
{
	if (len == (int)sizeof(unsigned short))
		snprintf(output, maxlen, "%d", ntohs(*((unsigned short *)value)));
	else
		snprintf(output, maxlen, "Invalid SHORT");
}

static void dump_byte(char *output, int maxlen, void *value, int len)
{
	if (len == (int)sizeof(unsigned char))
		snprintf(output, maxlen, "%d", *((unsigned char *)value));
	else
		snprintf(output, maxlen, "Invalid BYTE");
}

static char *proto2str(int proto, char *buf, int bufsiz)
{	
	switch(proto) {
	case DUNDI_PROTO_NONE:
		strncpy(buf, "None", bufsiz - 1);
		break;
	case DUNDI_PROTO_IAX:
		strncpy(buf, "IAX", bufsiz - 1);
		break;
	case DUNDI_PROTO_SIP:
		strncpy(buf, "SIP", bufsiz - 1);
		break;
	case DUNDI_PROTO_H323:
		strncpy(buf, "H.323", bufsiz - 1);
		break;
	default:
		snprintf(buf, bufsiz, "Unknown Proto(%d)", proto);
	}
	buf[bufsiz-1] = '\0';
	return buf;
}

char *dundi_flags2str(char *buf, int bufsiz, int flags)
{
	strcpy(buf, "");
	buf[bufsiz-1] = '\0';
	if (flags & DUNDI_FLAG_EXISTS) {
		strncat(buf, "EXISTS|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_MATCHMORE) {
		strncat(buf, "MATCHMORE|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_CANMATCH) {
		strncat(buf, "CANMATCH|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_IGNOREPAT) {
		strncat(buf, "IGNOREPAT|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_RESIDENTIAL) {
		strncat(buf, "RESIDENCE|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_COMMERCIAL) {
		strncat(buf, "COMMERCIAL|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_MOBILE) {
		strncat(buf, "MOBILE", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_NOUNSOLICITED) {
		strncat(buf, "NOUNSLCTD|", bufsiz - strlen(buf) - 1);
	}
	if (flags & DUNDI_FLAG_NOCOMUNSOLICIT) {
		strncat(buf, "NOCOMUNSLTD|", bufsiz - strlen(buf) - 1);
	}
	/* Get rid of trailing | */
	if (ast_strlen_zero(buf))
		strcpy(buf, "NONE|");
	buf[strlen(buf)-1] = '\0';
	return buf;
}

static void dump_answer(char *output, int maxlen, void *value, int len)
{
	struct dundi_answer *answer;
	char proto[40];
	char flags[40];
	char eid_str[40];
	char tmp[512]="";
	if (len >= 10) {
		answer = (struct dundi_answer *)(value);
		memcpy(tmp, answer->data, (len >= 500) ? 500 : len - 10);
		dundi_eid_to_str(eid_str, sizeof(eid_str), &answer->eid);
		snprintf(output, maxlen, "[%s] %d <%s/%s> from [%s]", 
			dundi_flags2str(flags, sizeof(flags), ntohs(answer->flags)), 
			ntohs(answer->weight),
			proto2str(answer->protocol, proto, sizeof(proto)), 
				tmp, eid_str);
	} else
		strncpy(output, "Invalid Answer", maxlen - 1);
}

static void dump_encrypted(char *output, int maxlen, void *value, int len)
{
	char iv[33];
	int x;
	if ((len > 16) && !(len % 16)) {
		/* Build up IV */
		for (x=0;x<16;x++) {
			snprintf(iv + (x << 1), 3, "%02x", ((unsigned char *)value)[x]);
		}
		snprintf(output, maxlen, "[IV %s] %d encrypted blocks\n", iv, len / 16);
	} else
		snprintf(output, maxlen, "Invalid Encrypted Datalen %d", len);
}

static void dump_raw(char *output, int maxlen, void *value, int len)
{
	int x;
	unsigned char *u = value;
	output[maxlen - 1] = '\0';
	strcpy(output, "[ ");
	for (x=0;x<len;x++) {
		snprintf(output + strlen(output), maxlen - strlen(output) - 1, "%02x ", u[x]);
	}
	strncat(output + strlen(output), "]", maxlen - strlen(output) - 1);
}

static struct dundi_ie {
	int ie;
	char *name;
	void (*dump)(char *output, int maxlen, void *value, int len);
} ies[] = {
	{ DUNDI_IE_EID, "ENTITY IDENT", dump_eid },
	{ DUNDI_IE_CALLED_CONTEXT, "CALLED CONTEXT", dump_string },
	{ DUNDI_IE_CALLED_NUMBER, "CALLED NUMBER", dump_string },
	{ DUNDI_IE_EID_DIRECT, "DIRECT EID", dump_eid },
	{ DUNDI_IE_ANSWER, "ANSWER", dump_answer },
	{ DUNDI_IE_TTL, "TTL", dump_short },
	{ DUNDI_IE_VERSION, "VERSION", dump_short },
	{ DUNDI_IE_EXPIRATION, "EXPIRATION", dump_short },
	{ DUNDI_IE_UNKNOWN, "UKWN DUNDI CMD", dump_byte },
	{ DUNDI_IE_CAUSE, "CAUSE", dump_cause },
	{ DUNDI_IE_REQEID, "REQUEST EID", dump_eid },
	{ DUNDI_IE_ENCDATA, "ENCDATA", dump_encrypted },
	{ DUNDI_IE_SHAREDKEY, "SHAREDKEY", dump_raw },
	{ DUNDI_IE_SIGNATURE, "SIGNATURE", dump_raw },
	{ DUNDI_IE_KEYCRC32, "KEYCRC32", dump_int },
	{ DUNDI_IE_HINT, "HINT", dump_hint },
	{ DUNDI_IE_DEPARTMENT, "DEPARTMENT", dump_string },
	{ DUNDI_IE_ORGANIZATION, "ORGANIZTN", dump_string },
	{ DUNDI_IE_LOCALITY, "LOCALITY", dump_string },
	{ DUNDI_IE_STATE_PROV, "STATEPROV", dump_string },
	{ DUNDI_IE_COUNTRY, "COUNTRY", dump_string },
	{ DUNDI_IE_EMAIL, "EMAIL", dump_string },
	{ DUNDI_IE_PHONE, "PHONE", dump_string },
	{ DUNDI_IE_IPADDR, "ADDRESS", dump_string },
	{ DUNDI_IE_CACHEBYPASS, "CBYPASS", dump_cbypass },
};

const char *dundi_ie2str(int ie)
{
	int x;
	for (x=0;x<(int)sizeof(ies) / (int)sizeof(ies[0]); x++) {
		if (ies[x].ie == ie)
			return ies[x].name;
	}
	return "Unknown IE";
}

static void dump_ies(unsigned char *iedata, int spaces, int len)
{
	int ielen;
	int ie;
	int x;
	int found;
	char interp[1024];
	char tmp[1024];
	if (len < 2)
		return;
	while(len >= 2) {
		ie = iedata[0];
		ielen = iedata[1];
		/* Encrypted data is the remainder */
		if (ie == DUNDI_IE_ENCDATA)
			ielen = len - 2;
		if (ielen + 2> len) {
			snprintf(tmp, (int)sizeof(tmp), "Total IE length of %d bytes exceeds remaining frame length of %d bytes\n", ielen + 2, len);
			outputf(tmp);
			return;
		}
		found = 0;
		for (x=0;x<(int)sizeof(ies) / (int)sizeof(ies[0]); x++) {
			if (ies[x].ie == ie) {
				if (ies[x].dump) {
					ies[x].dump(interp, (int)sizeof(interp), iedata + 2, ielen);
					snprintf(tmp, (int)sizeof(tmp), "   %s%-15.15s : %s\n", (spaces ? "     " : "" ), ies[x].name, interp);
					outputf(tmp);
				} else {
					if (ielen)
						snprintf(interp, (int)sizeof(interp), "%d bytes", ielen);
					else
						strcpy(interp, "Present");
					snprintf(tmp, (int)sizeof(tmp), "   %s%-15.15s : %s\n", (spaces ? "     " : "" ), ies[x].name, interp);
					outputf(tmp);
				}
				found++;
			}
		}
		if (!found) {
			snprintf(tmp, (int)sizeof(tmp), "   %sUnknown IE %03d  : Present\n", (spaces ? "     " : "" ), ie);
			outputf(tmp);
		}
		iedata += (2 + ielen);
		len -= (2 + ielen);
	}
	outputf("\n");
}

void dundi_showframe(struct dundi_hdr *fhi, int rx, struct sockaddr_in *sin, int datalen)
{
	char *pref[] = {
		"Tx",
		"Rx",
		"    ETx",
		"    Erx" };
	char *commands[] = {
		"ACK         ",
		"DPDISCOVER  ",
		"DPRESPONSE  ",
		"EIDQUERY    ",
		"EIDRESPONSE ",
		"PRECACHERQ  ",
		"PRECACHERP  ",
		"INVALID     ",
		"UNKNOWN CMD ",
		"NULL        ",
		"REQREQ      ",
		"REGRESPONSE ",
		"CANCEL      ",
		"ENCRYPT     ",
		"ENCREJ      " };
	char class2[20];
	char *class;
	char subclass2[20];
	char *subclass;
	char tmp[256];
	char retries[20];
	char iabuf[INET_ADDRSTRLEN];
	if (ntohs(fhi->dtrans) & DUNDI_FLAG_RETRANS)
		strcpy(retries, "Yes");
	else
		strcpy(retries, "No");
	if ((ntohs(fhi->strans) & DUNDI_FLAG_RESERVED)) {
		/* Ignore frames with high bit set to 1 */
		return;
	}
	if ((fhi->cmdresp & 0x3f) > (int)sizeof(commands)/(int)sizeof(char *)) {
		snprintf(class2, (int)sizeof(class2), "(%d?)", fhi->cmdresp);
		class = class2;
	} else {
		class = commands[(int)(fhi->cmdresp & 0x3f)];
	}
	snprintf(subclass2, (int)sizeof(subclass2), "%02x", fhi->cmdflags);
	subclass = subclass2;
	snprintf(tmp, (int)sizeof(tmp), 
		"%s-Frame Retry[%s] -- OSeqno: %3.3d ISeqno: %3.3d Type: %s (%s)\n",
		pref[rx],
		retries, fhi->oseqno, fhi->iseqno, class, fhi->cmdresp & 0x40 ? "Response" : "Command");
	outputf(tmp);
	snprintf(tmp, (int)sizeof(tmp), 
		"%s     Flags: %s STrans: %5.5d  DTrans: %5.5d [%s:%d]%s\n", (rx > 1) ? "     " : "",
		subclass, ntohs(fhi->strans) & ~DUNDI_FLAG_RESERVED, ntohs(fhi->dtrans) & ~DUNDI_FLAG_RETRANS,
		ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr), ntohs(sin->sin_port),
		fhi->cmdresp & 0x80 ? " (Final)" : "");
	outputf(tmp);
	dump_ies(fhi->ies, rx > 1, datalen);
}

int dundi_ie_append_raw(struct dundi_ie_data *ied, unsigned char ie, void *data, int datalen)
{
	char tmp[256];
	if (datalen > ((int)sizeof(ied->buf) - ied->pos)) {
		snprintf(tmp, (int)sizeof(tmp), "Out of space for ie '%s' (%d), need %d have %d\n", dundi_ie2str(ie), ie, datalen, (int)sizeof(ied->buf) - ied->pos);
		errorf(tmp);
		return -1;
	}
	ied->buf[ied->pos++] = ie;
	ied->buf[ied->pos++] = datalen;
	memcpy(ied->buf + ied->pos, data, datalen);
	ied->pos += datalen;
	return 0;
}

int dundi_ie_append_cause(struct dundi_ie_data *ied, unsigned char ie, unsigned char cause, unsigned char *data)
{
	char tmp[256];
	int datalen = data ? strlen(data) + 1 : 1;
	if (datalen > ((int)sizeof(ied->buf) - ied->pos)) {
		snprintf(tmp, (int)sizeof(tmp), "Out of space for ie '%s' (%d), need %d have %d\n", dundi_ie2str(ie), ie, datalen, (int)sizeof(ied->buf) - ied->pos);
		errorf(tmp);
		return -1;
	}
	ied->buf[ied->pos++] = ie;
	ied->buf[ied->pos++] = datalen;
	ied->buf[ied->pos++] = cause;
	memcpy(ied->buf + ied->pos, data, datalen-1);
	ied->pos += datalen-1;
	return 0;
}

int dundi_ie_append_hint(struct dundi_ie_data *ied, unsigned char ie, unsigned short flags, unsigned char *data)
{
	char tmp[256];
	int datalen = data ? strlen(data) + 2 : 2;
	if (datalen > ((int)sizeof(ied->buf) - ied->pos)) {
		snprintf(tmp, (int)sizeof(tmp), "Out of space for ie '%s' (%d), need %d have %d\n", dundi_ie2str(ie), ie, datalen, (int)sizeof(ied->buf) - ied->pos);
		errorf(tmp);
		return -1;
	}
	ied->buf[ied->pos++] = ie;
	ied->buf[ied->pos++] = datalen;
	flags = htons(flags);
	memcpy(ied->buf + ied->pos, &flags, sizeof(flags));
	ied->pos += 2;
	memcpy(ied->buf + ied->pos, data, datalen-1);
	ied->pos += datalen-2;
	return 0;
}

int dundi_ie_append_encdata(struct dundi_ie_data *ied, unsigned char ie, unsigned char *iv, void *data, int datalen)
{
	char tmp[256];
	datalen += 16;
	if (datalen > ((int)sizeof(ied->buf) - ied->pos)) {
		snprintf(tmp, (int)sizeof(tmp), "Out of space for ie '%s' (%d), need %d have %d\n", dundi_ie2str(ie), ie, datalen, (int)sizeof(ied->buf) - ied->pos);
		errorf(tmp);
		return -1;
	}
	ied->buf[ied->pos++] = ie;
	ied->buf[ied->pos++] = datalen;
	memcpy(ied->buf + ied->pos, iv, 16);
	ied->pos += 16;
	if (data) {
		memcpy(ied->buf + ied->pos, data, datalen-16);
		ied->pos += datalen-16;
	}
	return 0;
}

int dundi_ie_append_answer(struct dundi_ie_data *ied, unsigned char ie, dundi_eid *eid, unsigned char protocol, unsigned short flags, unsigned short weight, unsigned char *data)
{
	char tmp[256];
	int datalen = data ? strlen(data) + 11 : 11;
	int x;
	unsigned short myw;
	if (datalen > ((int)sizeof(ied->buf) - ied->pos)) {
		snprintf(tmp, (int)sizeof(tmp), "Out of space for ie '%s' (%d), need %d have %d\n", dundi_ie2str(ie), ie, datalen, (int)sizeof(ied->buf) - ied->pos);
		errorf(tmp);
		return -1;
	}
	ied->buf[ied->pos++] = ie;
	ied->buf[ied->pos++] = datalen;
	for (x=0;x<6;x++)
		ied->buf[ied->pos++] = eid->eid[x];
	ied->buf[ied->pos++] = protocol;
	myw = htons(flags);
	memcpy(ied->buf + ied->pos, &myw, 2);
	ied->pos += 2;
	myw = htons(weight);
	memcpy(ied->buf + ied->pos, &myw, 2);
	ied->pos += 2;
	memcpy(ied->buf + ied->pos, data, datalen-11);
	ied->pos += datalen-11;
	return 0;
}

int dundi_ie_append_addr(struct dundi_ie_data *ied, unsigned char ie, struct sockaddr_in *sin)
{
	return dundi_ie_append_raw(ied, ie, sin, (int)sizeof(struct sockaddr_in));
}

int dundi_ie_append_int(struct dundi_ie_data *ied, unsigned char ie, unsigned int value) 
{
	unsigned int newval;
	newval = htonl(value);
	return dundi_ie_append_raw(ied, ie, &newval, (int)sizeof(newval));
}

int dundi_ie_append_short(struct dundi_ie_data *ied, unsigned char ie, unsigned short value) 
{
	unsigned short newval;
	newval = htons(value);
	return dundi_ie_append_raw(ied, ie, &newval, (int)sizeof(newval));
}

int dundi_ie_append_str(struct dundi_ie_data *ied, unsigned char ie, unsigned char *str)
{
	return dundi_ie_append_raw(ied, ie, str, strlen(str));
}

int dundi_ie_append_eid(struct dundi_ie_data *ied, unsigned char ie, dundi_eid *eid)
{
	return dundi_ie_append_raw(ied, ie, (unsigned char *)eid, sizeof(dundi_eid));
}

int dundi_ie_append_byte(struct dundi_ie_data *ied, unsigned char ie, unsigned char dat)
{
	return dundi_ie_append_raw(ied, ie, &dat, 1);
}

int dundi_ie_append(struct dundi_ie_data *ied, unsigned char ie) 
{
	return dundi_ie_append_raw(ied, ie, NULL, 0);
}

void dundi_set_output(void (*func)(const char *))
{
	outputf = func;
}

void dundi_set_error(void (*func)(const char *))
{
	errorf = func;
}

int dundi_parse_ies(struct dundi_ies *ies, unsigned char *data, int datalen)
{
	/* Parse data into information elements */
	int len;
	int ie;
	char tmp[256];
	memset(ies, 0, (int)sizeof(struct dundi_ies));
	ies->ttl = -1;
	ies->expiration = -1;
	ies->unknowncmd = -1;
	ies->cause = -1;
	while(datalen >= 2) {
		ie = data[0];
		len = data[1];
		if (len > datalen - 2) {
			errorf("Information element length exceeds message size\n");
			return -1;
		}
		switch(ie) {
		case DUNDI_IE_EID:
		case DUNDI_IE_EID_DIRECT:
			if (len != (int)sizeof(dundi_eid)) {
				errorf("Improper entity identifer, expecting 6 bytes!\n");
			} else if (ies->eidcount < DUNDI_MAX_STACK) {
				ies->eids[ies->eidcount] = (dundi_eid *)(data + 2);
				ies->eid_direct[ies->eidcount] = (ie == DUNDI_IE_EID_DIRECT);
				ies->eidcount++;
			} else
				errorf("Too many entities in stack!\n");
			break;
		case DUNDI_IE_REQEID:
			if (len != (int)sizeof(dundi_eid)) {
				errorf("Improper requested entity identifer, expecting 6 bytes!\n");
			} else
				ies->reqeid = (dundi_eid *)(data + 2);
			break;
		case DUNDI_IE_CALLED_CONTEXT:
			ies->called_context = data + 2;
			break;
		case DUNDI_IE_CALLED_NUMBER:
			ies->called_number = data + 2;
			break;
		case DUNDI_IE_ANSWER:
			if (len < sizeof(struct dundi_answer)) {
				snprintf(tmp, (int)sizeof(tmp), "Answer expected to be >=%d bytes long but was %d\n", (int)sizeof(struct dundi_answer), len);
				errorf(tmp);
			} else {
				if (ies->anscount < DUNDI_MAX_ANSWERS)
					ies->answers[ies->anscount++]= (struct dundi_answer *)(data + 2);
				else 
					errorf("Ignoring extra answers!\n");
			}
			break;
		case DUNDI_IE_TTL:
			if (len != (int)sizeof(unsigned short)) {
				snprintf(tmp, (int)sizeof(tmp), "Expecting ttl to be %d bytes long but was %d\n", (int)sizeof(unsigned short), len);
				errorf(tmp);
			} else
				ies->ttl = ntohs(*((unsigned short *)(data + 2)));
			break;
		case DUNDI_IE_VERSION:
			if (len != (int)sizeof(unsigned short)) {
				snprintf(tmp, (int)sizeof(tmp),  "Expecting version to be %d bytes long but was %d\n", (int)sizeof(unsigned short), len);
				errorf(tmp);
			} else
				ies->version = ntohs(*((unsigned short *)(data + 2)));
			break;
		case DUNDI_IE_EXPIRATION:
			if (len != (int)sizeof(unsigned short)) {
				snprintf(tmp, (int)sizeof(tmp),  "Expecting expiration to be %d bytes long but was %d\n", (int)sizeof(unsigned short), len);
				errorf(tmp);
			} else
				ies->expiration = ntohs(*((unsigned short *)(data + 2)));
			break;
		case DUNDI_IE_KEYCRC32:
			if (len != (int)sizeof(unsigned int)) {
				snprintf(tmp, (int)sizeof(tmp),  "Expecting expiration to be %d bytes long but was %d\n", (int)sizeof(unsigned int), len);
				errorf(tmp);
			} else
				ies->keycrc32 = ntohl(*((unsigned int *)(data + 2)));
			break;
		case DUNDI_IE_UNKNOWN:
			if (len == 1)
				ies->unknowncmd = data[2];
			else {
				snprintf(tmp, (int)sizeof(tmp), "Expected single byte Unknown command, but was %d long\n", len);
				errorf(tmp);
			}
			break;
		case DUNDI_IE_CAUSE:
			if (len >= 1) {
				ies->cause = data[2];
				ies->causestr = data + 3;
			} else {
				snprintf(tmp, (int)sizeof(tmp), "Expected at least one byte cause, but was %d long\n", len);
				errorf(tmp);
			}
			break;
		case DUNDI_IE_HINT:
			if (len >= 2) {
				ies->hint = (struct dundi_hint *)(data + 2);
			} else {
				snprintf(tmp, (int)sizeof(tmp), "Expected at least two byte hint, but was %d long\n", len);
				errorf(tmp);
			}
			break;
		case DUNDI_IE_DEPARTMENT:
			ies->q_dept = data + 2;
			break;
		case DUNDI_IE_ORGANIZATION:
			ies->q_org = data + 2;
			break;
		case DUNDI_IE_LOCALITY:
			ies->q_locality = data + 2;
			break;
		case DUNDI_IE_STATE_PROV:
			ies->q_stateprov = data + 2;
			break;
		case DUNDI_IE_COUNTRY:
			ies->q_country = data + 2;
			break;
		case DUNDI_IE_EMAIL:
			ies->q_email = data + 2;
			break;
		case DUNDI_IE_PHONE:
			ies->q_phone = data + 2;
			break;
		case DUNDI_IE_IPADDR:
			ies->q_ipaddr = data + 2;
			break;
		case DUNDI_IE_ENCDATA:
			/* Recalculate len as the remainder of the message, regardless of
			   theoretical length */
			len = datalen - 2;
			if ((len > 16) && !(len % 16)) {
				ies->encblock = (struct dundi_encblock *)(data + 2);
				ies->enclen = len - 16;
			} else {
				snprintf(tmp, (int)sizeof(tmp), "Invalid encrypted data length %d\n", len);
				errorf(tmp);
			}
			break;
		case DUNDI_IE_SHAREDKEY:
			if (len == 128) {
				ies->encsharedkey = (unsigned char *)(data + 2);
			} else {
				snprintf(tmp, (int)sizeof(tmp), "Invalid encrypted shared key length %d\n", len);
				errorf(tmp);
			}
			break;
		case DUNDI_IE_SIGNATURE:
			if (len == 128) {
				ies->encsig = (unsigned char *)(data + 2);
			} else {
				snprintf(tmp, (int)sizeof(tmp), "Invalid encrypted signature length %d\n", len);
				errorf(tmp);
			}
			break;
		case DUNDI_IE_CACHEBYPASS:
			ies->cbypass = 1;
			break;
		default:
			snprintf(tmp, (int)sizeof(tmp), "Ignoring unknown information element '%s' (%d) of length %d\n", dundi_ie2str(ie), ie, len);
			outputf(tmp);
		}
		/* Overwrite information element with 0, to null terminate previous portion */
		data[0] = 0;
		datalen -= (len + 2);
		data += (len + 2);
	}
	/* Null-terminate last field */
	*data = '\0';
	if (datalen) {
		errorf("Invalid information element contents, strange boundary\n");
		return -1;
	}
	return 0;
}
