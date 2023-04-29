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

/*! \file
 * \brief Distributed Universal Number Discovery (DUNDi)
 * See also \arg \ref AstDUNDi
 */

#ifndef _ASTERISK_DUNDI_H
#define _ASTERISK_DUNDI_H

#include "asterisk/channel.h"
#include "asterisk/utils.h"

#define DUNDI_PORT 4520

typedef struct ast_eid dundi_eid;

struct dundi_hdr {
	unsigned short strans;			/*!< Source transaction */
	unsigned short dtrans;			/*!< Destination transaction */
	unsigned char iseqno;			/*!< Next expected incoming sequence number */
	unsigned char oseqno;			/*!< Outgoing sequence number */
	unsigned char cmdresp;			/*!< Command / Response */
	unsigned char cmdflags;			/*!< Command / Response specific flags*/
	unsigned char ies[0];
} __attribute__((__packed__));

struct dundi_ie_hdr {
	unsigned char ie;
	unsigned char len;
	unsigned char iedata[0];
} __attribute__((__packed__));

#define DUNDI_FLAG_RETRANS		(1 << 16)	/*!< Applies to dtrans */
#define DUNDI_FLAG_RESERVED		(1 << 16)	/*!< Applies to strans */

enum {
	/*! No answer yet */
	DUNDI_PROTO_NONE = 0,
	/*! IAX, version 2 */
	DUNDI_PROTO_IAX  = 1,
	/*! SIP - Session Initiation Protocol, RFC 3261 */
	DUNDI_PROTO_SIP  = 2,
	/*! ITU H.323 */
	DUNDI_PROTO_H323 = 3,
	/*! PJSIP */
	DUNDI_PROTO_PJSIP = 4,
};

enum {
	/*! Isn't and can't be a valid number */
	DUNDI_FLAG_NONEXISTENT =    (0),
	/*! Is a valid number */
	DUNDI_FLAG_EXISTS =         (1 << 0),
	/*! Might be valid if you add more digits */
	DUNDI_FLAG_MATCHMORE =      (1 << 1),
	/*! Might be a match */
	DUNDI_FLAG_CANMATCH =       (1 << 2),
	/*! Keep dialtone */
	DUNDI_FLAG_IGNOREPAT =      (1 << 3),
	/*! Destination known to be residential */
	DUNDI_FLAG_RESIDENTIAL =    (1 << 4),
	/*! Destination known to be commercial */
	DUNDI_FLAG_COMMERCIAL =     (1 << 5),
	/*! Destination known to be cellular/mobile */
	DUNDI_FLAG_MOBILE =         (1 << 6),
	/*! No unsolicited calls of any kind through this route */
	DUNDI_FLAG_NOUNSOLICITED =  (1 << 7),
	/*! No commercial unsolicited calls through this route */
	DUNDI_FLAG_NOCOMUNSOLICIT = (1 << 8),
};

enum {
	DUNDI_HINT_NONE =        (0),
	/*! TTL Expired */
	DUNDI_HINT_TTL_EXPIRED = (1 << 0),
	/*! Don't ask for anything beginning with data */
	DUNDI_HINT_DONT_ASK =    (1 << 1),
	/*! Answer not affected by entity list */
	DUNDI_HINT_UNAFFECTED =  (1 << 2),
};

struct dundi_encblock {				/*!< AES-128 encrypted block */
	unsigned char iv[16];			/*!< Initialization vector of random data */
	unsigned char encdata[0];		/*!< Encrypted / compressed data */
} __attribute__((__packed__));

struct dundi_answer {
	dundi_eid eid;				/*!< Original source of answer */
	unsigned char protocol;			/*!< Protocol (DUNDI_PROTO_*) */
	unsigned short flags;			/*!< Flags relating to answer */
	unsigned short weight;			/*!< Weight of answers */
	unsigned char data[0];			/*!< Protocol specific URI */
} __attribute__((__packed__));

struct dundi_hint {
	unsigned short flags;			/*!< Flags relating to answer */
	unsigned char data[0];			/*!< For data for hint */
} __attribute__((__packed__));

enum {
	/*! Success */
	DUNDI_CAUSE_SUCCESS =     0,
	/*! General unspecified failure */
	DUNDI_CAUSE_GENERAL =     1,
	/*! Requested entity is dynamic */
	DUNDI_CAUSE_DYNAMIC =     2,
	/*! No or improper authorization */
	DUNDI_CAUSE_NOAUTH =      3,
	/*! Duplicate request */
	DUNDI_CAUSE_DUPLICATE =   4,
	/*! Expired TTL */
	DUNDI_CAUSE_TTL_EXPIRED = 5,
	/*! Need new session key to decode */
	DUNDI_CAUSE_NEEDKEY =     6,
	/*! Badly encrypted data */
	DUNDI_CAUSE_BADENCRYPT =  7,
};

struct dundi_cause {
	unsigned char causecode;		/*!< Numerical cause (DUNDI_CAUSE_*) */
	char desc[0];				/*!< Textual description */
} __attribute__((__packed__));

struct dundi_peer_status {
	unsigned int flags;
	unsigned short netlag;
	unsigned short querylag;
	dundi_eid peereid;
} __attribute__((__packed__));

enum {
	DUNDI_PEER_PRIMARY =        (1 << 0),
	DUNDI_PEER_SECONDARY =      (1 << 1),
	DUNDI_PEER_UNAVAILABLE =    (1 << 2),
	DUNDI_PEER_REGISTERED =     (1 << 3),
	DUNDI_PEER_MOD_OUTBOUND =   (1 << 4),
	DUNDI_PEER_MOD_INBOUND =    (1 << 5),
	DUNDI_PEER_PCMOD_OUTBOUND = (1 << 6),
	DUNDI_PEER_PCMOD_INBOUND =  (1 << 7),
};

#define DUNDI_COMMAND_FINAL		(0x80)		/*!< Or'd with other flags */

#define DUNDI_COMMAND_ACK		(0 | 0x40)	/*!< Ack a message */
#define DUNDI_COMMAND_DPDISCOVER	1		/*!< Request discovery */
#define DUNDI_COMMAND_DPRESPONSE	(2 | 0x40)	/*!< Respond to a discovery request */
#define DUNDI_COMMAND_EIDQUERY		3		/*!< Request information for a peer */
#define DUNDI_COMMAND_EIDRESPONSE	(4 | 0x40)	/*!< Response to a peer query */
#define DUNDI_COMMAND_PRECACHERQ	5		/*!< Pre-cache Request */
#define DUNDI_COMMAND_PRECACHERP	(6 | 0x40)	/*!< Pre-cache Response */
#define DUNDI_COMMAND_INVALID		(7 | 0x40)	/*!< Invalid dialog state (does not require ack) */
#define DUNDI_COMMAND_UNKNOWN		(8 | 0x40)	/*!< Unknown command */
#define DUNDI_COMMAND_NULL		9		/*!< No-op */
#define DUNDI_COMMAND_REGREQ		(10)		/*!< Register Request */
#define DUNDI_COMMAND_REGRESPONSE	(11 | 0x40)	/*!< Register Response */
#define DUNDI_COMMAND_CANCEL		(12)		/*!< Cancel transaction entirely */
#define DUNDI_COMMAND_ENCRYPT		(13)		/*!< Send an encrypted message */
#define DUNDI_COMMAND_ENCREJ		(14 | 0x40)	/*!< Reject an encrypted message */

#define DUNDI_COMMAND_STATUS		15		/*!< Status command */

/*
 * Remember that some information elements may occur
 * more than one time within a message
 */

#define DUNDI_IE_EID			1	/*!< Entity identifier (dundi_eid) */
#define DUNDI_IE_CALLED_CONTEXT		2	/*!< DUNDi Context (string) */
#define DUNDI_IE_CALLED_NUMBER		3	/*!< Number of equivalent (string) */
#define DUNDI_IE_EID_DIRECT		4	/*!< Entity identifier (dundi_eid), direct connect */
#define DUNDI_IE_ANSWER			5	/*!< An answer (struct dundi_answer) */
#define DUNDI_IE_TTL			6	/*!< Max TTL for this request / Remaining TTL for the response  (short)*/
#define DUNDI_IE_VERSION		10	/*!< DUNDi version (should be 1) (short) */
#define DUNDI_IE_EXPIRATION		11	/*!< Recommended expiration (short) */
#define DUNDI_IE_UNKNOWN		12	/*!< Unknown command (byte) */
#define DUNDI_IE_CAUSE			14	/*!< Success or cause of failure */
#define DUNDI_IE_REQEID			15	/*!< EID being requested for EIDQUERY*/
#define DUNDI_IE_ENCDATA		16	/*!< AES-128 encrypted data */
#define DUNDI_IE_SHAREDKEY		17	/*!< RSA encrypted AES-128 key */
#define DUNDI_IE_SIGNATURE		18	/*!< RSA Signature of encrypted shared key */
#define DUNDI_IE_KEYCRC32		19	/*!< CRC32 of encrypted key (int) */
#define DUNDI_IE_HINT			20	/*!< Answer hints */

#define DUNDI_IE_DEPARTMENT		21	/*!< Department, for EIDQUERY (string) */
#define DUNDI_IE_ORGANIZATION		22	/*!< Organization, for EIDQUERY (string) */
#define DUNDI_IE_LOCALITY		23	/*!< City/Locality, for EIDQUERY (string) */
#define DUNDI_IE_STATE_PROV		24	/*!< State/Province, for EIDQUERY (string) */
#define DUNDI_IE_COUNTRY		25	/*!< Country, for EIDQUERY (string) */
#define DUNDI_IE_EMAIL			26	/*!< E-mail addy, for EIDQUERY (string) */
#define DUNDI_IE_PHONE			27	/*!< Contact Phone, for EIDQUERY (string) */
#define DUNDI_IE_IPADDR			28	/*!< IP Address, for EIDQUERY (string) */
#define DUNDI_IE_CACHEBYPASS		29	/*!< Bypass cache (empty) */

#define DUNDI_IE_PEERSTATUS		30 	/*!< Peer/peer status (struct dundi_peer_status) */

#define DUNDI_FLUFF_TIME		2000	/*!< Amount of time for answer */
#define DUNDI_TTL_TIME			200	/*!< Incremental average time */

#define DUNDI_DEFAULT_RETRANS		5
#define DUNDI_DEFAULT_RETRANS_TIMER	1000
#define DUNDI_DEFAULT_TTL		120	/*!< In seconds/hops like TTL */
#define DUNDI_DEFAULT_VERSION		1
#define DUNDI_DEFAULT_CACHE_TIME	3600	/*!< In seconds */
#define DUNDI_DEFAULT_KEY_EXPIRE	3600	/*!< Life of shared key In seconds */
#define DUNDI_DEF_EMPTY_CACHE_TIME	60	/*!< In seconds, cache of empty answer */
#define DUNDI_WINDOW			1	/*!< Max 1 message in window */

#define DEFAULT_MAXMS			2000

struct dundi_result {
	unsigned int flags;
	int weight;
	int expiration;
	int techint;
	dundi_eid eid;
	char eid_str[20];
	char tech[10];
	char dest[256];
};

struct dundi_entity_info {
	char country[80];
	char stateprov[80];
	char locality[80];
	char org[80];
	char orgunit[80];
	char email[80];
	char phone[80];
	char ipaddr[80];
};

/*!
 * \brief Lookup the given number in the given dundi context.
 * Lookup number in a given dundi context (if unspecified use e164), the given callerid (if specified)
 * and return up to maxret results in the array specified.
 * \retval the number of results found.
 * \retval -1 on a hangup of the channel.
*/
int dundi_lookup(struct dundi_result *result, int maxret, struct ast_channel *chan, const char *dcontext, const char *number, int nocache);

/*! \brief Retrieve information on a specific EID */
int dundi_query_eid(struct dundi_entity_info *dei, const char *dcontext, dundi_eid eid);

/*! \brief Pre-cache to push upstream peers */
int dundi_precache(const char *dcontext, const char *number);

#endif /* _ASTERISK_DUNDI_H */
