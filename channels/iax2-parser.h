/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Inter-Asterisk eXchange
 * 
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*!\file
 * \brief Implementation of the IAX2 protocol
 */
 
#ifndef _IAX2_PARSER_H
#define _IAX2_PARSER_H

#include "asterisk/linkedlists.h"
#include "asterisk/aes.h"

struct iax_ies {
	char *called_number;
	char *calling_number;
	char *calling_ani;
	char *calling_name;
	int calling_ton;
	int calling_tns;
	int calling_pres;
	char *called_context;
	char *username;
	char *password;
	unsigned int capability;
	unsigned int format;
	char *codec_prefs;
	char *language;
	int version;
	unsigned short adsicpe;
	char *dnid;
	char *rdnis;
	unsigned int authmethods;
	unsigned int encmethods;
	char *challenge;
	char *md5_result;
	char *rsa_result;
	struct sockaddr_in *apparent_addr;
	unsigned short refresh;
	unsigned short dpstatus;
	unsigned short callno;
	char *cause;
	unsigned char causecode;
	unsigned char iax_unknown;
	int msgcount;
	int autoanswer;
	int musiconhold;
	unsigned int transferid;
	unsigned int datetime;
	char *devicetype;
	char *serviceident;
	int firmwarever;
	unsigned int fwdesc;
	unsigned char *fwdata;
	unsigned char fwdatalen;
	unsigned char *enckey;
	unsigned char enckeylen;
	unsigned int provver;
	unsigned short samprate;
	int provverpres;
	unsigned int rr_jitter;
	unsigned int rr_loss;
	unsigned int rr_pkts;
	unsigned short rr_delay;
	unsigned int rr_dropped;
	unsigned int rr_ooo;
	struct ast_variable *vars;
	char *osptokenblock[IAX_MAX_OSPBLOCK_NUM];
	unsigned int ospblocklength[IAX_MAX_OSPBLOCK_NUM];
};

#define DIRECTION_INGRESS 1
#define DIRECTION_OUTGRESS 2

struct iax_frame {
#ifdef LIBIAX
	struct iax_session *session;
	struct iax_event *event;
#else
	int sockfd;
#endif

	/*! /Our/ call number */
	unsigned short callno;
	/*! /Their/ call number */
	unsigned short dcallno;
	/*! Start of raw frame (outgoing only) */
	void *data;
	/*! Length of frame (outgoing only) */
	int datalen;
	/*! How many retries so far? */
	int retries;
	/*! Outgoing relative timestamp (ms) */
	unsigned int ts;
	/*! How long to wait before retrying */
	int retrytime;
	/*! Are we received out of order?  */
	unsigned int outoforder:1;
	/*! Have we been sent at all yet? */
	unsigned int sentyet:1;
	/*! Non-zero if should be sent to transfer peer */
	unsigned int transfer:1;
	/*! Non-zero if this is the final message */
	unsigned int final:1;
	/*! Ingress or outgres */
	unsigned int direction:2;
	/*! Can this frame be cached? */
	unsigned int cacheable:1;
	/*! Outgoing Packet sequence number */
	int oseqno;
	/*! Next expected incoming packet sequence number */
	int iseqno;
	/*! Retransmission ID */
	int retrans;
	/*! is this packet encrypted or not. if set this varible holds encryption methods*/
	int encmethods;
	/*! store encrypt key */
	ast_aes_encrypt_key ecx;
	/*! store decrypt key which corresponds to ecx */
	ast_aes_decrypt_key mydcx;
	/*! random data for encryption pad */
	unsigned char semirand[32];
	/*! Easy linking */
	AST_LIST_ENTRY(iax_frame) list;
	/*! Actual, isolated frame header */
	struct ast_frame af;
	/*! Amount of space _allocated_ for data */
	size_t afdatalen;
	unsigned char unused[AST_FRIENDLY_OFFSET];
	unsigned char afdata[0];	/* Data for frame */
};

struct iax_ie_data {
	unsigned char buf[1024];
	int pos;
};

/* Choose a different function for output */
void iax_set_output(void (*output)(const char *data));
/* Choose a different function for errors */
void iax_set_error(void (*output)(const char *data));
void iax_showframe(struct iax_frame *f, struct ast_iax2_full_hdr *fhi, int rx, struct sockaddr_in *sin, int datalen);
void iax_frame_subclass2str(enum iax_frame_subclass subclass, char *str, size_t len);

const char *iax_ie2str(int ie);

int iax_ie_append_raw(struct iax_ie_data *ied, unsigned char ie, const void *data, int datalen);
int iax_ie_append_addr(struct iax_ie_data *ied, unsigned char ie, const struct sockaddr_in *sin);
int iax_ie_append_int(struct iax_ie_data *ied, unsigned char ie, unsigned int value);
int iax_ie_append_short(struct iax_ie_data *ied, unsigned char ie, unsigned short value);
int iax_ie_append_str(struct iax_ie_data *ied, unsigned char ie, const char *str);
int iax_ie_append_byte(struct iax_ie_data *ied, unsigned char ie, unsigned char dat);
int iax_ie_append(struct iax_ie_data *ied, unsigned char ie);
int iax_parse_ies(struct iax_ies *ies, unsigned char *data, int datalen);

int iax_get_frames(void);
int iax_get_iframes(void);
int iax_get_oframes(void);

void iax_frame_wrap(struct iax_frame *fr, struct ast_frame *f);
struct iax_frame *iax_frame_new(int direction, int datalen, unsigned int cacheable);
void iax_frame_free(struct iax_frame *fr);
#endif
