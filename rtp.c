/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Real-time Protocol Support
 * 	Supports RTP and RTCP with Symmetric RTP support for NAT
 * 	traversal
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

#define RTP_MTU		1200

static int dtmftimeout = 3000;	/* 3000 samples */

static int rtpstart = 0;
static int rtpend = 0;
static int rtpdebug = 0;		/* Are we debugging? */
static struct sockaddr_in rtpdebugaddr;	/* Debug packets to/from this host */
#ifdef SO_NO_CHECK
static int nochecksums = 0;
#endif

/* The value of each payload format mapping: */
struct rtpPayloadType {
	int isAstFormat; 	/* whether the following code is an AST_FORMAT */
	int code;
};

#define MAX_RTP_PT 256

#define FLAG_3389_WARNING (1 << 0)

struct ast_rtp {
	int s;
	char resp;
	struct ast_frame f;
	unsigned char rawdata[8192 + AST_FRIENDLY_OFFSET];
	unsigned int ssrc;
	unsigned int lastts;
	unsigned int lastdigitts;
	unsigned int lastrxts;
	unsigned int lastividtimestamp;
	unsigned int lastovidtimestamp;
	unsigned int lasteventseqn;
	unsigned int lasteventendseqn;
	int lasttxformat;
	int lastrxformat;
	int dtmfcount;
	unsigned int dtmfduration;
	int nat;
	int flags;
	struct sockaddr_in us;
	struct sockaddr_in them;
	struct timeval rxcore;
	struct timeval txcore;
	struct timeval dtmfmute;
	struct ast_smoother *smoother;
	int *ioid;
	unsigned short seqno;
	unsigned short rxseqno;
	struct sched_context *sched;
	struct io_context *io;
	void *data;
	ast_rtp_callback callback;
	struct rtpPayloadType current_RTP_PT[MAX_RTP_PT];
	int rtp_lookup_code_cache_isAstFormat;	/* a cache for the result of rtp_lookup_code(): */
	int rtp_lookup_code_cache_code;
	int rtp_lookup_code_cache_result;
	int rtp_offered_from_local;
	struct ast_rtcp *rtcp;
};

struct ast_rtcp {
	int s;		/* Socket */
	struct sockaddr_in us;
	struct sockaddr_in them;
};

static struct ast_rtp_protocol *protos = NULL;

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

static struct ast_frame *send_dtmf(struct ast_rtp *rtp)
{
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	char iabuf[INET_ADDRSTRLEN];

	if (ast_tvcmp(ast_tvnow(), rtp->dtmfmute) < 0) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Ignore potential DTMF echo from '%s'\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr));
		rtp->resp = 0;
		rtp->dtmfduration = 0;
		return &null_frame;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Sending dtmf: %d (%c), at %s\n", rtp->resp, rtp->resp, ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr));
	if (rtp->resp == 'X') {
		rtp->f.frametype = AST_FRAME_CONTROL;
		rtp->f.subclass = AST_CONTROL_FLASH;
	} else {
		rtp->f.frametype = AST_FRAME_DTMF;
		rtp->f.subclass = rtp->resp;
	}
	rtp->f.datalen = 0;
	rtp->f.samples = 0;
	rtp->f.mallocd = 0;
	rtp->f.src = "RTP";
	rtp->resp = 0;
	rtp->dtmfduration = 0;
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

static struct ast_frame *process_cisco_dtmf(struct ast_rtp *rtp, unsigned char *data, int len)
{
	unsigned int event;
	char resp = 0;
	struct ast_frame *f = NULL;
	event = ntohl(*((unsigned int *)(data)));
	event &= 0x001F;
#if 0
	printf("Cisco Digit: %08x (len = %d)\n", event, len);
#endif	
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
		f = send_dtmf(rtp);
	}
	rtp->resp = resp;
	rtp->dtmfcount = dtmftimeout;
	return f;
}

/* process_rfc2833: Process RTP DTMF and events according to RFC 2833:
	"RTP Payload for DTMF Digits, Telephony Tones and Telephony Signals"
*/
static struct ast_frame *process_rfc2833(struct ast_rtp *rtp, unsigned char *data, int len, unsigned int seqno)
{
	unsigned int event;
	unsigned int event_end;
	unsigned int duration;
	char resp = 0;
	struct ast_frame *f = NULL;
	event = ntohl(*((unsigned int *)(data)));
	event >>= 24;
	event_end = ntohl(*((unsigned int *)(data)));
	event_end <<= 8;
	event_end >>= 24;
	duration = ntohl(*((unsigned int *)(data)));
	duration &= 0xFFFF;
	if (rtpdebug)
		ast_log(LOG_DEBUG, "- RTP 2833 Event: %08x (len = %d)\n", event, len);
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
	}
	if (rtp->resp && (rtp->resp != resp)) {
		f = send_dtmf(rtp);
	} else if(event_end & 0x80) {
		if (rtp->resp) {
			if(rtp->lasteventendseqn != seqno) {
				f = send_dtmf(rtp);
				rtp->lasteventendseqn = seqno;
			}
			rtp->resp = 0;
		}
		resp = 0;
		duration = 0;
	} else if(rtp->dtmfduration && (duration < rtp->dtmfduration)) {
		f = send_dtmf(rtp);
	}
	if (!(event_end & 0x80))
		rtp->resp = resp;
	rtp->dtmfcount = dtmftimeout;
	rtp->dtmfduration = duration;
	return f;
}

/*--- process_rfc3389: Process Comfort Noise RTP. 
	This is incomplete at the moment.
*/
static struct ast_frame *process_rfc3389(struct ast_rtp *rtp, unsigned char *data, int len)
{
	struct ast_frame *f = NULL;
	/* Convert comfort noise into audio with various codecs.  Unfortunately this doesn't
	   totally help us out becuase we don't have an engine to keep it going and we are not
	   guaranteed to have it every 20ms or anything */
	if (rtpdebug)
		ast_log(LOG_DEBUG, "- RTP 3389 Comfort noise event: Level %d (len = %d)\n", rtp->lastrxformat, len);
	if (!(rtp->flags & FLAG_3389_WARNING)) {
		char iabuf[INET_ADDRSTRLEN];
		ast_log(LOG_NOTICE, "Comfort noise support incomplete in Asterisk (RFC 3389).  Please turn off on client if possible. Client IP: %s\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr));
		rtp->flags |= FLAG_3389_WARNING;
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
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	socklen_t len;
	int hdrlen = 8;
	int res;
	struct sockaddr_in sin;
	unsigned int rtcpdata[1024];
	char iabuf[INET_ADDRSTRLEN];
	
	if (!rtp || !rtp->rtcp)
		return &null_frame;

	len = sizeof(sin);
	
	res = recvfrom(rtp->rtcp->s, rtcpdata, sizeof(rtcpdata),
					0, (struct sockaddr *)&sin, &len);
	
	if (res < 0) {
		if (errno != EAGAIN)
			ast_log(LOG_WARNING, "RTP Read error: %s\n", strerror(errno));
		if (errno == EBADF)
			CRASH;
		return &null_frame;
	}

	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short\n");
		return &null_frame;
	}

	if (rtp->nat) {
		/* Send to whoever sent to us */
		if ((rtp->rtcp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (rtp->rtcp->them.sin_port != sin.sin_port)) {
			memcpy(&rtp->them, &sin, sizeof(rtp->them));
			rtp->rxseqno = 0;
			if (option_debug)
				ast_log(LOG_DEBUG, "RTP NAT: Using address %s:%d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
		}
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Got RTCP report of %d bytes\n", res);
	return &null_frame;
}

static void calc_rxstamp(struct timeval *tv, struct ast_rtp *rtp, unsigned int timestamp, int mark)
{
	struct timeval ts = ast_samp2tv( timestamp, 8000);
	if (ast_tvzero(rtp->rxcore) || mark) {
		rtp->rxcore = ast_tvsub(ast_tvnow(), ts);
		/* Round to 20ms for nice, pretty timestamps */
		rtp->rxcore.tv_usec -= rtp->rxcore.tv_usec % 20000;
	}
	*tv = ast_tvadd(rtp->rxcore, ts);
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
	int x;
	char iabuf[INET_ADDRSTRLEN];
	unsigned int timestamp;
	unsigned int *rtpheader;
	static struct ast_frame *f, null_frame = { AST_FRAME_NULL, };
	struct rtpPayloadType rtpPT;
	
	len = sizeof(sin);
	
	/* Cache where the header will go */
	res = recvfrom(rtp->s, rtp->rawdata + AST_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - AST_FRIENDLY_OFFSET,
					0, (struct sockaddr *)&sin, &len);


	rtpheader = (unsigned int *)(rtp->rawdata + AST_FRIENDLY_OFFSET);
	if (res < 0) {
		if (errno != EAGAIN)
			ast_log(LOG_WARNING, "RTP Read error: %s\n", strerror(errno));
		if (errno == EBADF)
			CRASH;
		return &null_frame;
	}
	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short\n");
		return &null_frame;
	}

	/* Ignore if the other side hasn't been given an address
	   yet.  */
	if (!rtp->them.sin_addr.s_addr || !rtp->them.sin_port)
		return &null_frame;

	if (rtp->nat) {
		/* Send to whoever sent to us */
		if ((rtp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (rtp->them.sin_port != sin.sin_port)) {
			memcpy(&rtp->them, &sin, sizeof(rtp->them));
			rtp->rxseqno = 0;
			ast_log(LOG_DEBUG, "RTP NAT: Using address %s:%d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port));
		}
	}

	/* Get fields */
	seqno = ntohl(rtpheader[0]);

	/* Check RTP version */
	version = (seqno & 0xC0000000) >> 30;
	if (version != 2)
		return &null_frame;
	
	payloadtype = (seqno & 0x7f0000) >> 16;
	padding = seqno & (1 << 29);
	mark = seqno & (1 << 23);
	ext = seqno & (1 << 28);
	seqno &= 0xffff;
	timestamp = ntohl(rtpheader[1]);
	
	if (padding) {
		/* Remove padding bytes */
		res -= rtp->rawdata[AST_FRIENDLY_OFFSET + res - 1];
	}
	
	if (ext) {
		/* RTP Extension present */
		hdrlen += 4;
		hdrlen += (ntohl(rtpheader[3]) & 0xffff) << 2;
	}

	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short (%d, expecting %d)\n", res, hdrlen);
		return &null_frame;
	}

	if(rtp_debug_test_addr(&sin))
		ast_verbose("Got RTP packet from %s:%d (type %d, seq %d, ts %d, len %d)\n"
			, ast_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp,res - hdrlen);

        rtpPT = ast_rtp_lookup_pt(rtp, payloadtype);
	if (!rtpPT.isAstFormat) {
		/* This is special in-band data that's not one of our codecs */
		if (rtpPT.code == AST_RTP_DTMF) {
			/* It's special -- rfc2833 process it */
			if(rtp_debug_test_addr(&sin)) {
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
				ast_verbose("Got rfc2833 RTP packet from %s:%d (type %d, seq %d, ts %d, len %d, mark %d, event %08x, end %d, duration %d) \n", ast_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp, res - hdrlen, (mark?1:0), event, ((event_end & 0x80)?1:0), duration);
			}
	    		if (rtp->lasteventseqn <= seqno || rtp->resp == 0 || (rtp->lasteventseqn >= 65530 && seqno <= 6)) {
	      			f = process_rfc2833(rtp, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno);
	      			rtp->lasteventseqn = seqno;
	    		} else 
				f = NULL;
	    		if (f) 
				return f; 
			else 
				return &null_frame;
	  	} else if (rtpPT.code == AST_RTP_CISCO_DTMF) {
	    		/* It's really special -- process it the Cisco way */
	    		if (rtp->lasteventseqn <= seqno || rtp->resp == 0 || (rtp->lasteventseqn >= 65530 && seqno <= 6)) {
	      			f = process_cisco_dtmf(rtp, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
	      			rtp->lasteventseqn = seqno;
	    		} else 
				f = NULL;
	    		if (f) 
				return f; 
			else 
				return &null_frame;
	  	} else if (rtpPT.code == AST_RTP_CN) {
	    		/* Comfort Noise */
	    		f = process_rfc3389(rtp, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
	    		if (f) 
				return f; 
			else 
				return &null_frame;
	  	} else {
	    		ast_log(LOG_NOTICE, "Unknown RTP codec %d received\n", payloadtype);
	    		return &null_frame;
	  	}
	}
	rtp->f.subclass = rtpPT.code;
	if (rtp->f.subclass < AST_FORMAT_MAX_AUDIO)
		rtp->f.frametype = AST_FRAME_VOICE;
	else
		rtp->f.frametype = AST_FRAME_VIDEO;
	rtp->lastrxformat = rtp->f.subclass;

	if (!rtp->lastrxts)
		rtp->lastrxts = timestamp;

	if (rtp->rxseqno) {
		for (x=rtp->rxseqno + 1; x < seqno; x++) {
			/* Queue empty frames */
			rtp->f.mallocd = 0;
			rtp->f.datalen = 0;
			rtp->f.data = NULL;
			rtp->f.offset = 0;
			rtp->f.samples = 0;
			rtp->f.src = "RTPMissedFrame";
		}
	}
	rtp->rxseqno = seqno;

	if (rtp->dtmfcount) {
#if 0
		printf("dtmfcount was %d\n", rtp->dtmfcount);
#endif		
		rtp->dtmfcount -= (timestamp - rtp->lastrxts);
		if (rtp->dtmfcount < 0)
			rtp->dtmfcount = 0;
#if 0
		if (dtmftimeout != rtp->dtmfcount)
			printf("dtmfcount is %d\n", rtp->dtmfcount);
#endif
	}
	rtp->lastrxts = timestamp;

	/* Send any pending DTMF */
	if (rtp->resp && !rtp->dtmfcount) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Sending pending DTMF\n");
		return send_dtmf(rtp);
	}
	rtp->f.mallocd = 0;
	rtp->f.datalen = res - hdrlen;
	rtp->f.data = rtp->rawdata + hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.offset = hdrlen + AST_FRIENDLY_OFFSET;
	if (rtp->f.subclass < AST_FORMAT_MAX_AUDIO) {
		rtp->f.samples = ast_codec_get_samples(&rtp->f);
		if (rtp->f.subclass == AST_FORMAT_SLINEAR) 
			ast_frame_byteswap_be(&rtp->f);
		calc_rxstamp(&rtp->f.delivery, rtp, timestamp, mark);
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
  {{1, AST_FORMAT_ALAW}, "audio", "PCMA"},
  {{1, AST_FORMAT_G726}, "audio", "G726-32"},
  {{1, AST_FORMAT_ADPCM}, "audio", "DVI4"},
  {{1, AST_FORMAT_SLINEAR}, "audio", "L16"},
  {{1, AST_FORMAT_LPC10}, "audio", "LPC"},
  {{1, AST_FORMAT_G729A}, "audio", "G729"},
  {{1, AST_FORMAT_SPEEX}, "audio", "speex"},
  {{1, AST_FORMAT_ILBC}, "audio", "iLBC"},
  {{0, AST_RTP_DTMF}, "audio", "telephone-event"},
  {{0, AST_RTP_CISCO_DTMF}, "audio", "cisco-telephone-event"},
  {{0, AST_RTP_CN}, "audio", "CN"},
  {{1, AST_FORMAT_JPEG}, "video", "JPEG"},
  {{1, AST_FORMAT_PNG}, "video", "PNG"},
  {{1, AST_FORMAT_H261}, "video", "H261"},
  {{1, AST_FORMAT_H263}, "video", "H263"},
  {{1, AST_FORMAT_H263_PLUS}, "video", "h263-1998"},
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
  [101] = {0, AST_RTP_DTMF},
  [110] = {1, AST_FORMAT_SPEEX},
  [111] = {1, AST_FORMAT_G726},
  [121] = {0, AST_RTP_CISCO_DTMF}, /* Must be type 121 */
};

void ast_rtp_pt_clear(struct ast_rtp* rtp) 
{
	int i;

	for (i = 0; i < MAX_RTP_PT; ++i) {
		rtp->current_RTP_PT[i].isAstFormat = 0;
		rtp->current_RTP_PT[i].code = 0;
	}

	rtp->rtp_lookup_code_cache_isAstFormat = 0;
	rtp->rtp_lookup_code_cache_code = 0;
	rtp->rtp_lookup_code_cache_result = 0;
}

void ast_rtp_pt_default(struct ast_rtp* rtp) 
{
	int i;

	/* Initialize to default payload types */
	for (i = 0; i < MAX_RTP_PT; ++i) {
		rtp->current_RTP_PT[i].isAstFormat = static_RTP_PT[i].isAstFormat;
		rtp->current_RTP_PT[i].code = static_RTP_PT[i].code;
	}

	rtp->rtp_lookup_code_cache_isAstFormat = 0;
	rtp->rtp_lookup_code_cache_code = 0;
	rtp->rtp_lookup_code_cache_result = 0;
}

/* Make a note of a RTP payload type that was seen in a SDP "m=" line. */
/* By default, use the well-known value for this type (although it may */
/* still be set to a different value by a subsequent "a=rtpmap:" line): */
void ast_rtp_set_m_type(struct ast_rtp* rtp, int pt) {
	if (pt < 0 || pt > MAX_RTP_PT) 
		return; /* bogus payload type */

	if (static_RTP_PT[pt].code != 0) {
		rtp->current_RTP_PT[pt] = static_RTP_PT[pt];
	}
} 

/* Make a note of a RTP payload type (with MIME type) that was seen in */
/* a SDP "a=rtpmap:" line. */
void ast_rtp_set_rtpmap_type(struct ast_rtp* rtp, int pt,
			 char* mimeType, char* mimeSubtype) {
	int i;

	if (pt < 0 || pt > MAX_RTP_PT) 
			return; /* bogus payload type */

	for (i = 0; i < sizeof mimeTypes/sizeof mimeTypes[0]; ++i) {
		if (strcasecmp(mimeSubtype, mimeTypes[i].subtype) == 0 &&
		     strcasecmp(mimeType, mimeTypes[i].type) == 0) {
			rtp->current_RTP_PT[pt] = mimeTypes[i].payloadType;
		return;
		}
	}
} 

/* Return the union of all of the codecs that were set by rtp_set...() calls */
/* They're returned as two distinct sets: AST_FORMATs, and AST_RTPs */
void ast_rtp_get_current_formats(struct ast_rtp* rtp,
			     int* astFormats, int* nonAstFormats) {
	int pt;

	*astFormats = *nonAstFormats = 0;
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
		if (rtp->current_RTP_PT[pt].isAstFormat) {
			*astFormats |= rtp->current_RTP_PT[pt].code;
		} else {
			*nonAstFormats |= rtp->current_RTP_PT[pt].code;
		}
	}
}

void ast_rtp_offered_from_local(struct ast_rtp* rtp, int local) {
	if (rtp)
		rtp->rtp_offered_from_local = local;
	else
		ast_log(LOG_WARNING, "rtp structure is null\n");
}

struct rtpPayloadType ast_rtp_lookup_pt(struct ast_rtp* rtp, int pt) 
{
	struct rtpPayloadType result;

	result.isAstFormat = result.code = 0;
	if (pt < 0 || pt > MAX_RTP_PT) 
		return result; /* bogus payload type */

	/* Start with the negotiated codecs */
	if (!rtp->rtp_offered_from_local)
		result = rtp->current_RTP_PT[pt];

	/* If it doesn't exist, check our static RTP type list, just in case */
	if (!result.code) 
		result = static_RTP_PT[pt];
	return result;
}

/* Looks up an RTP code out of our *static* outbound list */
int ast_rtp_lookup_code(struct ast_rtp* rtp, const int isAstFormat, const int code) {

	int pt;

	if (isAstFormat == rtp->rtp_lookup_code_cache_isAstFormat &&
		code == rtp->rtp_lookup_code_cache_code) {

		/* Use our cached mapping, to avoid the overhead of the loop below */
		return rtp->rtp_lookup_code_cache_result;
	}

	/* Check the dynamic list first */
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
  		if (rtp->current_RTP_PT[pt].code == code && rtp->current_RTP_PT[pt].isAstFormat == isAstFormat) {
			rtp->rtp_lookup_code_cache_isAstFormat = isAstFormat;
			rtp->rtp_lookup_code_cache_code = code;
			rtp->rtp_lookup_code_cache_result = pt;
			return pt;
		}
	}

	/* Then the static list */
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
		if (static_RTP_PT[pt].code == code && static_RTP_PT[pt].isAstFormat == isAstFormat) {
			rtp->rtp_lookup_code_cache_isAstFormat = isAstFormat;
  			rtp->rtp_lookup_code_cache_code = code;
			rtp->rtp_lookup_code_cache_result = pt;
			return pt;
		}
	}
	return -1;
}

char* ast_rtp_lookup_mime_subtype(const int isAstFormat, const int code) {

	int i;

	for (i = 0; i < sizeof mimeTypes/sizeof mimeTypes[0]; ++i) {
	if (mimeTypes[i].payloadType.code == code && mimeTypes[i].payloadType.isAstFormat == isAstFormat) {
      		return mimeTypes[i].subtype;
		}
	}
	return "";
}

char *ast_rtp_lookup_mime_multiple(char *buf, int size, const int capability, const int isAstFormat)
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
			const char *name = ast_rtp_lookup_mime_subtype(isAstFormat, format);
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

static struct ast_rtcp *ast_rtcp_new(void)
{
	struct ast_rtcp *rtcp;
	rtcp = malloc(sizeof(struct ast_rtcp));
	if (!rtcp)
		return NULL;
	memset(rtcp, 0, sizeof(struct ast_rtcp));
	rtcp->s = rtp_socket();
	rtcp->us.sin_family = AF_INET;
	if (rtcp->s < 0) {
		free(rtcp);
		ast_log(LOG_WARNING, "Unable to allocate socket: %s\n", strerror(errno));
		return NULL;
	}
	return rtcp;
}

struct ast_rtp *ast_rtp_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode, struct in_addr addr)
{
	struct ast_rtp *rtp;
	int x;
	int first;
	int startplace;
	rtp = malloc(sizeof(struct ast_rtp));
	if (!rtp)
		return NULL;
	memset(rtp, 0, sizeof(struct ast_rtp));
	rtp->them.sin_family = AF_INET;
	rtp->us.sin_family = AF_INET;
	rtp->s = rtp_socket();
	rtp->ssrc = rand();
	rtp->seqno = rand() & 0xffff;
	if (rtp->s < 0) {
		free(rtp);
		ast_log(LOG_ERROR, "Unable to allocate socket: %s\n", strerror(errno));
		return NULL;
	}
	if (sched && rtcpenable) {
		rtp->sched = sched;
		rtp->rtcp = ast_rtcp_new();
	}
	/* Find us a place */
	x = (rand() % (rtpend-rtpstart)) + rtpstart;
	x = x & ~1;
	startplace = x;
	for (;;) {
		/* Must be an even port number by RTP spec */
		rtp->us.sin_port = htons(x);
		rtp->us.sin_addr = addr;
		if (rtp->rtcp)
			rtp->rtcp->us.sin_port = htons(x + 1);
		if (!(first = bind(rtp->s, (struct sockaddr *)&rtp->us, sizeof(rtp->us))) &&
			(!rtp->rtcp || !bind(rtp->rtcp->s, (struct sockaddr *)&rtp->rtcp->us, sizeof(rtp->rtcp->us))))
			break;
		if (!first) {
			/* Primary bind succeeded! Gotta recreate it */
			close(rtp->s);
			rtp->s = rtp_socket();
		}
		if (errno != EADDRINUSE) {
			ast_log(LOG_ERROR, "Unexpected bind error: %s\n", strerror(errno));
			close(rtp->s);
			if (rtp->rtcp) {
				close(rtp->rtcp->s);
				free(rtp->rtcp);
			}
			free(rtp);
			return NULL;
		}
		x += 2;
		if (x > rtpend)
			x = (rtpstart + 1) & ~1;
		if (x == startplace) {
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
	if (io && sched && callbackmode) {
		/* Operate this one in a callback mode */
		rtp->sched = sched;
		rtp->io = io;
		rtp->ioid = ast_io_add(rtp->io, rtp->s, rtpread, AST_IO_IN, rtp);
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

void ast_rtp_get_peer(struct ast_rtp *rtp, struct sockaddr_in *them)
{
	them->sin_family = AF_INET;
	them->sin_port = rtp->them.sin_port;
	them->sin_addr = rtp->them.sin_addr;
}

void ast_rtp_get_us(struct ast_rtp *rtp, struct sockaddr_in *us)
{
	memcpy(us, &rtp->us, sizeof(rtp->us));
}

void ast_rtp_stop(struct ast_rtp *rtp)
{
	memset(&rtp->them.sin_addr, 0, sizeof(rtp->them.sin_addr));
	memset(&rtp->them.sin_port, 0, sizeof(rtp->them.sin_port));
	if (rtp->rtcp) {
		memset(&rtp->rtcp->them.sin_addr, 0, sizeof(rtp->them.sin_addr));
		memset(&rtp->rtcp->them.sin_port, 0, sizeof(rtp->them.sin_port));
	}
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
	rtp->lasteventendseqn = 0;
	rtp->lasttxformat = 0;
	rtp->lastrxformat = 0;
	rtp->dtmfcount = 0;
	rtp->dtmfduration = 0;
	rtp->seqno = 0;
	rtp->rxseqno = 0;
}

void ast_rtp_destroy(struct ast_rtp *rtp)
{
	if (rtp->smoother)
		ast_smoother_free(rtp->smoother);
	if (rtp->ioid)
		ast_io_remove(rtp->io, rtp->ioid);
	if (rtp->s > -1)
		close(rtp->s);
	if (rtp->rtcp) {
		close(rtp->rtcp->s);
		free(rtp->rtcp);
	}
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
	/* Use what we just got for next time */
	rtp->txcore = t;
	return (unsigned int) ms;
}

int ast_rtp_senddigit(struct ast_rtp *rtp, char digit)
{
	unsigned int *rtpheader;
	int hdrlen = 12;
	int res;
	int x;
	int payload;
	char data[256];
	char iabuf[INET_ADDRSTRLEN];

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
		return -1;
	}
	payload = ast_rtp_lookup_code(rtp, 0, AST_RTP_DTMF);

	/* If we have no peer, return immediately */	
	if (!rtp->them.sin_addr.s_addr)
		return 0;

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));
	
	/* Get a pointer to the header */
	rtpheader = (unsigned int *)data;
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc); 
	rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (0));
	for (x = 0; x < 6; x++) {
		if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
			res = sendto(rtp->s, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &rtp->them, sizeof(rtp->them));
			if (res < 0) 
				ast_log(LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
					ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr),
					ntohs(rtp->them.sin_port), strerror(errno));
			if (rtp_debug_test_addr(&rtp->them))
				ast_verbose("Sent RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n",
					    ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr),
					    ntohs(rtp->them.sin_port), payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
		}
		/* Sequence number of last two end packets does not get incremented */
		if (x < 3)
			rtp->seqno++;
		/* Clear marker bit and set seqno */
		rtpheader[0] = htonl((2 << 30) | (payload << 16) | (rtp->seqno));
		/* For the last three packets, set the duration and the end bit */
		if (x == 2) {
#if 0
			/* No, this is wrong...  Do not increment lastdigitts, that's not according
			   to the RFC, as best we can determine */
			rtp->lastdigitts++; /* or else the SPA3000 will click instead of beeping... */
			rtpheader[1] = htonl(rtp->lastdigitts);
#endif			
			/* Make duration 800 (100ms) */
			rtpheader[3] |= htonl((800));
			/* Set the End bit */
			rtpheader[3] |= htonl((1 << 23));
		}
	}
	/* Increment the digit timestamp by 120ms, to ensure that digits
	   sent sequentially with no intervening non-digit packets do not
	   get sent with the same timestamp, and that sequential digits
	   have some 'dead air' in between them
	*/
	rtp->lastdigitts += 960;
	/* Increment the sequence number to reflect the last packet
	   that was sent
	*/
	rtp->seqno++;
	return 0;
}

int ast_rtp_sendcng(struct ast_rtp *rtp, int level)
{
	unsigned int *rtpheader;
	int hdrlen = 12;
	int res;
	int payload;
	char data[256];
	char iabuf[INET_ADDRSTRLEN];
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
			ast_log(LOG_ERROR, "RTP Comfort Noise Transmission error to %s:%d: %s\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), strerror(errno));
		if(rtp_debug_test_addr(&rtp->them))
			ast_verbose("Sent Comfort Noise RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n"
					, ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), payload, rtp->seqno, rtp->lastts,res - hdrlen);		   
		   
	}
	return 0;
}

static int ast_rtp_raw_write(struct ast_rtp *rtp, struct ast_frame *f, int codec)
{
	unsigned char *rtpheader;
	char iabuf[INET_ADDRSTRLEN];
	int hdrlen = 12;
	int res;
	int ms;
	int pred;
	int mark = 0;

	ms = calc_txstamp(rtp, &f->delivery);
	/* Default prediction */
	if (f->subclass < AST_FORMAT_MAX_AUDIO) {
                pred = rtp->lastts + ast_codec_get_samples(f);

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
	} else {
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
	/* If the timestamp for non-digit packets has moved beyond the timestamp
	   for digits, update the digit timestamp.
	*/
	if (rtp->lastts > rtp->lastdigitts)
		rtp->lastdigitts = rtp->lastts;

	/* Get a pointer to the header */
	rtpheader = (unsigned char *)(f->data - hdrlen);

	put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (rtp->seqno) | (mark << 23)));
	put_unaligned_uint32(rtpheader + 4, htonl(rtp->lastts));
	put_unaligned_uint32(rtpheader + 8, htonl(rtp->ssrc)); 

	if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
		res = sendto(rtp->s, (void *)rtpheader, f->datalen + hdrlen, 0, (struct sockaddr *)&rtp->them, sizeof(rtp->them));
		if (res <0) 
			ast_log(LOG_NOTICE, "RTP Transmission error to %s:%d: %s\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), strerror(errno));
		if(rtp_debug_test_addr(&rtp->them))
			ast_verbose("Sent RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n"
					, ast_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), codec, rtp->seqno, rtp->lastts,res - hdrlen);
	}

	rtp->seqno++;

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
		ast_log(LOG_WARNING, "RTP can only send voice\n");
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


	switch(subclass) {
	case AST_FORMAT_SLINEAR:
		if (!rtp->smoother) {
			rtp->smoother = ast_smoother_new(320);
		}
		if (!rtp->smoother) {
			ast_log(LOG_WARNING, "Unable to create smoother :(\n");
			return -1;
		}
		ast_smoother_feed_be(rtp->smoother, _f);
		
		while((f = ast_smoother_read(rtp->smoother)))
			ast_rtp_raw_write(rtp, f, codec);
		break;
	case AST_FORMAT_ULAW:
	case AST_FORMAT_ALAW:
		if (!rtp->smoother) {
			rtp->smoother = ast_smoother_new(160);
		}
		if (!rtp->smoother) {
			ast_log(LOG_WARNING, "Unable to create smoother :(\n");
			return -1;
		}
		ast_smoother_feed(rtp->smoother, _f);
		
		while((f = ast_smoother_read(rtp->smoother)))
			ast_rtp_raw_write(rtp, f, codec);
		break;
	case AST_FORMAT_ADPCM:
	case AST_FORMAT_G726:
		if (!rtp->smoother) {
			rtp->smoother = ast_smoother_new(80);
		}
		if (!rtp->smoother) {
			ast_log(LOG_WARNING, "Unable to create smoother :(\n");
			return -1;
		}
		ast_smoother_feed(rtp->smoother, _f);
		
		while((f = ast_smoother_read(rtp->smoother)))
			ast_rtp_raw_write(rtp, f, codec);
		break;
	case AST_FORMAT_G729A:
		if (!rtp->smoother) {
			rtp->smoother = ast_smoother_new(20);
			if (rtp->smoother)
				ast_smoother_set_flags(rtp->smoother, AST_SMOOTHER_FLAG_G729);
		}
		if (!rtp->smoother) {
			ast_log(LOG_WARNING, "Unable to create g729 smoother :(\n");
			return -1;
		}
		ast_smoother_feed(rtp->smoother, _f);
		
		while((f = ast_smoother_read(rtp->smoother)))
			ast_rtp_raw_write(rtp, f, codec);
		break;
	case AST_FORMAT_GSM:
		if (!rtp->smoother) {
			rtp->smoother = ast_smoother_new(33);
		}
		if (!rtp->smoother) {
			ast_log(LOG_WARNING, "Unable to create GSM smoother :(\n");
			return -1;
		}
		ast_smoother_feed(rtp->smoother, _f);
		while((f = ast_smoother_read(rtp->smoother)))
			ast_rtp_raw_write(rtp, f, codec);
		break;
	case AST_FORMAT_ILBC:
		if (!rtp->smoother) {
			rtp->smoother = ast_smoother_new(50);
		}
		if (!rtp->smoother) {
			ast_log(LOG_WARNING, "Unable to create ILBC smoother :(\n");
			return -1;
		}
		ast_smoother_feed(rtp->smoother, _f);
		while((f = ast_smoother_read(rtp->smoother)))
			ast_rtp_raw_write(rtp, f, codec);
		break;
	default:	
		ast_log(LOG_WARNING, "Not sure about sending format %s packets\n", ast_getformatname(subclass));
		/* fall through to... */
	case AST_FORMAT_H261:
	case AST_FORMAT_H263:
	case AST_FORMAT_H263_PLUS:
	case AST_FORMAT_G723_1:
	case AST_FORMAT_LPC10:
	case AST_FORMAT_SPEEX:
	        /* Don't buffer outgoing frames; send them one-per-packet: */
		if (_f->offset < hdrlen) {
			f = ast_frdup(_f);
		} else {
			f = _f;
		}
		ast_rtp_raw_write(rtp, f, codec);
	}
		
	return 0;
}

void ast_rtp_proto_unregister(struct ast_rtp_protocol *proto)
{
	struct ast_rtp_protocol *cur, *prev;

	cur = protos;
	prev = NULL;
	while(cur) {
		if (cur == proto) {
			if (prev)
				prev->next = proto->next;
			else
				protos = proto->next;
			return;
		}
		prev = cur;
		cur = cur->next;
	}
}

int ast_rtp_proto_register(struct ast_rtp_protocol *proto)
{
	struct ast_rtp_protocol *cur;
	cur = protos;
	while(cur) {
		if (cur->type == proto->type) {
			ast_log(LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
			return -1;
		}
		cur = cur->next;
	}
	proto->next = protos;
	protos = proto;
	return 0;
}

static struct ast_rtp_protocol *get_proto(struct ast_channel *chan)
{
	struct ast_rtp_protocol *cur;
	cur = protos;
	while(cur) {
		if (cur->type == chan->type) {
			return cur;
		}
		cur = cur->next;
	}
	return NULL;
}

/* ast_rtp_bridge: Bridge calls. If possible and allowed, initiate
	re-invite so the peers exchange media directly outside 
	of Asterisk. */
int ast_rtp_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	struct ast_frame *f;
	struct ast_channel *who, *cs[3];
	struct ast_rtp *p0, *p1;
	struct ast_rtp *vp0, *vp1;
	struct ast_rtp_protocol *pr0, *pr1;
	struct sockaddr_in ac0, ac1;
	struct sockaddr_in vac0, vac1;
	struct sockaddr_in t0, t1;
	struct sockaddr_in vt0, vt1;
	char iabuf[INET_ADDRSTRLEN];
	
	void *pvt0, *pvt1;
	int to;
	int codec0,codec1, oldcodec0, oldcodec1;
	
	memset(&vt0, 0, sizeof(vt0));
	memset(&vt1, 0, sizeof(vt1));
	memset(&vac0, 0, sizeof(vac0));
	memset(&vac1, 0, sizeof(vac1));

	/* if need DTMF, cant native bridge */
	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return -2;
	ast_mutex_lock(&c0->lock);
	while(ast_mutex_trylock(&c1->lock)) {
		ast_mutex_unlock(&c0->lock);
		usleep(1);
		ast_mutex_lock(&c0->lock);
	}
	pr0 = get_proto(c0);
	pr1 = get_proto(c1);
	if (!pr0) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c0->name);
		ast_mutex_unlock(&c0->lock);
		ast_mutex_unlock(&c1->lock);
		return -1;
	}
	if (!pr1) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c1->name);
		ast_mutex_unlock(&c0->lock);
		ast_mutex_unlock(&c1->lock);
		return -1;
	}
	pvt0 = c0->tech_pvt;
	pvt1 = c1->tech_pvt;
	p0 = pr0->get_rtp_info(c0);
	if (pr0->get_vrtp_info)
		vp0 = pr0->get_vrtp_info(c0);
	else
		vp0 = NULL;
	p1 = pr1->get_rtp_info(c1);
	if (pr1->get_vrtp_info)
		vp1 = pr1->get_vrtp_info(c1);
	else
		vp1 = NULL;
	if (!p0 || !p1) {
		/* Somebody doesn't want to play... */
		ast_mutex_unlock(&c0->lock);
		ast_mutex_unlock(&c1->lock);
		return -2;
	}
	if (pr0->get_codec)
		codec0 = pr0->get_codec(c0);
	else
		codec0 = 0;
	if (pr1->get_codec)
		codec1 = pr1->get_codec(c1);
	else
		codec1 = 0;
	if (pr0->get_codec && pr1->get_codec) {
		/* Hey, we can't do reinvite if both parties speak different codecs */
		if (!(codec0 & codec1)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Channel codec0 = %d is not codec1 = %d, cannot native bridge in RTP.\n", codec0, codec1);
			ast_mutex_unlock(&c0->lock);
			ast_mutex_unlock(&c1->lock);
			return -2;
		}
	}
	/* Ok, we should be able to redirect the media. Start with one channel */
	if (pr0->set_rtp_peer(c0, p1, vp1, codec1)) 
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
	else {
		/* Store RTP peer */
		ast_rtp_get_peer(p1, &ac1);
		if (vp1)
			ast_rtp_get_peer(vp1, &vac1);
	}
	/* Then test the other channel */
	if (pr1->set_rtp_peer(c1, p0, vp0, codec0))
		ast_log(LOG_WARNING, "Channel '%s' failed to talk back to '%s'\n", c1->name, c0->name);
	else {
		/* Store RTP peer */
		ast_rtp_get_peer(p0, &ac0);
		if (vp0)
			ast_rtp_get_peer(vp0, &vac0);
	}
	ast_mutex_unlock(&c0->lock);
	ast_mutex_unlock(&c1->lock);
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	oldcodec0 = codec0;
	oldcodec1 = codec1;
	for (;;) {
		if ((c0->tech_pvt != pvt0)  ||
			(c1->tech_pvt != pvt1) ||
			(c0->masq || c0->masqr || c1->masq || c1->masqr)) {
				ast_log(LOG_DEBUG, "Oooh, something is weird, backing out\n");
				if (c0->tech_pvt == pvt0) {
					if (pr0->set_rtp_peer(c0, NULL, NULL, 0)) 
						ast_log(LOG_WARNING, "Channel '%s' failed to revert\n", c0->name);
				}
				if (c1->tech_pvt == pvt1) {
					if (pr1->set_rtp_peer(c1, NULL, NULL, 0)) 
						ast_log(LOG_WARNING, "Channel '%s' failed to revert back\n", c1->name);
				}
				/* Tell it to try again later */
				return -3;
		}
		to = -1;
		ast_rtp_get_peer(p1, &t1);
		ast_rtp_get_peer(p0, &t0);
		if (pr0->get_codec)
			codec0 = pr0->get_codec(c0);
		if (pr1->get_codec)
			codec1 = pr1->get_codec(c1);
		if (vp1)
			ast_rtp_get_peer(vp1, &vt1);
		if (vp0)
			ast_rtp_get_peer(vp0, &vt0);
		if (inaddrcmp(&t1, &ac1) || (vp1 && inaddrcmp(&vt1, &vac1)) || (codec1 != oldcodec1)) {
			if (option_debug) {
				ast_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n", 
					c1->name, ast_inet_ntoa(iabuf, sizeof(iabuf), t1.sin_addr), ntohs(t1.sin_port), codec1);
				ast_log(LOG_DEBUG, "Oooh, '%s' changed end vaddress to %s:%d (format %d)\n", 
					c1->name, ast_inet_ntoa(iabuf, sizeof(iabuf), vt1.sin_addr), ntohs(vt1.sin_port), codec1);
				ast_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
					c1->name, ast_inet_ntoa(iabuf, sizeof(iabuf), ac1.sin_addr), ntohs(ac1.sin_port), oldcodec1);
				ast_log(LOG_DEBUG, "Oooh, '%s' wasv %s:%d/(format %d)\n", 
					c1->name, ast_inet_ntoa(iabuf, sizeof(iabuf), vac1.sin_addr), ntohs(vac1.sin_port), oldcodec1);
			}
			if (pr0->set_rtp_peer(c0, t1.sin_addr.s_addr ? p1 : NULL, vt1.sin_addr.s_addr ? vp1 : NULL, codec1)) 
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c0->name, c1->name);
			memcpy(&ac1, &t1, sizeof(ac1));
			memcpy(&vac1, &vt1, sizeof(vac1));
			oldcodec1 = codec1;
		}
		if (inaddrcmp(&t0, &ac0) || (vp0 && inaddrcmp(&vt0, &vac0))) {
			if (option_debug) {
				ast_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n", 
					c0->name, ast_inet_ntoa(iabuf, sizeof(iabuf), t0.sin_addr), ntohs(t0.sin_port), codec0);
				ast_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
					c0->name, ast_inet_ntoa(iabuf, sizeof(iabuf), ac0.sin_addr), ntohs(ac0.sin_port), oldcodec0);
			}
			if (pr1->set_rtp_peer(c1, t0.sin_addr.s_addr ? p0 : NULL, vt0.sin_addr.s_addr ? vp0 : NULL, codec0))
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c1->name, c0->name);
			memcpy(&ac0, &t0, sizeof(ac0));
			memcpy(&vac0, &vt0, sizeof(vac0));
			oldcodec0 = codec0;
		}
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Ooh, empty read...\n");
			/* check for hangup / whentohangup */
			if (ast_check_hangup(c0) || ast_check_hangup(c1))
				break;
			continue;
		}
		f = ast_read(who);
		if (!f || ((f->frametype == AST_FRAME_DTMF) &&
				   (((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) || 
			       ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1))))) {
			*fo = f;
			*rc = who;
			if (option_debug)
				ast_log(LOG_DEBUG, "Oooh, got a %s\n", f ? "digit" : "hangup");
			if ((c0->tech_pvt == pvt0) && (!c0->_softhangup)) {
				if (pr0->set_rtp_peer(c0, NULL, NULL, 0)) 
					ast_log(LOG_WARNING, "Channel '%s' failed to revert\n", c0->name);
			}
			if ((c1->tech_pvt == pvt1) && (!c1->_softhangup)) {
				if (pr1->set_rtp_peer(c1, NULL, NULL, 0)) 
					ast_log(LOG_WARNING, "Channel '%s' failed to revert back\n", c1->name);
			}
			/* That's all we needed */
			return 0;
		} else {
			if ((f->frametype == AST_FRAME_DTMF) || 
				(f->frametype == AST_FRAME_VOICE) || 
				(f->frametype == AST_FRAME_VIDEO)) {
				/* Forward voice or DTMF frames if they happen upon us */
				if (who == c0) {
					ast_write(c1, f);
				} else if (who == c1) {
					ast_write(c0, f);
				}
			}
			ast_frfree(f);
		}
		/* Swap priority not that it's a big deal at this point */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
		
	}
	return -1;
}

static int rtp_do_debug_ip(int fd, int argc, char *argv[])
{
	struct hostent *hp;
	struct ast_hostent ahp;
	char iabuf[INET_ADDRSTRLEN];
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
		ast_cli(fd, "RTP Debugging Enabled for IP: %s\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtpdebugaddr.sin_addr));
	else
		ast_cli(fd, "RTP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), rtpdebugaddr.sin_addr), port);
	rtpdebug = 1;
	return RESULT_SUCCESS;
}

static int rtp_do_debug(int fd, int argc, char *argv[])
{
	if(argc != 2){
		if(argc != 4)
			return RESULT_SHOWUSAGE;
		return rtp_do_debug_ip(fd, argc, argv);
	}
	rtpdebug = 1;
	memset(&rtpdebugaddr,0,sizeof(rtpdebugaddr));
	ast_cli(fd, "RTP Debugging Enabled\n");
	return RESULT_SUCCESS;
}
   
static int rtp_no_debug(int fd, int argc, char *argv[])
{
	if(argc !=3)
		return RESULT_SHOWUSAGE;
	rtpdebug = 0;
	ast_cli(fd,"RTP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static char debug_usage[] =
  "Usage: rtp debug [ip host[:port]]\n"
  "       Enable dumping of all RTP packets to and from host.\n";

static char no_debug_usage[] =
  "Usage: rtp no debug\n"
  "       Disable all RTP debugging\n";

static struct ast_cli_entry  cli_debug_ip =
{{ "rtp", "debug", "ip", NULL } , rtp_do_debug, "Enable RTP debugging on IP", debug_usage };

static struct ast_cli_entry  cli_debug =
{{ "rtp", "debug", NULL } , rtp_do_debug, "Enable RTP debugging", debug_usage };

static struct ast_cli_entry  cli_no_debug =
{{ "rtp", "no", "debug", NULL } , rtp_no_debug, "Disable RTP debugging", no_debug_usage };

void ast_rtp_reload(void)
{
	struct ast_config *cfg;
	char *s;

	rtpstart = 5000;
	rtpend = 31000;
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
		ast_config_destroy(cfg);
	}
	if (rtpstart >= rtpend) {
		ast_log(LOG_WARNING, "Unreasonable values for RTP start/end port in rtp.conf\n");
		rtpstart = 5000;
		rtpend = 31000;
	}
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "RTP Allocating from port range %d -> %d\n", rtpstart, rtpend);
	
}

void ast_rtp_init(void)
{
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_debug_ip);
	ast_cli_register(&cli_no_debug);
	ast_rtp_reload();
}
