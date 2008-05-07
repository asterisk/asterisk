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

#ifndef _DUNDI_PARSER_H
#define _DUNDI_PARSER_H

#include "asterisk/dundi.h"
#include "asterisk/aes.h"

#define DUNDI_MAX_STACK 512
#define DUNDI_MAX_ANSWERS	100

struct dundi_ies {
	dundi_eid *eids[DUNDI_MAX_STACK + 1];
	int eid_direct[DUNDI_MAX_STACK + 1];
	dundi_eid *reqeid;
	int eidcount;
	char *called_context;
	char *called_number;
	struct dundi_answer *answers[DUNDI_MAX_ANSWERS + 1];
	struct dundi_hint *hint;
	int anscount;
	int ttl;
	int version;
	int expiration;
	int unknowncmd;
	unsigned char *pubkey;
	int cause;
	char *q_dept;
	char *q_org;
	char *q_locality;
	char *q_stateprov;
	char *q_country;
	char *q_email;
	char *q_phone;
	char *q_ipaddr;
	char *causestr;
	unsigned char *encsharedkey;
	unsigned char *encsig;
	unsigned long keycrc32;
	struct dundi_encblock *encblock;
	int enclen;
	int cbypass;
};

struct dundi_ie_data {
	int pos;
	unsigned char buf[8192];
};

/* Choose a different function for output */
extern void dundi_set_output(void (*output)(const char *data));
/* Choose a different function for errors */
extern void dundi_set_error(void (*output)(const char *data));
extern void dundi_showframe(struct dundi_hdr *fhi, int rx, struct sockaddr_in *sin, int datalen);

extern const char *dundi_ie2str(int ie);

extern int dundi_ie_append_raw(struct dundi_ie_data *ied, unsigned char ie, void *data, int datalen);
extern int dundi_ie_append_addr(struct dundi_ie_data *ied, unsigned char ie, struct sockaddr_in *sin);
extern int dundi_ie_append_int(struct dundi_ie_data *ied, unsigned char ie, unsigned int value);
extern int dundi_ie_append_short(struct dundi_ie_data *ied, unsigned char ie, unsigned short value);
extern int dundi_ie_append_str(struct dundi_ie_data *ied, unsigned char ie, char *str);
extern int dundi_ie_append_eid(struct dundi_ie_data *ied, unsigned char ie, dundi_eid *eid);
extern int dundi_ie_append_cause(struct dundi_ie_data *ied, unsigned char ie, unsigned char cause, char *desc);
extern int dundi_ie_append_hint(struct dundi_ie_data *ied, unsigned char ie, unsigned short flags, char *data);
extern int dundi_ie_append_answer(struct dundi_ie_data *ied, unsigned char ie, dundi_eid *eid, unsigned char protocol, unsigned short flags, unsigned short weight, char *desc);
extern int dundi_ie_append_encdata(struct dundi_ie_data *ied, unsigned char ie, unsigned char *iv, void *data, int datalen);
extern int dundi_ie_append_byte(struct dundi_ie_data *ied, unsigned char ie, unsigned char dat);
extern int dundi_ie_append(struct dundi_ie_data *ied, unsigned char ie);
extern int dundi_parse_ies(struct dundi_ies *ies, unsigned char *data, int datalen);
extern char *dundi_eid_to_str(char *s, int maxlen, dundi_eid *eid);
extern char *dundi_eid_to_str_short(char *s, int maxlen, dundi_eid *eid);
extern int dundi_str_to_eid(dundi_eid *eid, char *s);
extern int dundi_str_short_to_eid(dundi_eid *eid, char *s);
extern int dundi_eid_zero(dundi_eid *eid);
extern int dundi_eid_cmp(dundi_eid *eid1, dundi_eid *eid2);
extern char *dundi_flags2str(char *s, int maxlen, int flags);
extern char *dundi_hint2str(char *s, int maxlen, int flags);
#endif
