/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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

/*! 
 * \file 
 *
 * \brief Supports RTP and RTCP with Symmetric RTP support for NAT traversal.
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \note RTP is defined in RFC 3550.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "asterisk/rtp.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/acl.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/unaligned.h"
#include "asterisk/utils.h"

#define MAX_TIMESTAMP_SKEW	640

#define RTP_SEQ_MOD     (1<<16) 	/*!< A sequence number can't be more than 16 bits */
#define RTCP_DEFAULT_INTERVALMS   5000	/*!< Default milli-seconds between RTCP reports we send */
#define RTCP_MIN_INTERVALMS       500	/*!< Min milli-seconds between RTCP reports we send */
#define RTCP_MAX_INTERVALMS       60000	/*!< Max milli-seconds between RTCP reports we send */

#define RTCP_PT_FUR     192
#define RTCP_PT_SR      200
#define RTCP_PT_RR      201
#define RTCP_PT_SDES    202
#define RTCP_PT_BYE     203
#define RTCP_PT_APP     204

#define RTP_MTU		1200

#define DEFAULT_DTMF_TIMEOUT (150 * (8000 / 1000))	/*!< samples */

static int dtmftimeout = DEFAULT_DTMF_TIMEOUT;

static int rtpstart;			/*!< First port for RTP sessions (set in rtp.conf) */
static int rtpend;			/*!< Last port for RTP sessions (set in rtp.conf) */
static int rtpdebug;			/*!< Are we debugging? */
static int rtcpdebug;			/*!< Are we debugging RTCP? */
static int rtcpstats;			/*!< Are we debugging RTCP? */
static int rtcpinterval = RTCP_DEFAULT_INTERVALMS; /*!< Time between rtcp reports in millisecs */
static int stundebug;			/*!< Are we debugging stun? */
static struct sockaddr_in rtpdebugaddr;	/*!< Debug packets to/from this host */
static struct sockaddr_in rtcpdebugaddr;	/*!< Debug RTCP packets to/from this host */
#ifdef SO_NO_CHECK
static int nochecksums;
#endif

/* Uncomment this to enable more intense native bridging, but note: this is currently buggy */
/* #define P2P_INTENSE */

/*!
 * \brief Structure representing a RTP session.
 *
 * RTP session is defined on page 9 of RFC 3550: "An association among a set of participants communicating with RTP.  A participant may be involved in multiple RTP sessions at the same time [...]"
 *
 */
/*! \brief The value of each payload format mapping: */
struct rtpPayloadType {
	int isAstFormat; 	/*!< whether the following code is an AST_FORMAT */
	int code;
};


/*! \brief RTP session description */
struct ast_rtp {
	int s;
	struct ast_frame f;
	unsigned char rawdata[8192 + AST_FRIENDLY_OFFSET];
	unsigned int ssrc;		/*!< Synchronization source, RFC 3550, page 10. */
	unsigned int themssrc;		/*!< Their SSRC */
	unsigned int rxssrc;
	unsigned int lastts;
	unsigned int lastrxts;
	unsigned int lastividtimestamp;
	unsigned int lastovidtimestamp;
	unsigned int lasteventseqn;
	int lastrxseqno;                /*!< Last received sequence number */
	unsigned short seedrxseqno;     /*!< What sequence number did they start with?*/
	unsigned int seedrxts;          /*!< What RTP timestamp did they start with? */
	unsigned int rxcount;           /*!< How many packets have we received? */
	unsigned int rxoctetcount;      /*!< How many octets have we received? should be rxcount *160*/
	unsigned int txcount;           /*!< How many packets have we sent? */
	unsigned int txoctetcount;      /*!< How many octets have we sent? (txcount*160)*/
	unsigned int cycles;            /*!< Shifted count of sequence number cycles */
	double rxjitter;                /*!< Interarrival jitter at the moment */
	double rxtransit;               /*!< Relative transit time for previous packet */
	int lasttxformat;
	int lastrxformat;

	int rtptimeout;			/*!< RTP timeout time (negative or zero means disabled, negative value means temporarily disabled) */
	int rtpholdtimeout;		/*!< RTP timeout when on hold (negative or zero means disabled, negative value means temporarily disabled). */
	int rtpkeepalive;		/*!< Send RTP comfort noice packets for keepalive */

	/* DTMF Reception Variables */
	char resp;
	unsigned int lastevent;
	unsigned int dtmf_duration;	/*!< Total duration in samples since the digit start event */
	unsigned int dtmf_timeout;	/*!< When this timestamp is reached we consider END frame lost and forcibly abort digit */
	/* DTMF Transmission Variables */
	unsigned int lastdigitts;
	char sending_digit;	/*!< boolean - are we sending digits */
	char send_digit;	/*!< digit we are sending */
	int send_payload;
	int send_duration;
	int nat;
	unsigned int flags;
	struct sockaddr_in us;		/*!< Socket representation of the local endpoint. */
	struct sockaddr_in them;	/*!< Socket representation of the remote endpoint. */
	struct timeval rxcore;
	struct timeval txcore;
	double drxcore;                 /*!< The double representation of the first received packet */
	struct timeval lastrx;          /*!< timeval when we last received a packet */
	struct timeval dtmfmute;
	struct ast_smoother *smoother;
	int *ioid;
	unsigned short seqno;		/*!< Sequence number, RFC 3550, page 13. */
	unsigned short rxseqno;
	struct sched_context *sched;
	struct io_context *io;
	void *data;
	ast_rtp_callback callback;
	ast_mutex_t bridge_lock;
	struct rtpPayloadType current_RTP_PT[MAX_RTP_PT];
	int rtp_lookup_code_cache_isAstFormat; /*!< a cache for the result of rtp_lookup_code(): */
	int rtp_lookup_code_cache_code;
	int rtp_lookup_code_cache_result;
	struct ast_rtcp *rtcp;
	struct ast_codec_pref pref;
	struct ast_rtp *bridged;        /*!< Who we are Packet bridged to */
	int set_marker_bit:1;           /*!< Whether to set the marker bit or not */
};

/* Forward declarations */
static int ast_rtcp_write(const void *data);
static void timeval2ntp(struct timeval tv, unsigned int *msw, unsigned int *lsw);
static int ast_rtcp_write_sr(const void *data);
static int ast_rtcp_write_rr(const void *data);
static unsigned int ast_rtcp_calc_interval(struct ast_rtp *rtp);
static int ast_rtp_senddigit_continuation(struct ast_rtp *rtp);
int ast_rtp_senddigit_end(struct ast_rtp *rtp, char digit);

#define FLAG_3389_WARNING		(1 << 0)
#define FLAG_NAT_ACTIVE			(3 << 1)
#define FLAG_NAT_INACTIVE		(0 << 1)
#define FLAG_NAT_INACTIVE_NOWARN	(1 << 1)
#define FLAG_HAS_DTMF			(1 << 3)
#define FLAG_P2P_SENT_MARK              (1 << 4)
#define FLAG_P2P_NEED_DTMF              (1 << 5)
#define FLAG_CALLBACK_MODE              (1 << 6)
#define FLAG_DTMF_COMPENSATE            (1 << 7)
#define FLAG_HAS_STUN                   (1 << 8)

/*!
 * \brief Structure defining an RTCP session.
 * 
 * The concept "RTCP session" is not defined in RFC 3550, but since 
 * this structure is analogous to ast_rtp, which tracks a RTP session, 
 * it is logical to think of this as a RTCP session.
 *
 * RTCP packet is defined on page 9 of RFC 3550.
 * 
 */
struct ast_rtcp {
	int s;				/*!< Socket */
	struct sockaddr_in us;		/*!< Socket representation of the local endpoint. */
	struct sockaddr_in them;	/*!< Socket representation of the remote endpoint. */
	unsigned int soc;		/*!< What they told us */
	unsigned int spc;		/*!< What they told us */
	unsigned int themrxlsr;		/*!< The middle 32 bits of the NTP timestamp in the last received SR*/
	struct timeval rxlsr;		/*!< Time when we got their last SR */
	struct timeval txlsr;		/*!< Time when we sent or last SR*/
	unsigned int expected_prior;	/*!< no. packets in previous interval */
	unsigned int received_prior;	/*!< no. packets received in previous interval */
	int schedid;			/*!< Schedid returned from ast_sched_add() to schedule RTCP-transmissions*/
	unsigned int rr_count;		/*!< number of RRs we've sent, not including report blocks in SR's */
	unsigned int sr_count;		/*!< number of SRs we've sent */
	unsigned int lastsrtxcount;     /*!< Transmit packet count when last SR sent */
	double accumulated_transit;	/*!< accumulated a-dlsr-lsr */
	double rtt;			/*!< Last reported rtt */
	unsigned int reported_jitter;	/*!< The contents of their last jitter entry in the RR */
	unsigned int reported_lost;	/*!< Reported lost packets in their RR */
	char quality[AST_MAX_USER_FIELD];
	double maxrxjitter;
	double minrxjitter;
	double maxrtt;
	double minrtt;
	int sendfur;
};


typedef struct { unsigned int id[4]; } __attribute__((packed)) stun_trans_id;

/* XXX Maybe stun belongs in another file if it ever has use outside of RTP */
struct stun_header {
	unsigned short msgtype;
	unsigned short msglen;
	stun_trans_id  id;
	unsigned char ies[0];
} __attribute__((packed));

struct stun_attr {
	unsigned short attr;
	unsigned short len;
	unsigned char value[0];
} __attribute__((packed));

struct stun_addr {
	unsigned char unused;
	unsigned char family;
	unsigned short port;
	unsigned int addr;
} __attribute__((packed));

#define STUN_IGNORE		(0)
#define STUN_ACCEPT		(1)

#define STUN_BINDREQ	0x0001
#define STUN_BINDRESP	0x0101
#define STUN_BINDERR	0x0111
#define STUN_SECREQ	0x0002
#define STUN_SECRESP	0x0102
#define STUN_SECERR	0x0112

#define STUN_MAPPED_ADDRESS	0x0001
#define STUN_RESPONSE_ADDRESS	0x0002
#define STUN_CHANGE_REQUEST	0x0003
#define STUN_SOURCE_ADDRESS	0x0004
#define STUN_CHANGED_ADDRESS	0x0005
#define STUN_USERNAME		0x0006
#define STUN_PASSWORD		0x0007
#define STUN_MESSAGE_INTEGRITY	0x0008
#define STUN_ERROR_CODE		0x0009
#define STUN_UNKNOWN_ATTRIBUTES	0x000a
#define STUN_REFLECTED_FROM	0x000b

static const char *stun_msg2str(int msg)
{
	switch(msg) {
	case STUN_BINDREQ:
		return "Binding Request";
	case STUN_BINDRESP:
		return "Binding Response";
	case STUN_BINDERR:
		return "Binding Error Response";
	case STUN_SECREQ:
		return "Shared Secret Request";
	case STUN_SECRESP:
		return "Shared Secret Response";
	case STUN_SECERR:
		return "Shared Secret Error Response";
	}
	return "Non-RFC3489 Message";
}

static const char *stun_attr2str(int msg)
{
	switch(msg) {
	case STUN_MAPPED_ADDRESS:
		return "Mapped Address";
	case STUN_RESPONSE_ADDRESS:
		return "Response Address";
	case STUN_CHANGE_REQUEST:
		return "Change Request";
	case STUN_SOURCE_ADDRESS:
		return "Source Address";
	case STUN_CHANGED_ADDRESS:
		return "Changed Address";
	case STUN_USERNAME:
		return "Username";
	case STUN_PASSWORD:
		return "Password";
	case STUN_MESSAGE_INTEGRITY:
		return "Message Integrity";
	case STUN_ERROR_CODE:
		return "Error Code";
	case STUN_UNKNOWN_ATTRIBUTES:
		return "Unknown Attributes";
	case STUN_REFLECTED_FROM:
		return "Reflected From";
	}
	return "Non-RFC3489 Attribute";
}

struct stun_state {
	const char *username;
	const char *password;
};

static int stun_process_attr(struct stun_state *state, struct stun_attr *attr)
{
	if (stundebug)
		ast_verbose("Found STUN Attribute %s (%04x), length %d\n",
			stun_attr2str(ntohs(attr->attr)), ntohs(attr->attr), ntohs(attr->len));
	switch(ntohs(attr->attr)) {
	case STUN_USERNAME:
		state->username = (const char *) (attr->value);
		break;
	case STUN_PASSWORD:
		state->password = (const char *) (attr->value);
		break;
	default:
		if (stundebug)
			ast_verbose("Ignoring STUN attribute %s (%04x), length %d\n", 
				stun_attr2str(ntohs(attr->attr)), ntohs(attr->attr), ntohs(attr->len));
	}
	return 0;
}

static void append_attr_string(struct stun_attr **attr, int attrval, const char *s, int *len, int *left)
{
	int size = sizeof(**attr) + strlen(s);
	if (*left > size) {
		(*attr)->attr = htons(attrval);
		(*attr)->len = htons(strlen(s));
		memcpy((*attr)->value, s, strlen(s));
		(*attr) = (struct stun_attr *)((*attr)->value + strlen(s));
		*len += size;
		*left -= size;
	}
}

static void append_attr_address(struct stun_attr **attr, int attrval, struct sockaddr_in *sin, int *len, int *left)
{
	int size = sizeof(**attr) + 8;
	struct stun_addr *addr;
	if (*left > size) {
		(*attr)->attr = htons(attrval);
		(*attr)->len = htons(8);
		addr = (struct stun_addr *)((*attr)->value);
		addr->unused = 0;
		addr->family = 0x01;
		addr->port = sin->sin_port;
		addr->addr = sin->sin_addr.s_addr;
		(*attr) = (struct stun_attr *)((*attr)->value + 8);
		*len += size;
		*left -= size;
	}
}

static int stun_send(int s, struct sockaddr_in *dst, struct stun_header *resp)
{
	return sendto(s, resp, ntohs(resp->msglen) + sizeof(*resp), 0,
		(struct sockaddr *)dst, sizeof(*dst));
}

static void stun_req_id(struct stun_header *req)
{
	int x;
	for (x=0;x<4;x++)
		req->id.id[x] = ast_random();
}

size_t ast_rtp_alloc_size(void)
{
	return sizeof(struct ast_rtp);
}

void ast_rtp_stun_request(struct ast_rtp *rtp, struct sockaddr_in *suggestion, const char *username)
{
	struct stun_header *req;
	unsigned char reqdata[1024];
	int reqlen, reqleft;
	struct stun_attr *attr;

	req = (struct stun_header *)reqdata;
	stun_req_id(req);
	reqlen = 0;
	reqleft = sizeof(reqdata) - sizeof(struct stun_header);
	req->msgtype = 0;
	req->msglen = 0;
	attr = (struct stun_attr *)req->ies;
	if (username)
		append_attr_string(&attr, STUN_USERNAME, username, &reqlen, &reqleft);
	req->msglen = htons(reqlen);
	req->msgtype = htons(STUN_BINDREQ);
	stun_send(rtp->s, suggestion, req);
}

static int stun_handle_packet(int s, struct sockaddr_in *src, unsigned char *data, size_t len)
{
	struct stun_header *resp, *hdr = (struct stun_header *)data;
	struct stun_attr *attr;
	struct stun_state st;
	int ret = STUN_IGNORE;	
	unsigned char respdata[1024];
	int resplen, respleft;
	
	if (len < sizeof(struct stun_header)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Runt STUN packet (only %zd, wanting at least %zd)\n", len, sizeof(struct stun_header));
		return -1;
	}
	if (stundebug)
		ast_verbose("STUN Packet, msg %s (%04x), length: %d\n", stun_msg2str(ntohs(hdr->msgtype)), ntohs(hdr->msgtype), ntohs(hdr->msglen));
	if (ntohs(hdr->msglen) > len - sizeof(struct stun_header)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Scrambled STUN packet length (got %d, expecting %zd)\n", ntohs(hdr->msglen), len - sizeof(struct stun_header));
	} else
		len = ntohs(hdr->msglen);
	data += sizeof(struct stun_header);
	memset(&st, 0, sizeof(st));
	while(len) {
		if (len < sizeof(struct stun_attr)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Runt Attribute (got %zd, expecting %zd)\n", len, sizeof(struct stun_attr));
			break;
		}
		attr = (struct stun_attr *)data;
		if ((ntohs(attr->len) + sizeof(struct stun_attr)) > len) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Inconsistent Attribute (length %d exceeds remaining msg len %d)\n", (int) (ntohs(attr->len) + sizeof(struct stun_attr)), (int) len);
			break;
		}
		if (stun_process_attr(&st, attr)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Failed to handle attribute %s (%04x)\n", stun_attr2str(ntohs(attr->attr)), ntohs(attr->attr));
			break;
		}
		/* Clear attribute in case previous entry was a string */
		attr->attr = 0;
		data += ntohs(attr->len) + sizeof(struct stun_attr);
		len -= ntohs(attr->len) + sizeof(struct stun_attr);
	}
	/* Null terminate any string */
	*data = '\0';
	resp = (struct stun_header *)respdata;
	resplen = 0;
	respleft = sizeof(respdata) - sizeof(struct stun_header);
	resp->id = hdr->id;
	resp->msgtype = 0;
	resp->msglen = 0;
	attr = (struct stun_attr *)resp->ies;
	if (!len) {
		switch(ntohs(hdr->msgtype)) {
		case STUN_BINDREQ:
			if (stundebug)
				ast_verbose("STUN Bind Request, username: %s\n", 
					st.username ? st.username : "<none>");
			if (st.username)
				append_attr_string(&attr, STUN_USERNAME, st.username, &resplen, &respleft);
			append_attr_address(&attr, STUN_MAPPED_ADDRESS, src, &resplen, &respleft);
			resp->msglen = htons(resplen);
			resp->msgtype = htons(STUN_BINDRESP);
			stun_send(s, src, resp);
			ret = STUN_ACCEPT;
			break;
		default:
			if (stundebug)
				ast_verbose("Dunno what to do with STUN message %04x (%s)\n", ntohs(hdr->msgtype), stun_msg2str(ntohs(hdr->msgtype)));
		}
	}
	return ret;
}

/*! \brief List of current sessions */
static AST_LIST_HEAD_STATIC(protos, ast_rtp_protocol);

static void timeval2ntp(struct timeval tv, unsigned int *msw, unsigned int *lsw)
{
	unsigned int sec, usec, frac;
	sec = tv.tv_sec + 2208988800u; /* Sec between 1900 and 1970 */
	usec = tv.tv_usec;
	frac = (usec << 12) + (usec << 8) - ((usec * 3650) >> 6);
	*msw = sec;
	*lsw = frac;
}

int ast_rtp_fd(struct ast_rtp *rtp)
{
	return rtp->s;
}

int ast_rtcp_fd(struct ast_rtp *rtp)
{
	if (rtp->rtcp)
		return rtp->rtcp->s;
	return -1;
}

unsigned int ast_rtcp_calc_interval(struct ast_rtp *rtp)
{
	unsigned int interval;
	/*! \todo XXX Do a more reasonable calculation on this one
	* Look in RFC 3550 Section A.7 for an example*/
	interval = rtcpinterval;
	return interval;
}

/* \brief Put RTP timeout timers on hold during another transaction, like T.38 */
void ast_rtp_set_rtptimers_onhold(struct ast_rtp *rtp)
{
	rtp->rtptimeout = (-1) * rtp->rtptimeout;
	rtp->rtpholdtimeout = (-1) * rtp->rtpholdtimeout;
}

/*! \brief Set rtp timeout */
void ast_rtp_set_rtptimeout(struct ast_rtp *rtp, int timeout)
{
	rtp->rtptimeout = timeout;
}

/*! \brief Set rtp hold timeout */
void ast_rtp_set_rtpholdtimeout(struct ast_rtp *rtp, int timeout)
{
	rtp->rtpholdtimeout = timeout;
}

/*! \brief set RTP keepalive interval */
void ast_rtp_set_rtpkeepalive(struct ast_rtp *rtp, int period)
{
	rtp->rtpkeepalive = period;
}

/*! \brief Get rtp timeout */
int ast_rtp_get_rtptimeout(struct ast_rtp *rtp)
{
	if (rtp->rtptimeout < 0)	/* We're not checking, but remembering the setting (during T.38 transmission) */
		return 0;
	return rtp->rtptimeout;
}

/*! \brief Get rtp hold timeout */
int ast_rtp_get_rtpholdtimeout(struct ast_rtp *rtp)
{
	if (rtp->rtptimeout < 0)	/* We're not checking, but remembering the setting (during T.38 transmission) */
		return 0;
	return rtp->rtpholdtimeout;
}

/*! \brief Get RTP keepalive interval */
int ast_rtp_get_rtpkeepalive(struct ast_rtp *rtp)
{
	return rtp->rtpkeepalive;
}

void ast_rtp_set_data(struct ast_rtp *rtp, void *data)
{
	rtp->data = data;
}

void ast_rtp_set_callback(struct ast_rtp *rtp, ast_rtp_callback callback)
{
	rtp->callback = callback;
}

void ast_rtp_setnat(struct ast_rtp *rtp, int nat)
{
	rtp->nat = nat;
}

int ast_rtp_getnat(struct ast_rtp *rtp)
{
	return ast_test_flag(rtp, FLAG_NAT_ACTIVE);
}

void ast_rtp_setdtmf(struct ast_rtp *rtp, int dtmf)
{
	ast_set2_flag(rtp, dtmf ? 1 : 0, FLAG_HAS_DTMF);
}

void ast_rtp_setdtmfcompensate(struct ast_rtp *rtp, int compensate)
{
	ast_set2_flag(rtp, compensate ? 1 : 0, FLAG_DTMF_COMPENSATE);
}

void ast_rtp_setstun(struct ast_rtp *rtp, int stun_enable)
{
	ast_set2_flag(rtp, stun_enable ? 1 : 0, FLAG_HAS_STUN);
}

static struct ast_frame *send_dtmf(struct ast_rtp *rtp, enum ast_frame_type type)
{
	if (((ast_test_flag(rtp, FLAG_DTMF_COMPENSATE) && type == AST_FRAME_DTMF_END) ||
	     (type == AST_FRAME_DTMF_BEGIN)) && ast_tvcmp(ast_tvnow(), rtp->dtmfmute) < 0) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Ignore potential DTMF echo from '%s'\n", ast_inet_ntoa(rtp->them.sin_addr));
		rtp->resp = 0;
		return &ast_null_frame;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Sending dtmf: %d (%c), at %s\n", rtp->resp, rtp->resp, ast_inet_ntoa(rtp->them.sin_addr));
	if (rtp->resp == 'X') {
		rtp->f.frametype = AST_FRAME_CONTROL;
		rtp->f.subclass = AST_CONTROL_FLASH;
	} else {
		rtp->f.frametype = type;
		rtp->f.subclass = rtp->resp;
	}
	rtp->f.datalen = 0;
	rtp->f.samples = 0;
	rtp->f.mallocd = 0;
	rtp->f.src = "RTP";
	return &rtp->f;
	
}

static inline int rtp_debug_test_addr(struct sockaddr_in *addr)
{
	if (rtpdebug == 0)
		return 0;
	if (rtpdebugaddr.sin_addr.s_addr) {
		if (((ntohs(rtpdebugaddr.sin_port) != 0)
			&& (rtpdebugaddr.sin_port != addr->sin_port))
			|| (rtpdebugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
		return 0;
	}
	return 1;
}

static inline int rtcp_debug_test_addr(struct sockaddr_in *addr)
{
	if (rtcpdebug == 0)
		return 0;
	if (rtcpdebugaddr.sin_addr.s_addr) {
		if (((ntohs(rtcpdebugaddr.sin_port) != 0)
			&& (rtcpdebugaddr.sin_port != addr->sin_port))
			|| (rtcpdebugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
		return 0;
	}
	return 1;
}


static struct ast_frame *process_cisco_dtmf(struct ast_rtp *rtp, unsigned char *data, int len)
{
	unsigned int event;
	char resp = 0;
	struct ast_frame *f = NULL;
	event = ntohl(*((unsigned int *)(data)));
	event &= 0x001F;
	if (option_debug > 2 || rtpdebug)
		ast_log(LOG_DEBUG, "Cisco DTMF Digit: %08x (len = %d)\n", event, len);
	if (event < 10) {
		resp = '0' + event;
	} else if (event < 11) {
		resp = '*';
	} else if (event < 12) {
		resp = '#';
	} else if (event < 16) {
		resp = 'A' + (event - 12);
	} else if (event < 17) {
		resp = 'X';
	}
	if (rtp->resp && (rtp->resp != resp)) {
		f = send_dtmf(rtp, AST_FRAME_DTMF_END);
	}
	rtp->resp = resp;
	rtp->dtmf_timeout = 0;
	return f;
}

/*! 
 * \brief Process RTP DTMF and events according to RFC 2833.
 * 
 * RFC 2833 is "RTP Payload for DTMF Digits, Telephony Tones and Telephony Signals".
 * 
 * \param rtp
 * \param data
 * \param len
 * \param seqno
 * \returns
 */
static struct ast_frame *process_rfc2833(struct ast_rtp *rtp, unsigned char *data, int len, unsigned int seqno, unsigned int timestamp)
{
	unsigned int event;
	unsigned int event_end;
	unsigned int samples;
	char resp = 0;
	struct ast_frame *f = NULL;

	/* Figure out event, event end, and samples */
	event = ntohl(*((unsigned int *)(data)));
	event >>= 24;
	event_end = ntohl(*((unsigned int *)(data)));
	event_end <<= 8;
	event_end >>= 24;
	samples = ntohl(*((unsigned int *)(data)));
	samples &= 0xFFFF;

	/* Print out debug if turned on */
	if (rtpdebug || option_debug > 2)
		ast_log(LOG_DEBUG, "- RTP 2833 Event: %08x (len = %d)\n", event, len);

	/* Figure out what digit was pressed */
	if (event < 10) {
		resp = '0' + event;
	} else if (event < 11) {
		resp = '*';
	} else if (event < 12) {
		resp = '#';
	} else if (event < 16) {
		resp = 'A' + (event - 12);
	} else if (event < 17) {	/* Event 16: Hook flash */
		resp = 'X';	
	} else {
		/* Not a supported event */
		ast_log(LOG_DEBUG, "Ignoring RTP 2833 Event: %08x. Not a DTMF Digit.\n", event);
		return &ast_null_frame;
	}

	if (ast_test_flag(rtp, FLAG_DTMF_COMPENSATE)) {
		if ((rtp->lastevent != timestamp) || (rtp->resp && rtp->resp != resp)) {
			rtp->resp = resp;
			rtp->dtmf_timeout = 0;
			f = send_dtmf(rtp, AST_FRAME_DTMF_END);
			f->len = 0;
			rtp->lastevent = timestamp;
		}
	} else {
		/*  The duration parameter measures the complete
		    duration of the event (from the beginning) - RFC2833.
		    Account for the fact that duration is only 16 bits long
		    (about 8 seconds at 8000 Hz) and can wrap if digit
		    is held for too long. */
		unsigned int new_duration = rtp->dtmf_duration;
		unsigned int last_duration = new_duration & 0xFFFF;

		if (last_duration > 64000 && samples < last_duration)
			new_duration += 0xFFFF + 1;
		new_duration = (new_duration & ~0xFFFF) | samples;

		if (event_end & 0x80) {
			/* End event */
			if ((rtp->lastevent != seqno) && rtp->resp) {
				rtp->dtmf_duration = new_duration;
				f = send_dtmf(rtp, AST_FRAME_DTMF_END);
				f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, 8000), ast_tv(0, 0));
				rtp->resp = 0;
				rtp->dtmf_duration = rtp->dtmf_timeout = 0;
			}
		} else {
			/* Begin/continuation */

			if (rtp->resp && rtp->resp != resp) {
				/* Another digit already began. End it */
				f = send_dtmf(rtp, AST_FRAME_DTMF_END);
				f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, 8000), ast_tv(0, 0));
				rtp->resp = 0;
				rtp->dtmf_duration = rtp->dtmf_timeout = 0;
			}


			if (rtp->resp) {
				/* Digit continues */
				rtp->dtmf_duration = new_duration;
			} else {
				/* New digit began */
				rtp->resp = resp;
				f = send_dtmf(rtp, AST_FRAME_DTMF_BEGIN);
				rtp->dtmf_duration = samples;
			}

			rtp->dtmf_timeout = timestamp + rtp->dtmf_duration + dtmftimeout;
		}

		rtp->lastevent = seqno;
	}

	return f;
}

/*!
 * \brief Process Comfort Noise RTP.
 * 
 * This is incomplete at the moment.
 * 
*/
static struct ast_frame *process_rfc3389(struct ast_rtp *rtp, unsigned char *data, int len)
{
	struct ast_frame *f = NULL;
	/* Convert comfort noise into audio with various codecs.  Unfortunately this doesn't
	   totally help us out becuase we don't have an engine to keep it going and we are not
	   guaranteed to have it every 20ms or anything */
	if (rtpdebug)
		ast_log(LOG_DEBUG, "- RTP 3389 Comfort noise event: Level %d (len = %d)\n", rtp->lastrxformat, len);

	if (!(ast_test_flag(rtp, FLAG_3389_WARNING))) {
		ast_log(LOG_NOTICE, "Comfort noise support incomplete in Asterisk (RFC 3389). Please turn off on client if possible. Client IP: %s\n",
			ast_inet_ntoa(rtp->them.sin_addr));
		ast_set_flag(rtp, FLAG_3389_WARNING);
	}

	/* Must have at least one byte */
	if (!len)
		return NULL;
	if (len < 24) {
		rtp->f.data = rtp->rawdata + AST_FRIENDLY_OFFSET;
		rtp->f.datalen = len - 1;
		rtp->f.offset = AST_FRIENDLY_OFFSET;
		memcpy(rtp->f.data, data + 1, len - 1);
	} else {
		rtp->f.data = NULL;
		rtp->f.offset = 0;
		rtp->f.datalen = 0;
	}
	rtp->f.frametype = AST_FRAME_CNG;
	rtp->f.subclass = data[0] & 0x7f;
	rtp->f.datalen = len - 1;
	rtp->f.samples = 0;
	rtp->f.delivery.tv_usec = rtp->f.delivery.tv_sec = 0;
	f = &rtp->f;
	return f;
}

static int rtpread(int *id, int fd, short events, void *cbdata)
{
	struct ast_rtp *rtp = cbdata;
	struct ast_frame *f;
	f = ast_rtp_read(rtp);
	if (f) {
		if (rtp->callback)
			rtp->callback(rtp, f, rtp->data);
	}
	return 1;
}

struct ast_frame *ast_rtcp_read(struct ast_rtp *rtp)
{
	socklen_t len;
	int position, i, packetwords;
	int res;
	struct sockaddr_in sin;
	unsigned int rtcpdata[8192 + AST_FRIENDLY_OFFSET];
	unsigned int *rtcpheader;
	int pt;
	struct timeval now;
	unsigned int length;
	int rc;
	double rttsec;
	uint64_t rtt = 0;
	unsigned int dlsr;
	unsigned int lsr;
	unsigned int msw;
	unsigned int lsw;
	unsigned int comp;
	struct ast_frame *f = &ast_null_frame;
	
	if (!rtp || !rtp->rtcp)
		return &ast_null_frame;

	len = sizeof(sin);
	
	res = recvfrom(rtp->rtcp->s, rtcpdata + AST_FRIENDLY_OFFSET, sizeof(rtcpdata) - sizeof(unsigned int) * AST_FRIENDLY_OFFSET,
					0, (struct sockaddr *)&sin, &len);
	rtcpheader = (unsigned int *)(rtcpdata + AST_FRIENDLY_OFFSET);
	
	if (res < 0) {
		ast_assert(errno != EBADF);
		if (errno != EAGAIN) {
			ast_log(LOG_WARNING, "RTCP Read error: %s.  Hanging up.\n", strerror(errno));
			return NULL;
		}
		return &ast_null_frame;
	}

	packetwords = res / 4;
	
	if (rtp->nat) {
		/* Send to whoever sent to us */
		if ((rtp->rtcp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (rtp->rtcp->them.sin_port != sin.sin_port)) {
			memcpy(&rtp->rtcp->them, &sin, sizeof(rtp->rtcp->them));
			if (option_debug || rtpdebug)
				ast_log(LOG_DEBUG, "RTCP NAT: Got RTCP from other end. Now sending to address %s:%d\n", ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
		}
	}

	if (option_debug)
		ast_log(LOG_DEBUG, "Got RTCP report of %d bytes\n", res);

	/* Process a compound packet */
	position = 0;
	while (position < packetwords) {
		i = position;
		length = ntohl(rtcpheader[i]);
		pt = (length & 0xff0000) >> 16;
		rc = (length & 0x1f000000) >> 24;
		length &= 0xffff;
    
		if ((i + length) > packetwords) {
			ast_log(LOG_WARNING, "RTCP Read too short\n");
			return &ast_null_frame;
		}
		
		if (rtcp_debug_test_addr(&sin)) {
		  	ast_verbose("\n\nGot RTCP from %s:%d\n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
		  	ast_verbose("PT: %d(%s)\n", pt, (pt == 200) ? "Sender Report" : (pt == 201) ? "Receiver Report" : (pt == 192) ? "H.261 FUR" : "Unknown");
		  	ast_verbose("Reception reports: %d\n", rc);
		  	ast_verbose("SSRC of sender: %u\n", rtcpheader[i + 1]);
		}
    
		i += 2; /* Advance past header and ssrc */
		
		switch (pt) {
		case RTCP_PT_SR:
			gettimeofday(&rtp->rtcp->rxlsr,NULL); /* To be able to populate the dlsr */
			rtp->rtcp->spc = ntohl(rtcpheader[i+3]);
			rtp->rtcp->soc = ntohl(rtcpheader[i + 4]);
			rtp->rtcp->themrxlsr = ((ntohl(rtcpheader[i]) & 0x0000ffff) << 16) | ((ntohl(rtcpheader[i + 1]) & 0xffff0000) >> 16); /* Going to LSR in RR*/
    
			if (rtcp_debug_test_addr(&sin)) {
				ast_verbose("NTP timestamp: %lu.%010lu\n", (unsigned long) ntohl(rtcpheader[i]), (unsigned long) ntohl(rtcpheader[i + 1]) * 4096);
				ast_verbose("RTP timestamp: %lu\n", (unsigned long) ntohl(rtcpheader[i + 2]));
				ast_verbose("SPC: %lu\tSOC: %lu\n", (unsigned long) ntohl(rtcpheader[i + 3]), (unsigned long) ntohl(rtcpheader[i + 4]));
			}
			i += 5;
			if (rc < 1)
				break;
			/* Intentional fall through */
		case RTCP_PT_RR:
			/* Don't handle multiple reception reports (rc > 1) yet */
			/* Calculate RTT per RFC */
			gettimeofday(&now, NULL);
			timeval2ntp(now, &msw, &lsw);
			if (ntohl(rtcpheader[i + 4]) && ntohl(rtcpheader[i + 5])) { /* We must have the LSR && DLSR */
				comp = ((msw & 0xffff) << 16) | ((lsw & 0xffff0000) >> 16);
				lsr = ntohl(rtcpheader[i + 4]);
				dlsr = ntohl(rtcpheader[i + 5]);
				rtt = comp - lsr - dlsr;

				/* Convert end to end delay to usec (keeping the calculation in 64bit space)
				   sess->ee_delay = (eedelay * 1000) / 65536; */
				if (rtt < 4294) {
				    rtt = (rtt * 1000000) >> 16;
				} else {
				    rtt = (rtt * 1000) >> 16;
				    rtt *= 1000;
				}
				rtt = rtt / 1000.;
				rttsec = rtt / 1000.;

				if (comp - dlsr >= lsr) {
					rtp->rtcp->accumulated_transit += rttsec;
					rtp->rtcp->rtt = rttsec;
					if (rtp->rtcp->maxrtt<rttsec)
						rtp->rtcp->maxrtt = rttsec;
					if (rtp->rtcp->minrtt>rttsec)
						rtp->rtcp->minrtt = rttsec;
				} else if (rtcp_debug_test_addr(&sin)) {
					ast_verbose("Internal RTCP NTP clock skew detected: "
							   "lsr=%u, now=%u, dlsr=%u (%d:%03dms), "
							   "diff=%d\n",
							   lsr, comp, dlsr, dlsr / 65536,
							   (dlsr % 65536) * 1000 / 65536,
							   dlsr - (comp - lsr));
				}
			}

			rtp->rtcp->reported_jitter = ntohl(rtcpheader[i + 3]);
			rtp->rtcp->reported_lost = ntohl(rtcpheader[i + 1]) & 0xffffff;
			if (rtcp_debug_test_addr(&sin)) {
				ast_verbose("  Fraction lost: %ld\n", (((long) ntohl(rtcpheader[i + 1]) & 0xff000000) >> 24));
				ast_verbose("  Packets lost so far: %d\n", rtp->rtcp->reported_lost);
				ast_verbose("  Highest sequence number: %ld\n", (long) (ntohl(rtcpheader[i + 2]) & 0xffff));
				ast_verbose("  Sequence number cycles: %ld\n", (long) (ntohl(rtcpheader[i + 2]) & 0xffff) >> 16);
				ast_verbose("  Interarrival jitter: %u\n", rtp->rtcp->reported_jitter);
				ast_verbose("  Last SR(our NTP): %lu.%010lu\n",(unsigned long) ntohl(rtcpheader[i + 4]) >> 16,((unsigned long) ntohl(rtcpheader[i + 4]) << 16) * 4096);
				ast_verbose("  DLSR: %4.4f (sec)\n",ntohl(rtcpheader[i + 5])/65536.0);
				if (rtt)
					ast_verbose("  RTT: %lu(sec)\n", (unsigned long) rtt);
			}
			break;
		case RTCP_PT_FUR:
			if (rtcp_debug_test_addr(&sin))
				ast_verbose("Received an RTCP Fast Update Request\n");
			rtp->f.frametype = AST_FRAME_CONTROL;
			rtp->f.subclass = AST_CONTROL_VIDUPDATE;
			rtp->f.datalen = 0;
			rtp->f.samples = 0;
			rtp->f.mallocd = 0;
			rtp->f.src = "RTP";
			f = &rtp->f;
			break;
		case RTCP_PT_SDES:
			if (rtcp_debug_test_addr(&sin))
				ast_verbose("Received an SDES from %s:%d\n", ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
			break;
		case RTCP_PT_BYE:
			if (rtcp_debug_test_addr(&sin))
				ast_verbose("Received a BYE from %s:%d\n", ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
			break;
		default:
			if (option_debug)
				ast_log(LOG_DEBUG, "Unknown RTCP packet (pt=%d) received from %s:%d\n", pt, ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
			break;
		}
		position += (length + 1);
	}
			
	return f;
}

static void calc_rxstamp(struct timeval *tv, struct ast_rtp *rtp, unsigned int timestamp, int mark)
{
	struct timeval now;
	double transit;
	double current_time;
	double d;
	double dtv;
	double prog;
	
	if ((!rtp->rxcore.tv_sec && !rtp->rxcore.tv_usec) || mark) {
		gettimeofday(&rtp->rxcore, NULL);
		rtp->drxcore = (double) rtp->rxcore.tv_sec + (double) rtp->rxcore.tv_usec / 1000000;
		/* map timestamp to a real time */
		rtp->seedrxts = timestamp; /* Their RTP timestamp started with this */
		rtp->rxcore.tv_sec -= timestamp / 8000;
		rtp->rxcore.tv_usec -= (timestamp % 8000) * 125;
		/* Round to 0.1ms for nice, pretty timestamps */
		rtp->rxcore.tv_usec -= rtp->rxcore.tv_usec % 100;
		if (rtp->rxcore.tv_usec < 0) {
			/* Adjust appropriately if necessary */
			rtp->rxcore.tv_usec += 1000000;
			rtp->rxcore.tv_sec -= 1;
		}
	}

	gettimeofday(&now,NULL);
	/* rxcore is the mapping between the RTP timestamp and _our_ real time from gettimeofday() */
	tv->tv_sec = rtp->rxcore.tv_sec + timestamp / 8000;
	tv->tv_usec = rtp->rxcore.tv_usec + (timestamp % 8000) * 125;
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec += 1;
	}
	prog = (double)((timestamp-rtp->seedrxts)/8000.);
	dtv = (double)rtp->drxcore + (double)(prog);
	current_time = (double)now.tv_sec + (double)now.tv_usec/1000000;
	transit = current_time - dtv;
	d = transit - rtp->rxtransit;
	rtp->rxtransit = transit;
	if (d<0)
		d=-d;
	rtp->rxjitter += (1./16.) * (d - rtp->rxjitter);
	if (rtp->rtcp && rtp->rxjitter > rtp->rtcp->maxrxjitter)
		rtp->rtcp->maxrxjitter = rtp->rxjitter;
	if (rtp->rtcp && rtp->rxjitter < rtp->rtcp->minrxjitter)
		rtp->rtcp->minrxjitter = rtp->rxjitter;
}

/*! \brief Perform a Packet2Packet RTP write */
static int bridge_p2p_rtp_write(struct ast_rtp *rtp, struct ast_rtp *bridged, unsigned int *rtpheader, int len, int hdrlen)
{
	int res = 0, payload = 0, bridged_payload = 0, mark;
	struct rtpPayloadType rtpPT;
	int reconstruct = ntohl(rtpheader[0]);

	/* Get fields from packet */
	payload = (reconstruct & 0x7f0000) >> 16;
	mark = (((reconstruct & 0x800000) >> 23) != 0);

	/* Check what the payload value should be */
	rtpPT = ast_rtp_lookup_pt(rtp, payload);

	/* If the payload is DTMF, and we are listening for DTMF - then feed it into the core */
	if (ast_test_flag(rtp, FLAG_P2P_NEED_DTMF) && !rtpPT.isAstFormat && rtpPT.code == AST_RTP_DTMF)
		return -1;

	/* Otherwise adjust bridged payload to match */
	bridged_payload = ast_rtp_lookup_code(bridged, rtpPT.isAstFormat, rtpPT.code);

	/* If the payload coming in is not one of the negotiated ones then send it to the core, this will cause formats to change and the bridge to break */
	if (!bridged->current_RTP_PT[bridged_payload].code)
		return -1;


	/* If the mark bit has not been sent yet... do it now */
	if (!ast_test_flag(rtp, FLAG_P2P_SENT_MARK)) {
		mark = 1;
		ast_set_flag(rtp, FLAG_P2P_SENT_MARK);
	}

	/* Reconstruct part of the packet */
	reconstruct &= 0xFF80FFFF;
	reconstruct |= (bridged_payload << 16);
	reconstruct |= (mark << 23);
	rtpheader[0] = htonl(reconstruct);

	/* Send the packet back out */
	res = sendto(bridged->s, (void *)rtpheader, len, 0, (struct sockaddr *)&bridged->them, sizeof(bridged->them));
	if (res < 0) {
		if (!bridged->nat || (bridged->nat && (ast_test_flag(bridged, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE))) {
			ast_log(LOG_DEBUG, "RTP Transmission error of packet to %s:%d: %s\n", ast_inet_ntoa(bridged->them.sin_addr), ntohs(bridged->them.sin_port), strerror(errno));
		} else if (((ast_test_flag(bridged, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug) && !ast_test_flag(bridged, FLAG_NAT_INACTIVE_NOWARN)) {
			if (option_debug || rtpdebug)
				ast_log(LOG_DEBUG, "RTP NAT: Can't write RTP to private address %s:%d, waiting for other end to send audio...\n", ast_inet_ntoa(bridged->them.sin_addr), ntohs(bridged->them.sin_port));
			ast_set_flag(bridged, FLAG_NAT_INACTIVE_NOWARN);
		}
		return 0;
	} else if (rtp_debug_test_addr(&bridged->them))
			ast_verbose("Sent RTP P2P packet to %s:%u (type %-2.2d, len %-6.6u)\n", ast_inet_ntoa(bridged->them.sin_addr), ntohs(bridged->them.sin_port), bridged_payload, len - hdrlen);

	return 0;
}

struct ast_frame *ast_rtp_read(struct ast_rtp *rtp)
{
	int res;
	struct sockaddr_in sin;
	socklen_t len;
	unsigned int seqno;
	int version;
	int payloadtype;
	int hdrlen = 12;
	int padding;
	int mark;
	int ext;
	int cc;
	unsigned int ssrc;
	unsigned int timestamp;
	unsigned int *rtpheader;
	struct rtpPayloadType rtpPT;
	struct ast_rtp *bridged = NULL;
	
	/* If time is up, kill it */
	if (rtp->sending_digit)
		ast_rtp_senddigit_continuation(rtp);

	len = sizeof(sin);
	
	/* Cache where the header will go */
	res = recvfrom(rtp->s, rtp->rawdata + AST_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - AST_FRIENDLY_OFFSET,
					0, (struct sockaddr *)&sin, &len);

	rtpheader = (unsigned int *)(rtp->rawdata + AST_FRIENDLY_OFFSET);
	if (res < 0) {
		ast_assert(errno != EBADF);
		if (errno != EAGAIN) {
			ast_log(LOG_WARNING, "RTP Read error: %s.  Hanging up.\n", strerror(errno));
			return NULL;
		}
		return &ast_null_frame;
	}
	
	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short\n");
		return &ast_null_frame;
	}

	/* Get fields */
	seqno = ntohl(rtpheader[0]);

	/* Check RTP version */
	version = (seqno & 0xC0000000) >> 30;
	if (!version) {
		if ((stun_handle_packet(rtp->s, &sin, rtp->rawdata + AST_FRIENDLY_OFFSET, res) == STUN_ACCEPT) &&
			(!rtp->them.sin_port && !rtp->them.sin_addr.s_addr)) {
			memcpy(&rtp->them, &sin, sizeof(rtp->them));
		}
		return &ast_null_frame;
	}

#if 0	/* Allow to receive RTP stream with closed transmission path */
	/* If we don't have the other side's address, then ignore this */
	if (!rtp->them.sin_addr.s_addr || !rtp->them.sin_port)
		return &ast_null_frame;
#endif

	/* Send to whoever send to us if NAT is turned on */
	if (rtp->nat) {
		if ((rtp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (rtp->them.sin_port != sin.sin_port)) {
			rtp->them = sin;
			if (rtp->rtcp) {
				memcpy(&rtp->rtcp->them, &sin, sizeof(rtp->rtcp->them));
				rtp->rtcp->them.sin_port = htons(ntohs(rtp->them.sin_port)+1);
			}
			rtp->rxseqno = 0;
			ast_set_flag(rtp, FLAG_NAT_ACTIVE);
			if (option_debug || rtpdebug)
				ast_log(LOG_DEBUG, "RTP NAT: Got audio from other end. Now sending to address %s:%d\n", ast_inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port));
		}
	}

	/* If we are bridged to another RTP stream, send direct */
	if ((bridged = ast_rtp_get_bridged(rtp)) && !bridge_p2p_rtp_write(rtp, bridged, rtpheader, res, hdrlen))
		return &ast_null_frame;

	if (version != 2)
		return &ast_null_frame;

	payloadtype = (seqno & 0x7f0000) >> 16;
	padding = seqno & (1 << 29);
	mark = seqno & (1 << 23);
	ext = seqno & (1 << 28);
	cc = (seqno & 0xF000000) >> 24;
	seqno &= 0xffff;
	timestamp = ntohl(rtpheader[1]);
	ssrc = ntohl(rtpheader[2]);
	
	if (!mark && rtp->rxssrc && rtp->rxssrc != ssrc) {
		if (option_debug || rtpdebug)
			ast_log(LOG_DEBUG, "Forcing Marker bit, because SSRC has changed\n");
		mark = 1;
	}

	rtp->rxssrc = ssrc;
	
	if (padding) {
		/* Remove padding bytes */
		res -= rtp->rawdata[AST_FRIENDLY_OFFSET + res - 1];
	}
	
	if (cc) {
		/* CSRC fields present */
		hdrlen += cc*4;
	}

	if (ext) {
		/* RTP Extension present */
		hdrlen += (ntohl(rtpheader[hdrlen/4]) & 0xffff) << 2;
		hdrlen += 4;
	}

	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short (%d, expecting %d)\n", res, hdrlen);
		return &ast_null_frame;
	}

	rtp->rxcount++; /* Only count reasonably valid packets, this'll make the rtcp stats more accurate */

	if (rtp->rxcount==1) {
		/* This is the first RTP packet successfully received from source */
		rtp->seedrxseqno = seqno;
	}

	/* Do not schedule RR if RTCP isn't run */
	if (rtp->rtcp && rtp->rtcp->them.sin_addr.s_addr && rtp->rtcp->schedid < 1) {
		/* Schedule transmission of Receiver Report */
		rtp->rtcp->schedid = ast_sched_add(rtp->sched, ast_rtcp_calc_interval(rtp), ast_rtcp_write, rtp);
	}
	if ( (int)rtp->lastrxseqno - (int)seqno  > 100) /* if so it would indicate that the sender cycled; allow for misordering */
		rtp->cycles += RTP_SEQ_MOD;

	rtp->lastrxseqno = seqno;
	
	if (rtp->themssrc==0)
		rtp->themssrc = ntohl(rtpheader[2]); /* Record their SSRC to put in future RR */
	
	if (rtp_debug_test_addr(&sin))
		ast_verbose("Got  RTP packet from    %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
			ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp,res - hdrlen);

	rtpPT = ast_rtp_lookup_pt(rtp, payloadtype);
	if (!rtpPT.isAstFormat) {
		struct ast_frame *f = NULL;

		/* This is special in-band data that's not one of our codecs */
		if (rtpPT.code == AST_RTP_DTMF) {
			/* It's special -- rfc2833 process it */
			if (rtp_debug_test_addr(&sin)) {
				unsigned char *data;
				unsigned int event;
				unsigned int event_end;
				unsigned int duration;
				data = rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen;
				event = ntohl(*((unsigned int *)(data)));
				event >>= 24;
				event_end = ntohl(*((unsigned int *)(data)));
				event_end <<= 8;
				event_end >>= 24;
				duration = ntohl(*((unsigned int *)(data)));
				duration &= 0xFFFF;
				ast_verbose("Got  RTP RFC2833 from   %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u, mark %d, event %08x, end %d, duration %-5.5d) \n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp, res - hdrlen, (mark?1:0), event, ((event_end & 0x80)?1:0), duration);
			}
			f = process_rfc2833(rtp, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp);
		} else if (rtpPT.code == AST_RTP_CISCO_DTMF) {
			/* It's really special -- process it the Cisco way */
			if (rtp->lastevent <= seqno || (rtp->lastevent >= 65530 && seqno <= 6)) {
				f = process_cisco_dtmf(rtp, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
				rtp->lastevent = seqno;
			}
		} else if (rtpPT.code == AST_RTP_CN) {
			/* Comfort Noise */
			f = process_rfc3389(rtp, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
		} else {
			ast_log(LOG_NOTICE, "Unknown RTP codec %d received from '%s'\n", payloadtype, ast_inet_ntoa(rtp->them.sin_addr));
		}
		return f ? f : &ast_null_frame;
	}
	rtp->lastrxformat = rtp->f.subclass = rtpPT.code;
	rtp->f.frametype = (rtp->f.subclass < AST_FORMAT_MAX_AUDIO) ? AST_FRAME_VOICE : AST_FRAME_VIDEO;

	rtp->rxseqno = seqno;

	if (rtp->dtmf_timeout && rtp->dtmf_timeout < timestamp) {
		rtp->dtmf_timeout = 0;

		if (rtp->resp) {
			struct ast_frame *f;
			f = send_dtmf(rtp, AST_FRAME_DTMF_END);
			f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, 8000), ast_tv(0, 0));
			rtp->resp = 0;
			rtp->dtmf_timeout = rtp->dtmf_duration = 0;
			return f;
		}
	}

	/* Record received timestamp as last received now */
	rtp->lastrxts = timestamp;

	rtp->f.mallocd = 0;
	rtp->f.datalen = res - hdrlen;
	rtp->f.data = rtp->rawdata + hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.offset = hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.seqno = seqno;
	if (rtp->f.subclass < AST_FORMAT_MAX_AUDIO) {
		rtp->f.samples = ast_codec_get_samples(&rtp->f);
		if (rtp->f.subclass == AST_FORMAT_SLINEAR) 
			ast_frame_byteswap_be(&rtp->f);
		calc_rxstamp(&rtp->f.delivery, rtp, timestamp, mark);
		/* Add timing data to let ast_generic_bridge() put the frame into a jitterbuf */
		ast_set_flag(&rtp->f, AST_FRFLAG_HAS_TIMING_INFO);
		rtp->f.ts = timestamp / 8;
		rtp->f.len = rtp->f.samples / (ast_format_rate(rtp->f.subclass) / 1000);
	} else {
		/* Video -- samples is # of samples vs. 90000 */
		if (!rtp->lastividtimestamp)
			rtp->lastividtimestamp = timestamp;
		rtp->f.samples = timestamp - rtp->lastividtimestamp;
		rtp->lastividtimestamp = timestamp;
		rtp->f.delivery.tv_sec = 0;
		rtp->f.delivery.tv_usec = 0;
		if (mark)
			rtp->f.subclass |= 0x1;
	}
	rtp->f.src = "RTP";
	return &rtp->f;
}

/* The following array defines the MIME Media type (and subtype) for each
   of our codecs, or RTP-specific data type. */
static struct {
	struct rtpPayloadType payloadType;
	char* type;
	char* subtype;
} mimeTypes[] = {
	{{1, AST_FORMAT_G723_1}, "audio", "G723"},
	{{1, AST_FORMAT_GSM}, "audio", "GSM"},
	{{1, AST_FORMAT_ULAW}, "audio", "PCMU"},
	{{1, AST_FORMAT_ULAW}, "audio", "G711U"},
	{{1, AST_FORMAT_ALAW}, "audio", "PCMA"},
	{{1, AST_FORMAT_ALAW}, "audio", "G711A"},
	{{1, AST_FORMAT_G726}, "audio", "G726-32"},
	{{1, AST_FORMAT_ADPCM}, "audio", "DVI4"},
	{{1, AST_FORMAT_SLINEAR}, "audio", "L16"},
	{{1, AST_FORMAT_LPC10}, "audio", "LPC"},
	{{1, AST_FORMAT_G729A}, "audio", "G729"},
	{{1, AST_FORMAT_G729A}, "audio", "G729A"},
	{{1, AST_FORMAT_G729A}, "audio", "G.729"},
	{{1, AST_FORMAT_SPEEX}, "audio", "speex"},
	{{1, AST_FORMAT_ILBC}, "audio", "iLBC"},
	{{1, AST_FORMAT_G722}, "audio", "G722"},
	{{1, AST_FORMAT_G726_AAL2}, "audio", "AAL2-G726-32"},
	{{0, AST_RTP_DTMF}, "audio", "telephone-event"},
	{{0, AST_RTP_CISCO_DTMF}, "audio", "cisco-telephone-event"},
	{{0, AST_RTP_CN}, "audio", "CN"},
	{{1, AST_FORMAT_JPEG}, "video", "JPEG"},
	{{1, AST_FORMAT_PNG}, "video", "PNG"},
	{{1, AST_FORMAT_H261}, "video", "H261"},
	{{1, AST_FORMAT_H263}, "video", "H263"},
	{{1, AST_FORMAT_H263_PLUS}, "video", "h263-1998"},
	{{1, AST_FORMAT_H264}, "video", "H264"},
};

/* Static (i.e., well-known) RTP payload types for our "AST_FORMAT..."s:
   also, our own choices for dynamic payload types.  This is our master
   table for transmission */
static struct rtpPayloadType static_RTP_PT[MAX_RTP_PT] = {
	[0] = {1, AST_FORMAT_ULAW},
#ifdef USE_DEPRECATED_G726
	[2] = {1, AST_FORMAT_G726}, /* Technically this is G.721, but if Cisco can do it, so can we... */
#endif
	[3] = {1, AST_FORMAT_GSM},
	[4] = {1, AST_FORMAT_G723_1},
	[5] = {1, AST_FORMAT_ADPCM}, /* 8 kHz */
	[6] = {1, AST_FORMAT_ADPCM}, /* 16 kHz */
	[7] = {1, AST_FORMAT_LPC10},
	[8] = {1, AST_FORMAT_ALAW},
	[9] = {1, AST_FORMAT_G722},
	[10] = {1, AST_FORMAT_SLINEAR}, /* 2 channels */
	[11] = {1, AST_FORMAT_SLINEAR}, /* 1 channel */
	[13] = {0, AST_RTP_CN},
	[16] = {1, AST_FORMAT_ADPCM}, /* 11.025 kHz */
	[17] = {1, AST_FORMAT_ADPCM}, /* 22.050 kHz */
	[18] = {1, AST_FORMAT_G729A},
	[19] = {0, AST_RTP_CN},		/* Also used for CN */
	[26] = {1, AST_FORMAT_JPEG},
	[31] = {1, AST_FORMAT_H261},
	[34] = {1, AST_FORMAT_H263},
	[103] = {1, AST_FORMAT_H263_PLUS},
	[97] = {1, AST_FORMAT_ILBC},
	[99] = {1, AST_FORMAT_H264},
	[101] = {0, AST_RTP_DTMF},
	[110] = {1, AST_FORMAT_SPEEX},
	[111] = {1, AST_FORMAT_G726},
	[112] = {1, AST_FORMAT_G726_AAL2},
	[121] = {0, AST_RTP_CISCO_DTMF}, /* Must be type 121 */
};

void ast_rtp_pt_clear(struct ast_rtp* rtp) 
{
	int i;

	if (!rtp)
		return;

	ast_mutex_lock(&rtp->bridge_lock);

	for (i = 0; i < MAX_RTP_PT; ++i) {
		rtp->current_RTP_PT[i].isAstFormat = 0;
		rtp->current_RTP_PT[i].code = 0;
	}

	rtp->rtp_lookup_code_cache_isAstFormat = 0;
	rtp->rtp_lookup_code_cache_code = 0;
	rtp->rtp_lookup_code_cache_result = 0;

	ast_mutex_unlock(&rtp->bridge_lock);
}

void ast_rtp_pt_default(struct ast_rtp* rtp) 
{
	int i;

	ast_mutex_lock(&rtp->bridge_lock);

	/* Initialize to default payload types */
	for (i = 0; i < MAX_RTP_PT; ++i) {
		rtp->current_RTP_PT[i].isAstFormat = static_RTP_PT[i].isAstFormat;
		rtp->current_RTP_PT[i].code = static_RTP_PT[i].code;
	}

	rtp->rtp_lookup_code_cache_isAstFormat = 0;
	rtp->rtp_lookup_code_cache_code = 0;
	rtp->rtp_lookup_code_cache_result = 0;

	ast_mutex_unlock(&rtp->bridge_lock);
}

void ast_rtp_pt_copy(struct ast_rtp *dest, struct ast_rtp *src)
{
	unsigned int i;

	ast_mutex_lock(&dest->bridge_lock);
	ast_mutex_lock(&src->bridge_lock);

	for (i=0; i < MAX_RTP_PT; ++i) {
		dest->current_RTP_PT[i].isAstFormat = 
			src->current_RTP_PT[i].isAstFormat;
		dest->current_RTP_PT[i].code = 
			src->current_RTP_PT[i].code; 
	}
	dest->rtp_lookup_code_cache_isAstFormat = 0;
	dest->rtp_lookup_code_cache_code = 0;
	dest->rtp_lookup_code_cache_result = 0;

	ast_mutex_unlock(&src->bridge_lock);
	ast_mutex_unlock(&dest->bridge_lock);
}

/*! \brief Get channel driver interface structure */
static struct ast_rtp_protocol *get_proto(struct ast_channel *chan)
{
	struct ast_rtp_protocol *cur = NULL;

	AST_LIST_LOCK(&protos);
	AST_LIST_TRAVERSE(&protos, cur, list) {
		if (cur->type == chan->tech->type)
			break;
	}
	AST_LIST_UNLOCK(&protos);

	return cur;
}

int ast_rtp_early_bridge(struct ast_channel *dest, struct ast_channel *src)
{
	struct ast_rtp *destp = NULL, *srcp = NULL;		/* Audio RTP Channels */
	struct ast_rtp *vdestp = NULL, *vsrcp = NULL;		/* Video RTP channels */
	struct ast_rtp_protocol *destpr = NULL, *srcpr = NULL;
	enum ast_rtp_get_result audio_dest_res = AST_RTP_GET_FAILED, video_dest_res = AST_RTP_GET_FAILED;
	enum ast_rtp_get_result audio_src_res = AST_RTP_GET_FAILED, video_src_res = AST_RTP_GET_FAILED;
	int srccodec, destcodec, nat_active = 0;

	/* Lock channels */
	ast_channel_lock(dest);
	if (src) {
		while(ast_channel_trylock(src)) {
			ast_channel_unlock(dest);
			usleep(1);
			ast_channel_lock(dest);
		}
	}

	/* Find channel driver interfaces */
	destpr = get_proto(dest);
	if (src)
		srcpr = get_proto(src);
	if (!destpr) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Channel '%s' has no RTP, not doing anything\n", dest->name);
		ast_channel_unlock(dest);
		if (src)
			ast_channel_unlock(src);
		return 0;
	}
	if (!srcpr) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Channel '%s' has no RTP, not doing anything\n", src ? src->name : "<unspecified>");
		ast_channel_unlock(dest);
		if (src)
			ast_channel_unlock(src);
		return 0;
	}

	/* Get audio and video interface (if native bridge is possible) */
	audio_dest_res = destpr->get_rtp_info(dest, &destp);
	video_dest_res = destpr->get_vrtp_info ? destpr->get_vrtp_info(dest, &vdestp) : AST_RTP_GET_FAILED;
	if (srcpr) {
		audio_src_res = srcpr->get_rtp_info(src, &srcp);
		video_src_res = srcpr->get_vrtp_info ? srcpr->get_vrtp_info(src, &vsrcp) : AST_RTP_GET_FAILED;
	}

	/* Check if bridge is still possible (In SIP canreinvite=no stops this, like NAT) */
	if (audio_dest_res != AST_RTP_TRY_NATIVE || (video_dest_res != AST_RTP_GET_FAILED && video_dest_res != AST_RTP_TRY_NATIVE)) {
		/* Somebody doesn't want to play... */
		ast_channel_unlock(dest);
		if (src)
			ast_channel_unlock(src);
		return 0;
	}
	if (audio_src_res == AST_RTP_TRY_NATIVE && (video_src_res == AST_RTP_GET_FAILED || video_src_res == AST_RTP_TRY_NATIVE) && srcpr->get_codec)
		srccodec = srcpr->get_codec(src);
	else
		srccodec = 0;
	if (audio_dest_res == AST_RTP_TRY_NATIVE && (video_dest_res == AST_RTP_GET_FAILED || video_dest_res == AST_RTP_TRY_NATIVE) && destpr->get_codec)
		destcodec = destpr->get_codec(dest);
	else
		destcodec = 0;
	/* Ensure we have at least one matching codec */
	if (srcp && !(srccodec & destcodec)) {
		ast_channel_unlock(dest);
		ast_channel_unlock(src);
		return 0;
	}
	/* Consider empty media as non-existant */
	if (audio_src_res == AST_RTP_TRY_NATIVE && !srcp->them.sin_addr.s_addr)
		srcp = NULL;
	/* If the client has NAT stuff turned on then just safe NAT is active */
	if (srcp && (srcp->nat || ast_test_flag(srcp, FLAG_NAT_ACTIVE)))
		nat_active = 1;
	/* Bridge media early */
	if (destpr->set_rtp_peer(dest, srcp, vsrcp, srccodec, nat_active))
		ast_log(LOG_WARNING, "Channel '%s' failed to setup early bridge to '%s'\n", dest->name, src ? src->name : "<unspecified>");
	ast_channel_unlock(dest);
	if (src)
		ast_channel_unlock(src);
	if (option_debug)
		ast_log(LOG_DEBUG, "Setting early bridge SDP of '%s' with that of '%s'\n", dest->name, src ? src->name : "<unspecified>");
	return 1;
}

int ast_rtp_make_compatible(struct ast_channel *dest, struct ast_channel *src, int media)
{
	struct ast_rtp *destp = NULL, *srcp = NULL;		/* Audio RTP Channels */
	struct ast_rtp *vdestp = NULL, *vsrcp = NULL;		/* Video RTP channels */
	struct ast_rtp_protocol *destpr = NULL, *srcpr = NULL;
	enum ast_rtp_get_result audio_dest_res = AST_RTP_GET_FAILED, video_dest_res = AST_RTP_GET_FAILED;
	enum ast_rtp_get_result audio_src_res = AST_RTP_GET_FAILED, video_src_res = AST_RTP_GET_FAILED; 
	int srccodec, destcodec;

	/* Lock channels */
	ast_channel_lock(dest);
	while(ast_channel_trylock(src)) {
		ast_channel_unlock(dest);
		usleep(1);
		ast_channel_lock(dest);
	}

	/* Find channel driver interfaces */
	if (!(destpr = get_proto(dest))) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Channel '%s' has no RTP, not doing anything\n", dest->name);
		ast_channel_unlock(dest);
		ast_channel_unlock(src);
		return 0;
	}
	if (!(srcpr = get_proto(src))) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Channel '%s' has no RTP, not doing anything\n", src->name);
		ast_channel_unlock(dest);
		ast_channel_unlock(src);
		return 0;
	}

	/* Get audio and video interface (if native bridge is possible) */
	audio_dest_res = destpr->get_rtp_info(dest, &destp);
	video_dest_res = destpr->get_vrtp_info ? destpr->get_vrtp_info(dest, &vdestp) : AST_RTP_GET_FAILED;
	audio_src_res = srcpr->get_rtp_info(src, &srcp);
	video_src_res = srcpr->get_vrtp_info ? srcpr->get_vrtp_info(src, &vsrcp) : AST_RTP_GET_FAILED;

	/* Ensure we have at least one matching codec */
	if (srcpr->get_codec)
		srccodec = srcpr->get_codec(src);
	else
		srccodec = 0;
	if (destpr->get_codec)
		destcodec = destpr->get_codec(dest);
	else
		destcodec = 0;

	/* Check if bridge is still possible (In SIP canreinvite=no stops this, like NAT) */
	if (audio_dest_res != AST_RTP_TRY_NATIVE || (video_dest_res != AST_RTP_GET_FAILED && video_dest_res != AST_RTP_TRY_NATIVE) || audio_src_res != AST_RTP_TRY_NATIVE || (video_src_res != AST_RTP_GET_FAILED && video_src_res != AST_RTP_TRY_NATIVE) || !(srccodec & destcodec)) {
		/* Somebody doesn't want to play... */
		ast_channel_unlock(dest);
		ast_channel_unlock(src);
		return 0;
	}
	ast_rtp_pt_copy(destp, srcp);
	if (vdestp && vsrcp)
		ast_rtp_pt_copy(vdestp, vsrcp);
	if (media) {
		/* Bridge early */
		if (destpr->set_rtp_peer(dest, srcp, vsrcp, srccodec, ast_test_flag(srcp, FLAG_NAT_ACTIVE)))
			ast_log(LOG_WARNING, "Channel '%s' failed to setup early bridge to '%s'\n", dest->name, src->name);
	}
	ast_channel_unlock(dest);
	ast_channel_unlock(src);
	if (option_debug)
		ast_log(LOG_DEBUG, "Seeded SDP of '%s' with that of '%s'\n", dest->name, src->name);
	return 1;
}

/*! \brief  Make a note of a RTP payload type that was seen in a SDP "m=" line.
 * By default, use the well-known value for this type (although it may 
 * still be set to a different value by a subsequent "a=rtpmap:" line)
 */
void ast_rtp_set_m_type(struct ast_rtp* rtp, int pt) 
{
	if (pt < 0 || pt > MAX_RTP_PT || static_RTP_PT[pt].code == 0) 
		return; /* bogus payload type */

	ast_mutex_lock(&rtp->bridge_lock);
	rtp->current_RTP_PT[pt] = static_RTP_PT[pt];
	ast_mutex_unlock(&rtp->bridge_lock);
} 

/*! \brief remove setting from payload type list if the rtpmap header indicates
    an unknown media type */
void ast_rtp_unset_m_type(struct ast_rtp* rtp, int pt) 
{
	if (pt < 0 || pt > MAX_RTP_PT)
		return; /* bogus payload type */

	ast_mutex_lock(&rtp->bridge_lock);
	rtp->current_RTP_PT[pt].isAstFormat = 0;
	rtp->current_RTP_PT[pt].code = 0;
	ast_mutex_unlock(&rtp->bridge_lock);
}

/*! \brief Make a note of a RTP payload type (with MIME type) that was seen in
 * an SDP "a=rtpmap:" line.
 * \return 0 if the MIME type was found and set, -1 if it wasn't found
 */
int ast_rtp_set_rtpmap_type(struct ast_rtp *rtp, int pt,
			     char *mimeType, char *mimeSubtype,
			     enum ast_rtp_options options)
{
	unsigned int i;
	int found = 0;

	if (pt < 0 || pt > MAX_RTP_PT) 
		return -1; /* bogus payload type */
	
	ast_mutex_lock(&rtp->bridge_lock);

	for (i = 0; i < sizeof(mimeTypes)/sizeof(mimeTypes[0]); ++i) {
		if (strcasecmp(mimeSubtype, mimeTypes[i].subtype) == 0 &&
		    strcasecmp(mimeType, mimeTypes[i].type) == 0) {
			found = 1;
			rtp->current_RTP_PT[pt] = mimeTypes[i].payloadType;
			if ((mimeTypes[i].payloadType.code == AST_FORMAT_G726) &&
			    mimeTypes[i].payloadType.isAstFormat &&
			    (options & AST_RTP_OPT_G726_NONSTANDARD))
				rtp->current_RTP_PT[pt].code = AST_FORMAT_G726_AAL2;
			break;
		}
	}

	ast_mutex_unlock(&rtp->bridge_lock);

	return (found ? 0 : -1);
} 

/*! \brief Return the union of all of the codecs that were set by rtp_set...() calls 
 * They're returned as two distinct sets: AST_FORMATs, and AST_RTPs */
void ast_rtp_get_current_formats(struct ast_rtp* rtp,
				 int* astFormats, int* nonAstFormats)
{
	int pt;
	
	ast_mutex_lock(&rtp->bridge_lock);
	
	*astFormats = *nonAstFormats = 0;
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
		if (rtp->current_RTP_PT[pt].isAstFormat) {
			*astFormats |= rtp->current_RTP_PT[pt].code;
		} else {
			*nonAstFormats |= rtp->current_RTP_PT[pt].code;
		}
	}
	
	ast_mutex_unlock(&rtp->bridge_lock);
	
	return;
}

struct rtpPayloadType ast_rtp_lookup_pt(struct ast_rtp* rtp, int pt) 
{
	struct rtpPayloadType result;

	result.isAstFormat = result.code = 0;

	if (pt < 0 || pt > MAX_RTP_PT) 
		return result; /* bogus payload type */

	/* Start with negotiated codecs */
	ast_mutex_lock(&rtp->bridge_lock);
	result = rtp->current_RTP_PT[pt];
	ast_mutex_unlock(&rtp->bridge_lock);

	/* If it doesn't exist, check our static RTP type list, just in case */
	if (!result.code) 
		result = static_RTP_PT[pt];

	return result;
}

/*! \brief Looks up an RTP code out of our *static* outbound list */
int ast_rtp_lookup_code(struct ast_rtp* rtp, const int isAstFormat, const int code)
{
	int pt = 0;

	ast_mutex_lock(&rtp->bridge_lock);

	if (isAstFormat == rtp->rtp_lookup_code_cache_isAstFormat &&
		code == rtp->rtp_lookup_code_cache_code) {
		/* Use our cached mapping, to avoid the overhead of the loop below */
		pt = rtp->rtp_lookup_code_cache_result;
		ast_mutex_unlock(&rtp->bridge_lock);
		return pt;
	}

	/* Check the dynamic list first */
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
		if (rtp->current_RTP_PT[pt].code == code && rtp->current_RTP_PT[pt].isAstFormat == isAstFormat) {
			rtp->rtp_lookup_code_cache_isAstFormat = isAstFormat;
			rtp->rtp_lookup_code_cache_code = code;
			rtp->rtp_lookup_code_cache_result = pt;
			ast_mutex_unlock(&rtp->bridge_lock);
			return pt;
		}
	}

	/* Then the static list */
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
		if (static_RTP_PT[pt].code == code && static_RTP_PT[pt].isAstFormat == isAstFormat) {
			rtp->rtp_lookup_code_cache_isAstFormat = isAstFormat;
  			rtp->rtp_lookup_code_cache_code = code;
			rtp->rtp_lookup_code_cache_result = pt;
			ast_mutex_unlock(&rtp->bridge_lock);
			return pt;
		}
	}

	ast_mutex_unlock(&rtp->bridge_lock);

	return -1;
}

const char *ast_rtp_lookup_mime_subtype(const int isAstFormat, const int code,
				  enum ast_rtp_options options)
{
	unsigned int i;

	for (i = 0; i < sizeof(mimeTypes)/sizeof(mimeTypes[0]); ++i) {
		if ((mimeTypes[i].payloadType.code == code) && (mimeTypes[i].payloadType.isAstFormat == isAstFormat)) {
			if (isAstFormat &&
			    (code == AST_FORMAT_G726_AAL2) &&
			    (options & AST_RTP_OPT_G726_NONSTANDARD))
				return "G726-32";
			else
				return mimeTypes[i].subtype;
		}
	}

	return "";
}

char *ast_rtp_lookup_mime_multiple(char *buf, size_t size, const int capability,
				   const int isAstFormat, enum ast_rtp_options options)
{
	int format;
	unsigned len;
	char *end = buf;
	char *start = buf;

	if (!buf || !size)
		return NULL;

	snprintf(end, size, "0x%x (", capability);

	len = strlen(end);
	end += len;
	size -= len;
	start = end;

	for (format = 1; format < AST_RTP_MAX; format <<= 1) {
		if (capability & format) {
			const char *name = ast_rtp_lookup_mime_subtype(isAstFormat, format, options);

			snprintf(end, size, "%s|", name);
			len = strlen(end);
			end += len;
			size -= len;
		}
	}

	if (start == end)
		snprintf(start, size, "nothing)"); 
	else if (size > 1)
		*(end -1) = ')';
	
	return buf;
}

static int rtp_socket(void)
{
	int s;
	long flags;
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s > -1) {
		flags = fcntl(s, F_GETFL);
		fcntl(s, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
		if (nochecksums)
			setsockopt(s, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
	}
	return s;
}

/*!
 * \brief Initialize a new RTCP session.
 * 
 * \returns The newly initialized RTCP session.
 */
static struct ast_rtcp *ast_rtcp_new(void)
{
	struct ast_rtcp *rtcp;

	if (!(rtcp = ast_calloc(1, sizeof(*rtcp))))
		return NULL;
	rtcp->s = rtp_socket();
	rtcp->us.sin_family = AF_INET;
	rtcp->them.sin_family = AF_INET;
	rtcp->schedid = -1;

	if (rtcp->s < 0) {
		free(rtcp);
		ast_log(LOG_WARNING, "Unable to allocate RTCP socket: %s\n", strerror(errno));
		return NULL;
	}

	return rtcp;
}

/*!
 * \brief Initialize a new RTP structure.
 *
 */
void ast_rtp_new_init(struct ast_rtp *rtp)
{
	ast_mutex_init(&rtp->bridge_lock);

	rtp->them.sin_family = AF_INET;
	rtp->us.sin_family = AF_INET;
	rtp->ssrc = ast_random();
	rtp->seqno = ast_random() & 0xffff;
	ast_set_flag(rtp, FLAG_HAS_DTMF);

	return;
}

struct ast_rtp *ast_rtp_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode, struct in_addr addr)
{
	struct ast_rtp *rtp;
	int x;
	int first;
	int startplace;
	
	if (!(rtp = ast_calloc(1, sizeof(*rtp))))
		return NULL;

	ast_rtp_new_init(rtp);

	rtp->s = rtp_socket();
	if (rtp->s < 0) {
		free(rtp);
		ast_log(LOG_ERROR, "Unable to allocate socket: %s\n", strerror(errno));
		return NULL;
	}
	if (sched && rtcpenable) {
		rtp->sched = sched;
		rtp->rtcp = ast_rtcp_new();
	}
	
	/* Select a random port number in the range of possible RTP */
	x = (rtpend == rtpstart) ? rtpstart : (ast_random() % (rtpend - rtpstart)) + rtpstart;
	x = x & ~1;
	/* Save it for future references. */
	startplace = x;
	/* Iterate tring to bind that port and incrementing it otherwise untill a port was found or no ports are available. */
	for (;;) {
		/* Must be an even port number by RTP spec */
		rtp->us.sin_port = htons(x);
		rtp->us.sin_addr = addr;
		/* If there's rtcp, initialize it as well. */
		if (rtp->rtcp) {
			rtp->rtcp->us.sin_port = htons(x + 1);
			rtp->rtcp->us.sin_addr = addr;
		}
		/* Try to bind it/them. */
		if (!(first = bind(rtp->s, (struct sockaddr *)&rtp->us, sizeof(rtp->us))) &&
			(!rtp->rtcp || !bind(rtp->rtcp->s, (struct sockaddr *)&rtp->rtcp->us, sizeof(rtp->rtcp->us))))
			break;
		if (!first) {
			/* Primary bind succeeded! Gotta recreate it */
			close(rtp->s);
			rtp->s = rtp_socket();
		}
		if (errno != EADDRINUSE) {
			/* We got an error that wasn't expected, abort! */
			ast_log(LOG_ERROR, "Unexpected bind error: %s\n", strerror(errno));
			close(rtp->s);
			if (rtp->rtcp) {
				close(rtp->rtcp->s);
				free(rtp->rtcp);
			}
			free(rtp);
			return NULL;
		}
		/* The port was used, increment it (by two). */
		x += 2;
		/* Did we go over the limit ? */
		if (x > rtpend)
			/* then, start from the begingig. */
			x = (rtpstart + 1) & ~1;
		/* Check if we reached the place were we started. */
		if (x == startplace) {
			/* If so, there's no ports available. */
			ast_log(LOG_ERROR, "No RTP ports remaining. Can't setup media stream for this call.\n");
			close(rtp->s);
			if (rtp->rtcp) {
				close(rtp->rtcp->s);
				free(rtp->rtcp);
			}
			free(rtp);
			return NULL;
		}
	}
	rtp->sched = sched;
	rtp->io = io;
	if (callbackmode) {
		rtp->ioid = ast_io_add(rtp->io, rtp->s, rtpread, AST_IO_IN, rtp);
		ast_set_flag(rtp, FLAG_CALLBACK_MODE);
	}
	ast_rtp_pt_default(rtp);
	return rtp;
}

struct ast_rtp *ast_rtp_new(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode)
{
	struct in_addr ia;

	memset(&ia, 0, sizeof(ia));
	return ast_rtp_new_with_bindaddr(sched, io, rtcpenable, callbackmode, ia);
}

int ast_rtp_settos(struct ast_rtp *rtp, int tos)
{
	int res;

	if ((res = setsockopt(rtp->s, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)))) 
		ast_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
	return res;
}

void ast_rtp_new_source(struct ast_rtp *rtp)
{
	if (rtp) {
		rtp->set_marker_bit = 1;
	}
	return;
}

void ast_rtp_set_peer(struct ast_rtp *rtp, struct sockaddr_in *them)
{
	rtp->them.sin_port = them->sin_port;
	rtp->them.sin_addr = them->sin_addr;
	if (rtp->rtcp) {
		rtp->rtcp->them.sin_port = htons(ntohs(them->sin_port) + 1);
		rtp->rtcp->them.sin_addr = them->sin_addr;
	}
	rtp->rxseqno = 0;
}

int ast_rtp_get_peer(struct ast_rtp *rtp, struct sockaddr_in *them)
{
	if ((them->sin_family != AF_INET) ||
		(them->sin_port != rtp->them.sin_port) ||
		(them->sin_addr.s_addr != rtp->them.sin_addr.s_addr)) {
		them->sin_family = AF_INET;
		them->sin_port = rtp->them.sin_port;
		them->sin_addr = rtp->them.sin_addr;
		return 1;
	}
	return 0;
}

void ast_rtp_get_us(struct ast_rtp *rtp, struct sockaddr_in *us)
{
	*us = rtp->us;
}

struct ast_rtp *ast_rtp_get_bridged(struct ast_rtp *rtp)
{
	struct ast_rtp *bridged = NULL;

	ast_mutex_lock(&rtp->bridge_lock);
	bridged = rtp->bridged;
	ast_mutex_unlock(&rtp->bridge_lock);

	return bridged;
}

void ast_rtp_stop(struct ast_rtp *rtp)
{
	if (rtp->rtcp) {
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
	}

	memset(&rtp->them.sin_addr, 0, sizeof(rtp->them.sin_addr));
	memset(&rtp->them.sin_port, 0, sizeof(rtp->them.sin_port));
	if (rtp->rtcp) {
		memset(&rtp->rtcp->them.sin_addr, 0, sizeof(rtp->rtcp->them.sin_addr));
		memset(&rtp->rtcp->them.sin_port, 0, sizeof(rtp->rtcp->them.sin_port));
	}
	
	ast_clear_flag(rtp, FLAG_P2P_SENT_MARK);
}

void ast_rtp_reset(struct ast_rtp *rtp)
{
	memset(&rtp->rxcore, 0, sizeof(rtp->rxcore));
	memset(&rtp->txcore, 0, sizeof(rtp->txcore));
	memset(&rtp->dtmfmute, 0, sizeof(rtp->dtmfmute));
	rtp->lastts = 0;
	rtp->lastdigitts = 0;
	rtp->lastrxts = 0;
	rtp->lastividtimestamp = 0;
	rtp->lastovidtimestamp = 0;
	rtp->lasteventseqn = 0;
	rtp->lastevent = 0;
	rtp->lasttxformat = 0;
	rtp->lastrxformat = 0;
	rtp->dtmf_timeout = 0;
	rtp->seqno = 0;
	rtp->rxseqno = 0;
}

char *ast_rtp_get_quality(struct ast_rtp *rtp, struct ast_rtp_quality *qual)
{
	/*
	*ssrc          our ssrc
	*themssrc      their ssrc
	*lp            lost packets
	*rxjitter      our calculated jitter(rx)
	*rxcount       no. received packets
	*txjitter      reported jitter of the other end
	*txcount       transmitted packets
	*rlp           remote lost packets
	*rtt           round trip time
	*/

	if (qual && rtp) {
		qual->local_ssrc = rtp->ssrc;
		qual->local_jitter = rtp->rxjitter;
		qual->local_count = rtp->rxcount;
		qual->remote_ssrc = rtp->themssrc;
		qual->remote_count = rtp->txcount;
		if (rtp->rtcp) {
			qual->local_lostpackets = rtp->rtcp->expected_prior - rtp->rtcp->received_prior;
			qual->remote_lostpackets = rtp->rtcp->reported_lost;
			qual->remote_jitter = rtp->rtcp->reported_jitter / 65536.0;
			qual->rtt = rtp->rtcp->rtt;
		}
	}
	if (rtp->rtcp) {
		snprintf(rtp->rtcp->quality, sizeof(rtp->rtcp->quality),
			"ssrc=%u;themssrc=%u;lp=%u;rxjitter=%f;rxcount=%u;txjitter=%f;txcount=%u;rlp=%u;rtt=%f",
			rtp->ssrc,
			rtp->themssrc,
			rtp->rtcp->expected_prior - rtp->rtcp->received_prior,
			rtp->rxjitter,
			rtp->rxcount,
			(double)rtp->rtcp->reported_jitter / 65536.0,
			rtp->txcount,
			rtp->rtcp->reported_lost,
			rtp->rtcp->rtt);
		return rtp->rtcp->quality;
	} else
		return "<Unknown> - RTP/RTCP has already been destroyed";
}

void ast_rtp_destroy(struct ast_rtp *rtp)
{
	if (rtcp_debug_test_addr(&rtp->them) || rtcpstats) {
		/*Print some info on the call here */
		ast_verbose("  RTP-stats\n");
		ast_verbose("* Our Receiver:\n");
		ast_verbose("  SSRC:		 %u\n", rtp->themssrc);
		ast_verbose("  Received packets: %u\n", rtp->rxcount);
		ast_verbose("  Lost packets:	 %u\n", rtp->rtcp ? (rtp->rtcp->expected_prior - rtp->rtcp->received_prior) : 0);
		ast_verbose("  Jitter:		 %.4f\n", rtp->rxjitter);
		ast_verbose("  Transit:		 %.4f\n", rtp->rxtransit);
		ast_verbose("  RR-count:	 %u\n", rtp->rtcp ? rtp->rtcp->rr_count : 0);
		ast_verbose("* Our Sender:\n");
		ast_verbose("  SSRC:		 %u\n", rtp->ssrc);
		ast_verbose("  Sent packets:	 %u\n", rtp->txcount);
		ast_verbose("  Lost packets:	 %u\n", rtp->rtcp ? rtp->rtcp->reported_lost : 0);
		ast_verbose("  Jitter:		 %u\n", rtp->rtcp ? (rtp->rtcp->reported_jitter / (unsigned int)65536.0) : 0);
		ast_verbose("  SR-count:	 %u\n", rtp->rtcp ? rtp->rtcp->sr_count : 0);
		ast_verbose("  RTT:		 %f\n", rtp->rtcp ? rtp->rtcp->rtt : 0);
	}

	if (rtp->smoother)
		ast_smoother_free(rtp->smoother);
	if (rtp->ioid)
		ast_io_remove(rtp->io, rtp->ioid);
	if (rtp->s > -1)
		close(rtp->s);
	if (rtp->rtcp) {
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
		close(rtp->rtcp->s);
		free(rtp->rtcp);
		rtp->rtcp=NULL;
	}

	ast_mutex_destroy(&rtp->bridge_lock);

	free(rtp);
}

static unsigned int calc_txstamp(struct ast_rtp *rtp, struct timeval *delivery)
{
	struct timeval t;
	long ms;
	if (ast_tvzero(rtp->txcore)) {
		rtp->txcore = ast_tvnow();
		/* Round to 20ms for nice, pretty timestamps */
		rtp->txcore.tv_usec -= rtp->txcore.tv_usec % 20000;
	}
	/* Use previous txcore if available */
	t = (delivery && !ast_tvzero(*delivery)) ? *delivery : ast_tvnow();
	ms = ast_tvdiff_ms(t, rtp->txcore);
	if (ms < 0)
		ms = 0;
	/* Use what we just got for next time */
	rtp->txcore = t;
	return (unsigned int) ms;
}

/*! \brief Send begin frames for DTMF */
int ast_rtp_senddigit_begin(struct ast_rtp *rtp, char digit)
{
	unsigned int *rtpheader;
	int hdrlen = 12, res = 0, i = 0, payload = 0;
	char data[256];

	if ((digit <= '9') && (digit >= '0'))
		digit -= '0';
	else if (digit == '*')
		digit = 10;
	else if (digit == '#')
		digit = 11;
	else if ((digit >= 'A') && (digit <= 'D'))
		digit = digit - 'A' + 12;
	else if ((digit >= 'a') && (digit <= 'd'))
		digit = digit - 'a' + 12;
	else {
		ast_log(LOG_WARNING, "Don't know how to represent '%c'\n", digit);
		return 0;
	}

	/* If we have no peer, return immediately */	
	if (!rtp->them.sin_addr.s_addr || !rtp->them.sin_port)
		return 0;

	payload = ast_rtp_lookup_code(rtp, 0, AST_RTP_DTMF);

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));
	rtp->send_duration = 160;
	rtp->lastdigitts = rtp->lastts + rtp->send_duration;
	
	/* Get a pointer to the header */
	rtpheader = (unsigned int *)data;
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc); 

	for (i = 0; i < 2; i++) {
		rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (rtp->send_duration));
		res = sendto(rtp->s, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &rtp->them, sizeof(rtp->them));
		if (res < 0) 
			ast_log(LOG_ERROR, "RTP Transmission error to %s:%u: %s\n",
				ast_inet_ntoa(rtp->them.sin_addr),
				ntohs(rtp->them.sin_port), strerror(errno));
		if (rtp_debug_test_addr(&rtp->them))
			ast_verbose("Sent RTP DTMF packet to %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
				    ast_inet_ntoa(rtp->them.sin_addr),
				    ntohs(rtp->them.sin_port), payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
		/* Increment sequence number */
		rtp->seqno++;
		/* Increment duration */
		rtp->send_duration += 160;
		/* Clear marker bit and set seqno */
		rtpheader[0] = htonl((2 << 30) | (payload << 16) | (rtp->seqno));
	}

	/* Since we received a begin, we can safely store the digit and disable any compensation */
	rtp->sending_digit = 1;
	rtp->send_digit = digit;
	rtp->send_payload = payload;

	return 0;
}

/*! \brief Send continuation frame for DTMF */
static int ast_rtp_senddigit_continuation(struct ast_rtp *rtp)
{
	unsigned int *rtpheader;
	int hdrlen = 12, res = 0;
	char data[256];

	if (!rtp->them.sin_addr.s_addr || !rtp->them.sin_port)
		return 0;

	/* Setup packet to send */
	rtpheader = (unsigned int *)data;
        rtpheader[0] = htonl((2 << 30) | (1 << 23) | (rtp->send_payload << 16) | (rtp->seqno));
        rtpheader[1] = htonl(rtp->lastdigitts);
        rtpheader[2] = htonl(rtp->ssrc);
        rtpheader[3] = htonl((rtp->send_digit << 24) | (0xa << 16) | (rtp->send_duration));
	rtpheader[0] = htonl((2 << 30) | (rtp->send_payload << 16) | (rtp->seqno));
	
	/* Transmit */
	res = sendto(rtp->s, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &rtp->them, sizeof(rtp->them));
	if (res < 0)
		ast_log(LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
			ast_inet_ntoa(rtp->them.sin_addr),
			ntohs(rtp->them.sin_port), strerror(errno));
	if (rtp_debug_test_addr(&rtp->them))
		ast_verbose("Sent RTP DTMF packet to %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
			    ast_inet_ntoa(rtp->them.sin_addr),
			    ntohs(rtp->them.sin_port), rtp->send_payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);

	/* Increment sequence number */
	rtp->seqno++;
	/* Increment duration */
	rtp->send_duration += 160;

	return 0;
}

/*! \brief Send end packets for DTMF */
int ast_rtp_senddigit_end(struct ast_rtp *rtp, char digit)
{
	unsigned int *rtpheader;
	int hdrlen = 12, res = 0, i = 0;
	char data[256];
	
	/* If no address, then bail out */
	if (!rtp->them.sin_addr.s_addr || !rtp->them.sin_port)
		return 0;
	
	if ((digit <= '9') && (digit >= '0'))
		digit -= '0';
	else if (digit == '*')
		digit = 10;
	else if (digit == '#')
		digit = 11;
	else if ((digit >= 'A') && (digit <= 'D'))
		digit = digit - 'A' + 12;
	else if ((digit >= 'a') && (digit <= 'd'))
		digit = digit - 'a' + 12;
	else {
		ast_log(LOG_WARNING, "Don't know how to represent '%c'\n", digit);
		return 0;
	}

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));

	rtpheader = (unsigned int *)data;
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc);
	rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (rtp->send_duration));
	/* Set end bit */
	rtpheader[3] |= htonl((1 << 23));

	/* Send 3 termination packets */
	for (i = 0; i < 3; i++) {
		rtpheader[0] = htonl((2 << 30) | (rtp->send_payload << 16) | (rtp->seqno));
		res = sendto(rtp->s, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &rtp->them, sizeof(rtp->them));
		rtp->seqno++;
		if (res < 0)
			ast_log(LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
				ast_inet_ntoa(rtp->them.sin_addr),
				ntohs(rtp->them.sin_port), strerror(errno));
		if (rtp_debug_test_addr(&rtp->them))
			ast_verbose("Sent RTP DTMF packet to %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
				    ast_inet_ntoa(rtp->them.sin_addr),
				    ntohs(rtp->them.sin_port), rtp->send_payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
	}
	rtp->lastts += rtp->send_duration;
	rtp->sending_digit = 0;
	rtp->send_digit = 0;

	return res;
}

/*! \brief Public function: Send an H.261 fast update request, some devices need this rather than SIP XML */
int ast_rtcp_send_h261fur(void *data)
{
	struct ast_rtp *rtp = data;
	int res;

	rtp->rtcp->sendfur = 1;
	res = ast_rtcp_write(data);
	
	return res;
}

/*! \brief Send RTCP sender's report */
static int ast_rtcp_write_sr(const void *data)
{
	struct ast_rtp *rtp = (struct ast_rtp *)data;
	int res;
	int len = 0;
	struct timeval now;
	unsigned int now_lsw;
	unsigned int now_msw;
	unsigned int *rtcpheader;
	unsigned int lost;
	unsigned int extended;
	unsigned int expected;
	unsigned int expected_interval;
	unsigned int received_interval;
	int lost_interval;
	int fraction;
	struct timeval dlsr;
	char bdata[512];

	/* Commented condition is always not NULL if rtp->rtcp is not NULL */
	if (!rtp || !rtp->rtcp/* || (&rtp->rtcp->them.sin_addr == 0)*/)
		return 0;
	
	if (!rtp->rtcp->them.sin_addr.s_addr) {  /* This'll stop rtcp for this rtp session */
		ast_verbose("RTCP SR transmission error, rtcp halted\n");
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
		return 0;
	}

	gettimeofday(&now, NULL);
	timeval2ntp(now, &now_msw, &now_lsw); /* fill thses ones in from utils.c*/
	rtcpheader = (unsigned int *)bdata;
	rtcpheader[1] = htonl(rtp->ssrc);               /* Our SSRC */
	rtcpheader[2] = htonl(now_msw);                 /* now, MSW. gettimeofday() + SEC_BETWEEN_1900_AND_1970*/
	rtcpheader[3] = htonl(now_lsw);                 /* now, LSW */
	rtcpheader[4] = htonl(rtp->lastts);             /* FIXME shouldn't be that, it should be now */
	rtcpheader[5] = htonl(rtp->txcount);            /* No. packets sent */
	rtcpheader[6] = htonl(rtp->txoctetcount);       /* No. bytes sent */
	len += 28;
	
	extended = rtp->cycles + rtp->lastrxseqno;
	expected = extended - rtp->seedrxseqno + 1;
	if (rtp->rxcount > expected) 
		expected += rtp->rxcount - expected;
	lost = expected - rtp->rxcount;
	expected_interval = expected - rtp->rtcp->expected_prior;
	rtp->rtcp->expected_prior = expected;
	received_interval = rtp->rxcount - rtp->rtcp->received_prior;
	rtp->rtcp->received_prior = rtp->rxcount;
	lost_interval = expected_interval - received_interval;
	if (expected_interval == 0 || lost_interval <= 0)
		fraction = 0;
	else
		fraction = (lost_interval << 8) / expected_interval;
	timersub(&now, &rtp->rtcp->rxlsr, &dlsr);
	rtcpheader[7] = htonl(rtp->themssrc);
	rtcpheader[8] = htonl(((fraction & 0xff) << 24) | (lost & 0xffffff));
	rtcpheader[9] = htonl((rtp->cycles) | ((rtp->lastrxseqno & 0xffff)));
	rtcpheader[10] = htonl((unsigned int)(rtp->rxjitter * 65536.));
	rtcpheader[11] = htonl(rtp->rtcp->themrxlsr);
	rtcpheader[12] = htonl((((dlsr.tv_sec * 1000) + (dlsr.tv_usec / 1000)) * 65536) / 1000);
	len += 24;
	
	rtcpheader[0] = htonl((2 << 30) | (1 << 24) | (RTCP_PT_SR << 16) | ((len/4)-1));

	if (rtp->rtcp->sendfur) {
		rtcpheader[13] = htonl((2 << 30) | (0 << 24) | (RTCP_PT_FUR << 16) | 1);
		rtcpheader[14] = htonl(rtp->ssrc);               /* Our SSRC */
		len += 8;
		rtp->rtcp->sendfur = 0;
	}
	
	/* Insert SDES here. Probably should make SDES text equal to mimetypes[code].type (not subtype 'cos */ 
	/* it can change mid call, and SDES can't) */
	rtcpheader[len/4]     = htonl((2 << 30) | (1 << 24) | (RTCP_PT_SDES << 16) | 2);
	rtcpheader[(len/4)+1] = htonl(rtp->ssrc);               /* Our SSRC */
	rtcpheader[(len/4)+2] = htonl(0x01 << 24);                    /* Empty for the moment */
	len += 12;
	
	res = sendto(rtp->rtcp->s, (unsigned int *)rtcpheader, len, 0, (struct sockaddr *)&rtp->rtcp->them, sizeof(rtp->rtcp->them));
	if (res < 0) {
		ast_log(LOG_ERROR, "RTCP SR transmission error to %s:%d, rtcp halted %s\n",ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port), strerror(errno));
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
		return 0;
	}
	
	/* FIXME Don't need to get a new one */
	gettimeofday(&rtp->rtcp->txlsr, NULL);
	rtp->rtcp->sr_count++;

	rtp->rtcp->lastsrtxcount = rtp->txcount;	
	
	if (rtcp_debug_test_addr(&rtp->rtcp->them)) {
		ast_verbose("* Sent RTCP SR to %s:%d\n", ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
		ast_verbose("  Our SSRC: %u\n", rtp->ssrc);
		ast_verbose("  Sent(NTP): %u.%010u\n", (unsigned int)now.tv_sec, (unsigned int)now.tv_usec*4096);
		ast_verbose("  Sent(RTP): %u\n", rtp->lastts);
		ast_verbose("  Sent packets: %u\n", rtp->txcount);
		ast_verbose("  Sent octets: %u\n", rtp->txoctetcount);
		ast_verbose("  Report block:\n");
		ast_verbose("  Fraction lost: %u\n", fraction);
		ast_verbose("  Cumulative loss: %u\n", lost);
		ast_verbose("  IA jitter: %.4f\n", rtp->rxjitter);
		ast_verbose("  Their last SR: %u\n", rtp->rtcp->themrxlsr);
		ast_verbose("  DLSR: %4.4f (sec)\n\n", (double)(ntohl(rtcpheader[12])/65536.0));
	}
	return res;
}

/*! \brief Send RTCP recepient's report */
static int ast_rtcp_write_rr(const void *data)
{
	struct ast_rtp *rtp = (struct ast_rtp *)data;
	int res;
	int len = 32;
	unsigned int lost;
	unsigned int extended;
	unsigned int expected;
	unsigned int expected_interval;
	unsigned int received_interval;
	int lost_interval;
	struct timeval now;
	unsigned int *rtcpheader;
	char bdata[1024];
	struct timeval dlsr;
	int fraction;

	if (!rtp || !rtp->rtcp || (&rtp->rtcp->them.sin_addr == 0))
		return 0;
	  
	if (!rtp->rtcp->them.sin_addr.s_addr) {
		ast_log(LOG_ERROR, "RTCP RR transmission error, rtcp halted\n");
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
		return 0;
	}

	extended = rtp->cycles + rtp->lastrxseqno;
	expected = extended - rtp->seedrxseqno + 1;
	lost = expected - rtp->rxcount;
	expected_interval = expected - rtp->rtcp->expected_prior;
	rtp->rtcp->expected_prior = expected;
	received_interval = rtp->rxcount - rtp->rtcp->received_prior;
	rtp->rtcp->received_prior = rtp->rxcount;
	lost_interval = expected_interval - received_interval;
	if (expected_interval == 0 || lost_interval <= 0)
		fraction = 0;
	else
		fraction = (lost_interval << 8) / expected_interval;
	gettimeofday(&now, NULL);
	timersub(&now, &rtp->rtcp->rxlsr, &dlsr);
	rtcpheader = (unsigned int *)bdata;
	rtcpheader[0] = htonl((2 << 30) | (1 << 24) | (RTCP_PT_RR << 16) | ((len/4)-1));
	rtcpheader[1] = htonl(rtp->ssrc);
	rtcpheader[2] = htonl(rtp->themssrc);
	rtcpheader[3] = htonl(((fraction & 0xff) << 24) | (lost & 0xffffff));
	rtcpheader[4] = htonl((rtp->cycles) | ((rtp->lastrxseqno & 0xffff)));
	rtcpheader[5] = htonl((unsigned int)(rtp->rxjitter * 65536.));
	rtcpheader[6] = htonl(rtp->rtcp->themrxlsr);
	rtcpheader[7] = htonl((((dlsr.tv_sec * 1000) + (dlsr.tv_usec / 1000)) * 65536) / 1000);

	if (rtp->rtcp->sendfur) {
		rtcpheader[8] = htonl((2 << 30) | (0 << 24) | (RTCP_PT_FUR << 16) | 1); /* Header from page 36 in RFC 3550 */
		rtcpheader[9] = htonl(rtp->ssrc);               /* Our SSRC */
		len += 8;
		rtp->rtcp->sendfur = 0;
	}

	/*! \note Insert SDES here. Probably should make SDES text equal to mimetypes[code].type (not subtype 'cos 
	it can change mid call, and SDES can't) */
	rtcpheader[len/4]     = htonl((2 << 30) | (1 << 24) | (RTCP_PT_SDES << 16) | 2);
	rtcpheader[(len/4)+1] = htonl(rtp->ssrc);               /* Our SSRC */
	rtcpheader[(len/4)+2] = htonl(0x01 << 24);              /* Empty for the moment */
	len += 12;
	
	res = sendto(rtp->rtcp->s, (unsigned int *)rtcpheader, len, 0, (struct sockaddr *)&rtp->rtcp->them, sizeof(rtp->rtcp->them));

	if (res < 0) {
		ast_log(LOG_ERROR, "RTCP RR transmission error, rtcp halted: %s\n",strerror(errno));
		/* Remove the scheduler */
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
		return 0;
	}

	rtp->rtcp->rr_count++;

	if (rtcp_debug_test_addr(&rtp->rtcp->them)) {
		ast_verbose("\n* Sending RTCP RR to %s:%d\n"
			"  Our SSRC: %u\nTheir SSRC: %u\niFraction lost: %d\nCumulative loss: %u\n" 
			"  IA jitter: %.4f\n" 
			"  Their last SR: %u\n" 
			"  DLSR: %4.4f (sec)\n\n",
			ast_inet_ntoa(rtp->rtcp->them.sin_addr),
			ntohs(rtp->rtcp->them.sin_port),
			rtp->ssrc, rtp->themssrc, fraction, lost,
			rtp->rxjitter,
			rtp->rtcp->themrxlsr,
			(double)(ntohl(rtcpheader[7])/65536.0));
	}

	return res;
}

/*! \brief Write and RTCP packet to the far end
 * \note Decide if we are going to send an SR (with Reception Block) or RR 
 * RR is sent if we have not sent any rtp packets in the previous interval */
static int ast_rtcp_write(const void *data)
{
	struct ast_rtp *rtp = (struct ast_rtp *)data;
	int res;
	
	if (!rtp || !rtp->rtcp)
		return 0;

	if (rtp->txcount > rtp->rtcp->lastsrtxcount)
		res = ast_rtcp_write_sr(data);
	else
		res = ast_rtcp_write_rr(data);
	
	return res;
}

/*! \brief generate comfort noice (CNG) */
int ast_rtp_sendcng(struct ast_rtp *rtp, int level)
{
	unsigned int *rtpheader;
	int hdrlen = 12;
	int res;
	int payload;
	char data[256];
	level = 127 - (level & 0x7f);
	payload = ast_rtp_lookup_code(rtp, 0, AST_RTP_CN);

	/* If we have no peer, return immediately */	
	if (!rtp->them.sin_addr.s_addr)
		return 0;

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));

	/* Get a pointer to the header */
	rtpheader = (unsigned int *)data;
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno++));
	rtpheader[1] = htonl(rtp->lastts);
	rtpheader[2] = htonl(rtp->ssrc); 
	data[12] = level;
	if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
		res = sendto(rtp->s, (void *)rtpheader, hdrlen + 1, 0, (struct sockaddr *)&rtp->them, sizeof(rtp->them));
		if (res <0) 
			ast_log(LOG_ERROR, "RTP Comfort Noise Transmission error to %s:%d: %s\n", ast_inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port), strerror(errno));
		if (rtp_debug_test_addr(&rtp->them))
			ast_verbose("Sent Comfort Noise RTP packet to %s:%u (type %d, seq %u, ts %u, len %d)\n"
					, ast_inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port), payload, rtp->seqno, rtp->lastts,res - hdrlen);		   
		   
	}
	return 0;
}

static int ast_rtp_raw_write(struct ast_rtp *rtp, struct ast_frame *f, int codec)
{
	unsigned char *rtpheader;
	int hdrlen = 12;
	int res;
	unsigned int ms;
	int pred;
	int mark = 0;

	if (rtp->sending_digit) {
		return 0;
	}

	ms = calc_txstamp(rtp, &f->delivery);
	/* Default prediction */
	if (f->frametype == AST_FRAME_VOICE) {
		pred = rtp->lastts + f->samples;

		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * 8;
		if (ast_tvzero(f->delivery)) {
			/* If this isn't an absolute delivery time, Check if it is close to our prediction, 
			   and if so, go with our prediction */
			if (abs(rtp->lastts - pred) < MAX_TIMESTAMP_SKEW)
				rtp->lastts = pred;
			else {
				if (option_debug > 2)
					ast_log(LOG_DEBUG, "Difference is %d, ms is %d\n", abs(rtp->lastts - pred), ms);
				mark = 1;
			}
		}
	} else if (f->frametype == AST_FRAME_VIDEO) {
		mark = f->subclass & 0x1;
		pred = rtp->lastovidtimestamp + f->samples;
		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * 90;
		/* If it's close to our prediction, go for it */
		if (ast_tvzero(f->delivery)) {
			if (abs(rtp->lastts - pred) < 7200) {
				rtp->lastts = pred;
				rtp->lastovidtimestamp += f->samples;
			} else {
				if (option_debug > 2)
					ast_log(LOG_DEBUG, "Difference is %d, ms is %d (%d), pred/ts/samples %d/%d/%d\n", abs(rtp->lastts - pred), ms, ms * 90, rtp->lastts, pred, f->samples);
				rtp->lastovidtimestamp = rtp->lastts;
			}
		}
	}

	/* If we have been explicitly told to set the marker bit do so */
	if (rtp->set_marker_bit) {
		mark = 1;
		rtp->set_marker_bit = 0;
	}

	/* If the timestamp for non-digit packets has moved beyond the timestamp
	   for digits, update the digit timestamp.
	*/
	if (rtp->lastts > rtp->lastdigitts)
		rtp->lastdigitts = rtp->lastts;

	if (ast_test_flag(f, AST_FRFLAG_HAS_TIMING_INFO))
		rtp->lastts = f->ts * 8;

	/* Get a pointer to the header */
	rtpheader = (unsigned char *)(f->data - hdrlen);

	put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (rtp->seqno) | (mark << 23)));
	put_unaligned_uint32(rtpheader + 4, htonl(rtp->lastts));
	put_unaligned_uint32(rtpheader + 8, htonl(rtp->ssrc)); 

	if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
		res = sendto(rtp->s, (void *)rtpheader, f->datalen + hdrlen, 0, (struct sockaddr *)&rtp->them, sizeof(rtp->them));
		if (res <0) {
			if (!rtp->nat || (rtp->nat && (ast_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE))) {
				ast_log(LOG_DEBUG, "RTP Transmission error of packet %d to %s:%d: %s\n", rtp->seqno, ast_inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port), strerror(errno));
			} else if (((ast_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug) && !ast_test_flag(rtp, FLAG_NAT_INACTIVE_NOWARN)) {
				/* Only give this error message once if we are not RTP debugging */
				if (option_debug || rtpdebug)
					ast_log(LOG_DEBUG, "RTP NAT: Can't write RTP to private address %s:%d, waiting for other end to send audio...\n", ast_inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port));
				ast_set_flag(rtp, FLAG_NAT_INACTIVE_NOWARN);
			}
		} else {
			rtp->txcount++;
			rtp->txoctetcount +=(res - hdrlen);
			
			/* Do not schedule RR if RTCP isn't run */
			if (rtp->rtcp && rtp->rtcp->them.sin_addr.s_addr && rtp->rtcp->schedid < 1) {
			    rtp->rtcp->schedid = ast_sched_add(rtp->sched, ast_rtcp_calc_interval(rtp), ast_rtcp_write, rtp);
			}
		}
				
		if (rtp_debug_test_addr(&rtp->them))
			ast_verbose("Sent RTP packet to      %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
					ast_inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port), codec, rtp->seqno, rtp->lastts,res - hdrlen);
	}

	rtp->seqno++;

	return 0;
}

int ast_rtp_codec_setpref(struct ast_rtp *rtp, struct ast_codec_pref *prefs)
{
	struct ast_format_list current_format_old, current_format_new;

	/* if no packets have been sent through this session yet, then
	 *  changing preferences does not require any extra work
	 */
	if (rtp->lasttxformat == 0) {
		rtp->pref = *prefs;
		return 0;
	}

	current_format_old = ast_codec_pref_getsize(&rtp->pref, rtp->lasttxformat);

	rtp->pref = *prefs;

	current_format_new = ast_codec_pref_getsize(&rtp->pref, rtp->lasttxformat);

	/* if the framing desired for the current format has changed, we may have to create
	 * or adjust the smoother for this session
	 */
	if ((current_format_new.inc_ms != 0) &&
	    (current_format_new.cur_ms != current_format_old.cur_ms)) {
		int new_size = (current_format_new.cur_ms * current_format_new.fr_len) / current_format_new.inc_ms;

		if (rtp->smoother) {
			ast_smoother_reconfigure(rtp->smoother, new_size);
			if (option_debug) {
				ast_log(LOG_DEBUG, "Adjusted smoother to %d ms and %d bytes\n", current_format_new.cur_ms, new_size);
			}
		} else {
			if (!(rtp->smoother = ast_smoother_new(new_size))) {
				ast_log(LOG_WARNING, "Unable to create smoother: format: %d ms: %d len: %d\n", rtp->lasttxformat, current_format_new.cur_ms, new_size);
				return -1;
			}
			if (current_format_new.flags) {
				ast_smoother_set_flags(rtp->smoother, current_format_new.flags);
			}
			if (option_debug) {
				ast_log(LOG_DEBUG, "Created smoother: format: %d ms: %d len: %d\n", rtp->lasttxformat, current_format_new.cur_ms, new_size);
			}
		}
	}

	return 0;
}

struct ast_codec_pref *ast_rtp_codec_getpref(struct ast_rtp *rtp)
{
	return &rtp->pref;
}

int ast_rtp_codec_getformat(int pt)
{
	if (pt < 0 || pt > MAX_RTP_PT)
		return 0; /* bogus payload type */

	if (static_RTP_PT[pt].isAstFormat)
		return static_RTP_PT[pt].code;
	else
		return 0;
}

int ast_rtp_write(struct ast_rtp *rtp, struct ast_frame *_f)
{
	struct ast_frame *f;
	int codec;
	int hdrlen = 12;
	int subclass;
	

	/* If we have no peer, return immediately */	
	if (!rtp->them.sin_addr.s_addr)
		return 0;

	/* If there is no data length, return immediately */
	if (!_f->datalen) 
		return 0;
	
	/* Make sure we have enough space for RTP header */
	if ((_f->frametype != AST_FRAME_VOICE) && (_f->frametype != AST_FRAME_VIDEO)) {
		ast_log(LOG_WARNING, "RTP can only send voice and video\n");
		return -1;
	}

	subclass = _f->subclass;
	if (_f->frametype == AST_FRAME_VIDEO)
		subclass &= ~0x1;

	codec = ast_rtp_lookup_code(rtp, 1, subclass);
	if (codec < 0) {
		ast_log(LOG_WARNING, "Don't know how to send format %s packets with RTP\n", ast_getformatname(_f->subclass));
		return -1;
	}

	if (rtp->lasttxformat != subclass) {
		/* New format, reset the smoother */
		if (option_debug)
			ast_log(LOG_DEBUG, "Ooh, format changed from %s to %s\n", ast_getformatname(rtp->lasttxformat), ast_getformatname(subclass));
		rtp->lasttxformat = subclass;
		if (rtp->smoother)
			ast_smoother_free(rtp->smoother);
		rtp->smoother = NULL;
	}

	if (!rtp->smoother && subclass != AST_FORMAT_SPEEX && subclass != AST_FORMAT_G723_1) {
		struct ast_format_list fmt = ast_codec_pref_getsize(&rtp->pref, subclass);
		if (fmt.inc_ms) { /* if codec parameters is set / avoid division by zero */
			if (!(rtp->smoother = ast_smoother_new((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms))) {
				ast_log(LOG_WARNING, "Unable to create smoother: format: %d ms: %d len: %d\n", subclass, fmt.cur_ms, ((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms));
				return -1;
			}
			if (fmt.flags)
				ast_smoother_set_flags(rtp->smoother, fmt.flags);
			if (option_debug)
				ast_log(LOG_DEBUG, "Created smoother: format: %d ms: %d len: %d\n", subclass, fmt.cur_ms, ((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms));
		}
	}
	if (rtp->smoother) {
		if (ast_smoother_test_flag(rtp->smoother, AST_SMOOTHER_FLAG_BE)) {
			ast_smoother_feed_be(rtp->smoother, _f);
		} else {
			ast_smoother_feed(rtp->smoother, _f);
		}

		while ((f = ast_smoother_read(rtp->smoother)) && (f->data)) {
			if (f->subclass == AST_FORMAT_G722) {
				/* G.722 is silllllllllllllly */
				f->samples /= 2;
			}

			ast_rtp_raw_write(rtp, f, codec);
		}
	} else {
		/* Don't buffer outgoing frames; send them one-per-packet: */
		if (_f->offset < hdrlen) {
			f = ast_frdup(_f);
		} else {
			f = _f;
		}
		if (f->data) {
			if (f->subclass == AST_FORMAT_G722) {
				/* G.722 is silllllllllllllly */
				f->samples /= 2;
			}
			ast_rtp_raw_write(rtp, f, codec);
		}
		if (f != _f)
			ast_frfree(f);
	}
		
	return 0;
}

/*! \brief Unregister interface to channel driver */
void ast_rtp_proto_unregister(struct ast_rtp_protocol *proto)
{
	AST_LIST_LOCK(&protos);
	AST_LIST_REMOVE(&protos, proto, list);
	AST_LIST_UNLOCK(&protos);
}

/*! \brief Register interface to channel driver */
int ast_rtp_proto_register(struct ast_rtp_protocol *proto)
{
	struct ast_rtp_protocol *cur;

	AST_LIST_LOCK(&protos);
	AST_LIST_TRAVERSE(&protos, cur, list) {	
		if (!strcmp(cur->type, proto->type)) {
			ast_log(LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
			AST_LIST_UNLOCK(&protos);
			return -1;
		}
	}
	AST_LIST_INSERT_HEAD(&protos, proto, list);
	AST_LIST_UNLOCK(&protos);
	
	return 0;
}

/*! \brief Bridge loop for true native bridge (reinvite) */
static enum ast_bridge_result bridge_native_loop(struct ast_channel *c0, struct ast_channel *c1, struct ast_rtp *p0, struct ast_rtp *p1, struct ast_rtp *vp0, struct ast_rtp *vp1, struct ast_rtp_protocol *pr0, struct ast_rtp_protocol *pr1, int codec0, int codec1, int timeoutms, int flags, struct ast_frame **fo, struct ast_channel **rc, void *pvt0, void *pvt1)
{
	struct ast_frame *fr = NULL;
	struct ast_channel *who = NULL, *other = NULL, *cs[3] = {NULL, };
	int oldcodec0 = codec0, oldcodec1 = codec1;
	struct sockaddr_in ac1 = {0,}, vac1 = {0,}, ac0 = {0,}, vac0 = {0,};
	struct sockaddr_in t1 = {0,}, vt1 = {0,}, t0 = {0,}, vt0 = {0,};
	
	/* Set it up so audio goes directly between the two endpoints */

	/* Test the first channel */
	if (!(pr0->set_rtp_peer(c0, p1, vp1, codec1, ast_test_flag(p1, FLAG_NAT_ACTIVE)))) {
		ast_rtp_get_peer(p1, &ac1);
		if (vp1)
			ast_rtp_get_peer(vp1, &vac1);
	} else
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
	
	/* Test the second channel */
	if (!(pr1->set_rtp_peer(c1, p0, vp0, codec0, ast_test_flag(p0, FLAG_NAT_ACTIVE)))) {
		ast_rtp_get_peer(p0, &ac0);
		if (vp0)
			ast_rtp_get_peer(vp0, &vac0);
	} else
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c1->name, c0->name);

	/* Now we can unlock and move into our loop */
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	/* Throw our channels into the structure and enter the loop */
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	for (;;) {
		/* Check if anything changed */
		if ((c0->tech_pvt != pvt0) ||
		    (c1->tech_pvt != pvt1) ||
		    (c0->masq || c0->masqr || c1->masq || c1->masqr) ||
		    (c0->monitor || c0->audiohooks || c1->monitor || c1->audiohooks)) {
			ast_log(LOG_DEBUG, "Oooh, something is weird, backing out\n");
			if (c0->tech_pvt == pvt0)
				if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0))
					ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
			if (c1->tech_pvt == pvt1)
				if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0))
					ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
			return AST_BRIDGE_RETRY;
		}

		/* Check if they have changed their address */
		ast_rtp_get_peer(p1, &t1);
		if (vp1)
			ast_rtp_get_peer(vp1, &vt1);
		if (pr1->get_codec)
			codec1 = pr1->get_codec(c1);
		ast_rtp_get_peer(p0, &t0);
		if (vp0)
			ast_rtp_get_peer(vp0, &vt0);
		if (pr0->get_codec)
			codec0 = pr0->get_codec(c0);
		if ((inaddrcmp(&t1, &ac1)) ||
		    (vp1 && inaddrcmp(&vt1, &vac1)) ||
		    (codec1 != oldcodec1)) {
			if (option_debug > 1) {
				ast_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n",
					c1->name, ast_inet_ntoa(t1.sin_addr), ntohs(t1.sin_port), codec1);
				ast_log(LOG_DEBUG, "Oooh, '%s' changed end vaddress to %s:%d (format %d)\n",
					c1->name, ast_inet_ntoa(vt1.sin_addr), ntohs(vt1.sin_port), codec1);
				ast_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n",
					c1->name, ast_inet_ntoa(ac1.sin_addr), ntohs(ac1.sin_port), oldcodec1);
				ast_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n",
					c1->name, ast_inet_ntoa(vac1.sin_addr), ntohs(vac1.sin_port), oldcodec1);
			}
			if (pr0->set_rtp_peer(c0, t1.sin_addr.s_addr ? p1 : NULL, vt1.sin_addr.s_addr ? vp1 : NULL, codec1, ast_test_flag(p1, FLAG_NAT_ACTIVE)))
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c0->name, c1->name);
			memcpy(&ac1, &t1, sizeof(ac1));
			memcpy(&vac1, &vt1, sizeof(vac1));
			oldcodec1 = codec1;
		}
		if ((inaddrcmp(&t0, &ac0)) ||
		    (vp0 && inaddrcmp(&vt0, &vac0)) ||
		    (codec0 != oldcodec0)) {
			if (option_debug > 1) {
				ast_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n",
					c0->name, ast_inet_ntoa(t0.sin_addr), ntohs(t0.sin_port), codec0);
				ast_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n",
					c0->name, ast_inet_ntoa(ac0.sin_addr), ntohs(ac0.sin_port), oldcodec0);
			}
			if (pr1->set_rtp_peer(c1, t0.sin_addr.s_addr ? p0 : NULL, vt0.sin_addr.s_addr ? vp0 : NULL, codec0, ast_test_flag(p0, FLAG_NAT_ACTIVE)))
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c1->name, c0->name);
			memcpy(&ac0, &t0, sizeof(ac0));
			memcpy(&vac0, &vt0, sizeof(vac0));
			oldcodec0 = codec0;
		}

		/* Wait for frame to come in on the channels */
		if (!(who = ast_waitfor_n(cs, 2, &timeoutms))) {
			if (!timeoutms) {
				if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0))
					ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
				if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0))
					ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
				return AST_BRIDGE_RETRY;
			}
			if (option_debug)
				ast_log(LOG_DEBUG, "Ooh, empty read...\n");
			if (ast_check_hangup(c0) || ast_check_hangup(c1))
				break;
			continue;
		}
		fr = ast_read(who);
		other = (who == c0) ? c1 : c0;
		if (!fr || ((fr->frametype == AST_FRAME_DTMF_BEGIN || fr->frametype == AST_FRAME_DTMF_END) &&
			    (((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) ||
			     ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1))))) {
			/* Break out of bridge */
			*fo = fr;
			*rc = who;
			if (option_debug)
				ast_log(LOG_DEBUG, "Oooh, got a %s\n", fr ? "digit" : "hangup");
			if (c0->tech_pvt == pvt0)
				if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0))
					ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
			if (c1->tech_pvt == pvt1)
				if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0))
					ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
			return AST_BRIDGE_COMPLETE;
		} else if ((fr->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			if ((fr->subclass == AST_CONTROL_HOLD) ||
			    (fr->subclass == AST_CONTROL_UNHOLD) ||
			    (fr->subclass == AST_CONTROL_VIDUPDATE) ||
			    (fr->subclass == AST_CONTROL_SRCUPDATE)) {
				if (fr->subclass == AST_CONTROL_HOLD) {
					/* If we someone went on hold we want the other side to reinvite back to us */
					if (who == c0)
						pr1->set_rtp_peer(c1, NULL, NULL, 0, 0);
					else
						pr0->set_rtp_peer(c0, NULL, NULL, 0, 0);
				} else if (fr->subclass == AST_CONTROL_UNHOLD) {
					/* If they went off hold they should go back to being direct */
					if (who == c0)
						pr1->set_rtp_peer(c1, p0, vp0, codec0, ast_test_flag(p0, FLAG_NAT_ACTIVE));
					else
						pr0->set_rtp_peer(c0, p1, vp1, codec1, ast_test_flag(p1, FLAG_NAT_ACTIVE));
				}
				/* Update local address information */
				ast_rtp_get_peer(p0, &t0);
				memcpy(&ac0, &t0, sizeof(ac0));
				ast_rtp_get_peer(p1, &t1);
				memcpy(&ac1, &t1, sizeof(ac1));
				/* Update codec information */
				if (pr0->get_codec && c0->tech_pvt)
					oldcodec0 = codec0 = pr0->get_codec(c0);
				if (pr1->get_codec && c1->tech_pvt)
					oldcodec1 = codec1 = pr1->get_codec(c1);
				ast_indicate_data(other, fr->subclass, fr->data, fr->datalen);
				ast_frfree(fr);
			} else {
				*fo = fr;
				*rc = who;
				ast_log(LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", fr->subclass, who->name);
				return AST_BRIDGE_COMPLETE;
			}
		} else {
			if ((fr->frametype == AST_FRAME_DTMF_BEGIN) ||
			    (fr->frametype == AST_FRAME_DTMF_END) ||
			    (fr->frametype == AST_FRAME_VOICE) ||
			    (fr->frametype == AST_FRAME_VIDEO) ||
			    (fr->frametype == AST_FRAME_IMAGE) ||
			    (fr->frametype == AST_FRAME_HTML) ||
			    (fr->frametype == AST_FRAME_MODEM) ||
			    (fr->frametype == AST_FRAME_TEXT)) {
				ast_write(other, fr);
			}
			ast_frfree(fr);
		}
		/* Swap priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}

	if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0))
		ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
	if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0))
		ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);

	return AST_BRIDGE_FAILED;
}

/*! \brief P2P RTP Callback */
#ifdef P2P_INTENSE
static int p2p_rtp_callback(int *id, int fd, short events, void *cbdata)
{
	int res = 0, hdrlen = 12;
	struct sockaddr_in sin;
	socklen_t len;
	unsigned int *header;
	struct ast_rtp *rtp = cbdata, *bridged = NULL;

	if (!rtp)
		return 1;

	len = sizeof(sin);
	if ((res = recvfrom(fd, rtp->rawdata + AST_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - AST_FRIENDLY_OFFSET, 0, (struct sockaddr *)&sin, &len)) < 0)
		return 1;

	header = (unsigned int *)(rtp->rawdata + AST_FRIENDLY_OFFSET);

	/* If NAT support is turned on, then see if we need to change their address */
	if ((rtp->nat) && 
	    ((rtp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
	     (rtp->them.sin_port != sin.sin_port))) {
		rtp->them = sin;
		rtp->rxseqno = 0;
		ast_set_flag(rtp, FLAG_NAT_ACTIVE);
		if (option_debug || rtpdebug)
			ast_log(LOG_DEBUG, "P2P RTP NAT: Got audio from other end. Now sending to address %s:%d\n", ast_inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port));
	}

	/* Write directly out to other RTP stream if bridged */
	if ((bridged = ast_rtp_get_bridged(rtp)))
		bridge_p2p_rtp_write(rtp, bridged, header, res, hdrlen);

	return 1;
}

/*! \brief Helper function to switch a channel and RTP stream into callback mode */
static int p2p_callback_enable(struct ast_channel *chan, struct ast_rtp *rtp, int *fds, int **iod)
{
	/* If we need DTMF, are looking for STUN, or we have no IO structure then we can't do direct callback */
	if (ast_test_flag(rtp, FLAG_P2P_NEED_DTMF) || ast_test_flag(rtp, FLAG_HAS_STUN) || !rtp->io)
		return 0;

	/* If the RTP structure is already in callback mode, remove it temporarily */
	if (rtp->ioid) {
		ast_io_remove(rtp->io, rtp->ioid);
		rtp->ioid = NULL;
	}

	/* Steal the file descriptors from the channel and stash them away */
	fds[0] = chan->fds[0];
	chan->fds[0] = -1;

	/* Now, fire up callback mode */
	iod[0] = ast_io_add(rtp->io, fds[0], p2p_rtp_callback, AST_IO_IN, rtp);

	return 1;
}
#else
static int p2p_callback_enable(struct ast_channel *chan, struct ast_rtp *rtp, int *fds, int **iod)
{
	return 0;
}
#endif

/*! \brief Helper function to switch a channel and RTP stream out of callback mode */
static int p2p_callback_disable(struct ast_channel *chan, struct ast_rtp *rtp, int *fds, int **iod)
{
	ast_channel_lock(chan);

	/* Remove the callback from the IO context */
	ast_io_remove(rtp->io, iod[0]);

	/* Restore file descriptors */
	chan->fds[0] = fds[0];
	ast_channel_unlock(chan);

	/* Restore callback mode if previously used */
	if (ast_test_flag(rtp, FLAG_CALLBACK_MODE))
		rtp->ioid = ast_io_add(rtp->io, rtp->s, rtpread, AST_IO_IN, rtp);

	return 0;
}

/*! \brief Helper function that sets what an RTP structure is bridged to */
static void p2p_set_bridge(struct ast_rtp *rtp0, struct ast_rtp *rtp1)
{
	ast_mutex_lock(&rtp0->bridge_lock);
	rtp0->bridged = rtp1;
	ast_mutex_unlock(&rtp0->bridge_lock);

	return;
}

/*! \brief Bridge loop for partial native bridge (packet2packet) */
static enum ast_bridge_result bridge_p2p_loop(struct ast_channel *c0, struct ast_channel *c1, struct ast_rtp *p0, struct ast_rtp *p1, int timeoutms, int flags, struct ast_frame **fo, struct ast_channel **rc, void *pvt0, void *pvt1)
{
	struct ast_frame *fr = NULL;
	struct ast_channel *who = NULL, *other = NULL, *cs[3] = {NULL, };
	int p0_fds[2] = {-1, -1}, p1_fds[2] = {-1, -1};
	int *p0_iod[2] = {NULL, NULL}, *p1_iod[2] = {NULL, NULL};
	int p0_callback = 0, p1_callback = 0;
	enum ast_bridge_result res = AST_BRIDGE_FAILED;

	/* Okay, setup each RTP structure to do P2P forwarding */
	ast_clear_flag(p0, FLAG_P2P_SENT_MARK);
	p2p_set_bridge(p0, p1);
	ast_clear_flag(p1, FLAG_P2P_SENT_MARK);
	p2p_set_bridge(p1, p0);

	/* Activate callback modes if possible */
	p0_callback = p2p_callback_enable(c0, p0, &p0_fds[0], &p0_iod[0]);
	p1_callback = p2p_callback_enable(c1, p1, &p1_fds[0], &p1_iod[0]);

	/* Now let go of the channel locks and be on our way */
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	/* Go into a loop forwarding frames until we don't need to anymore */
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	for (;;) {
		/* If the underlying formats have changed force this bridge to break */
		if ((c0->rawreadformat != c1->rawwriteformat) || (c1->rawreadformat != c0->rawwriteformat)) {
			ast_log(LOG_DEBUG, "Oooh, formats changed, backing out\n");
			res = AST_BRIDGE_FAILED_NOWARN;
			break;
		}
		/* Check if anything changed */
		if ((c0->tech_pvt != pvt0) ||
		    (c1->tech_pvt != pvt1) ||
		    (c0->masq || c0->masqr || c1->masq || c1->masqr) ||
		    (c0->monitor || c0->audiohooks || c1->monitor || c1->audiohooks)) {
			ast_log(LOG_DEBUG, "Oooh, something is weird, backing out\n");
			if ((c0->masq || c0->masqr) && (fr = ast_read(c0)))
				ast_frfree(fr);
			if ((c1->masq || c1->masqr) && (fr = ast_read(c1)))
				ast_frfree(fr);
			res = AST_BRIDGE_RETRY;
			break;
		}
		/* Wait on a channel to feed us a frame */
		if (!(who = ast_waitfor_n(cs, 2, &timeoutms))) {
			if (!timeoutms) {
				res = AST_BRIDGE_RETRY;
				break;
			}
			if (option_debug)
				ast_log(LOG_NOTICE, "Ooh, empty read...\n");
			if (ast_check_hangup(c0) || ast_check_hangup(c1))
				break;
			continue;
		}
		/* Read in frame from channel */
		fr = ast_read(who);
		other = (who == c0) ? c1 : c0;
		/* Dependong on the frame we may need to break out of our bridge */
		if (!fr || ((fr->frametype == AST_FRAME_DTMF_BEGIN || fr->frametype == AST_FRAME_DTMF_END) &&
			    ((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) |
			    ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1)))) {
			/* Record received frame and who */
			*fo = fr;
			*rc = who;
			if (option_debug)
				ast_log(LOG_DEBUG, "Oooh, got a %s\n", fr ? "digit" : "hangup");
			res = AST_BRIDGE_COMPLETE;
			break;
		} else if ((fr->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			if ((fr->subclass == AST_CONTROL_HOLD) ||
			    (fr->subclass == AST_CONTROL_UNHOLD) ||
			    (fr->subclass == AST_CONTROL_VIDUPDATE) ||
			    (fr->subclass == AST_CONTROL_SRCUPDATE)) {
				/* If we are going on hold, then break callback mode and P2P bridging */
				if (fr->subclass == AST_CONTROL_HOLD) {
					if (p0_callback)
						p0_callback = p2p_callback_disable(c0, p0, &p0_fds[0], &p0_iod[0]);
					if (p1_callback)
						p1_callback = p2p_callback_disable(c1, p1, &p1_fds[0], &p1_iod[0]);
					p2p_set_bridge(p0, NULL);
					p2p_set_bridge(p1, NULL);
				} else if (fr->subclass == AST_CONTROL_UNHOLD) {
					/* If we are off hold, then go back to callback mode and P2P bridging */
					ast_clear_flag(p0, FLAG_P2P_SENT_MARK);
					p2p_set_bridge(p0, p1);
					ast_clear_flag(p1, FLAG_P2P_SENT_MARK);
					p2p_set_bridge(p1, p0);
					p0_callback = p2p_callback_enable(c0, p0, &p0_fds[0], &p0_iod[0]);
					p1_callback = p2p_callback_enable(c1, p1, &p1_fds[0], &p1_iod[0]);
				}
				ast_indicate_data(other, fr->subclass, fr->data, fr->datalen);
				ast_frfree(fr);
			} else {
				*fo = fr;
				*rc = who;
				ast_log(LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", fr->subclass, who->name);
				res = AST_BRIDGE_COMPLETE;
				break;
			}
		} else {
			if ((fr->frametype == AST_FRAME_DTMF_BEGIN) ||
			    (fr->frametype == AST_FRAME_DTMF_END) ||
			    (fr->frametype == AST_FRAME_VOICE) ||
			    (fr->frametype == AST_FRAME_VIDEO) ||
			    (fr->frametype == AST_FRAME_IMAGE) ||
			    (fr->frametype == AST_FRAME_HTML) ||
			    (fr->frametype == AST_FRAME_MODEM) ||
			    (fr->frametype == AST_FRAME_TEXT)) {
				ast_write(other, fr);
			}

			ast_frfree(fr);
		}
		/* Swap priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}

	/* If we are totally avoiding the core, then restore our link to it */
	if (p0_callback)
		p0_callback = p2p_callback_disable(c0, p0, &p0_fds[0], &p0_iod[0]);
	if (p1_callback)
		p1_callback = p2p_callback_disable(c1, p1, &p1_fds[0], &p1_iod[0]);

	/* Break out of the direct bridge */
	p2p_set_bridge(p0, NULL);
	p2p_set_bridge(p1, NULL);

	return res;
}

/*! \brief Bridge calls. If possible and allowed, initiate
	re-invite so the peers exchange media directly outside 
	of Asterisk. */
enum ast_bridge_result ast_rtp_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms)
{
	struct ast_rtp *p0 = NULL, *p1 = NULL;		/* Audio RTP Channels */
	struct ast_rtp *vp0 = NULL, *vp1 = NULL;	/* Video RTP channels */
	struct ast_rtp_protocol *pr0 = NULL, *pr1 = NULL;
	enum ast_rtp_get_result audio_p0_res = AST_RTP_GET_FAILED, video_p0_res = AST_RTP_GET_FAILED;
	enum ast_rtp_get_result audio_p1_res = AST_RTP_GET_FAILED, video_p1_res = AST_RTP_GET_FAILED;
	enum ast_bridge_result res = AST_BRIDGE_FAILED;
	int codec0 = 0, codec1 = 0;
	void *pvt0 = NULL, *pvt1 = NULL;

	/* Lock channels */
	ast_channel_lock(c0);
	while(ast_channel_trylock(c1)) {
		ast_channel_unlock(c0);
		usleep(1);
		ast_channel_lock(c0);
	}

	/* Ensure neither channel got hungup during lock avoidance */
	if (ast_check_hangup(c0) || ast_check_hangup(c1)) {
		ast_log(LOG_WARNING, "Got hangup while attempting to bridge '%s' and '%s'\n", c0->name, c1->name);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED;
	}
		
	/* Find channel driver interfaces */
	if (!(pr0 = get_proto(c0))) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c0->name);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED;
	}
	if (!(pr1 = get_proto(c1))) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c1->name);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED;
	}

	/* Get channel specific interface structures */
	pvt0 = c0->tech_pvt;
	pvt1 = c1->tech_pvt;

	/* Get audio and video interface (if native bridge is possible) */
	audio_p0_res = pr0->get_rtp_info(c0, &p0);
	video_p0_res = pr0->get_vrtp_info ? pr0->get_vrtp_info(c0, &vp0) : AST_RTP_GET_FAILED;
	audio_p1_res = pr1->get_rtp_info(c1, &p1);
	video_p1_res = pr1->get_vrtp_info ? pr1->get_vrtp_info(c1, &vp1) : AST_RTP_GET_FAILED;

	/* If we are carrying video, and both sides are not reinviting... then fail the native bridge */
	if (video_p0_res != AST_RTP_GET_FAILED && (audio_p0_res != AST_RTP_TRY_NATIVE || video_p0_res != AST_RTP_TRY_NATIVE))
		audio_p0_res = AST_RTP_GET_FAILED;
	if (video_p1_res != AST_RTP_GET_FAILED && (audio_p1_res != AST_RTP_TRY_NATIVE || video_p1_res != AST_RTP_TRY_NATIVE))
		audio_p1_res = AST_RTP_GET_FAILED;

	/* Check if a bridge is possible (partial/native) */
	if (audio_p0_res == AST_RTP_GET_FAILED || audio_p1_res == AST_RTP_GET_FAILED) {
		/* Somebody doesn't want to play... */
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}

	/* If we need to feed DTMF frames into the core then only do a partial native bridge */
	if (ast_test_flag(p0, FLAG_HAS_DTMF) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) {
		ast_set_flag(p0, FLAG_P2P_NEED_DTMF);
		audio_p0_res = AST_RTP_TRY_PARTIAL;
	}

	if (ast_test_flag(p1, FLAG_HAS_DTMF) && (flags & AST_BRIDGE_DTMF_CHANNEL_1)) {
		ast_set_flag(p1, FLAG_P2P_NEED_DTMF);
		audio_p1_res = AST_RTP_TRY_PARTIAL;
	}

	/* If both sides are not using the same method of DTMF transmission 
	 * (ie: one is RFC2833, other is INFO... then we can not do direct media. 
	 * --------------------------------------------------
	 * | DTMF Mode |  HAS_DTMF  |  Accepts Begin Frames |
	 * |-----------|------------|-----------------------|
	 * | Inband    | False      | True                  |
	 * | RFC2833   | True       | True                  |
	 * | SIP INFO  | False      | False                 |
	 * --------------------------------------------------
	 * However, if DTMF from both channels is being monitored by the core, then
	 * we can still do packet-to-packet bridging, because passing through the 
	 * core will handle DTMF mode translation.
	 */
	if ( (ast_test_flag(p0, FLAG_HAS_DTMF) != ast_test_flag(p1, FLAG_HAS_DTMF)) ||
		 (!c0->tech->send_digit_begin != !c1->tech->send_digit_begin)) {
		if (!ast_test_flag(p0, FLAG_P2P_NEED_DTMF) || !ast_test_flag(p1, FLAG_P2P_NEED_DTMF)) {
			ast_channel_unlock(c0);
			ast_channel_unlock(c1);
			return AST_BRIDGE_FAILED_NOWARN;
		}
		audio_p0_res = AST_RTP_TRY_PARTIAL;
		audio_p1_res = AST_RTP_TRY_PARTIAL;
	}

	/* If we need to feed frames into the core don't do a P2P bridge */
	if ((audio_p0_res == AST_RTP_TRY_PARTIAL && ast_test_flag(p0, FLAG_P2P_NEED_DTMF)) ||
	    (audio_p1_res == AST_RTP_TRY_PARTIAL && ast_test_flag(p1, FLAG_P2P_NEED_DTMF))) {
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}

	/* Get codecs from both sides */
	codec0 = pr0->get_codec ? pr0->get_codec(c0) : 0;
	codec1 = pr1->get_codec ? pr1->get_codec(c1) : 0;
	if (codec0 && codec1 && !(codec0 & codec1)) {
		/* Hey, we can't do native bridging if both parties speak different codecs */
		if (option_debug)
			ast_log(LOG_DEBUG, "Channel codec0 = %d is not codec1 = %d, cannot native bridge in RTP.\n", codec0, codec1);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}

	/* If either side can only do a partial bridge, then don't try for a true native bridge */
	if (audio_p0_res == AST_RTP_TRY_PARTIAL || audio_p1_res == AST_RTP_TRY_PARTIAL) {
		struct ast_format_list fmt0, fmt1;

		/* In order to do Packet2Packet bridging both sides must be in the same rawread/rawwrite */
		if (c0->rawreadformat != c1->rawwriteformat || c1->rawreadformat != c0->rawwriteformat) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Cannot packet2packet bridge - raw formats are incompatible\n");
			ast_channel_unlock(c0);
			ast_channel_unlock(c1);
			return AST_BRIDGE_FAILED_NOWARN;
		}
		/* They must also be using the same packetization */
		fmt0 = ast_codec_pref_getsize(&p0->pref, c0->rawreadformat);
		fmt1 = ast_codec_pref_getsize(&p1->pref, c1->rawreadformat);
		if (fmt0.cur_ms != fmt1.cur_ms) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Cannot packet2packet bridge - packetization settings prevent it\n");
			ast_channel_unlock(c0);
			ast_channel_unlock(c1);
			return AST_BRIDGE_FAILED_NOWARN;
		}

		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Packet2Packet bridging %s and %s\n", c0->name, c1->name);
		res = bridge_p2p_loop(c0, c1, p0, p1, timeoutms, flags, fo, rc, pvt0, pvt1);
	} else {
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Native bridging %s and %s\n", c0->name, c1->name);
		res = bridge_native_loop(c0, c1, p0, p1, vp0, vp1, pr0, pr1, codec0, codec1, timeoutms, flags, fo, rc, pvt0, pvt1);
	}

	return res;
}

static int rtp_do_debug_ip(int fd, int argc, char *argv[])
{
	struct hostent *hp;
	struct ast_hostent ahp;
	int port = 0;
	char *p, *arg;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	arg = argv[3];
	p = strstr(arg, ":");
	if (p) {
		*p = '\0';
		p++;
		port = atoi(p);
	}
	hp = ast_gethostbyname(arg, &ahp);
	if (hp == NULL)
		return RESULT_SHOWUSAGE;
	rtpdebugaddr.sin_family = AF_INET;
	memcpy(&rtpdebugaddr.sin_addr, hp->h_addr, sizeof(rtpdebugaddr.sin_addr));
	rtpdebugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(fd, "RTP Debugging Enabled for IP: %s\n", ast_inet_ntoa(rtpdebugaddr.sin_addr));
	else
		ast_cli(fd, "RTP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(rtpdebugaddr.sin_addr), port);
	rtpdebug = 1;
	return RESULT_SUCCESS;
}

static int rtcp_do_debug_ip_deprecated(int fd, int argc, char *argv[])
{
	struct hostent *hp;
	struct ast_hostent ahp;
	int port = 0;
	char *p, *arg;
	if (argc != 5)
		return RESULT_SHOWUSAGE;

	arg = argv[4];
	p = strstr(arg, ":");
	if (p) {
		*p = '\0';
		p++;
		port = atoi(p);
	}
	hp = ast_gethostbyname(arg, &ahp);
	if (hp == NULL)
		return RESULT_SHOWUSAGE;
	rtcpdebugaddr.sin_family = AF_INET;
	memcpy(&rtcpdebugaddr.sin_addr, hp->h_addr, sizeof(rtcpdebugaddr.sin_addr));
	rtcpdebugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(fd, "RTCP Debugging Enabled for IP: %s\n", ast_inet_ntoa(rtcpdebugaddr.sin_addr));
	else
		ast_cli(fd, "RTCP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(rtcpdebugaddr.sin_addr), port);
	rtcpdebug = 1;
	return RESULT_SUCCESS;
}

static int rtcp_do_debug_ip(int fd, int argc, char *argv[])
{
	struct hostent *hp;
	struct ast_hostent ahp;
	int port = 0;
	char *p, *arg;
	if (argc != 4)
		return RESULT_SHOWUSAGE;

	arg = argv[3];
	p = strstr(arg, ":");
	if (p) {
		*p = '\0';
		p++;
		port = atoi(p);
	}
	hp = ast_gethostbyname(arg, &ahp);
	if (hp == NULL)
		return RESULT_SHOWUSAGE;
	rtcpdebugaddr.sin_family = AF_INET;
	memcpy(&rtcpdebugaddr.sin_addr, hp->h_addr, sizeof(rtcpdebugaddr.sin_addr));
	rtcpdebugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(fd, "RTCP Debugging Enabled for IP: %s\n", ast_inet_ntoa(rtcpdebugaddr.sin_addr));
	else
		ast_cli(fd, "RTCP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(rtcpdebugaddr.sin_addr), port);
	rtcpdebug = 1;
	return RESULT_SUCCESS;
}

static int rtp_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2) {
		if (argc != 4)
			return RESULT_SHOWUSAGE;
		return rtp_do_debug_ip(fd, argc, argv);
	}
	rtpdebug = 1;
	memset(&rtpdebugaddr,0,sizeof(rtpdebugaddr));
	ast_cli(fd, "RTP Debugging Enabled\n");
	return RESULT_SUCCESS;
}
   
static int rtcp_do_debug_deprecated(int fd, int argc, char *argv[]) {
	if (argc != 3) {
		if (argc != 5)
			return RESULT_SHOWUSAGE;
		return rtcp_do_debug_ip_deprecated(fd, argc, argv);
	}
	rtcpdebug = 1;
	memset(&rtcpdebugaddr,0,sizeof(rtcpdebugaddr));
	ast_cli(fd, "RTCP Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int rtcp_do_debug(int fd, int argc, char *argv[]) {
	if (argc != 2) {
		if (argc != 4)
			return RESULT_SHOWUSAGE;
		return rtcp_do_debug_ip(fd, argc, argv);
	}
	rtcpdebug = 1;
	memset(&rtcpdebugaddr,0,sizeof(rtcpdebugaddr));
	ast_cli(fd, "RTCP Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int rtcp_do_stats_deprecated(int fd, int argc, char *argv[]) {
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	rtcpstats = 1;
	ast_cli(fd, "RTCP Stats Enabled\n");
	return RESULT_SUCCESS;
}

static int rtcp_do_stats(int fd, int argc, char *argv[]) {
	if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}
	rtcpstats = 1;
	ast_cli(fd, "RTCP Stats Enabled\n");
	return RESULT_SUCCESS;
}

static int rtp_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	rtpdebug = 0;
	ast_cli(fd,"RTP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int rtcp_no_debug_deprecated(int fd, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	rtcpdebug = 0;
	ast_cli(fd,"RTCP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int rtcp_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	rtcpdebug = 0;
	ast_cli(fd,"RTCP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int rtcp_no_stats_deprecated(int fd, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	rtcpstats = 0;
	ast_cli(fd,"RTCP Stats Disabled\n");
	return RESULT_SUCCESS;
}

static int rtcp_no_stats(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	rtcpstats = 0;
	ast_cli(fd,"RTCP Stats Disabled\n");
	return RESULT_SUCCESS;
}

static int stun_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}
	stundebug = 1;
	ast_cli(fd, "STUN Debugging Enabled\n");
	return RESULT_SUCCESS;
}
   
static int stun_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	stundebug = 0;
	ast_cli(fd, "STUN Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static char debug_usage[] =
  "Usage: rtp debug [ip host[:port]]\n"
  "       Enable dumping of all RTP packets to and from host.\n";

static char no_debug_usage[] =
  "Usage: rtp debug off\n"
  "       Disable all RTP debugging\n";

static char stun_debug_usage[] =
  "Usage: stun debug\n"
  "       Enable STUN (Simple Traversal of UDP through NATs) debugging\n";

static char stun_no_debug_usage[] =
  "Usage: stun debug off\n"
  "       Disable STUN debugging\n";

static char rtcp_debug_usage[] =
  "Usage: rtcp debug [ip host[:port]]\n"
  "       Enable dumping of all RTCP packets to and from host.\n";
  
static char rtcp_no_debug_usage[] =
  "Usage: rtcp debug off\n"
  "       Disable all RTCP debugging\n";

static char rtcp_stats_usage[] =
  "Usage: rtcp stats\n"
  "       Enable dumping of RTCP stats.\n";
  
static char rtcp_no_stats_usage[] =
  "Usage: rtcp stats off\n"
  "       Disable all RTCP stats\n";

static struct ast_cli_entry cli_rtp_no_debug_deprecated = {
	{ "rtp", "no", "debug", NULL },
	rtp_no_debug, NULL,
        NULL };

static struct ast_cli_entry cli_rtp_rtcp_debug_ip_deprecated = {
	{ "rtp", "rtcp", "debug", "ip", NULL },
	rtcp_do_debug_deprecated, NULL,
        NULL };

static struct ast_cli_entry cli_rtp_rtcp_debug_deprecated = {
	{ "rtp", "rtcp", "debug", NULL },
	rtcp_do_debug_deprecated, NULL,
        NULL };

static struct ast_cli_entry cli_rtp_rtcp_no_debug_deprecated = {
	{ "rtp", "rtcp", "no", "debug", NULL },
	rtcp_no_debug_deprecated, NULL,
        NULL };

static struct ast_cli_entry cli_rtp_rtcp_stats_deprecated = {
	{ "rtp", "rtcp", "stats", NULL },
	rtcp_do_stats_deprecated, NULL,
        NULL };

static struct ast_cli_entry cli_rtp_rtcp_no_stats_deprecated = {
	{ "rtp", "rtcp", "no", "stats", NULL },
	rtcp_no_stats_deprecated, NULL,
        NULL };

static struct ast_cli_entry cli_stun_no_debug_deprecated = {
	{ "stun", "no", "debug", NULL },
	stun_no_debug, NULL,
	NULL };

static struct ast_cli_entry cli_rtp[] = {
	{ { "rtp", "debug", "ip", NULL },
	rtp_do_debug, "Enable RTP debugging on IP",
	debug_usage },

	{ { "rtp", "debug", NULL },
	rtp_do_debug, "Enable RTP debugging",
	debug_usage },

	{ { "rtp", "debug", "off", NULL },
	rtp_no_debug, "Disable RTP debugging",
	no_debug_usage, NULL, &cli_rtp_no_debug_deprecated },

	{ { "rtcp", "debug", "ip", NULL },
	rtcp_do_debug, "Enable RTCP debugging on IP",
	rtcp_debug_usage, NULL, &cli_rtp_rtcp_debug_ip_deprecated },

	{ { "rtcp", "debug", NULL },
	rtcp_do_debug, "Enable RTCP debugging",
	rtcp_debug_usage, NULL, &cli_rtp_rtcp_debug_deprecated },

	{ { "rtcp", "debug", "off", NULL },
	rtcp_no_debug, "Disable RTCP debugging",
	rtcp_no_debug_usage, NULL, &cli_rtp_rtcp_no_debug_deprecated },

	{ { "rtcp", "stats", NULL },
	rtcp_do_stats, "Enable RTCP stats",
	rtcp_stats_usage, NULL, &cli_rtp_rtcp_stats_deprecated },

	{ { "rtcp", "stats", "off", NULL },
	rtcp_no_stats, "Disable RTCP stats",
	rtcp_no_stats_usage, NULL, &cli_rtp_rtcp_no_stats_deprecated },

	{ { "stun", "debug", NULL },
	stun_do_debug, "Enable STUN debugging",
	stun_debug_usage },

	{ { "stun", "debug", "off", NULL },
	stun_no_debug, "Disable STUN debugging",
	stun_no_debug_usage, NULL, &cli_stun_no_debug_deprecated },
};

int ast_rtp_reload(void)
{
	struct ast_config *cfg;
	const char *s;

	rtpstart = 5000;
	rtpend = 31000;
	dtmftimeout = DEFAULT_DTMF_TIMEOUT;
	cfg = ast_config_load("rtp.conf");
	if (cfg) {
		if ((s = ast_variable_retrieve(cfg, "general", "rtpstart"))) {
			rtpstart = atoi(s);
			if (rtpstart < 1024)
				rtpstart = 1024;
			if (rtpstart > 65535)
				rtpstart = 65535;
		}
		if ((s = ast_variable_retrieve(cfg, "general", "rtpend"))) {
			rtpend = atoi(s);
			if (rtpend < 1024)
				rtpend = 1024;
			if (rtpend > 65535)
				rtpend = 65535;
		}
		if ((s = ast_variable_retrieve(cfg, "general", "rtcpinterval"))) {
			rtcpinterval = atoi(s);
			if (rtcpinterval == 0)
				rtcpinterval = 0; /* Just so we're clear... it's zero */
			if (rtcpinterval < RTCP_MIN_INTERVALMS)
				rtcpinterval = RTCP_MIN_INTERVALMS; /* This catches negative numbers too */
			if (rtcpinterval > RTCP_MAX_INTERVALMS)
				rtcpinterval = RTCP_MAX_INTERVALMS;
		}
		if ((s = ast_variable_retrieve(cfg, "general", "rtpchecksums"))) {
#ifdef SO_NO_CHECK
			if (ast_false(s))
				nochecksums = 1;
			else
				nochecksums = 0;
#else
			if (ast_false(s))
				ast_log(LOG_WARNING, "Disabling RTP checksums is not supported on this operating system!\n");
#endif
		}
		if ((s = ast_variable_retrieve(cfg, "general", "dtmftimeout"))) {
			dtmftimeout = atoi(s);
			if ((dtmftimeout < 0) || (dtmftimeout > 64000)) {
				ast_log(LOG_WARNING, "DTMF timeout of '%d' outside range, using default of '%d' instead\n",
					dtmftimeout, DEFAULT_DTMF_TIMEOUT);
				dtmftimeout = DEFAULT_DTMF_TIMEOUT;
			};
		}
		ast_config_destroy(cfg);
	}
	if (rtpstart >= rtpend) {
		ast_log(LOG_WARNING, "Unreasonable values for RTP start/end port in rtp.conf\n");
		rtpstart = 5000;
		rtpend = 31000;
	}
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "RTP Allocating from port range %d -> %d\n", rtpstart, rtpend);
	return 0;
}

/*! \brief Initialize the RTP system in Asterisk */
void ast_rtp_init(void)
{
	ast_cli_register_multiple(cli_rtp, sizeof(cli_rtp) / sizeof(struct ast_cli_entry));
	ast_rtp_reload();
}

