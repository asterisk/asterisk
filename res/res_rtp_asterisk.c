/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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

#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <math.h>

#include "asterisk/stun.h"
#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/acl.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/netsock.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/unaligned.h"
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"

#define MAX_TIMESTAMP_SKEW	640

#define RTP_SEQ_MOD     (1<<16)	/*!< A sequence number can't be more than 16 bits */
#define RTCP_DEFAULT_INTERVALMS   5000	/*!< Default milli-seconds between RTCP reports we send */
#define RTCP_MIN_INTERVALMS       500	/*!< Min milli-seconds between RTCP reports we send */
#define RTCP_MAX_INTERVALMS       60000	/*!< Max milli-seconds between RTCP reports we send */

#define DEFAULT_RTP_START 5000 /*!< Default port number to start allocating RTP ports from */
#define DEFAULT_RTP_END 31000  /*!< Default maximum port number to end allocating RTP ports at */

#define MINIMUM_RTP_PORT 1024 /*!< Minimum port number to accept */
#define MAXIMUM_RTP_PORT 65535 /*!< Maximum port number to accept */

#define RTCP_PT_FUR     192
#define RTCP_PT_SR      200
#define RTCP_PT_RR      201
#define RTCP_PT_SDES    202
#define RTCP_PT_BYE     203
#define RTCP_PT_APP     204

#define RTP_MTU		1200

#define DEFAULT_DTMF_TIMEOUT (150 * (8000 / 1000))	/*!< samples */

#define ZFONE_PROFILE_ID 0x505a

static int dtmftimeout = DEFAULT_DTMF_TIMEOUT;

static int rtpstart = DEFAULT_RTP_START;			/*!< First port for RTP sessions (set in rtp.conf) */
static int rtpend = DEFAULT_RTP_END;			/*!< Last port for RTP sessions (set in rtp.conf) */
static int rtpdebug;			/*!< Are we debugging? */
static int rtcpdebug;			/*!< Are we debugging RTCP? */
static int rtcpstats;			/*!< Are we debugging RTCP? */
static int rtcpinterval = RTCP_DEFAULT_INTERVALMS; /*!< Time between rtcp reports in millisecs */
static struct sockaddr_in rtpdebugaddr;	/*!< Debug packets to/from this host */
static struct sockaddr_in rtcpdebugaddr;	/*!< Debug RTCP packets to/from this host */
#ifdef SO_NO_CHECK
static int nochecksums;
#endif
static int strictrtp;

enum strict_rtp_state {
	STRICT_RTP_OPEN = 0, /*! No RTP packets should be dropped, all sources accepted */
	STRICT_RTP_LEARN,    /*! Accept next packet as source */
	STRICT_RTP_CLOSED,   /*! Drop all RTP packets not coming from source that was learned */
};

#define FLAG_3389_WARNING               (1 << 0)
#define FLAG_NAT_ACTIVE                 (3 << 1)
#define FLAG_NAT_INACTIVE               (0 << 1)
#define FLAG_NAT_INACTIVE_NOWARN        (1 << 1)
#define FLAG_NEED_MARKER_BIT            (1 << 3)
#define FLAG_DTMF_COMPENSATE            (1 << 4)

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
	unsigned int lastitexttimestamp;
	unsigned int lastotexttimestamp;
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
	unsigned int dtmf_duration;     /*!< Total duration in samples since the digit start event */
	unsigned int dtmf_timeout;      /*!< When this timestamp is reached we consider END frame lost and forcibly abort digit */
	unsigned int dtmfsamples;
	/* DTMF Transmission Variables */
	unsigned int lastdigitts;
	char sending_digit;	/*!< boolean - are we sending digits */
	char send_digit;	/*!< digit we are sending */
	int send_payload;
	int send_duration;
	unsigned int flags;
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
	struct ast_rtcp *rtcp;
	struct ast_rtp *bridged;        /*!< Who we are Packet bridged to */

	enum strict_rtp_state strict_rtp_state; /*!< Current state that strict RTP protection is in */
	struct sockaddr_in strict_rtp_address;  /*!< Remote address information for strict RTP purposes */

	struct rtp_red *red;
};

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
	int rtcp_info;
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

	double reported_maxjitter;
	double reported_minjitter;
	double reported_normdev_jitter;
	double reported_stdev_jitter;
	unsigned int reported_jitter_count;

	double reported_maxlost;
	double reported_minlost;
	double reported_normdev_lost;
	double reported_stdev_lost;

	double rxlost;
	double maxrxlost;
	double minrxlost;
	double normdev_rxlost;
	double stdev_rxlost;
	unsigned int rxlost_count;

	double maxrxjitter;
	double minrxjitter;
	double normdev_rxjitter;
	double stdev_rxjitter;
	unsigned int rxjitter_count;
	double maxrtt;
	double minrtt;
	double normdevrtt;
	double stdevrtt;
	unsigned int rtt_count;
};

struct rtp_red {
	struct ast_frame t140;  /*!< Primary data  */
	struct ast_frame t140red;   /*!< Redundant t140*/
	unsigned char pt[AST_RED_MAX_GENERATION];  /*!< Payload types for redundancy data */
	unsigned char ts[AST_RED_MAX_GENERATION]; /*!< Time stamps */
	unsigned char len[AST_RED_MAX_GENERATION]; /*!< length of each generation */
	int num_gen; /*!< Number of generations */
	int schedid; /*!< Timer id */
	int ti; /*!< How long to buffer data before send */
	unsigned char t140red_data[64000];
	unsigned char buf_data[64000]; /*!< buffered primary data */
	int hdrlen;
	long int prev_ts;
};

/* Forward Declarations */
static int ast_rtp_new(struct ast_rtp_instance *instance, struct sched_context *sched, struct sockaddr_in *sin, void *data);
static int ast_rtp_destroy(struct ast_rtp_instance *instance);
static int ast_rtp_dtmf_begin(struct ast_rtp_instance *instance, char digit);
static int ast_rtp_dtmf_end(struct ast_rtp_instance *instance, char digit);
static void ast_rtp_new_source(struct ast_rtp_instance *instance);
static int ast_rtp_write(struct ast_rtp_instance *instance, struct ast_frame *frame);
static struct ast_frame *ast_rtp_read(struct ast_rtp_instance *instance, int rtcp);
static void ast_rtp_prop_set(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value);
static int ast_rtp_fd(struct ast_rtp_instance *instance, int rtcp);
static void ast_rtp_remote_address_set(struct ast_rtp_instance *instance, struct sockaddr_in *sin);
static int rtp_red_init(struct ast_rtp_instance *instance, int buffer_time, int *payloads, int generations);
static int rtp_red_buffer(struct ast_rtp_instance *instance, struct ast_frame *frame);
static int ast_rtp_local_bridge(struct ast_rtp_instance *instance0, struct ast_rtp_instance *instance1);
static int ast_rtp_get_stat(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat);
static int ast_rtp_dtmf_compatible(struct ast_channel *chan0, struct ast_rtp_instance *instance0, struct ast_channel *chan1, struct ast_rtp_instance *instance1);
static void ast_rtp_stun_request(struct ast_rtp_instance *instance, struct sockaddr_in *suggestion, const char *username);
static void ast_rtp_stop(struct ast_rtp_instance *instance);

/* RTP Engine Declaration */
static struct ast_rtp_engine asterisk_rtp_engine = {
	.name = "asterisk",
	.new = ast_rtp_new,
	.destroy = ast_rtp_destroy,
	.dtmf_begin = ast_rtp_dtmf_begin,
	.dtmf_end = ast_rtp_dtmf_end,
	.new_source = ast_rtp_new_source,
	.write = ast_rtp_write,
	.read = ast_rtp_read,
	.prop_set = ast_rtp_prop_set,
	.fd = ast_rtp_fd,
	.remote_address_set = ast_rtp_remote_address_set,
	.red_init = rtp_red_init,
	.red_buffer = rtp_red_buffer,
	.local_bridge = ast_rtp_local_bridge,
	.get_stat = ast_rtp_get_stat,
	.dtmf_compatible = ast_rtp_dtmf_compatible,
	.stun_request = ast_rtp_stun_request,
	.stop = ast_rtp_stop,
};

static inline int rtp_debug_test_addr(struct sockaddr_in *addr)
{
	if (!rtpdebug) {
		return 0;
	}

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
	if (!rtcpdebug) {
		return 0;
	}

	if (rtcpdebugaddr.sin_addr.s_addr) {
		if (((ntohs(rtcpdebugaddr.sin_port) != 0)
		     && (rtcpdebugaddr.sin_port != addr->sin_port))
		    || (rtcpdebugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
			return 0;
	}

	return 1;
}

static unsigned int ast_rtcp_calc_interval(struct ast_rtp *rtp)
{
	unsigned int interval;
	/*! \todo XXX Do a more reasonable calculation on this one
	 * Look in RFC 3550 Section A.7 for an example*/
	interval = rtcpinterval;
	return interval;
}

/*! \brief Calculate normal deviation */
static double normdev_compute(double normdev, double sample, unsigned int sample_count)
{
	normdev = normdev * sample_count + sample;
	sample_count++;

	return normdev / sample_count;
}

static double stddev_compute(double stddev, double sample, double normdev, double normdev_curent, unsigned int sample_count)
{
/*
		for the formula check http://www.cs.umd.edu/~austinjp/constSD.pdf
		return sqrt( (sample_count*pow(stddev,2) + sample_count*pow((sample-normdev)/(sample_count+1),2) + pow(sample-normdev_curent,2)) / (sample_count+1));
		we can compute the sigma^2 and that way we would have to do the sqrt only 1 time at the end and would save another pow 2 compute
		optimized formula
*/
#define SQUARE(x) ((x) * (x))

	stddev = sample_count * stddev;
	sample_count++;

	return stddev +
		( sample_count * SQUARE( (sample - normdev) / sample_count ) ) +
		( SQUARE(sample - normdev_curent) / sample_count );

#undef SQUARE
}

static int create_new_socket(const char *type)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);

	if (sock < 0) {
		if (!type) {
			type = "RTP/RTCP";
		}
		ast_log(LOG_WARNING, "Unable to allocate %s socket: %s\n", type, strerror(errno));
	} else {
		long flags = fcntl(sock, F_GETFL);
		fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
		if (nochecksums) {
			setsockopt(sock, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
		}
#endif
	}

	return sock;
}

static int ast_rtp_new(struct ast_rtp_instance *instance, struct sched_context *sched, struct sockaddr_in *sin, void *data)
{
	struct ast_rtp *rtp = NULL;
	int x, startplace;

	/* Create a new RTP structure to hold all of our data */
	if (!(rtp = ast_calloc(1, sizeof(*rtp)))) {
		return -1;
	}

	/* Set default parameters on the newly created RTP structure */
	rtp->ssrc = ast_random();
	rtp->seqno = ast_random() & 0xffff;
	rtp->strict_rtp_state = (strictrtp ? STRICT_RTP_LEARN : STRICT_RTP_OPEN);

	/* Create a new socket for us to listen on and use */
	if ((rtp->s = create_new_socket("RTP")) < 0) {
		ast_debug(1, "Failed to create a new socket for RTP instance '%p'\n", instance);
		ast_free(rtp);
		return -1;
	}

	/* Now actually find a free RTP port to use */
	x = (rtpend == rtpstart) ? rtpstart : (ast_random() % (rtpend - rtpstart)) + rtpstart;
	x = x & ~1;
	startplace = x;

	for (;;) {
		sin->sin_port = htons(x);
		/* Try to bind, this will tell us whether the port is available or not */
		if (!bind(rtp->s, (struct sockaddr *)sin, sizeof(*sin))) {
			ast_debug(1, "Allocated port %d for RTP instance '%p'\n", x, instance);
			ast_rtp_instance_set_local_address(instance, sin);
			break;
		}

		x += 2;
		if (x > rtpend) {
			x = (rtpstart + 1) & ~1;
		}

		/* See if we ran out of ports or if the bind actually failed because of something other than the address being in use */
		if (x == startplace || errno != EADDRINUSE) {
			ast_log(LOG_ERROR, "Oh dear... we couldn't allocate a port for RTP instance '%p'\n", instance);
			return -1;
		}
	}

	/* Record any information we may need */
	rtp->sched = sched;

	/* Associate the RTP structure with the RTP instance and be done */
	ast_rtp_instance_set_data(instance, rtp);

	return 0;
}

static int ast_rtp_destroy(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* Destroy the smoother that was smoothing out audio if present */
	if (rtp->smoother) {
		ast_smoother_free(rtp->smoother);
	}

	/* Close our own socket so we no longer get packets */
	if (rtp->s > -1) {
		close(rtp->s);
	}

	/* Destroy RTCP if it was being used */
	if (rtp->rtcp) {
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
		close(rtp->rtcp->s);
		ast_free(rtp->rtcp);
	}

	/* Destroy RED if it was being used */
	if (rtp->red) {
		AST_SCHED_DEL(rtp->sched, rtp->red->schedid);
		ast_free(rtp->red);
	}

	/* Finally destroy ourselves */
	ast_free(rtp);

	return 0;
}

static int ast_rtp_dtmf_begin(struct ast_rtp_instance *instance, char digit)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in remote_address = { 0, };
	int hdrlen = 12, res = 0, i = 0, payload = 101;
	char data[256];
	unsigned int *rtpheader = (unsigned int*)data;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* If we have no remote address information bail out now */
	if (!remote_address.sin_addr.s_addr || !remote_address.sin_port) {
		return -1;
	}

	/* Convert given digit into what we want to transmit */
	if ((digit <= '9') && (digit >= '0')) {
		digit -= '0';
	} else if (digit == '*') {
		digit = 10;
	} else if (digit == '#') {
		digit = 11;
	} else if ((digit >= 'A') && (digit <= 'D')) {
		digit = digit - 'A' + 12;
	} else if ((digit >= 'a') && (digit <= 'd')) {
		digit = digit - 'a' + 12;
	} else {
		ast_log(LOG_WARNING, "Don't know how to represent '%c'\n", digit);
		return -1;
	}

	/* Grab the payload that they expect the RFC2833 packet to be received in */
	payload = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance), 0, AST_RTP_DTMF);

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));
	rtp->send_duration = 160;
	rtp->lastdigitts = rtp->lastts + rtp->send_duration;

	/* Create the actual packet that we will be sending */
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc);

	/* Actually send the packet */
	for (i = 0; i < 2; i++) {
		rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (rtp->send_duration));
		res = sendto(rtp->s, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &remote_address, sizeof(remote_address));
		if (res < 0) {
			ast_log(LOG_ERROR, "RTP Transmission error to %s:%u: %s\n",
				ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), strerror(errno));
		}
		if (rtp_debug_test_addr(&remote_address)) {
			ast_verbose("Sent RTP DTMF packet to %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
				    ast_inet_ntoa(remote_address.sin_addr),
				    ntohs(remote_address.sin_port), payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
		}
		rtp->seqno++;
		rtp->send_duration += 160;
		rtpheader[0] = htonl((2 << 30) | (payload << 16) | (rtp->seqno));
	}

	/* Record that we are in the process of sending a digit and information needed to continue doing so */
	rtp->sending_digit = 1;
	rtp->send_digit = digit;
	rtp->send_payload = payload;

	return 0;
}

static int ast_rtp_dtmf_continuation(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in remote_address = { 0, };
	int hdrlen = 12, res = 0;
	char data[256];
	unsigned int *rtpheader = (unsigned int*)data;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* Make sure we know where the other side is so we can send them the packet */
	if (!remote_address.sin_addr.s_addr || !remote_address.sin_port) {
		return -1;
	}

	/* Actually create the packet we will be sending */
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (rtp->send_payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc);
	rtpheader[3] = htonl((rtp->send_digit << 24) | (0xa << 16) | (rtp->send_duration));
	rtpheader[0] = htonl((2 << 30) | (rtp->send_payload << 16) | (rtp->seqno));

	/* Boom, send it on out */
	res = sendto(rtp->s, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &remote_address, sizeof(remote_address));
	if (res < 0) {
		ast_log(LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
			ast_inet_ntoa(remote_address.sin_addr),
			ntohs(remote_address.sin_port), strerror(errno));
	}

	if (rtp_debug_test_addr(&remote_address)) {
		ast_verbose("Sent RTP DTMF packet to %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
			    ast_inet_ntoa(remote_address.sin_addr),
			    ntohs(remote_address.sin_port), rtp->send_payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
	}

	/* And now we increment some values for the next time we swing by */
	rtp->seqno++;
	rtp->send_duration += 160;

	return 0;
}

static int ast_rtp_dtmf_end(struct ast_rtp_instance *instance, char digit)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in remote_address = { 0, };
	int hdrlen = 12, res = 0, i = 0;
	char data[256];
	unsigned int *rtpheader = (unsigned int*)data;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* Make sure we know where the remote side is so we can send them the packet we construct */
	if (!remote_address.sin_addr.s_addr || !remote_address.sin_port) {
		return -1;
	}

	/* Convert the given digit to the one we are going to send */
	if ((digit <= '9') && (digit >= '0')) {
		digit -= '0';
	} else if (digit == '*') {
		digit = 10;
	} else if (digit == '#') {
		digit = 11;
	} else if ((digit >= 'A') && (digit <= 'D')) {
		digit = digit - 'A' + 12;
	} else if ((digit >= 'a') && (digit <= 'd')) {
		digit = digit - 'a' + 12;
	} else {
		ast_log(LOG_WARNING, "Don't know how to represent '%c'\n", digit);
		return -1;
	}

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));

	/* Construct the packet we are going to send */
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (rtp->send_payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc);
	rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (rtp->send_duration));
	rtpheader[3] |= htonl((1 << 23));
	rtpheader[0] = htonl((2 << 30) | (rtp->send_payload << 16) | (rtp->seqno));

	/* Send it 3 times, that's the magical number */
	for (i = 0; i < 3; i++) {
		res = sendto(rtp->s, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &remote_address, sizeof(remote_address));
		if (res < 0) {
			ast_log(LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
				ast_inet_ntoa(remote_address.sin_addr),
				ntohs(remote_address.sin_port), strerror(errno));
		}
		if (rtp_debug_test_addr(&remote_address)) {
			ast_verbose("Sent RTP DTMF packet to %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
				    ast_inet_ntoa(remote_address.sin_addr),
				    ntohs(remote_address.sin_port), rtp->send_payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
		}
	}

	/* Oh and we can't forget to turn off the stuff that says we are sending DTMF */
	rtp->lastts += rtp->send_duration;
	rtp->sending_digit = 0;
	rtp->send_digit = 0;

	return 0;
}

static void ast_rtp_new_source(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* We simply set this bit so that the next packet sent will have the marker bit turned on */
	ast_set_flag(rtp, FLAG_NEED_MARKER_BIT);

	return;
}

static unsigned int calc_txstamp(struct ast_rtp *rtp, struct timeval *delivery)
{
	struct timeval t;
	long ms;

	if (ast_tvzero(rtp->txcore)) {
		rtp->txcore = ast_tvnow();
		rtp->txcore.tv_usec -= rtp->txcore.tv_usec % 20000;
	}

	t = (delivery && !ast_tvzero(*delivery)) ? *delivery : ast_tvnow();
	if ((ms = ast_tvdiff_ms(t, rtp->txcore)) < 0) {
		ms = 0;
	}
	rtp->txcore = t;

	return (unsigned int) ms;
}

static void timeval2ntp(struct timeval tv, unsigned int *msw, unsigned int *lsw)
{
	unsigned int sec, usec, frac;
	sec = tv.tv_sec + 2208988800u; /* Sec between 1900 and 1970 */
	usec = tv.tv_usec;
	frac = (usec << 12) + (usec << 8) - ((usec * 3650) >> 6);
	*msw = sec;
	*lsw = frac;
}

/*! \brief Send RTCP recipient's report */
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

	double rxlost_current;

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

	if (lost_interval <= 0)
		rtp->rtcp->rxlost = 0;
	else rtp->rtcp->rxlost = rtp->rtcp->rxlost;
	if (rtp->rtcp->rxlost_count == 0)
		rtp->rtcp->minrxlost = rtp->rtcp->rxlost;
	if (lost_interval < rtp->rtcp->minrxlost)
		rtp->rtcp->minrxlost = rtp->rtcp->rxlost;
	if (lost_interval > rtp->rtcp->maxrxlost)
		rtp->rtcp->maxrxlost = rtp->rtcp->rxlost;

	rxlost_current = normdev_compute(rtp->rtcp->normdev_rxlost, rtp->rtcp->rxlost, rtp->rtcp->rxlost_count);
	rtp->rtcp->stdev_rxlost = stddev_compute(rtp->rtcp->stdev_rxlost, rtp->rtcp->rxlost, rtp->rtcp->normdev_rxlost, rxlost_current, rtp->rtcp->rxlost_count);
	rtp->rtcp->normdev_rxlost = rxlost_current;
	rtp->rtcp->rxlost_count++;

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
	manager_event(EVENT_FLAG_REPORTING, "RTCPSent", "To %s:%d\r\n"
					    "OurSSRC: %u\r\n"
					    "SentNTP: %u.%010u\r\n"
					    "SentRTP: %u\r\n"
					    "SentPackets: %u\r\n"
					    "SentOctets: %u\r\n"
					    "ReportBlock:\r\n"
					    "FractionLost: %u\r\n"
					    "CumulativeLoss: %u\r\n"
					    "IAJitter: %.4f\r\n"
					    "TheirLastSR: %u\r\n"
		      "DLSR: %4.4f (sec)\r\n",
		      ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port),
		      rtp->ssrc,
		      (unsigned int)now.tv_sec, (unsigned int)now.tv_usec*4096,
		      rtp->lastts,
		      rtp->txcount,
		      rtp->txoctetcount,
		      fraction,
		      lost,
		      rtp->rxjitter,
		      rtp->rtcp->themrxlsr,
		      (double)(ntohl(rtcpheader[12])/65536.0));
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

static int ast_rtp_raw_write(struct ast_rtp_instance *instance, struct ast_frame *frame, int codec)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	int pred, mark = 0;
	unsigned int ms = calc_txstamp(rtp, &frame->delivery);
	struct sockaddr_in remote_address = { 0, };

	if (rtp->sending_digit) {
		return 0;
	}

	if (frame->frametype == AST_FRAME_VOICE) {
		pred = rtp->lastts + frame->samples;

		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * 8;
		if (ast_tvzero(frame->delivery)) {
			/* If this isn't an absolute delivery time, Check if it is close to our prediction,
			   and if so, go with our prediction */
			if (abs(rtp->lastts - pred) < MAX_TIMESTAMP_SKEW) {
				rtp->lastts = pred;
			} else {
				ast_debug(3, "Difference is %d, ms is %d\n", abs(rtp->lastts - pred), ms);
				mark = 1;
			}
		}
	} else if (frame->frametype == AST_FRAME_VIDEO) {
		mark = frame->subclass & 0x1;
		pred = rtp->lastovidtimestamp + frame->samples;
		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * 90;
		/* If it's close to our prediction, go for it */
		if (ast_tvzero(frame->delivery)) {
			if (abs(rtp->lastts - pred) < 7200) {
				rtp->lastts = pred;
				rtp->lastovidtimestamp += frame->samples;
			} else {
				ast_debug(3, "Difference is %d, ms is %d (%d), pred/ts/samples %d/%d/%d\n", abs(rtp->lastts - pred), ms, ms * 90, rtp->lastts, pred, frame->samples);
				rtp->lastovidtimestamp = rtp->lastts;
			}
		}
	} else {
		pred = rtp->lastotexttimestamp + frame->samples;
		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms;
		/* If it's close to our prediction, go for it */
		if (ast_tvzero(frame->delivery)) {
			if (abs(rtp->lastts - pred) < 7200) {
				rtp->lastts = pred;
				rtp->lastotexttimestamp += frame->samples;
			} else {
				ast_debug(3, "Difference is %d, ms is %d, pred/ts/samples %d/%d/%d\n", abs(rtp->lastts - pred), ms, rtp->lastts, pred, frame->samples);
				rtp->lastotexttimestamp = rtp->lastts;
			}
		}
	}

	/* If we have been explicitly told to set the marker bit then do so */
	if (ast_test_flag(rtp, FLAG_NEED_MARKER_BIT)) {
		mark = 1;
		ast_clear_flag(rtp, FLAG_NEED_MARKER_BIT);
	}

	/* If the timestamp for non-digt packets has moved beyond the timestamp for digits, update the digit timestamp */
	if (rtp->lastts > rtp->lastdigitts) {
		rtp->lastdigitts = rtp->lastts;
	}

	if (ast_test_flag(frame, AST_FRFLAG_HAS_TIMING_INFO)) {
		rtp->lastts = frame->ts * 8;
	}

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* If we know the remote address construct a packet and send it out */
	if (remote_address.sin_port && remote_address.sin_addr.s_addr) {
		int hdrlen = 12, res;
		unsigned char *rtpheader = (unsigned char *)(frame->data.ptr - hdrlen);

		put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (rtp->seqno) | (mark << 23)));
		put_unaligned_uint32(rtpheader + 4, htonl(rtp->lastts));
		put_unaligned_uint32(rtpheader + 8, htonl(rtp->ssrc));

		if ((res = sendto(rtp->s, (void *)rtpheader, frame->datalen + hdrlen, 0, (struct sockaddr *)&remote_address, sizeof(remote_address))) < 0) {
			if (!ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT) || (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT) && (ast_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE))) {
				ast_debug(1, "RTP Transmission error of packet %d to %s:%d: %s\n", rtp->seqno, ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), strerror(errno));
			} else if (((ast_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug) && !ast_test_flag(rtp, FLAG_NAT_INACTIVE_NOWARN)) {
				/* Only give this error message once if we are not RTP debugging */
				if (option_debug || rtpdebug)
					ast_debug(0, "RTP NAT: Can't write RTP to private address %s:%d, waiting for other end to send audio...\n", ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port));
				ast_set_flag(rtp, FLAG_NAT_INACTIVE_NOWARN);
			}
		} else {
			rtp->txcount++;
			rtp->txoctetcount += (res - hdrlen);

			if (rtp->rtcp && rtp->rtcp->schedid < 1) {
				ast_debug(1, "Starting RTCP transmission on RTP instance '%p'\n", instance);
				rtp->rtcp->schedid = ast_sched_add(rtp->sched, ast_rtcp_calc_interval(rtp), ast_rtcp_write, rtp);
			}
		}

		if (rtp_debug_test_addr(&remote_address)) {
			ast_verbose("Sent RTP packet to      %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
				    ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), codec, rtp->seqno, rtp->lastts, res - hdrlen);
		}
	}

	rtp->seqno++;

	return 0;
}

static struct ast_frame *red_t140_to_red(struct rtp_red *red) {
	unsigned char *data = red->t140red.data.ptr;
	int len = 0;
	int i;

	/* replace most aged generation */
	if (red->len[0]) {
		for (i = 1; i < red->num_gen+1; i++)
			len += red->len[i];

		memmove(&data[red->hdrlen], &data[red->hdrlen+red->len[0]], len);
	}

	/* Store length of each generation and primary data length*/
	for (i = 0; i < red->num_gen; i++)
		red->len[i] = red->len[i+1];
	red->len[i] = red->t140.datalen;

	/* write each generation length in red header */
	len = red->hdrlen;
	for (i = 0; i < red->num_gen; i++)
		len += data[i*4+3] = red->len[i];

	/* add primary data to buffer */
	memcpy(&data[len], red->t140.data.ptr, red->t140.datalen);
	red->t140red.datalen = len + red->t140.datalen;

	/* no primary data and no generations to send */
	if (len == red->hdrlen && !red->t140.datalen)
		return NULL;

	/* reset t.140 buffer */
	red->t140.datalen = 0;

	return &red->t140red;
}

static int ast_rtp_write(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in remote_address = { 0, };
	int codec, subclass;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* If we don't actually know the remote address don't even bother doing anything */
	if (!remote_address.sin_addr.s_addr) {
		ast_debug(1, "No remote address on RTP instance '%p' so dropping frame\n", instance);
		return 0;
	}

	/* If there is no data length we can't very well send the packet */
	if (!frame->datalen) {
		ast_debug(1, "Received frame with no data for RTP instance '%p' so dropping frame\n", instance);
		return 0;
	}

	/* If the packet is not one our RTP stack supports bail out */
	if (frame->frametype != AST_FRAME_VOICE && frame->frametype != AST_FRAME_VIDEO && frame->frametype != AST_FRAME_TEXT) {
		ast_log(LOG_WARNING, "RTP can only send voice, video, and text\n");
		return -1;
	}

	if (rtp->red) {
		/* return 0; */
		/* no primary data or generations to send */
		if ((frame = red_t140_to_red(rtp->red)) == NULL)
			return 0;
	}

	/* Grab the subclass and look up the payload we are going to use */
	subclass = frame->subclass;
	if (frame->frametype == AST_FRAME_VIDEO) {
		subclass &= ~0x1;
	}
	if ((codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance), 1, subclass)) < 0) {
		ast_log(LOG_WARNING, "Don't know how to send format %s packets with RTP\n", ast_getformatname(frame->subclass));
		return -1;
	}

	/* Oh dear, if the format changed we will have to set up a new smoother */
	if (rtp->lasttxformat != subclass) {
		ast_debug(1, "Ooh, format changed from %s to %s\n", ast_getformatname(rtp->lasttxformat), ast_getformatname(subclass));
		rtp->lasttxformat = subclass;
		if (rtp->smoother) {
			ast_smoother_free(rtp->smoother);
			rtp->smoother = NULL;
		}
	}

	/* If no smoother is present see if we have to set one up */
	if (!rtp->smoother) {
		struct ast_format_list fmt = ast_codec_pref_getsize(&ast_rtp_instance_get_codecs(instance)->pref, subclass);

		switch (subclass) {
		case AST_FORMAT_SPEEX:
		case AST_FORMAT_G723_1:
		case AST_FORMAT_SIREN7:
		case AST_FORMAT_SIREN14:
			/* these are all frame-based codecs and cannot be safely run through
			   a smoother */
			break;
		default:
			if (fmt.inc_ms) {
				if (!(rtp->smoother = ast_smoother_new((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms))) {
					ast_log(LOG_WARNING, "Unable to create smoother: format %d ms: %d len: %d\n", subclass, fmt.cur_ms, ((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms));
					return -1;
				}
				if (fmt.flags) {
					ast_smoother_set_flags(rtp->smoother, fmt.flags);
				}
				ast_debug(1, "Created smoother: format: %d ms: %d len: %d\n", subclass, fmt.cur_ms, ((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms));
			}
		}
	}

	/* Feed audio frames into the actual function that will create a frame and send it */
	if (rtp->smoother) {
		struct ast_frame *f;

		if (ast_smoother_test_flag(rtp->smoother, AST_SMOOTHER_FLAG_BE)) {
			ast_smoother_feed_be(rtp->smoother, frame);
		} else {
			ast_smoother_feed(rtp->smoother, frame);
		}

		while ((f = ast_smoother_read(rtp->smoother)) && (f->data.ptr)) {
			if (f->subclass == AST_FORMAT_G722) {
				f->samples /= 2;
			}

			ast_rtp_raw_write(instance, f, codec);
		}
	} else {
		int hdrlen = 12;
		struct ast_frame *f = NULL;

		if (frame->offset < hdrlen) {
			f = ast_frdup(frame);
		} else {
			f = frame;
		}
		if (f->data.ptr) {
			ast_rtp_raw_write(instance, f, codec);
		}
		if (f != frame) {
			ast_frfree(f);
		}

	}

	return 0;
}

static void calc_rxstamp(struct timeval *tv, struct ast_rtp *rtp, unsigned int timestamp, int mark)
{
	struct timeval now;
	double transit;
	double current_time;
	double d;
	double dtv;
	double prog;

	double normdev_rxjitter_current;
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

	if (rtp->rtcp) {
		if (rtp->rxjitter > rtp->rtcp->maxrxjitter)
			rtp->rtcp->maxrxjitter = rtp->rxjitter;
		if (rtp->rtcp->rxjitter_count == 1)
			rtp->rtcp->minrxjitter = rtp->rxjitter;
		if (rtp->rtcp && rtp->rxjitter < rtp->rtcp->minrxjitter)
			rtp->rtcp->minrxjitter = rtp->rxjitter;

		normdev_rxjitter_current = normdev_compute(rtp->rtcp->normdev_rxjitter,rtp->rxjitter,rtp->rtcp->rxjitter_count);
		rtp->rtcp->stdev_rxjitter = stddev_compute(rtp->rtcp->stdev_rxjitter,rtp->rxjitter,rtp->rtcp->normdev_rxjitter,normdev_rxjitter_current,rtp->rtcp->rxjitter_count);

		rtp->rtcp->normdev_rxjitter = normdev_rxjitter_current;
		rtp->rtcp->rxjitter_count++;
	}
}

static struct ast_frame *send_dtmf(struct ast_rtp_instance *instance, enum ast_frame_type type, int compensate)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in remote_address = { 0, };

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	if (((compensate && type == AST_FRAME_DTMF_END) || (type == AST_FRAME_DTMF_BEGIN)) && ast_tvcmp(ast_tvnow(), rtp->dtmfmute) < 0) {
		ast_debug(1, "Ignore potential DTMF echo from '%s'\n", ast_inet_ntoa(remote_address.sin_addr));
		rtp->resp = 0;
		rtp->dtmfsamples = 0;
		return &ast_null_frame;
	}
	ast_debug(1, "Sending dtmf: %d (%c), at %s\n", rtp->resp, rtp->resp, ast_inet_ntoa(remote_address.sin_addr));
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

static struct ast_frame *process_dtmf_rfc2833(struct ast_rtp_instance *instance, unsigned char *data, int len, unsigned int seqno, unsigned int timestamp, struct sockaddr_in *sin, int payloadtype, int mark)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in remote_address = { 0, };
	unsigned int event, event_end, samples;
	char resp = 0;
	struct ast_frame *f = NULL;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* Figure out event, event end, and samples */
	event = ntohl(*((unsigned int *)(data)));
	event >>= 24;
	event_end = ntohl(*((unsigned int *)(data)));
	event_end <<= 8;
	event_end >>= 24;
	samples = ntohl(*((unsigned int *)(data)));
	samples &= 0xFFFF;

	if (rtp_debug_test_addr(&remote_address)) {
		ast_verbose("Got  RTP RFC2833 from   %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u, mark %d, event %08x, end %d, duration %-5.5d) \n", ast_inet_ntoa(remote_address.sin_addr),
			    ntohs(remote_address.sin_port), payloadtype, seqno, timestamp, len, (mark?1:0), event, ((event_end & 0x80)?1:0), samples);
	}

	/* Print out debug if turned on */
	if (rtpdebug || option_debug > 2)
		ast_debug(0, "- RTP 2833 Event: %08x (len = %d)\n", event, len);

	/* Figure out what digit was pressed */
	if (event < 10) {
		resp = '0' + event;
	} else if (event < 11) {
		resp = '*';
	} else if (event < 12) {
		resp = '#';
	} else if (event < 16) {
		resp = 'A' + (event - 12);
	} else if (event < 17) {        /* Event 16: Hook flash */
		resp = 'X';
	} else {
		/* Not a supported event */
		ast_log(LOG_DEBUG, "Ignoring RTP 2833 Event: %08x. Not a DTMF Digit.\n", event);
		return &ast_null_frame;
	}

	if (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_DTMF_COMPENSATE)) {
		if ((rtp->lastevent != timestamp) || (rtp->resp && rtp->resp != resp)) {
			rtp->resp = resp;
			rtp->dtmf_timeout = 0;
			f = send_dtmf(instance, AST_FRAME_DTMF_END, ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_DTMF_COMPENSATE));
			f->len = 0;
			rtp->lastevent = timestamp;
		}
	} else {
		/*  The duration parameter measures the complete
		    duration of the event (from the beginning) - RFC2833.
		    Account for the fact that duration is only 16 bits long
		    (about 8 seconds at 8000 Hz) and can wrap is digit
		    is hold for too long. */
		unsigned int new_duration = rtp->dtmf_duration;
		unsigned int last_duration = new_duration & 0xFFFF;

		if (last_duration > 64000 && samples < last_duration) {
			new_duration += 0xFFFF + 1;
		}
		new_duration = (new_duration & ~0xFFFF) | samples;

		if (event_end & 0x80) {
			/* End event */
			if ((rtp->lastevent != seqno) && rtp->resp) {
				rtp->dtmf_duration = new_duration;
				f = send_dtmf(instance, AST_FRAME_DTMF_END, 0);
				f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, 8000), ast_tv(0, 0));
				rtp->resp = 0;
				rtp->dtmf_duration = rtp->dtmf_timeout = 0;
			}
		} else {
			/* Begin/continuation */

			if (rtp->resp && rtp->resp != resp) {
				/* Another digit already began. End it */
				f = send_dtmf(instance, AST_FRAME_DTMF_END, 0);
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
				f = send_dtmf(instance, AST_FRAME_DTMF_BEGIN, 0);
				rtp->dtmf_duration = samples;
			}

			rtp->dtmf_timeout = timestamp + rtp->dtmf_duration + dtmftimeout;
		}

		rtp->lastevent = seqno;
	}

	rtp->dtmfsamples = samples;

	return f;
}

static struct ast_frame *process_dtmf_cisco(struct ast_rtp_instance *instance, unsigned char *data, int len, unsigned int seqno, unsigned int timestamp, struct sockaddr_in *sin, int payloadtype, int mark)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	unsigned int event, flags, power;
	char resp = 0;
	unsigned char seq;
	struct ast_frame *f = NULL;

	if (len < 4) {
		return NULL;
	}

	/*      The format of Cisco RTP DTMF packet looks like next:
		+0                              - sequence number of DTMF RTP packet (begins from 1,
						  wrapped to 0)
		+1                              - set of flags
		+1 (bit 0)              - flaps by different DTMF digits delimited by audio
						  or repeated digit without audio???
		+2 (+4,+6,...)  - power level? (rises from 0 to 32 at begin of tone
						  then falls to 0 at its end)
		+3 (+5,+7,...)  - detected DTMF digit (0..9,*,#,A-D,...)
		Repeated DTMF information (bytes 4/5, 6/7) is history shifted right
		by each new packet and thus provides some redudancy.

		Sample of Cisco RTP DTMF packet is (all data in hex):
			19 07 00 02 12 02 20 02
		showing end of DTMF digit '2'.

		The packets
			27 07 00 02 0A 02 20 02
			28 06 20 02 00 02 0A 02
		shows begin of new digit '2' with very short pause (20 ms) after
		previous digit '2'. Bit +1.0 flips at begin of new digit.

		Cisco RTP DTMF packets comes as replacement of audio RTP packets
		so its uses the same sequencing and timestamping rules as replaced
		audio packets. Repeat interval of DTMF packets is 20 ms and not rely
		on audio framing parameters. Marker bit isn't used within stream of
		DTMFs nor audio stream coming immediately after DTMF stream. Timestamps
		are not sequential at borders between DTMF and audio streams,
	*/

	seq = data[0];
	flags = data[1];
	power = data[2];
	event = data[3] & 0x1f;

	if (option_debug > 2 || rtpdebug)
		ast_debug(0, "Cisco DTMF Digit: %02x (len=%d, seq=%d, flags=%02x, power=%d, history count=%d)\n", event, len, seq, flags, power, (len - 4) / 2);
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
	if ((!rtp->resp && power) || (rtp->resp && (rtp->resp != resp))) {
		rtp->resp = resp;
		/* Why we should care on DTMF compensation at reception? */
		if (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_DTMF_COMPENSATE)) {
			f = send_dtmf(instance, AST_FRAME_DTMF_BEGIN, 0);
			rtp->dtmfsamples = 0;
		}
	} else if ((rtp->resp == resp) && !power) {
		f = send_dtmf(instance, AST_FRAME_DTMF_END, ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_DTMF_COMPENSATE));
		f->samples = rtp->dtmfsamples * 8;
		rtp->resp = 0;
	} else if (rtp->resp == resp)
		rtp->dtmfsamples += 20 * 8;
	rtp->dtmf_timeout = 0;

	return f;
}

static struct ast_frame *process_cn_rfc3389(struct ast_rtp_instance *instance, unsigned char *data, int len, unsigned int seqno, unsigned int timestamp, struct sockaddr_in *sin, int payloadtype, int mark)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* Convert comfort noise into audio with various codecs.  Unfortunately this doesn't
	   totally help us out becuase we don't have an engine to keep it going and we are not
	   guaranteed to have it every 20ms or anything */
	if (rtpdebug)
		ast_debug(0, "- RTP 3389 Comfort noise event: Level %d (len = %d)\n", rtp->lastrxformat, len);

	if (ast_test_flag(rtp, FLAG_3389_WARNING)) {
		struct sockaddr_in remote_address = { 0, };

		ast_rtp_instance_get_remote_address(instance, &remote_address);

		ast_log(LOG_NOTICE, "Comfort noise support incomplete in Asterisk (RFC 3389). Please turn off on client if possible. Client IP: %s\n",
			ast_inet_ntoa(remote_address.sin_addr));
		ast_set_flag(rtp, FLAG_3389_WARNING);
	}

	/* Must have at least one byte */
	if (!len)
		return NULL;
	if (len < 24) {
		rtp->f.data.ptr = rtp->rawdata + AST_FRIENDLY_OFFSET;
		rtp->f.datalen = len - 1;
		rtp->f.offset = AST_FRIENDLY_OFFSET;
		memcpy(rtp->f.data.ptr, data + 1, len - 1);
	} else {
		rtp->f.data.ptr = NULL;
		rtp->f.offset = 0;
		rtp->f.datalen = 0;
	}
	rtp->f.frametype = AST_FRAME_CNG;
	rtp->f.subclass = data[0] & 0x7f;
	rtp->f.datalen = len - 1;
	rtp->f.samples = 0;
	rtp->f.delivery.tv_usec = rtp->f.delivery.tv_sec = 0;

	return &rtp->f;
}

static struct ast_frame *ast_rtcp_read(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);
	unsigned int rtcpdata[8192 + AST_FRIENDLY_OFFSET];
	unsigned int *rtcpheader = (unsigned int *)(rtcpdata + AST_FRIENDLY_OFFSET);
	int res, packetwords, position = 0;
	struct ast_frame *f = &ast_null_frame;

	/* Read in RTCP data from the socket */
	if ((res = recvfrom(rtp->rtcp->s, rtcpdata + AST_FRIENDLY_OFFSET, sizeof(rtcpdata) - sizeof(unsigned int) * AST_FRIENDLY_OFFSET, 0, (struct sockaddr *)&sin, &len)) < 0) {
		ast_assert(errno != EBADF);
		if (errno != EAGAIN) {
			ast_log(LOG_WARNING, "RTCP Read error: %s.  Hanging up.\n", strerror(errno));
			return NULL;
		}
		return &ast_null_frame;
	}

	packetwords = res / 4;

	if (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT)) {
		/* Send to whoever sent to us */
		if ((rtp->rtcp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (rtp->rtcp->them.sin_port != sin.sin_port)) {
			memcpy(&rtp->rtcp->them, &sin, sizeof(rtp->rtcp->them));
			if (option_debug || rtpdebug)
				ast_debug(0, "RTCP NAT: Got RTCP from other end. Now sending to address %s:%d\n", ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
		}
	}

	ast_debug(1, "Got RTCP report of %d bytes\n", res);

	while (position < packetwords) {
		int i, pt, rc;
		unsigned int length, dlsr, lsr, msw, lsw, comp;
		struct timeval now;
		double rttsec, reported_jitter, reported_normdev_jitter_current, normdevrtt_current, reported_lost, reported_normdev_lost_current;
		uint64_t rtt = 0;

		i = position;
		length = ntohl(rtcpheader[i]);
		pt = (length & 0xff0000) >> 16;
		rc = (length & 0x1f000000) >> 24;
		length &= 0xffff;

		if ((i + length) > packetwords) {
			if (option_debug || rtpdebug)
				ast_log(LOG_DEBUG, "RTCP Read too short\n");
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
				rtp->rtcp->rtt = rttsec;

				if (comp - dlsr >= lsr) {
					rtp->rtcp->accumulated_transit += rttsec;

					if (rtp->rtcp->rtt_count == 0)
						rtp->rtcp->minrtt = rttsec;

					if (rtp->rtcp->maxrtt<rttsec)
						rtp->rtcp->maxrtt = rttsec;
					if (rtp->rtcp->minrtt>rttsec)
						rtp->rtcp->minrtt = rttsec;

					normdevrtt_current = normdev_compute(rtp->rtcp->normdevrtt, rttsec, rtp->rtcp->rtt_count);

					rtp->rtcp->stdevrtt = stddev_compute(rtp->rtcp->stdevrtt, rttsec, rtp->rtcp->normdevrtt, normdevrtt_current, rtp->rtcp->rtt_count);

					rtp->rtcp->normdevrtt = normdevrtt_current;

					rtp->rtcp->rtt_count++;
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
			reported_jitter = (double) rtp->rtcp->reported_jitter;

			if (rtp->rtcp->reported_jitter_count == 0)
				rtp->rtcp->reported_minjitter = reported_jitter;

			if (reported_jitter < rtp->rtcp->reported_minjitter)
				rtp->rtcp->reported_minjitter = reported_jitter;

			if (reported_jitter > rtp->rtcp->reported_maxjitter)
				rtp->rtcp->reported_maxjitter = reported_jitter;

			reported_normdev_jitter_current = normdev_compute(rtp->rtcp->reported_normdev_jitter, reported_jitter, rtp->rtcp->reported_jitter_count);

			rtp->rtcp->reported_stdev_jitter = stddev_compute(rtp->rtcp->reported_stdev_jitter, reported_jitter, rtp->rtcp->reported_normdev_jitter, reported_normdev_jitter_current, rtp->rtcp->reported_jitter_count);

			rtp->rtcp->reported_normdev_jitter = reported_normdev_jitter_current;

			rtp->rtcp->reported_lost = ntohl(rtcpheader[i + 1]) & 0xffffff;

			reported_lost = (double) rtp->rtcp->reported_lost;

			/* using same counter as for jitter */
			if (rtp->rtcp->reported_jitter_count == 0)
				rtp->rtcp->reported_minlost = reported_lost;

			if (reported_lost < rtp->rtcp->reported_minlost)
				rtp->rtcp->reported_minlost = reported_lost;

			if (reported_lost > rtp->rtcp->reported_maxlost)
				rtp->rtcp->reported_maxlost = reported_lost;
			reported_normdev_lost_current = normdev_compute(rtp->rtcp->reported_normdev_lost, reported_lost, rtp->rtcp->reported_jitter_count);

			rtp->rtcp->reported_stdev_lost = stddev_compute(rtp->rtcp->reported_stdev_lost, reported_lost, rtp->rtcp->reported_normdev_lost, reported_normdev_lost_current, rtp->rtcp->reported_jitter_count);

			rtp->rtcp->reported_normdev_lost = reported_normdev_lost_current;

			rtp->rtcp->reported_jitter_count++;

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
			if (rtt) {
				manager_event(EVENT_FLAG_REPORTING, "RTCPReceived", "From %s:%d\r\n"
								    "PT: %d(%s)\r\n"
								    "ReceptionReports: %d\r\n"
								    "SenderSSRC: %u\r\n"
								    "FractionLost: %ld\r\n"
								    "PacketsLost: %d\r\n"
								    "HighestSequence: %ld\r\n"
								    "SequenceNumberCycles: %ld\r\n"
								    "IAJitter: %u\r\n"
								    "LastSR: %lu.%010lu\r\n"
								    "DLSR: %4.4f(sec)\r\n"
					      "RTT: %llu(sec)\r\n",
					      ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port),
					      pt, (pt == 200) ? "Sender Report" : (pt == 201) ? "Receiver Report" : (pt == 192) ? "H.261 FUR" : "Unknown",
					      rc,
					      rtcpheader[i + 1],
					      (((long) ntohl(rtcpheader[i + 1]) & 0xff000000) >> 24),
					      rtp->rtcp->reported_lost,
					      (long) (ntohl(rtcpheader[i + 2]) & 0xffff),
					      (long) (ntohl(rtcpheader[i + 2]) & 0xffff) >> 16,
					      rtp->rtcp->reported_jitter,
					      (unsigned long) ntohl(rtcpheader[i + 4]) >> 16, ((unsigned long) ntohl(rtcpheader[i + 4]) << 16) * 4096,
					      ntohl(rtcpheader[i + 5])/65536.0,
					      (unsigned long long)rtt);
			} else {
				manager_event(EVENT_FLAG_REPORTING, "RTCPReceived", "From %s:%d\r\n"
								    "PT: %d(%s)\r\n"
								    "ReceptionReports: %d\r\n"
								    "SenderSSRC: %u\r\n"
								    "FractionLost: %ld\r\n"
								    "PacketsLost: %d\r\n"
								    "HighestSequence: %ld\r\n"
								    "SequenceNumberCycles: %ld\r\n"
								    "IAJitter: %u\r\n"
								    "LastSR: %lu.%010lu\r\n"
					      "DLSR: %4.4f(sec)\r\n",
					      ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port),
					      pt, (pt == 200) ? "Sender Report" : (pt == 201) ? "Receiver Report" : (pt == 192) ? "H.261 FUR" : "Unknown",
					      rc,
					      rtcpheader[i + 1],
					      (((long) ntohl(rtcpheader[i + 1]) & 0xff000000) >> 24),
					      rtp->rtcp->reported_lost,
					      (long) (ntohl(rtcpheader[i + 2]) & 0xffff),
					      (long) (ntohl(rtcpheader[i + 2]) & 0xffff) >> 16,
					      rtp->rtcp->reported_jitter,
					      (unsigned long) ntohl(rtcpheader[i + 4]) >> 16,
					      ((unsigned long) ntohl(rtcpheader[i + 4]) << 16) * 4096,
					      ntohl(rtcpheader[i + 5])/65536.0);
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
			ast_debug(1, "Unknown RTCP packet (pt=%d) received from %s:%d\n", pt, ast_inet_ntoa(rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
			break;
		}
		position += (length + 1);
	}

	rtp->rtcp->rtcp_info = 1;

	return f;
}

static int bridge_p2p_rtp_write(struct ast_rtp_instance *instance, unsigned int *rtpheader, int len, int hdrlen)
{
	struct ast_rtp_instance *instance1 = ast_rtp_instance_get_bridged(instance);
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance), *bridged = ast_rtp_instance_get_data(instance1);
	int res = 0, payload = 0, bridged_payload = 0, mark;
	struct ast_rtp_payload_type payload_type;
	int reconstruct = ntohl(rtpheader[0]);
	struct sockaddr_in remote_address = { 0, };

	/* Get fields from packet */
	payload = (reconstruct & 0x7f0000) >> 16;
	mark = (((reconstruct & 0x800000) >> 23) != 0);

	/* Check what the payload value should be */
	payload_type = ast_rtp_codecs_payload_lookup(ast_rtp_instance_get_codecs(instance), payload);

	/* Otherwise adjust bridged payload to match */
	bridged_payload = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance1), payload_type.asterisk_format, payload_type.code);

	/* If the payload coming in is not one of the negotiated ones then send it to the core, this will cause formats to change and the bridge to break */
	if (!(ast_rtp_instance_get_codecs(instance1)->payloads[bridged_payload].code)) {
		return -1;
	}

	/* If the marker bit has been explicitly set turn it on */
	if (ast_test_flag(rtp, FLAG_NEED_MARKER_BIT)) {
		mark = 1;
		ast_clear_flag(rtp, FLAG_NEED_MARKER_BIT);
	}

	/* Reconstruct part of the packet */
	reconstruct &= 0xFF80FFFF;
	reconstruct |= (bridged_payload << 16);
	reconstruct |= (mark << 23);
	rtpheader[0] = htonl(reconstruct);

	ast_rtp_instance_get_remote_address(instance1, &remote_address);

	/* Send the packet back out */
	res = sendto(bridged->s, (void *)rtpheader, len, 0, (struct sockaddr *)&remote_address, sizeof(remote_address));
	if (res < 0) {
		if (!ast_rtp_instance_get_prop(instance1, AST_RTP_PROPERTY_NAT) || (ast_rtp_instance_get_prop(instance1, AST_RTP_PROPERTY_NAT) && (ast_test_flag(bridged, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE))) {
			ast_debug(1, "RTP Transmission error of packet to %s:%d: %s\n", ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), strerror(errno));
		} else if (((ast_test_flag(bridged, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug) && !ast_test_flag(bridged, FLAG_NAT_INACTIVE_NOWARN)) {
			if (option_debug || rtpdebug)
				ast_debug(0, "RTP NAT: Can't write RTP to private address %s:%d, waiting for other end to send audio...\n", ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port));
			ast_set_flag(bridged, FLAG_NAT_INACTIVE_NOWARN);
		}
		return 0;
	} else if (rtp_debug_test_addr(&remote_address)) {
		ast_verbose("Sent RTP P2P packet to %s:%u (type %-2.2d, len %-6.6u)\n", ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), bridged_payload, len - hdrlen);
	}

	return 0;
}

static struct ast_frame *ast_rtp_read(struct ast_rtp_instance *instance, int rtcp)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);
	int res, hdrlen = 12, version, payloadtype, padding, mark, ext, cc, prev_seqno;
	unsigned int *rtpheader = (unsigned int*)(rtp->rawdata + AST_FRIENDLY_OFFSET), seqno, ssrc, timestamp;
	struct ast_rtp_payload_type payload;
	struct sockaddr_in remote_address = { 0, };

	/* If this is actually RTCP let's hop on over and handle it */
	if (rtcp) {
		if (rtp->rtcp) {
			return ast_rtcp_read(instance);
		}
		return &ast_null_frame;
	}

	/* If we are currently sending DTMF to the remote party send a continuation packet */
	if (rtp->sending_digit) {
		ast_rtp_dtmf_continuation(instance);
	}

	/* Actually read in the data from the socket */
	if ((res = recvfrom(rtp->s, rtp->rawdata + AST_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - AST_FRIENDLY_OFFSET, 0, (struct sockaddr*)&sin, &len)) < 0) {
		ast_assert(errno != EBADF);
		if (errno != EAGAIN) {
			ast_log(LOG_WARNING, "RTP Read error: %s. Hanging up.\n", strerror(errno));
			return NULL;
		}
		return &ast_null_frame;
	}

	/* Make sure the data that was read in is actually enough to make up an RTP packet */
	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short\n");
		return &ast_null_frame;
	}

	/* If strict RTP protection is enabled see if we need to learn the remote address or if we need to drop the packet */
	if (rtp->strict_rtp_state == STRICT_RTP_LEARN) {
		memcpy(&rtp->strict_rtp_address, &sin, sizeof(rtp->strict_rtp_address));
		rtp->strict_rtp_state = STRICT_RTP_CLOSED;
	} else if (rtp->strict_rtp_state == STRICT_RTP_CLOSED) {
		if ((rtp->strict_rtp_address.sin_addr.s_addr != sin.sin_addr.s_addr) || (rtp->strict_rtp_address.sin_port != sin.sin_port)) {
			ast_debug(1, "Received RTP packet from %s:%d, dropping due to strict RTP protection. Expected it to be from %s:%d\n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), ast_inet_ntoa(rtp->strict_rtp_address.sin_addr), ntohs(rtp->strict_rtp_address.sin_port));
			return &ast_null_frame;
		}
	}

	/* Get fields and verify this is an RTP packet */
	seqno = ntohl(rtpheader[0]);

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	if (!(version = (seqno & 0xC0000000) >> 30)) {
		if ((ast_stun_handle_packet(rtp->s, &sin, rtp->rawdata + AST_FRIENDLY_OFFSET, res, NULL, NULL) == AST_STUN_ACCEPT) &&
		    (!remote_address.sin_port && !remote_address.sin_addr.s_addr)) {
			ast_rtp_instance_set_remote_address(instance, &sin);
		}
		return &ast_null_frame;
	}

	/* If symmetric RTP is enabled see if the remote side is not what we expected and change where we are sending audio */
	if (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT)) {
		if ((remote_address.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (remote_address.sin_port != sin.sin_port)) {
			ast_rtp_instance_set_remote_address(instance, &sin);
			memcpy(&remote_address, &sin, sizeof(remote_address));
			if (rtp->rtcp) {
				memcpy(&rtp->rtcp->them, &sin, sizeof(rtp->rtcp->them));
				rtp->rtcp->them.sin_port = htons(ntohs(sin.sin_port)+1);
			}
			rtp->rxseqno = 0;
			ast_set_flag(rtp, FLAG_NAT_ACTIVE);
			if (option_debug || rtpdebug)
				ast_debug(0, "RTP NAT: Got audio from other end. Now sending to address %s:%d\n", ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port));
		}
	}

	/* If we are directly bridged to another instance send the audio directly out */
	if (ast_rtp_instance_get_bridged(instance) && !bridge_p2p_rtp_write(instance, rtpheader, res, hdrlen)) {
		return &ast_null_frame;
	}

	/* If the version is not what we expected by this point then just drop the packet */
	if (version != 2) {
		return &ast_null_frame;
	}

	/* Pull out the various other fields we will need */
	payloadtype = (seqno & 0x7f0000) >> 16;
	padding = seqno & (1 << 29);
	mark = seqno & (1 << 23);
	ext = seqno & (1 << 28);
	cc = (seqno & 0xF000000) >> 24;
	seqno &= 0xffff;
	timestamp = ntohl(rtpheader[1]);
	ssrc = ntohl(rtpheader[2]);

	/* Force a marker bit if the SSRC changes */
	if (!mark && rtp->rxssrc && rtp->rxssrc != ssrc) {
		if (option_debug || rtpdebug) {
			ast_debug(1, "Forcing Marker bit, because SSRC has changed\n");
		}
		mark = 1;
	}

	/* Remove any padding bytes that may be present */
	if (padding) {
		res -= rtp->rawdata[AST_FRIENDLY_OFFSET + res - 1];
	}

	/* Skip over any CSRC fields */
	if (cc) {
		hdrlen += cc * 4;
	}

	/* Look for any RTP extensions, currently we do not support any */
	if (ext) {
		hdrlen += (ntohl(rtpheader[hdrlen/4]) & 0xffff) << 2;
		hdrlen += 4;
		if (option_debug) {
			int profile;
			profile = (ntohl(rtpheader[3]) & 0xffff0000) >> 16;
			if (profile == 0x505a)
				ast_debug(1, "Found Zfone extension in RTP stream - zrtp - not supported.\n");
			else
				ast_debug(1, "Found unknown RTP Extensions %x\n", profile);
		}
	}

	/* Make sure after we potentially mucked with the header length that it is once again valid */
	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short (%d, expecting %d\n", res, hdrlen);
		return &ast_null_frame;
	}

	rtp->rxcount++;
	if (rtp->rxcount == 1) {
		rtp->seedrxseqno = seqno;
	}

	/* Do not schedule RR if RTCP isn't run */
	if (rtp->rtcp && rtp->rtcp->them.sin_addr.s_addr && rtp->rtcp->schedid < 1) {
		/* Schedule transmission of Receiver Report */
		rtp->rtcp->schedid = ast_sched_add(rtp->sched, ast_rtcp_calc_interval(rtp), ast_rtcp_write, rtp);
	}
	if ((int)rtp->lastrxseqno - (int)seqno  > 100) /* if so it would indicate that the sender cycled; allow for misordering */
		rtp->cycles += RTP_SEQ_MOD;

	prev_seqno = rtp->lastrxseqno;
	rtp->lastrxseqno = seqno;

	if (!rtp->themssrc) {
		rtp->themssrc = ntohl(rtpheader[2]); /* Record their SSRC to put in future RR */
	}

	if (rtp_debug_test_addr(&sin)) {
		ast_verbose("Got  RTP packet from    %s:%u (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6u)\n",
			    ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp,res - hdrlen);
	}

	payload = ast_rtp_codecs_payload_lookup(ast_rtp_instance_get_codecs(instance), payloadtype);

	/* If the payload is not actually an Asterisk one but a special one pass it off to the respective handler */
	if (!payload.asterisk_format) {
		struct ast_frame *f = NULL;

		if (payload.code == AST_RTP_DTMF) {
			f = process_dtmf_rfc2833(instance, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp, &sin, payloadtype, mark);
		} else if (payload.code == AST_RTP_CISCO_DTMF) {
			f = process_dtmf_cisco(instance, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp, &sin, payloadtype, mark);
		} else if (payload.code == AST_RTP_CN) {
			f = process_cn_rfc3389(instance, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp, &sin, payloadtype, mark);
		} else {
			ast_log(LOG_NOTICE, "Unknown RTP codec %d received from '%s'\n", payloadtype, ast_inet_ntoa(remote_address.sin_addr));
		}

		return f ? f : &ast_null_frame;
	}

	rtp->lastrxformat = rtp->f.subclass = payload.code;
	rtp->f.frametype = (rtp->f.subclass & AST_FORMAT_AUDIO_MASK) ? AST_FRAME_VOICE : (rtp->f.subclass & AST_FORMAT_VIDEO_MASK) ? AST_FRAME_VIDEO : AST_FRAME_TEXT;

	rtp->rxseqno = seqno;

	if (rtp->dtmf_timeout && rtp->dtmf_timeout < timestamp) {
		rtp->dtmf_timeout = 0;

		if (rtp->resp) {
			struct ast_frame *f;
			f = send_dtmf(instance, AST_FRAME_DTMF_END, 0);
			f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, 8000), ast_tv(0, 0));
			rtp->resp = 0;
			rtp->dtmf_timeout = rtp->dtmf_duration = 0;
			return f;
		}
	}

	rtp->lastrxts = timestamp;

	rtp->f.src = "RTP";
	rtp->f.mallocd = 0;
	rtp->f.datalen = res - hdrlen;
	rtp->f.data.ptr = rtp->rawdata + hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.offset = hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.seqno = seqno;

	if (rtp->f.subclass == AST_FORMAT_T140 && (int)seqno - (prev_seqno+1) > 0 && (int)seqno - (prev_seqno+1) < 10) {
		unsigned char *data = rtp->f.data.ptr;

		memmove(rtp->f.data.ptr+3, rtp->f.data.ptr, rtp->f.datalen);
		rtp->f.datalen +=3;
		*data++ = 0xEF;
		*data++ = 0xBF;
		*data = 0xBD;
	}

	if (rtp->f.subclass == AST_FORMAT_T140RED) {
		unsigned char *data = rtp->f.data.ptr;
		unsigned char *header_end;
		int num_generations;
		int header_length;
		int len;
		int diff =(int)seqno - (prev_seqno+1); /* if diff = 0, no drop*/
		int x;

		rtp->f.subclass = AST_FORMAT_T140;
		header_end = memchr(data, ((*data) & 0x7f), rtp->f.datalen);
		header_end++;

		header_length = header_end - data;
		num_generations = header_length / 4;
		len = header_length;

		if (!diff) {
			for (x = 0; x < num_generations; x++)
				len += data[x * 4 + 3];

			if (!(rtp->f.datalen - len))
				return &ast_null_frame;

			rtp->f.data.ptr += len;
			rtp->f.datalen -= len;
		} else if (diff > num_generations && diff < 10) {
			len -= 3;
			rtp->f.data.ptr += len;
			rtp->f.datalen -= len;

			data = rtp->f.data.ptr;
			*data++ = 0xEF;
			*data++ = 0xBF;
			*data = 0xBD;
		} else {
			for ( x = 0; x < num_generations - diff; x++)
				len += data[x * 4 + 3];

			rtp->f.data.ptr += len;
			rtp->f.datalen -= len;
		}
	}

	if (rtp->f.subclass & AST_FORMAT_AUDIO_MASK) {
		rtp->f.samples = ast_codec_get_samples(&rtp->f);
		if (rtp->f.subclass == AST_FORMAT_SLINEAR)
			ast_frame_byteswap_be(&rtp->f);
		calc_rxstamp(&rtp->f.delivery, rtp, timestamp, mark);
		/* Add timing data to let ast_generic_bridge() put the frame into a jitterbuf */
		ast_set_flag(&rtp->f, AST_FRFLAG_HAS_TIMING_INFO);
		rtp->f.ts = timestamp / 8;
		rtp->f.len = rtp->f.samples / ((ast_format_rate(rtp->f.subclass) / 1000));
	} else if (rtp->f.subclass & AST_FORMAT_VIDEO_MASK) {
		/* Video -- samples is # of samples vs. 90000 */
		if (!rtp->lastividtimestamp)
			rtp->lastividtimestamp = timestamp;
		rtp->f.samples = timestamp - rtp->lastividtimestamp;
		rtp->lastividtimestamp = timestamp;
		rtp->f.delivery.tv_sec = 0;
		rtp->f.delivery.tv_usec = 0;
		/* Pass the RTP marker bit as bit 0 in the subclass field.
		 * This is ok because subclass is actually a bitmask, and
		 * the low bits represent audio formats, that are not
		 * involved here since we deal with video.
		 */
		if (mark)
			rtp->f.subclass |= 0x1;
	} else {
		/* TEXT -- samples is # of samples vs. 1000 */
		if (!rtp->lastitexttimestamp)
			rtp->lastitexttimestamp = timestamp;
		rtp->f.samples = timestamp - rtp->lastitexttimestamp;
		rtp->lastitexttimestamp = timestamp;
		rtp->f.delivery.tv_sec = 0;
		rtp->f.delivery.tv_usec = 0;
	}

	return &rtp->f;
}

static void ast_rtp_prop_set(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (property == AST_RTP_PROPERTY_RTCP) {
		if (rtp->rtcp) {
			ast_debug(1, "Ignoring duplicate RTCP property on RTP instance '%p'\n", instance);
			return;
		}
		if (!(rtp->rtcp = ast_calloc(1, sizeof(*rtp->rtcp)))) {
			return;
		}
		if ((rtp->rtcp->s = create_new_socket("RTCP")) < 0) {
			ast_debug(1, "Failed to create a new socket for RTCP on instance '%p'\n", instance);
			ast_free(rtp->rtcp);
			rtp->rtcp = NULL;
			return;
		}

		/* Grab the IP address and port we are going to use */
		ast_rtp_instance_get_local_address(instance, &rtp->rtcp->us);
		rtp->rtcp->us.sin_port = htons(ntohs(rtp->rtcp->us.sin_port) + 1);

		/* Try to actually bind to the IP address and port we are going to use for RTCP, if this fails we have to bail out */
		if (bind(rtp->rtcp->s, (struct sockaddr*)&rtp->rtcp->us, sizeof(rtp->rtcp->us))) {
			ast_debug(1, "Failed to setup RTCP on RTP instance '%p'\n", instance);
			close(rtp->rtcp->s);
			ast_free(rtp->rtcp);
			rtp->rtcp = NULL;
			return;
		}

		ast_debug(1, "Setup RTCP on RTP instance '%p'\n", instance);
		rtp->rtcp->schedid = -1;

		return;
	}

	return;
}

static int ast_rtp_fd(struct ast_rtp_instance *instance, int rtcp)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtcp ? (rtp->rtcp ? rtp->rtcp->s : -1) : rtp->s;
}

static void ast_rtp_remote_address_set(struct ast_rtp_instance *instance, struct sockaddr_in *sin)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (rtp->rtcp) {
		ast_debug(1, "Setting RTCP address on RTP instance '%p'\n", instance);
		memcpy(&rtp->rtcp->them, sin, sizeof(rtp->rtcp->them));
		rtp->rtcp->them.sin_port = htons(ntohs(sin->sin_port) + 1);
	}

	rtp->rxseqno = 0;

	if (strictrtp) {
		rtp->strict_rtp_state = STRICT_RTP_LEARN;
	}

	return;
}

/*! \brief Write t140 redundacy frame
 * \param data primary data to be buffered
 */
static int red_write(const void *data)
{
	struct ast_rtp_instance *instance = (struct ast_rtp_instance*) data;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	ast_rtp_write(instance, &rtp->red->t140);

	return 1;
}

static int rtp_red_init(struct ast_rtp_instance *instance, int buffer_time, int *payloads, int generations)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	int x;

	if (!(rtp->red = ast_calloc(1, sizeof(*rtp->red)))) {
		return -1;
	}

	rtp->red->t140.frametype = AST_FRAME_TEXT;
	rtp->red->t140.subclass = AST_FORMAT_T140RED;
	rtp->red->t140.data.ptr = &rtp->red->buf_data;

	rtp->red->t140.ts = 0;
	rtp->red->t140red = rtp->red->t140;
	rtp->red->t140red.data.ptr = &rtp->red->t140red_data;
	rtp->red->t140red.datalen = 0;
	rtp->red->ti = buffer_time;
	rtp->red->num_gen = generations;
	rtp->red->hdrlen = generations * 4 + 1;
	rtp->red->prev_ts = 0;

	for (x = 0; x < generations; x++) {
		rtp->red->pt[x] = payloads[x];
		rtp->red->pt[x] |= 1 << 7; /* mark redundant generations pt */
		rtp->red->t140red_data[x*4] = rtp->red->pt[x];
	}
	rtp->red->t140red_data[x*4] = rtp->red->pt[x] = payloads[x]; /* primary pt */
	rtp->red->schedid = ast_sched_add(rtp->sched, generations, red_write, instance);

	rtp->red->t140.datalen = 0;

	return 0;
}

static int rtp_red_buffer(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (frame->datalen > -1) {
		struct rtp_red *red = rtp->red;
		memcpy(&red->buf_data[red->t140.datalen], frame->data.ptr, frame->datalen);
		red->t140.datalen += frame->datalen;
		red->t140.ts = frame->ts;
	}

	return 0;
}

static int ast_rtp_local_bridge(struct ast_rtp_instance *instance0, struct ast_rtp_instance *instance1)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance0);

	ast_set_flag(rtp, FLAG_NEED_MARKER_BIT);

	return 0;
}

static int ast_rtp_get_stat(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!rtp->rtcp) {
		return -1;
	}

	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_TXCOUNT, -1, stats->txcount, rtp->txcount);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_RXCOUNT, -1, stats->rxcount, rtp->rxcount);

	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_TXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->txploss, rtp->rtcp->reported_lost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_RXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->rxploss, rtp->rtcp->expected_prior - rtp->rtcp->received_prior);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_MAXRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->remote_maxrxploss, rtp->rtcp->reported_maxlost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_MINRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->remote_minrxploss, rtp->rtcp->reported_minlost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_NORMDEVRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->remote_normdevrxploss, rtp->rtcp->reported_normdev_lost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_STDEVRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->remote_stdevrxploss, rtp->rtcp->reported_stdev_lost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_MAXRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->local_maxrxploss, rtp->rtcp->maxrxlost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_MINRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->local_minrxploss, rtp->rtcp->minrxlost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_NORMDEVRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->local_normdevrxploss, rtp->rtcp->normdev_rxlost);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_STDEVRXPLOSS, AST_RTP_INSTANCE_STAT_COMBINED_LOSS, stats->local_stdevrxploss, rtp->rtcp->stdev_rxlost);
	AST_RTP_STAT_TERMINATOR(AST_RTP_INSTANCE_STAT_COMBINED_LOSS);

	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_TXJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->txjitter, rtp->rxjitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_RXJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->rxjitter, rtp->rtcp->reported_jitter / (unsigned int) 65536.0);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_MAXJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->remote_maxjitter, rtp->rtcp->reported_maxjitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_MINJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->remote_minjitter, rtp->rtcp->reported_minjitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_NORMDEVJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->remote_normdevjitter, rtp->rtcp->reported_normdev_jitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_STDEVJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->remote_stdevjitter, rtp->rtcp->reported_stdev_jitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_MAXJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->local_maxjitter, rtp->rtcp->maxrxjitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_MINJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->local_minjitter, rtp->rtcp->minrxjitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_NORMDEVJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->local_normdevjitter, rtp->rtcp->normdev_rxjitter);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_STDEVJITTER, AST_RTP_INSTANCE_STAT_COMBINED_JITTER, stats->local_stdevjitter, rtp->rtcp->stdev_rxjitter);
	AST_RTP_STAT_TERMINATOR(AST_RTP_INSTANCE_STAT_COMBINED_JITTER);

	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_RTT, AST_RTP_INSTANCE_STAT_COMBINED_RTT, stats->rtt, rtp->rtcp->rtt);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_MAX_RTT, AST_RTP_INSTANCE_STAT_COMBINED_RTT, stats->maxrtt, rtp->rtcp->maxrtt);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_MIN_RTT, AST_RTP_INSTANCE_STAT_COMBINED_RTT, stats->minrtt, rtp->rtcp->minrtt);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_NORMDEVRTT, AST_RTP_INSTANCE_STAT_COMBINED_RTT, stats->normdevrtt, rtp->rtcp->normdevrtt);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_STDEVRTT, AST_RTP_INSTANCE_STAT_COMBINED_RTT, stats->stdevrtt, rtp->rtcp->stdevrtt);
	AST_RTP_STAT_TERMINATOR(AST_RTP_INSTANCE_STAT_COMBINED_RTT);

	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_SSRC, -1, stats->local_ssrc, rtp->ssrc);
	AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_REMOTE_SSRC, -1, stats->remote_ssrc, rtp->themssrc);

	return 0;
}

static int ast_rtp_dtmf_compatible(struct ast_channel *chan0, struct ast_rtp_instance *instance0, struct ast_channel *chan1, struct ast_rtp_instance *instance1)
{
	/* If both sides are not using the same method of DTMF transmission
	 * (ie: one is RFC2833, other is INFO... then we can not do direct media.
	 * --------------------------------------------------
	 * | DTMF Mode |  HAS_DTMF  |  Accepts Begin Frames |
	 * |-----------|------------|-----------------------|
	 * | Inband    | False      | True                  |
	 * | RFC2833   | True       | True                  |
	 * | SIP INFO  | False      | False                 |
	 * --------------------------------------------------
	 */
	return (((ast_rtp_instance_get_prop(instance0, AST_RTP_PROPERTY_DTMF) != ast_rtp_instance_get_prop(instance1, AST_RTP_PROPERTY_DTMF)) ||
		 (!chan0->tech->send_digit_begin != !chan1->tech->send_digit_begin)) ? 0 : 1);
}

static void ast_rtp_stun_request(struct ast_rtp_instance *instance, struct sockaddr_in *suggestion, const char *username)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	ast_stun_request(rtp->s, suggestion, username, NULL);
}

static void ast_rtp_stop(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in sin = { 0, };

	if (rtp->rtcp) {
		AST_SCHED_DEL(rtp->sched, rtp->rtcp->schedid);
	}
	if (rtp->red) {
		AST_SCHED_DEL(rtp->sched, rtp->red->schedid);
		free(rtp->red);
		rtp->red = NULL;
	}

	ast_rtp_instance_set_remote_address(instance, &sin);
	if (rtp->rtcp) {
		memset(&rtp->rtcp->them.sin_addr, 0, sizeof(rtp->rtcp->them.sin_addr));
		memset(&rtp->rtcp->them.sin_port, 0, sizeof(rtp->rtcp->them.sin_port));
	}

	ast_set_flag(rtp, FLAG_NEED_MARKER_BIT);
}

static char *rtp_do_debug_ip(struct ast_cli_args *a)
{
	struct hostent *hp;
	struct ast_hostent ahp;
	int port = 0;
	char *p, *arg = ast_strdupa(a->argv[3]);

	p = strstr(arg, ":");
	if (p) {
		*p = '\0';
		p++;
		port = atoi(p);
	}
	hp = ast_gethostbyname(arg, &ahp);
	if (hp == NULL) {
		ast_cli(a->fd, "Lookup failed for '%s'\n", arg);
		return CLI_FAILURE;
	}
	rtpdebugaddr.sin_family = AF_INET;
	memcpy(&rtpdebugaddr.sin_addr, hp->h_addr, sizeof(rtpdebugaddr.sin_addr));
	rtpdebugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(a->fd, "RTP Debugging Enabled for IP: %s\n", ast_inet_ntoa(rtpdebugaddr.sin_addr));
	else
		ast_cli(a->fd, "RTP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(rtpdebugaddr.sin_addr), port);
	rtpdebug = 1;
	return CLI_SUCCESS;
}

static char *rtcp_do_debug_ip(struct ast_cli_args *a)
{
	struct hostent *hp;
	struct ast_hostent ahp;
	int port = 0;
	char *p, *arg = ast_strdupa(a->argv[3]);

	p = strstr(arg, ":");
	if (p) {
		*p = '\0';
		p++;
		port = atoi(p);
	}
	hp = ast_gethostbyname(arg, &ahp);
	if (hp == NULL) {
		ast_cli(a->fd, "Lookup failed for '%s'\n", arg);
		return CLI_FAILURE;
	}
	rtcpdebugaddr.sin_family = AF_INET;
	memcpy(&rtcpdebugaddr.sin_addr, hp->h_addr, sizeof(rtcpdebugaddr.sin_addr));
	rtcpdebugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(a->fd, "RTCP Debugging Enabled for IP: %s\n", ast_inet_ntoa(rtcpdebugaddr.sin_addr));
	else
		ast_cli(a->fd, "RTCP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(rtcpdebugaddr.sin_addr), port);
	rtcpdebug = 1;
	return CLI_SUCCESS;
}

static char *handle_cli_rtp_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rtp set debug {on|off|ip}";
		e->usage =
			"Usage: rtp set debug {on|off|ip host[:port]}\n"
			"       Enable/Disable dumping of all RTP packets. If 'ip' is\n"
			"       specified, limit the dumped packets to those to and from\n"
			"       the specified 'host' with optional port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == e->args) { /* set on or off */
		if (!strncasecmp(a->argv[e->args-1], "on", 2)) {
			rtpdebug = 1;
			memset(&rtpdebugaddr, 0, sizeof(rtpdebugaddr));
			ast_cli(a->fd, "RTP Debugging Enabled\n");
			return CLI_SUCCESS;
		} else if (!strncasecmp(a->argv[e->args-1], "off", 3)) {
			rtpdebug = 0;
			ast_cli(a->fd, "RTP Debugging Disabled\n");
			return CLI_SUCCESS;
		}
	} else if (a->argc == e->args +1) { /* ip */
		return rtp_do_debug_ip(a);
	}

	return CLI_SHOWUSAGE;   /* default, failure */
}

static char *handle_cli_rtcp_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rtcp set debug {on|off|ip}";
		e->usage =
			"Usage: rtcp set debug {on|off|ip host[:port]}\n"
			"       Enable/Disable dumping of all RTCP packets. If 'ip' is\n"
			"       specified, limit the dumped packets to those to and from\n"
			"       the specified 'host' with optional port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == e->args) { /* set on or off */
		if (!strncasecmp(a->argv[e->args-1], "on", 2)) {
			rtcpdebug = 1;
			memset(&rtcpdebugaddr, 0, sizeof(rtcpdebugaddr));
			ast_cli(a->fd, "RTCP Debugging Enabled\n");
			return CLI_SUCCESS;
		} else if (!strncasecmp(a->argv[e->args-1], "off", 3)) {
			rtcpdebug = 0;
			ast_cli(a->fd, "RTCP Debugging Disabled\n");
			return CLI_SUCCESS;
		}
	} else if (a->argc == e->args +1) { /* ip */
		return rtcp_do_debug_ip(a);
	}

	return CLI_SHOWUSAGE;   /* default, failure */
}

static char *handle_cli_rtcp_set_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rtcp set stats {on|off}";
		e->usage =
			"Usage: rtcp set stats {on|off}\n"
			"       Enable/Disable dumping of RTCP stats.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strncasecmp(a->argv[e->args-1], "on", 2))
		rtcpstats = 1;
	else if (!strncasecmp(a->argv[e->args-1], "off", 3))
		rtcpstats = 0;
	else
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "RTCP Stats %s\n", rtcpstats ? "Enabled" : "Disabled");
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_rtp[] = {
	AST_CLI_DEFINE(handle_cli_rtp_set_debug,  "Enable/Disable RTP debugging"),
	AST_CLI_DEFINE(handle_cli_rtcp_set_debug, "Enable/Disable RTCP debugging"),
	AST_CLI_DEFINE(handle_cli_rtcp_set_stats, "Enable/Disable RTCP stats"),
};

static int rtp_reload(int reload)
{
	struct ast_config *cfg;
	const char *s;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	cfg = ast_config_load2("rtp.conf", "rtp", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	rtpstart = DEFAULT_RTP_START;
	rtpend = DEFAULT_RTP_END;
	dtmftimeout = DEFAULT_DTMF_TIMEOUT;
	strictrtp = STRICT_RTP_OPEN;
	if (cfg) {
		if ((s = ast_variable_retrieve(cfg, "general", "rtpstart"))) {
			rtpstart = atoi(s);
			if (rtpstart < MINIMUM_RTP_PORT)
				rtpstart = MINIMUM_RTP_PORT;
			if (rtpstart > MAXIMUM_RTP_PORT)
				rtpstart = MAXIMUM_RTP_PORT;
		}
		if ((s = ast_variable_retrieve(cfg, "general", "rtpend"))) {
			rtpend = atoi(s);
			if (rtpend < MINIMUM_RTP_PORT)
				rtpend = MINIMUM_RTP_PORT;
			if (rtpend > MAXIMUM_RTP_PORT)
				rtpend = MAXIMUM_RTP_PORT;
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
			nochecksums = ast_false(s) ? 1 : 0;
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
		if ((s = ast_variable_retrieve(cfg, "general", "strictrtp"))) {
			strictrtp = ast_true(s);
		}
		ast_config_destroy(cfg);
	}
	if (rtpstart >= rtpend) {
		ast_log(LOG_WARNING, "Unreasonable values for RTP start/end port in rtp.conf\n");
		rtpstart = DEFAULT_RTP_START;
		rtpend = DEFAULT_RTP_END;
	}
	ast_verb(2, "RTP Allocating from port range %d -> %d\n", rtpstart, rtpend);
	return 0;
}

static int reload_module(void)
{
	rtp_reload(1);
	return 0;
}

static int load_module(void)
{
	if (ast_rtp_engine_register(&asterisk_rtp_engine)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_cli_register_multiple(cli_rtp, ARRAY_LEN(cli_rtp))) {
		ast_rtp_engine_unregister(&asterisk_rtp_engine);
		return AST_MODULE_LOAD_DECLINE;
	}

	rtp_reload(0);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_rtp_engine_unregister(&asterisk_rtp_engine);
	ast_cli_unregister_multiple(cli_rtp, ARRAY_LEN(cli_rtp));

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk RTP Stack",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		);
