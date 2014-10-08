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
 *
 * \ingroup rtp_engines
 */

/*** MODULEINFO
	<use type="external">uuid</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>

#ifdef HAVE_OPENSSL_SRTP
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#endif

#ifdef USE_PJPROJECT
/* Asterisk discourages the use of bzero in favor of memset, in fact if you try to use bzero it will tell you to use memset. As a result bzero has to be undefined
 * here since it is used internally by pjlib. The only other option would be to modify pjlib... which won't happen. */
#undef bzero
#define bzero bzero
#include "pjlib.h"
#include "pjlib-util.h"
#include "pjnath.h"
#endif

#include "asterisk/stun.h"
#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/acl.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/unaligned.h"
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/test.h"

#define MAX_TIMESTAMP_SKEW	640

#define RTP_SEQ_MOD     (1<<16)	/*!< A sequence number can't be more than 16 bits */
#define RTCP_DEFAULT_INTERVALMS   5000	/*!< Default milli-seconds between RTCP reports we send */
#define RTCP_MIN_INTERVALMS       500	/*!< Min milli-seconds between RTCP reports we send */
#define RTCP_MAX_INTERVALMS       60000	/*!< Max milli-seconds between RTCP reports we send */

#define DEFAULT_RTP_START 5000 /*!< Default port number to start allocating RTP ports from */
#define DEFAULT_RTP_END 31000  /*!< Default maximum port number to end allocating RTP ports at */

#define MINIMUM_RTP_PORT 1024 /*!< Minimum port number to accept */
#define MAXIMUM_RTP_PORT 65535 /*!< Maximum port number to accept */

#define DEFAULT_TURN_PORT 3478

#define TURN_STATE_WAIT_TIME 2000

#define RTCP_PT_FUR     192
#define RTCP_PT_SR      200
#define RTCP_PT_RR      201
#define RTCP_PT_SDES    202
#define RTCP_PT_BYE     203
#define RTCP_PT_APP     204

#define RTP_MTU		1200
#define DTMF_SAMPLE_RATE_MS    8 /*!< DTMF samples per millisecond */

#define DEFAULT_DTMF_TIMEOUT (150 * (8000 / 1000))	/*!< samples */

#define ZFONE_PROFILE_ID 0x505a

#define DEFAULT_LEARNING_MIN_SEQUENTIAL 4

#define SRTP_MASTER_KEY_LEN 16
#define SRTP_MASTER_SALT_LEN 14
#define SRTP_MASTER_LEN (SRTP_MASTER_KEY_LEN + SRTP_MASTER_SALT_LEN)

enum strict_rtp_state {
	STRICT_RTP_OPEN = 0, /*! No RTP packets should be dropped, all sources accepted */
	STRICT_RTP_LEARN,    /*! Accept next packet as source */
	STRICT_RTP_CLOSED,   /*! Drop all RTP packets not coming from source that was learned */
};

#define DEFAULT_STRICT_RTP STRICT_RTP_CLOSED
#define DEFAULT_ICESUPPORT 1

extern struct ast_srtp_res *res_srtp;
extern struct ast_srtp_policy_res *res_srtp_policy;

static int dtmftimeout = DEFAULT_DTMF_TIMEOUT;

static int rtpstart = DEFAULT_RTP_START;			/*!< First port for RTP sessions (set in rtp.conf) */
static int rtpend = DEFAULT_RTP_END;			/*!< Last port for RTP sessions (set in rtp.conf) */
static int rtpdebug;			/*!< Are we debugging? */
static int rtcpdebug;			/*!< Are we debugging RTCP? */
static int rtcpstats;			/*!< Are we debugging RTCP? */
static int rtcpinterval = RTCP_DEFAULT_INTERVALMS; /*!< Time between rtcp reports in millisecs */
static struct ast_sockaddr rtpdebugaddr;	/*!< Debug packets to/from this host */
static struct ast_sockaddr rtcpdebugaddr;	/*!< Debug RTCP packets to/from this host */
static int rtpdebugport;		/*< Debug only RTP packets from IP or IP+Port if port is > 0 */
static int rtcpdebugport;		/*< Debug only RTCP packets from IP or IP+Port if port is > 0 */
#ifdef SO_NO_CHECK
static int nochecksums;
#endif
static int strictrtp = DEFAULT_STRICT_RTP; /*< Only accept RTP frames from a defined source. If we receive an indication of a changing source, enter learning mode. */
static int learning_min_sequential = DEFAULT_LEARNING_MIN_SEQUENTIAL; /*< Number of sequential RTP frames needed from a single source during learning mode to accept new source. */
static int icesupport = DEFAULT_ICESUPPORT;
static struct sockaddr_in stunaddr;

#ifdef USE_PJPROJECT
static pj_str_t turnaddr;
static int turnport = DEFAULT_TURN_PORT;
static pj_str_t turnusername;
static pj_str_t turnpassword;

/*! \brief Pool factory used by pjlib to allocate memory. */
static pj_caching_pool cachingpool;

/*! \brief Global memory pool for configuration and timers */
static pj_pool_t *pool;

/*! \brief Global timer heap */
static pj_timer_heap_t *timer_heap;

/*! \brief Thread executing the timer heap */
static pj_thread_t *timer_thread;

/*! \brief Used to tell the timer thread to terminate */
static int timer_terminate;

/*! \brief Structure which contains ioqueue thread information */
struct ast_rtp_ioqueue_thread {
	/*! \brief Pool used by the thread */
	pj_pool_t *pool;
	/*! \brief The thread handling the queue and timer heap */
	pj_thread_t *thread;
	/*! \brief Ioqueue which polls on sockets */
	pj_ioqueue_t *ioqueue;
	/*! \brief Timer heap for scheduled items */
	pj_timer_heap_t *timerheap;
	/*! \brief Termination request */
	int terminate;
	/*! \brief Current number of descriptors being waited on */
	unsigned int count;
	/*! \brief Linked list information */
	AST_LIST_ENTRY(ast_rtp_ioqueue_thread) next;
};

/*! \brief List of ioqueue threads */
static AST_LIST_HEAD_STATIC(ioqueues, ast_rtp_ioqueue_thread);

#endif

#define FLAG_3389_WARNING               (1 << 0)
#define FLAG_NAT_ACTIVE                 (3 << 1)
#define FLAG_NAT_INACTIVE               (0 << 1)
#define FLAG_NAT_INACTIVE_NOWARN        (1 << 1)
#define FLAG_NEED_MARKER_BIT            (1 << 3)
#define FLAG_DTMF_COMPENSATE            (1 << 4)

#define TRANSPORT_SOCKET_RTP 0
#define TRANSPORT_SOCKET_RTCP 1
#define TRANSPORT_TURN_RTP 2
#define TRANSPORT_TURN_RTCP 3

/*! \brief RTP learning mode tracking information */
struct rtp_learning_info {
	int max_seq;	/*!< The highest sequence number received */
	int packets;	/*!< The number of remaining packets before the source is accepted */
};

#ifdef HAVE_OPENSSL_SRTP
struct dtls_details {
	SSL *ssl;         /*!< SSL session */
	BIO *read_bio;    /*!< Memory buffer for reading */
	BIO *write_bio;   /*!< Memory buffer for writing */
	enum ast_rtp_dtls_setup dtls_setup; /*!< Current setup state */
	enum ast_rtp_dtls_connection connection; /*!< Whether this is a new or existing connection */
};
#endif

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
	double rxjitter;                /*!< Interarrival jitter at the moment in seconds */
	double rxtransit;               /*!< Relative transit time for previous packet */
	struct ast_format lasttxformat;
	struct ast_format lastrxformat;

	int rtptimeout;			/*!< RTP timeout time (negative or zero means disabled, negative value means temporarily disabled) */
	int rtpholdtimeout;		/*!< RTP timeout when on hold (negative or zero means disabled, negative value means temporarily disabled). */
	int rtpkeepalive;		/*!< Send RTP comfort noice packets for keepalive */

	/* DTMF Reception Variables */
	char resp;                        /*!< The current digit being processed */
	unsigned int last_seqno;          /*!< The last known sequence number for any DTMF packet */
	unsigned int last_end_timestamp;  /*!< The last known timestamp received from an END packet */
	unsigned int dtmf_duration;       /*!< Total duration in samples since the digit start event */
	unsigned int dtmf_timeout;        /*!< When this timestamp is reached we consider END frame lost and forcibly abort digit */
	unsigned int dtmfsamples;
	enum ast_rtp_dtmf_mode dtmfmode;  /*!< The current DTMF mode of the RTP stream */
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
	struct ast_sched_context *sched;
	struct io_context *io;
	void *data;
	struct ast_rtcp *rtcp;
	struct ast_rtp *bridged;        /*!< Who we are Packet bridged to */

	enum strict_rtp_state strict_rtp_state; /*!< Current state that strict RTP protection is in */
	struct ast_sockaddr strict_rtp_address;  /*!< Remote address information for strict RTP purposes */
	struct ast_sockaddr alt_rtp_address; /*!<Alternate remote address information */

	/*
	 * Learning mode values based on pjmedia's probation mode.  Many of these values are redundant to the above,
	 * but these are in place to keep learning mode sequence values sealed from their normal counterparts.
	 */
	struct rtp_learning_info rtp_source_learn;	/* Learning mode track for the expected RTP source */
	struct rtp_learning_info alt_source_learn;	/* Learning mode tracking for a new RTP source after one has been chosen */

	struct rtp_red *red;

#ifdef USE_PJPROJECT
	pj_ice_sess *ice;           /*!< ICE session */
	pj_turn_sock *turn_rtp;     /*!< RTP TURN relay */
	pj_turn_sock *turn_rtcp;    /*!< RTCP TURN relay */
	ast_mutex_t lock;           /*!< Lock for synchronization purposes */
	pj_turn_state_t turn_state; /*!< Current state of the TURN relay session */
	ast_cond_t cond;            /*!< Condition for signaling */
	unsigned int passthrough:1; /*!< Bit to indicate that the received packet should be passed through */
	unsigned int rtp_passthrough:1; /*!< Bit to indicate that TURN RTP should be passed through */
	unsigned int rtcp_passthrough:1; /*!< Bit to indicate that TURN RTCP should be passed through */
	unsigned int ice_port;      /*!< Port that ICE was started with if it was previously started */
	struct ast_sockaddr rtp_loop; /*!< Loopback address for forwarding RTP from TURN */
	struct ast_sockaddr rtcp_loop; /*!< Loopback address for forwarding RTCP from TURN */

	struct ast_rtp_ioqueue_thread *ioqueue; /*!< The ioqueue thread handling us */

	char remote_ufrag[256];  /*!< The remote ICE username */
	char remote_passwd[256]; /*!< The remote ICE password */

	char local_ufrag[256];  /*!< The local ICE username */
	char local_passwd[256]; /*!< The local ICE password */

	struct ao2_container *ice_local_candidates;           /*!< The local ICE candidates */
	struct ao2_container *ice_active_remote_candidates;   /*!< The remote ICE candidates */
	struct ao2_container *ice_proposed_remote_candidates; /*!< Incoming remote ICE candidates for new session */
	struct ast_sockaddr ice_original_rtp_addr;            /*!< rtp address that ICE started on first session */
#endif

#ifdef HAVE_OPENSSL_SRTP
	SSL_CTX *ssl_ctx; /*!< SSL context */
	ast_mutex_t dtls_timer_lock;           /*!< Lock for synchronization purposes */
	enum ast_rtp_dtls_verify dtls_verify; /*!< What to verify */
	enum ast_srtp_suite suite;   /*!< SRTP crypto suite */
	enum ast_rtp_dtls_hash local_hash; /*!< Local hash used for the fingerprint */
	char local_fingerprint[160]; /*!< Fingerprint of our certificate */
	enum ast_rtp_dtls_hash remote_hash; /*!< Remote hash used for the fingerprint */
	unsigned char remote_fingerprint[EVP_MAX_MD_SIZE]; /*!< Fingerprint of the peer certificate */
	unsigned int rekey; /*!< Interval at which to renegotiate and rekey */
	int rekeyid; /*!< Scheduled item id for rekeying */
	int dtlstimerid; /*!< Scheduled item id for DTLS retransmission for RTP */
	struct dtls_details dtls; /*!< DTLS state information */
#endif
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
	struct ast_sockaddr us;		/*!< Socket representation of the local endpoint. */
	struct ast_sockaddr them;	/*!< Socket representation of the remote endpoint. */
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

#ifdef HAVE_OPENSSL_SRTP
	struct dtls_details dtls; /*!< DTLS state information */
#endif
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

AST_LIST_HEAD_NOLOCK(frame_list, ast_frame);

/* Forward Declarations */
static int ast_rtp_new(struct ast_rtp_instance *instance, struct ast_sched_context *sched, struct ast_sockaddr *addr, void *data);
static int ast_rtp_destroy(struct ast_rtp_instance *instance);
static int ast_rtp_dtmf_begin(struct ast_rtp_instance *instance, char digit);
static int ast_rtp_dtmf_end(struct ast_rtp_instance *instance, char digit);
static int ast_rtp_dtmf_end_with_duration(struct ast_rtp_instance *instance, char digit, unsigned int duration);
static int ast_rtp_dtmf_mode_set(struct ast_rtp_instance *instance, enum ast_rtp_dtmf_mode dtmf_mode);
static enum ast_rtp_dtmf_mode ast_rtp_dtmf_mode_get(struct ast_rtp_instance *instance);
static void ast_rtp_update_source(struct ast_rtp_instance *instance);
static void ast_rtp_change_source(struct ast_rtp_instance *instance);
static int ast_rtp_write(struct ast_rtp_instance *instance, struct ast_frame *frame);
static struct ast_frame *ast_rtp_read(struct ast_rtp_instance *instance, int rtcp);
static void ast_rtp_prop_set(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value);
static int ast_rtp_fd(struct ast_rtp_instance *instance, int rtcp);
static void ast_rtp_remote_address_set(struct ast_rtp_instance *instance, struct ast_sockaddr *addr);
static void ast_rtp_alt_remote_address_set(struct ast_rtp_instance *instance, struct ast_sockaddr *addr);
static int rtp_red_init(struct ast_rtp_instance *instance, int buffer_time, int *payloads, int generations);
static int rtp_red_buffer(struct ast_rtp_instance *instance, struct ast_frame *frame);
static int ast_rtp_local_bridge(struct ast_rtp_instance *instance0, struct ast_rtp_instance *instance1);
static int ast_rtp_get_stat(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat);
static int ast_rtp_dtmf_compatible(struct ast_channel *chan0, struct ast_rtp_instance *instance0, struct ast_channel *chan1, struct ast_rtp_instance *instance1);
static void ast_rtp_stun_request(struct ast_rtp_instance *instance, struct ast_sockaddr *suggestion, const char *username);
static void ast_rtp_stop(struct ast_rtp_instance *instance);
static int ast_rtp_qos_set(struct ast_rtp_instance *instance, int tos, int cos, const char* desc);
static int ast_rtp_sendcng(struct ast_rtp_instance *instance, int level);

#ifdef HAVE_OPENSSL_SRTP
static int ast_rtp_activate(struct ast_rtp_instance *instance);
static void dtls_srtp_check_pending(struct ast_rtp_instance *instance, struct ast_rtp *rtp, int rtcp);
#endif

static int __rtp_sendto(struct ast_rtp_instance *instance, void *buf, size_t size, int flags, struct ast_sockaddr *sa, int rtcp, int *ice, int use_srtp);

#ifdef USE_PJPROJECT
/*! \brief Destructor for locally created ICE candidates */
static void ast_rtp_ice_candidate_destroy(void *obj)
{
	struct ast_rtp_engine_ice_candidate *candidate = obj;

	if (candidate->foundation) {
		ast_free(candidate->foundation);
	}

	if (candidate->transport) {
		ast_free(candidate->transport);
	}
}

static void ast_rtp_ice_set_authentication(struct ast_rtp_instance *instance, const char *ufrag, const char *password)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!ast_strlen_zero(ufrag)) {
		ast_copy_string(rtp->remote_ufrag, ufrag, sizeof(rtp->remote_ufrag));
	}

	if (!ast_strlen_zero(password)) {
		ast_copy_string(rtp->remote_passwd, password, sizeof(rtp->remote_passwd));
	}
}

static int ice_candidate_cmp(void *obj, void *arg, int flags)
{
	struct ast_rtp_engine_ice_candidate *candidate1 = obj, *candidate2 = arg;

	if (strcmp(candidate1->foundation, candidate2->foundation) ||
			candidate1->id != candidate2->id ||
			ast_sockaddr_cmp(&candidate1->address, &candidate2->address) ||
			candidate1->type != candidate1->type) {
		return 0;
	}

	return CMP_MATCH | CMP_STOP;
}

static void ast_rtp_ice_add_remote_candidate(struct ast_rtp_instance *instance, const struct ast_rtp_engine_ice_candidate *candidate)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_rtp_engine_ice_candidate *remote_candidate;

	/* ICE sessions only support UDP candidates */
	if (strcmp(candidate->transport, "UDP")) {
		return;
	}

	if (!rtp->ice_proposed_remote_candidates &&
			!(rtp->ice_proposed_remote_candidates = ao2_container_alloc(1, NULL, ice_candidate_cmp))) {
		return;
	}

	/* If this is going to exceed the maximum number of ICE candidates don't even add it */
	if (ao2_container_count(rtp->ice_proposed_remote_candidates) == PJ_ICE_MAX_CAND) {
		return;
	}

	if (!(remote_candidate = ao2_alloc(sizeof(*remote_candidate), ast_rtp_ice_candidate_destroy))) {
		return;
	}

	remote_candidate->foundation = ast_strdup(candidate->foundation);
	remote_candidate->id = candidate->id;
	remote_candidate->transport = ast_strdup(candidate->transport);
	remote_candidate->priority = candidate->priority;
	ast_sockaddr_copy(&remote_candidate->address, &candidate->address);
	ast_sockaddr_copy(&remote_candidate->relay_address, &candidate->relay_address);
	remote_candidate->type = candidate->type;

	ao2_link(rtp->ice_proposed_remote_candidates, remote_candidate);
	ao2_ref(remote_candidate, -1);
}

AST_THREADSTORAGE(pj_thread_storage);

/*! \brief Function used to check if the calling thread is registered with pjlib. If it is not it will be registered. */
static void pj_thread_register_check(void)
{
	pj_thread_desc *desc;
	pj_thread_t *thread;

	if (pj_thread_is_registered() == PJ_TRUE) {
		return;
	}

	desc = ast_threadstorage_get(&pj_thread_storage, sizeof(pj_thread_desc));
	if (!desc) {
		ast_log(LOG_ERROR, "Could not get thread desc from thread-local storage. Expect awful things to occur\n");
		return;
	}
	pj_bzero(*desc, sizeof(*desc));

	if (pj_thread_register("Asterisk Thread", *desc, &thread) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Coudln't register thread with PJLIB.\n");
	}
	return;
}

/*! \brief Helper function which updates an ast_sockaddr with the candidate used for the component */
static void update_address_with_ice_candidate(struct ast_rtp *rtp, enum ast_rtp_ice_component_type component, struct ast_sockaddr *cand_address)
{
	char address[PJ_INET6_ADDRSTRLEN];

	if (!rtp->ice || (component < 1) || !rtp->ice->comp[component - 1].valid_check) {
		return;
	}

	ast_sockaddr_parse(cand_address, pj_sockaddr_print(&rtp->ice->comp[component - 1].valid_check->rcand->addr, address, sizeof(address), 0), 0);
	ast_sockaddr_set_port(cand_address, pj_sockaddr_get_port(&rtp->ice->comp[component - 1].valid_check->rcand->addr));
}

/*! \brief Helper function which sets up channel binding on a TURN session if applicable */
static void turn_enable_bind_channel(struct ast_rtp *rtp, pj_turn_sock *turn, enum ast_rtp_ice_component_type component, int transport)
{
	if (!rtp->ice || !turn || (component < 1) || !rtp->ice->comp[component - 1].valid_check ||
		(rtp->ice->comp[component - 1].valid_check->lcand->transport_id != transport)) {
		return;
	}

	pj_turn_sock_bind_channel(turn, &rtp->ice->comp[component - 1].valid_check->rcand->addr,
		sizeof(rtp->ice->comp[component - 1].valid_check->rcand->addr));
}

static int ice_create(struct ast_rtp_instance *instance, struct ast_sockaddr *addr,
	int port, int replace);

static void ast_rtp_ice_stop(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!rtp->ice) {
		return;
	}

	pj_thread_register_check();

	pj_ice_sess_destroy(rtp->ice);
	rtp->ice = NULL;
}

static int ice_reset_session(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	pj_ice_sess_role role = rtp->ice->role;
	int res;

	ast_rtp_ice_stop(instance);

	res = ice_create(instance, &rtp->ice_original_rtp_addr, rtp->ice_port, 1);
	if (!res) {
		/* Preserve the role that the old ICE session used */
		pj_ice_sess_change_role(rtp->ice, role);
	}

	return res;
}

static int ice_candidates_compare(struct ao2_container *left, struct ao2_container *right)
{
	struct ao2_iterator i;
	struct ast_rtp_engine_ice_candidate *right_candidate;

	if (ao2_container_count(left) != ao2_container_count(right)) {
		return -1;
	}

	i = ao2_iterator_init(right, 0);
	while ((right_candidate = ao2_iterator_next(&i))) {
		struct ast_rtp_engine_ice_candidate *left_candidate = ao2_find(left, right_candidate, OBJ_POINTER);

		if (!left_candidate) {
			ao2_ref(right_candidate, -1);
			ao2_iterator_destroy(&i);
			return -1;
		}

		ao2_ref(left_candidate, -1);
		ao2_ref(right_candidate, -1);
	}
	ao2_iterator_destroy(&i);

	return 0;
}

static void ast_rtp_ice_start(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	pj_str_t ufrag = pj_str(rtp->remote_ufrag), passwd = pj_str(rtp->remote_passwd);
	pj_ice_sess_cand candidates[PJ_ICE_MAX_CAND];
	struct ao2_iterator i;
	struct ast_rtp_engine_ice_candidate *candidate;
	int cand_cnt = 0;

	if (!rtp->ice || !rtp->ice_proposed_remote_candidates) {
		return;
	}

	/* Check for equivalence in the lists */
	if (rtp->ice_active_remote_candidates &&
			!ice_candidates_compare(rtp->ice_proposed_remote_candidates, rtp->ice_active_remote_candidates)) {
		ao2_cleanup(rtp->ice_proposed_remote_candidates);
		rtp->ice_proposed_remote_candidates = NULL;
		return;
	}

	/* Out with the old, in with the new */
	ao2_cleanup(rtp->ice_active_remote_candidates);
	rtp->ice_active_remote_candidates = rtp->ice_proposed_remote_candidates;
	rtp->ice_proposed_remote_candidates = NULL;

	/* Reset the ICE session. Is this going to work? */
	if (ice_reset_session(instance)) {
		ast_log(LOG_NOTICE, "Failed to create replacement ICE session\n");
		return;
	}

	pj_thread_register_check();

	i = ao2_iterator_init(rtp->ice_active_remote_candidates, 0);

	while ((candidate = ao2_iterator_next(&i)) && (cand_cnt < PJ_ICE_MAX_CAND)) {
		pj_str_t address;

		pj_strdup2(rtp->ice->pool, &candidates[cand_cnt].foundation, candidate->foundation);
		candidates[cand_cnt].comp_id = candidate->id;
		candidates[cand_cnt].prio = candidate->priority;

		pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&address, ast_sockaddr_stringify(&candidate->address)), &candidates[cand_cnt].addr);

		if (!ast_sockaddr_isnull(&candidate->relay_address)) {
			pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&address, ast_sockaddr_stringify(&candidate->relay_address)), &candidates[cand_cnt].rel_addr);
		}

		if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_HOST) {
			candidates[cand_cnt].type = PJ_ICE_CAND_TYPE_HOST;
		} else if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_SRFLX) {
			candidates[cand_cnt].type = PJ_ICE_CAND_TYPE_SRFLX;
		} else if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_RELAYED) {
			candidates[cand_cnt].type = PJ_ICE_CAND_TYPE_RELAYED;
		}

		if (candidate->id == AST_RTP_ICE_COMPONENT_RTP && rtp->turn_rtp) {
			pj_turn_sock_set_perm(rtp->turn_rtp, 1, &candidates[cand_cnt].addr, 1);
		} else if (candidate->id == AST_RTP_ICE_COMPONENT_RTCP && rtp->turn_rtcp) {
			pj_turn_sock_set_perm(rtp->turn_rtcp, 1, &candidates[cand_cnt].addr, 1);
		}

		cand_cnt++;
		ao2_ref(candidate, -1);
	}

	ao2_iterator_destroy(&i);

	if (pj_ice_sess_create_check_list(rtp->ice, &ufrag, &passwd, ao2_container_count(rtp->ice_active_remote_candidates), &candidates[0]) == PJ_SUCCESS) {
		ast_test_suite_event_notify("ICECHECKLISTCREATE", "Result: SUCCESS");
		pj_ice_sess_start_check(rtp->ice);
		pj_timer_heap_poll(timer_heap, NULL);
		rtp->strict_rtp_state = STRICT_RTP_OPEN;
		return;
	}

	ast_test_suite_event_notify("ICECHECKLISTCREATE", "Result: FAILURE");

	/* even though create check list failed don't stop ice as
	   it might still work */
	ast_debug(1, "Failed to create ICE session check list\n");
	/* however we do need to reset remote candidates since
	   this function may be re-entered */
	ao2_ref(rtp->ice_active_remote_candidates, -1);
	rtp->ice_active_remote_candidates = NULL;
	rtp->ice->rcand_cnt = rtp->ice->clist.count = 0;
}

static const char *ast_rtp_ice_get_ufrag(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtp->local_ufrag;
}

static const char *ast_rtp_ice_get_password(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtp->local_passwd;
}

static struct ao2_container *ast_rtp_ice_get_local_candidates(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (rtp->ice_local_candidates) {
		ao2_ref(rtp->ice_local_candidates, +1);
	}

	return rtp->ice_local_candidates;
}

static void ast_rtp_ice_lite(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!rtp->ice) {
		return;
	}

	pj_thread_register_check();

	pj_ice_sess_change_role(rtp->ice, PJ_ICE_SESS_ROLE_CONTROLLING);
}

static void ast_rtp_ice_set_role(struct ast_rtp_instance *instance, enum ast_rtp_ice_role role)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!rtp->ice) {
		return;
	}

	pj_thread_register_check();

	pj_ice_sess_change_role(rtp->ice, role == AST_RTP_ICE_ROLE_CONTROLLED ?
		PJ_ICE_SESS_ROLE_CONTROLLED : PJ_ICE_SESS_ROLE_CONTROLLING);
}

static void ast_rtp_ice_add_cand(struct ast_rtp *rtp, unsigned comp_id, unsigned transport_id, pj_ice_cand_type type, pj_uint16_t local_pref,
					const pj_sockaddr_t *addr, const pj_sockaddr_t *base_addr, const pj_sockaddr_t *rel_addr, int addr_len)
{
	pj_str_t foundation;
	struct ast_rtp_engine_ice_candidate *candidate, *existing;
	char address[PJ_INET6_ADDRSTRLEN];

	pj_thread_register_check();

	pj_ice_calc_foundation(rtp->ice->pool, &foundation, type, addr);

	if (!rtp->ice_local_candidates && !(rtp->ice_local_candidates = ao2_container_alloc(1, NULL, ice_candidate_cmp))) {
		return;
	}

	if (!(candidate = ao2_alloc(sizeof(*candidate), ast_rtp_ice_candidate_destroy))) {
		return;
	}

	candidate->foundation = ast_strndup(pj_strbuf(&foundation), pj_strlen(&foundation));
	candidate->id = comp_id;
	candidate->transport = ast_strdup("UDP");

	ast_sockaddr_parse(&candidate->address, pj_sockaddr_print(addr, address, sizeof(address), 0), 0);
	ast_sockaddr_set_port(&candidate->address, pj_sockaddr_get_port(addr));

	if (rel_addr) {
		ast_sockaddr_parse(&candidate->relay_address, pj_sockaddr_print(rel_addr, address, sizeof(address), 0), 0);
		ast_sockaddr_set_port(&candidate->relay_address, pj_sockaddr_get_port(rel_addr));
	}

	if (type == PJ_ICE_CAND_TYPE_HOST) {
		candidate->type = AST_RTP_ICE_CANDIDATE_TYPE_HOST;
	} else if (type == PJ_ICE_CAND_TYPE_SRFLX) {
		candidate->type = AST_RTP_ICE_CANDIDATE_TYPE_SRFLX;
	} else if (type == PJ_ICE_CAND_TYPE_RELAYED) {
		candidate->type = AST_RTP_ICE_CANDIDATE_TYPE_RELAYED;
	}

	if ((existing = ao2_find(rtp->ice_local_candidates, candidate, OBJ_POINTER))) {
		ao2_ref(existing, -1);
		ao2_ref(candidate, -1);
		return;
	}

	if (pj_ice_sess_add_cand(rtp->ice, comp_id, transport_id, type, local_pref, &foundation, addr, base_addr, rel_addr, addr_len, NULL) != PJ_SUCCESS) {
		ao2_ref(candidate, -1);
		return;
	}

	/* By placing the candidate into the ICE session it will have produced the priority, so update the local candidate with it */
	candidate->priority = rtp->ice->lcand[rtp->ice->lcand_cnt - 1].prio;

	ao2_link(rtp->ice_local_candidates, candidate);
	ao2_ref(candidate, -1);
}

static void ast_rtp_on_turn_rx_rtp_data(pj_turn_sock *turn_sock, void *pkt, unsigned pkt_len, const pj_sockaddr_t *peer_addr, unsigned addr_len)
{
	struct ast_rtp_instance *instance = pj_turn_sock_get_user_data(turn_sock);
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	pj_status_t status;

	status = pj_ice_sess_on_rx_pkt(rtp->ice, AST_RTP_ICE_COMPONENT_RTP, TRANSPORT_TURN_RTP, pkt, pkt_len, peer_addr,
		addr_len);
	if (status != PJ_SUCCESS) {
		char buf[100];

		pj_strerror(status, buf, sizeof(buf));
		ast_log(LOG_WARNING, "PJ ICE Rx error status code: %d '%s'.\n",
			(int)status, buf);
		return;
	}
	if (!rtp->rtp_passthrough) {
		return;
	}
	rtp->rtp_passthrough = 0;

	ast_sendto(rtp->s, pkt, pkt_len, 0, &rtp->rtp_loop);
}

static void ast_rtp_on_turn_rtp_state(pj_turn_sock *turn_sock, pj_turn_state_t old_state, pj_turn_state_t new_state)
{
	struct ast_rtp_instance *instance = pj_turn_sock_get_user_data(turn_sock);
	struct ast_rtp *rtp;

	/* If this is a leftover from an already notified RTP instance just ignore the state change */
	if (!instance) {
		return;
	}

	rtp = ast_rtp_instance_get_data(instance);

	/* We store the new state so the other thread can actually handle it */
	ast_mutex_lock(&rtp->lock);
	rtp->turn_state = new_state;
	ast_cond_signal(&rtp->cond);

	if (new_state == PJ_TURN_STATE_DESTROYING) {
		pj_turn_sock_set_user_data(rtp->turn_rtp, NULL);
		rtp->turn_rtp = NULL;
	}

	ast_mutex_unlock(&rtp->lock);
}

/* RTP TURN Socket interface declaration */
static pj_turn_sock_cb ast_rtp_turn_rtp_sock_cb = {
	.on_rx_data = ast_rtp_on_turn_rx_rtp_data,
	.on_state = ast_rtp_on_turn_rtp_state,
};

static void ast_rtp_on_turn_rx_rtcp_data(pj_turn_sock *turn_sock, void *pkt, unsigned pkt_len, const pj_sockaddr_t *peer_addr, unsigned addr_len)
{
	struct ast_rtp_instance *instance = pj_turn_sock_get_user_data(turn_sock);
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	pj_status_t status;

	status = pj_ice_sess_on_rx_pkt(rtp->ice, AST_RTP_ICE_COMPONENT_RTCP, TRANSPORT_TURN_RTCP, pkt, pkt_len, peer_addr,
		addr_len);
	if (status != PJ_SUCCESS) {
		char buf[100];

		pj_strerror(status, buf, sizeof(buf));
		ast_log(LOG_WARNING, "PJ ICE Rx error status code: %d '%s'.\n",
			(int)status, buf);
		return;
	}
	if (!rtp->rtcp_passthrough) {
		return;
	}
	rtp->rtcp_passthrough = 0;

	ast_sendto(rtp->rtcp->s, pkt, pkt_len, 0, &rtp->rtcp_loop);
}

static void ast_rtp_on_turn_rtcp_state(pj_turn_sock *turn_sock, pj_turn_state_t old_state, pj_turn_state_t new_state)
{
	struct ast_rtp_instance *instance = pj_turn_sock_get_user_data(turn_sock);
	struct ast_rtp *rtp = NULL;

	/* If this is a leftover from an already destroyed RTP instance just ignore the state change */
	if (!instance) {
		return;
	}

	rtp = ast_rtp_instance_get_data(instance);

	/* We store the new state so the other thread can actually handle it */
	ast_mutex_lock(&rtp->lock);
	rtp->turn_state = new_state;
	ast_cond_signal(&rtp->cond);

	if (new_state == PJ_TURN_STATE_DESTROYING) {
		pj_turn_sock_set_user_data(rtp->turn_rtcp, NULL);
		rtp->turn_rtcp = NULL;
	}

	ast_mutex_unlock(&rtp->lock);
}

/* RTCP TURN Socket interface declaration */
static pj_turn_sock_cb ast_rtp_turn_rtcp_sock_cb = {
	.on_rx_data = ast_rtp_on_turn_rx_rtcp_data,
	.on_state = ast_rtp_on_turn_rtcp_state,
};

/*! \brief Worker thread for ioqueue and timerheap */
static int ioqueue_worker_thread(void *data)
{
	struct ast_rtp_ioqueue_thread *ioqueue = data;

	while (!ioqueue->terminate) {
		const pj_time_val delay = {0, 10};

		pj_ioqueue_poll(ioqueue->ioqueue, &delay);

		pj_timer_heap_poll(ioqueue->timerheap, NULL);
	}

	return 0;
}

/*! \brief Destroyer for ioqueue thread */
static void rtp_ioqueue_thread_destroy(struct ast_rtp_ioqueue_thread *ioqueue)
{
	if (ioqueue->thread) {
		ioqueue->terminate = 1;
		pj_thread_join(ioqueue->thread);
		pj_thread_destroy(ioqueue->thread);
	}

	pj_pool_release(ioqueue->pool);
	ast_free(ioqueue);
}

/*! \brief Removal function for ioqueue thread, determines if it should be terminated and destroyed */
static void rtp_ioqueue_thread_remove(struct ast_rtp_ioqueue_thread *ioqueue)
{
	int destroy = 0;

	/* If nothing is using this ioqueue thread destroy it */
	AST_LIST_LOCK(&ioqueues);
	if ((ioqueue->count - 2) == 0) {
		destroy = 1;
		AST_LIST_REMOVE(&ioqueues, ioqueue, next);
	}
	AST_LIST_UNLOCK(&ioqueues);

	if (!destroy) {
		return;
	}

	rtp_ioqueue_thread_destroy(ioqueue);
}

/*! \brief Finder and allocator for an ioqueue thread */
static struct ast_rtp_ioqueue_thread *rtp_ioqueue_thread_get_or_create(void)
{
	struct ast_rtp_ioqueue_thread *ioqueue;
	pj_lock_t *lock;

	AST_LIST_LOCK(&ioqueues);

	/* See if an ioqueue thread exists that can handle more */
	AST_LIST_TRAVERSE(&ioqueues, ioqueue, next) {
		if ((ioqueue->count + 2) < PJ_IOQUEUE_MAX_HANDLES) {
			break;
		}
	}

	/* If we found one bump it up and return it */
	if (ioqueue) {
		ioqueue->count += 2;
		goto end;
	}

	ioqueue = ast_calloc(1, sizeof(*ioqueue));
	if (!ioqueue) {
		goto end;
	}

	ioqueue->pool = pj_pool_create(&cachingpool.factory, "rtp", 512, 512, NULL);

	/* We use a timer on the ioqueue thread for TURN so that two threads aren't operating
	 * on a session at the same time
	 */
	if (pj_timer_heap_create(ioqueue->pool, 4, &ioqueue->timerheap) != PJ_SUCCESS) {
		goto fatal;
	}

	if (pj_lock_create_recursive_mutex(ioqueue->pool, "rtp%p", &lock) != PJ_SUCCESS) {
		goto fatal;
	}

	pj_timer_heap_set_lock(ioqueue->timerheap, lock, PJ_TRUE);

	if (pj_ioqueue_create(ioqueue->pool, PJ_IOQUEUE_MAX_HANDLES, &ioqueue->ioqueue) != PJ_SUCCESS) {
		goto fatal;
	}

	if (pj_thread_create(ioqueue->pool, "ice", &ioqueue_worker_thread, ioqueue, 0, 0, &ioqueue->thread) != PJ_SUCCESS) {
		goto fatal;
	}

	AST_LIST_INSERT_HEAD(&ioqueues, ioqueue, next);

	/* Since this is being returned to an active session the count always starts at 2 */
	ioqueue->count = 2;

	goto end;

fatal:
	rtp_ioqueue_thread_destroy(ioqueue);
	ioqueue = NULL;

end:
	AST_LIST_UNLOCK(&ioqueues);
	return ioqueue;
}

static void ast_rtp_ice_turn_request(struct ast_rtp_instance *instance, enum ast_rtp_ice_component_type component,
		pj_turn_tp_type conn_type, const char *server, unsigned int port, const char *username, const char *password)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	pj_turn_sock **turn_sock;
	const pj_turn_sock_cb *turn_cb;
	int conn_transport;
	pj_stun_auth_cred cred = { 0, };
	pj_str_t turn_addr;
	struct ast_sockaddr addr = { { 0, } };
	pj_stun_config stun_config;
	struct timeval wait = ast_tvadd(ast_tvnow(), ast_samp2tv(TURN_STATE_WAIT_TIME, 1000));
	struct timespec ts = { .tv_sec = wait.tv_sec, .tv_nsec = wait.tv_usec * 1000, };
	pj_turn_session_info info;
	struct ast_sockaddr local, loop;

	ast_rtp_instance_get_local_address(instance, &local);
	if (ast_sockaddr_is_ipv4(&local)) {
		ast_sockaddr_parse(&loop, "127.0.0.1", PARSE_PORT_FORBID);
	} else {
		ast_sockaddr_parse(&loop, "::1", PARSE_PORT_FORBID);
	}

	/* Determine what component we are requesting a TURN session for */
	if (component == AST_RTP_ICE_COMPONENT_RTP) {
		turn_sock = &rtp->turn_rtp;
		turn_cb = &ast_rtp_turn_rtp_sock_cb;
		conn_transport = TRANSPORT_TURN_RTP;
		ast_sockaddr_set_port(&loop, ast_sockaddr_port(&local));
	} else if (component == AST_RTP_ICE_COMPONENT_RTCP) {
		turn_sock = &rtp->turn_rtcp;
		turn_cb = &ast_rtp_turn_rtcp_sock_cb;
		conn_transport = TRANSPORT_TURN_RTCP;
		ast_sockaddr_set_port(&loop, ast_sockaddr_port(&rtp->rtcp->us));
	} else {
		return;
	}

	ast_sockaddr_parse(&addr, server, PARSE_PORT_FORBID);

	ast_mutex_lock(&rtp->lock);
	if (*turn_sock) {
		pj_turn_sock_destroy(*turn_sock);
		rtp->turn_state = PJ_TURN_STATE_NULL;
		while (rtp->turn_state != PJ_TURN_STATE_DESTROYING) {
			ast_cond_timedwait(&rtp->cond, &rtp->lock, &ts);
		}
	}
	ast_mutex_unlock(&rtp->lock);

	if (component == AST_RTP_ICE_COMPONENT_RTP && !rtp->ioqueue) {
		rtp->ioqueue = rtp_ioqueue_thread_get_or_create();
		if (!rtp->ioqueue) {
			return;
		}
	}

	pj_stun_config_init(&stun_config, &cachingpool.factory, 0, rtp->ioqueue->ioqueue, rtp->ioqueue->timerheap);

	if (pj_turn_sock_create(&stun_config, ast_sockaddr_is_ipv4(&addr) ? pj_AF_INET() : pj_AF_INET6(), conn_type,
		turn_cb, NULL, instance, turn_sock) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Could not create a TURN client socket\n");
		return;
	}

	cred.type = PJ_STUN_AUTH_CRED_STATIC;
	pj_strset2(&cred.data.static_cred.username, (char*)username);
	cred.data.static_cred.data_type = PJ_STUN_PASSWD_PLAIN;
	pj_strset2(&cred.data.static_cred.data, (char*)password);

	/* Because the TURN socket is asynchronous but we are synchronous we need to wait until it is done */
	ast_mutex_lock(&rtp->lock);
	pj_turn_sock_alloc(*turn_sock, pj_cstr(&turn_addr, server), port, NULL, &cred, NULL);
	while (rtp->turn_state < PJ_TURN_STATE_READY) {
		ast_cond_timedwait(&rtp->cond, &rtp->lock, &ts);
	}
	ast_mutex_unlock(&rtp->lock);

	/* If a TURN session was allocated add it as a candidate */
	if (rtp->turn_state != PJ_TURN_STATE_READY) {
		return;
	}

	pj_turn_sock_get_info(*turn_sock, &info);

	ast_rtp_ice_add_cand(rtp, component, conn_transport, PJ_ICE_CAND_TYPE_RELAYED, 65535, &info.relay_addr,
		&info.relay_addr, &info.mapped_addr, pj_sockaddr_get_len(&info.relay_addr));

	if (component == AST_RTP_ICE_COMPONENT_RTP) {
		ast_sockaddr_copy(&rtp->rtp_loop, &loop);
	} else if (component == AST_RTP_ICE_COMPONENT_RTCP) {
		ast_sockaddr_copy(&rtp->rtcp_loop, &loop);
	}
}

static char *generate_random_string(char *buf, size_t size)
{
        long val[4];
        int x;

        for (x=0; x<4; x++)
                val[x] = ast_random();
        snprintf(buf, size, "%08lx%08lx%08lx%08lx", val[0], val[1], val[2], val[3]);

        return buf;
}

/* ICE RTP Engine interface declaration */
static struct ast_rtp_engine_ice ast_rtp_ice = {
	.set_authentication = ast_rtp_ice_set_authentication,
	.add_remote_candidate = ast_rtp_ice_add_remote_candidate,
	.start = ast_rtp_ice_start,
	.stop = ast_rtp_ice_stop,
	.get_ufrag = ast_rtp_ice_get_ufrag,
	.get_password = ast_rtp_ice_get_password,
	.get_local_candidates = ast_rtp_ice_get_local_candidates,
	.ice_lite = ast_rtp_ice_lite,
	.set_role = ast_rtp_ice_set_role,
};
#endif

#ifdef HAVE_OPENSSL_SRTP
static int dtls_verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
	/* We don't want to actually verify the certificate so just accept what they have provided */
	return 1;
}

static int dtls_details_initialize(struct dtls_details *dtls, SSL_CTX *ssl_ctx,
	enum ast_rtp_dtls_setup setup)
{
	dtls->dtls_setup = setup;

	if (!(dtls->ssl = SSL_new(ssl_ctx))) {
		ast_log(LOG_ERROR, "Failed to allocate memory for SSL\n");
		goto error;
	}

	if (!(dtls->read_bio = BIO_new(BIO_s_mem()))) {
		ast_log(LOG_ERROR, "Failed to allocate memory for inbound SSL traffic\n");
		goto error;
	}
	BIO_set_mem_eof_return(dtls->read_bio, -1);

	if (!(dtls->write_bio = BIO_new(BIO_s_mem()))) {
		ast_log(LOG_ERROR, "Failed to allocate memory for outbound SSL traffic\n");
		goto error;
	}
	BIO_set_mem_eof_return(dtls->write_bio, -1);

	SSL_set_bio(dtls->ssl, dtls->read_bio, dtls->write_bio);

	if (dtls->dtls_setup == AST_RTP_DTLS_SETUP_PASSIVE) {
		SSL_set_accept_state(dtls->ssl);
	} else {
		SSL_set_connect_state(dtls->ssl);
	}
	dtls->connection = AST_RTP_DTLS_CONNECTION_NEW;

	return 0;

error:
	if (dtls->read_bio) {
		BIO_free(dtls->read_bio);
		dtls->read_bio = NULL;
	}

	if (dtls->write_bio) {
		BIO_free(dtls->write_bio);
		dtls->write_bio = NULL;
	}

	if (dtls->ssl) {
		SSL_free(dtls->ssl);
		dtls->ssl = NULL;
	}
	return -1;
}

static int dtls_setup_rtcp(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!rtp->ssl_ctx || !rtp->rtcp) {
		return 0;
	}

	return dtls_details_initialize(&rtp->rtcp->dtls, rtp->ssl_ctx, rtp->dtls.dtls_setup);
}

static int ast_rtp_dtls_set_configuration(struct ast_rtp_instance *instance, const struct ast_rtp_dtls_cfg *dtls_cfg)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	int res;

	if (!dtls_cfg->enabled) {
		return 0;
	}

	if (!ast_rtp_engine_srtp_is_registered()) {
		return -1;
	}

	if (!(rtp->ssl_ctx = SSL_CTX_new(DTLSv1_method()))) {
		return -1;
	}

	rtp->dtls_verify = dtls_cfg->verify;

	SSL_CTX_set_verify(rtp->ssl_ctx, (rtp->dtls_verify & AST_RTP_DTLS_VERIFY_FINGERPRINT) || (rtp->dtls_verify & AST_RTP_DTLS_VERIFY_CERTIFICATE) ?
		SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT : SSL_VERIFY_NONE, !(rtp->dtls_verify & AST_RTP_DTLS_VERIFY_CERTIFICATE) ?
		dtls_verify_callback : NULL);

	if (dtls_cfg->suite == AST_AES_CM_128_HMAC_SHA1_80) {
		SSL_CTX_set_tlsext_use_srtp(rtp->ssl_ctx, "SRTP_AES128_CM_SHA1_80");
	} else if (dtls_cfg->suite == AST_AES_CM_128_HMAC_SHA1_32) {
		SSL_CTX_set_tlsext_use_srtp(rtp->ssl_ctx, "SRTP_AES128_CM_SHA1_32");
	} else {
		ast_log(LOG_ERROR, "Unsupported suite specified for DTLS-SRTP on RTP instance '%p'\n", instance);
		return -1;
	}

	rtp->local_hash = dtls_cfg->hash;

	if (!ast_strlen_zero(dtls_cfg->certfile)) {
		char *private = ast_strlen_zero(dtls_cfg->pvtfile) ? dtls_cfg->certfile : dtls_cfg->pvtfile;
		BIO *certbio;
		X509 *cert;
		const EVP_MD *type;
		unsigned int size, i;
		unsigned char fingerprint[EVP_MAX_MD_SIZE];
		char *local_fingerprint = rtp->local_fingerprint;

		if (!SSL_CTX_use_certificate_file(rtp->ssl_ctx, dtls_cfg->certfile, SSL_FILETYPE_PEM)) {
			ast_log(LOG_ERROR, "Specified certificate file '%s' for RTP instance '%p' could not be used\n",
				dtls_cfg->certfile, instance);
			return -1;
		}

		if (!SSL_CTX_use_PrivateKey_file(rtp->ssl_ctx, private, SSL_FILETYPE_PEM) ||
		    !SSL_CTX_check_private_key(rtp->ssl_ctx)) {
			ast_log(LOG_ERROR, "Specified private key file '%s' for RTP instance '%p' could not be used\n",
				private, instance);
			return -1;
		}

		if (!(certbio = BIO_new(BIO_s_file()))) {
			ast_log(LOG_ERROR, "Failed to allocate memory for certificate fingerprinting on RTP instance '%p'\n",
				instance);
			return -1;
		}

		if (rtp->local_hash == AST_RTP_DTLS_HASH_SHA1) {
			type = EVP_sha1();
		} else if (rtp->local_hash == AST_RTP_DTLS_HASH_SHA256) {
			type = EVP_sha256();
		} else {
			ast_log(LOG_ERROR, "Unsupported fingerprint hash type on RTP instance '%p'\n",
				instance);
			return -1;
		}

		if (!BIO_read_filename(certbio, dtls_cfg->certfile) ||
		    !(cert = PEM_read_bio_X509(certbio, NULL, 0, NULL)) ||
		    !X509_digest(cert, type, fingerprint, &size) ||
		    !size) {
			ast_log(LOG_ERROR, "Could not produce fingerprint from certificate '%s' for RTP instance '%p'\n",
				dtls_cfg->certfile, instance);
			BIO_free_all(certbio);
			return -1;
		}

		for (i = 0; i < size; i++) {
			sprintf(local_fingerprint, "%.2X:", (unsigned)fingerprint[i]);
			local_fingerprint += 3;
		}

		*(local_fingerprint-1) = 0;

		BIO_free_all(certbio);
	}

	if (!ast_strlen_zero(dtls_cfg->cipher)) {
		if (!SSL_CTX_set_cipher_list(rtp->ssl_ctx, dtls_cfg->cipher)) {
			ast_log(LOG_ERROR, "Invalid cipher specified in cipher list '%s' for RTP instance '%p'\n",
				dtls_cfg->cipher, instance);
			return -1;
		}
	}

	if (!ast_strlen_zero(dtls_cfg->cafile) || !ast_strlen_zero(dtls_cfg->capath)) {
		if (!SSL_CTX_load_verify_locations(rtp->ssl_ctx, S_OR(dtls_cfg->cafile, NULL), S_OR(dtls_cfg->capath, NULL))) {
			ast_log(LOG_ERROR, "Invalid certificate authority file '%s' or path '%s' specified for RTP instance '%p'\n",
				S_OR(dtls_cfg->cafile, ""), S_OR(dtls_cfg->capath, ""), instance);
			return -1;
		}
	}

	rtp->rekey = dtls_cfg->rekey;
	rtp->suite = dtls_cfg->suite;

	res = dtls_details_initialize(&rtp->dtls, rtp->ssl_ctx, dtls_cfg->default_setup);
	if (!res) {
		dtls_setup_rtcp(instance);
	}

	return res;
}

static int ast_rtp_dtls_active(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return !rtp->ssl_ctx ? 0 : 1;
}

static void ast_rtp_dtls_stop(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (rtp->ssl_ctx) {
		SSL_CTX_free(rtp->ssl_ctx);
		rtp->ssl_ctx = NULL;
	}

	if (rtp->dtls.ssl) {
		SSL_free(rtp->dtls.ssl);
		rtp->dtls.ssl = NULL;
	}

	if (rtp->rtcp && rtp->rtcp->dtls.ssl) {
		SSL_free(rtp->rtcp->dtls.ssl);
		rtp->rtcp->dtls.ssl = NULL;
	}
}

static void ast_rtp_dtls_reset(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (SSL_is_init_finished(rtp->dtls.ssl)) {
		SSL_shutdown(rtp->dtls.ssl);
		rtp->dtls.connection = AST_RTP_DTLS_CONNECTION_NEW;
	}

	if (rtp->rtcp && SSL_is_init_finished(rtp->rtcp->dtls.ssl)) {
		SSL_shutdown(rtp->rtcp->dtls.ssl);
		rtp->rtcp->dtls.connection = AST_RTP_DTLS_CONNECTION_NEW;
	}
}

static enum ast_rtp_dtls_connection ast_rtp_dtls_get_connection(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtp->dtls.connection;
}

static enum ast_rtp_dtls_setup ast_rtp_dtls_get_setup(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtp->dtls.dtls_setup;
}

static void dtls_set_setup(enum ast_rtp_dtls_setup *dtls_setup, enum ast_rtp_dtls_setup setup, SSL *ssl)
{
	enum ast_rtp_dtls_setup old = *dtls_setup;

	switch (setup) {
	case AST_RTP_DTLS_SETUP_ACTIVE:
		*dtls_setup = AST_RTP_DTLS_SETUP_PASSIVE;
		break;
	case AST_RTP_DTLS_SETUP_PASSIVE:
		*dtls_setup = AST_RTP_DTLS_SETUP_ACTIVE;
		break;
	case AST_RTP_DTLS_SETUP_ACTPASS:
		/* We can't respond to an actpass setup with actpass ourselves... so respond with active, as we can initiate connections */
		if (*dtls_setup == AST_RTP_DTLS_SETUP_ACTPASS) {
			*dtls_setup = AST_RTP_DTLS_SETUP_ACTIVE;
		}
		break;
	case AST_RTP_DTLS_SETUP_HOLDCONN:
		*dtls_setup = AST_RTP_DTLS_SETUP_HOLDCONN;
		break;
	default:
		/* This should never occur... if it does exit early as we don't know what state things are in */
		return;
	}

	/* If the setup state did not change we go on as if nothing happened */
	if (old == *dtls_setup) {
		return;
	}

	/* If they don't want us to establish a connection wait until later */
	if (*dtls_setup == AST_RTP_DTLS_SETUP_HOLDCONN) {
		return;
	}

	if (*dtls_setup == AST_RTP_DTLS_SETUP_ACTIVE) {
		SSL_set_connect_state(ssl);
	} else if (*dtls_setup == AST_RTP_DTLS_SETUP_PASSIVE) {
		SSL_set_accept_state(ssl);
	} else {
		return;
	}
}

static void ast_rtp_dtls_set_setup(struct ast_rtp_instance *instance, enum ast_rtp_dtls_setup setup)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (rtp->dtls.ssl) {
		dtls_set_setup(&rtp->dtls.dtls_setup, setup, rtp->dtls.ssl);
	}

	if (rtp->rtcp && rtp->rtcp->dtls.ssl) {
		dtls_set_setup(&rtp->rtcp->dtls.dtls_setup, setup, rtp->rtcp->dtls.ssl);
	}
}

static void ast_rtp_dtls_set_fingerprint(struct ast_rtp_instance *instance, enum ast_rtp_dtls_hash hash, const char *fingerprint)
{
	char *tmp = ast_strdupa(fingerprint), *value;
	int pos = 0;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (hash != AST_RTP_DTLS_HASH_SHA1 && hash != AST_RTP_DTLS_HASH_SHA256) {
		return;
	}

	rtp->remote_hash = hash;

	while ((value = strsep(&tmp, ":")) && (pos != (EVP_MAX_MD_SIZE - 1))) {
		sscanf(value, "%02x", (unsigned int*)&rtp->remote_fingerprint[pos++]);
	}
}

static enum ast_rtp_dtls_hash ast_rtp_dtls_get_fingerprint_hash(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtp->local_hash;
}

static const char *ast_rtp_dtls_get_fingerprint(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtp->local_fingerprint;
}

/* DTLS RTP Engine interface declaration */
static struct ast_rtp_engine_dtls ast_rtp_dtls = {
	.set_configuration = ast_rtp_dtls_set_configuration,
	.active = ast_rtp_dtls_active,
	.stop = ast_rtp_dtls_stop,
	.reset = ast_rtp_dtls_reset,
	.get_connection = ast_rtp_dtls_get_connection,
	.get_setup = ast_rtp_dtls_get_setup,
	.set_setup = ast_rtp_dtls_set_setup,
	.set_fingerprint = ast_rtp_dtls_set_fingerprint,
	.get_fingerprint_hash = ast_rtp_dtls_get_fingerprint_hash,
	.get_fingerprint = ast_rtp_dtls_get_fingerprint,
};

#endif

/* RTP Engine Declaration */
static struct ast_rtp_engine asterisk_rtp_engine = {
	.name = "asterisk",
	.new = ast_rtp_new,
	.destroy = ast_rtp_destroy,
	.dtmf_begin = ast_rtp_dtmf_begin,
	.dtmf_end = ast_rtp_dtmf_end,
	.dtmf_end_with_duration = ast_rtp_dtmf_end_with_duration,
	.dtmf_mode_set = ast_rtp_dtmf_mode_set,
	.dtmf_mode_get = ast_rtp_dtmf_mode_get,
	.update_source = ast_rtp_update_source,
	.change_source = ast_rtp_change_source,
	.write = ast_rtp_write,
	.read = ast_rtp_read,
	.prop_set = ast_rtp_prop_set,
	.fd = ast_rtp_fd,
	.remote_address_set = ast_rtp_remote_address_set,
	.alt_remote_address_set = ast_rtp_alt_remote_address_set,
	.red_init = rtp_red_init,
	.red_buffer = rtp_red_buffer,
	.local_bridge = ast_rtp_local_bridge,
	.get_stat = ast_rtp_get_stat,
	.dtmf_compatible = ast_rtp_dtmf_compatible,
	.stun_request = ast_rtp_stun_request,
	.stop = ast_rtp_stop,
	.qos = ast_rtp_qos_set,
	.sendcng = ast_rtp_sendcng,
#ifdef USE_PJPROJECT
	.ice = &ast_rtp_ice,
#endif
#ifdef HAVE_OPENSSL_SRTP
	.dtls = &ast_rtp_dtls,
	.activate = ast_rtp_activate,
#endif
};

static void rtp_learning_seq_init(struct rtp_learning_info *info, uint16_t seq);

#ifdef HAVE_OPENSSL_SRTP
static void dtls_perform_handshake(struct ast_rtp_instance *instance, struct dtls_details *dtls, int rtcp)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!dtls->ssl) {
		return;
	}

	if (SSL_is_init_finished(dtls->ssl)) {
		SSL_clear(dtls->ssl);
		if (dtls->dtls_setup == AST_RTP_DTLS_SETUP_PASSIVE) {
			SSL_set_accept_state(dtls->ssl);
		} else {
			SSL_set_connect_state(dtls->ssl);
		}
		dtls->connection = AST_RTP_DTLS_CONNECTION_NEW;
	}
	SSL_do_handshake(dtls->ssl);
	dtls_srtp_check_pending(instance, rtp, rtcp);
}
#endif

#ifdef USE_PJPROJECT
static void ast_rtp_on_ice_complete(pj_ice_sess *ice, pj_status_t status)
{
	struct ast_rtp_instance *instance = ice->user_data;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (status == PJ_SUCCESS) {
		struct ast_sockaddr remote_address;

		/* Symmetric RTP must be disabled for the remote address to not get overwritten */
		ast_rtp_instance_set_prop(instance, AST_RTP_PROPERTY_NAT, 0);

		update_address_with_ice_candidate(rtp, AST_RTP_ICE_COMPONENT_RTP, &remote_address);
		ast_rtp_instance_set_remote_address(instance, &remote_address);
		turn_enable_bind_channel(rtp, rtp->turn_rtp, AST_RTP_ICE_COMPONENT_RTP, TRANSPORT_TURN_RTP);

		if (rtp->rtcp) {
			update_address_with_ice_candidate(rtp, AST_RTP_ICE_COMPONENT_RTCP, &rtp->rtcp->them);
			turn_enable_bind_channel(rtp, rtp->turn_rtcp, AST_RTP_ICE_COMPONENT_RTCP, TRANSPORT_TURN_RTCP);
		}
	}

#ifdef HAVE_OPENSSL_SRTP
	dtls_perform_handshake(instance, &rtp->dtls, 0);

	if (rtp->rtcp) {
		dtls_perform_handshake(instance, &rtp->rtcp->dtls, 1);
	}
#endif

	if (!strictrtp) {
		return;
	}

	rtp->strict_rtp_state = STRICT_RTP_LEARN;
	rtp_learning_seq_init(&rtp->rtp_source_learn, (uint16_t)rtp->seqno);
}

static void ast_rtp_on_ice_rx_data(pj_ice_sess *ice, unsigned comp_id, unsigned transport_id, void *pkt, pj_size_t size, const pj_sockaddr_t *src_addr, unsigned src_addr_len)
{
	struct ast_rtp_instance *instance = ice->user_data;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* Instead of handling the packet here (which really doesn't work with our architecture) we set a bit to indicate that it should be handled after pj_ice_sess_on_rx_pkt
	 * returns */
	if (transport_id == TRANSPORT_SOCKET_RTP || transport_id == TRANSPORT_SOCKET_RTCP) {
		rtp->passthrough = 1;
	} else if (transport_id == TRANSPORT_TURN_RTP) {
		rtp->rtp_passthrough = 1;
	} else if (transport_id == TRANSPORT_TURN_RTCP) {
		rtp->rtcp_passthrough = 1;
	}
}

static pj_status_t ast_rtp_on_ice_tx_pkt(pj_ice_sess *ice, unsigned comp_id, unsigned transport_id, const void *pkt, pj_size_t size, const pj_sockaddr_t *dst_addr, unsigned dst_addr_len)
{
	struct ast_rtp_instance *instance = ice->user_data;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	pj_status_t status = PJ_EINVALIDOP;
	pj_ssize_t _size = (pj_ssize_t)size;

	if (transport_id == TRANSPORT_SOCKET_RTP) {
		/* Traffic is destined to go right out the RTP socket we already have */
		status = pj_sock_sendto(rtp->s, pkt, &_size, 0, dst_addr, dst_addr_len);
		/* sendto on a connectionless socket should send all the data, or none at all */
		ast_assert(_size == size || status != PJ_SUCCESS);
	} else if (transport_id == TRANSPORT_SOCKET_RTCP) {
		/* Traffic is destined to go right out the RTCP socket we already have */
		if (rtp->rtcp) {
			status = pj_sock_sendto(rtp->rtcp->s, pkt, &_size, 0, dst_addr, dst_addr_len);
			/* sendto on a connectionless socket should send all the data, or none at all */
			ast_assert(_size == size || status != PJ_SUCCESS);
		} else {
			status = PJ_SUCCESS;
		}
	} else if (transport_id == TRANSPORT_TURN_RTP) {
		/* Traffic is going through the RTP TURN relay */
		if (rtp->turn_rtp) {
			status = pj_turn_sock_sendto(rtp->turn_rtp, pkt, size, dst_addr, dst_addr_len);
		}
	} else if (transport_id == TRANSPORT_TURN_RTCP) {
		/* Traffic is going through the RTCP TURN relay */
		if (rtp->turn_rtcp) {
			status = pj_turn_sock_sendto(rtp->turn_rtcp, pkt, size, dst_addr, dst_addr_len);
		}
	}

	return status;
}

/* ICE Session interface declaration */
static pj_ice_sess_cb ast_rtp_ice_sess_cb = {
	.on_ice_complete = ast_rtp_on_ice_complete,
	.on_rx_data = ast_rtp_on_ice_rx_data,
	.on_tx_pkt = ast_rtp_on_ice_tx_pkt,
};

/*! \brief Worker thread for timerheap */
static int timer_worker_thread(void *data)
{
	pj_ioqueue_t *ioqueue;

	if (pj_ioqueue_create(pool, 1, &ioqueue) != PJ_SUCCESS) {
		return -1;
	}

	while (!timer_terminate) {
		const pj_time_val delay = {0, 10};

		pj_timer_heap_poll(timer_heap, NULL);
		pj_ioqueue_poll(ioqueue, &delay);
	}

	return 0;
}
#endif

static inline int rtp_debug_test_addr(struct ast_sockaddr *addr)
{
	if (!rtpdebug) {
		return 0;
	}
	if (!ast_sockaddr_isnull(&rtpdebugaddr)) {
		if (rtpdebugport) {
			return (ast_sockaddr_cmp(&rtpdebugaddr, addr) == 0); /* look for RTP packets from IP+Port */
		} else {
			return (ast_sockaddr_cmp_addr(&rtpdebugaddr, addr) == 0); /* only look for RTP packets from IP */
		}
	}

	return 1;
}

static inline int rtcp_debug_test_addr(struct ast_sockaddr *addr)
{
	if (!rtcpdebug) {
		return 0;
	}
	if (!ast_sockaddr_isnull(&rtcpdebugaddr)) {
		if (rtcpdebugport) {
			return (ast_sockaddr_cmp(&rtcpdebugaddr, addr) == 0); /* look for RTCP packets from IP+Port */
		} else {
			return (ast_sockaddr_cmp_addr(&rtcpdebugaddr, addr) == 0); /* only look for RTCP packets from IP */
		}
	}

	return 1;
}

#ifdef HAVE_OPENSSL_SRTP

static int dtls_srtp_handle_timeout(const void *data)
{
	struct ast_rtp_instance *instance = (struct ast_rtp_instance *)data;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (!rtp)
	{
		return 0;
	}

	ast_mutex_lock(&rtp->dtls_timer_lock);
	if (rtp->dtlstimerid == -1)
	{
		ast_mutex_unlock(&rtp->dtls_timer_lock);
		ao2_ref(instance, -1);
		return 0;
	}

	rtp->dtlstimerid = -1;
	ast_mutex_unlock(&rtp->dtls_timer_lock);

	if (rtp->dtls.ssl && !SSL_is_init_finished(rtp->dtls.ssl)) {
		DTLSv1_handle_timeout(rtp->dtls.ssl);
	}
	dtls_srtp_check_pending(instance, rtp, 0);

	if (rtp->rtcp && rtp->rtcp->dtls.ssl && !SSL_is_init_finished(rtp->rtcp->dtls.ssl)) {
		DTLSv1_handle_timeout(rtp->rtcp->dtls.ssl);
	}
	dtls_srtp_check_pending(instance, rtp, 1);

	ao2_ref(instance, -1);

	return 0;
}

static void dtls_srtp_check_pending(struct ast_rtp_instance *instance, struct ast_rtp *rtp, int rtcp)
{
	struct dtls_details *dtls = !rtcp ? &rtp->dtls : &rtp->rtcp->dtls;
	size_t pending;
	struct timeval dtls_timeout; /* timeout on DTLS  */

	if (!dtls->ssl || !dtls->write_bio) {
		return;
	}

	pending = BIO_ctrl_pending(dtls->write_bio);

	if (pending > 0) {
		char outgoing[pending];
		size_t out;
		struct ast_sockaddr remote_address = { {0, } };
		int ice;

		if (!rtcp) {
			ast_rtp_instance_get_remote_address(instance, &remote_address);
		} else {
			ast_sockaddr_copy(&remote_address, &rtp->rtcp->them);
		}

		/* If we do not yet know an address to send this to defer it until we do */
		if (ast_sockaddr_isnull(&remote_address)) {
			return;
		}

		out = BIO_read(dtls->write_bio, outgoing, sizeof(outgoing));

		/* Stop existing DTLS timer if running */
		ast_mutex_lock(&rtp->dtls_timer_lock);
		if (rtp->dtlstimerid > -1) {
			AST_SCHED_DEL_UNREF(rtp->sched, rtp->dtlstimerid, ao2_ref(instance, -1));
			rtp->dtlstimerid = -1;
		}

		if (DTLSv1_get_timeout(dtls->ssl, &dtls_timeout)) {
			int timeout = dtls_timeout.tv_sec * 1000 + dtls_timeout.tv_usec / 1000;
			ao2_ref(instance, +1);
			if ((rtp->dtlstimerid = ast_sched_add(rtp->sched, timeout, dtls_srtp_handle_timeout, instance)) < 0) {
				ao2_ref(instance, -1);
				ast_log(LOG_WARNING, "scheduling DTLS retransmission for RTP instance [%p] failed.\n", instance);
			}
		}
		ast_mutex_unlock(&rtp->dtls_timer_lock);

		__rtp_sendto(instance, outgoing, out, 0, &remote_address, rtcp, &ice, 0);
	}
}

static int dtls_srtp_renegotiate(const void *data)
{
	struct ast_rtp_instance *instance = (struct ast_rtp_instance *)data;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	SSL_renegotiate(rtp->dtls.ssl);
	SSL_do_handshake(rtp->dtls.ssl);
	dtls_srtp_check_pending(instance, rtp, 0);

	if (rtp->rtcp && rtp->rtcp->dtls.ssl) {
		SSL_renegotiate(rtp->rtcp->dtls.ssl);
		SSL_do_handshake(rtp->rtcp->dtls.ssl);
		dtls_srtp_check_pending(instance, rtp, 1);
	}

	rtp->rekeyid = -1;
	ao2_ref(instance, -1);

	return 0;
}

static int dtls_srtp_setup(struct ast_rtp *rtp, struct ast_srtp *srtp, struct ast_rtp_instance *instance)
{
	unsigned char material[SRTP_MASTER_LEN * 2];
	unsigned char *local_key, *local_salt, *remote_key, *remote_salt;
	struct ast_srtp_policy *local_policy, *remote_policy = NULL;
	struct ast_rtp_instance_stats stats = { 0, };

	/* If a fingerprint is present in the SDP make sure that the peer certificate matches it */
	if (rtp->dtls_verify & AST_RTP_DTLS_VERIFY_FINGERPRINT) {
		X509 *certificate;

		if (!(certificate = SSL_get_peer_certificate(rtp->dtls.ssl))) {
			ast_log(LOG_WARNING, "No certificate was provided by the peer on RTP instance '%p'\n", instance);
			return -1;
		}

		/* If a fingerprint is present in the SDP make sure that the peer certificate matches it */
		if (rtp->remote_fingerprint[0]) {
			const EVP_MD *type;
			unsigned char fingerprint[EVP_MAX_MD_SIZE];
			unsigned int size;

			if (rtp->remote_hash == AST_RTP_DTLS_HASH_SHA1) {
				type = EVP_sha1();
			} else if (rtp->remote_hash == AST_RTP_DTLS_HASH_SHA256) {
				type = EVP_sha256();
			} else {
				ast_log(LOG_WARNING, "Unsupported fingerprint hash type on RTP instance '%p'\n", instance);
				return -1;
			}

			if (!X509_digest(certificate, type, fingerprint, &size) ||
			    !size ||
			    memcmp(fingerprint, rtp->remote_fingerprint, size)) {
				X509_free(certificate);
				ast_log(LOG_WARNING, "Fingerprint provided by remote party does not match that of peer certificate on RTP instance '%p'\n",
					instance);
				return -1;
			}
		}

		X509_free(certificate);
	}

	/* Ensure that certificate verification was successful */
	if ((rtp->dtls_verify & AST_RTP_DTLS_VERIFY_CERTIFICATE) && SSL_get_verify_result(rtp->dtls.ssl) != X509_V_OK) {
		ast_log(LOG_WARNING, "Peer certificate on RTP instance '%p' failed verification test\n",
			instance);
		return -1;
	}

	/* Produce key information and set up SRTP */
	if (!SSL_export_keying_material(rtp->dtls.ssl, material, SRTP_MASTER_LEN * 2, "EXTRACTOR-dtls_srtp", 19, NULL, 0, 0)) {
		ast_log(LOG_WARNING, "Unable to extract SRTP keying material from DTLS-SRTP negotiation on RTP instance '%p'\n",
			instance);
		return -1;
	}

	/* Whether we are acting as a server or client determines where the keys/salts are */
	if (rtp->dtls.dtls_setup == AST_RTP_DTLS_SETUP_ACTIVE) {
		local_key = material;
		remote_key = local_key + SRTP_MASTER_KEY_LEN;
		local_salt = remote_key + SRTP_MASTER_KEY_LEN;
		remote_salt = local_salt + SRTP_MASTER_SALT_LEN;
	} else {
		remote_key = material;
		local_key = remote_key + SRTP_MASTER_KEY_LEN;
		remote_salt = local_key + SRTP_MASTER_KEY_LEN;
		local_salt = remote_salt + SRTP_MASTER_SALT_LEN;
	}

	if (!(local_policy = res_srtp_policy->alloc())) {
		return -1;
	}

	if (res_srtp_policy->set_master_key(local_policy, local_key, SRTP_MASTER_KEY_LEN, local_salt, SRTP_MASTER_SALT_LEN) < 0) {
		ast_log(LOG_WARNING, "Could not set key/salt information on local policy of '%p' when setting up DTLS-SRTP\n", rtp);
		goto error;
	}

	if (res_srtp_policy->set_suite(local_policy, rtp->suite)) {
		ast_log(LOG_WARNING, "Could not set suite to '%u' on local policy of '%p' when setting up DTLS-SRTP\n", rtp->suite, rtp);
		goto error;
	}

	if (ast_rtp_instance_get_stats(instance, &stats, AST_RTP_INSTANCE_STAT_LOCAL_SSRC)) {
		goto error;
	}

	res_srtp_policy->set_ssrc(local_policy, stats.local_ssrc, 0);

	if (!(remote_policy = res_srtp_policy->alloc())) {
		goto error;
	}

	if (res_srtp_policy->set_master_key(remote_policy, remote_key, SRTP_MASTER_KEY_LEN, remote_salt, SRTP_MASTER_SALT_LEN) < 0) {
		ast_log(LOG_WARNING, "Could not set key/salt information on remote policy of '%p' when setting up DTLS-SRTP\n", rtp);
		goto error;
	}

	if (res_srtp_policy->set_suite(remote_policy, rtp->suite)) {
		ast_log(LOG_WARNING, "Could not set suite to '%u' on remote policy of '%p' when setting up DTLS-SRTP\n", rtp->suite, rtp);
		goto error;
	}

	res_srtp_policy->set_ssrc(remote_policy, 0, 1);

	if (ast_rtp_instance_add_srtp_policy(instance, remote_policy, local_policy)) {
		ast_log(LOG_WARNING, "Could not set policies when setting up DTLS-SRTP on '%p'\n", rtp);
		goto error;
	}

	if (rtp->rekey) {
		ao2_ref(instance, +1);
		if ((rtp->rekeyid = ast_sched_add(rtp->sched, rtp->rekey * 1000, dtls_srtp_renegotiate, instance)) < 0) {
			ao2_ref(instance, -1);
			goto error;
		}
	}

	return 0;

error:
	res_srtp_policy->destroy(local_policy);

	if (remote_policy) {
		res_srtp_policy->destroy(remote_policy);
	}

	return -1;
}
#endif

static int __rtp_recvfrom(struct ast_rtp_instance *instance, void *buf, size_t size, int flags, struct ast_sockaddr *sa, int rtcp)
{
	int len;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_srtp *srtp = ast_rtp_instance_get_srtp(instance);
	char *in = buf;
#ifdef USE_PJPROJECT
	struct ast_sockaddr *loop = rtcp ? &rtp->rtcp_loop : &rtp->rtp_loop;
#endif

	if ((len = ast_recvfrom(rtcp ? rtp->rtcp->s : rtp->s, buf, size, flags, sa)) < 0) {
	   return len;
	}

#ifdef HAVE_OPENSSL_SRTP
	dtls_srtp_check_pending(instance, rtp, rtcp);

	/* If this is an SSL packet pass it to OpenSSL for processing */
	if ((*in >= 20) && (*in <= 64)) {
		struct dtls_details *dtls = !rtcp ? &rtp->dtls : &rtp->rtcp->dtls;
		int res = 0;

		/* If no SSL session actually exists terminate things */
		if (!dtls->ssl) {
			ast_log(LOG_ERROR, "Received SSL traffic on RTP instance '%p' without an SSL session\n",
				instance);
			return -1;
		}

		/* If we don't yet know if we are active or passive and we receive a packet... we are obviously passive */
		if (dtls->dtls_setup == AST_RTP_DTLS_SETUP_ACTPASS) {
			dtls->dtls_setup = AST_RTP_DTLS_SETUP_PASSIVE;
			SSL_set_accept_state(dtls->ssl);
		}

		dtls_srtp_check_pending(instance, rtp, rtcp);

		BIO_write(dtls->read_bio, buf, len);

		len = SSL_read(dtls->ssl, buf, len);

		if ((len < 0) && (SSL_get_error(dtls->ssl, len) == SSL_ERROR_SSL)) {
			unsigned long error = ERR_get_error();
			ast_log(LOG_ERROR, "DTLS failure occurred on RTP instance '%p' due to reason '%s', terminating\n",
				instance, ERR_reason_error_string(error));
			return -1;
		}

		dtls_srtp_check_pending(instance, rtp, rtcp);

		if (SSL_is_init_finished(dtls->ssl)) {
			/* Any further connections will be existing since this is now established */
			dtls->connection = AST_RTP_DTLS_CONNECTION_EXISTING;
			if (!rtcp) {
				/* Use the keying material to set up key/salt information */
				res = dtls_srtp_setup(rtp, srtp, instance);
			}
		}

		return res;
	}
#endif

#ifdef USE_PJPROJECT
	if (!ast_sockaddr_isnull(loop) && !ast_sockaddr_cmp(loop, sa)) {
		/* ICE traffic will have been handled in the TURN callback, so skip it but update the address
		 * so it reflects the actual source and not the loopback
		 */
		if (rtcp) {
			ast_sockaddr_copy(sa, &rtp->rtcp->them);
		} else {
			ast_rtp_instance_get_remote_address(instance, sa);
		}
	} else if (rtp->ice) {
		pj_str_t combined = pj_str(ast_sockaddr_stringify(sa));
		pj_sockaddr address;
		pj_status_t status;

		pj_thread_register_check();

		pj_sockaddr_parse(pj_AF_UNSPEC(), 0, &combined, &address);

		status = pj_ice_sess_on_rx_pkt(rtp->ice, rtcp ? AST_RTP_ICE_COMPONENT_RTCP : AST_RTP_ICE_COMPONENT_RTP,
			rtcp ? TRANSPORT_SOCKET_RTCP : TRANSPORT_SOCKET_RTP, buf, len, &address,
			pj_sockaddr_get_len(&address));
		if (status != PJ_SUCCESS) {
			char buf[100];

			pj_strerror(status, buf, sizeof(buf));
			ast_log(LOG_WARNING, "PJ ICE Rx error status code: %d '%s'.\n",
				(int)status, buf);
			return -1;
		}
		if (!rtp->passthrough) {
			return 0;
		}
		rtp->passthrough = 0;
	}
#endif

	if ((*in & 0xC0) && res_srtp && srtp && res_srtp->unprotect(srtp, buf, &len, rtcp) < 0) {
	   return -1;
	}

	return len;
}

static int rtcp_recvfrom(struct ast_rtp_instance *instance, void *buf, size_t size, int flags, struct ast_sockaddr *sa)
{
	return __rtp_recvfrom(instance, buf, size, flags, sa, 1);
}

static int rtp_recvfrom(struct ast_rtp_instance *instance, void *buf, size_t size, int flags, struct ast_sockaddr *sa)
{
	return __rtp_recvfrom(instance, buf, size, flags, sa, 0);
}

static int __rtp_sendto(struct ast_rtp_instance *instance, void *buf, size_t size, int flags, struct ast_sockaddr *sa, int rtcp, int *ice, int use_srtp)
{
	int len = size;
	void *temp = buf;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_srtp *srtp = ast_rtp_instance_get_srtp(instance);

	*ice = 0;

	if (use_srtp && res_srtp && srtp && res_srtp->protect(srtp, &temp, &len, rtcp) < 0) {
		return -1;
	}

#ifdef USE_PJPROJECT
	if (rtp->ice) {
		pj_thread_register_check();

		if (pj_ice_sess_send_data(rtp->ice, rtcp ? AST_RTP_ICE_COMPONENT_RTCP : AST_RTP_ICE_COMPONENT_RTP, temp, len) == PJ_SUCCESS) {
			*ice = 1;
			return len;
		}
	}
#endif

	return ast_sendto(rtcp ? rtp->rtcp->s : rtp->s, temp, len, flags, sa);
}

static int rtcp_sendto(struct ast_rtp_instance *instance, void *buf, size_t size, int flags, struct ast_sockaddr *sa, int *ice)
{
	return __rtp_sendto(instance, buf, size, flags, sa, 1, ice, 1);
}

static int rtp_sendto(struct ast_rtp_instance *instance, void *buf, size_t size, int flags, struct ast_sockaddr *sa, int *ice)
{
	return __rtp_sendto(instance, buf, size, flags, sa, 0, ice, 1);
}

static int rtp_get_rate(struct ast_format *format)
{
	return (format->id == AST_FORMAT_G722) ? 8000 : ast_format_rate(format);
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

static int create_new_socket(const char *type, int af)
{
	int sock = socket(af, SOCK_DGRAM, 0);

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

/*!
 * \internal
 * \brief Initializes sequence values and probation for learning mode.
 * \note This is an adaptation of pjmedia's pjmedia_rtp_seq_init function.
 *
 * \param info The learning information to track
 * \param seq sequence number read from the rtp header to initialize the information with
 */
static void rtp_learning_seq_init(struct rtp_learning_info *info, uint16_t seq)
{
	info->max_seq = seq - 1;
	info->packets = learning_min_sequential;
}

/*!
 * \internal
 * \brief Updates sequence information for learning mode and determines if probation/learning mode should remain in effect.
 * \note This function was adapted from pjmedia's pjmedia_rtp_seq_update function.
 *
 * \param info Structure tracking the learning progress of some address
 * \param seq sequence number read from the rtp header
 * \retval 0 if probation mode should exit for this address
 * \retval non-zero if probation mode should continue
 */
static int rtp_learning_rtp_seq_update(struct rtp_learning_info *info, uint16_t seq)
{
	if (seq == info->max_seq + 1) {
		/* packet is in sequence */
		info->packets--;
	} else {
		/* Sequence discontinuity; reset */
		info->packets = learning_min_sequential - 1;
	}
	info->max_seq = seq;

	return (info->packets == 0);
}

#ifdef USE_PJPROJECT
static void rtp_add_candidates_to_ice(struct ast_rtp_instance *instance, struct ast_rtp *rtp, struct ast_sockaddr *addr, int port, int component,
				      int transport)
{
	pj_sockaddr address[16];
	unsigned int count = PJ_ARRAY_SIZE(address), pos = 0;

	/* Add all the local interface IP addresses */
	if (ast_sockaddr_is_ipv4(addr)) {
		pj_enum_ip_interface(pj_AF_INET(), &count, address);
	} else if (ast_sockaddr_is_any(addr)) {
		pj_enum_ip_interface(pj_AF_UNSPEC(), &count, address);
	} else {
		pj_enum_ip_interface(pj_AF_INET6(), &count, address);
	}

	for (pos = 0; pos < count; pos++) {
		pj_sockaddr_set_port(&address[pos], port);
		ast_rtp_ice_add_cand(rtp, component, transport, PJ_ICE_CAND_TYPE_HOST, 65535, &address[pos], &address[pos], NULL,
				     pj_sockaddr_get_len(&address[pos]));
	}

	/* If configured to use a STUN server to get our external mapped address do so */
	if (stunaddr.sin_addr.s_addr && ast_sockaddr_is_ipv4(addr) && count) {
		struct sockaddr_in answer;

		if (!ast_stun_request(component == AST_RTP_ICE_COMPONENT_RTCP ? rtp->rtcp->s : rtp->s, &stunaddr, NULL, &answer)) {
			pj_sockaddr base;
			pj_str_t mapped = pj_str(ast_strdupa(ast_inet_ntoa(answer.sin_addr)));

			/* Use the first local host candidate as the base */
			pj_sockaddr_cp(&base, &address[0]);

			pj_sockaddr_init(pj_AF_INET(), &address[0], &mapped, ntohs(answer.sin_port));

			ast_rtp_ice_add_cand(rtp, component, transport, PJ_ICE_CAND_TYPE_SRFLX, 65535, &address[0], &base,
					     &base, pj_sockaddr_get_len(&address[0]));
		}
	}

	/* If configured to use a TURN relay create a session and allocate */
	if (pj_strlen(&turnaddr)) {
		ast_rtp_ice_turn_request(instance, component, PJ_TURN_TP_TCP, pj_strbuf(&turnaddr), turnport,
			pj_strbuf(&turnusername), pj_strbuf(&turnpassword));
	}
}
#endif

/*!
 * \internal
 * \brief Calculates the elapsed time from issue of the first tx packet in an
 *        rtp session and a specified time
 *
 * \param rtp pointer to the rtp struct with the transmitted rtp packet
 * \param delivery time of delivery - if NULL or zero value, will be ast_tvnow()
 *
 * \return time elapsed in milliseconds
 */
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

#ifdef USE_PJPROJECT
/*!
 * \internal
 * \brief Creates an ICE session. Can be used to replace a destroyed ICE session.
 *
 * \param instance RTP instance for which the ICE session is being replaced
 * \param addr ast_sockaddr to use for adding RTP candidates to the ICE session
 * \param port port to use for adding RTP candidates to the ICE session
 * \param replace 0 when creating a new session, 1 when replacing a destroyed session
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int ice_create(struct ast_rtp_instance *instance, struct ast_sockaddr *addr,
	int port, int replace)
{
	pj_stun_config stun_config;
	pj_str_t ufrag, passwd;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	ao2_cleanup(rtp->ice_local_candidates);
	rtp->ice_local_candidates = NULL;

	pj_thread_register_check();

	pj_stun_config_init(&stun_config, &cachingpool.factory, 0, NULL, timer_heap);

	ufrag = pj_str(rtp->local_ufrag);
	passwd = pj_str(rtp->local_passwd);

	/* Create an ICE session for ICE negotiation */
	if (pj_ice_sess_create(&stun_config, NULL, PJ_ICE_SESS_ROLE_UNKNOWN, 2,
			&ast_rtp_ice_sess_cb, &ufrag, &passwd, &rtp->ice) == PJ_SUCCESS) {
		/* Make this available for the callbacks */
		rtp->ice->user_data = instance;

		/* Add all of the available candidates to the ICE session */
		rtp_add_candidates_to_ice(instance, rtp, addr, port, AST_RTP_ICE_COMPONENT_RTP,
			TRANSPORT_SOCKET_RTP);

		/* Only add the RTCP candidates to ICE when replacing the session. New sessions
		 * handle this in a separate part of the setup phase */
		if (replace && rtp->rtcp) {
			rtp_add_candidates_to_ice(instance, rtp, &rtp->rtcp->us,
				ast_sockaddr_port(&rtp->rtcp->us), AST_RTP_ICE_COMPONENT_RTCP,
				TRANSPORT_SOCKET_RTCP);
		}

		return 0;
	}

	return -1;

}
#endif

static int ast_rtp_new(struct ast_rtp_instance *instance,
		       struct ast_sched_context *sched, struct ast_sockaddr *addr,
		       void *data)
{
	struct ast_rtp *rtp = NULL;
	int x, startplace;

	/* Create a new RTP structure to hold all of our data */
	if (!(rtp = ast_calloc(1, sizeof(*rtp)))) {
		return -1;
	}

#ifdef USE_PJPROJECT
	/* Initialize synchronization aspects */
	ast_mutex_init(&rtp->lock);
	ast_cond_init(&rtp->cond, NULL);
#endif

	/* Set default parameters on the newly created RTP structure */
	rtp->ssrc = ast_random();
	rtp->seqno = ast_random() & 0xffff;
	rtp->strict_rtp_state = (strictrtp ? STRICT_RTP_LEARN : STRICT_RTP_OPEN);
	if (strictrtp) {
		rtp_learning_seq_init(&rtp->rtp_source_learn, (uint16_t)rtp->seqno);
		rtp_learning_seq_init(&rtp->alt_source_learn, (uint16_t)rtp->seqno);
	}

	/* Create a new socket for us to listen on and use */
	if ((rtp->s =
	     create_new_socket("RTP",
			       ast_sockaddr_is_ipv4(addr) ? AF_INET  :
			       ast_sockaddr_is_ipv6(addr) ? AF_INET6 : -1)) < 0) {
		ast_debug(1, "Failed to create a new socket for RTP instance '%p'\n", instance);
		ast_free(rtp);
		return -1;
	}

	/* Now actually find a free RTP port to use */
	x = (rtpend == rtpstart) ? rtpstart : (ast_random() % (rtpend - rtpstart)) + rtpstart;
	x = x & ~1;
	startplace = x;

	for (;;) {
		ast_sockaddr_set_port(addr, x);
		/* Try to bind, this will tell us whether the port is available or not */
		if (!ast_bind(rtp->s, addr)) {
			ast_debug(1, "Allocated port %d for RTP instance '%p'\n", x, instance);
			ast_rtp_instance_set_local_address(instance, addr);
			break;
		}

		x += 2;
		if (x > rtpend) {
			x = (rtpstart + 1) & ~1;
		}

		/* See if we ran out of ports or if the bind actually failed because of something other than the address being in use */
		if (x == startplace || (errno != EADDRINUSE && errno != EACCES)) {
			ast_log(LOG_ERROR, "Oh dear... we couldn't allocate a port for RTP instance '%p'\n", instance);
			close(rtp->s);
			ast_free(rtp);
			return -1;
		}
	}

	ast_rtp_instance_set_data(instance, rtp);

#ifdef USE_PJPROJECT
	generate_random_string(rtp->local_ufrag, sizeof(rtp->local_ufrag));
	generate_random_string(rtp->local_passwd, sizeof(rtp->local_passwd));

	/* Create an ICE session for ICE negotiation */
	if (icesupport) {
		if (ice_create(instance, addr, x, 0)) {
			ast_log(LOG_NOTICE, "Failed to start ICE session\n");
		} else {
			rtp->ice_port = x;
			ast_sockaddr_copy(&rtp->ice_original_rtp_addr, addr);
		}
	}
#endif

	/* Record any information we may need */
	rtp->sched = sched;

#ifdef HAVE_OPENSSL_SRTP
	rtp->rekeyid = -1;
	rtp->dtlstimerid = -1;
#endif

	return 0;
}

static int ast_rtp_destroy(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
#ifdef USE_PJPROJECT
	struct timeval wait = ast_tvadd(ast_tvnow(), ast_samp2tv(TURN_STATE_WAIT_TIME, 1000));
	struct timespec ts = { .tv_sec = wait.tv_sec, .tv_nsec = wait.tv_usec * 1000, };
#endif

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
		/*
		 * It is not possible for there to be an active RTCP scheduler
		 * entry at this point since it holds a reference to the
		 * RTP instance while it's active.
		 */
		close(rtp->rtcp->s);
#ifdef HAVE_OPENSSL_SRTP
		if (rtp->rtcp->dtls.ssl) {
			SSL_free(rtp->rtcp->dtls.ssl);
		}
#endif
		ast_free(rtp->rtcp);
	}

	/* Destroy RED if it was being used */
	if (rtp->red) {
		AST_SCHED_DEL(rtp->sched, rtp->red->schedid);
		ast_free(rtp->red);
	}

#ifdef USE_PJPROJECT
	pj_thread_register_check();

	/* Destroy the RTP TURN relay if being used */
	ast_mutex_lock(&rtp->lock);
	if (rtp->turn_rtp) {
		pj_turn_sock_destroy(rtp->turn_rtp);
		rtp->turn_state = PJ_TURN_STATE_NULL;
		while (rtp->turn_state != PJ_TURN_STATE_DESTROYING) {
			ast_cond_timedwait(&rtp->cond, &rtp->lock, &ts);
		}
	}

	/* Destroy the RTCP TURN relay if being used */
	if (rtp->turn_rtcp) {
		pj_turn_sock_destroy(rtp->turn_rtcp);
		rtp->turn_state = PJ_TURN_STATE_NULL;
		while (rtp->turn_state != PJ_TURN_STATE_DESTROYING) {
			ast_cond_timedwait(&rtp->cond, &rtp->lock, &ts);
		}
	}
	ast_mutex_unlock(&rtp->lock);

	if (rtp->ioqueue) {
		rtp_ioqueue_thread_remove(rtp->ioqueue);
	}

	/* Destroy the ICE session if being used */
	if (rtp->ice) {
		pj_ice_sess_destroy(rtp->ice);
	}

	/* Destroy any candidates */
	if (rtp->ice_local_candidates) {
		ao2_ref(rtp->ice_local_candidates, -1);
	}

	if (rtp->ice_active_remote_candidates) {
		ao2_ref(rtp->ice_active_remote_candidates, -1);
	}

	/* Destroy synchronization items */
	ast_mutex_destroy(&rtp->lock);
	ast_cond_destroy(&rtp->cond);
#endif

#ifdef HAVE_OPENSSL_SRTP
	/* Destroy the SSL context if present */
	if (rtp->ssl_ctx) {
		SSL_CTX_free(rtp->ssl_ctx);
	}

	/* Destroy the SSL session if present */
	if (rtp->dtls.ssl) {
		SSL_free(rtp->dtls.ssl);
	}
#endif

	/* Finally destroy ourselves */
	ast_free(rtp);

	return 0;
}

static int ast_rtp_dtmf_mode_set(struct ast_rtp_instance *instance, enum ast_rtp_dtmf_mode dtmf_mode)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	rtp->dtmfmode = dtmf_mode;
	return 0;
}

static enum ast_rtp_dtmf_mode ast_rtp_dtmf_mode_get(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	return rtp->dtmfmode;
}

static int ast_rtp_dtmf_begin(struct ast_rtp_instance *instance, char digit)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr remote_address = { {0,} };
	int hdrlen = 12, res = 0, i = 0, payload = 101;
	char data[256];
	unsigned int *rtpheader = (unsigned int*)data;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* If we have no remote address information bail out now */
	if (ast_sockaddr_isnull(&remote_address)) {
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
	payload = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance), 0, NULL, AST_RTP_DTMF);

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));
	rtp->send_duration = 160;
	rtp->lastts += calc_txstamp(rtp, NULL) * DTMF_SAMPLE_RATE_MS;
	rtp->lastdigitts = rtp->lastts + rtp->send_duration;

	/* Create the actual packet that we will be sending */
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc);

	/* Actually send the packet */
	for (i = 0; i < 2; i++) {
		int ice;

		rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (rtp->send_duration));
		res = rtp_sendto(instance, (void *) rtpheader, hdrlen + 4, 0, &remote_address, &ice);
		if (res < 0) {
			ast_log(LOG_ERROR, "RTP Transmission error to %s: %s\n",
				ast_sockaddr_stringify(&remote_address),
				strerror(errno));
		}
		if (rtp_debug_test_addr(&remote_address)) {
			ast_verbose("Sent RTP DTMF packet to %s%s (type %-2.2d, seq %-6.6d, ts %-6.6u, len %-6.6d)\n",
				    ast_sockaddr_stringify(&remote_address),
				    ice ? " (via ICE)" : "",
				    payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
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
	struct ast_sockaddr remote_address = { {0,} };
	int hdrlen = 12, res = 0;
	char data[256];
	unsigned int *rtpheader = (unsigned int*)data;
	int ice;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* Make sure we know where the other side is so we can send them the packet */
	if (ast_sockaddr_isnull(&remote_address)) {
		return -1;
	}

	/* Actually create the packet we will be sending */
	rtpheader[0] = htonl((2 << 30) | (rtp->send_payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc);
	rtpheader[3] = htonl((rtp->send_digit << 24) | (0xa << 16) | (rtp->send_duration));

	/* Boom, send it on out */
	res = rtp_sendto(instance, (void *) rtpheader, hdrlen + 4, 0, &remote_address, &ice);
	if (res < 0) {
		ast_log(LOG_ERROR, "RTP Transmission error to %s: %s\n",
			ast_sockaddr_stringify(&remote_address),
			strerror(errno));
	}

	if (rtp_debug_test_addr(&remote_address)) {
		ast_verbose("Sent RTP DTMF packet to %s%s (type %-2.2d, seq %-6.6d, ts %-6.6u, len %-6.6d)\n",
			    ast_sockaddr_stringify(&remote_address),
			    ice ? " (via ICE)" : "",
			    rtp->send_payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
	}

	/* And now we increment some values for the next time we swing by */
	rtp->seqno++;
	rtp->send_duration += 160;
	rtp->lastts += calc_txstamp(rtp, NULL) * DTMF_SAMPLE_RATE_MS;

	return 0;
}

static int ast_rtp_dtmf_end_with_duration(struct ast_rtp_instance *instance, char digit, unsigned int duration)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr remote_address = { {0,} };
	int hdrlen = 12, res = -1, i = 0;
	char data[256];
	unsigned int *rtpheader = (unsigned int*)data;
	unsigned int measured_samples;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* Make sure we know where the remote side is so we can send them the packet we construct */
	if (ast_sockaddr_isnull(&remote_address)) {
		goto cleanup;
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
		goto cleanup;
	}

	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));

	if (duration > 0 && (measured_samples = duration * rtp_get_rate(&rtp->f.subclass.format) / 1000) > rtp->send_duration) {
		ast_debug(2, "Adjusting final end duration from %d to %u\n", rtp->send_duration, measured_samples);
		rtp->send_duration = measured_samples;
	}

	/* Construct the packet we are going to send */
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc);
	rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (rtp->send_duration));
	rtpheader[3] |= htonl((1 << 23));

	/* Send it 3 times, that's the magical number */
	for (i = 0; i < 3; i++) {
		int ice;

		rtpheader[0] = htonl((2 << 30) | (rtp->send_payload << 16) | (rtp->seqno));

		res = rtp_sendto(instance, (void *) rtpheader, hdrlen + 4, 0, &remote_address, &ice);

		if (res < 0) {
			ast_log(LOG_ERROR, "RTP Transmission error to %s: %s\n",
				ast_sockaddr_stringify(&remote_address),
				strerror(errno));
		}

		if (rtp_debug_test_addr(&remote_address)) {
			ast_verbose("Sent RTP DTMF packet to %s%s (type %-2.2d, seq %-6.6d, ts %-6.6u, len %-6.6d)\n",
				    ast_sockaddr_stringify(&remote_address),
				    ice ? " (via ICE)" : "",
				    rtp->send_payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
		}

		rtp->seqno++;
	}
	res = 0;

	/* Oh and we can't forget to turn off the stuff that says we are sending DTMF */
	rtp->lastts += calc_txstamp(rtp, NULL) * DTMF_SAMPLE_RATE_MS;
cleanup:
	rtp->sending_digit = 0;
	rtp->send_digit = 0;

	return res;
}

static int ast_rtp_dtmf_end(struct ast_rtp_instance *instance, char digit)
{
	return ast_rtp_dtmf_end_with_duration(instance, digit, 0);
}

static void ast_rtp_update_source(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* We simply set this bit so that the next packet sent will have the marker bit turned on */
	ast_set_flag(rtp, FLAG_NEED_MARKER_BIT);
	ast_debug(3, "Setting the marker bit due to a source update\n");

	return;
}

static void ast_rtp_change_source(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_srtp *srtp = ast_rtp_instance_get_srtp(instance);
	unsigned int ssrc = ast_random();

	if (!rtp->lastts) {
		ast_debug(3, "Not changing SSRC since we haven't sent any RTP yet\n");
		return;
	}

	/* We simply set this bit so that the next packet sent will have the marker bit turned on */
	ast_set_flag(rtp, FLAG_NEED_MARKER_BIT);

	ast_debug(3, "Changing ssrc from %u to %u due to a source change\n", rtp->ssrc, ssrc);

	if (srtp) {
		ast_debug(3, "Changing ssrc for SRTP from %u to %u\n", rtp->ssrc, ssrc);
		res_srtp->change_source(srtp, rtp->ssrc, ssrc);
	}

	rtp->ssrc = ssrc;

	return;
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
static int ast_rtcp_write_rr(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
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
	int rate = rtp_get_rate(&rtp->f.subclass.format);
	int ice;
	double rxlost_current;
	struct ast_sockaddr remote_address = { {0,} };

	if (!rtp || !rtp->rtcp)
		return 0;

	if (ast_sockaddr_isnull(&rtp->rtcp->them)) {
		/*
		 * RTCP was stopped.
		 */
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
	rtcpheader[5] = htonl((unsigned int)(rtp->rxjitter * rate));
	rtcpheader[6] = htonl(rtp->rtcp->themrxlsr);
	rtcpheader[7] = htonl((((dlsr.tv_sec * 1000) + (dlsr.tv_usec / 1000)) * 65536) / 1000);

	/*! \note Insert SDES here. Probably should make SDES text equal to mimetypes[code].type (not subtype 'cos
	  it can change mid call, and SDES can't) */
	rtcpheader[len/4]     = htonl((2 << 30) | (1 << 24) | (RTCP_PT_SDES << 16) | 2);
	rtcpheader[(len/4)+1] = htonl(rtp->ssrc);               /* Our SSRC */
	rtcpheader[(len/4)+2] = htonl(0x01 << 24);              /* Empty for the moment */
	len += 12;

	ast_sockaddr_copy(&remote_address, &rtp->rtcp->them);

	res = rtcp_sendto(instance, (unsigned int *)rtcpheader, len, 0, &remote_address, &ice);

	if (res < 0) {
		ast_log(LOG_ERROR, "RTCP RR transmission error, rtcp halted: %s\n",strerror(errno));
		return 0;
	}

	rtp->rtcp->rr_count++;

	if (rtcp_debug_test_addr(&remote_address)) {
		ast_verbose("\n* Sending RTCP RR to %s%s\n"
			"  Our SSRC: %u\nTheir SSRC: %u\niFraction lost: %d\nCumulative loss: %u\n"
			"  IA jitter: %.4f\n"
			"  Their last SR: %u\n"
			    "  DLSR: %4.4f (sec)\n\n",
			    ast_sockaddr_stringify(&remote_address),
			    ice ? " (via ICE)" : "",
			    rtp->ssrc, rtp->themssrc, fraction, lost,
			    rtp->rxjitter,
			    rtp->rtcp->themrxlsr,
			    (double)(ntohl(rtcpheader[7])/65536.0));
	}

	return res;
}

/*! \brief Send RTCP sender's report */
static int ast_rtcp_write_sr(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
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
	int rate = rtp_get_rate(&rtp->f.subclass.format);
	int ice;
	struct ast_sockaddr remote_address = { {0,} };

	if (!rtp || !rtp->rtcp)
		return 0;

	if (ast_sockaddr_isnull(&rtp->rtcp->them)) {  /* This'll stop rtcp for this rtp session */
		/*
		 * RTCP was stopped.
		 */
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
	rtcpheader[10] = htonl((unsigned int)(rtp->rxjitter * rate));
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

	ast_sockaddr_copy(&remote_address, &rtp->rtcp->them);

	res = rtcp_sendto(instance, (unsigned int *)rtcpheader, len, 0, &remote_address, &ice);
	if (res < 0) {
		ast_log(LOG_ERROR, "RTCP SR transmission error to %s, rtcp halted %s\n",
			ast_sockaddr_stringify(&rtp->rtcp->them),
			strerror(errno));
		return 0;
	}

	/* FIXME Don't need to get a new one */
	gettimeofday(&rtp->rtcp->txlsr, NULL);
	rtp->rtcp->sr_count++;

	rtp->rtcp->lastsrtxcount = rtp->txcount;

	if (rtcp_debug_test_addr(&rtp->rtcp->them)) {
		ast_verbose("* Sent RTCP SR to %s%s\n", ast_sockaddr_stringify(&remote_address), ice ? " (via ICE)" : "");
		ast_verbose("  Our SSRC: %u\n", rtp->ssrc);
		ast_verbose("  Sent(NTP): %u.%010u\n", (unsigned int)now.tv_sec, (unsigned int)now.tv_usec*4096);
		ast_verbose("  Sent(RTP): %u\n", rtp->lastts);
		ast_verbose("  Sent packets: %u\n", rtp->txcount);
		ast_verbose("  Sent octets: %u\n", rtp->txoctetcount);
		ast_verbose("  Report block:\n");
		ast_verbose("  Fraction lost: %d\n", fraction);
		ast_verbose("  Cumulative loss: %u\n", lost);
		ast_verbose("  IA jitter: %.4f\n", rtp->rxjitter);
		ast_verbose("  Their last SR: %u\n", rtp->rtcp->themrxlsr);
		ast_verbose("  DLSR: %4.4f (sec)\n\n", (double)(ntohl(rtcpheader[12])/65536.0));
	}
	manager_event(EVENT_FLAG_REPORTING, "RTCPSent", "To: %s\r\n"
					    "OurSSRC: %u\r\n"
					    "SentNTP: %u.%010u\r\n"
					    "SentRTP: %u\r\n"
					    "SentPackets: %u\r\n"
					    "SentOctets: %u\r\n"
					    "ReportBlock:\r\n"
					    "FractionLost: %d\r\n"
					    "CumulativeLoss: %u\r\n"
					    "IAJitter: %.4f\r\n"
					    "TheirLastSR: %u\r\n"
		      "DLSR: %4.4f (sec)\r\n",
		      ast_sockaddr_stringify(&remote_address),
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
	struct ast_rtp_instance *instance = (struct ast_rtp_instance *) data;
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	int res;

	if (!rtp || !rtp->rtcp || rtp->rtcp->schedid == -1) {
		ao2_ref(instance, -1);
		return 0;
	}

	if (rtp->txcount > rtp->rtcp->lastsrtxcount) {
		res = ast_rtcp_write_sr(instance);
	} else {
		res = ast_rtcp_write_rr(instance);
	}

	if (!res) {
		/* 
		 * Not being rescheduled.
		 */
		ao2_ref(instance, -1);
		rtp->rtcp->schedid = -1;
	}

	return res;
}

static int ast_rtp_raw_write(struct ast_rtp_instance *instance, struct ast_frame *frame, int codec)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	int pred, mark = 0;
	unsigned int ms = calc_txstamp(rtp, &frame->delivery);
	struct ast_sockaddr remote_address = { {0,} };
	int rate = rtp_get_rate(&frame->subclass.format) / 1000;

	if (frame->subclass.format.id == AST_FORMAT_G722) {
		frame->samples /= 2;
	}

	if (rtp->sending_digit) {
		return 0;
	}

	if (frame->frametype == AST_FRAME_VOICE) {
		pred = rtp->lastts + frame->samples;

		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * rate;
		if (ast_tvzero(frame->delivery)) {
			/* If this isn't an absolute delivery time, Check if it is close to our prediction,
			   and if so, go with our prediction */
			if (abs(rtp->lastts - pred) < MAX_TIMESTAMP_SKEW) {
				rtp->lastts = pred;
			} else {
				ast_debug(3, "Difference is %d, ms is %u\n", abs(rtp->lastts - pred), ms);
				mark = 1;
			}
		}
	} else if (frame->frametype == AST_FRAME_VIDEO) {
		mark = ast_format_get_video_mark(&frame->subclass.format);
		pred = rtp->lastovidtimestamp + frame->samples;
		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * 90;
		/* If it's close to our prediction, go for it */
		if (ast_tvzero(frame->delivery)) {
			if (abs(rtp->lastts - pred) < 7200) {
				rtp->lastts = pred;
				rtp->lastovidtimestamp += frame->samples;
			} else {
				ast_debug(3, "Difference is %d, ms is %u (%u), pred/ts/samples %u/%d/%d\n", abs(rtp->lastts - pred), ms, ms * 90, rtp->lastts, pred, frame->samples);
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
				ast_debug(3, "Difference is %d, ms is %u, pred/ts/samples %u/%d/%d\n", abs(rtp->lastts - pred), ms, rtp->lastts, pred, frame->samples);
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
		rtp->lastts = frame->ts * rate;
	}

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* If we know the remote address construct a packet and send it out */
	if (!ast_sockaddr_isnull(&remote_address)) {
		int hdrlen = 12, res, ice;
		unsigned char *rtpheader = (unsigned char *)(frame->data.ptr - hdrlen);

		put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (rtp->seqno) | (mark << 23)));
		put_unaligned_uint32(rtpheader + 4, htonl(rtp->lastts));
		put_unaligned_uint32(rtpheader + 8, htonl(rtp->ssrc));

		if ((res = rtp_sendto(instance, (void *)rtpheader, frame->datalen + hdrlen, 0, &remote_address, &ice)) < 0) {
			if (!ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT) || (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT) && (ast_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE))) {
				ast_debug(1, "RTP Transmission error of packet %d to %s: %s\n",
					  rtp->seqno,
					  ast_sockaddr_stringify(&remote_address),
					  strerror(errno));
			} else if (((ast_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug) && !ast_test_flag(rtp, FLAG_NAT_INACTIVE_NOWARN)) {
				/* Only give this error message once if we are not RTP debugging */
				if (rtpdebug)
					ast_debug(0, "RTP NAT: Can't write RTP to private address %s, waiting for other end to send audio...\n",
						  ast_sockaddr_stringify(&remote_address));
				ast_set_flag(rtp, FLAG_NAT_INACTIVE_NOWARN);
			}
		} else {
			rtp->txcount++;
			rtp->txoctetcount += (res - hdrlen);

			if (rtp->rtcp && rtp->rtcp->schedid < 1) {
				ast_debug(1, "Starting RTCP transmission on RTP instance '%p'\n", instance);
				ao2_ref(instance, +1);
				rtp->rtcp->schedid = ast_sched_add(rtp->sched, ast_rtcp_calc_interval(rtp), ast_rtcp_write, instance);
				if (rtp->rtcp->schedid < 0) {
					ao2_ref(instance, -1);
					ast_log(LOG_WARNING, "scheduling RTCP transmission failed.\n");
				}
			}
		}

		if (rtp_debug_test_addr(&remote_address)) {
			ast_verbose("Sent RTP packet to      %s%s (type %-2.2d, seq %-6.6d, ts %-6.6u, len %-6.6d)\n",
				    ast_sockaddr_stringify(&remote_address),
				    ice ? " (via ICE)" : "",
				    codec, rtp->seqno, rtp->lastts, res - hdrlen);
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
	struct ast_sockaddr remote_address = { {0,} };
	struct ast_format subclass;
	int codec;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* If we don't actually know the remote address don't even bother doing anything */
	if (ast_sockaddr_isnull(&remote_address)) {
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
	ast_format_copy(&subclass, &frame->subclass.format);
	if ((codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance), 1, &subclass, 0)) < 0) {
		ast_log(LOG_WARNING, "Don't know how to send format %s packets with RTP\n", ast_getformatname(&frame->subclass.format));
		return -1;
	}

	/* Oh dear, if the format changed we will have to set up a new smoother */
	if (ast_format_cmp(&rtp->lasttxformat, &subclass) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Ooh, format changed from %s to %s\n", ast_getformatname(&rtp->lasttxformat), ast_getformatname(&subclass));
		rtp->lasttxformat = subclass;
		ast_format_copy(&rtp->lasttxformat, &subclass);
		if (rtp->smoother) {
			ast_smoother_free(rtp->smoother);
			rtp->smoother = NULL;
		}
	}

	/* If no smoother is present see if we have to set one up */
	if (!rtp->smoother) {
		struct ast_format_list fmt = ast_codec_pref_getsize(&ast_rtp_instance_get_codecs(instance)->pref, &subclass);

		switch (subclass.id) {
		case AST_FORMAT_SPEEX:
		case AST_FORMAT_SPEEX16:
		case AST_FORMAT_SPEEX32:
		case AST_FORMAT_SILK:
		case AST_FORMAT_CELT:
		case AST_FORMAT_G723_1:
		case AST_FORMAT_SIREN7:
		case AST_FORMAT_SIREN14:
		case AST_FORMAT_G719:
			/* these are all frame-based codecs and cannot be safely run through
			   a smoother */
			break;
		default:
			if (fmt.inc_ms) {
				if (!(rtp->smoother = ast_smoother_new((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms))) {
					ast_log(LOG_WARNING, "Unable to create smoother: format %s ms: %d len: %d\n", ast_getformatname(&subclass), fmt.cur_ms, ((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms));
					return -1;
				}
				if (fmt.flags) {
					ast_smoother_set_flags(rtp->smoother, fmt.flags);
				}
				ast_debug(1, "Created smoother: format: %s ms: %d len: %d\n", ast_getformatname(&subclass), fmt.cur_ms, ((fmt.cur_ms * fmt.fr_len) / fmt.inc_ms));
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
	struct timeval tmp;
	double transit;
	double current_time;
	double d;
	double dtv;
	double prog;
	int rate = rtp_get_rate(&rtp->f.subclass.format);

	double normdev_rxjitter_current;
	if ((!rtp->rxcore.tv_sec && !rtp->rxcore.tv_usec) || mark) {
		gettimeofday(&rtp->rxcore, NULL);
		rtp->drxcore = (double) rtp->rxcore.tv_sec + (double) rtp->rxcore.tv_usec / 1000000;
		/* map timestamp to a real time */
		rtp->seedrxts = timestamp; /* Their RTP timestamp started with this */
		tmp = ast_samp2tv(timestamp, rate);
		rtp->rxcore = ast_tvsub(rtp->rxcore, tmp);
		/* Round to 0.1ms for nice, pretty timestamps */
		rtp->rxcore.tv_usec -= rtp->rxcore.tv_usec % 100;
	}

	gettimeofday(&now,NULL);
	/* rxcore is the mapping between the RTP timestamp and _our_ real time from gettimeofday() */
	tmp = ast_samp2tv(timestamp, rate);
	*tv = ast_tvadd(rtp->rxcore, tmp);

	prog = (double)((timestamp-rtp->seedrxts)/(float)(rate));
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

static struct ast_frame *create_dtmf_frame(struct ast_rtp_instance *instance, enum ast_frame_type type, int compensate)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr remote_address = { {0,} };

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	if (((compensate && type == AST_FRAME_DTMF_END) || (type == AST_FRAME_DTMF_BEGIN)) && ast_tvcmp(ast_tvnow(), rtp->dtmfmute) < 0) {
		ast_debug(1, "Ignore potential DTMF echo from '%s'\n",
			  ast_sockaddr_stringify(&remote_address));
		rtp->resp = 0;
		rtp->dtmfsamples = 0;
		return &ast_null_frame;
	}
	ast_debug(1, "Creating %s DTMF Frame: %d (%c), at %s\n",
		type == AST_FRAME_DTMF_END ? "END" : "BEGIN",
		rtp->resp, rtp->resp,
		ast_sockaddr_stringify(&remote_address));
	if (rtp->resp == 'X') {
		rtp->f.frametype = AST_FRAME_CONTROL;
		rtp->f.subclass.integer = AST_CONTROL_FLASH;
	} else {
		rtp->f.frametype = type;
		rtp->f.subclass.integer = rtp->resp;
	}
	rtp->f.datalen = 0;
	rtp->f.samples = 0;
	rtp->f.mallocd = 0;
	rtp->f.src = "RTP";
	AST_LIST_NEXT(&rtp->f, frame_list) = NULL;

	return &rtp->f;
}

static void process_dtmf_rfc2833(struct ast_rtp_instance *instance, unsigned char *data, int len, unsigned int seqno, unsigned int timestamp, struct ast_sockaddr *addr, int payloadtype, int mark, struct frame_list *frames)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr remote_address = { {0,} };
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
		ast_verbose("Got  RTP RFC2833 from   %s (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6d, mark %d, event %08x, end %d, duration %-5.5u) \n",
			    ast_sockaddr_stringify(&remote_address),
			    payloadtype, seqno, timestamp, len, (mark?1:0), event, ((event_end & 0x80)?1:0), samples);
	}

	/* Print out debug if turned on */
	if (rtpdebug)
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
		ast_debug(1, "Ignoring RTP 2833 Event: %08x. Not a DTMF Digit.\n", event);
		return;
	}

	if (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_DTMF_COMPENSATE)) {
		if ((rtp->last_end_timestamp != timestamp) || (rtp->resp && rtp->resp != resp)) {
			rtp->resp = resp;
			rtp->dtmf_timeout = 0;
			f = ast_frdup(create_dtmf_frame(instance, AST_FRAME_DTMF_END, ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_DTMF_COMPENSATE)));
			f->len = 0;
			rtp->last_end_timestamp = timestamp;
			AST_LIST_INSERT_TAIL(frames, f, frame_list);
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
			if ((seqno != rtp->last_seqno) && (timestamp > rtp->last_end_timestamp)) {
				rtp->last_end_timestamp = timestamp;
				rtp->dtmf_duration = new_duration;
				rtp->resp = resp;
				f = ast_frdup(create_dtmf_frame(instance, AST_FRAME_DTMF_END, 0));
				f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, rtp_get_rate(&f->subclass.format)), ast_tv(0, 0));
				rtp->resp = 0;
				rtp->dtmf_duration = rtp->dtmf_timeout = 0;
				AST_LIST_INSERT_TAIL(frames, f, frame_list);
			} else if (rtpdebug) {
				ast_debug(1, "Dropping re-transmitted, duplicate, or out of order DTMF END frame (seqno: %u, ts %u, digit %c)\n",
					seqno, timestamp, resp);
			}
		} else {
			/* Begin/continuation */

			/* The second portion of the seqno check is to not mistakenly
			 * stop accepting DTMF if the seqno rolls over beyond
			 * 65535.
			 */
			if ((rtp->last_seqno > seqno && rtp->last_seqno - seqno < 50)
				|| timestamp <= rtp->last_end_timestamp) {
				/* Out of order frame. Processing this can cause us to
				 * improperly duplicate incoming DTMF, so just drop
				 * this.
				 */
				if (rtpdebug) {
					ast_debug(1, "Dropping out of order DTMF frame (seqno %u, ts %u, digit %c)\n",
						seqno, timestamp, resp);
				}
				return;
			}

			if (rtp->resp && rtp->resp != resp) {
				/* Another digit already began. End it */
				f = ast_frdup(create_dtmf_frame(instance, AST_FRAME_DTMF_END, 0));
				f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, rtp_get_rate(&f->subclass.format)), ast_tv(0, 0));
				rtp->resp = 0;
				rtp->dtmf_duration = rtp->dtmf_timeout = 0;
				AST_LIST_INSERT_TAIL(frames, f, frame_list);
			}

			if (rtp->resp) {
				/* Digit continues */
				rtp->dtmf_duration = new_duration;
			} else {
				/* New digit began */
				rtp->resp = resp;
				f = ast_frdup(create_dtmf_frame(instance, AST_FRAME_DTMF_BEGIN, 0));
				rtp->dtmf_duration = samples;
				AST_LIST_INSERT_TAIL(frames, f, frame_list);
			}

			rtp->dtmf_timeout = timestamp + rtp->dtmf_duration + dtmftimeout;
		}

		rtp->last_seqno = seqno;
	}

	rtp->dtmfsamples = samples;

	return;
}

static struct ast_frame *process_dtmf_cisco(struct ast_rtp_instance *instance, unsigned char *data, int len, unsigned int seqno, unsigned int timestamp, struct ast_sockaddr *addr, int payloadtype, int mark)
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

	if (rtpdebug)
		ast_debug(0, "Cisco DTMF Digit: %02x (len=%d, seq=%d, flags=%02x, power=%u, history count=%d)\n", event, len, seq, flags, power, (len - 4) / 2);
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
			f = create_dtmf_frame(instance, AST_FRAME_DTMF_BEGIN, 0);
			rtp->dtmfsamples = 0;
		}
	} else if ((rtp->resp == resp) && !power) {
		f = create_dtmf_frame(instance, AST_FRAME_DTMF_END, ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_DTMF_COMPENSATE));
		f->samples = rtp->dtmfsamples * (rtp->lastrxformat.id ? (rtp_get_rate(&rtp->lastrxformat) / 1000) : 8);
		rtp->resp = 0;
	} else if (rtp->resp == resp)
		rtp->dtmfsamples += 20 * (rtp->lastrxformat.id ? (rtp_get_rate(&rtp->lastrxformat) / 1000) : 8);

	rtp->dtmf_timeout = 0;

	return f;
}

static struct ast_frame *process_cn_rfc3389(struct ast_rtp_instance *instance, unsigned char *data, int len, unsigned int seqno, unsigned int timestamp, struct ast_sockaddr *addr, int payloadtype, int mark)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* Convert comfort noise into audio with various codecs.  Unfortunately this doesn't
	   totally help us out becuase we don't have an engine to keep it going and we are not
	   guaranteed to have it every 20ms or anything */
	if (rtpdebug)
		ast_debug(0, "- RTP 3389 Comfort noise event: Level %d (len = %d)\n", (int) rtp->lastrxformat.id, len);

	if (ast_test_flag(rtp, FLAG_3389_WARNING)) {
		struct ast_sockaddr remote_address = { {0,} };

		ast_rtp_instance_get_remote_address(instance, &remote_address);

		ast_log(LOG_NOTICE, "Comfort noise support incomplete in Asterisk (RFC 3389). Please turn off on client if possible. Client address: %s\n",
			ast_sockaddr_stringify(&remote_address));
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
	rtp->f.subclass.integer = data[0] & 0x7f;
	rtp->f.samples = 0;
	rtp->f.delivery.tv_usec = rtp->f.delivery.tv_sec = 0;

	return &rtp->f;
}

static struct ast_frame *ast_rtcp_read(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr addr;
	unsigned char rtcpdata[8192 + AST_FRIENDLY_OFFSET];
	unsigned int *rtcpheader = (unsigned int *)(rtcpdata + AST_FRIENDLY_OFFSET);
	int res, packetwords, position = 0;
	struct ast_frame *f = &ast_null_frame;

	/* Read in RTCP data from the socket */
	if ((res = rtcp_recvfrom(instance, rtcpdata + AST_FRIENDLY_OFFSET,
				sizeof(rtcpdata) - AST_FRIENDLY_OFFSET,
				0, &addr)) < 0) {
		ast_assert(errno != EBADF);
		if (errno != EAGAIN) {
			ast_log(LOG_WARNING, "RTCP Read error: %s.  Hanging up.\n",
				(errno) ? strerror(errno) : "Unspecified");
			return NULL;
		}
		return &ast_null_frame;
	}

	/* If this was handled by the ICE session don't do anything further */
	if (!res) {
		return &ast_null_frame;
	}

	if (!*(rtcpdata + AST_FRIENDLY_OFFSET)) {
		struct sockaddr_in addr_tmp;
		struct ast_sockaddr addr_v4;

		if (ast_sockaddr_is_ipv4(&addr)) {
			ast_sockaddr_to_sin(&addr, &addr_tmp);
		} else if (ast_sockaddr_ipv4_mapped(&addr, &addr_v4)) {
			ast_debug(1, "Using IPv6 mapped address %s for STUN\n",
				  ast_sockaddr_stringify(&addr));
			ast_sockaddr_to_sin(&addr_v4, &addr_tmp);
		} else {
			ast_debug(1, "Cannot do STUN for non IPv4 address %s\n",
				  ast_sockaddr_stringify(&addr));
			return &ast_null_frame;
		}
		if ((ast_stun_handle_packet(rtp->rtcp->s, &addr_tmp, rtcpdata + AST_FRIENDLY_OFFSET, res, NULL, NULL) == AST_STUN_ACCEPT)) {
			ast_sockaddr_from_sin(&addr, &addr_tmp);
			ast_sockaddr_copy(&rtp->rtcp->them, &addr);
		}
		return &ast_null_frame;
	}

	packetwords = res / 4;

	if (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT)) {
		/* Send to whoever sent to us */
		if (ast_sockaddr_cmp(&rtp->rtcp->them, &addr)) {
			ast_sockaddr_copy(&rtp->rtcp->them, &addr);
			if (rtpdebug)
				ast_debug(0, "RTCP NAT: Got RTCP from other end. Now sending to address %s\n",
					  ast_sockaddr_stringify(&rtp->rtcp->them));
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
			if (rtpdebug)
				ast_debug(1, "RTCP Read too short\n");
			return &ast_null_frame;
		}

		if (rtcp_debug_test_addr(&addr)) {
			ast_verbose("\n\nGot RTCP from %s\n",
				    ast_sockaddr_stringify(&addr));
			ast_verbose("PT: %d(%s)\n", pt, (pt == 200) ? "Sender Report" : (pt == 201) ? "Receiver Report" : (pt == 192) ? "H.261 FUR" : "Unknown");
			ast_verbose("Reception reports: %d\n", rc);
			ast_verbose("SSRC of sender: %u\n", rtcpheader[i + 1]);
		}

		i += 2; /* Advance past header and ssrc */
		if (rc == 0 && pt == RTCP_PT_RR) {      /* We're receiving a receiver report with no reports, which is ok */
			position += (length + 1);
			continue;
		}

		switch (pt) {
		case RTCP_PT_SR:
			gettimeofday(&rtp->rtcp->rxlsr,NULL); /* To be able to populate the dlsr */
			rtp->rtcp->spc = ntohl(rtcpheader[i+3]);
			rtp->rtcp->soc = ntohl(rtcpheader[i + 4]);
			rtp->rtcp->themrxlsr = ((ntohl(rtcpheader[i]) & 0x0000ffff) << 16) | ((ntohl(rtcpheader[i + 1]) & 0xffff0000) >> 16); /* Going to LSR in RR*/

			if (rtcp_debug_test_addr(&addr)) {
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
				} else if (rtcp_debug_test_addr(&addr)) {
					ast_verbose("Internal RTCP NTP clock skew detected: "
							   "lsr=%u, now=%u, dlsr=%u (%u:%03ums), "
						    "diff=%u\n",
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

			if (rtcp_debug_test_addr(&addr)) {
				ast_verbose("  Fraction lost: %ld\n", (((long) ntohl(rtcpheader[i + 1]) & 0xff000000) >> 24));
				ast_verbose("  Packets lost so far: %u\n", rtp->rtcp->reported_lost);
				ast_verbose("  Highest sequence number: %ld\n", (long) (ntohl(rtcpheader[i + 2]) & 0xffff));
				ast_verbose("  Sequence number cycles: %ld\n", (long) (ntohl(rtcpheader[i + 2])) >> 16);
				ast_verbose("  Interarrival jitter: %u\n", rtp->rtcp->reported_jitter);
				ast_verbose("  Last SR(our NTP): %lu.%010lu\n",(unsigned long) ntohl(rtcpheader[i + 4]) >> 16,((unsigned long) ntohl(rtcpheader[i + 4]) << 16) * 4096);
				ast_verbose("  DLSR: %4.4f (sec)\n",ntohl(rtcpheader[i + 5])/65536.0);
				if (rtt)
					ast_verbose("  RTT: %lu(sec)\n", (unsigned long) rtt);
			}
			if (rtt) {
				manager_event(EVENT_FLAG_REPORTING, "RTCPReceived", "From: %s\r\n"
								    "PT: %d(%s)\r\n"
								    "ReceptionReports: %d\r\n"
								    "SenderSSRC: %u\r\n"
								    "FractionLost: %ld\r\n"
								    "PacketsLost: %u\r\n"
								    "HighestSequence: %ld\r\n"
								    "SequenceNumberCycles: %ld\r\n"
								    "IAJitter: %u\r\n"
								    "LastSR: %lu.%010lu\r\n"
								    "DLSR: %4.4f(sec)\r\n"
					      "RTT: %llu(sec)\r\n",
					      ast_sockaddr_stringify(&addr),
					      pt, (pt == 200) ? "Sender Report" : (pt == 201) ? "Receiver Report" : (pt == 192) ? "H.261 FUR" : "Unknown",
					      rc,
					      rtcpheader[i + 1],
					      (((long) ntohl(rtcpheader[i + 1]) & 0xff000000) >> 24),
					      rtp->rtcp->reported_lost,
					      (long) (ntohl(rtcpheader[i + 2]) & 0xffff),
					      (long) (ntohl(rtcpheader[i + 2])) >> 16,
					      rtp->rtcp->reported_jitter,
					      (unsigned long) ntohl(rtcpheader[i + 4]) >> 16, ((unsigned long) ntohl(rtcpheader[i + 4]) << 16) * 4096,
					      ntohl(rtcpheader[i + 5])/65536.0,
					      (unsigned long long)rtt);
			} else {
				manager_event(EVENT_FLAG_REPORTING, "RTCPReceived", "From: %s\r\n"
								    "PT: %d(%s)\r\n"
								    "ReceptionReports: %d\r\n"
								    "SenderSSRC: %u\r\n"
								    "FractionLost: %ld\r\n"
								    "PacketsLost: %u\r\n"
								    "HighestSequence: %ld\r\n"
								    "SequenceNumberCycles: %ld\r\n"
								    "IAJitter: %u\r\n"
								    "LastSR: %lu.%010lu\r\n"
					      "DLSR: %4.4f(sec)\r\n",
					      ast_sockaddr_stringify(&addr),
					      pt, (pt == 200) ? "Sender Report" : (pt == 201) ? "Receiver Report" : (pt == 192) ? "H.261 FUR" : "Unknown",
					      rc,
					      rtcpheader[i + 1],
					      (((long) ntohl(rtcpheader[i + 1]) & 0xff000000) >> 24),
					      rtp->rtcp->reported_lost,
					      (long) (ntohl(rtcpheader[i + 2]) & 0xffff),
					      (long) (ntohl(rtcpheader[i + 2])) >> 16,
					      rtp->rtcp->reported_jitter,
					      (unsigned long) ntohl(rtcpheader[i + 4]) >> 16,
					      ((unsigned long) ntohl(rtcpheader[i + 4]) << 16) * 4096,
					      ntohl(rtcpheader[i + 5])/65536.0);
			}
			break;
		case RTCP_PT_FUR:
			if (rtcp_debug_test_addr(&addr))
				ast_verbose("Received an RTCP Fast Update Request\n");
			rtp->f.frametype = AST_FRAME_CONTROL;
			rtp->f.subclass.integer = AST_CONTROL_VIDUPDATE;
			rtp->f.datalen = 0;
			rtp->f.samples = 0;
			rtp->f.mallocd = 0;
			rtp->f.src = "RTP";
			f = &rtp->f;
			break;
		case RTCP_PT_SDES:
			if (rtcp_debug_test_addr(&addr))
				ast_verbose("Received an SDES from %s\n",
					    ast_sockaddr_stringify(&rtp->rtcp->them));
			break;
		case RTCP_PT_BYE:
			if (rtcp_debug_test_addr(&addr))
				ast_verbose("Received a BYE from %s\n",
					    ast_sockaddr_stringify(&rtp->rtcp->them));
			break;
		default:
			ast_debug(1, "Unknown RTCP packet (pt=%d) received from %s\n",
				  pt, ast_sockaddr_stringify(&rtp->rtcp->them));
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
	struct ast_sockaddr remote_address = { {0,} };
	int ice;

	/* Get fields from packet */
	payload = (reconstruct & 0x7f0000) >> 16;
	mark = (((reconstruct & 0x800000) >> 23) != 0);

	/* Check what the payload value should be */
	payload_type = ast_rtp_codecs_payload_lookup(ast_rtp_instance_get_codecs(instance), payload);

	/* Otherwise adjust bridged payload to match */
	bridged_payload = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance1), payload_type.asterisk_format, &payload_type.format, payload_type.rtp_code);

	/* If no codec could be matched between instance and instance1, then somehow things were made incompatible while we were still bridged.  Bail. */
	if (bridged_payload < 0) {
		return -1;
	}

	/* If the payload coming in is not one of the negotiated ones then send it to the core, this will cause formats to change and the bridge to break */
	if (ast_rtp_codecs_find_payload_code(ast_rtp_instance_get_codecs(instance1), bridged_payload) == -1) {
		ast_debug(1, "Unsupported payload type received \n");
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

	if (ast_sockaddr_isnull(&remote_address)) {
		ast_debug(1, "Remote address is null, most likely RTP has been stopped\n");
		return 0;
	}

	/* Send the packet back out */
	res = rtp_sendto(instance1, (void *)rtpheader, len, 0, &remote_address, &ice);
	if (res < 0) {
		if (!ast_rtp_instance_get_prop(instance1, AST_RTP_PROPERTY_NAT) || (ast_rtp_instance_get_prop(instance1, AST_RTP_PROPERTY_NAT) && (ast_test_flag(bridged, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE))) {
			ast_log(LOG_WARNING,
				"RTP Transmission error of packet to %s: %s\n",
				ast_sockaddr_stringify(&remote_address),
				strerror(errno));
		} else if (((ast_test_flag(bridged, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug) && !ast_test_flag(bridged, FLAG_NAT_INACTIVE_NOWARN)) {
			if (option_debug || rtpdebug)
				ast_log(LOG_WARNING,
					"RTP NAT: Can't write RTP to private "
					"address %s, waiting for other end to "
					"send audio...\n",
					ast_sockaddr_stringify(&remote_address));
			ast_set_flag(bridged, FLAG_NAT_INACTIVE_NOWARN);
		}
		return 0;
	}

	if (rtp_debug_test_addr(&remote_address)) {
		ast_verbose("Sent RTP P2P packet to %s%s (type %-2.2d, len %-6.6d)\n",
			    ast_sockaddr_stringify(&remote_address),
			    ice ? " (via ICE)" : "",
			    bridged_payload, len - hdrlen);
	}

	return 0;
}

static struct ast_frame *ast_rtp_read(struct ast_rtp_instance *instance, int rtcp)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr addr;
	int res, hdrlen = 12, version, payloadtype, padding, mark, ext, cc, prev_seqno;
	unsigned int *rtpheader = (unsigned int*)(rtp->rawdata + AST_FRIENDLY_OFFSET), seqno, ssrc, timestamp;
	struct ast_rtp_payload_type payload;
	struct ast_sockaddr remote_address = { {0,} };
	struct frame_list frames;

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
	if ((res = rtp_recvfrom(instance, rtp->rawdata + AST_FRIENDLY_OFFSET,
				sizeof(rtp->rawdata) - AST_FRIENDLY_OFFSET, 0,
				&addr)) < 0) {
		ast_assert(errno != EBADF);
		if (errno != EAGAIN) {
			ast_log(LOG_WARNING, "RTP Read error: %s.  Hanging up.\n",
				(errno) ? strerror(errno) : "Unspecified");
			return NULL;
		}
		return &ast_null_frame;
	}

	/* If this was handled by the ICE session don't do anything */
	if (!res) {
		return &ast_null_frame;
	}

	/* Make sure the data that was read in is actually enough to make up an RTP packet */
	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short\n");
		return &ast_null_frame;
	}

	/* Get fields and verify this is an RTP packet */
	seqno = ntohl(rtpheader[0]);

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	if (!(version = (seqno & 0xC0000000) >> 30)) {
		struct sockaddr_in addr_tmp;
		struct ast_sockaddr addr_v4;
		if (ast_sockaddr_is_ipv4(&addr)) {
			ast_sockaddr_to_sin(&addr, &addr_tmp);
		} else if (ast_sockaddr_ipv4_mapped(&addr, &addr_v4)) {
			ast_debug(1, "Using IPv6 mapped address %s for STUN\n",
				  ast_sockaddr_stringify(&addr));
			ast_sockaddr_to_sin(&addr_v4, &addr_tmp);
		} else {
			ast_debug(1, "Cannot do STUN for non IPv4 address %s\n",
				  ast_sockaddr_stringify(&addr));
			return &ast_null_frame;
		}
		if ((ast_stun_handle_packet(rtp->s, &addr_tmp, rtp->rawdata + AST_FRIENDLY_OFFSET, res, NULL, NULL) == AST_STUN_ACCEPT) &&
		    ast_sockaddr_isnull(&remote_address)) {
			ast_sockaddr_from_sin(&addr, &addr_tmp);
			ast_rtp_instance_set_remote_address(instance, &addr);
		}
		return &ast_null_frame;
	}

	/* If strict RTP protection is enabled see if we need to learn the remote address or if we need to drop the packet */
	if (rtp->strict_rtp_state == STRICT_RTP_LEARN) {
		ast_debug(1, "%p -- Probation learning mode pass with source address %s\n", rtp, ast_sockaddr_stringify(&addr));
		/* For now, we always copy the address. */
		ast_sockaddr_copy(&rtp->strict_rtp_address, &addr);

		/* Send the rtp and the seqno from header to rtp_learning_rtp_seq_update to see whether we can exit or not*/
		if (rtp_learning_rtp_seq_update(&rtp->rtp_source_learn, seqno)) {
			ast_debug(1, "%p -- Probation at seq %d with %d to go; discarding frame\n",
				rtp, rtp->rtp_source_learn.max_seq, rtp->rtp_source_learn.packets);
			return &ast_null_frame;
		}

		ast_verb(4, "%p -- Probation passed - setting RTP source address to %s\n", rtp, ast_sockaddr_stringify(&addr));
		rtp->strict_rtp_state = STRICT_RTP_CLOSED;
	}
	if (rtp->strict_rtp_state == STRICT_RTP_CLOSED) {
		if (!ast_sockaddr_cmp(&rtp->strict_rtp_address, &addr)) {
			/* Always reset the alternate learning source */
			rtp_learning_seq_init(&rtp->alt_source_learn, seqno);
		} else {
			/* Hmm, not the strict address. Perhaps we're getting audio from the alternate? */
			if (!ast_sockaddr_cmp(&rtp->alt_rtp_address, &addr)) {
				/* ooh, we did! You're now the new expected address, son! */
				ast_sockaddr_copy(&rtp->strict_rtp_address,
						  &addr);
			} else {
				/* Start trying to learn from the new address. If we pass a probationary period with
				 * it, that means we've stopped getting RTP from the original source and we should
				 * switch to it.
				 */
				if (rtp_learning_rtp_seq_update(&rtp->alt_source_learn, seqno)) {
					ast_debug(1, "%p -- Received RTP packet from %s, dropping due to strict RTP protection. Will switch to it in %d packets\n",
							rtp, ast_sockaddr_stringify(&addr), rtp->alt_source_learn.packets);
					return &ast_null_frame;
				}
				ast_verb(4, "%p -- Switching RTP source address to %s\n", rtp, ast_sockaddr_stringify(&addr));
				ast_sockaddr_copy(&rtp->strict_rtp_address, &addr);
			}
		}
	}

	/* If symmetric RTP is enabled see if the remote side is not what we expected and change where we are sending audio */
	if (ast_rtp_instance_get_prop(instance, AST_RTP_PROPERTY_NAT)) {
		if (ast_sockaddr_cmp(&remote_address, &addr)) {
			ast_rtp_instance_set_remote_address(instance, &addr);
			ast_sockaddr_copy(&remote_address, &addr);
			if (rtp->rtcp) {
				ast_sockaddr_copy(&rtp->rtcp->them, &addr);
				ast_sockaddr_set_port(&rtp->rtcp->them, ast_sockaddr_port(&addr) + 1);
			}
			rtp->rxseqno = 0;
			ast_set_flag(rtp, FLAG_NAT_ACTIVE);
			if (rtpdebug)
				ast_debug(0, "RTP NAT: Got audio from other end. Now sending to address %s\n",
					  ast_sockaddr_stringify(&remote_address));
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

	AST_LIST_HEAD_INIT_NOLOCK(&frames);
	/* Force a marker bit and change SSRC if the SSRC changes */
	if (rtp->rxssrc && rtp->rxssrc != ssrc) {
		struct ast_frame *f, srcupdate = {
			AST_FRAME_CONTROL,
			.subclass.integer = AST_CONTROL_SRCCHANGE,
		};

		if (!mark) {
			if (rtpdebug) {
				ast_debug(1, "Forcing Marker bit, because SSRC has changed\n");
			}
			mark = 1;
		}

		f = ast_frisolate(&srcupdate);
		AST_LIST_INSERT_TAIL(&frames, f, frame_list);

		rtp->seedrxseqno = 0;
		rtp->rxcount = 0;
		rtp->cycles = 0;
		rtp->lastrxseqno = 0;
		rtp->last_seqno = 0;
		rtp->last_end_timestamp = 0;
		if (rtp->rtcp) {
			rtp->rtcp->expected_prior = 0;
			rtp->rtcp->received_prior = 0;
		}
	}

	rtp->rxssrc = ssrc;

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
			unsigned int profile;
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
		return AST_LIST_FIRST(&frames) ? AST_LIST_FIRST(&frames) : &ast_null_frame;
	}

	rtp->rxcount++;
	if (rtp->rxcount == 1) {
		rtp->seedrxseqno = seqno;
	}

	/* Do not schedule RR if RTCP isn't run */
	if (rtp->rtcp && !ast_sockaddr_isnull(&rtp->rtcp->them) && rtp->rtcp->schedid < 1) {
		/* Schedule transmission of Receiver Report */
		ao2_ref(instance, +1);
		rtp->rtcp->schedid = ast_sched_add(rtp->sched, ast_rtcp_calc_interval(rtp), ast_rtcp_write, instance);
		if (rtp->rtcp->schedid < 0) {
			ao2_ref(instance, -1);
			ast_log(LOG_WARNING, "scheduling RTCP transmission failed.\n");
		}
	}
	if ((int)rtp->lastrxseqno - (int)seqno  > 100) /* if so it would indicate that the sender cycled; allow for misordering */
		rtp->cycles += RTP_SEQ_MOD;

	prev_seqno = rtp->lastrxseqno;
	rtp->lastrxseqno = seqno;

	if (!rtp->themssrc) {
		rtp->themssrc = ntohl(rtpheader[2]); /* Record their SSRC to put in future RR */
	}

	if (rtp_debug_test_addr(&addr)) {
		ast_verbose("Got  RTP packet from    %s (type %-2.2d, seq %-6.6u, ts %-6.6u, len %-6.6d)\n",
			    ast_sockaddr_stringify(&addr),
			    payloadtype, seqno, timestamp,res - hdrlen);
	}

	payload = ast_rtp_codecs_payload_lookup(ast_rtp_instance_get_codecs(instance), payloadtype);

	/* If the payload is not actually an Asterisk one but a special one pass it off to the respective handler */
	if (!payload.asterisk_format) {
		struct ast_frame *f = NULL;
		if (payload.rtp_code == AST_RTP_DTMF) {
			/* process_dtmf_rfc2833 may need to return multiple frames. We do this
			 * by passing the pointer to the frame list to it so that the method
			 * can append frames to the list as needed.
			 */
			process_dtmf_rfc2833(instance, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp, &addr, payloadtype, mark, &frames);
		} else if (payload.rtp_code == AST_RTP_CISCO_DTMF) {
			f = process_dtmf_cisco(instance, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp, &addr, payloadtype, mark);
		} else if (payload.rtp_code == AST_RTP_CN) {
			f = process_cn_rfc3389(instance, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp, &addr, payloadtype, mark);
		} else {
			ast_log(LOG_NOTICE, "Unknown RTP codec %d received from '%s'\n",
				payloadtype,
				ast_sockaddr_stringify(&remote_address));
		}

		if (f) {
			AST_LIST_INSERT_TAIL(&frames, f, frame_list);
		}
		/* Even if no frame was returned by one of the above methods,
		 * we may have a frame to return in our frame list
		 */
		if (!AST_LIST_EMPTY(&frames)) {
			return AST_LIST_FIRST(&frames);
		}
		return &ast_null_frame;
	}

	ast_format_copy(&rtp->lastrxformat, &payload.format);
	ast_format_copy(&rtp->f.subclass.format, &payload.format);
	rtp->f.frametype = (AST_FORMAT_GET_TYPE(rtp->f.subclass.format.id) == AST_FORMAT_TYPE_AUDIO) ? AST_FRAME_VOICE : (AST_FORMAT_GET_TYPE(rtp->f.subclass.format.id) == AST_FORMAT_TYPE_VIDEO) ? AST_FRAME_VIDEO : AST_FRAME_TEXT;

	rtp->rxseqno = seqno;

	if (rtp->dtmf_timeout && rtp->dtmf_timeout < timestamp) {
		rtp->dtmf_timeout = 0;

		if (rtp->resp) {
			struct ast_frame *f;
			f = create_dtmf_frame(instance, AST_FRAME_DTMF_END, 0);
			f->len = ast_tvdiff_ms(ast_samp2tv(rtp->dtmf_duration, rtp_get_rate(&f->subclass.format)), ast_tv(0, 0));
			rtp->resp = 0;
			rtp->dtmf_timeout = rtp->dtmf_duration = 0;
			AST_LIST_INSERT_TAIL(&frames, f, frame_list);
			return AST_LIST_FIRST(&frames);
		}
	}

	rtp->lastrxts = timestamp;

	rtp->f.src = "RTP";
	rtp->f.mallocd = 0;
	rtp->f.datalen = res - hdrlen;
	rtp->f.data.ptr = rtp->rawdata + hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.offset = hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.seqno = seqno;

	if (rtp->f.subclass.format.id == AST_FORMAT_T140 && (int)seqno - (prev_seqno+1) > 0 && (int)seqno - (prev_seqno+1) < 10) {
		unsigned char *data = rtp->f.data.ptr;

		memmove(rtp->f.data.ptr+3, rtp->f.data.ptr, rtp->f.datalen);
		rtp->f.datalen +=3;
		*data++ = 0xEF;
		*data++ = 0xBF;
		*data = 0xBD;
	}

	if (rtp->f.subclass.format.id == AST_FORMAT_T140RED) {
		unsigned char *data = rtp->f.data.ptr;
		unsigned char *header_end;
		int num_generations;
		int header_length;
		int len;
		int diff =(int)seqno - (prev_seqno+1); /* if diff = 0, no drop*/
		int x;

		ast_format_set(&rtp->f.subclass.format, AST_FORMAT_T140, 0);
		header_end = memchr(data, ((*data) & 0x7f), rtp->f.datalen);
		if (header_end == NULL) {
			return AST_LIST_FIRST(&frames) ? AST_LIST_FIRST(&frames) : &ast_null_frame;
		}
		header_end++;

		header_length = header_end - data;
		num_generations = header_length / 4;
		len = header_length;

		if (!diff) {
			for (x = 0; x < num_generations; x++)
				len += data[x * 4 + 3];

			if (!(rtp->f.datalen - len))
				return AST_LIST_FIRST(&frames) ? AST_LIST_FIRST(&frames) : &ast_null_frame;

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

	if (AST_FORMAT_GET_TYPE(rtp->f.subclass.format.id) == AST_FORMAT_TYPE_AUDIO) {
		rtp->f.samples = ast_codec_get_samples(&rtp->f);
		if (ast_format_is_slinear(&rtp->f.subclass.format)) {
			ast_frame_byteswap_be(&rtp->f);
		}
		calc_rxstamp(&rtp->f.delivery, rtp, timestamp, mark);
		/* Add timing data to let ast_generic_bridge() put the frame into a jitterbuf */
		ast_set_flag(&rtp->f, AST_FRFLAG_HAS_TIMING_INFO);
		rtp->f.ts = timestamp / (rtp_get_rate(&rtp->f.subclass.format) / 1000);
		rtp->f.len = rtp->f.samples / ((ast_format_rate(&rtp->f.subclass.format) / 1000));
	} else if (AST_FORMAT_GET_TYPE(rtp->f.subclass.format.id) == AST_FORMAT_TYPE_VIDEO) {
		/* Video -- samples is # of samples vs. 90000 */
		if (!rtp->lastividtimestamp)
			rtp->lastividtimestamp = timestamp;
		rtp->f.samples = timestamp - rtp->lastividtimestamp;
		rtp->lastividtimestamp = timestamp;
		rtp->f.delivery.tv_sec = 0;
		rtp->f.delivery.tv_usec = 0;
		/* Pass the RTP marker bit as bit */
		if (mark) {
			ast_format_set_video_mark(&rtp->f.subclass.format);
		}
	} else {
		/* TEXT -- samples is # of samples vs. 1000 */
		if (!rtp->lastitexttimestamp)
			rtp->lastitexttimestamp = timestamp;
		rtp->f.samples = timestamp - rtp->lastitexttimestamp;
		rtp->lastitexttimestamp = timestamp;
		rtp->f.delivery.tv_sec = 0;
		rtp->f.delivery.tv_usec = 0;
	}

	AST_LIST_INSERT_TAIL(&frames, &rtp->f, frame_list);
	return AST_LIST_FIRST(&frames);
}

static void ast_rtp_prop_set(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (property == AST_RTP_PROPERTY_RTCP) {
		if (value) {
			if (rtp->rtcp) {
				ast_debug(1, "Ignoring duplicate RTCP property on RTP instance '%p'\n", instance);
				return;
			}
			/* Setup RTCP to be activated on the next RTP write */
			if (!(rtp->rtcp = ast_calloc(1, sizeof(*rtp->rtcp)))) {
				return;
			}

			/* Grab the IP address and port we are going to use */
			ast_rtp_instance_get_local_address(instance, &rtp->rtcp->us);
			ast_sockaddr_set_port(&rtp->rtcp->us,
					      ast_sockaddr_port(&rtp->rtcp->us) + 1);

			if ((rtp->rtcp->s =
			     create_new_socket("RTCP",
					       ast_sockaddr_is_ipv4(&rtp->rtcp->us) ?
					       AF_INET :
					       ast_sockaddr_is_ipv6(&rtp->rtcp->us) ?
					       AF_INET6 : -1)) < 0) {
				ast_debug(1, "Failed to create a new socket for RTCP on instance '%p'\n", instance);
				ast_free(rtp->rtcp);
				rtp->rtcp = NULL;
				return;
			}

			/* Try to actually bind to the IP address and port we are going to use for RTCP, if this fails we have to bail out */
			if (ast_bind(rtp->rtcp->s, &rtp->rtcp->us)) {
				ast_debug(1, "Failed to setup RTCP on RTP instance '%p'\n", instance);
				close(rtp->rtcp->s);
				ast_free(rtp->rtcp);
				rtp->rtcp = NULL;
				return;
			}

			ast_debug(1, "Setup RTCP on RTP instance '%p'\n", instance);
			rtp->rtcp->schedid = -1;

#ifdef USE_PJPROJECT
			if (rtp->ice) {
				rtp_add_candidates_to_ice(instance, rtp, &rtp->rtcp->us, ast_sockaddr_port(&rtp->rtcp->us), AST_RTP_ICE_COMPONENT_RTCP, TRANSPORT_SOCKET_RTCP);
			}
#endif

#ifdef HAVE_OPENSSL_SRTP
			dtls_setup_rtcp(instance);
#endif

			return;
		} else {
			if (rtp->rtcp) {
				if (rtp->rtcp->schedid > 0) {
					if (!ast_sched_del(rtp->sched, rtp->rtcp->schedid)) {
						/* Successfully cancelled scheduler entry. */
						ao2_ref(instance, -1);
					} else {
						/* Unable to cancel scheduler entry */
						ast_debug(1, "Failed to tear down RTCP on RTP instance '%p'\n", instance);
						return;
					}
					rtp->rtcp->schedid = -1;
				}
				close(rtp->rtcp->s);
#ifdef HAVE_OPENSSL_SRTP
				if (rtp->rtcp->dtls.ssl) {
					SSL_free(rtp->rtcp->dtls.ssl);
				}
#endif
				ast_free(rtp->rtcp);
				rtp->rtcp = NULL;
			}
			return;
		}
	}

	return;
}

static int ast_rtp_fd(struct ast_rtp_instance *instance, int rtcp)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return rtcp ? (rtp->rtcp ? rtp->rtcp->s : -1) : rtp->s;
}

static void ast_rtp_remote_address_set(struct ast_rtp_instance *instance, struct ast_sockaddr *addr)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	if (rtp->rtcp) {
		ast_debug(1, "Setting RTCP address on RTP instance '%p'\n", instance);
		ast_sockaddr_copy(&rtp->rtcp->them, addr);
		if (!ast_sockaddr_isnull(addr)) {
			ast_sockaddr_set_port(&rtp->rtcp->them,
					      ast_sockaddr_port(addr) + 1);
		}
	}

	rtp->rxseqno = 0;

	if (strictrtp && rtp->strict_rtp_state != STRICT_RTP_OPEN) {
		rtp->strict_rtp_state = STRICT_RTP_LEARN;
		rtp_learning_seq_init(&rtp->rtp_source_learn, rtp->seqno);
	}

	return;
}

static void ast_rtp_alt_remote_address_set(struct ast_rtp_instance *instance, struct ast_sockaddr *addr)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* No need to futz with rtp->rtcp here because ast_rtcp_read is already able to adjust if receiving
	 * RTCP from an "unexpected" source
	 */
	ast_sockaddr_copy(&rtp->alt_rtp_address, addr);

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
	ast_format_set(&rtp->red->t140.subclass.format, AST_FORMAT_T140RED, 0);
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
		 (!ast_channel_tech(chan0)->send_digit_begin != !ast_channel_tech(chan1)->send_digit_begin)) ? 0 : 1);
}

static void ast_rtp_stun_request(struct ast_rtp_instance *instance, struct ast_sockaddr *suggestion, const char *username)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct sockaddr_in suggestion_tmp;

	ast_sockaddr_to_sin(suggestion, &suggestion_tmp);
	ast_stun_request(rtp->s, &suggestion_tmp, username, NULL);
	ast_sockaddr_from_sin(suggestion, &suggestion_tmp);
}

static void ast_rtp_stop(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr addr = { {0,} };

#ifdef HAVE_OPENSSL_SRTP
	AST_SCHED_DEL_UNREF(rtp->sched, rtp->rekeyid, ao2_ref(instance, -1));
	ast_mutex_lock(&rtp->dtls_timer_lock);
	AST_SCHED_DEL_UNREF(rtp->sched, rtp->dtlstimerid, ao2_ref(instance, -1));
	ast_mutex_unlock(&rtp->dtls_timer_lock);
#endif

	if (rtp->rtcp && rtp->rtcp->schedid > 0) {
		if (!ast_sched_del(rtp->sched, rtp->rtcp->schedid)) {
			/* successfully cancelled scheduler entry. */
			ao2_ref(instance, -1);
		}
		rtp->rtcp->schedid = -1;
	}

	if (rtp->red) {
		AST_SCHED_DEL(rtp->sched, rtp->red->schedid);
		free(rtp->red);
		rtp->red = NULL;
	}

	ast_rtp_instance_set_remote_address(instance, &addr);
	if (rtp->rtcp) {
		ast_sockaddr_setnull(&rtp->rtcp->them);
	}

	ast_set_flag(rtp, FLAG_NEED_MARKER_BIT);
}

static int ast_rtp_qos_set(struct ast_rtp_instance *instance, int tos, int cos, const char *desc)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	return ast_set_qos(rtp->s, tos, cos, desc);
}

/*! \brief generate comfort noice (CNG) */
static int ast_rtp_sendcng(struct ast_rtp_instance *instance, int level)
{
	unsigned int *rtpheader;
	int hdrlen = 12;
	int res, payload = 0;
	char data[256];
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);
	struct ast_sockaddr remote_address = { {0,} };
	int ice;

	ast_rtp_instance_get_remote_address(instance, &remote_address);

	if (ast_sockaddr_isnull(&remote_address)) {
		return -1;
	}

	payload = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance), 0, NULL, AST_RTP_CN);

	level = 127 - (level & 0x7f);
	
	rtp->dtmfmute = ast_tvadd(ast_tvnow(), ast_tv(0, 500000));

	/* Get a pointer to the header */
	rtpheader = (unsigned int *)data;
	rtpheader[0] = htonl((2 << 30) | (payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastts);
	rtpheader[2] = htonl(rtp->ssrc); 
	data[12] = level;

	res = rtp_sendto(instance, (void *) rtpheader, hdrlen + 1, 0, &remote_address, &ice);

	if (res < 0) {
		ast_log(LOG_ERROR, "RTP Comfort Noise Transmission error to %s: %s\n", ast_sockaddr_stringify(&remote_address), strerror(errno));
		return res;
	}

	if (rtp_debug_test_addr(&remote_address)) {
		ast_verbose("Sent Comfort Noise RTP packet to %s%s (type %-2.2d, seq %-6.6d, ts %-6.6u, len %-6.6d)\n",
			    ast_sockaddr_stringify(&remote_address),
			    ice ? " (via ICE)" : "",
			    AST_RTP_CN, rtp->seqno, rtp->lastdigitts, res - hdrlen);
	}

	rtp->seqno++;

	return res;
}

#ifdef HAVE_OPENSSL_SRTP
static int ast_rtp_activate(struct ast_rtp_instance *instance)
{
	struct ast_rtp *rtp = ast_rtp_instance_get_data(instance);

	/* If ICE negotiation is enabled the DTLS Handshake will be performed upon completion of it */
#ifdef USE_PJPROJECT
	if (rtp->ice) {
		return 0;
	}
#endif

	dtls_perform_handshake(instance, &rtp->dtls, 0);

	if (rtp->rtcp) {
		dtls_perform_handshake(instance, &rtp->rtcp->dtls, 1);
	}

	return 0;
}
#endif

static char *rtp_do_debug_ip(struct ast_cli_args *a)
{
	char *arg = ast_strdupa(a->argv[4]);
	char *debughost = NULL;
	char *debugport = NULL;

	if (!ast_sockaddr_parse(&rtpdebugaddr, arg, 0) || !ast_sockaddr_split_hostport(arg, &debughost, &debugport, 0)) {
		ast_cli(a->fd, "Lookup failed for '%s'\n", arg);
		return CLI_FAILURE;
	}
	rtpdebugport = (!ast_strlen_zero(debugport) && debugport[0] != '0');
	ast_cli(a->fd, "RTP Debugging Enabled for address: %s\n",
		ast_sockaddr_stringify(&rtpdebugaddr));
	rtpdebug = 1;
	return CLI_SUCCESS;
}

static char *rtcp_do_debug_ip(struct ast_cli_args *a)
{
	char *arg = ast_strdupa(a->argv[4]);
	char *debughost = NULL;
	char *debugport = NULL;

	if (!ast_sockaddr_parse(&rtcpdebugaddr, arg, 0) || !ast_sockaddr_split_hostport(arg, &debughost, &debugport, 0)) {
		ast_cli(a->fd, "Lookup failed for '%s'\n", arg);
		return CLI_FAILURE;
	}
	rtcpdebugport = (!ast_strlen_zero(debugport) && debugport[0] != '0');
	ast_cli(a->fd, "RTCP Debugging Enabled for address: %s\n",
		ast_sockaddr_stringify(&rtcpdebugaddr));
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
	strictrtp = DEFAULT_STRICT_RTP;
	learning_min_sequential = DEFAULT_LEARNING_MIN_SEQUENTIAL;

	/** This resource is not "reloaded" so much as unloaded and loaded again.
	 * In the case of the TURN related variables, the memory referenced by a
	 * previously loaded instance  *should* have been released when the
	 * corresponding pool was destroyed. If at some point in the future this
	 * resource were to support ACTUAL live reconfiguration and did NOT release
	 * the pool this will cause a small memory leak.
	 */

	icesupport = DEFAULT_ICESUPPORT;
	memset(&stunaddr, 0, sizeof(stunaddr));
#ifdef USE_PJPROJECT
	turnport = DEFAULT_TURN_PORT;
	turnaddr = pj_str(NULL);
	turnusername = pj_str(NULL);
	turnpassword = pj_str(NULL);
#endif

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
		if ((s = ast_variable_retrieve(cfg, "general", "probation"))) {
			if ((sscanf(s, "%d", &learning_min_sequential) <= 0) || learning_min_sequential <= 0) {
				ast_log(LOG_WARNING, "Value for 'probation' could not be read, using default of '%d' instead\n",
					DEFAULT_LEARNING_MIN_SEQUENTIAL);
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "icesupport"))) {
			icesupport = ast_true(s);
		}
		if ((s = ast_variable_retrieve(cfg, "general", "stunaddr"))) {
			stunaddr.sin_port = htons(STANDARD_STUN_PORT);
			if (ast_parse_arg(s, PARSE_INADDR, &stunaddr)) {
				ast_log(LOG_WARNING, "Invalid STUN server address: %s\n", s);
			}
		}
#ifdef USE_PJPROJECT
		if ((s = ast_variable_retrieve(cfg, "general", "turnaddr"))) {
			struct sockaddr_in addr;
			addr.sin_port = htons(DEFAULT_TURN_PORT);
			if (ast_parse_arg(s, PARSE_INADDR, &addr)) {
				ast_log(LOG_WARNING, "Invalid TURN server address: %s\n", s);
			} else {
				pj_strdup2_with_null(pool, &turnaddr, ast_inet_ntoa(addr.sin_addr));
				/* ntohs() is not a bug here. The port number is used in host byte order with
				 * a pjnat API. */
				turnport = ntohs(addr.sin_port);
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "turnusername"))) {
			pj_strdup2_with_null(pool, &turnusername, s);
		}
		if ((s = ast_variable_retrieve(cfg, "general", "turnpassword"))) {
			pj_strdup2_with_null(pool, &turnpassword, s);
		}
#endif
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

#ifdef USE_PJPROJECT
static void rtp_terminate_pjproject(void)
{
	pj_thread_register_check();

	if (timer_thread) {
		timer_terminate = 1;
		pj_thread_join(timer_thread);
		pj_thread_destroy(timer_thread);
	}

	pj_caching_pool_destroy(&cachingpool);
	pj_shutdown();
}
#endif

static int load_module(void)
{
#ifdef USE_PJPROJECT
	pj_lock_t *lock;

	pj_log_set_level(0);

	if (pj_init() != PJ_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjlib_util_init() != PJ_SUCCESS) {
		rtp_terminate_pjproject();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjnath_init() != PJ_SUCCESS) {
		rtp_terminate_pjproject();
		return AST_MODULE_LOAD_DECLINE;
	}

	pj_caching_pool_init(&cachingpool, &pj_pool_factory_default_policy, 0);

	pool = pj_pool_create(&cachingpool.factory, "rtp", 512, 512, NULL);

	if (pj_timer_heap_create(pool, 100, &timer_heap) != PJ_SUCCESS) {
		rtp_terminate_pjproject();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pj_lock_create_recursive_mutex(pool, "rtp%p", &lock) != PJ_SUCCESS) {
		rtp_terminate_pjproject();
		return AST_MODULE_LOAD_DECLINE;
	}

	pj_timer_heap_set_lock(timer_heap, lock, PJ_TRUE);

	if (pj_thread_create(pool, "ice", &timer_worker_thread, NULL, 0, 0, &timer_thread) != PJ_SUCCESS) {
		rtp_terminate_pjproject();
		return AST_MODULE_LOAD_DECLINE;
	}
#endif

	if (ast_rtp_engine_register(&asterisk_rtp_engine)) {
#ifdef USE_PJPROJECT
		rtp_terminate_pjproject();
#endif
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_cli_register_multiple(cli_rtp, ARRAY_LEN(cli_rtp))) {
#ifdef USE_PJPROJECT
		rtp_terminate_pjproject();
#endif
		return AST_MODULE_LOAD_DECLINE;
	}

	rtp_reload(0);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_rtp_engine_unregister(&asterisk_rtp_engine);
	ast_cli_unregister_multiple(cli_rtp, ARRAY_LEN(cli_rtp));

#ifdef USE_PJPROJECT
	rtp_terminate_pjproject();
#endif

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk RTP Stack",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
		);
