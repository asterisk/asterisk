/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Session Initiation Protocol
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/lock.h>
#include <asterisk/sched.h>
#include <asterisk/io.h>
#include <asterisk/rtp.h>
#include <asterisk/acl.h>
#include <asterisk/callerid.h>
#include <asterisk/cli.h>
#include <asterisk/md5.h>
#include <asterisk/app.h>
#include <asterisk/musiconhold.h>
#include <asterisk/dsp.h>
#include <asterisk/parking.h>
#include <asterisk/acl.h>
#include <asterisk/srv.h>
#include <asterisk/astdb.h>
#include <asterisk/causes.h>
#include <asterisk/utils.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/signal.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#ifdef SIP_MYSQL_FRIENDS
#define MYSQL_FRIENDS
#include <mysql/mysql.h>
#endif

#define VIDEO_CODEC_MASK        0x1fc0000 /* Video codecs from H.261 thru AST_FORMAT_MAX_VIDEO */
#ifndef IPTOS_MINCOST
#define IPTOS_MINCOST 0x02
#endif

/* #define VOCAL_DATA_HACK */

#define SIPDUMPER
#define DEFAULT_DEFAULT_EXPIRY  120
#define DEFAULT_MAX_EXPIRY      3600

/* guard limit must be larger than guard secs */
/* guard min must be < 1000, and should be >= 250 */
#define EXPIRY_GUARD_SECS	15	/* How long before expiry do we reregister */
#define EXPIRY_GUARD_LIMIT      30	/* Below here, we use EXPIRY_GUARD_PCT instead of EXPIRY_GUARD_SECS */
#define EXPIRY_GUARD_MIN	500	/* This is the minimum guard time applied. If GUARD_PCT turns out
					to be lower than this, it will use this time instead. This is in
					milliseconds. */
#define EXPIRY_GUARD_PCT        0.20	/* Percentage of expires timeout to use when below EXPIRY_GUARD_LIMIT */

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define CALLERID_UNKNOWN	"Unknown"

#define SIP_DTMF_RFC2833	(1 << 0)
#define SIP_DTMF_INBAND		(1 << 1)
#define SIP_DTMF_INFO		(1 << 2)

static int max_expiry = DEFAULT_MAX_EXPIRY;
static int default_expiry = DEFAULT_DEFAULT_EXPIRY;

#define DEFAULT_MAXMS		2000		/* Must be faster than 2 seconds by default */
#define DEFAULT_FREQ_OK		60 * 1000		/* How often to check for the host to be up */
#define DEFAULT_FREQ_NOTOK	10 * 1000		/* How often to check, if the host is down... */

#define DEFAULT_RETRANS		1000			/* How frequently to retransmit */
#define MAX_RETRANS			5				/* Try only 5 times for retransmissions */

#ifdef MYSQL_FRIENDS
static ast_mutex_t mysqllock = AST_MUTEX_INITIALIZER;
static MYSQL *mysql;
static char mydbuser[80];
static char mydbpass[80];
static char mydbhost[80];
static char mydbname[80];
#endif

							/* SIP Debug		*/
#define DEBUG_READ	0				/* Recieved data	*/
#define DEBUG_SEND	1				/* Transmit data	*/

static char *desc = "Session Initiation Protocol (SIP)";
static char *type = "SIP";
static char *tdesc = "Session Initiation Protocol (SIP)";
static char *config = "sip.conf";

#define DEFAULT_SIP_PORT	5060	/* From RFC 2543 */
#define SIP_MAX_PACKET	1500		/* Also from RFC 2543, should sub headers tho */

#define ALLOWED_METHODS "INVITE, ACK, CANCEL, OPTIONS, BYE, REFER"

static char context[AST_MAX_EXTENSION] = "default";

static char language[MAX_LANGUAGE] = "";

static char callerid[AST_MAX_EXTENSION] = "asterisk";

static char fromdomain[AST_MAX_EXTENSION] = "";

static char notifymime[AST_MAX_EXTENSION] = "application/simple-message-summary";

static int srvlookup = 0;

static int pedanticsipchecking = 0;

static int autocreatepeer = 0;

static int relaxdtmf = 0;

static int usecnt =0;
static ast_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;

/* Protect the interface list (of sip_pvt's) */
static ast_mutex_t iflock = AST_MUTEX_INITIALIZER;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
static ast_mutex_t netlock = AST_MUTEX_INITIALIZER;

static ast_mutex_t monlock = AST_MUTEX_INITIALIZER;

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

/* Codecs that we support by default: */
static int capability = AST_FORMAT_ULAW | AST_FORMAT_ALAW | AST_FORMAT_GSM | AST_FORMAT_H263;
static int noncodeccapability = AST_RTP_DTMF;

static char ourhost[256];
static struct in_addr __ourip;
static int ourport;

static int sipdebug = 0;
static struct sockaddr_in debugaddr;

static int tos = 0;

static int videosupport = 0;

static int globaldtmfmode = SIP_DTMF_RFC2833;

/* Expire slowly */
static int expiry = 900;

static struct sched_context *sched;
static struct io_context *io;
/* The private structures of the  sip channels are linked for
   selecting outgoing channels */
   
#define SIP_MAX_HEADERS		64
#define SIP_MAX_LINES 		64

#define DEC_IN_USE	0
#define INC_IN_USE	1
#define DEC_OUT_USE	2
#define INC_OUT_USE	3

static struct sip_codec_pref {
	int codec;
	struct sip_codec_pref *next;
} *prefs;

struct sip_request {
  char *rlPart1; /* SIP Method Name or "SIP/2.0" protocol version */
  char *rlPart2; /* The Request URI or Response Status */
	int len;
	int headers;					/* SIP Headers */
	char *header[SIP_MAX_HEADERS];
	int lines;						/* SDP Content */
	char *line[SIP_MAX_LINES];
	char data[SIP_MAX_PACKET];
};

struct sip_pkt;

struct sip_route {
	struct sip_route *next;
	char hop[0];
};

static struct sip_pvt {
	ast_mutex_t lock;				/* Channel private lock */
	char callid[80];					/* Global CallID */
	char randdata[80];	/* Random data */
	unsigned int ocseq;					/* Current outgoing seqno */
	unsigned int icseq;					/* Current incoming seqno */
	unsigned int callgroup;
	unsigned int pickupgroup;
	int lastinvite;						/* Last Cseq of invite */
	int alreadygone;					/* Whether or not we've already been destroyed by or peer */
	int needdestroy;					/* if we need to be destroyed */
	int capability;						/* Special capability */
	int jointcapability;				/* Supported capability at both ends */
	int prefcodec;						/* Preferred codec (outbound only) */
	int noncodeccapability;
	int outgoing;						/* Outgoing or incoming call? */
	int authtries;						/* Times we've tried to authenticate */
	int insecure;						/* Don't check source port/ip */
	int expiry;						/* How long we take to expire */
	int branch;							/* One random number */
	int canreinvite;					/* Do we support reinvite */
	int ringing;						/* Have sent 180 ringing */
	int progress;						/* Have sent 183 message progress */
	int tag;							/* Another random number */
	int nat;							/* Whether to try to support NAT */
	int sessionid;						/* SDP Session ID */
	int sessionversion;					/* SDP Session Version */
	struct sockaddr_in sa;				/* Our peer */
	struct sockaddr_in redirip;			/* Where our RTP should be going if not to us */
	struct sockaddr_in vredirip;		/* Where our Video RTP should be going if not to us */
	struct sockaddr_in recv;			/* Received as */
	struct in_addr ourip;				/* Our IP */
	struct ast_channel *owner;			/* Who owns us */
	char exten[AST_MAX_EXTENSION];		/* Extention where to start */
	char refer_to[AST_MAX_EXTENSION];	/* Place to store REFER-TO extension */
	char referred_by[AST_MAX_EXTENSION];/* Place to store REFERRED-BY extension */
	char refer_contact[AST_MAX_EXTENSION];/* Place to store Contact info from a REFER extension */
	struct sip_pvt *refer_call;			/* Call we are referring */
	struct sip_route *route;			/* Head of linked list of routing steps (fm Record-Route) */
	int route_persistant;				/* Is this the "real" route? */
	char remote_party_id[256];
	char from[256];
	char context[AST_MAX_EXTENSION];
	char fromdomain[AST_MAX_EXTENSION];	/* Domain to show in the from field */
	char fromuser[AST_MAX_EXTENSION];	/* Domain to show in the user field */
	char tohost[AST_MAX_EXTENSION];		/* Host we should put in the "to" field */
	char language[MAX_LANGUAGE];
	char rdnis[256];				/* Referring DNIS */
	char theirtag[256];				/* Their tag */
	char username[256];
	char peername[256];
	char authname[256];				/* Who we use for authentication */
	char uri[256];					/* Original requested URI */
	char peersecret[256];
	char peermd5secret[256];
	char callerid[256];					/* Caller*ID */
	int restrictcid;			/* hide presentation from remote user */
	char via[256];
	char accountcode[20];				/* Account code */
	char our_contact[256];				/* Our contact header */
	char realm[256];				/* Authorization realm */
	char nonce[256];				/* Authorization nonce */
	char opaque[256];				/* Opaque nonsense */
	char qop[80];				/* Quality of Protection, since SIP wasn't complicated enough yet. */
	char domain[256];				/* Authorization nonce */
	char lastmsg[256];				/* Last Message sent/received */
	int amaflags;						/* AMA Flags */
	int pendinginvite;					/* Any pending invite */
	int pendingbye;						/* Need to send bye after we ack? */
	int gotrefer;						/* Got a refer? */
	struct sip_request initreq;			/* Initial request */
	
	int maxtime;						/* Max time for first response */
	int initid;							/* Auto-congest ID if appropriate */
	int autokillid;						/* Auto-kill ID */

	int subscribed;
    	int stateid;
	int dialogver;
	
        int dtmfmode;
        struct ast_dsp *vad;
	
	struct sip_peer *peerpoke;			/* If this calls is to poke a peer, which one */
	struct sip_registry *registry;			/* If this is a REGISTER call, to which registry */
	struct ast_rtp *rtp;				/* RTP Session */
	struct ast_rtp *vrtp;				/* Video RTP session */
	struct sip_pkt *packets;			/* Packets scheduled for re-transmission */
	struct sip_pvt *next;
} *iflist = NULL;

#define FLAG_RESPONSE (1 << 0)
#define FLAG_FATAL (1 << 1)

struct sip_pkt {
	struct sip_pkt *next;				/* Next packet */
	int retrans;						/* Retransmission number */
	int seqno;							/* Sequence number */
	int flags;							/* non-zero if this is a response packet (e.g. 200 OK) */
	struct sip_pvt *owner;				/* Owner call */
	int retransid;						/* Retransmission ID */
	int packetlen;						/* Length of packet */
	char data[0];
};	

struct sip_user {
	/* Users who can access various contexts */
	char name[80];
	char secret[80];
        char md5secret[80];
	char context[80];
	char callerid[80];
	char methods[80];
	char accountcode[20];
	char language[MAX_LANGUAGE];
	unsigned int callgroup;
	unsigned int pickupgroup;
	int nat;
	int hascallerid;
	int amaflags;
	int insecure;
	int canreinvite;
	int capability;
	int dtmfmode;
	int inUse;
	int incominglimit;
	int outUse;
	int outgoinglimit;
	int restrictcid;
	struct ast_ha *ha;
	struct sip_user *next;
};

struct sip_peer {
	char name[80];
	char secret[80];
	char md5secret[80];
	char context[80];		/* JK02: peers need context too to allow parking etc */
	char methods[80];
	char username[80];
	char tohost[80];
	char fromuser[80];
	char fromdomain[80];
	char mailbox[AST_MAX_EXTENSION];
	int lastmsgssent;
	time_t	lastmsgcheck;
	int dynamic;
	int expire;
	int expiry;
	int capability;
	int insecure;
	int nat;
	int canreinvite;
	unsigned int callgroup;
	unsigned int pickupgroup;
        int dtmfmode;
	struct sockaddr_in addr;
	struct in_addr mask;

	/* Qualification */
	struct sip_pvt *call;		/* Call pointer */
	int pokeexpire;				/* When to expire poke */
	int lastms;					/* How long last response took (in ms), or -1 for no response */
	int maxms;					/* Max ms we will accept for the host to be up, 0 to not monitor */
	struct timeval ps;			/* Ping send time */
	
	struct sockaddr_in defaddr;
	struct ast_ha *ha;
	int delme;
	int selfdestruct;
	int lastmsg;
	int temponly;
	struct sip_peer *next;
};

static ast_mutex_t sip_reload_lock = AST_MUTEX_INITIALIZER;
static int sip_reloading = 0;

#define REG_STATE_UNREGISTERED 0
#define REG_STATE_REGSENT	   1
#define REG_STATE_AUTHSENT 	   2
#define REG_STATE_REGISTERED   3
#define REG_STATE_REJECTED	   4
#define REG_STATE_TIMEOUT	   5
#define REG_STATE_NOAUTH	   6

struct sip_registry {
	struct sockaddr_in addr;		/* Who we connect to for registration purposes */
	char username[80];				/* Who we are registering as */
	char authuser[80];				/* Who we *authenticate* as */
	char hostname[80];
	char secret[80];			/* Password or key name in []'s */	
	char md5secret[80];
	char contact[80];			/* Contact extension */
	char random[80];
	int expire;					/* Sched ID of expiration */
	int timeout; 					/* sched id of sip_reg_timeout */
	int refresh;					/* How often to refresh */
	struct sip_pvt *call;				/* create a sip_pvt structure for each outbound "registration call" in progress */
	int regstate;
	int callid_valid;		/* 0 means we haven't chosen callid for this registry yet. */
	char callid[80];		/* Global CallID for this registry */
	unsigned int ocseq;		/* Sequence number we got to for REGISTERs for this registry */
	struct sockaddr_in us;			/* Who the server thinks we are */
	struct sip_registry *next;
};

static struct ast_user_list {
	struct sip_user *users;
	ast_mutex_t lock;
} userl = { NULL, AST_MUTEX_INITIALIZER };

static struct ast_peer_list {
	struct sip_peer *peers;
	ast_mutex_t lock;
} peerl = { NULL, AST_MUTEX_INITIALIZER };

static struct ast_register_list {
	struct sip_registry *registrations;
	ast_mutex_t lock;
	int recheck;
} regl = { NULL, AST_MUTEX_INITIALIZER };


#define REINVITE_INVITE		1
#define REINVITE_UPDATE		2

static int __sip_do_register(struct sip_registry *r);

static int sipsock  = -1;
static int globalnat = 0;
static int globalcanreinvite = REINVITE_INVITE;


static struct sockaddr_in bindaddr;
static struct sockaddr_in localnet;
static struct sockaddr_in localmask;
static struct sockaddr_in externip;

static struct ast_frame  *sip_read(struct ast_channel *ast);
static int transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req);
static int transmit_response_with_sdp(struct sip_pvt *p, char *msg, struct sip_request *req, int retrans);
static int transmit_response_with_auth(struct sip_pvt *p, char *msg, struct sip_request *req, char *rand, int reliable);
static int transmit_request(struct sip_pvt *p, char *msg, int inc, int reliable, int newbranch);
static int transmit_request_with_auth(struct sip_pvt *p, char *msg, int inc, int reliable, int newbranch);
static int transmit_invite(struct sip_pvt *p, char *msg, int sendsdp, char *auth, char *authheader, char *vxml_url,char *distinctive_ring, int init);
static int transmit_reinvite_with_sdp(struct sip_pvt *p, struct ast_rtp *rtp, struct ast_rtp *vrtp);
static int transmit_info_with_digit(struct sip_pvt *p, char digit);
static int transmit_message_with_text(struct sip_pvt *p, char *text);
static int transmit_refer(struct sip_pvt *p, char *dest);
static struct sip_peer *temp_peer(char *name);
static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader, char *msg, int init);
/* static char *getsipuri(char *header); */
static void free_old_route(struct sip_route *route);
static int build_reply_digest(struct sip_pvt *p, char *orig_header, char *digest, int digest_len);
static int update_user_counter(struct sip_pvt *fup, int event);
static void prune_peers(void);
static int sip_do_reload(void);
static int sip_debug_test_addr(struct sockaddr_in *addr);
static int sip_debug_test_pvt(struct sip_pvt *p);

static int __sip_xmit(struct sip_pvt *p, char *data, int len)
{
	int res;
	if (p->nat)
	    res=sendto(sipsock, data, len, 0, (struct sockaddr *)&p->recv, sizeof(struct sockaddr_in));
	else
	    res=sendto(sipsock, data, len, 0, (struct sockaddr *)&p->sa, sizeof(struct sockaddr_in));
	if (res != len) {
		ast_log(LOG_WARNING, "sip_xmit of %p (len %d) to %s returned %d: %s\n", data, len, inet_ntoa(p->sa.sin_addr), res, strerror(errno));
	}
	return res;
}

static void sip_destroy(struct sip_pvt *p);

static int ast_sip_ouraddrfor(struct in_addr *them, struct in_addr *us)
{
	/*
	  check to see if them is contained in our localnet/mask,
	  if not, use our externip for us, otherwise use the 
	  real internal address in bindaddr
         */
	if (localnet.sin_addr.s_addr && externip.sin_addr.s_addr &&
      	    ((htonl(them->s_addr) & htonl(localnet.sin_addr.s_addr)) != htonl(localnet.sin_addr.s_addr)))
		memcpy(us, &externip.sin_addr, sizeof(struct in_addr));
	else if (bindaddr.sin_addr.s_addr)
		memcpy(us, &bindaddr.sin_addr, sizeof(struct in_addr));
	else
		return ast_ouraddrfor(them, us);
	return 0;
}

static int retrans_pkt(void *data)
{
	struct sip_pkt *pkt=data;
	int res = 0;
	ast_mutex_lock(&pkt->owner->lock);
	if (pkt->retrans < MAX_RETRANS) {
		pkt->retrans++;
		if (sip_debug_test_pvt(pkt->owner)) {
			if (pkt->owner->nat)
				ast_verbose("Retransmitting #%d (NAT):\n%s\n to %s:%d\n", pkt->retrans, pkt->data, inet_ntoa(pkt->owner->recv.sin_addr), ntohs(pkt->owner->recv.sin_port));
			else
				ast_verbose("Retransmitting #%d (no NAT):\n%s\n to %s:%d\n", pkt->retrans, pkt->data, inet_ntoa(pkt->owner->sa.sin_addr), ntohs(pkt->owner->sa.sin_port));
		}
		__sip_xmit(pkt->owner, pkt->data, pkt->packetlen);
		res = 1;
	} else {
		ast_log(LOG_WARNING, "Maximum retries exceeded on call %s for seqno %d (%s %s)\n", pkt->owner->callid, pkt->seqno, (pkt->flags & FLAG_FATAL) ? "Critical" : "Non-critical", (pkt->flags & FLAG_RESPONSE) ? "Response" : "Request");
		pkt->retransid = -1;
		if (pkt->flags & FLAG_FATAL) {
			while(pkt->owner->owner && ast_mutex_trylock(&pkt->owner->owner->lock)) {
				ast_mutex_unlock(&pkt->owner->lock);
				usleep(1);
				ast_mutex_lock(&pkt->owner->lock);
			}
			if (pkt->owner->owner) {
				/* XXX Potential deadlocK?? XXX */
				ast_queue_hangup(pkt->owner->owner);
				ast_mutex_unlock(&pkt->owner->owner->lock);
			} else {
				/* If no owner, destroy now */
				pkt->owner->needdestroy = 1;
			}
		} else {
			/* Okay, it's not fatal, just continue.  XXX If we were nice, we'd free it now, rather than wait for the
			   end of the call XXX */
		}
	}
	if (pkt)
		ast_mutex_unlock(&pkt->owner->lock);
	return res;
}

static int __sip_reliable_xmit(struct sip_pvt *p, int seqno, int resp, char *data, int len, int fatal)
{
	struct sip_pkt *pkt;
	pkt = malloc(sizeof(struct sip_pkt) + len);
	if (!pkt)
		return -1;
	memset(pkt, 0, sizeof(struct sip_pkt));
	memcpy(pkt->data, data, len);
	pkt->packetlen = len;
	pkt->next = p->packets;
	pkt->owner = p;
	pkt->seqno = seqno;
	pkt->flags = resp;
	if (fatal)
		pkt->flags |= FLAG_FATAL;
	/* Schedule retransmission */
	pkt->retransid = ast_sched_add(sched, DEFAULT_RETRANS, retrans_pkt, pkt);
	pkt->next = p->packets;
	p->packets = pkt;
	__sip_xmit(pkt->owner, pkt->data, pkt->packetlen);
	if (!strncasecmp(pkt->data, "INVITE", 6)) {
		/* Note this is a pending invite */
		p->pendinginvite = seqno;
	}
	return 0;
}

static int __sip_autodestruct(void *data)
{
	struct sip_pvt *p = data;
	p->autokillid = -1;
	ast_log(LOG_DEBUG, "Auto destroying call '%s'\n", p->callid);
	if (p->owner) {
		ast_log(LOG_WARNING, "Autodestruct on call '%s' with owner in place\n", p->callid);
		ast_queue_hangup(p->owner);
	} else {
		sip_destroy(p);
	}
	return 0;
}

static int sip_scheddestroy(struct sip_pvt *p, int ms)
{
	if (sip_debug_test_pvt(p))
		ast_verbose("Scheduling destruction of call '%s' in %d ms\n", p->callid, ms);
	if (p->autokillid > -1)
		ast_sched_del(sched, p->autokillid);
	p->autokillid = ast_sched_add(sched, ms, __sip_autodestruct, p);
	return 0;
}

static int sip_cancel_destroy(struct sip_pvt *p)
{
	if (p->autokillid > -1)
		ast_sched_del(sched, p->autokillid);
	p->autokillid = -1;
	return 0;
}

static int __sip_ack(struct sip_pvt *p, int seqno, int resp)
{
	struct sip_pkt *cur, *prev = NULL;
	int res = -1;
	int resetinvite = 0;
	cur = p->packets;
	while(cur) {
		if ((cur->seqno == seqno) && ((cur->flags & FLAG_RESPONSE) == resp)) {
			if (!resp && (seqno == p->pendinginvite)) {
				ast_log(LOG_DEBUG, "Acked pending invite %d\n", p->pendinginvite);
				p->pendinginvite = 0;
				resetinvite = 1;
			}
			/* this is our baby */
			if (prev)
				prev->next = cur->next;
			else
				p->packets = cur->next;
			if (cur->retransid > -1)
				ast_sched_del(sched, cur->retransid);
			free(cur);
			res = 0;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	ast_log(LOG_DEBUG, "Stopping retransmission on '%s' of %s %d: %s\n", p->callid, resp ? "Response" : "Request", seqno, res ? "Not Found" : "Found");
	return res;
}

static int __sip_semi_ack(struct sip_pvt *p, int seqno, int resp)
{
	struct sip_pkt *cur;
	int res = -1;
	cur = p->packets;
	while(cur) {
		if ((cur->seqno == seqno) && ((cur->flags & FLAG_RESPONSE) == resp)) {
			/* this is our baby */
			if (cur->retransid > -1)
				ast_sched_del(sched, cur->retransid);
			cur->retransid = -1;
			/* Schedule destruction in 15000ms just in case */
			sip_scheddestroy(p, 15000);
			res = 0;
			break;
		}
		cur = cur->next;
	}
	ast_log(LOG_DEBUG, "(Provisional) Stopping retransmission (but retaining packet) on '%s' %s %d: %s\n", p->callid, resp ? "Response" : "Request", seqno, res ? "Not Found" : "Found");
	return res;
}

static int send_response(struct sip_pvt *p, struct sip_request *req, int reliable, int seqno)
{
	int res;
	if (sip_debug_test_pvt(p)) {
		if (p->nat)
			ast_verbose("%sTransmitting (NAT):\n%s\n to %s:%d\n", reliable ? "Reliably " : "", req->data, inet_ntoa(p->recv.sin_addr), ntohs(p->recv.sin_port));
		else
			ast_verbose("%sTransmitting (no NAT):\n%s\n to %s:%d\n", reliable ? "Reliably " : "", req->data, inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	}
	if (reliable)
		res = __sip_reliable_xmit(p, seqno, 1, req->data, req->len, (reliable > 1));
	else
		res = __sip_xmit(p, req->data, req->len);
	if (res > 0)
		res = 0;
	return res;
}

static int send_request(struct sip_pvt *p, struct sip_request *req, int reliable, int seqno)
{
	int res;
	if (sip_debug_test_pvt(p)) {
		if (p->nat)
			ast_verbose("%sTransmitting:\n%s (NAT) to %s:%d\n", reliable ? "Reliably " : "", req->data, inet_ntoa(p->recv.sin_addr), ntohs(p->recv.sin_port));
		else
			ast_verbose("%sTransmitting:\n%s (no NAT) to %s:%d\n", reliable ? "Reliably " : "", req->data, inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	}
	if (reliable)
		res = __sip_reliable_xmit(p, seqno, 0, req->data, req->len, (reliable > 1));
	else
		res = __sip_xmit(p, req->data, req->len);
	return res;
}

static char *ditch_braces(char *tmp)
{
	char *c = tmp;
	char *n;
	if ((n = strchr(tmp, '<')) ) {
		c = n + 1;
		while(*c && *c != '>') c++;
		if (*c != '>') {
			ast_log(LOG_WARNING, "No closing brace in '%s'\n", tmp);
		} else {
			*c = '\0';
		}
		return n+1;
	}
	return c;
}

static int sip_sendtext(struct ast_channel *ast, char *text)
{
	struct sip_pvt *p = ast->pvt->pvt;
	if (sip_debug_test_pvt(p))
		ast_verbose("Sending text %s on %s\n", text, ast->name);
	if (!p)
		return -1;
	if (!text || ast_strlen_zero(text))
		return 0;
	if (sip_debug_test_pvt(p))
		ast_verbose("Really sending text %s on %s\n", text, ast->name);
	transmit_message_with_text(p, text);
	return 0;	
}

#ifdef MYSQL_FRIENDS

static void mysql_update_peer(char *peer, struct sockaddr_in *sin, char *username, int expiry)
{
	if (mysql && (strlen(peer) < 128)) {
		char query[512];
		char *name;
		char *uname;
		time_t nowtime;
		name = alloca(strlen(peer) * 2 + 1);
		uname = alloca(strlen(username) * 2 + 1);
		time(&nowtime);
		mysql_real_escape_string(mysql, name, peer, strlen(peer));
		mysql_real_escape_string(mysql, uname, username, strlen(username));
		snprintf(query, sizeof(query), "UPDATE sipfriends SET ipaddr=\"%s\", port=\"%d\", regseconds=\"%ld\", username=\"%s\" WHERE name=\"%s\"", 
			inet_ntoa(sin->sin_addr), ntohs(sin->sin_port), nowtime + expiry, uname, name);
		ast_mutex_lock(&mysqllock);
		if (mysql_real_query(mysql, query, strlen(query))) 
			ast_log(LOG_WARNING, "Unable to update database\n");
			
		ast_mutex_unlock(&mysqllock);
	}
}

static struct sip_peer *mysql_peer(char *peer, struct sockaddr_in *sin)
{
	struct sip_peer *p;
	int success = 0;
	
	p = malloc(sizeof(struct sip_peer));
	memset(p, 0, sizeof(struct sip_peer));
	if (mysql && (!peer || (strlen(peer) < 128))) {
		char query[512];
		char *name = NULL;
		int numfields, x;
		int port;
		time_t regseconds, nowtime;
		MYSQL_RES *result;
		MYSQL_FIELD *fields;
		MYSQL_ROW rowval;
		if (peer) {
			name = alloca(strlen(peer) * 2 + 1);
			mysql_real_escape_string(mysql, name, peer, strlen(peer));
		}
		if (sin)
			snprintf(query, sizeof(query), "SELECT name, secret, context, username, ipaddr, port, regseconds FROM sipfriends WHERE ipaddr=\"%s\" AND port=\"%d\"", inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
		else
			snprintf(query, sizeof(query), "SELECT name, secret, context, username, ipaddr, port, regseconds FROM sipfriends WHERE name=\"%s\"", name);
		ast_mutex_lock(&mysqllock);
		mysql_query(mysql, query);
		if ((result = mysql_store_result(mysql))) {
			if ((rowval = mysql_fetch_row(result))) {
				numfields = mysql_num_fields(result);
				fields = mysql_fetch_fields(result);
				success = 1;
				for (x=0;x<numfields;x++) {
					if (rowval[x]) {
						if (!strcasecmp(fields[x].name, "secret")) {
							strncpy(p->secret, rowval[x], sizeof(p->secret));
						} else if (!strcasecmp(fields[x].name, "name")) {
							strncpy(p->name, rowval[x], sizeof(p->name) - 1);
						} else if (!strcasecmp(fields[x].name, "context")) {
							strncpy(p->context, rowval[x], sizeof(p->context) - 1);
						} else if (!strcasecmp(fields[x].name, "username")) {
							strncpy(p->username, rowval[x], sizeof(p->username) - 1);
						} else if (!strcasecmp(fields[x].name, "ipaddr")) {
							inet_aton(rowval[x], &p->addr.sin_addr);
						} else if (!strcasecmp(fields[x].name, "port")) {
							if (sscanf(rowval[x], "%i", &port) != 1)
								port = 0;
							p->addr.sin_port = htons(port);
						} else if (!strcasecmp(fields[x].name, "regseconds")) {
							if (sscanf(rowval[x], "%li", &regseconds) != 1)
								regseconds = 0;
						}
					}
				}
				time(&nowtime);
				if (nowtime > regseconds) 
					memset(&p->addr, 0, sizeof(p->addr));
			}
			mysql_free_result(result);
			result = NULL;
		}
		ast_mutex_unlock(&mysqllock);
	}
	if (!success) {
		free(p);
		p = NULL;
	} else {
		p->dynamic = 1;
		p->capability = capability;
		p->nat = globalnat;
		p->dtmfmode = globaldtmfmode;
		p->insecure = 1;
		p->expire = -1;
		p->temponly = 1;
		
	}
	return p;
}
#endif /* MYSQL_FRIENDS */

static void update_peer(struct sip_peer *p, int expiry)
{
#ifdef MYSQL_FRIENDS
	if (p->temponly)
		mysql_update_peer(p->name, &p->addr, p->username, expiry);
#endif
	return;
}

static struct sip_peer *find_peer(char *peer, struct sockaddr_in *sin)
{
	struct sip_peer *p = NULL;

	p = peerl.peers;
	if (peer) {
		/* Find by peer name */
		while(p) {
			if (!strcasecmp(p->name, peer)) {
				break;
			}
			p = p->next;
		}	
	}
	else {
		/* Find by sin */
		while(p) {
			if (!inaddrcmp(&p->addr, sin) || 
					(p->insecure &&
					(p->addr.sin_addr.s_addr == sin->sin_addr.s_addr))) {
				break;
			}
			p = p->next;
		}
	}

#ifdef MYSQL_FRIENDS
	if (!p) {
		p = mysql_peer(peer, sin);
	}
#endif

	return(p);
}

static struct sip_user *find_user(char *name)
{
	struct sip_user *u = NULL;

	u = userl.users;
	while(u) {
		if (!strcasecmp(u->name, name)) {
			break;
		}
		u = u->next;
	}

	return(u);
}

static int sip_debug_test_addr(struct sockaddr_in *addr) {
	/* See if we pass debug IP filter */
	if (sipdebug == 0) return 0;
	if (debugaddr.sin_addr.s_addr) {
		if (((ntohs(debugaddr.sin_port) != 0) &&
		     (debugaddr.sin_port != addr->sin_port)) ||
		    (debugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
			return 0;
	}
	return 1;
}

static int sip_debug_test_pvt(struct sip_pvt *p) {
	return (sipdebug && sip_debug_test_addr((p->nat ? &p->recv : &p->sa)));
}

static int create_addr(struct sip_pvt *r, char *peer)
{
	struct hostent *hp;
	struct ast_hostent ahp;
	struct sip_peer *p;
	int found=0;
	char *port;
	int portno;
	char host[256], *hostn;

	r->sa.sin_family = AF_INET;
	ast_mutex_lock(&peerl.lock);
	p = find_peer(peer, NULL);

	if (p) {
			found++;
			r->capability = p->capability;
			r->nat = p->nat;
			if (r->rtp) {
				ast_log(LOG_DEBUG, "Setting NAT on RTP to %d\n", r->nat);
				ast_rtp_setnat(r->rtp, r->nat);
			}
			if (r->vrtp) {
				ast_log(LOG_DEBUG, "Setting NAT on VRTP to %d\n", r->nat);
				ast_rtp_setnat(r->vrtp, r->nat);
			}
			strncpy(r->peername, p->username, sizeof(r->peername)-1);
			strncpy(r->authname, p->username, sizeof(r->authname)-1);
			strncpy(r->peersecret, p->secret, sizeof(r->peersecret)-1);
			strncpy(r->peermd5secret, p->md5secret, sizeof(r->peermd5secret)-1);
			strncpy(r->username, p->username, sizeof(r->username)-1);
			strncpy(r->tohost, p->tohost, sizeof(r->tohost)-1);
			if (ast_strlen_zero(r->tohost)) {
				if (p->addr.sin_addr.s_addr)
					snprintf(r->tohost, sizeof(r->tohost), inet_ntoa(p->addr.sin_addr));
				else
					snprintf(r->tohost, sizeof(r->tohost), inet_ntoa(p->defaddr.sin_addr));
			}
			if (!ast_strlen_zero(p->fromdomain))
				strncpy(r->fromdomain, p->fromdomain, sizeof(r->fromdomain)-1);
			if (!ast_strlen_zero(p->fromuser))
				strncpy(r->fromuser, p->fromuser, sizeof(r->fromuser)-1);
			r->insecure = p->insecure;
			r->canreinvite = p->canreinvite;
			r->maxtime = p->maxms;
			r->callgroup = p->callgroup;
			r->pickupgroup = p->pickupgroup;
			if (p->dtmfmode) {
				r->dtmfmode = p->dtmfmode;
				if (r->dtmfmode & SIP_DTMF_RFC2833)
					r->noncodeccapability |= AST_RTP_DTMF;
				else
					r->noncodeccapability &= ~AST_RTP_DTMF;
			}
			strncpy(r->context, p->context,sizeof(r->context)-1);
			if ((p->addr.sin_addr.s_addr || p->defaddr.sin_addr.s_addr) &&
				(!p->maxms || ((p->lastms > 0)  && (p->lastms <= p->maxms)))) {
				if (p->addr.sin_addr.s_addr) {
					r->sa.sin_addr = p->addr.sin_addr;
					r->sa.sin_port = p->addr.sin_port;
				} else {
					r->sa.sin_addr = p->defaddr.sin_addr;
					r->sa.sin_port = p->defaddr.sin_port;
				}
				memcpy(&r->recv, &r->sa, sizeof(r->recv));
			} else {
				if (p->temponly) {
					if (p->ha) {
						ast_free_ha(p->ha);
					}
					free(p);
				}
				p = NULL;
			}
	}
	ast_mutex_unlock(&peerl.lock);
	if (!p && !found) {
		if ((port=strchr(peer, ':'))) {
			*port='\0';
			port++;
		}
		hostn = peer;
		if (port)
			portno = atoi(port);
		else
			portno = DEFAULT_SIP_PORT;
		if (srvlookup) {
			char service[256];
			int tportno;
			int ret;
			snprintf(service, sizeof(service), "_sip._udp.%s", peer);
			ret = ast_get_srv(NULL, host, sizeof(host), &tportno, service);
			if (ret > 0) {
				hostn = host;
				portno = tportno;
			}
		}
		hp = ast_gethostbyname(hostn, &ahp);
		if (hp) {
			strncpy(r->tohost, peer, sizeof(r->tohost) - 1);
			memcpy(&r->sa.sin_addr, hp->h_addr, sizeof(r->sa.sin_addr));
			r->sa.sin_port = htons(portno);
			memcpy(&r->recv, &r->sa, sizeof(r->recv));
			return 0;
		} else {
			ast_log(LOG_WARNING, "No such host: %s\n", peer);
			return -1;
		}
	} else if (!p)
		return -1;
	else {
		if (p->temponly) {
			if (p->ha) {
				ast_free_ha(p->ha);
			}
			free(p);
		}
		return 0;
	}
}

static int auto_congest(void *nothing)
{
	struct sip_pvt *p = nothing;
	ast_mutex_lock(&p->lock);
	p->initid = -1;
	if (p->owner) {
		if (!ast_mutex_trylock(&p->owner->lock)) {
			ast_log(LOG_NOTICE, "Auto-congesting %s\n", p->owner->name);
			ast_queue_control(p->owner, AST_CONTROL_CONGESTION);
			ast_mutex_unlock(&p->owner->lock);
		}
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}

static void sip_prefs_free(void)
{
	struct sip_codec_pref *cur, *next;
	cur = prefs;
	while(cur) {
		next = cur->next;
		free(cur);
		cur = next;
	}
	prefs = NULL;
}

static void sip_pref_remove(int format)
{
	struct sip_codec_pref *cur, *prev=NULL;
	cur = prefs;
	while(cur) {
		if (cur->codec == format) {
			if (prev)
				prev->next = cur->next;
			else
				prefs = cur->next;
			free(cur);
			return;
		}
		prev = cur;
		cur = cur->next;
	}
}

static int sip_pref_append(int format)
{
	struct sip_codec_pref *cur, *tmp;
	sip_pref_remove(format);
	tmp = (struct sip_codec_pref *)malloc(sizeof(struct sip_codec_pref));
	if (!tmp)
		return -1;
	memset(tmp, 0, sizeof(struct sip_codec_pref));
	tmp->codec = format;
	if (prefs) {
		cur = prefs;
		while(cur->next)
			cur = cur->next;
		cur->next = tmp;
	} else
		prefs = tmp;
	return 0;
}

static int sip_codec_choose(int formats)
{
	struct sip_codec_pref *cur;
	formats &= ((AST_FORMAT_MAX_AUDIO << 1) - 1);
	cur = prefs;
	while(cur) {
		if (formats & cur->codec)
			return cur->codec;
		cur = cur->next;
	}
	return ast_best_codec(formats);
}

static int sip_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res;
	struct sip_pvt *p;
	char *vxml_url = NULL;
	char *distinctive_ring = NULL;
	struct varshead *headp;
	struct ast_var_t *current;
	
	p = ast->pvt->pvt;
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "sip_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* Check whether there is vxml_url, distinctive ring variables */

	headp=&ast->varshead;
	AST_LIST_TRAVERSE(headp,current,entries) {
		/* Check whether there is a VXML_URL variable */
		if (strcasecmp(ast_var_name(current),"VXML_URL")==0)
	        {
			vxml_url = ast_var_value(current);
			break;
		}
		/* Check whether there is a ALERT_INFO variable */
		if (strcasecmp(ast_var_name(current),"ALERT_INFO")==0)
	        {
			distinctive_ring = ast_var_value(current);
			break;
		}
	}
	
	res = 0;
	p->outgoing = 1;
	ast_log(LOG_DEBUG, "Outgoing Call for %s\n", p->username);
	res = update_user_counter(p,INC_OUT_USE);
	if ( res != -1 ) {
		p->restrictcid = ast->restrictcid;
		p->jointcapability = p->capability;
		transmit_invite(p, "INVITE", 1, NULL, NULL, vxml_url,distinctive_ring, 1);
		if (p->maxtime) {
			/* Initialize auto-congest time */
			p->initid = ast_sched_add(sched, p->maxtime * 4, auto_congest, p);
		}
	}
	return res;
}

static void __sip_destroy(struct sip_pvt *p, int lockowner)
{
	struct sip_pvt *cur, *prev = NULL;
	struct sip_pkt *cp;
	if (sip_debug_test_pvt(p))
		ast_verbose("Destroying call '%s'\n", p->callid);
	if (p->stateid > -1)
		ast_extension_state_del(p->stateid, NULL);
	if (p->initid > -1)
		ast_sched_del(sched, p->initid);
	if (p->autokillid > -1)
		ast_sched_del(sched, p->autokillid);

	if (p->rtp) {
		ast_rtp_destroy(p->rtp);
	}
	if (p->vrtp) {
		ast_rtp_destroy(p->vrtp);
	}
	if (p->route) {
		free_old_route(p->route);
		p->route = NULL;
	}
	if (p->registry) {
		/* Carefully unlink from registry */
		struct sip_registry *reg;
		ast_mutex_lock(&regl.lock);
		reg = regl.registrations;
		while(reg) {
			if ((reg == p->registry) && (p->registry->call == p))
				p->registry->call=NULL;
			reg = reg->next;
		}
		ast_mutex_unlock(&regl.lock);
	}
	/* Unlink us from the owner if we have one */
	if (p->owner) {
		if (lockowner)
			ast_mutex_lock(&p->owner->lock);
		ast_log(LOG_DEBUG, "Detaching from %s\n", p->owner->name);
		p->owner->pvt->pvt = NULL;
		if (lockowner)
			ast_mutex_unlock(&p->owner->lock);
	}
	cur = iflist;
	while(cur) {
		if (cur == p) {
			if (prev)
				prev->next = cur->next;
			else
				iflist = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	if (!cur) {
		ast_log(LOG_WARNING, "%p is not in list?!?! \n", cur);
	} else {
		if (p->initid > -1)
			ast_sched_del(sched, p->initid);
		while((cp = p->packets)) {
			p->packets = p->packets->next;
			if (cp->retransid > -1)
				ast_sched_del(sched, cp->retransid);
			free(cp);
		}
		free(p);
	}
}

static int update_user_counter(struct sip_pvt *fup, int event)
{
	char name[256] = "";
	struct sip_user *u;
	strncpy(name, fup->username, sizeof(name) - 1);
	ast_mutex_lock(&userl.lock);
	u = find_user(name);
	if (!u) {
		ast_log(LOG_DEBUG, "%s is not a local user\n", name);
		ast_mutex_unlock(&userl.lock);
		return 0;
	}
	switch(event) {
		/* incoming and outgoing affects the inUse counter */
		case DEC_OUT_USE:
		case DEC_IN_USE:
			if ( u->inUse > 0 ) {
				u->inUse--;
			} else {
				u->inUse = 0;
			}
			break;
		case INC_IN_USE:
		case INC_OUT_USE:
			if (u->incominglimit > 0 ) {
				if (u->inUse >= u->incominglimit) {
					ast_log(LOG_ERROR, "Call from user '%s' rejected due to usage limit of %d\n", u->name, u->incominglimit);
					/* inc inUse as well */
					if ( event == INC_OUT_USE ) {
						u->inUse++;
					}
					ast_mutex_unlock(&userl.lock);
					return -1; 
				}
			}
			u->inUse++;
			ast_log(LOG_DEBUG, "Call from user '%s' is %d out of %d\n", u->name, u->inUse, u->incominglimit);
			break;
		/* we don't use these anymore
		case DEC_OUT_USE:
			if ( u->outUse > 0 ) {
				u->outUse--;
			} else {
				u->outUse = 0;
			}
			break;
		case INC_OUT_USE:
			if ( u->outgoinglimit > 0 ) {
				if ( u->outUse >= u->outgoinglimit ) {
					ast_log(LOG_ERROR, "Outgoing call from user '%s' rejected due to usage limit of %d\n", u->name, u->outgoinglimit);
					ast_mutex_unlock(&userl.lock);
					return -1;
				}
			}
			u->outUse++;
			break;
		*/
		default:
			ast_log(LOG_ERROR, "update_user_counter(%s,%d) called with no event!\n",u->name,event);
	}
	ast_mutex_unlock(&userl.lock);
	return 0;
}

static void sip_destroy(struct sip_pvt *p)
{
	ast_mutex_lock(&iflock);
	__sip_destroy(p, 1);
	ast_mutex_unlock(&iflock);
}

static int transmit_response_reliable(struct sip_pvt *p, char *msg, struct sip_request *req, int fatal);

static int hangup_sip2cause(int cause)
{
	switch(cause)
	{
		case 486:
			return AST_CAUSE_BUSY;
		default:
			return AST_CAUSE_NORMAL;
	}
	/* Never reached */
	return 0;
}

static char *hangup_cause2sip(int cause)
{
	switch(cause)
	{
		case AST_CAUSE_BUSY:
			return "486 Busy";
		default:
			return NULL;
	}
	/* Never reached */
	return 0;
}

static int sip_hangup(struct ast_channel *ast)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int needcancel = 0;
	int needdestroy = 0;
	if (option_debug)
		ast_log(LOG_DEBUG, "sip_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_DEBUG, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_mutex_lock(&p->lock);
	if ( p->outgoing ) {
		ast_log(LOG_DEBUG, "update_user_counter(%s) - decrement outUse counter\n", p->username);
		update_user_counter(p, DEC_OUT_USE);
	} else {
		ast_log(LOG_DEBUG, "update_user_counter(%s) - decrement inUse counter\n", p->username);
		update_user_counter(p, DEC_IN_USE);
	}
	/* Determine how to disconnect */
	if (p->owner != ast) {
		ast_log(LOG_WARNING, "Huh?  We aren't the owner?\n");
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	if (!ast || (ast->_state != AST_STATE_UP))
		needcancel = 1;
	/* Disconnect */
	p = ast->pvt->pvt;
        if (p->vad) {
            ast_dsp_free(p->vad);
        }
	p->owner = NULL;
	ast->pvt->pvt = NULL;

	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	needdestroy = 1;
	/* Start the process if it's not already started */
	if (!p->alreadygone && !ast_strlen_zero(p->initreq.data)) {
		if (needcancel) {
			if (p->outgoing) {
				transmit_request_with_auth(p, "CANCEL", p->ocseq, 1, 0);
				/* Actually don't destroy us yet, wait for the 487 on our original 
				   INVITE, but do set an autodestruct just in case. */
				needdestroy = 0;
				sip_scheddestroy(p, 15000);
				if ( p->initid != -1 ) {
					/* channel still up - reverse dec of inUse counter
					   only if the channel is not auto-congested */
					if ( p->outgoing ) {
						update_user_counter(p, INC_OUT_USE);
					}
					else {
						update_user_counter(p, INC_IN_USE);
					}
				}
			} else {
				char *res;
				if (ast->hangupcause && ((res = hangup_cause2sip(ast->hangupcause)))) {
					transmit_response_reliable(p, res, &p->initreq, 1);
				} else 
					transmit_response_reliable(p, "403 Forbidden", &p->initreq, 1);
			}
		} else {
			if (!p->pendinginvite) {
				/* Send a hangup */
				transmit_request_with_auth(p, "BYE", 0, 1, 1);
			} else {
				/* Note we will need a BYE when this all settles out
				   but we can't send one while we have "INVITE" outstanding. */
				p->pendingbye = 1;
			}
		}
	}
	p->needdestroy = needdestroy;
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int sip_answer(struct ast_channel *ast)
{
	int res = 0,fmt;
	char *codec;
	struct sip_pvt *p = ast->pvt->pvt;

	
	if (ast->_state != AST_STATE_UP) {
	
	
	
		codec=pbx_builtin_getvar_helper(p->owner,"SIP_CODEC");
		if (codec) {
			fmt=ast_getformatbyname(codec);
			if (fmt) {
				ast_log(LOG_NOTICE, "Changing codec to '%s' for this call because of ${SIP_CODEC) variable\n",codec);
				p->jointcapability=fmt;
			} else ast_log(LOG_NOTICE, "Ignoring ${SIP_CODEC} variable because of unrecognized/not configured codec (check allow/disallow in sip.conf): %s\n",codec);
		}

		ast_setstate(ast, AST_STATE_UP);
		if (option_debug)
			ast_log(LOG_DEBUG, "sip_answer(%s)\n", ast->name);
		res = transmit_response_with_sdp(p, "200 OK", &p->initreq, 1);
	}
	return res;
}

static int sip_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int res = 0;
	if (frame->frametype == AST_FRAME_VOICE) {
		if (!(frame->subclass & ast->nativeformats)) {
			ast_log(LOG_WARNING, "Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
				frame->subclass, ast->nativeformats, ast->readformat, ast->writeformat);
			return -1;
		}
		if (p) {
			ast_mutex_lock(&p->lock);
			if (p->rtp) {
				if ((ast->_state != AST_STATE_UP) && !p->progress && !p->outgoing) {
					transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
					p->progress = 1;
				}
				res =  ast_rtp_write(p->rtp, frame);
			}
			ast_mutex_unlock(&p->lock);
		}
	} else if (frame->frametype == AST_FRAME_VIDEO) {
		if (p) {
			ast_mutex_lock(&p->lock);
			if (p->vrtp) {
				if ((ast->_state != AST_STATE_UP) && !p->progress && !p->outgoing) {
					transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
					p->progress = 1;
				}
				res =  ast_rtp_write(p->vrtp, frame);
			}
			ast_mutex_unlock(&p->lock);
		}
	} else if (frame->frametype == AST_FRAME_IMAGE) {
		return 0;
	} else {
		ast_log(LOG_WARNING, "Can't send %d type frames with SIP write\n", frame->frametype);
		return 0;
	}

	return res;
}

static int sip_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct sip_pvt *p = newchan->pvt->pvt;
	ast_mutex_lock(&p->lock);
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	p->owner = newchan;
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int sip_senddigit(struct ast_channel *ast, char digit)
{
	struct sip_pvt *p = ast->pvt->pvt;
	if (p && (p->dtmfmode & SIP_DTMF_INFO)) {
		transmit_info_with_digit(p, digit);
	}
	if (p && p->rtp && (p->dtmfmode & SIP_DTMF_RFC2833)) {
		ast_rtp_senddigit(p->rtp, digit);
	}
	/* If in-band DTMF is desired, send that */
	if (p->dtmfmode & SIP_DTMF_INBAND)
		return -1;
	return 0;
}

static int sip_transfer(struct ast_channel *ast, char *dest)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int res;
	res = transmit_refer(p, dest);
	return res;
}

static int sip_indicate(struct ast_channel *ast, int condition)
{
	struct sip_pvt *p = ast->pvt->pvt;
	switch(condition) {
	case AST_CONTROL_RINGING:
		if (ast->_state == AST_STATE_RING) {
			if (!p->progress) {
				transmit_response(p, "180 Ringing", &p->initreq);
				p->ringing = 1;
				break;
			} else {
				/* Oops, we've sent progress tones.  Let Asterisk do it instead */
			}
		}
		return -1;
	case AST_CONTROL_BUSY:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "486 Busy Here", &p->initreq);
			p->alreadygone = 1;
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
			break;
		}
		return -1;
	case AST_CONTROL_CONGESTION:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "503 Service Unavailable", &p->initreq);
			p->alreadygone = 1;
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
			break;
		}
		return -1;
	case AST_CONTROL_PROGRESS:
		if ((ast->_state != AST_STATE_UP) && !p->progress && !p->outgoing) {
			transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
			p->progress = 1;
			break;
		}
		return -1;
	case -1:
		return -1;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", condition);
		return -1;
	}
	return 0;
}



static struct ast_channel *sip_new(struct sip_pvt *i, int state, char *title)
{
	struct ast_channel *tmp;
	int fmt;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		/* Select our native format based on codec preference until we receive
		   something from another device to the contrary. */
		if (i->jointcapability)
			tmp->nativeformats = sip_codec_choose(i->jointcapability);
		else if (i->capability)
			tmp->nativeformats = sip_codec_choose(i->capability);
		else
			tmp->nativeformats = sip_codec_choose(capability);
		fmt = ast_best_codec(tmp->nativeformats);
		if (title)
			snprintf(tmp->name, sizeof(tmp->name), "SIP/%s-%04x", title, rand() & 0xffff);
		else
			if (strchr(i->fromdomain,':'))
			{
				snprintf(tmp->name, sizeof(tmp->name), "SIP/%s-%08x", strchr(i->fromdomain,':')+1, (int)(i));
			}
			else
			{
				snprintf(tmp->name, sizeof(tmp->name), "SIP/%s-%08x", i->fromdomain, (int)(i));
			}
		tmp->type = type;
                if (i->dtmfmode & SIP_DTMF_INBAND) {
                    i->vad = ast_dsp_new();
                    ast_dsp_set_features(i->vad, DSP_FEATURE_DTMF_DETECT);
		    if (relaxdtmf)
			ast_dsp_digitmode(i->vad, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
                }
		tmp->fds[0] = ast_rtp_fd(i->rtp);
		tmp->fds[1] = ast_rtcp_fd(i->rtp);
		if (i->vrtp) {
			tmp->fds[2] = ast_rtp_fd(i->vrtp);
			tmp->fds[3] = ast_rtcp_fd(i->vrtp);
		}
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->adsicpe = AST_ADSI_UNAVAILABLE;
		tmp->writeformat = fmt;
		tmp->pvt->rawwriteformat = fmt;
		tmp->readformat = fmt;
		tmp->pvt->rawreadformat = fmt;
		tmp->pvt->pvt = i;
		tmp->pvt->send_text = sip_sendtext;
		tmp->pvt->call = sip_call;
		tmp->pvt->hangup = sip_hangup;
		tmp->pvt->answer = sip_answer;
		tmp->pvt->read = sip_read;
		tmp->pvt->write = sip_write;
		tmp->pvt->write_video = sip_write;
		tmp->pvt->indicate = sip_indicate;
		tmp->pvt->transfer = sip_transfer;
		tmp->pvt->fixup = sip_fixup;
		tmp->pvt->send_digit = sip_senddigit;

		tmp->pvt->bridge = ast_rtp_bridge;

		tmp->callgroup = i->callgroup;
		tmp->pickupgroup = i->pickupgroup;
		tmp->restrictcid = i->restrictcid;
                if (!ast_strlen_zero(i->accountcode))
                        strncpy(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode)-1);
                if (i->amaflags)
                        tmp->amaflags = i->amaflags;
		if (!ast_strlen_zero(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		i->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, i->exten, sizeof(tmp->exten)-1);
		if (!ast_strlen_zero(i->callerid))
			tmp->callerid = strdup(i->callerid);
		if (!ast_strlen_zero(i->rdnis))
			tmp->rdnis = strdup(i->rdnis);
		if (!ast_strlen_zero(i->exten) && strcmp(i->exten, "s"))
			tmp->dnid = strdup(i->exten);
		tmp->priority = 1;
		if (!ast_strlen_zero(i->domain)) {
			pbx_builtin_setvar_helper(tmp, "SIPDOMAIN", i->domain);
		}
		ast_setstate(tmp, state);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}

static struct cfalias {
	char *fullname;
	char *shortname;
} aliases[] = {
	{ "Content-Type", "c" },
	{ "Content-Encoding", "e" },
	{ "From", "f" },
	{ "Call-ID", "i" },
	{ "Contact", "m" },
	{ "Content-Length", "l" },
	{ "Subject", "s" },
	{ "To", "t" },
	{ "Via", "v" },
};

static char* get_sdp_by_line(char* line, char *name, int nameLen) {
  if (strncasecmp(line, name, nameLen) == 0 && line[nameLen] == '=') {
    char* r = line + nameLen + 1;
    while (*r && (*r < 33)) ++r;
    return r;
  }

  return "";
}

static char *get_sdp(struct sip_request *req, char *name) {
  int x;
  int len = strlen(name);
  char *r;

  for (x=0; x<req->lines; x++) {
    r = get_sdp_by_line(req->line[x], name, len);
    if (r[0] != '\0') return r;
  }
  return "";
}

static void sdpLineNum_iterator_init(int* iterator) {
  *iterator = 0;
}

static char* get_sdp_iterate(int* iterator,
			     struct sip_request *req, char *name) {
  int len = strlen(name);
  char *r;
  while (*iterator < req->lines) {
    r = get_sdp_by_line(req->line[(*iterator)++], name, len);
    if (r[0] != '\0') return r;
  }
  return "";
}

static char *__get_header(struct sip_request *req, char *name, int *start)
{
	int x;
	int len = strlen(name);
	char *r;
	for (x=*start;x<req->headers;x++) {
		if (!strncasecmp(req->header[x], name, len) && 
				(req->header[x][len] == ':')) {
					r = req->header[x] + len + 1;
					while(*r && (*r < 33))
							r++;
					*start = x+1;
					return r;
		}
	}
	/* Try aliases */
	for (x=0;x<sizeof(aliases) / sizeof(aliases[0]); x++) 
		if (!strcasecmp(aliases[x].fullname, name))
			return __get_header(req, aliases[x].shortname, start);

	/* Don't return NULL, so get_header is always a valid pointer */
	return "";
}

static char *get_header(struct sip_request *req, char *name)
{
	int start = 0;
	return __get_header(req, name, &start);
}

static struct ast_frame *sip_rtp_read(struct ast_channel *ast, struct sip_pvt *p)
{
	/* Retrieve audio/etc from channel.  Assumes p->lock is already held. */
	struct ast_frame *f;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	switch(ast->fdno) {
	case 0:
		f = ast_rtp_read(p->rtp);
		break;
	case 1:
		f = ast_rtcp_read(p->rtp);
		break;
	case 2:
		f = ast_rtp_read(p->vrtp);
		break;
	case 3:
		f = ast_rtcp_read(p->vrtp);
		break;
	default:
		f = &null_frame;
	}
	/* Don't send RFC2833 if we're not supposed to */
	if (f && (f->frametype == AST_FRAME_DTMF) && !(p->dtmfmode & SIP_DTMF_RFC2833))
		return &null_frame;
	if (p->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass != p->owner->nativeformats) {
				ast_log(LOG_DEBUG, "Oooh, format changed to %d\n", f->subclass);
				p->owner->nativeformats = f->subclass;
				ast_set_read_format(p->owner, p->owner->readformat);
				ast_set_write_format(p->owner, p->owner->writeformat);
			}
            if ((p->dtmfmode & SIP_DTMF_INBAND) && p->vad) {
                   f = ast_dsp_process(p->owner,p->vad,f);
		   if (f && (f->frametype == AST_FRAME_DTMF)) 
			ast_log(LOG_DEBUG, "Detected DTMF '%c'\n", f->subclass);
            }
		}
	}
	return f;
}

static struct ast_frame *sip_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct sip_pvt *p = ast->pvt->pvt;
	ast_mutex_lock(&p->lock);
	fr = sip_rtp_read(ast, p);
	ast_mutex_unlock(&p->lock);
	return fr;
}

static void build_callid(char *callid, int len, struct in_addr ourip)
{
	int res;
	int val;
	int x;
	for (x=0;x<4;x++) {
		val = rand();
		res = snprintf(callid, len, "%08x", val);
		len -= res;
		callid += res;
	}
	/* It's not important that we really use our right IP here... */
	snprintf(callid, len, "@%s", inet_ntoa(ourip));
}

static struct sip_pvt *sip_alloc(char *callid, struct sockaddr_in *sin, int useglobalnat)
{
	struct sip_pvt *p;

	p = malloc(sizeof(struct sip_pvt));
	if (!p)
		return NULL;
	/* Keep track of stuff */
	memset(p, 0, sizeof(struct sip_pvt));
	p->initid = -1;
	p->autokillid = -1;
	p->stateid = -1;
	p->rtp = ast_rtp_new(sched, io, 1, 0);
	if (videosupport)
		p->vrtp = ast_rtp_new(sched, io, 1, 0);
	p->branch = rand();	
	p->tag = rand();
	
	/* Start with 101 instead of 1 */
	p->ocseq = 101;
	if (!p->rtp) {
		ast_log(LOG_WARNING, "Unable to create RTP session: %s\n", strerror(errno));
		free(p);
		return NULL;
	}
	ast_rtp_settos(p->rtp, tos);
	if (p->vrtp)
		ast_rtp_settos(p->vrtp, tos);
	if (useglobalnat && sin) {
		/* Setup NAT structure according to global settings if we have an address */
		p->nat = globalnat;
		memcpy(&p->recv, sin, sizeof(p->recv));
		ast_rtp_setnat(p->rtp, p->nat);
		if (p->vrtp)
			ast_rtp_setnat(p->vrtp, p->nat);
	}
	ast_mutex_init(&p->lock);

	if (sin) {
		memcpy(&p->sa, sin, sizeof(p->sa));
		if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
			memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	} else {
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	}
	/* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", inet_ntoa(p->ourip), ourport, p->branch);
	if (!callid)
		build_callid(p->callid, sizeof(p->callid), p->ourip);
	else
		strncpy(p->callid, callid, sizeof(p->callid) - 1);
	/* Assume reinvite OK and via INVITE */
	p->canreinvite = globalcanreinvite;
	p->dtmfmode = globaldtmfmode;
	p->capability = capability;
	if (p->dtmfmode & SIP_DTMF_RFC2833)
		p->noncodeccapability |= AST_RTP_DTMF;
	strncpy(p->context, context, sizeof(p->context) - 1);
	strncpy(p->fromdomain, fromdomain, sizeof(p->fromdomain) - 1);
	/* Add to list */
	ast_mutex_lock(&iflock);
	p->next = iflist;
	iflist = p;
	ast_mutex_unlock(&iflock);
	if (option_debug)
		ast_log(LOG_DEBUG, "Allocating new SIP call for %s\n", callid);
	return p;
}

static struct sip_pvt *find_call(struct sip_request *req, struct sockaddr_in *sin)
{
	struct sip_pvt *p;
	char *callid;
	char tmp[256] = "";
	char *cmd;
	char *tag = "", *c;
	int themisfrom;
	callid = get_header(req, "Call-ID");

	if (pedanticsipchecking) {
		/* In principle Call-ID's uniquely identify a call, however some vendors
		   (i.e. Pingtel) send multiple calls with the same Call-ID and different
		   tags in order to simplify billing.  The RFC does state that we have to
		   compare tags in addition to the call-id, but this generate substantially
		   more overhead which is totally unnecessary for the vast majority of sane
		   SIP implementations, and thus Asterisk does not enable this behavior
		   by default. Short version: You'll need this option to support conferencing
		   on the pingtel */
		strncpy(tmp, req->header[0], sizeof(tmp) - 1);
		cmd = tmp;
		c = strchr(tmp, ' ');
		if (c)
			*c = '\0';
		if (!strcasecmp(cmd, "SIP/2.0")) {
			themisfrom = 0;
		} else {
			themisfrom = 1;
		}
		if (themisfrom)
			strncpy(tmp, get_header(req, "From"), sizeof(tmp) - 1);
		else
			strncpy(tmp, get_header(req, "To"), sizeof(tmp) - 1);
		tag = strstr(tmp, "tag=");
		if (tag) {
			tag += 4;
			c = strchr(tag, ';');
			if (c)
				*c = '\0';
		}
			
	}
		
	if (ast_strlen_zero(callid)) {
		ast_log(LOG_WARNING, "Call missing call ID from '%s'\n", inet_ntoa(sin->sin_addr));
		return NULL;
	}
	ast_mutex_lock(&iflock);
	p = iflist;
	while(p) {
		if (!strcmp(p->callid, callid) && 
			(!pedanticsipchecking || !tag || ast_strlen_zero(p->theirtag) || !strcmp(p->theirtag, tag))) {
			/* Found the call */
			ast_mutex_lock(&p->lock);
			ast_mutex_unlock(&iflock);
			return p;
		}
		p = p->next;
	}
	ast_mutex_unlock(&iflock);
	p = sip_alloc(callid, sin, 1);
	if (p)
		ast_mutex_lock(&p->lock);
	return p;
}

static int sip_register(char *value, int lineno)
{
	struct sip_registry *reg;
	char copy[256] = "";
	char *username=NULL, *hostname=NULL, *secret=NULL, *authuser=NULL;
	char *porta=NULL;
	char *contact=NULL;
	char *stringp=NULL;
	
	struct hostent *hp;
	struct ast_hostent ahp;
	if (!value)
		return -1;
	strncpy(copy, value, sizeof(copy)-1);
	stringp=copy;
	username = stringp;
	hostname = strrchr(stringp, '@');
	if (hostname) {
		*hostname = '\0';
		hostname++;
	}
	if (!username || ast_strlen_zero(username) || !hostname || !strlen(hostname)) {
		ast_log(LOG_WARNING, "Format for registration is user[:secret[:authuser]]@host[:port][/contact] at line %d", lineno);
		return -1;
	}
	stringp=username;
	username = strsep(&stringp, ":");
	if (username) {
		secret = strsep(&stringp, ":");
		if (secret) 
			authuser = strsep(&stringp, ":");
	}
	stringp = hostname;
	hostname = strsep(&stringp, "/");
	if (hostname) 
		contact = strsep(&stringp, "/");
	if (!contact || ast_strlen_zero(contact))
		contact = "s";
	stringp=hostname;
	hostname = strsep(&stringp, ":");
	porta = strsep(&stringp, ":");
	
	if (porta && !atoi(porta)) {
		ast_log(LOG_WARNING, "%s is not a valid port number at line %d\n", porta, lineno);
		return -1;
	}
	hp = ast_gethostbyname(hostname, &ahp);
	if (!hp) {
		ast_log(LOG_WARNING, "Host '%s' not found at line %d\n", hostname, lineno);
		return -1;
	}
	reg = malloc(sizeof(struct sip_registry));
	if (reg) {
		memset(reg, 0, sizeof(struct sip_registry));
		strncpy(reg->contact, contact, sizeof(reg->contact) - 1);
		if (username)
			strncpy(reg->username, username, sizeof(reg->username)-1);
		if (hostname)
			strncpy(reg->hostname, hostname, sizeof(reg->hostname)-1);
		if (authuser)
			strncpy(reg->authuser, authuser, sizeof(reg->authuser)-1);
		if (secret)
			strncpy(reg->secret, secret, sizeof(reg->secret)-1);
		reg->expire = -1;
		reg->timeout =  -1;
		reg->refresh = default_expiry;
		reg->addr.sin_family = AF_INET;
		memcpy(&reg->addr.sin_addr, hp->h_addr, sizeof(&reg->addr.sin_addr));
		reg->addr.sin_port = porta ? htons(atoi(porta)) : htons(DEFAULT_SIP_PORT);
		reg->callid_valid = 0;
		reg->ocseq = 101;
		ast_mutex_lock(&regl.lock);
		reg->next = regl.registrations;
		regl.registrations = reg;
		ast_mutex_unlock(&regl.lock);
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	return 0;
}

static int lws2sws(char *msgbuf, int len) 
{ 
	int h = 0, t = 0; 
	int lws = 0; 

	for (; h < len;) { 
		/* Eliminate all CRs */ 
		if (msgbuf[h] == '\r') { 
			h++; 
			continue; 
		} 
		/* Check for end-of-line */ 
		if (msgbuf[h] == '\n') { 
		/* Check for end-of-message */ 
			if (h + 1 == len) 
			break; 
		/* Check for a continuation line */ 
		if (msgbuf[h + 1] == ' ') { 
		/* Merge continuation line */ 
			h++; 
			continue; 
		} 
		/* Propagate LF and start new line */ 
		msgbuf[t++] = msgbuf[h++]; 
		lws = 0;
		continue; 
	} 

	if (msgbuf[h] == ' ' || msgbuf[h] == '\t') { 
		if (lws) { 
			h++; 
			continue; 
		} 
		msgbuf[t++] = msgbuf[h++]; 
		lws = 1; 
		continue; 
	} 
	msgbuf[t++] = msgbuf[h++]; 
	if (lws) 
		lws = 0; 
	} 
	msgbuf[t] = '\0'; 
	return t; 
}

static void parse(struct sip_request *req)
{
	/* Divide fields by NULL's */
	char *c;
	int f = 0;
	c = req->data;

	/* First header starts immediately */
	req->header[f] = c;
	while(*c) {
		if (*c == '\n') {
			/* We've got a new header */
			*c = 0;

#if 0
			printf("Header: %s (%d)\n", req->header[f], strlen(req->header[f]));
#endif			
			if (ast_strlen_zero(req->header[f])) {
				/* Line by itself means we're now in content */
				c++;
				break;
			}
			if (f >= SIP_MAX_HEADERS - 1) {
				ast_log(LOG_WARNING, "Too many SIP headers...\n");
			} else
				f++;
			req->header[f] = c + 1;
		} else if (*c == '\r') {
			/* Ignore but eliminate \r's */
			*c = 0;
		}
		c++;
	}
	/* Check for last header */
	if (!ast_strlen_zero(req->header[f])) 
		f++;
	req->headers = f;
	/* Now we process any mime content */
	f = 0;
	req->line[f] = c;
	while(*c) {
		if (*c == '\n') {
			/* We've got a new line */
			*c = 0;
#if 0
			printf("Line: %s (%d)\n", req->line[f], strlen(req->line[f]));
#endif			
			if (f >= SIP_MAX_LINES - 1) {
				ast_log(LOG_WARNING, "Too many SDP lines...\n");
			} else
				f++;
			req->line[f] = c + 1;
		} else if (*c == '\r') {
			/* Ignore and eliminate \r's */
			*c = 0;
		}
		c++;
	}
	/* Check for last line */
	if (!ast_strlen_zero(req->line[f])) 
		f++;
	req->lines = f;
	if (*c) 
		ast_log(LOG_WARNING, "Odd content, extra stuff left over ('%s')\n", c);
}

static int process_sdp(struct sip_pvt *p, struct sip_request *req)
{
	char *m;
	char *c;
	char *a;
	char host[258];
	int len = -1;
	int portno=0;
	int vportno=0;
	int peercapability, peernoncodeccapability;
	int vpeercapability=0, vpeernoncodeccapability=0;
	struct sockaddr_in sin;
	char *codecs;
	struct hostent *hp;
	struct ast_hostent ahp;
	int codec;
	int iterator;
	int sendonly = 0;
	int x;

	/* Get codec and RTP info from SDP */
	if (strcasecmp(get_header(req, "Content-Type"), "application/sdp")) {
		ast_log(LOG_NOTICE, "Content is '%s', not 'application/sdp'\n", get_header(req, "Content-Type"));
		return -1;
	}
	m = get_sdp(req, "m");
	c = get_sdp(req, "c");
	if (ast_strlen_zero(m) || ast_strlen_zero(c)) {
		ast_log(LOG_WARNING, "Insufficient information for SDP (m = '%s', c = '%s')\n", m, c);
		return -1;
	}
	if (sscanf(c, "IN IP4 %256s", host) != 1) {
		ast_log(LOG_WARNING, "Invalid host in c= line, '%s'\n", c);
		return -1;
	}
	/* XXX This could block for a long time, and block the main thread! XXX */
	hp = ast_gethostbyname(host, &ahp);
	if (!hp) {
		ast_log(LOG_WARNING, "Unable to lookup host in c= line, '%s'\n", c);
		return -1;
	}
	sdpLineNum_iterator_init(&iterator);
	while ((m = get_sdp_iterate(&iterator, req, "m"))[0] != '\0') {
		if ((sscanf(m, "audio %d RTP/AVP %n", &x, &len) == 1)) {
			portno = x;
			/* Scan through the RTP payload types specified in a "m=" line: */
			ast_rtp_pt_clear(p->rtp);
			codecs = m + len;
			while(!ast_strlen_zero(codecs)) {
				if (sscanf(codecs, "%d%n", &codec, &len) != 1) {
					ast_log(LOG_WARNING, "Error in codec string '%s'\n", codecs);
					return -1;
				}
				if (sip_debug_test_pvt(p))
					ast_verbose("Found RTP audio format %d\n", codec);
				ast_rtp_set_m_type(p->rtp, codec);
				codecs += len;
				/* Skip over any whitespace */
				while(*codecs && (*codecs < 33)) codecs++;
			}
		}
		if (p->vrtp)
			ast_rtp_pt_clear(p->vrtp);  /* Must be cleared in case no m=video line exists */

		if (p->vrtp && (sscanf(m, "video %d RTP/AVP %n", &x, &len) == 1)) {
			vportno = x;
			/* Scan through the RTP payload types specified in a "m=" line: */
			codecs = m + len;
			while(!ast_strlen_zero(codecs)) {
				if (sscanf(codecs, "%d%n", &codec, &len) != 1) {
					ast_log(LOG_WARNING, "Error in codec string '%s'\n", codecs);
					return -1;
				}
				if (sip_debug_test_pvt(p))
					ast_verbose("Found video format %s\n", ast_getformatname(codec));
				ast_rtp_set_m_type(p->vrtp, codec);
				codecs += len;
				/* Skip over any whitespace */
				while(*codecs && (*codecs < 33)) codecs++;
			}
		}
	}
	sin.sin_family = AF_INET;
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	/* Setup audio port number */
	sin.sin_port = htons(portno);
	if (p->rtp && sin.sin_port)
		ast_rtp_set_peer(p->rtp, &sin);
	/* Setup video port number */
	sin.sin_port = htons(vportno);
	if (p->vrtp && sin.sin_port)
		ast_rtp_set_peer(p->vrtp, &sin);
#if 0
	printf("Peer RTP is at port %s:%d\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
#endif	
	/* Next, scan through each "a=rtpmap:" line, noting each
	 * specified RTP payload type (with corresponding MIME subtype):
	 */
	sdpLineNum_iterator_init(&iterator);
	while ((a = get_sdp_iterate(&iterator, req, "a"))[0] != '\0') {
      char* mimeSubtype = ast_strdupa(a); /* ensures we have enough space */
	  if (!strcasecmp(a, "sendonly")) {
	  	sendonly=1;
		continue;
	  }
	  if (!strcasecmp(a, "sendrecv")) {
	  	sendonly=0;
	  }
	  if (sscanf(a, "rtpmap: %u %[^/]/", &codec, mimeSubtype) != 2) continue;
	  if (sip_debug_test_pvt(p))
		ast_verbose("Found description format %s\n", mimeSubtype);
	  /* Note: should really look at the 'freq' and '#chans' params too */
	  ast_rtp_set_rtpmap_type(p->rtp, codec, "audio", mimeSubtype);
	  if (p->vrtp)
		  ast_rtp_set_rtpmap_type(p->vrtp, codec, "video", mimeSubtype);
	}

	/* Now gather all of the codecs that were asked for: */
	ast_rtp_get_current_formats(p->rtp,
				&peercapability, &peernoncodeccapability);
	if (p->vrtp)
		ast_rtp_get_current_formats(p->vrtp,
				&vpeercapability, &vpeernoncodeccapability);
	p->jointcapability = p->capability & (peercapability | vpeercapability);
	p->noncodeccapability = noncodeccapability & (peernoncodeccapability | vpeernoncodeccapability);
	
	if (sip_debug_test_pvt(p)) {
		ast_verbose("Capabilities: us - %d, them - %d/%d, combined - %d\n",
			    p->capability, peercapability, vpeercapability, p->jointcapability);
		ast_verbose("Non-codec capabilities: us - %d, them - %d, combined - %d\n",
			    noncodeccapability, peernoncodeccapability,
			    p->noncodeccapability);
	}
	if (!p->jointcapability) {
		ast_log(LOG_WARNING, "No compatible codecs!\n");
		return -1;
	}
	if (p->owner) {
		if (!(p->owner->nativeformats & p->jointcapability)) {
			ast_log(LOG_DEBUG, "Oooh, we need to change our formats since our peer supports only %d and not %d\n", p->jointcapability, p->owner->nativeformats);
			p->owner->nativeformats = sip_codec_choose(p->jointcapability);
			ast_set_read_format(p->owner, p->owner->readformat);
			ast_set_write_format(p->owner, p->owner->writeformat);
		}
		if (p->owner->bridge) {
			/* Turn on/off music on hold if we are holding/unholding */
			if (sin.sin_addr.s_addr && !sendonly) {
				ast_moh_stop(p->owner->bridge);
			} else {
				ast_moh_start(p->owner->bridge, NULL);
			}
		}
	}
	return 0;
	
}

static int add_header(struct sip_request *req, char *var, char *value)
{
	if (req->len >= sizeof(req->data) - 4) {
		ast_log(LOG_WARNING, "Out of space, can't add anymore (%s:%s)\n", var, value);
		return -1;
	}
	if (req->lines) {
		ast_log(LOG_WARNING, "Can't add more headers when lines have been added\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	snprintf(req->header[req->headers], sizeof(req->data) - req->len - 4, "%s: %s\r\n", var, value);
	req->len += strlen(req->header[req->headers]);
	if (req->headers < SIP_MAX_HEADERS)
		req->headers++;
	else {
		ast_log(LOG_WARNING, "Out of header space\n");
		return -1;
	}
	return 0;	
}

static int add_blank_header(struct sip_request *req)
{
	if (req->len >= sizeof(req->data) - 4) {
		ast_log(LOG_WARNING, "Out of space, can't add anymore\n");
		return -1;
	}
	if (req->lines) {
		ast_log(LOG_WARNING, "Can't add more headers when lines have been added\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	snprintf(req->header[req->headers], sizeof(req->data) - req->len, "\r\n");
	req->len += strlen(req->header[req->headers]);
	if (req->headers < SIP_MAX_HEADERS)
		req->headers++;
	else {
		ast_log(LOG_WARNING, "Out of header space\n");
		return -1;
	}
	return 0;	
}

static int add_line(struct sip_request *req, char *line)
{
	if (req->len >= sizeof(req->data) - 4) {
		ast_log(LOG_WARNING, "Out of space, can't add anymore\n");
		return -1;
	}
	if (!req->lines) {
		/* Add extra empty return */
		snprintf(req->data + req->len, sizeof(req->data) - req->len, "\r\n");
		req->len += strlen(req->data + req->len);
	}
	req->line[req->lines] = req->data + req->len;
	snprintf(req->line[req->lines], sizeof(req->data) - req->len, "%s", line);
	req->len += strlen(req->line[req->lines]);
	if (req->lines < SIP_MAX_LINES)
		req->lines++;
	else {
		ast_log(LOG_WARNING, "Out of line space\n");
		return -1;
	}
	return 0;	
}

static int copy_header(struct sip_request *req, struct sip_request *orig, char *field)
{
	char *tmp;
	tmp = get_header(orig, field);
	if (!ast_strlen_zero(tmp)) {
		/* Add what we're responding to */
		return add_header(req, field, tmp);
	}
	ast_log(LOG_NOTICE, "No field '%s' present to copy\n", field);
	return -1;
}

static int copy_all_header(struct sip_request *req, struct sip_request *orig, char *field)
{
	char *tmp;
	int start = 0;
	int copied = 0;
	for (;;) {
		tmp = __get_header(orig, field, &start);
		if (!ast_strlen_zero(tmp)) {
			/* Add what we're responding to */
			add_header(req, field, tmp);
			copied++;
		} else
			break;
	}
	return copied ? 0 : -1;
}

static int copy_via_headers(struct sip_pvt *p, struct sip_request *req, struct sip_request *orig, char *field)
{
	char *tmp;
	int start = 0;
	int copied = 0;
	char new[256];
	for (;;) {
		tmp = __get_header(orig, field, &start);
		if (!ast_strlen_zero(tmp)) {
			if (!copied && p->nat) {
#ifdef THE_SIP_AUTHORS_CAN_SUCK_MY_GONADS
				/* SLD: FIXME: Nice try, but the received= should not have a port */
				/* SLD: FIXME: See RFC2543 BNF in Section 6.40.5 */
				/* MAS: Yup, RFC says you can't do it.  No way to indicate PAT...
				   good job fellas. */
				if (ntohs(p->recv.sin_port) != DEFAULT_SIP_PORT)
					snprintf(new, sizeof(new), "%s;received=%s:%d", tmp, inet_ntoa(p->recv.sin_addr), ntohs(p->recv.sin_port));
				else
#endif				
					snprintf(new, sizeof(new), "%s;received=%s", tmp, inet_ntoa(p->recv.sin_addr));
				add_header(req, field, new);
			} else {
				/* Add what we're responding to */
				add_header(req, field, tmp);
			}
			copied++;
		} else
			break;
	}
	if (!copied) {
		ast_log(LOG_NOTICE, "No field '%s' present to copy\n", field);
		return -1;
	}
	return 0;
}

/* Add Route: header into request per learned route */
static void add_route(struct sip_request *req, struct sip_route *route)
{
	char r[256], *p;
	int n, rem = 255; /* sizeof(r)-1: Room for terminating 0 */

	if (!route) return;

	p = r;
	while (route) {
		n = strlen(route->hop);
		if ((n+3)>rem) break;
		if (p != r) {
			*p++ = ',';
			--rem;
		}
		*p++ = '<';
		strcpy(p, route->hop);  p += n;
		*p++ = '>';
		rem -= (n+2);
		route = route->next;
	}
	*p = '\0';
	add_header(req, "Route", r);
}

static void set_destination(struct sip_pvt *p, char *uri)
{
	char *h, *maddr, hostname[256];
	int port, hn;
	struct hostent *hp;
	struct ast_hostent ahp;

	/* Parse uri to h (host) and port - uri is already just the part inside the <> */
	/* general form we are expecting is sip[s]:username[:password]@host[:port][;...] */

	if (sip_debug_test_pvt(p))
		ast_verbose("set_destination: Parsing <%s> for address/port to send to\n", uri);

	/* Find and parse hostname */
	h = strchr(uri, '@');
	if (h)
		++h;
	else {
		h = uri;
		if (strncmp(h, "sip:", 4) == 0)
			h += 4;
		else if (strncmp(h, "sips:", 5) == 0)
			h += 5;
	}
	hn = strcspn(h, ":;>");
	if (hn>255) hn=255;
	strncpy(hostname, h, hn);  hostname[hn] = '\0';
	h+=hn;

	/* Is "port" present? if not default to 5060 */
	if (*h == ':') {
		/* Parse port */
		++h;
		port = strtol(h, &h, 10);
	}
	else
		port = 5060;

	/* Got the hostname:port - but maybe there's a "maddr=" to override address? */
	maddr = strstr(h, "maddr=");
	if (maddr) {
		maddr += 6;
		hn = strspn(maddr, "0123456789.");
		if (hn>255) hn=255;
		strncpy(hostname, maddr, hn);  hostname[hn] = '\0';
	}
	
	hp = ast_gethostbyname(hostname, &ahp);
	if (hp == NULL)  {
		ast_log(LOG_WARNING, "Can't find address for host '%s'\n", hostname);
		return;
	}
	p->sa.sin_family = AF_INET;
	memcpy(&p->sa.sin_addr, hp->h_addr, sizeof(p->sa.sin_addr));
	p->sa.sin_port = htons(port);
	if (sip_debug_test_pvt(p))
		ast_verbose("set_destination: set destination to %s, port %d\n", inet_ntoa(p->sa.sin_addr), port);
}

static int init_resp(struct sip_request *req, char *resp, struct sip_request *orig)
{
	/* Initialize a response */
	if (req->headers || req->len) {
		ast_log(LOG_WARNING, "Request already initialized?!?\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	snprintf(req->header[req->headers], sizeof(req->data) - req->len, "SIP/2.0 %s\r\n", resp);
	req->len += strlen(req->header[req->headers]);
	if (req->headers < SIP_MAX_HEADERS)
		req->headers++;
	else
		ast_log(LOG_WARNING, "Out of header space\n");
	return 0;
}

static int init_req(struct sip_request *req, char *resp, char *recip)
{
	/* Initialize a response */
	if (req->headers || req->len) {
		ast_log(LOG_WARNING, "Request already initialized?!?\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s %s SIP/2.0\r\n", resp, recip);
	req->len += strlen(req->header[req->headers]);
	if (req->headers < SIP_MAX_HEADERS)
		req->headers++;
	else
		ast_log(LOG_WARNING, "Out of header space\n");
	return 0;
}

static int respprep(struct sip_request *resp, struct sip_pvt *p, char *msg, struct sip_request *req)
{
	char newto[256] = "", *ot;
	memset(resp, 0, sizeof(*resp));
	init_resp(resp, msg, req);
	copy_via_headers(p, resp, req, "Via");
	if (msg[0] == '2') copy_all_header(resp, req, "Record-Route");
	copy_header(resp, req, "From");
	ot = get_header(req, "To");
	if (!strstr(ot, "tag=")) {
		/* Add the proper tag if we don't have it already.  If they have specified
		   their tag, use it.  Otherwise, use our own tag */
		if (!ast_strlen_zero(p->theirtag) && p->outgoing)
			snprintf(newto, sizeof(newto), "%s;tag=%s", ot, p->theirtag);
		else if (p->tag && !p->outgoing)
			snprintf(newto, sizeof(newto), "%s;tag=as%08x", ot, p->tag);
		else
			strncpy(newto, ot, sizeof(newto) - 1);
		ot = newto;
	}
	add_header(resp, "To", ot);
	copy_header(resp, req, "Call-ID");
	copy_header(resp, req, "CSeq");
	add_header(resp, "User-Agent", "Asterisk PBX");
	add_header(resp, "Allow", ALLOWED_METHODS);
	if (p->expiry) {
		/* For registration responses, we also need expiry and
		   contact info */
		char contact[256];
		char tmp[256];
		snprintf(contact, sizeof(contact), "%s;expires=%d", p->our_contact, p->expiry);
		snprintf(tmp, sizeof(tmp), "%d", p->expiry);
		add_header(resp, "Expires", tmp);
		add_header(resp, "Contact", contact);
	} else {
		add_header(resp, "Contact", p->our_contact);
	}
	return 0;
}

static int reqprep(struct sip_request *req, struct sip_pvt *p, char *msg, int seqno, int newbranch)
{
	struct sip_request *orig = &p->initreq;
	char stripped[80] ="";
	char tmp[80];
	char newto[256];
	char *c, *n;
	char *ot, *of;

	memset(req, 0, sizeof(struct sip_request));
	
	snprintf(p->lastmsg, sizeof(p->lastmsg), "Tx: %s", msg);
	
	if (!seqno) {
		p->ocseq++;
		seqno = p->ocseq;
	}
	
	if (newbranch) {
		p->branch ^= rand();
		snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", inet_ntoa(p->ourip), ourport, p->branch);
	}

	if (!ast_strlen_zero(p->uri)) {
		c = p->uri;
	} else {
		if (p->outgoing)
			strncpy(stripped, get_header(orig, "To"), sizeof(stripped) - 1);
		else
			strncpy(stripped, get_header(orig, "From"), sizeof(stripped) - 1);
		
		c = strchr(stripped, '<');
		if (c) 
			c++;
		else
			c = stripped;
		n = strchr(c, '>');
		if (n)
			*n = '\0';
		n = strchr(c, ';');
		if (n)
			*n = '\0';
	}	
	init_req(req, msg, c);

	snprintf(tmp, sizeof(tmp), "%d %s", seqno, msg);

	add_header(req, "Via", p->via);
	if (p->route) {
		set_destination(p, p->route->hop);
		add_route(req, p->route->next);
	}

	ot = get_header(orig, "To");
	of = get_header(orig, "From");

	/* Add tag *unless* this is a CANCEL, in which case we need to send it exactly
	   as our original request, including tag (or presumably lack thereof) */
	if (!strstr(ot, "tag=") && strcasecmp(msg, "CANCEL")) {
		/* Add the proper tag if we don't have it already.  If they have specified
		   their tag, use it.  Otherwise, use our own tag */
		if (p->outgoing && !ast_strlen_zero(p->theirtag))
			snprintf(newto, sizeof(newto), "%s;tag=%s", ot, p->theirtag);
		else if (!p->outgoing)
			snprintf(newto, sizeof(newto), "%s;tag=as%08x", ot, p->tag);
		else
			snprintf(newto, sizeof(newto), "%s", ot);
		ot = newto;
	}

	if (p->outgoing) {
		add_header(req, "From", of);
		add_header(req, "To", ot);
	} else {
		add_header(req, "From", ot);
		add_header(req, "To", of);
	}
	add_header(req, "Contact", p->our_contact);
	copy_header(req, orig, "Call-ID");
	add_header(req, "CSeq", tmp);

	add_header(req, "User-Agent", "Asterisk PBX");
	return 0;
}

static int __transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req, int reliable)
{
	struct sip_request resp;
	int seqno = 0;
	if (reliable && (sscanf(get_header(req, "CSeq"), "%i ", &seqno) != 1)) {
		ast_log(LOG_WARNING, "Unable to determine sequence number from '%s'\n", get_header(req, "CSeq"));
		return -1;
	}
	respprep(&resp, p, msg, req);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp, reliable, seqno);
}

static int transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req) 
{
	return __transmit_response(p, msg, req, 0);
}
static int transmit_response_reliable(struct sip_pvt *p, char *msg, struct sip_request *req, int fatal)
{
	return __transmit_response(p, msg, req, fatal ? 2 : 1);
}

static void append_date(struct sip_request *req)
{
	char tmpdat[256];
	struct tm tm;
	time_t t;
	time(&t);
	gmtime_r(&t, &tm);
	strftime(tmpdat, sizeof(tmpdat), "%a, %d %b %Y %T GMT", &tm);
	add_header(req, "Date", tmpdat);
}

static int transmit_response_with_date(struct sip_pvt *p, char *msg, struct sip_request *req)
{
	struct sip_request resp;
	respprep(&resp, p, msg, req);
	append_date(&resp);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp, 0, 0);
}

static int transmit_response_with_allow(struct sip_pvt *p, char *msg, struct sip_request *req)
{
	struct sip_request resp;
	respprep(&resp, p, msg, req);
	add_header(&resp, "Accept", "application/sdp");
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp, 0, 0);
}

static int transmit_response_with_auth(struct sip_pvt *p, char *msg, struct sip_request *req, char *randdata, int reliable)
{
	struct sip_request resp;
	char tmp[256];
	int seqno = 0;
	if (reliable && (sscanf(get_header(req, "CSeq"), "%i ", &seqno) != 1)) {
		ast_log(LOG_WARNING, "Unable to determine sequence number from '%s'\n", get_header(req, "CSeq"));
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "Digest realm=\"asterisk\", nonce=\"%s\"", randdata);
	respprep(&resp, p, msg, req);
	add_header(&resp, "Proxy-Authenticate", tmp);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp, reliable, seqno);
}

static int add_text(struct sip_request *req, char *text)
{
	/* XXX Convert \n's to \r\n's XXX */
	int len = strlen(text);
	char clen[256];
	snprintf(clen, sizeof(clen), "%d", len);
	add_header(req, "Content-Type", "text/plain");
	add_header(req, "Content-Length", clen);
	add_line(req, text);
	return 0;
}

static int add_digit(struct sip_request *req, char digit)
{
	char tmp[256];
	int len;
	char clen[256];
	snprintf(tmp, sizeof(tmp), "Signal=%c\r\nDuration=250\r\n", digit);
	len = strlen(tmp);
	snprintf(clen, sizeof(clen), "%d", len);
	add_header(req, "Content-Type", "application/dtmf-relay");
	add_header(req, "Content-Length", clen);
	add_line(req, tmp);
	return 0;
}

static int add_sdp(struct sip_request *resp, struct sip_pvt *p, struct ast_rtp *rtp, struct ast_rtp *vrtp)
{
	int len;
	int codec;
	int alreadysent = 0;
	char costr[80];
	struct sockaddr_in sin;
	struct sockaddr_in vsin;
	struct sip_codec_pref *cur;
	char v[256];
	char s[256];
	char o[256];
	char c[256];
	char t[256];
	char m[256];
	char m2[256];
	char a[1024] = "";
	char a2[1024] = "";
	int x;
	struct sockaddr_in dest;
	struct sockaddr_in vdest = { 0, };
	/* XXX We break with the "recommendation" and send our IP, in order that our
	       peer doesn't have to ast_gethostbyname() us XXX */
	len = 0;
	if (!p->rtp) {
		ast_log(LOG_WARNING, "No way to add SDP without an RTP structure\n");
		return -1;
	}
	if (!p->sessionid) {
		p->sessionid = getpid();
		p->sessionversion = p->sessionid;
	} else
		p->sessionversion++;
	ast_rtp_get_us(p->rtp, &sin);
	if (p->vrtp)
		ast_rtp_get_us(p->vrtp, &vsin);

	if (p->redirip.sin_addr.s_addr) {
		dest.sin_port = p->redirip.sin_port;
		dest.sin_addr = p->redirip.sin_addr;
	} else if (rtp) {
		ast_rtp_get_peer(rtp, &dest);
	} else {
		dest.sin_addr = p->ourip;
		dest.sin_port = sin.sin_port;
	}

	/* Determine video destination */
	if (p->vrtp) {
		if (p->vredirip.sin_addr.s_addr) {
			vdest.sin_port = p->vredirip.sin_port;
			vdest.sin_addr = p->vredirip.sin_addr;
		} else if (vrtp) {
			ast_rtp_get_peer(vrtp, &vdest);
		} else {
			vdest.sin_addr = p->ourip;
			vdest.sin_port = vsin.sin_port;
		}
	}
	if (sip_debug_test_pvt(p))
		ast_verbose("We're at %s port %d\n", inet_ntoa(p->ourip), ntohs(sin.sin_port));	
	if (sip_debug_test_pvt(p) && p->vrtp)
		ast_verbose("Video is at %s port %d\n", inet_ntoa(p->ourip), ntohs(vsin.sin_port));	
	snprintf(v, sizeof(v), "v=0\r\n");
	snprintf(o, sizeof(o), "o=root %d %d IN IP4 %s\r\n", p->sessionid, p->sessionversion, inet_ntoa(dest.sin_addr));
	snprintf(s, sizeof(s), "s=session\r\n");
	snprintf(c, sizeof(c), "c=IN IP4 %s\r\n", inet_ntoa(dest.sin_addr));
	snprintf(t, sizeof(t), "t=0 0\r\n");
	snprintf(m, sizeof(m), "m=audio %d RTP/AVP", ntohs(dest.sin_port));
	snprintf(m2, sizeof(m2), "m=video %d RTP/AVP", ntohs(vdest.sin_port));
	if (p->jointcapability & p->prefcodec) {
		if (sip_debug_test_pvt(p))
			ast_verbose("Answering/Requesting with root capability %d\n", p->prefcodec);
		codec = ast_rtp_lookup_code(p->rtp, 1, p->prefcodec);
		if (codec > -1) {
			snprintf(costr, sizeof(costr), " %d", codec);
			if (p->prefcodec <= AST_FORMAT_MAX_AUDIO) {
				strncat(m, costr, sizeof(m) - strlen(m));
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(1, p->prefcodec));
				strncat(a, costr, sizeof(a));
			} else {
				strncat(m2, costr, sizeof(m2) - strlen(m2));
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/90000\r\n", codec, ast_rtp_lookup_mime_subtype(1, p->prefcodec));
				strncat(a2, costr, sizeof(a2));
			}
		}
		alreadysent |= p->prefcodec;
	}
	/* Start by sending our preferred codecs */
	cur = prefs;
	while(cur) {
		if (p->jointcapability & cur->codec) {
			if (sip_debug_test_pvt(p))
				ast_verbose("Answering/Requesting with preferred capability %d\n", cur->codec);
			codec = ast_rtp_lookup_code(p->rtp, 1, cur->codec);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				if (cur->codec <= AST_FORMAT_MAX_AUDIO) {
					strncat(m, costr, sizeof(m) - strlen(m));
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(1, cur->codec));
					strncat(a, costr, sizeof(a));
				} else {
					strncat(m2, costr, sizeof(m2) - strlen(m2));
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/90000\r\n", codec, ast_rtp_lookup_mime_subtype(1, cur->codec));
					strncat(a2, costr, sizeof(a2));
				}
			}
		}
		alreadysent |= cur->codec;
		cur = cur->next;
	}
	/* Now send any other common codecs, and non-codec formats: */
	for (x = 1; x <= (videosupport ? AST_FORMAT_MAX_VIDEO : AST_FORMAT_MAX_AUDIO); x <<= 1) {
		if ((p->jointcapability & x) && !(alreadysent & x)) {
			if (sip_debug_test_pvt(p))
				ast_verbose("Answering with capability %d\n", x);	
			codec = ast_rtp_lookup_code(p->rtp, 1, x);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				if (x <= AST_FORMAT_MAX_AUDIO) {
					strncat(m, costr, sizeof(m) - strlen(m));
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(1, x));
					strncat(a, costr, sizeof(a) - strlen(a));
				} else {
					strncat(m2, costr, sizeof(m2) - strlen(m2));
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/90000\r\n", codec, ast_rtp_lookup_mime_subtype(1, x));
					strncat(a2, costr, sizeof(a2) - strlen(a2));
				}
			}
		}
	}
	for (x = 1; x <= AST_RTP_MAX; x <<= 1) {
		if (p->noncodeccapability & x) {
			if (sip_debug_test_pvt(p))
				ast_verbose("Answering with non-codec capability %d\n", x);
			codec = ast_rtp_lookup_code(p->rtp, 0, x);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				strncat(m, costr, sizeof(m) - strlen(m));
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(0, x));
				strncat(a, costr, sizeof(a) - strlen(a));
				if (x == AST_RTP_DTMF) {
				  /* Indicate we support DTMF...  Not sure about 16, but MSN supports it so dang it, we will too... */
				  snprintf(costr, sizeof costr, "a=fmtp:%d 0-16\r\n",
					   codec);
				  strncat(a, costr, sizeof(a) - strlen(a));
				}
			}
		}
	}
	strncat(a, "a=silenceSupp:off - - - -\r\n", sizeof(a) - strlen(a));
	if (strlen(m) < sizeof(m) - 2)
		strcat(m, "\r\n");
	if (strlen(m2) < sizeof(m2) - 2)
		strcat(m2, "\r\n");
	if ((sizeof(m) <= strlen(m) - 2) || (sizeof(m2) <= strlen(m2) - 2) || (sizeof(a) == strlen(a)) || (sizeof(a2) == strlen(a2)))
		ast_log(LOG_WARNING, "SIP SDP may be truncated due to undersized buffer!!\n");
	len = strlen(v) + strlen(s) + strlen(o) + strlen(c) + strlen(t) + strlen(m) + strlen(a);
	if ((p->vrtp) && (p->jointcapability & VIDEO_CODEC_MASK)) /* only if video response is appropriate */
		len += strlen(m2) + strlen(a2);
	snprintf(costr, sizeof(costr), "%d", len);
	add_header(resp, "Content-Type", "application/sdp");
	add_header(resp, "Content-Length", costr);
	add_line(resp, v);
	add_line(resp, o);
	add_line(resp, s);
	add_line(resp, c);
	add_line(resp, t);
	add_line(resp, m);
	add_line(resp, a);
	if ((p->vrtp) && (p->jointcapability & VIDEO_CODEC_MASK)) { /* only if video response is appropriate */
		add_line(resp, m2);
		add_line(resp, a2);
	}
	return 0;
}

static void copy_request(struct sip_request *dst,struct sip_request *src)
{
	long offset;
	int x;
	offset = ((void *)dst) - ((void *)src);
	/* First copy stuff */
	memcpy(dst, src, sizeof(*dst));
	/* Now fix pointer arithmetic */
	for (x=0;x<src->headers;x++)
		dst->header[x] += offset;
	for (x=0;x<src->lines;x++)
		dst->line[x] += offset;
}

static int transmit_response_with_sdp(struct sip_pvt *p, char *msg, struct sip_request *req, int retrans)
{
	struct sip_request resp;
	int seqno;
	if (sscanf(get_header(req, "CSeq"), "%i ", &seqno) != 1) {
		ast_log(LOG_WARNING, "Unable to get seqno from '%s'\n", get_header(req, "CSeq"));
		return -1;
	}
	respprep(&resp, p, msg, req);
	add_sdp(&resp, p, NULL, NULL);
	return send_response(p, &resp, retrans, seqno);
}

static int determine_firstline_parts( struct sip_request *req ) {

  char *e, *cmd;
  int len;
  
  cmd= req->header[0];
  while(*cmd && (*cmd < 33)) {
    cmd++;
  }
  if (!*cmd) {
    return -1;
  }
  e= cmd;
  while(*e && (*e > 32)) {
    e++;
  }
  /* Get the command */
  if (*e) {
    *e = '\0';
    e++;
  }
  req->rlPart1= cmd;
  while( *e && ( *e < 33 ) ) {
    e++; 
  }
  if( !*e ) {
    return -1;
  }
    
  if ( !strcasecmp(cmd, "SIP/2.0") ) {
    /* We have a response */
    req->rlPart2= e;
    len= strlen( req->rlPart2 );
    if( len < 2 ) { return -1; }
    e+= len - 1;
    while( *e && *e<33 ) {
      e--; 
    }
    *(++e)= '\0';
  } else {
    /* We have a request */
    if( *e == '<' ) { 
      e++;
      if( !*e ) { return -1; }  
    }
    req->rlPart2= e;
    if( ( e= strrchr( req->rlPart2, 'S' ) ) == NULL ) {
      return -1;
    }
    while( isspace( *(--e) ) ) {}
    if( *e == '>' ) {
      *e= '\0';
    } else {
      *(++e)= '\0';
    }
  }
  return 1;
}

static int transmit_reinvite_with_sdp(struct sip_pvt *p, struct ast_rtp *rtp, struct ast_rtp *vrtp)
{
	struct sip_request req;
	if (p->canreinvite == REINVITE_UPDATE)
		reqprep(&req, p, "UPDATE", 0, 1);
	else 
		reqprep(&req, p, "INVITE", 0, 1);
	
	add_header(&req, "Allow", ALLOWED_METHODS);
	add_sdp(&req, p, rtp, vrtp);
	/* Use this as the basis */
	copy_request(&p->initreq, &req);
	parse(&p->initreq);
	if (sip_debug_test_pvt(p))
		ast_verbose("%d headers, %d lines\n", p->initreq.headers, p->initreq.lines);
	determine_firstline_parts(&p->initreq);
	p->lastinvite = p->ocseq;
	p->outgoing = 1;
	return send_request(p, &req, 1, p->ocseq);
}

static void extract_uri(struct sip_pvt *p, struct sip_request *req)
{
	char stripped[256]="";
	char *c, *n;
	strncpy(stripped, get_header(req, "Contact"), sizeof(stripped) - 1);
	c = strchr(stripped, '<');
	if (c) 
		c++;
	else
		c = stripped;
	n = strchr(c, '>');
	if (n)
		*n = '\0';
	n = strchr(c, ';');
	if (n)
		*n = '\0';
	if (c && !ast_strlen_zero(c))
		strncpy(p->uri, c, sizeof(p->uri) - 1);
}

static void build_contact(struct sip_pvt *p)
{
	/* Construct Contact: header */
	if (ourport != 5060)
		snprintf(p->our_contact, sizeof(p->our_contact), "<sip:%s@%s:%d>", p->exten, inet_ntoa(p->ourip), ourport);
	else
		snprintf(p->our_contact, sizeof(p->our_contact), "<sip:%s@%s>", p->exten, inet_ntoa(p->ourip));
}

static void initreqprep(struct sip_request *req, struct sip_pvt *p, char *cmd, char *vxml_url)
{
	char invite[256];
	char from[256];
	char to[256];
	char tmp[80];
	char cid[256];
	char *l = callerid, *n=NULL;

	snprintf(p->lastmsg, sizeof(p->lastmsg), "Init: %s", cmd);

	if (p->owner && p->owner->callerid) {
		strcpy(cid, p->owner->callerid);
		ast_callerid_parse(cid, &n, &l);
		if (l) 
			ast_shrink_phone_number(l);
		if (!l || !ast_isphonenumber(l))
				l = callerid;
	}
	/* if user want's his callerid restricted */
	if (p->restrictcid) {
		l = CALLERID_UNKNOWN;
		n = l;
	}
	if (!n || ast_strlen_zero(n))
		n = l;
	/* Allow user to be overridden */
	if (!ast_strlen_zero(p->fromuser))
		l = p->fromuser;

	if ((ourport != 5060) && ast_strlen_zero(p->fromdomain))
		snprintf(from, sizeof(from), "\"%s\" <sip:%s@%s:%d>;tag=as%08x", n, l, strlen(p->fromdomain) ? p->fromdomain : inet_ntoa(p->ourip), ourport, p->tag);
	else
		snprintf(from, sizeof(from), "\"%s\" <sip:%s@%s>;tag=as%08x", n, l, strlen(p->fromdomain) ? p->fromdomain : inet_ntoa(p->ourip), p->tag);

	if (!ast_strlen_zero(p->username)) {
		if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
			snprintf(invite, sizeof(invite), "sip:%s@%s:%d",p->username, p->tohost, ntohs(p->sa.sin_port));
		} else {
			snprintf(invite, sizeof(invite), "sip:%s@%s",p->username, p->tohost);
		}
	} else if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
		snprintf(invite, sizeof(invite), "sip:%s:%d", p->tohost, ntohs(p->sa.sin_port));
	} else {
		snprintf(invite, sizeof(invite), "sip:%s", p->tohost);
	}
	strncpy(p->uri, invite, sizeof(p->uri) - 1);
	/* If there is a VXML URL append it to the SIP URL */
	if (vxml_url)
	{
		snprintf(to, sizeof(to), "<%s>;%s", invite, vxml_url);
	}
	else
	{
		snprintf(to, sizeof(to), "<%s>", invite );
	}
	memset(req, 0, sizeof(struct sip_request));
	init_req(req, cmd, invite);
	snprintf(tmp, sizeof(tmp), "%d %s", ++p->ocseq, cmd);

	add_header(req, "Via", p->via);
	/* SLD: FIXME?: do Route: here too?  I think not cos this is the first request.
	 * OTOH, then we won't have anything in p->route anyway */
	add_header(req, "From", from);
	strncpy(p->exten, l, sizeof(p->exten) - 1);
	build_contact(p);
	add_header(req, "To", to);
	add_header(req, "Contact", p->our_contact);
	add_header(req, "Call-ID", p->callid);
	add_header(req, "CSeq", tmp);
	add_header(req, "User-Agent", "Asterisk PBX");
}

static int transmit_invite(struct sip_pvt *p, char *cmd, int sdp, char *auth, char *authheader, char *vxml_url, char *distinctive_ring, int init)
{
	struct sip_request req;
	
	if (init) {
		/* Bump branch even on initial requests */
		p->branch ^= rand();
		snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", inet_ntoa(p->ourip), ourport, p->branch);
		initreqprep(&req, p, cmd, vxml_url);
	} else
		reqprep(&req, p, cmd, 0, 1);
		
	if (auth)
		add_header(&req, authheader, auth);
	append_date(&req);
	if (!strcasecmp(cmd, "REFER")) {
		if (!ast_strlen_zero(p->refer_to))
			add_header(&req, "Refer-To", p->refer_to);
		if (!ast_strlen_zero(p->referred_by))
			add_header(&req, "Referred-By", p->referred_by);
	}
	
	if (distinctive_ring)
	{
		add_header(&req, "Alert-info",distinctive_ring);
	}
	add_header(&req, "Allow", ALLOWED_METHODS);
	if (sdp) {
		add_sdp(&req, p, NULL, NULL);
	} else {
		add_header(&req, "Content-Length", "0");
		add_blank_header(&req);
	}

	if (!p->initreq.headers) {
		/* Use this as the basis */
		copy_request(&p->initreq, &req);
		parse(&p->initreq);
		if (sip_debug_test_pvt(p))
			ast_verbose("%d headers, %d lines\n", p->initreq.headers, p->initreq.lines);
		determine_firstline_parts(&p->initreq);
	}
	p->lastinvite = p->ocseq;
	return send_request(p, &req, init ? 2 : 1, p->ocseq);
}

static int transmit_state_notify(struct sip_pvt *p, int state, int full)
{
	char tmp[2000];
	char from[256], to[256];
	char *t, *c, *a;
	char *mfrom, *mto;
	struct sip_request req;
	char clen[20];
	
	strncpy(from, get_header(&p->initreq, "From"), sizeof(from)-1);

	c = ditch_braces(from);
	if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return -1;
	}
	if ((a = strchr(c, ';'))) {
		*a = '\0';
	}
	mfrom = c;
		
	reqprep(&req, p, "NOTIFY", 0, 1);

	if (p->subscribed == 1) {
    	    strncpy(to, get_header(&p->initreq, "To"), sizeof(to)-1);

	    c = ditch_braces(to);
	    if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return -1;
	    }
	    if ((a = strchr(c, ';'))) {
		*a = '\0';
	    }
	    mto = c;

	    add_header(&req, "Content-Type", "application/xpidf+xml");

	    if ((state==AST_EXTENSION_UNAVAILABLE) || (state==AST_EXTENSION_BUSY))
		state = 2;
	    else if (state==AST_EXTENSION_INUSE)
		state = 1;
	    else
		state = 0;
	    
	    t = tmp;		
	    sprintf(t, "<?xml version=\"1.0\"?>\n");
	    t = tmp + strlen(tmp);
	    sprintf(t, "<!DOCTYPE presence PUBLIC \"-//IETF//DTD RFCxxxx XPIDF 1.0//EN\" \"xpidf.dtd\">\n");
	    t = tmp + strlen(tmp);
	    sprintf(t, "<presence>\n");
	    t = tmp + strlen(tmp);
	    sprintf(t, "<presentity uri=\"%s;method=SUBSCRIBE\" />\n", mfrom);
	    t = tmp + strlen(tmp);
	    sprintf(t, "<atom id=\"%s\">\n", p->exten);
	    t = tmp + strlen(tmp);
	    sprintf(t, "<address uri=\"%s;user=ip\" priority=\"0,800000\">\n", mto);
	    t = tmp + strlen(tmp);
	    sprintf(t, "<status status=\"%s\" />\n", !state ? "open" : (state==1) ? "inuse" : "closed");
	    t = tmp + strlen(tmp);
	    sprintf(t, "<msnsubstatus substatus=\"%s\" />\n", !state ? "online" : (state==1) ? "onthephone" : "offline");
	    t = tmp + strlen(tmp);
	    sprintf(t, "</address>\n</atom>\n</presence>\n");	    	
	} else {
    	    add_header(&req, "Event", "dialog");
	    add_header(&req, "Content-Type", "application/dialog-info+xml");
	
	    t = tmp;		
	    sprintf(t, "<?xml version=\"1.0\"?>\n");
	    t = tmp + strlen(tmp);
	    sprintf(t, "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\"%d\" state=\"%s\" entity=\"%s\">\n", p->dialogver++, full ? "full":"partial", mfrom);
	    t = tmp + strlen(tmp);
	    sprintf(t, "<dialog id=\"%s\">\n", p->exten);
	    t = tmp + strlen(tmp);
	    sprintf(t, "<state>%s</state>\n", state ? "confirmed" : "terminated");
	    t = tmp + strlen(tmp);
	    sprintf(t, "</dialog>\n</dialog-info>\n");	
	}
	if (t > tmp + sizeof(tmp))
		ast_log(LOG_WARNING, "Buffer overflow detected!!  (Please file a bug report)\n");

	snprintf(clen, sizeof(clen), "%d", strlen(tmp));
	add_header(&req, "Content-Length", clen);
	add_line(&req, tmp);

	return send_request(p, &req, 1, p->ocseq);
}

static int transmit_notify(struct sip_pvt *p, int newmsgs, int oldmsgs)
{
	struct sip_request req;
	char tmp[256];
	char tmp2[256];
	char clen[20];
	initreqprep(&req, p, "NOTIFY", NULL);
	add_header(&req, "Event", "message-summary");
	add_header(&req, "Content-Type", notifymime);

	snprintf(tmp, sizeof(tmp), "Messages-Waiting: %s\n", newmsgs ? "yes" : "no");
	snprintf(tmp2, sizeof(tmp2), "Voicemail: %d/%d\n", newmsgs, oldmsgs);
	snprintf(clen, sizeof(clen), "%d", strlen(tmp) + strlen(tmp2));
	add_header(&req, "Content-Length", clen);
	add_line(&req, tmp);
	add_line(&req, tmp2);

	if (!p->initreq.headers) {
		/* Use this as the basis */
		copy_request(&p->initreq, &req);
		parse(&p->initreq);
		if (sip_debug_test_pvt(p))
			ast_verbose("%d headers, %d lines\n", p->initreq.headers, p->initreq.lines);
		determine_firstline_parts(&p->initreq);
	}

	return send_request(p, &req, 1, p->ocseq);
}

static int transmit_register(struct sip_registry *r, char *cmd, char *auth, char *authheader);

static int sip_reregister(void *data) 
{
	/* if we are here, we know that we need to reregister. */
	struct sip_registry *r=(struct sip_registry *)data;
	ast_mutex_lock(&regl.lock);
	r->expire = -1;
	__sip_do_register(r);
	ast_mutex_unlock(&regl.lock);
	return 0;
}


static int __sip_do_register(struct sip_registry *r)
{
	int res;
	res=transmit_register(r, "REGISTER", NULL, NULL);
	return res;
}

static int sip_reg_timeout(void *data)
{
	/* if we are here, our registration timed out, so we'll just do it over */
	struct sip_registry *r=data;
	struct sip_pvt *p;
	int res;
	ast_mutex_lock(&regl.lock);
	ast_log(LOG_NOTICE, "Registration for '%s@%s' timed out, trying again\n", r->username, inet_ntoa(r->addr.sin_addr)); 
	if (r->call) {
		/* Unlink us, destroy old call.  Locking is not relevent here because all this happens
		   in the single SIP manager thread. */
		p = r->call;
		p->registry = NULL;
		r->call = NULL;
		p->needdestroy = 1;
	}
	r->regstate=REG_STATE_UNREGISTERED;
	r->timeout = -1;
	res=transmit_register(r, "REGISTER", NULL, NULL);
	ast_mutex_unlock(&regl.lock);
	return 0;
}

static int transmit_register(struct sip_registry *r, char *cmd, char *auth, char *authheader)
{
	struct sip_request req;
	char from[256];
	char to[256];
	char tmp[80];
	char via[80];
	char addr[80];
	struct sip_pvt *p;
	struct ast_hostent ahp;
	struct hostent *hp;

	/* exit if we are already in process with this registrar ?*/
	if ( r == NULL || ((auth==NULL) && (r->regstate==REG_STATE_REGSENT || r->regstate==REG_STATE_AUTHSENT))) {
		ast_log(LOG_NOTICE, "Strange, trying to register when registration already pending\n");
		return 0;
	}

	if (r->call) {
		if (!auth) {
			ast_log(LOG_WARNING, "Already have a call??\n");
			return 0;
		} else
			p = r->call;
	} else {
		if (!r->callid_valid) {
			build_callid(r->callid, sizeof(r->callid), __ourip);
			r->callid_valid = 1;
		}
		p=sip_alloc( r->callid, &r->addr, 0);
		if (!p) {
			ast_log(LOG_WARNING, "Unable to allocate registration call\n");
			return 0;
		}
		p->outgoing = 1;
		r->call=p;
		p->registry=r;
		strncpy(p->peersecret, r->secret, sizeof(p->peersecret)-1);
		strncpy(p->peermd5secret, r->md5secret, sizeof(p->peermd5secret)-1);
		if (!ast_strlen_zero(r->authuser)) {
			strncpy(p->peername, r->authuser, sizeof(p->peername)-1);
			strncpy(p->authname, r->authuser, sizeof(p->authname)-1);
		} else {
			strncpy(p->peername, r->username, sizeof(p->peername)-1);
			strncpy(p->authname, r->username, sizeof(p->authname)-1);
		}
		strncpy(p->username, r->username, sizeof(p->username)-1);
		strncpy(p->exten, r->contact, sizeof(p->exten) - 1);

		/*
		  check which address we should use in our contact header 
		  based on whether the remote host is on the external or
		  internal network so we can register through nat
		 */
		if ((hp = ast_gethostbyname(r->hostname, &ahp))) {
			if (ast_sip_ouraddrfor((struct in_addr *)hp->h_addr, &p->ourip))
				memcpy(&p->ourip, &bindaddr.sin_addr, sizeof(p->ourip));
		}
		build_contact(p);
	}

	/* set up a timeout */
	if (auth==NULL)  {
		if (r->timeout > -1) {
			ast_log(LOG_WARNING, "Still have a timeout, %d\n", r->timeout);
			ast_sched_del(sched, r->timeout);
		}
		r->timeout = ast_sched_add(sched, 20*1000, sip_reg_timeout, r);
		ast_log(LOG_DEBUG, "Scheduled a timeout # %d\n", r->timeout);
	}

	if (strchr(r->username, '@')) {
		snprintf(from, sizeof(from), "<sip:%s>;tag=as%08x", r->username, p->tag);
		snprintf(to, sizeof(to),     "<sip:%s>", r->username);
	} else {
		snprintf(from, sizeof(from), "<sip:%s@%s>;tag=as%08x", r->username, r->hostname, p->tag);
		snprintf(to, sizeof(to),     "<sip:%s@%s>", r->username, r->hostname);
	}
	
	snprintf(addr, sizeof(addr), "sip:%s", r->hostname);
	strncpy(p->uri, addr, sizeof(p->uri) - 1);

	p->branch ^= rand();

	memset(&req, 0, sizeof(req));
	init_req(&req, cmd, addr);

	snprintf(tmp, sizeof(tmp), "%u %s", ++r->ocseq, cmd);
	p->ocseq = r->ocseq;

	/* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
	snprintf(via, sizeof(via), "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", inet_ntoa(p->ourip), ourport, p->branch);
	add_header(&req, "Via", via);
	add_header(&req, "From", from);
	add_header(&req, "To", to);
	add_header(&req, "Call-ID", p->callid);
	add_header(&req, "CSeq", tmp);
	add_header(&req, "User-Agent", "Asterisk PBX");
	if (auth) 
		add_header(&req, authheader, auth);

	snprintf(tmp, sizeof(tmp), "%d", default_expiry);
	add_header(&req, "Expires", tmp);
	add_header(&req, "Contact", p->our_contact);
	add_header(&req, "Event", "registration");
	add_header(&req, "Content-Length", "0");
	add_blank_header(&req);
	copy_request(&p->initreq, &req);
	parse(&p->initreq);
	if (sip_debug_test_pvt(p))
		ast_verbose("%d headers, %d lines\n", p->initreq.headers, p->initreq.lines);
	determine_firstline_parts(&p->initreq);
	r->regstate=auth?REG_STATE_AUTHSENT:REG_STATE_REGSENT;
	return send_request(p, &req, 2, p->ocseq);
}

static int transmit_message_with_text(struct sip_pvt *p, char *text)
{
	struct sip_request req;
	reqprep(&req, p, "MESSAGE", 0, 1);
	add_text(&req, text);
	return send_request(p, &req, 1, p->ocseq);
}

static int transmit_refer(struct sip_pvt *p, char *dest)
{
	struct sip_request req;
	char from[256];
	char *of, *c;
	char referto[256];
	if (p->outgoing) 
		of = get_header(&p->initreq, "To");
	else
		of = get_header(&p->initreq, "From");
	strncpy(from, of, sizeof(from) - 1);
	of = ditch_braces(from);
	strncpy(p->from,of,sizeof(p->from) - 1);
	if (strncmp(of, "sip:", 4)) {
		ast_log(LOG_NOTICE, "From address missing 'sip:', using it anyway\n");
	} else
		of += 4;
	/* Get just the username part */
	if ((c = strchr(of, '@'))) {
		*c = '\0';
		c++;
	}
	if (c) {
		snprintf(referto, sizeof(referto), "<sip:%s@%s>", dest, c);
	} else {
		snprintf(referto, sizeof(referto), "<sip:%s>", dest);
	}

	/* save in case we get 407 challenge */
	strncpy(p->refer_to, referto, sizeof(p->refer_to) - 1); 
	strncpy(p->referred_by, p->our_contact, sizeof(p->referred_by) - 1); 

	reqprep(&req, p, "REFER", 0, 1);
	add_header(&req, "Refer-To", referto);
	if (!ast_strlen_zero(p->our_contact))
		add_header(&req, "Referred-By", p->our_contact);
	add_blank_header(&req);
	return send_request(p, &req, 1, p->ocseq);
}

static int transmit_info_with_digit(struct sip_pvt *p, char digit)
{
	struct sip_request req;
	reqprep(&req, p, "INFO", 0, 1);
	add_digit(&req, digit);
	return send_request(p, &req, 1, p->ocseq);
}

static int transmit_request(struct sip_pvt *p, char *msg, int seqno, int reliable, int newbranch)
{
	struct sip_request resp;
	reqprep(&resp, p, msg, seqno, newbranch);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_request(p, &resp, reliable, seqno ? seqno : p->ocseq);
}

static int transmit_request_with_auth(struct sip_pvt *p, char *msg, int seqno, int reliable, int newbranch)
{
	struct sip_request resp;
	reqprep(&resp, p, msg, seqno, newbranch);
	if (*p->realm)
	{
		char digest[1024];
		memset(digest,0,sizeof(digest));
		build_reply_digest(p, msg, digest, sizeof(digest));
		add_header(&resp, "Proxy-Authorization", digest);
	}

	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_request(p, &resp, reliable, seqno ? seqno : p->ocseq);	
}

static int expire_register(void *data)
{
	struct sip_peer *p = data;
	memset(&p->addr, 0, sizeof(p->addr));
	ast_db_del("SIP/Registry", p->name);
	p->expire = -1;
	ast_device_state_changed("SIP/%s", p->name);
	if (p->selfdestruct) {
		p->delme = 1;
		prune_peers();
	}
	return 0;
}

static int sip_poke_peer(struct sip_peer *peer);

static void reg_source_db(struct sip_peer *p)
{
	char data[80];
	struct in_addr in;
	char *c, *d, *u;
	int expiry;
	if (!ast_db_get("SIP/Registry", p->name, data, sizeof(data))) {
		c = strchr(data, ':');
		if (c) {
			*c = '\0';
			c++;
			if (inet_aton(data, &in)) {
				d = strchr(c, ':');
				if (d) {
					*d = '\0';
					d++;
					u = strchr(d, ':');
					if (u) {
						*u = '\0';
						u++;
						strncpy(p->username, u, sizeof(p->username));
					}
					ast_verbose(VERBOSE_PREFIX_3 "SIP Seeding '%s' at %s@%s:%d for %d\n", p->name, 
						p->username, inet_ntoa(in), atoi(c), atoi(d));
					sip_poke_peer(p);
					expiry = atoi(d);
					memset(&p->addr, 0, sizeof(p->addr));
					p->addr.sin_family = AF_INET;
					p->addr.sin_addr = in;
					p->addr.sin_port = htons(atoi(c));
					if (p->expire > -1)
						ast_sched_del(sched, p->expire);
					p->expire = ast_sched_add(sched, (expiry + 10) * 1000, expire_register, (void *)p);
				}					
					
			}
		}
	}
}

static int parse_contact(struct sip_pvt *pvt, struct sip_peer *p, struct sip_request *req)
{
	char contact[80]= ""; 
	char data[256];
	char *expires = get_header(req, "Expires");
	int expiry = atoi(expires);
	char *c, *n, *pt;
	int port;
	struct hostent *hp;
	struct ast_hostent ahp;
	struct sockaddr_in oldsin;
	if (ast_strlen_zero(expires)) {
		expires = strstr(get_header(req, "Contact"), "expires=");
		if (expires) {
			if (sscanf(expires + 8, "%d;", &expiry) != 1)
				expiry = default_expiry;
		} else {
			/* Nothing has been specified */
			expiry = default_expiry;
		}
	}
	/* Look for brackets */
	strncpy(contact, get_header(req, "Contact"), sizeof(contact) - 1);
	c = contact;
	
	if ((n=strchr(c, '<'))) {
		c = n + 1;
		n = strchr(c, '>');
		/* Lose the part after the > */
		if (n) 
			*n = '\0';
	}
	if (!strcasecmp(c, "*") || !expiry) {
		/* This means remove all registrations and return OK */
		memset(&p->addr, 0, sizeof(p->addr));
		if (p->expire > -1)
			ast_sched_del(sched, p->expire);
		p->expire = -1;
		ast_db_del("SIP/Registry", p->name);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Unregistered SIP '%s'\n", p->name);
		return 0;
	}
	/* Make sure it's a SIP URL */
	if (strncasecmp(c, "sip:", 4)) {
		ast_log(LOG_NOTICE, "'%s' is not a valid SIP contact (missing sip:) trying to use anyway\n", c);
	} else
		c += 4;
	/* Ditch q */
	n = strchr(c, ';');
	if (n) 
		*n = '\0';
	/* Grab host */
	n = strchr(c, '@');
	if (!n) {
		n = c;
		c = NULL;
	} else {
		*n = '\0';
		n++;
	}
	pt = strchr(n, ':');
	if (pt) {
		*pt = '\0';
		pt++;
		port = atoi(pt);
	} else
		port = DEFAULT_SIP_PORT;
	memcpy(&oldsin, &p->addr, sizeof(oldsin));
	if (!p->nat) {
		/* XXX This could block for a long time XXX */
		hp = ast_gethostbyname(n, &ahp);
		if (!hp)  {
			ast_log(LOG_WARNING, "Invalid host '%s'\n", n);
			return -1;
		}
		p->addr.sin_family = AF_INET;
		memcpy(&p->addr.sin_addr, hp->h_addr, sizeof(p->addr.sin_addr));
		p->addr.sin_port = htons(port);
	} else {
		/* Don't trust the contact field.  Just use what they came to us
		   with */
		memcpy(&p->addr, &pvt->recv, sizeof(p->addr));
	}
	if (c)
		strncpy(p->username, c, sizeof(p->username) - 1);
	else
		strcpy(p->username, "");
	if (p->expire > -1)
		ast_sched_del(sched, p->expire);
	if ((expiry < 1) || (expiry > max_expiry))
		expiry = max_expiry;
	if (!p->temponly)
		p->expire = ast_sched_add(sched, (expiry + 10) * 1000, expire_register, p);
	pvt->expiry = expiry;
	snprintf(data, sizeof(data), "%s:%d:%d:%s", inet_ntoa(p->addr.sin_addr), ntohs(p->addr.sin_port), expiry, p->username);
	ast_db_put("SIP/Registry", p->name, data);
	if (inaddrcmp(&p->addr, &oldsin)) {
		sip_poke_peer(p);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Registered SIP '%s' at %s port %d expires %d\n", p->name, inet_ntoa(p->addr.sin_addr), ntohs(p->addr.sin_port), expiry);
	}
	return 0;
}

static void free_old_route(struct sip_route *route)
{
	struct sip_route *next;
	while (route) {
		next = route->next;
		free(route);
		route = next;
	}
}

static void list_route(struct sip_route *route)
{
	if (!route) {
		ast_verbose("list_route: no route\n");
		return;
	}
	while (route) {
		ast_verbose("list_route: hop: <%s>\n", route->hop);
		route = route->next;
	}
}

static void build_route(struct sip_pvt *p, struct sip_request *req, int backwards)
{
	struct sip_route *thishop, *head, *tail;
	int start = 0;
	int len;
	char *rr, *contact, *c;

	/* Once a persistant route is set, don't fool with it */
	if (p->route && p->route_persistant) {
		ast_log(LOG_DEBUG, "build_route: Retaining previous route: <%s>\n", p->route->hop);
		return;
	}

	if (p->route) {
		free_old_route(p->route);
		p->route = NULL;
	}
	
	p->route_persistant = backwards;
	
	/* We build up head, then assign it to p->route when we're done */
	head = NULL;  tail = head;
	/* 1st we pass through all the hops in any Record-Route headers */
	for (;;) {
		/* Each Record-Route header */
		rr = __get_header(req, "Record-Route", &start);
		if (*rr == '\0') break;
		for (;;) {
			/* Each route entry */
			/* Find < */
			rr = strchr(rr, '<');
			if (!rr) break; /* No more hops */
			++rr;
			len = strcspn(rr, ">");
			/* Make a struct route */
			thishop = (struct sip_route *)malloc(sizeof(struct sip_route)+len+1);
			if (thishop) {
				strncpy(thishop->hop, rr, len);
				thishop->hop[len] = '\0';
				ast_log(LOG_DEBUG, "build_route: Record-Route hop: <%s>\n", thishop->hop);
				/* Link in */
				if (backwards) {
					/* Link in at head so they end up in reverse order */
					thishop->next = head;
					head = thishop;
					/* If this was the first then it'll be the tail */
					if (!tail) tail = thishop;
				} else {
					thishop->next = NULL;
					/* Link in at the end */
					if (tail)
						tail->next = thishop;
					else
						head = thishop;
					tail = thishop;
				}
			}
			rr += len+1;
		}
	}
	/* 2nd append the Contact: if there is one */
	/* Can be multiple Contact headers, comma separated values - we just take the first */
	contact = get_header(req, "Contact");
	if (!ast_strlen_zero(contact)) {
		ast_log(LOG_DEBUG, "build_route: Contact hop: %s\n", contact);
		/* Look for <: delimited address */
		c = strchr(contact, '<');
		if (c) {
			/* Take to > */
			++c;
			len = strcspn(c, ">");
		} else {
			/* No <> - just take the lot */
			c = contact; len = strlen(contact);
		}
		thishop = (struct sip_route *)malloc(sizeof(struct sip_route)+len+1);
		if (thishop) {
			strncpy(thishop->hop, c, len);
			thishop->hop[len] = '\0';
			thishop->next = NULL;
			/* Goes at the end */
			if (tail)
				tail->next = thishop;
			else
				head = thishop;
		}
	}
	/* Store as new route */
	p->route = head;

	/* For debugging dump what we ended up with */
	if (sip_debug_test_pvt(p))
		list_route(p->route);
}

static void md5_hash(char *output, char *input)
{
		struct MD5Context md5;
		unsigned char digest[16];
		char *ptr;
		int x;
		MD5Init(&md5);
		MD5Update(&md5, input, strlen(input));
		MD5Final(digest, &md5);
		ptr = output;
		for (x=0;x<16;x++)
			ptr += sprintf(ptr, "%2.2x", digest[x]);
}

static int check_auth(struct sip_pvt *p, struct sip_request *req, char *randdata, int randlen, char *username, char *secret, char *md5secret, char *method, char *uri, int reliable, int ignore)
{
	int res = -1;
	/* Always OK if no secret */
	if (ast_strlen_zero(secret) && ast_strlen_zero(md5secret))
		return 0;
	if (ignore && !ast_strlen_zero(randdata) && ast_strlen_zero(get_header(req, "Proxy-Authorization"))) {
		/* This is a retransmitted invite/register/etc, don't reconstruct authentication
		   information */
		if (!ast_strlen_zero(randdata)) {
			if (!reliable) {
				/* Resend message if this was NOT a reliable delivery.   Otherwise the
				   retransmission should get it */
				transmit_response_with_auth(p, "407 Proxy Authentication Required", req, randdata, reliable);
				/* Schedule auto destroy in 15 seconds */
				sip_scheddestroy(p, 15000);
			}
			res = 1;
		}
	} else if (ast_strlen_zero(randdata) || ast_strlen_zero(get_header(req, "Proxy-Authorization"))) {
		snprintf(randdata, randlen, "%08x", rand());
		transmit_response_with_auth(p, "407 Proxy Authentication Required", req, randdata, reliable);
		/* Schedule auto destroy in 15 seconds */
		sip_scheddestroy(p, 15000);
		res = 1;
	} else {
		/* Whoever came up with the authentication section of SIP can suck my %&#$&* for not putting
		   an example in the spec of just what it is you're doing a hash on. */
		char a1[256];
		char a2[256];
		char a1_hash[256];
		char a2_hash[256];
		char resp[256];
		char resp_hash[256]="";
		char tmp[256] = "";
		char *c;
		char *z;
		char *response ="";
		char *resp_uri ="";

		/* Find their response among the mess that we'r sent for comparison */
		strncpy(tmp, get_header(req, "Proxy-Authorization"), sizeof(tmp) - 1);
		c = tmp;

		while(c) {
			while (*c && (*c < 33)) c++;
			if (!*c)
				break;
			if (!strncasecmp(c, "response=", strlen("response="))) {
				c+= strlen("response=");
				if ((*c == '\"')) {
					response=++c;
					if((c = strchr(c,'\"')))
						*c = '\0';

				} else {
					response=c;
					if((c = strchr(c,',')))
						*c = '\0';
				}

			} else if (!strncasecmp(c, "uri=", strlen("uri="))) {
				c+= strlen("uri=");
				if ((*c == '\"')) {
					resp_uri=++c;
					if((c = strchr(c,'\"')))
						*c = '\0';
				} else {
					resp_uri=c;
					if((c = strchr(c,',')))
						*c = '\0';
				}

			} else
				if ((z = strchr(c,' ')) || (z = strchr(c,','))) c=z;
			if (c)
				c++;
		}
		snprintf(a1, sizeof(a1), "%s:%s:%s", username, "asterisk", secret);
		if(!ast_strlen_zero(resp_uri))
			snprintf(a2, sizeof(a2), "%s:%s", method, resp_uri);
		else
			snprintf(a2, sizeof(a2), "%s:%s", method, uri);
		if (!ast_strlen_zero(md5secret))
		        snprintf(a1_hash, sizeof(a1_hash), "%s", md5secret);
		else
		        md5_hash(a1_hash, a1);
		md5_hash(a2_hash, a2);
		snprintf(resp, sizeof(resp), "%s:%s:%s", a1_hash, randdata, a2_hash);
		md5_hash(resp_hash, resp);

		/* resp_hash now has the expected response, compare the two */

		if (response && !strncasecmp(response, resp_hash, strlen(resp_hash))) {
			/* Auth is OK */
			res = 0;
		}
		/* Assume success ;-) */
	}
	return res;
}

static int cb_extensionstate(char *context, char* exten, int state, void *data)
{
    struct sip_pvt *p = data;
    if (state == -1) {
	sip_scheddestroy(p, 15000);
	p->stateid = -1;
	return 0;
    }
    
    transmit_state_notify(p, state, 1);
    
    if (option_debug)
        ast_verbose(VERBOSE_PREFIX_1 "Extension Changed %s new state %d for Notify User %s\n", exten, state, p->username);
    return 0;
}

static int register_verify(struct sip_pvt *p, struct sockaddr_in *sin, struct sip_request *req, char *uri, int ignore)
{
	int res = -1;
	struct sip_peer *peer;
	char tmp[256] = "";
	char *name, *c;
	char *t;
	/* Terminate URI */
	t = uri;
	while(*t && (*t > 32) && (*t != ';'))
		t++;
	*t = '\0';
	
	strncpy(tmp, get_header(req, "To"), sizeof(tmp) - 1);
	c = ditch_braces(tmp);
	/* Ditch ;user=phone */
	name = strchr(c, ';');
	if (name)
		*name = '\0';

	if (!strncmp(c, "sip:", 4)) {
		name = c + 4;
	} else {
		name = c;
		ast_log(LOG_NOTICE, "Invalid to address: '%s' from %s (missing sip:) trying to use anyway...\n", c, inet_ntoa(sin->sin_addr));
	}
	c = strchr(name, '@');
	if (c) 
		*c = '\0';
	strncpy(p->exten, name, sizeof(p->exten) - 1);
	build_contact(p);
	ast_mutex_lock(&peerl.lock);
	peer = find_peer(name, NULL);
	if (!(peer && ast_apply_ha(peer->ha, sin))) {
		if (peer && peer->temponly) {
			if (peer->ha) {
				ast_free_ha(peer->ha);
			}
			free(peer);
		}
		peer = NULL;
	}
	ast_mutex_unlock(&peerl.lock);

	if (peer) {
			if (!peer->dynamic) {
				ast_log(LOG_NOTICE, "Peer '%s' is trying to register, but not configured as host=dynamic\n", peer->name);
			} else {
				p->nat = peer->nat;
				transmit_response(p, "100 Trying", req);
				if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), peer->name, peer->secret, peer->md5secret, "REGISTER", uri, 0, ignore))) {
					sip_cancel_destroy(p);
					if (parse_contact(p, peer, req)) {
						ast_log(LOG_WARNING, "Failed to parse contact info\n");
					} else {
					update_peer(peer, p->expiry);
					/* Say OK and ask subsystem to retransmit msg counter */
						transmit_response_with_date(p, "200 OK", req);
						peer->lastmsgssent = -1;
						res = 0;
					}
				} 
			}
	}
	if (!peer && autocreatepeer) {
		/* Create peer if we have autocreate mode enabled */
		peer = temp_peer(name);
		if (peer) {
			peer->next = peerl.peers;
			peerl.peers = peer;
			peer->lastmsgssent = -1;
			sip_cancel_destroy(p);
			if (parse_contact(p, peer, req)) {
				ast_log(LOG_WARNING, "Failed to parse contact info\n");
			} else {
				/* Say OK and ask subsystem to retransmit msg counter */
				transmit_response_with_date(p, "200 OK", req);
				peer->lastmsgssent = -1;
				res = 0;
			}
		}
	}
	if (!res) {
	    ast_device_state_changed("SIP/%s", peer->name);
	}
	if (res < 0)
		transmit_response(p, "401 Unauthorized", &p->initreq);
	if (peer && peer->temponly) {
		if (peer->ha) {
			ast_free_ha(peer->ha);
		}
		free(peer);
	}
	return res;
}

static int get_rdnis(struct sip_pvt *p, struct sip_request *oreq)
{
	char tmp[256] = "", *c, *a;
	struct sip_request *req;
	
	req = oreq;
	if (!req)
		req = &p->initreq;
	strncpy(tmp, get_header(req, "Diversion"), sizeof(tmp) - 1);
	if (ast_strlen_zero(tmp))
		return 0;
	c = ditch_braces(tmp);
	if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not an RDNIS SIP header (%s)?\n", c);
		return -1;
	}
	c += 4;
	if ((a = strchr(c, '@')) || (a = strchr(c, ';'))) {
		*a = '\0';
	}
	if (sip_debug_test_pvt(p))
		ast_verbose("RDNIS is %s\n", c);
	strncpy(p->rdnis, c, sizeof(p->rdnis) - 1);

	return 0;
}
static int get_destination(struct sip_pvt *p, struct sip_request *oreq)
{
	char tmp[256] = "", *c, *a;
	char tmpf[256]= "", *fr;
	struct sip_request *req;
	
	req = oreq;
	if (!req)
		req = &p->initreq;
	if (req->rlPart2)
		strncpy(tmp, req->rlPart2, sizeof(tmp) - 1);
	c = ditch_braces(tmp);
	
	strncpy(tmpf, get_header(req, "From"), sizeof(tmpf) - 1);
	fr = ditch_braces(tmpf);
	
	if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return -1;
	}
	c += 4;
	if (!ast_strlen_zero(fr)) {
		if (strncmp(fr, "sip:", 4)) {
			ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", fr);
			return -1;
		}
		fr += 4;
	} else
		fr = NULL;
	if ((a = strchr(c, '@'))) {
		*a = '\0';
		a++;
		strncpy(p->domain, a, sizeof(p->domain)-1);
	}
	if ((a = strchr(c, ';'))) {
		*a = '\0';
	}
	if (fr) {
		if ((a = strchr(fr, ';')))
			*a = '\0';
		if ((a = strchr(fr, '@'))) {
			*a = '\0';
			strncpy(p->fromdomain, a + 1, sizeof(p->fromdomain) - 1);
		} else
			strncpy(p->fromdomain, fr, sizeof(p->fromdomain) - 1);
	}
	if (sip_debug_test_pvt(p))
		ast_verbose("Looking for %s in %s\n", c, p->context);
	if (ast_exists_extension(NULL, p->context, c, 1, fr) ||
		!strcmp(c, ast_pickup_ext())) {
		if (!oreq)
			strncpy(p->exten, c, sizeof(p->exten) - 1);
		return 0;
	}

	if (ast_canmatch_extension(NULL, p->context, c, 1, fr) ||
	    !strncmp(c, ast_pickup_ext(),strlen(c))) {
		return 1;
	}
	
	return -1;
}

static int hex2int(char a)
{
	if ((a >= '0') && (a <= '9')) {
		return a - '0';
	} else if ((a >= 'a') && (a <= 'f')) {
		return a - 'a' + 10;
	} else if ((a >= 'A') && (a <= 'F')) {
		return a - 'A' + 10;
	}
	return 0;
}

static int get_refer_info(struct sip_pvt *p, struct sip_request *oreq)
{
	char tmp[256] = "", *c, *a;
	char tmp2[256] = "", *c2, *a2;
	char tmp3[256];
	char tmp4[256];
	char tmp5[256] = "";		/* CallID to replace */
	struct sip_request *req;
	struct sip_pvt *p2;
	
	req = oreq;
	if (!req)
		req = &p->initreq;
	strncpy(tmp, get_header(req, "Refer-To"), sizeof(tmp) - 1);
	strncpy(tmp2, get_header(req, "Referred-By"), sizeof(tmp2) - 1);
	strncpy(tmp3, get_header(req, "Contact"), sizeof(tmp3) - 1);
	strncpy(tmp4, get_header(req, "Remote-Party-ID"), sizeof(tmp4) - 1);
	
	c = ditch_braces(tmp);
	c2 = ditch_braces(tmp2);
	
		
	if (strncmp(c, "sip:", 4) && strncmp(c2, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c2);
		return -1;
	}
	c += 4;
	c2 += 4;
	if ((a = strchr(c, '?'))) {
		/* Search for arguemnts */
		*a = '\0';
		a++;
		if (!strncasecmp(a, "REPLACES=", strlen("REPLACES="))) {
			strncpy(tmp5, a + strlen("REPLACES="), sizeof(tmp5) - 1);
			a = tmp5;
			while ((a = strchr(a, '%'))) {
				/* Yuck!  Pingtel converts the '@' to a %40, icky icky!  Convert
				   back to an '@' */
				if (strlen(a) < 3)
					break;
				*a = hex2int(a[1]) * 16 + hex2int(a[2]);
				memmove(a + 1, a+3, strlen(a + 3) + 1);
				a++;
			}
			if ((a = strchr(tmp5, '%'))) 
				*a = '\0';
			if ((a = strchr(tmp5, ';'))) 
				*a = '\0';
			/* Skip leading whitespace */
			while(tmp5[0] && (tmp5[0] < 33))
				memmove(tmp5, tmp5+1, strlen(tmp5));
				
		}
	}
	
	if ((a = strchr(c, '@')))
		*a = '\0';
	if ((a = strchr(c, ';'))) 
		*a = '\0';
	

	if ((a2 = strchr(c2, '@')))
		*a2 = '\0';

	if ((a2 = strchr(c2, ';'))) 
		*a2 = '\0';
	
	
	if (sip_debug_test_pvt(p)) {
		ast_verbose("Looking for %s in %s\n", c, p->context);
		ast_verbose("Looking for %s in %s\n", c2, p->context);
	}
	if (!ast_strlen_zero(tmp5)) {	
		/* This is a supervised transfer */
		ast_log(LOG_DEBUG,"Assigning Replace-Call-ID Info %s to REPLACE_CALL_ID\n",tmp5);
		
		strncpy(p->refer_to, "", sizeof(p->refer_to) - 1);
		strncpy(p->referred_by, "", sizeof(p->referred_by) - 1);
		strncpy(p->refer_contact, "", sizeof(p->refer_contact) - 1);
		strncpy(p->remote_party_id, "", sizeof(p->remote_party_id) - 1);
		p->refer_call = NULL;
		ast_mutex_lock(&iflock);
		/* Search interfaces and find the match */
		p2 = iflist;
		while(p2) {
			if (!strcmp(p2->callid, tmp5)) {
				/* Go ahead and lock it before returning */
				ast_mutex_lock(&p2->lock);
				p->refer_call = p2;
				break;
			}
			p2 = p2->next;
		}
		ast_mutex_unlock(&iflock);
		if (p->refer_call)
			return 0;
		else
			ast_log(LOG_NOTICE, "Supervised transfer requested, but unable to find callid '%s'\n", tmp5);
	} else if (ast_exists_extension(NULL, p->context, c, 1, NULL)) {
		/* This is an unsupervised transfer */
		ast_log(LOG_DEBUG,"Assigning Extension %s to REFER-TO\n", c);
		ast_log(LOG_DEBUG,"Assigning Extension %s to REFERRED-BY\n", c2);
		ast_log(LOG_DEBUG,"Assigning Contact Info %s to REFER_CONTACT\n", tmp3);
		ast_log(LOG_DEBUG,"Assigning Remote-Party-ID Info %s to REMOTE_PARTY_ID\n",tmp4);
		strncpy(p->refer_to, c, sizeof(p->refer_to) - 1);
		strncpy(p->referred_by, c2, sizeof(p->referred_by) - 1);
		strncpy(p->refer_contact, tmp3, sizeof(p->refer_contact) - 1);
		strncpy(p->remote_party_id, tmp4, sizeof(p->remote_party_id) - 1);
		p->refer_call = NULL;
		return 0;
	} else if (ast_canmatch_extension(NULL, p->context, c, 1, NULL)) {
		return 1;
	}

	return -1;
}

static int get_also_info(struct sip_pvt *p, struct sip_request *oreq)
{
	char tmp[256] = "", *c, *a;
	struct sip_request *req;
	
	req = oreq;
	if (!req)
		req = &p->initreq;
	strncpy(tmp, get_header(req, "Also"), sizeof(tmp) - 1);
	
	c = ditch_braces(tmp);
	
		
	if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return -1;
	}
	c += 4;
	if ((a = strchr(c, '@')))
		*a = '\0';
	if ((a = strchr(c, ';'))) 
		*a = '\0';
	
	if (sip_debug_test_pvt(p)) {
		ast_verbose("Looking for %s in %s\n", c, p->context);
	}
	if (ast_exists_extension(NULL, p->context, c, 1, NULL)) {
		/* This is an unsupervised transfer */
		ast_log(LOG_DEBUG,"Assigning Extension %s to REFER-TO\n", c);
		strncpy(p->refer_to, c, sizeof(p->refer_to) - 1);
		strncpy(p->referred_by, "", sizeof(p->referred_by) - 1);
		strncpy(p->refer_contact, "", sizeof(p->refer_contact) - 1);
		strncpy(p->remote_party_id, "", sizeof(p->remote_party_id) - 1);
		p->refer_call = NULL;
		return 0;
	} else if (ast_canmatch_extension(NULL, p->context, c, 1, NULL)) {
		return 1;
	}

	return -1;
}

static int check_via(struct sip_pvt *p, struct sip_request *req)
{
	char via[256] = "";
	char *c, *pt;
	struct hostent *hp;
	struct ast_hostent ahp;

	memset(via, 0, sizeof(via));
	strncpy(via, get_header(req, "Via"), sizeof(via) - 1);
	c = strchr(via, ';');
	if (c) 
		*c = '\0';
	c = strchr(via, ' ');
	if (c) {
		*c = '\0';
		c++;
		while(*c && (*c < 33))
			c++;
		if (strcmp(via, "SIP/2.0/UDP")) {
			ast_log(LOG_WARNING, "Don't know how to respond via '%s'\n", via);
			return -1;
		}
		pt = strchr(c, ':');
		if (pt) {
			*pt = '\0';
			pt++;
		}
		hp = ast_gethostbyname(c, &ahp);
		if (!hp) {
			ast_log(LOG_WARNING, "'%s' is not a valid host\n", c);
			return -1;
		}
		memset(&p->sa, 0, sizeof(p->sa));
		p->sa.sin_family = AF_INET;
		memcpy(&p->sa.sin_addr, hp->h_addr, sizeof(p->sa.sin_addr));
		p->sa.sin_port = htons(pt ? atoi(pt) : DEFAULT_SIP_PORT);
		if (sip_debug_test_pvt(p)) {
			if (p->nat)
				ast_verbose("Sending to %s : %d (NAT)\n", inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
			else
				ast_verbose("Sending to %s : %d (non-NAT)\n", inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
		}
	}
	return 0;
}
static char *get_calleridname(char *input,char *output)
{
	char *end = strchr(input,'<');
	char *tmp = strchr(input,'\"');
	if (!end || (end == input)) return NULL;
	/* move away from "<" */
	end--;
	/* we found "name" */
	if (tmp && tmp < end) {
		end = strchr(tmp+1,'\"');
		if (!end) return NULL;
		strncpy(output,tmp+1,(int)(end-tmp-1));
	} else {
		/* we didn't find "name" */
		/* clear the empty characters in the begining*/
		while(*input && (*input < 33))
			input++;
		/* clear the empty characters in the end */
		while(*end && (*end < 33) && end > input)
			end--;
		if (end >= input)
			strncpy(output,input,(int)(end-input)+1);
		else
			output = NULL;
	}
	return output;
}
static int check_user(struct sip_pvt *p, struct sip_request *req, char *cmd, char *uri, int reliable, struct sockaddr_in *sin, int ignore)
{
	struct sip_user *user;
	struct sip_peer *peer;
	char *of, from[256] = "", *c;
	int res = 0;
	char *t;
	char calleridname[50];
	/* Terminate URI */
	t = uri;
	while(*t && (*t > 32) && (*t != ';'))
		t++;
	*t = '\0';
	of = get_header(req, "From");
	strncpy(from, of, sizeof(from) - 1);
	memset(calleridname,0,sizeof(calleridname));
	get_calleridname(from,calleridname);
	of = ditch_braces(from);
	if (ast_strlen_zero(p->exten)) {
		t = uri;
		if (!strncmp(t, "sip:", 4))
			t+= 4;
		strncpy(p->exten, t, sizeof(p->exten) - 1);
		t = strchr(p->exten, '@');
		if (t)
			*t = '\0';
		if (ast_strlen_zero(p->our_contact))
			build_contact(p);
	}
	if (strncmp(of, "sip:", 4)) {
		ast_log(LOG_NOTICE, "From address missing 'sip:', using it anyway\n");
	} else
		of += 4;
	/* Get just the username part */
	if ((c = strchr(of, '@')))
		*c = '\0';
	if ((c = strchr(of, ':')))
		*c = '\0';
	if (*calleridname)
		sprintf(p->callerid,"\"%s\" <%s>",calleridname,of);
	else
		strncpy(p->callerid, of, sizeof(p->callerid) - 1);
	if (ast_strlen_zero(of))
			return 0;
	ast_mutex_lock(&userl.lock);
	user = find_user(of);
	if (user && ast_apply_ha(user->ha, sin)) {
		p->nat = user->nat;
		if (p->rtp) {
			ast_log(LOG_DEBUG, "Setting NAT on RTP to %d\n", p->nat);
			ast_rtp_setnat(p->rtp, p->nat);
		}
		if (p->vrtp) {
			ast_log(LOG_DEBUG, "Setting NAT on VRTP to %d\n", p->nat);
			ast_rtp_setnat(p->vrtp, p->nat);
		}
		if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), user->name, user->secret, user->md5secret, cmd, uri, reliable, ignore))) {
			sip_cancel_destroy(p);
			if (!ast_strlen_zero(user->context))
				strncpy(p->context, user->context, sizeof(p->context) - 1);
			if (!ast_strlen_zero(user->callerid) && !ast_strlen_zero(p->callerid)) 
				strncpy(p->callerid, user->callerid, sizeof(p->callerid) - 1);
			strncpy(p->username, user->name, sizeof(p->username) - 1);
			strncpy(p->peersecret, user->secret, sizeof(p->peersecret) - 1);
			strncpy(p->peermd5secret, user->md5secret, sizeof(p->peermd5secret) - 1);
			strncpy(p->accountcode, user->accountcode, sizeof(p->accountcode)  -1);
			strncpy(p->language, user->language, sizeof(p->language)  -1);
			p->canreinvite = user->canreinvite;
			p->amaflags = user->amaflags;
			p->callgroup = user->callgroup;
			p->pickupgroup = user->pickupgroup;
			p->restrictcid = user->restrictcid;
			p->capability = user->capability;
			p->jointcapability = user->capability;
			if (user->dtmfmode) {
				p->dtmfmode = user->dtmfmode;
				if (p->dtmfmode & SIP_DTMF_RFC2833)
					p->noncodeccapability |= AST_RTP_DTMF;
				else
					p->noncodeccapability &= ~AST_RTP_DTMF;
			}
		}
	}
	ast_mutex_unlock(&userl.lock);
	if (!user) {
	/* If we didn't find a user match, check for peers */
		ast_mutex_lock(&peerl.lock);
		/* which should be used? non-mysql code uses "p->recv", but
		 * mysql code used "sin"
		 */
		peer = find_peer(NULL, &p->recv);
		/* peer = find_peer(NULL, sin); */
		ast_mutex_unlock(&peerl.lock);
		if (peer) {
				/* Take the peer */
				p->nat = peer->nat;
				if (p->rtp) {
					ast_log(LOG_DEBUG, "Setting NAT on RTP to %d\n", p->nat);
					ast_rtp_setnat(p->rtp, p->nat);
				}
				if (p->vrtp) {
					ast_log(LOG_DEBUG, "Setting NAT on VRTP to %d\n", p->nat);
					ast_rtp_setnat(p->vrtp, p->nat);
				}
				p->canreinvite = peer->canreinvite;
				strncpy(p->peername, peer->name, sizeof(p->peername) - 1);
				strncpy(p->authname, peer->name, sizeof(p->authname) - 1);
				if (!ast_strlen_zero(peer->username)) {
					strncpy(p->username, peer->username, sizeof(p->username) - 1);
					strncpy(p->authname, peer->username, sizeof(p->authname) - 1);
				}
				if (!ast_strlen_zero(peer->context))
					strncpy(p->context, peer->context, sizeof(p->context) - 1);
				strncpy(p->peersecret, peer->secret, sizeof(p->peersecret) - 1);
				strncpy(p->peermd5secret, peer->md5secret, sizeof(p->peermd5secret) - 1);
				if (peer->insecure > 1) {
					/* Pretend there is no required authentication if insecure is "very" */
					strcpy(p->peersecret, "");
					strcpy(p->peermd5secret, "");
				}
				p->callgroup = peer->callgroup;
				p->pickupgroup = peer->pickupgroup;
				p->capability = peer->capability;
				p->jointcapability = peer->capability;
				if (peer->dtmfmode) {
					p->dtmfmode = peer->dtmfmode;
					if (p->dtmfmode & SIP_DTMF_RFC2833)
						p->noncodeccapability |= AST_RTP_DTMF;
					else
						p->noncodeccapability &= ~AST_RTP_DTMF;
				}
			if (peer->temponly) {
				if (peer->ha) {
					ast_free_ha(peer->ha);
				}
				free(peer);
			}
		}
	}
	return res;
}

static int get_msg_text(char *buf, int len, struct sip_request *req)
{
	int x;
	int y;
	strcpy(buf, "");
	y = len - strlen(buf) - 5;
	if (y < 0)
		y = 0;
	for (x=0;x<req->lines;x++) {
		strncat(buf, req->line[x], y);
		y -= strlen(req->line[x]) + 1;
		if (y < 0)
			y = 0;
		if (y != 0)
			strcat(buf, "\n");
	}
	return 0;
}

static void receive_message(struct sip_pvt *p, struct sip_request *req)
{
	char buf[1024];
	struct ast_frame f;
	if (get_msg_text(buf, sizeof(buf), req)) {
		ast_log(LOG_WARNING, "Unable to retrieve text from %s\n", p->callid);
		return;
	}
	if (p->owner) {
		if (sip_debug_test_pvt(p))
			ast_verbose("Message received: '%s'\n", buf);
		  memset(&f, 0, sizeof(f));
		  f.frametype = AST_FRAME_TEXT;
		  f.subclass = 0;
		  f.offset = 0;
		  f.data = buf;
		  f.datalen = strlen(buf);
		  ast_queue_frame(p->owner, &f);
	}
}

static int sip_show_inuse(int fd, int argc, char *argv[]) {
#define FORMAT  "%-15.15s %-15.15s %-15.15s %-15.15s %-15.15s\n"
#define FORMAT2 "%-15.15s %-15.15s %-15.15s %-15.15s %-15.15s\n"
	struct sip_user *user;
	char ilimits[40];
	char olimits[40];
	char iused[40];
	char oused[40];
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&userl.lock);
	user = userl.users;
	ast_cli(fd, FORMAT, "Username", "incoming", "Limit","outgoing","Limit");
	for(user=userl.users;user;user=user->next) {
		if (user->incominglimit)
			snprintf(ilimits, sizeof(ilimits), "%d", user->incominglimit);
		else
			strcpy(ilimits, "N/A");
		if (user->outgoinglimit)
			snprintf(olimits, sizeof(olimits), "%d", user->outgoinglimit);
		else
			strcpy(olimits, "N/A");
		snprintf(iused, sizeof(iused), "%d", user->inUse);
		snprintf(oused, sizeof(oused), "%d", user->outUse);
		ast_cli(fd, FORMAT2, user->name, iused, ilimits,oused,olimits);
	}
	ast_mutex_unlock(&userl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}


static int sip_show_users(int fd, int argc, char *argv[])
{
#define FORMAT  "%-15.15s  %-15.15s  %-15.15s  %-15.15s  %-5.5s\n"
	struct sip_user *user;
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&userl.lock);
	ast_cli(fd, FORMAT, "Username", "Secret", "Authen", "Def.Context", "A/C");
	for(user=userl.users;user;user=user->next) {
		ast_cli(fd, FORMAT, user->name, user->secret, user->methods, 
				user->context,user->ha ? "Yes" : "No");
	}
	ast_mutex_unlock(&userl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
}

static int sip_show_peers(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-15.15s %s  %-15.15s  %-8s %-10s\n"
#define FORMAT "%-15.15s  %-15.15s %s  %-15.15s  %-8d %-10s\n"
	struct sip_peer *peer;
	char name[256] = "";
	if (argc != 3 && argc != 5)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&peerl.lock);
	ast_cli(fd, FORMAT2, "Name/username", "Host", "   ", "Mask", "Port", "Status");
	for (peer = peerl.peers;peer;peer = peer->next) {
		char nm[20] = "";
		char status[20];
		int print_line = -1;
		char srch[2000];
		strncpy(nm, inet_ntoa(peer->mask), sizeof(nm)-1);
		if (!ast_strlen_zero(peer->username))
			snprintf(name, sizeof(name), "%s/%s", peer->name, peer->username);
		else
			strncpy(name, peer->name, sizeof(name) - 1);
		if (peer->maxms) {
			if (peer->lastms < 0)
				strcpy(status, "UNREACHABLE");
			else if (peer->lastms > peer->maxms) 
				snprintf(status, sizeof(status), "LAGGED (%d ms)", peer->lastms);
			else if (peer->lastms) 
				snprintf(status, sizeof(status), "OK (%d ms)", peer->lastms);
			else 
				strcpy(status, "UNKNOWN");
		} else 
			strcpy(status, "Unmonitored");
                sprintf(srch, FORMAT, name,
                                        peer->addr.sin_addr.s_addr ? inet_ntoa(peer->addr.sin_addr) : "(Unspecified)",
                                        peer->dynamic ? "(D)" : "   ",
                                        nm,
                                        ntohs(peer->addr.sin_port), status);

                if (argc == 5) {
                  if (!strcasecmp(argv[3],"include") && strstr(srch,argv[4])) {
                        print_line = -1;
                   } else if (!strcasecmp(argv[3],"exclude") && !strstr(srch,argv[4])) {
                        print_line = 1;
                   } else if (!strcasecmp(argv[3],"begin") && !strncasecmp(srch,argv[4],strlen(argv[4]))) {
                        print_line = -1;
                   } else {
                        print_line = 0;
                  }
                }

		if (print_line) {
		ast_cli(fd, FORMAT, name, 
					peer->addr.sin_addr.s_addr ? inet_ntoa(peer->addr.sin_addr) : "(Unspecified)",
					peer->dynamic ? "(D)" : "   ",
					nm,
					ntohs(peer->addr.sin_port), status);
		}
	}
	ast_mutex_unlock(&peerl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char *regstate2str(int regstate)
{
	switch(regstate) {
	case REG_STATE_UNREGISTERED:
		return "Unregistered";
	case REG_STATE_REGSENT:
		return "Request Sent";
	case REG_STATE_AUTHSENT:
		return "Auth. Sent";
	case REG_STATE_REGISTERED:
		return "Registered";
	case REG_STATE_REJECTED:
		return "Rejected";
	case REG_STATE_TIMEOUT:
		return "Timeout";
	case REG_STATE_NOAUTH:
		return "No Authentication";
	default:
		return "Unknown";
	}
}

static int sip_show_registry(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-20.20s  %-10.10s  %8.8s %-20.20s\n"
#define FORMAT "%-20.20s  %-10.10s  %8d %-20.20s\n"
	struct sip_registry *reg;
	char host[80];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&regl.lock);
	ast_cli(fd, FORMAT2, "Host", "Username", "Refresh", "State");
	for (reg = regl.registrations;reg;reg = reg->next) {
		snprintf(host, sizeof(host), "%s:%d", inet_ntoa(reg->addr.sin_addr), ntohs(reg->addr.sin_port));
		ast_cli(fd, FORMAT, host,
					reg->username, reg->refresh, regstate2str(reg->regstate));
	}
	ast_mutex_unlock(&regl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int sip_show_channels(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-10.10s  %-11.11s  %-11.11s  %-7.7s  %-6.6s  %s\n"
#define FORMAT  "%-15.15s  %-10.10s  %-11.11s  %5.5d/%5.5d  %-5.5dms  %-4.4dms  %-6.6s%s\n"
	struct sip_pvt *cur;
	int numchans = 0;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&iflock);
	cur = iflist;
	ast_cli(fd, FORMAT2, "Peer", "User/ANR", "Call ID", "Seq (Tx/Rx)", "Lag", "Jitter", "Format");
	while (cur) {
		if (!cur->subscribed) {
			ast_cli(fd, FORMAT, inet_ntoa(cur->sa.sin_addr), 
						strlen(cur->username) ? cur->username : ( strlen(cur->callerid) ? cur->callerid: "(None)" ), 
						cur->callid, 
						cur->ocseq, cur->icseq, 
						0,
						0,
						ast_getformatname(cur->owner ? cur->owner->nativeformats : 0), cur->needdestroy ? "(d)" : "" );
		numchans++;
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&iflock);
	ast_cli(fd, "%d active SIP channel(s)\n", numchans);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char *complete_sipch(char *line, char *word, int pos, int state)
{
	int which=0;
	struct sip_pvt *cur;
	char *c = NULL;
	ast_mutex_lock(&iflock);
	cur = iflist;
	while(cur) {
		if (!strncasecmp(word, cur->callid, strlen(word))) {
			if (++which > state) {
				c = strdup(cur->callid);
				break;
			}
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&iflock);
	return c;
}

static int sip_show_channel(int fd, int argc, char *argv[])
{
	struct sip_pvt *cur;
	char tmp[256];
	size_t len;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	len = strlen(argv[3]);
	ast_mutex_lock(&iflock);
	cur = iflist;
	while(cur) {
		if (!strncasecmp(cur->callid, argv[3],len)) {
			ast_cli(fd, "Call-ID: %s\n", cur->callid);
			ast_cli(fd, "Our Codec Capability: %d\n", cur->capability);
			ast_cli(fd, "Non-Codec Capability: %d\n", cur->noncodeccapability);
			ast_cli(fd, "Joint Codec Capability: %d\n", cur->jointcapability);
			ast_cli(fd, "Theoretical Address: %s:%d\n", inet_ntoa(cur->sa.sin_addr), ntohs(cur->sa.sin_port));
			ast_cli(fd, "Received Address:    %s:%d\n", inet_ntoa(cur->recv.sin_addr), ntohs(cur->recv.sin_port));
			ast_cli(fd, "NAT Support:         %s\n", cur->nat ? "Yes" : "No");
			ast_cli(fd, "Our Tag:             %08d\n", cur->tag);
			ast_cli(fd, "Their Tag:           %s\n", cur->theirtag);
			ast_cli(fd, "Need Destroy:        %d\n", cur->needdestroy);
			ast_cli(fd, "Last Message:        %s\n", cur->lastmsg);
			ast_cli(fd, "Route:               %s\n", cur->route ? cur->route->hop : "N/A");
			strcpy(tmp, "");
			if (cur->dtmfmode & SIP_DTMF_RFC2833)
				strcat(tmp, "rfc2833 ");
			if (cur->dtmfmode & SIP_DTMF_INFO)
				strcat(tmp, "info ");
			if (cur->dtmfmode & SIP_DTMF_INBAND)
				strcat(tmp, "inband ");
			ast_cli(fd, "DTMF Mode: %s\n\n", tmp);
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&iflock);
	if (!cur) 
		ast_cli(fd, "No such SIP Call ID starting with '%s'\n", argv[3]);
	return RESULT_SUCCESS;
}

static void receive_info(struct sip_pvt *p, struct sip_request *req)
{
	char buf[1024] = "";
	unsigned int event;
	char resp = 0;
	struct ast_frame f;
	char *c;
	/* Try getting the "signal=" part */
	if (!ast_strlen_zero(c = get_sdp(req, "Signal")) || !ast_strlen_zero(c = get_sdp(req, "d"))) {
		strncpy(buf, c, sizeof(buf) - 1);
	} else if (get_msg_text(buf, sizeof(buf), req)) {
		/* Normal INFO method */
		ast_log(LOG_WARNING, "Unable to retrieve text from %s\n", p->callid);
		return;
	}
	
	if (p->owner) {
		if (!ast_strlen_zero(buf)) {
			if (sip_debug_test_pvt(p))
				ast_verbose("DTMF received: '%c'\n", buf[0]);
			if (buf[0] == '*')
				event = 10;
			else if (buf[0] == '#')
				event = 11;
			else
				event = atoi(buf);
                        if (event < 10) {
                                resp = '0' + event;
                        } else if (event < 11) {
                                resp = '*';
                        } else if (event < 12) {
                                resp = '#';
                        } else if (event < 16) {
                                resp = 'A' + (event - 12);
                        }
                        memset(&f, 0, sizeof(f));
                        f.frametype = AST_FRAME_DTMF;
                        f.subclass = resp;
                        f.offset = 0;
                        f.data = NULL;
                        f.datalen = 0;
                        ast_queue_frame(p->owner, &f);
		}
	}
}

static int sip_do_debug_ip(int fd, int argc, char *argv[])
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
	if (hp == NULL)  {
		return RESULT_SHOWUSAGE;
	}
	debugaddr.sin_family = AF_INET;
	memcpy(&debugaddr.sin_addr, hp->h_addr, sizeof(debugaddr.sin_addr));
	debugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(fd, "SIP Debugging Enabled for IP: %s\n", inet_ntoa(debugaddr.sin_addr));
	else
		ast_cli(fd, "SIP Debugging Enabled for IP: %s:%d\n", inet_ntoa(debugaddr.sin_addr), port);
	sipdebug = 1;
	return RESULT_SUCCESS;
}

static int sip_do_debug_peer(int fd, int argc, char *argv[])
{
	struct sip_peer *peer;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&peerl.lock);
	for (peer = peerl.peers;peer;peer = peer->next)
		if (!strcmp(peer->name, argv[3])) 
			break;
	ast_mutex_unlock(&peerl.lock);
#ifdef MYSQL_FRIENDS
	if (!peer)
		peer = mysql_peer(argv[3], NULL);
#endif		
	if (peer) {
		if (peer->addr.sin_addr.s_addr) {
			debugaddr.sin_family = AF_INET;
			memcpy(&debugaddr.sin_addr, &peer->addr.sin_addr, sizeof(debugaddr.sin_addr));
			debugaddr.sin_port = peer->addr.sin_port;
			ast_cli(fd, "SIP Debugging Enabled for IP: %s:%d\n", inet_ntoa(debugaddr.sin_addr), ntohs(debugaddr.sin_port));
			sipdebug = 1;
		} else
			ast_cli(fd, "Unable to get IP address of peer '%s'\n", argv[3]);
		if (peer->temponly)
			free(peer);
		peer = NULL;
	} else
		ast_cli(fd, "No such peer '%s'\n", argv[3]);
	return RESULT_SUCCESS;
}

static int sip_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2) {
		if (argc != 4) 
			return RESULT_SHOWUSAGE;
		else if (strncmp(argv[2], "ip\0", 3) == 0)
			return sip_do_debug_ip(fd, argc, argv);
		else if (strncmp(argv[2], "peer\0", 5) == 0)
			return sip_do_debug_peer(fd, argc, argv);
		else return RESULT_SHOWUSAGE;
	}
	sipdebug = 1;
	memset(&debugaddr, 0, sizeof(debugaddr));
	ast_cli(fd, "SIP Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int sip_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	sipdebug = 0;
	ast_cli(fd, "SIP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int reply_digest(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader, char *digest, int digest_len);

static int do_register_auth(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader) {
	char digest[1024];
	p->authtries++;
	memset(digest,0,sizeof(digest));
	if (reply_digest(p,req, header, "REGISTER", digest, sizeof(digest))) {
		/* There's nothing to use for authentication */
		return -1;
	}
	return transmit_register(p->registry,"REGISTER",digest, respheader); 
}

static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader, char *msg, int init) {
	char digest[1024];
	p->authtries++;
	memset(digest,0,sizeof(digest));
	if (reply_digest(p,req, header, msg, digest, sizeof(digest) )) {
		/* No way to authenticate */
		return -1;
	}
	return transmit_invite(p,msg,!strcasecmp(msg, "INVITE"),digest, respheader, NULL,NULL, init); 
}

static int reply_digest(struct sip_pvt *p, struct sip_request *req, char *header, char *orig_header, char *digest, int digest_len) {

	char tmp[512] = "";
	char *realm = "";
	char *nonce = "";
	char *domain = "";
	char *opaque = "";
	char *qop = "";
	char *c;


	strncpy(tmp, get_header(req, header),sizeof(tmp) - 1);
	if (ast_strlen_zero(tmp)) 
		return -1;
	c = tmp;
	c+=strlen("Digest ");
	while (c) {
		while (*c && (*c < 33)) c++;
		if (!*c)
			break;
		if (!strncasecmp(c,"realm=", strlen("realm="))) {
			c+=strlen("realm=");
			if ((*c == '\"')) {
				realm=++c;
				if ((c = strchr(c,'\"')))
					*c = '\0';
			} else {
				realm = c;
				if ((c = strchr(c,',')))
					*c = '\0';
			}
		} else if (!strncasecmp(c, "nonce=", strlen("nonce="))) {
			c+=strlen("nonce=");
			if ((*c == '\"')) {
				nonce=++c;
				if ((c = strchr(c,'\"')))
					*c = '\0';
			} else {
				nonce = c;
				if ((c = strchr(c,',')))
					*c = '\0';
			}
		} else if (!strncasecmp(c, "opaque=", strlen("opaque="))) {
			c+=strlen("opaque=");
			if ((*c == '\"')) {
				opaque=++c;
				if ((c = strchr(c,'\"')))
					*c = '\0';
			} else {
				opaque = c;
				if ((c = strchr(c,',')))
					*c = '\0';
			}
		} else if (!strncasecmp(c, "qop=", strlen("qop="))) {
			c+=strlen("qop=");
			if ((*c == '\"')) {
				qop=++c;
				if ((c = strchr(c,'\"')))
					*c = '\0';
			} else {
				qop = c;
				if ((c = strchr(c,',')))
					*c = '\0';
			}
		} else if (!strncasecmp(c, "domain=", strlen("domain="))) {
			c+=strlen("domain=");
			if ((*c == '\"')) {
				domain=++c;
				if ((c = strchr(c,'\"')))
					*c = '\0';
			} else {
				domain = c;
				if ((c = strchr(c,',')))
					*c = '\0';
			}
		} else
			c = strchr(c,',');
		if (c)
			c++;
	}
	if (strlen(tmp) >= sizeof(tmp))
		ast_log(LOG_WARNING, "Buffer overflow detected!  Please file a bug.\n");

	/* copy realm and nonce for later authorization of CANCELs and BYEs */
	strncpy(p->realm, realm, sizeof(p->realm)-1);
	strncpy(p->nonce, nonce, sizeof(p->nonce)-1);
	strncpy(p->domain, domain, sizeof(p->domain)-1);
	strncpy(p->opaque, opaque, sizeof(p->opaque)-1);
	strncpy(p->qop, qop, sizeof(p->qop)-1);
	build_reply_digest(p, orig_header, digest, digest_len); 
	return 0;
}

static int build_reply_digest(struct sip_pvt *p, char* orig_header, char* digest, int digest_len)
{
        char a1[256];
	char a2[256];
	char a1_hash[256];
	char a2_hash[256];
	char resp[256];
	char resp_hash[256];
	char uri[256] = "";
	char cnonce[80];

	if (!ast_strlen_zero(p->domain))
		strncpy(uri, p->domain, sizeof(uri) - 1);
	else if (!ast_strlen_zero(p->uri))
		strncpy(uri, p->uri, sizeof(uri) - 1);
	else
		snprintf(uri, sizeof(uri), "sip:%s@%s",p->username, inet_ntoa(p->sa.sin_addr));

	snprintf(cnonce, sizeof(cnonce), "%08x", rand());

	snprintf(a1,sizeof(a1),"%s:%s:%s",p->authname,p->realm,p->peersecret);
	snprintf(a2,sizeof(a2),"%s:%s",orig_header,uri);
	if (!ast_strlen_zero(p->peermd5secret))
	        strncpy(a1_hash, p->peermd5secret, sizeof(a1_hash) - 1);
	else
	        md5_hash(a1_hash,a1);
	md5_hash(a2_hash,a2);
	/* XXX We hard code the nonce-number to 1... What are the odds? Are we seriously going to keep
	       track of every nonce we've seen? Also we hard code to "auth"...  XXX */
	if (!ast_strlen_zero(p->qop))
		snprintf(resp,sizeof(resp),"%s:%s:%s:%s:%s:%s",a1_hash,p->nonce, "00000001", cnonce, "auth", a2_hash);
	else
		snprintf(resp,sizeof(resp),"%s:%s:%s",a1_hash,p->nonce,a2_hash);
	md5_hash(resp_hash,resp);
	/* XXX We hard code our qop to "auth" for now.  XXX */
	if (!ast_strlen_zero(p->qop))
		snprintf(digest,digest_len,"Digest username=\"%s\", realm=\"%s\", algorithm=\"MD5\", uri=\"%s\", nonce=\"%s\", response=\"%s\", opaque=\"%s\", qop=\"%s\", cnonce=\"%s\", nc=%s",p->authname,p->realm,uri,p->nonce,resp_hash, p->opaque, "auth", cnonce, "00000001");
	else
		snprintf(digest,digest_len,"Digest username=\"%s\", realm=\"%s\", algorithm=\"MD5\", uri=\"%s\", nonce=\"%s\", response=\"%s\", opaque=\"%s\"",p->authname,p->realm,uri,p->nonce,resp_hash, p->opaque);

	return 0;
}
	



static char show_users_usage[] = 
"Usage: sip show users\n"
"       Lists all users known to the SIP (Session Initiation Protocol) subsystem.\n";

static char show_inuse_usage[] = 
"Usage: sip show inuse\n"
"       List all users known to the SIP (Session Initiation Protocol) subsystem usage counters and limits.\n";

static char show_channels_usage[] = 
"Usage: sip show channels\n"
"       Lists all currently active SIP channels.\n";

static char show_channel_usage[] = 
"Usage: sip show channel <channel>\n"
"       Provides detailed status on a given SIP channel.\n";

static char show_peers_usage[] = 
"Usage: sip show peers\n"
"       Lists all known SIP peers.\n";

static char show_reg_usage[] =
"Usage: sip show registry\n"
"       Lists all registration requests and status.\n";

static char debug_usage[] = 
"Usage: sip debug\n"
"       Enables dumping of SIP packets for debugging purposes\n\n"
"       sip debug ip <host[:PORT]>\n"
"       Enables dumping of SIP packets to and from host.\n\n"
"       sip debug peer <peername>\n"
"       Enables dumping of SIP packets to and from host.\n"
"       Require peer to be registered.\n";

static char no_debug_usage[] = 
"Usage: sip no debug\n"
"       Disables dumping of SIP packets for debugging purposes\n";

static char sip_reload_usage[] =
"Usage: sip reload\n"
"       Reloads SIP configuration from sip.conf\n";

static struct ast_cli_entry  cli_show_users = 
	{ { "sip", "show", "users", NULL }, sip_show_users, "Show defined SIP users", show_users_usage };
static struct ast_cli_entry  cli_show_channels =
	{ { "sip", "show", "channels", NULL }, sip_show_channels, "Show active SIP channels", show_channels_usage};
static struct ast_cli_entry  cli_show_channel =
	{ { "sip", "show", "channel", NULL }, sip_show_channel, "Show detailed SIP channel info", show_channel_usage, complete_sipch  };
static struct ast_cli_entry  cli_debug_ip =
	{ { "sip", "debug", "ip", NULL }, sip_do_debug, "Enable SIP debugging on IP", debug_usage };
static struct ast_cli_entry  cli_debug_peer =
	{ { "sip", "debug", "peer", NULL }, sip_do_debug, "Enable SIP debugging on Peername", debug_usage };
static struct ast_cli_entry  cli_show_peers =
	{ { "sip", "show", "peers", NULL }, sip_show_peers, "Show defined SIP peers", show_peers_usage };
static struct ast_cli_entry  cli_show_peers_include =
        { { "sip", "show", "peers", "include", NULL }, sip_show_peers, "Show defined SIP peers", show_peers_usage };
static struct ast_cli_entry  cli_show_peers_exclude =
        { { "sip", "show", "peers", "exclude", NULL }, sip_show_peers, "Show defined SIP peers", show_peers_usage };
static struct ast_cli_entry  cli_show_peers_begin =
        { { "sip", "show", "peers", "begin", NULL }, sip_show_peers, "Show defined SIP peers", show_peers_usage };
static struct ast_cli_entry  cli_inuse_show =
	{ { "sip", "show", "inuse", NULL }, sip_show_inuse, "List all inuse/limit", show_inuse_usage };
static struct ast_cli_entry  cli_show_registry =
	{ { "sip", "show", "registry", NULL }, sip_show_registry, "Show SIP registration status", show_reg_usage };
static struct ast_cli_entry  cli_debug =
	{ { "sip", "debug", NULL }, sip_do_debug, "Enable SIP debugging", debug_usage };
static struct ast_cli_entry  cli_no_debug =
	{ { "sip", "no", "debug", NULL }, sip_no_debug, "Disable SIP debugging", no_debug_usage };

static int sip_poke_peer_s(void *data)
{
	struct sip_peer *peer = data;
	peer->pokeexpire = -1;
	sip_poke_peer(peer);
	return 0;
}

static void parse_moved_contact(struct sip_pvt *p, struct sip_request *req)
{
	char tmp[256] = "";
	char *s, *e;
	strncpy(tmp, get_header(req, "Contact"), sizeof(tmp) - 1);
	s = ditch_braces(tmp);
	e = strchr(tmp, '@');
	if (e)
		*e = '\0';
	if (!strncasecmp(s, "sip:", 4))
		s += 4;
	ast_log(LOG_DEBUG, "Found 302 Redirect to extension '%s'\n", s);
	if (p->owner)
		strncpy(p->owner->call_forward, s, sizeof(p->owner->call_forward) - 1);
}

static void handle_response(struct sip_pvt *p, int resp, char *rest, struct sip_request *req, int ignore)
{
	char *to;
	char *msg, *c;
	struct ast_channel *owner;
	struct sip_peer *peer;
	int pingtime;
	struct timeval tv;
	int seqno=0;
	c = get_header(req, "Cseq");
	if (sscanf(c, "%d ", &seqno) != 1) {
		ast_log(LOG_WARNING, "Unable to determine sequence number\n");
	}
	msg = strchr(c, ' ');
	if (!msg) msg = ""; else msg++;
	owner = p->owner;
	if (owner) 
		owner->hangupcause = hangup_sip2cause(resp);
	/* Acknowledge whatever it is destined for */
	if ((resp >= 100) && (resp <= 199))
		__sip_semi_ack(p, seqno, 0);
	else
		__sip_ack(p, seqno, 0);
	/* Get their tag if we haven't already */
	to = get_header(req, "To");
	to = strstr(to, "tag=");
	if (to) {
		to += 4;
		strncpy(p->theirtag, to, sizeof(p->theirtag) - 1);
		to = strchr(p->theirtag, ';');
		if (to)
			*to = '\0';
	}
	if (p->peerpoke) {
		/* We don't really care what the response is, just that it replied back. 
		   Well, as long as it's not a 100 response...  since we might
		   need to hang around for something more "difinitive" */
		if (resp != 100) {
			int statechanged = 0;
			peer = p->peerpoke;
			gettimeofday(&tv, NULL);
			pingtime = (tv.tv_sec - peer->ps.tv_sec) * 1000 +
						(tv.tv_usec - peer->ps.tv_usec) / 1000;
			if (pingtime < 1)
				pingtime = 1;
			if ((peer->lastms < 0)  || (peer->lastms > peer->maxms)) {
				if (pingtime <= peer->maxms) {
					ast_log(LOG_NOTICE, "Peer '%s' is now REACHABLE!\n", peer->name);
					statechanged = 1;
				}
			} else if ((peer->lastms > 0) && (peer->lastms <= peer->maxms)) {
				if (pingtime > peer->maxms) {
					ast_log(LOG_NOTICE, "Peer '%s' is now TOO LAGGED!\n", peer->name);
					statechanged = 1;
				}
			}
			if (!peer->lastms)
			    statechanged = 1;
			peer->lastms = pingtime;
			peer->call = NULL;
			if (statechanged)
			    ast_device_state_changed("SIP/%s", peer->name);

			if (peer->pokeexpire > -1)
				ast_sched_del(sched, peer->pokeexpire);
			if (!strcasecmp(msg, "INVITE"))
				transmit_request(p, "ACK", seqno, 0, 0);
			p->needdestroy = 1;
			/* Try again eventually */
			if ((peer->lastms < 0)  || (peer->lastms > peer->maxms))
    				peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_NOTOK, sip_poke_peer_s, peer);
			else
				peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_OK, sip_poke_peer_s, peer);
		}
	} else if (p->outgoing) {
		/* Acknowledge sequence number */
		if (p->initid > -1) {
			/* Don't auto congest anymore since we've gotten something useful back */
			ast_sched_del(sched, p->initid);
			p->initid = -1;
		}
		switch(resp) {
		case 100:
			break;
		case 183:	
			if (!strcasecmp(msg, "INVITE")) {
				if (!ast_strlen_zero(get_header(req, "Content-Type")))
					process_sdp(p, req);
				if (p->owner) {
					/* Queue a progress frame */
					ast_queue_control(p->owner, AST_CONTROL_PROGRESS);
				}
			}
			break;
		case 180:
			if (p->owner) {
				ast_queue_control(p->owner, AST_CONTROL_RINGING);
				if (p->owner->_state != AST_STATE_UP)
					ast_setstate(p->owner, AST_STATE_RINGING);
			}
			break;
		case 200:
			if (!strcasecmp(msg, "NOTIFY")) {
				/* They got the notify, this is the end */
				if (p->owner) {
					ast_log(LOG_WARNING, "Notify answer on an owned channel?\n");
					ast_queue_hangup(p->owner);
				} else {
					if (!p->subscribed) {
					    p->needdestroy = 1;
					}
				}
			} else if (!strcasecmp(msg, "INVITE")) {
				if (!ast_strlen_zero(get_header(req, "Content-Type")))
					process_sdp(p, req);
				/* Save Record-Route for any later requests we make on this dialogue */
				build_route(p, req, 1);
				if (p->owner) {
					if (p->owner->_state != AST_STATE_UP) {
						ast_setstate(p->owner, AST_STATE_UP);
						ast_queue_control(p->owner, AST_CONTROL_ANSWER);
					} else {
						struct ast_frame af = { AST_FRAME_NULL, };
						ast_queue_frame(p->owner, &af);
					}
				}
				p->authtries = 0;
				/* If I understand this right, the branch is different for a non-200 ACK only */
				transmit_request(p, "ACK", seqno, 0, 1);
				/* Go ahead and send bye at this point */
				if (p->pendingbye) {
					transmit_request_with_auth(p, "BYE", 0, 1, 1);
					p->needdestroy = 1;
				}
			} else if (!strcasecmp(msg, "REGISTER")) {
				/* char *exp; */
				int expires, expires_ms;
				struct sip_registry *r;
				r=p->registry;
				if (r) {
					r->regstate=REG_STATE_REGISTERED;
					ast_log(LOG_DEBUG, "Registration successful\n");
					if (r->timeout > -1) {
						ast_log(LOG_DEBUG, "Cancelling timeout %d\n", r->timeout);
						ast_sched_del(sched, r->timeout);
					}
					r->timeout=-1;
					r->call = NULL;
					p->registry = NULL;
					p->needdestroy = 1;
					/* set us up for re-registering */
					/* figure out how long we got registered for */
					if (r->expire > -1)
						ast_sched_del(sched, r->expire);
					/* according to section 6.13 of RFC, contact headers override
					   expires headers, so check those first */
					expires = 0;
					if (!ast_strlen_zero(get_header(req, "Contact"))) {
						char *contact = NULL;
						char *tmptmp = NULL;
						int start = 0;
						for(;;) {
							contact = __get_header(req, "Contact", &start);
							/* this loop ensures we get a contact header about our register request */
							if(!ast_strlen_zero(contact)) {
								if(strstr(contact, p->our_contact))
									break;
							} else
								break;
						}
						tmptmp = strstr(contact, "expires=");
						if (tmptmp) {
							if (sscanf(tmptmp + 8, "%d;", &expires) != 1)
								expires = 0;
						}
					}
					if (!expires) expires=atoi(get_header(req, "expires"));
					if (!expires) expires=default_expiry;

					expires_ms = expires * 1000;
					if (expires <= EXPIRY_GUARD_LIMIT)
						expires_ms -= MAX((expires_ms * EXPIRY_GUARD_PCT),EXPIRY_GUARD_MIN);
					else
						expires_ms -= EXPIRY_GUARD_SECS * 1000;

					r->expire=ast_sched_add(sched, expires_ms, sip_reregister, r); 
				} else
					ast_log(LOG_WARNING, "Got 200 OK on REGISTER that isn't a register\n");

			}
			break;
		case 401: /* Not authorized on REGISTER */
			if (!strcasecmp(msg, "INVITE")) {
				/* First we ACK */
				transmit_request(p, "ACK", seqno, 0, 0);
				/* Then we AUTH */
				if ((p->authtries > 1) || do_proxy_auth(p, req, "WWW-Authenticate", "Authorization", "INVITE", 1)) {
					ast_log(LOG_NOTICE, "Failed to authenticate on INVITE to '%s'\n", get_header(&p->initreq, "From"));
					p->needdestroy = 1;
				}
			} else if (p->registry && !strcasecmp(msg, "REGISTER")) {
				if ((p->authtries > 1) || do_register_auth(p, req, "WWW-Authenticate", "Authorization")) {
					ast_log(LOG_NOTICE, "Failed to authenticate on REGISTER to '%s'\n", get_header(&p->initreq, "From"));
					p->needdestroy = 1;
				}
			} else
				p->needdestroy = 1;
			break;
		case 407:
			if (!strcasecmp(msg, "INVITE")) {
				/* First we ACK */
				transmit_request(p, "ACK", seqno, 0, 0);
				/* Then we AUTH */
				/* But only if the packet wasn't marked as ignore in handle_request */
				if(!ignore){
					if ((p->authtries > 1) || do_proxy_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization", "INVITE", 1)) {
						ast_log(LOG_NOTICE, "Failed to authenticate on INVITE to '%s'\n", get_header(&p->initreq, "From"));
						p->needdestroy = 1;
					}
				}
			} else if (!strcasecmp(msg, "BYE") || !strcasecmp(msg, "REFER")) {
				if (ast_strlen_zero(p->authname))
					ast_log(LOG_WARNING, "Asked to authenticate %s, to %s:%d but we have no matching peer!\n",
							msg, inet_ntoa(p->recv.sin_addr), ntohs(p->recv.sin_port));
				if ((p->authtries > 1) || do_proxy_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization", msg, 0)) {
					ast_log(LOG_NOTICE, "Failed to authenticate on %s to '%s'\n", msg, get_header(&p->initreq, "From"));
					p->needdestroy = 1;
				}
			} else if (p->registry && !strcasecmp(msg, "REGISTER")) {
				if ((p->authtries > 1) || do_register_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization")) {
					ast_log(LOG_NOTICE, "Failed to authenticate on REGISTER to '%s' (tries '%d')\n", get_header(&p->initreq, "From"), p->authtries);
					p->needdestroy = 1;
				}
			} else
				p->needdestroy = 1;

			break;
		case 501: /* Not Implemented */
			if (!strcasecmp(msg, "INVITE"))
				ast_queue_control(p->owner, AST_CONTROL_CONGESTION);
			else
				ast_log(LOG_WARNING, "Host '%s' does not implement '%s'\n", inet_ntoa(p->sa.sin_addr), msg);
			break;
		default:
			if ((resp >= 300) && (resp < 700)) {
				if ((option_verbose > 2) && (resp != 487))
					ast_verbose(VERBOSE_PREFIX_3 "Got SIP response %d \"%s\" back from %s\n", resp, rest, inet_ntoa(p->sa.sin_addr));
				p->alreadygone = 1;
				if (p->rtp) {
					/* Immediately stop RTP */
					ast_rtp_stop(p->rtp);
				}
				if (p->vrtp) {
					/* Immediately stop VRTP */
					ast_rtp_stop(p->vrtp);
				}
				/* XXX Locking issues?? XXX */
				switch(resp) {
				case 300: /* Multiple Choices */
				case 301: /* Moved permenantly */
				case 302: /* Moved temporarily */
				case 305: /* Use Proxy */
					parse_moved_contact(p, req);
					if (p->owner)
						ast_queue_control(p->owner, AST_CONTROL_BUSY);
					break;
				case 487:
					/* channel now destroyed - dec the inUse counter */
					if ( p->outgoing ) {
						update_user_counter(p, DEC_OUT_USE);
					}
					else {
						update_user_counter(p, DEC_IN_USE);
					}
					break;
				case 486: /* Busy here */
				case 600: /* Busy everywhere */
				case 603: /* Decline */
					if (p->owner)
						ast_queue_control(p->owner, AST_CONTROL_BUSY);
					break;
				case 480: /* Temporarily Unavailable */
				case 404: /* Not Found */
				case 410: /* Gone */
				case 500: /* Server error */
					if (owner)
						ast_queue_control(p->owner, AST_CONTROL_CONGESTION);
					break;
				default:
					/* Send hangup */	
					if (owner)
						ast_queue_hangup(p->owner);
					break;
				}
				/* ACK on invite */
				if (!strcasecmp(msg, "INVITE"))
					transmit_request(p, "ACK", seqno, 0, 0);
				p->alreadygone = 1;
				if (!p->owner)
					p->needdestroy = 1;
			} else
				ast_log(LOG_NOTICE, "Dunno anything about a %d %s response from %s\n", resp, rest, p->owner ? p->owner->name : inet_ntoa(p->sa.sin_addr));
		}
	} else {
		if (sip_debug_test_pvt(p))
			ast_verbose("Message is %s\n", msg);
		switch(resp) {
		case 200:
			/* Change branch since this is a 200 response */
			if (!strcasecmp(msg, "INVITE") || !strcasecmp(msg, "REGISTER") )
				transmit_request(p, "ACK", seqno, 0, 1);
			break;
		case 407:
			if (!strcasecmp(msg, "BYE") || !strcasecmp(msg, "REFER")) {
				if (ast_strlen_zero(p->authname))
					ast_log(LOG_WARNING, "Asked to authenticate %s, to %s:%d but we have no matching peer!\n",
							msg, inet_ntoa(p->recv.sin_addr), ntohs(p->recv.sin_port));
				if ((p->authtries > 1) || do_proxy_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization", msg, 0)) {
					ast_log(LOG_NOTICE, "Failed to authenticate on %s to '%s'\n", msg, get_header(&p->initreq, "From"));
					p->needdestroy = 1;
				}
			}
			break;
		}
	}
}

static int attempt_transfer(struct sip_pvt *p1, struct sip_pvt *p2)
{
	if (!p1->owner || !p2->owner) {
		ast_log(LOG_WARNING, "Transfer attempted without dual ownership?\n");
		return -1;
	}
	if (p1->owner->bridge) {
		if (p2->owner->bridge)
			ast_moh_stop(p2->owner->bridge);
		ast_moh_stop(p1->owner->bridge);
		ast_moh_stop(p1->owner);
		ast_moh_stop(p2->owner);
		if (ast_channel_masquerade(p2->owner, p1->owner->bridge)) {
			ast_log(LOG_WARNING, "Failed to masquerade %s into %s\n", p2->owner->name, p1->owner->bridge->name);
			return -1;
		}
	} else if (p2->owner->bridge) {
		ast_moh_stop(p2->owner->bridge);
		ast_moh_stop(p2->owner);
		ast_moh_stop(p1->owner);
		if (ast_channel_masquerade(p1->owner, p2->owner->bridge)) {
			ast_log(LOG_WARNING, "Failed to masquerade %s into %s\n", p1->owner->name, p2->owner->bridge->name);
			return -1;
		}
	} else {
		ast_log(LOG_NOTICE, "Transfer attempted with no bridged calls to transfer\n");
		if (p1->owner)
			ast_softhangup_nolock(p1->owner, AST_SOFTHANGUP_DEV);
		if (p2->owner)
			ast_softhangup_nolock(p2->owner, AST_SOFTHANGUP_DEV);
		return -1;
	}
	return 0;
}

static int handle_request(struct sip_pvt *p, struct sip_request *req, struct sockaddr_in *sin, int *recount)
{
	/* Called with p->lock held, as well as p->owner->lock if appropriate, keeping things
	   relatively static */
	struct sip_request resp;
	char *cmd;
	char *cseq;
	char *from;
	char *e;
	struct ast_channel *c=NULL;
	struct ast_channel *transfer_to;
	int seqno;
	int len;
	int ignore=0;
	int respid;
	int res;
	int gotdest;
	struct ast_frame af = { AST_FRAME_NULL, };
	/* Clear out potential response */
	memset(&resp, 0, sizeof(resp));
	/* Get Method and Cseq */
	cseq = get_header(req, "Cseq");
	cmd = req->header[0];
	/* Must have Cseq */
	if (ast_strlen_zero(cmd) || ast_strlen_zero(cseq))
			return -1;
	if (sscanf(cseq, "%i%n", &seqno, &len) != 1) {
		ast_log(LOG_DEBUG, "No seqno in '%s'\n", cmd);
		return -1;
	}
	/* Get the command */
	cseq += len;

  /* Determine the request URI for sip, sips or tel URIs */
	if( determine_firstline_parts( req ) < 0 ) {
	  return -1; 
	}
	cmd= req->rlPart1;
	e= req->rlPart2;
	
	if (strcasecmp(cmd, "SIP/2.0")) {
		/* Request coming in */			
		if (p->icseq && (p->icseq < seqno)) {
			ast_log(LOG_DEBUG, "Ignoring out of order packet %d (expecting %d)\n", seqno, p->icseq);
			return -1;
		} else if (p->icseq && (p->icseq != seqno)) {
			/* ignore means "don't do anything with it" but still have to 
			   respond appropriately  */
			ignore=1;
		}
		if (ast_strlen_zero(p->theirtag)) {
			from = get_header(req, "From");
			from = strstr(from, "tag=");
			if (from) {
				from += 4;
				strncpy(p->theirtag, from, sizeof(p->theirtag) - 1);
				from = strchr(p->theirtag, ';');
				if (from)
					*from = '\0';
			}
		}
		snprintf(p->lastmsg, sizeof(p->lastmsg), "Rx: %s", cmd);
	} else {
		/* Response to our request -- Do some sanity checks */	
		if (!p->initreq.headers) {
			ast_log(LOG_DEBUG, "That's odd...  Got a response on a call we dont know about.\n");
			p->needdestroy = 1;
			return 0;
		} else if (p->ocseq && (p->ocseq < seqno)) {
			ast_log(LOG_DEBUG, "Ignoring out of order response %d (expecting %d)\n", seqno, p->ocseq);
			return -1;
		} else if (p->ocseq && (p->ocseq != seqno)) {
			/* ignore means "don't do anything with it" but still have to 
			   respond appropriately  */
			ignore=1;
		}
	}
	
	if (strcmp(cmd, "SIP/2.0") && (seqno >= p->icseq))
		/* Next should follow monotonically increasing */
		p->icseq = seqno + 1;

	/* Initialize the context if it hasn't been already */
	if (!strcasecmp(cmd, "OPTIONS")) {
		res = get_destination(p, req);
		build_contact(p);
		/* XXX Should we authenticate OPTIONS? XXX */
		if (ast_strlen_zero(p->context))
			strncpy(p->context, context, sizeof(p->context) - 1);
		if (res < 0)
			transmit_response_with_allow(p, "404 Not Found", req);
		else if (res > 0)
			transmit_response_with_allow(p, "484 Address Incomplete", req);
		else 
			transmit_response_with_allow(p, "200 OK", req);
		/* Destroy if this OPTIONS was the opening request, but not if
		   it's in the middle of a normal call flow. */
		if (!p->lastinvite)
			p->needdestroy = 1;
	} else if (!strcasecmp(cmd, "INVITE")) {
		if (p->outgoing && p->owner && (p->owner->_state != AST_STATE_UP)) {
			/* This is a call to ourself.  Send ourselves an error code and stop
			   processing immediately, as SIP really has no good mechanism for
			   being able to call yourself */
			transmit_response(p, "482 Loop Detected", req);
			/* We do NOT destroy p here, so that our response will be accepted */
			return 0;
		}
		/* Process the SDP portion */
		if (!ignore) {
			/* Use this as the basis */
			if (sip_debug_test_pvt(p))
				ast_verbose("Using latest request as basis request\n");
			/* This call is no longer outgoing if it ever was */
			p->outgoing = 0;
			copy_request(&p->initreq, req);
			check_via(p, req);
			if (!ast_strlen_zero(get_header(req, "Content-Type"))) {
				if (process_sdp(p, req))
					return -1;
			} else {
				p->jointcapability = p->capability;
				ast_log(LOG_DEBUG, "Hm....  No sdp for the moment\n");
			}
			/* Queue NULL frame to prod ast_rtp_bridge if appropriate */
			if (p->owner)
				ast_queue_frame(p->owner, &af);
		} else if (sip_debug_test_pvt(p))
			ast_verbose("Ignoring this request\n");
		if (!p->lastinvite) {
			/* Handle authentication if this is our first invite */
			res = check_user(p, req, cmd, e, 1, sin, ignore);
			if (res) {
				if (res < 0) {
					ast_log(LOG_NOTICE, "Failed to authenticate user %s\n", get_header(req, "From"));
					p->needdestroy = 1;
				}
				return 0;
			}
			/* Initialize the context if it hasn't been already */
			if (ast_strlen_zero(p->context))
				strncpy(p->context, context, sizeof(p->context) - 1);
			/* Check number of concurrent calls -vs- incoming limit HERE */
			ast_log(LOG_DEBUG, "Check for res for %s\n", p->username);
			res = update_user_counter(p,INC_IN_USE);
			if (res) {
				if (res < 0) {
					ast_log(LOG_DEBUG, "Failed to place call for user %s, too many calls\n", p->username);
					p->needdestroy = 1;
				}
				return 0;
			}
			/* Get destination right away */
			gotdest = get_destination(p, NULL);
			get_rdnis(p, NULL);
			extract_uri(p, req);
			build_contact(p);

			if (gotdest) {
				if (gotdest < 0) {
					transmit_response(p, "404 Not Found", req);
					update_user_counter(p,DEC_IN_USE);
				} else {
					transmit_response(p, "484 Address Incomplete", req);
					update_user_counter(p,DEC_IN_USE);
				}
				p->needdestroy = 1;
			} else {
				/* If no extension was specified, use the s one */
				if (ast_strlen_zero(p->exten))
					strncpy(p->exten, "s", sizeof(p->exten) - 1);
				/* Initialize tag */	
				p->tag = rand();
				/* First invitation */
				c = sip_new(p, AST_STATE_DOWN, ast_strlen_zero(p->username) ? NULL : p->username );
				*recount = 1;
				/* Save Record-Route for any later requests we make on this dialogue */
				build_route(p, req, 0);
				if (c) {
					/* Pre-lock the call */
					ast_mutex_lock(&c->lock);
				}
			}
			
		} else 
			c = p->owner;
		if (!ignore && p)
			p->lastinvite = seqno;
		if (c) {
			switch(c->_state) {
			case AST_STATE_DOWN:
				transmit_response(p, "100 Trying", req);
				ast_setstate(c, AST_STATE_RING);
				if (strcmp(p->exten, ast_pickup_ext())) {
					if (ast_pbx_start(c)) {
						ast_log(LOG_WARNING, "Failed to start PBX :(\n");
						/* Unlock locks so ast_hangup can do its magic */
						ast_mutex_unlock(&c->lock);
						ast_mutex_unlock(&p->lock);
						ast_hangup(c);
						ast_mutex_lock(&p->lock);
						transmit_response_reliable(p, "503 Unavailable", req, 1);
						c = NULL;
					}
				} else {
					ast_mutex_unlock(&c->lock);
					if (ast_pickup_call(c)) {
						ast_log(LOG_NOTICE, "Nothing to pick up\n");
						transmit_response_reliable(p, "503 Unavailable", req, 1);
						p->alreadygone = 1;
						/* Unlock locks so ast_hangup can do its magic */
						ast_mutex_unlock(&p->lock);
						ast_hangup(c);
						ast_mutex_lock(&p->lock);
						c = NULL;
					} else {
						ast_mutex_unlock(&p->lock);
						ast_setstate(c, AST_STATE_DOWN);
						ast_hangup(c);
						ast_mutex_lock(&p->lock);
						c = NULL;
					}
				}
				break;
			case AST_STATE_RING:
				transmit_response(p, "100 Trying", req);
				break;
			case AST_STATE_RINGING:
				transmit_response(p, "180 Ringing", req);
				break;
			case AST_STATE_UP:
				transmit_response_with_sdp(p, "200 OK", req, 1);
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to handle INVITE in state %d\n", c->_state);
				transmit_response(p, "100 Trying", req);
			}
		} else {
			if (p && !p->needdestroy) {
				ast_log(LOG_NOTICE, "Unable to create/find channel\n");
				transmit_response_reliable(p, "503 Unavailable", req, 1);
				p->needdestroy = 1;
			}
		}
	} else if (!strcasecmp(cmd, "REFER")) {
		ast_log(LOG_DEBUG, "We found a REFER!\n");
		if (ast_strlen_zero(p->context))
			strncpy(p->context, context, sizeof(p->context) - 1);
		res = get_refer_info(p, req);
		if (res < 0)
			transmit_response_with_allow(p, "404 Not Found", req);
		else if (res > 0)
			transmit_response_with_allow(p, "484 Address Incomplete", req);
		else {
			transmit_response(p, "202 Accepted", req);
			if (!ignore) {
				if (p->refer_call) {
					ast_log(LOG_DEBUG,"202 Accepted (supervised)\n");
					attempt_transfer(p, p->refer_call);
					ast_mutex_unlock(&p->refer_call->lock);
					p->refer_call = NULL;
					p->gotrefer = 1;
				} else {
					ast_log(LOG_DEBUG,"202 Accepted (blind)\n");
					c = p->owner;
					if (c) {
						transfer_to = c->bridge;
						if (transfer_to) {
							ast_moh_stop(transfer_to);
							ast_async_goto(transfer_to,p->context, p->refer_to,1);
						} else {
							ast_queue_hangup(p->owner);
						}
					}
					p->gotrefer = 1;
				}
				/* Always increment on a BYE */
				transmit_request_with_auth(p, "BYE", 0, 1, 1);
				p->alreadygone = 1;
			}
		}
	} else if (!strcasecmp(cmd, "CANCEL")) {
		check_via(p, req);
		p->alreadygone = 1;
		if (p->rtp) {
			/* Immediately stop RTP */
			ast_rtp_stop(p->rtp);
		}
		if (p->vrtp) {
			/* Immediately stop VRTP */
			ast_rtp_stop(p->vrtp);
		}
		if (p->owner)
			ast_queue_hangup(p->owner);
		else
			p->needdestroy = 1;
		if (p->initreq.len > 0) {
			transmit_response_reliable(p, "487 Request Terminated", &p->initreq, 1);
			transmit_response(p, "200 OK", req);
		} else {
			transmit_response_reliable(p, "481 Call Leg Does Not Exist", req, 1);
		}
	} else if (!strcasecmp(cmd, "BYE")) {
		copy_request(&p->initreq, req);
		check_via(p, req);
		p->alreadygone = 1;
		if (p->rtp) {
			/* Immediately stop RTP */
			ast_rtp_stop(p->rtp);
		}
		if (p->vrtp) {
			/* Immediately stop VRTP */
			ast_rtp_stop(p->vrtp);
		}
		if (!ast_strlen_zero(get_header(req, "Also"))) {
			ast_log(LOG_NOTICE, "Client '%s' using deprecated BYE/Also transfer method.  Ask vendor to support REFER instead\n",
				inet_ntoa(p->recv.sin_addr));
			if (ast_strlen_zero(p->context))
				strncpy(p->context, context, sizeof(p->context) - 1);
			res = get_also_info(p, req);
			if (!res) {
				c = p->owner;
				if (c) {
					transfer_to = c->bridge;
					if (transfer_to) {
						/* Don't actually hangup here... */
						ast_moh_stop(transfer_to);
						ast_async_goto(transfer_to,p->context, p->refer_to,1);
					} else
						ast_queue_hangup(p->owner);
				}
			} else {
				ast_log(LOG_WARNING, "Invalid transfer information from '%s'\n", inet_ntoa(p->recv.sin_addr));
				ast_queue_hangup(p->owner);
			}
		} else if (p->owner)
			ast_queue_hangup(p->owner);
		else
			p->needdestroy = 1;
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "MESSAGE")) {
		if (!ignore) {
			if (sip_debug_test_pvt(p))
				ast_verbose("Receiving message!\n");
			receive_message(p, req);
		}
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "SUBSCRIBE")) {
		if (!ignore) {
			/* Use this as the basis */
			if (sip_debug_test_pvt(p))
				ast_verbose("Using latest SUBSCRIBE request as basis request\n");
			/* This call is no longer outgoing if it ever was */
			p->outgoing = 0;
			copy_request(&p->initreq, req);
			check_via(p, req);
		} else if (sip_debug_test_pvt(p))
			ast_verbose("Ignoring this request\n");

		if (!p->lastinvite) {
			/* Handle authentication if this is our first subscribe */
			res = check_user(p, req, cmd, e, 0, sin, ignore);
			if (res) {
				if (res < 0) {
					ast_log(LOG_NOTICE, "Failed to authenticate user %s for SUBSCRIBE\n", get_header(req, "From"));
					p->needdestroy = 1;
				}
				return 0;
			}
			/* Initialize the context if it hasn't been already */
			if (ast_strlen_zero(p->context))
				strncpy(p->context, context, sizeof(p->context) - 1);
			/* Get destination right away */
			gotdest = get_destination(p, NULL);
			build_contact(p);
			if (gotdest) {
				if (gotdest < 0)
					transmit_response(p, "404 Not Found", req);
				else
					transmit_response(p, "484 Address Incomplete", req);
				p->needdestroy = 1;
			} else {
				/* Initialize tag */	
				p->tag = rand();
				if (!strcmp(get_header(req, "Accept"), "application/dialog-info+xml"))
				    p->subscribed = 2;
				else
				    p->subscribed = 1;
				
				p->stateid = ast_extension_state_add(p->context, p->exten, cb_extensionstate, p);
			}
			
		} else 
			c = p->owner;

		if (!ignore && p)
			p->lastinvite = seqno;
		if (p) {
		    if (!(p->expiry = atoi(get_header(req, "Expires")))) {
			transmit_response(p, "200 OK", req);
			p->needdestroy = 1;
			return 0;
		    }
		    /* The next line can be removed if the SNOM200 Expires bug is fixed */
		    if (p->subscribed == 1) {  
			if (p->expiry>max_expiry)
			    p->expiry = max_expiry;
		    }
		    transmit_response(p, "200 OK", req);
		    sip_scheddestroy(p, (p->expiry+10)*1000);
		    transmit_state_notify(p, ast_extension_state(NULL, p->context, p->exten),1);
		}
	} else if (!strcasecmp(cmd, "INFO")) {
		if (!ignore) {
			if (sip_debug_test_pvt(p))
				ast_verbose("Receiving DTMF!\n");
			receive_info(p, req);
		}
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "REGISTER")) {
		/* Use this as the basis */
		if (sip_debug_test_pvt(p))
			ast_verbose("Using latest request as basis request\n");
		copy_request(&p->initreq, req);
		check_via(p, req);
		if ((res = register_verify(p, sin, req, e, ignore)) < 0) 
			ast_log(LOG_NOTICE, "Registration from '%s' failed for '%s'\n", get_header(req, "To"), inet_ntoa(sin->sin_addr));
		if (res < 1) {
			/* Destroy the session, but keep us around for just a bit in case they don't
			   get our 200 OK */
		    sip_scheddestroy(p, 15*1000);
		}
	} else if (!strcasecmp(cmd, "ACK")) {
		/* Uhm, I haven't figured out the point of the ACK yet.  Are we
		   supposed to retransmit responses until we get an ack? 
		   Make sure this is on a valid call */
		__sip_ack(p, seqno, FLAG_RESPONSE);
		if (!ast_strlen_zero(get_header(req, "Content-Type"))) {
			if (process_sdp(p, req))
				return -1;
		} 
		if (!p->lastinvite && ast_strlen_zero(p->randdata))
			p->needdestroy = 1;
	} else if (!strcasecmp(cmd, "SIP/2.0")) {
		extract_uri(p, req);
		while(*e && (*e < 33)) e++;
		if (sscanf(e, "%i %n", &respid, &len) != 1) {
			ast_log(LOG_WARNING, "Invalid response: '%s'\n", e);
		} else {
			handle_response(p, respid, e + len, req,ignore);
		}
	} else {
		transmit_response_with_allow(p, "405 Method Not Allowed", req);
		ast_log(LOG_NOTICE, "Unknown SIP command '%s' from '%s'\n", 
			cmd, inet_ntoa(p->sa.sin_addr));
		/* If this is some new method, and we don't have a call, destroy it now */
		if (!p->initreq.headers)
			p->needdestroy = 1;
	}
	return 0;
}

static int sipsock_read(int *id, int fd, short events, void *ignore)
{
	struct sip_request req;
	struct sockaddr_in sin = { 0, };
	struct sip_pvt *p;
	int res;
	int len;
	int recount = 0;
	len = sizeof(sin);
	memset(&req, 0, sizeof(req));
	res = recvfrom(sipsock, req.data, sizeof(req.data) - 1, 0, (struct sockaddr *)&sin, &len);
	if (res < 0) {
		if (errno != ECONNREFUSED)
			ast_log(LOG_WARNING, "Recv error: %s\n", strerror(errno));
		return 1;
	}
	req.data[res] = '\0';
	req.len = res;
	if (sip_debug_test_addr(&sin))
		ast_verbose("\n\nSip read: \n%s\n", req.data);
	if (pedanticsipchecking)
		req.len = lws2sws(req.data, req.len);
	parse(&req);
	if (sip_debug_test_addr(&sin))
		ast_verbose("%d headers, %d lines\n", req.headers, req.lines);
	if (req.headers < 2) {
		/* Must have at least two headers */
		return 1;
	}
	/* Process request, with netlock held */
retrylock:
	ast_mutex_lock(&netlock);
	p = find_call(&req, &sin);
	if (p) {
		/* Go ahead and lock the owner if it has one -- we may need it */
		if (p->owner && ast_mutex_trylock(&p->owner->lock)) {
			ast_log(LOG_DEBUG, "Failed to grab lock, trying again...\n");
			ast_mutex_unlock(&p->lock);
			ast_mutex_unlock(&netlock);
			/* Sleep infintismly short amount of time */
			usleep(1);
			goto retrylock;
		}
		memcpy(&p->recv, &sin, sizeof(p->recv));
		handle_request(p, &req, &sin, &recount);
		if (p->owner)
			ast_mutex_unlock(&p->owner->lock);
		ast_mutex_unlock(&p->lock);
	}
	ast_mutex_unlock(&netlock);
	if (recount)
		ast_update_use_count();

	return 1;
}

static int sip_send_mwi_to_peer(struct sip_peer *peer)
{
	/* Called with peerl lock, but releases it */
	struct sip_pvt *p;
	char name[256] = "";
	int newmsgs, oldmsgs;
	/* Check for messages */
	ast_app_messagecount(peer->mailbox, &newmsgs, &oldmsgs);
	
	time(&peer->lastmsgcheck);
	
	/* Return now if it's the same thing we told them last time */
	if (((newmsgs << 8) | (oldmsgs)) == peer->lastmsgssent) {
		ast_mutex_unlock(&peerl.lock);
		return 0;
	}
	
	p = sip_alloc(NULL, NULL, 0);
	if (!p) {
		ast_log(LOG_WARNING, "Unable to build sip pvt data for MWI\n");
		ast_mutex_unlock(&peerl.lock);
		return -1;
	}
	strncpy(name, peer->name, sizeof(name) - 1);
	peer->lastmsgssent = ((newmsgs << 8) | (oldmsgs));
	ast_mutex_unlock(&peerl.lock);
	if (create_addr(p, name)) {
		/* Maybe they're not registered, etc. */
		sip_destroy(p);
		return 0;
	}
	/* Recalculate our side, and recalculate Call ID */
	if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	/* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", inet_ntoa(p->ourip), ourport, p->branch);
	build_callid(p->callid, sizeof(p->callid), p->ourip);
	/* Send MWI */
	p->outgoing = 1;
	transmit_notify(p, newmsgs, oldmsgs);
	sip_scheddestroy(p, 15000);
	return 0;
}

static void *do_monitor(void *data)
{
	int res;
	struct sip_pvt *sip;
	struct sip_peer *peer;
	time_t t;
	int fastrestart =0;
	int lastpeernum = -1;
	int curpeernum;
	int reloading;
	/* Add an I/O event to our UDP socket */
	if (sipsock > -1) 
		ast_io_add(io, sipsock, sipsock_read, AST_IO_IN, NULL);
	
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
	for(;;) {
		/* Check for a reload request */
		ast_mutex_lock(&sip_reload_lock);
		reloading = sip_reloading;
		sip_reloading = 0;
		ast_mutex_unlock(&sip_reload_lock);
		if (reloading) {
			if (option_verbose > 0)
				ast_verbose(VERBOSE_PREFIX_1 "Reloading SIP\n");
			sip_do_reload();
		}
		/* Check for interfaces needing to be killed */
		ast_mutex_lock(&iflock);
restartsearch:		
		sip = iflist;
		while(sip) {
			ast_mutex_lock(&sip->lock);
			if (sip->needdestroy && !sip->packets) {
				ast_mutex_unlock(&sip->lock);
				__sip_destroy(sip, 1);
				goto restartsearch;
			}
			ast_mutex_unlock(&sip->lock);
			sip = sip->next;
		}
		ast_mutex_unlock(&iflock);
		/* Don't let anybody kill us right away.  Nobody should lock the interface list
		   and wait for the monitor list, but the other way around is okay. */
		ast_mutex_lock(&monlock);
		/* Lock the network interface */
		ast_mutex_lock(&netlock);
		/* Okay, now that we know what to do, release the network lock */
		ast_mutex_unlock(&netlock);
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		ast_mutex_unlock(&monlock);
		pthread_testcancel();
		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		if ((res < 0) || (res > 1000))
			res = 1000;
		/* If we might need to send more mailboxes, don't wait long at all.*/
		if (fastrestart)
			res = 1;
		res = ast_io_wait(io, res);
		ast_mutex_lock(&monlock);
		if (res >= 0) 
			ast_sched_runq(sched);
		/* needs work to send mwi to mysql peers */
		ast_mutex_lock(&peerl.lock);
		peer = peerl.peers;
		time(&t);
		fastrestart = 0;
		curpeernum = 0;
		while(peer) {
			if ((curpeernum > lastpeernum) && !ast_strlen_zero(peer->mailbox) && ((t - peer->lastmsgcheck) > 10)) {
				sip_send_mwi_to_peer(peer);
				fastrestart = 1;
				lastpeernum = curpeernum;
				break;
			}
			curpeernum++;
			peer = peer->next;
		}
		/* Remember, sip_send_mwi_to_peer releases the lock if we've called it */
		if (!peer) {
			/* Reset where we come from */
			lastpeernum = -1;
			ast_mutex_unlock(&peerl.lock);
		}
		ast_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor(void)
{
	pthread_attr_t attr;
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == AST_PTHREADT_STOP)
		return 0;
	if (ast_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread != AST_PTHREADT_NULL) {
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else {
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		/* Start a new monitor */
		if (pthread_create(&monitor_thread, &attr, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static int sip_poke_noanswer(void *data)
{
	struct sip_peer *peer = data;
	
	peer->pokeexpire = -1;
	if (peer->lastms > -1)
		ast_log(LOG_NOTICE, "Peer '%s' is now UNREACHABLE!\n", peer->name);
	if (peer->call)
		sip_destroy(peer->call);
	peer->call = NULL;
	peer->lastms = -1;
	ast_device_state_changed("SIP/%s", peer->name);
	/* Try again quickly */
	peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_NOTOK, sip_poke_peer_s, peer);
	return 0;
}

static int sip_poke_peer(struct sip_peer *peer)
{
	struct sip_pvt *p;
	if (!peer->maxms || !peer->addr.sin_addr.s_addr) {
		/* IF we have no IP, or this isn't to be monitored, return
		  imeediately after clearing things out */
		peer->lastms = 0;
		peer->pokeexpire = -1;
		peer->call = NULL;
		return 0;
	}
	if (peer->call > 0) {
		ast_log(LOG_NOTICE, "Still have a call...\n");
		sip_destroy(peer->call);
	}
	p = peer->call = sip_alloc(NULL, NULL, 0);
	if (!peer->call) {
		ast_log(LOG_WARNING, "Unable to allocate call for poking peer '%s'\n", peer->name);
		return -1;
	}
	memcpy(&p->sa, &peer->addr, sizeof(p->sa));
	memcpy(&p->recv, &peer->addr, sizeof(p->sa));
	if (!ast_strlen_zero(p->tohost))
		strncpy(p->tohost, peer->tohost, sizeof(p->tohost) - 1);
	else
		snprintf(p->tohost, sizeof(p->tohost), "%s", inet_ntoa(peer->addr.sin_addr));

	/* Recalculate our side, and recalculate Call ID */
	if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	/* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", inet_ntoa(p->ourip), ourport, p->branch);
	build_callid(p->callid, sizeof(p->callid), p->ourip);

	if (peer->pokeexpire > -1)
		ast_sched_del(sched, peer->pokeexpire);
	p->peerpoke = peer;
	p->outgoing = 1;
#ifdef VOCAL_DATA_HACK
	strncpy(p->username, "__VOCAL_DATA_SHOULD_READ_THE_SIP_SPEC__", sizeof(p->username));
	transmit_invite(p, "INVITE", 0, NULL, NULL, NULL,NULL, 1);
#else
	transmit_invite(p, "OPTIONS", 0, NULL, NULL, NULL,NULL, 1);
#endif
	gettimeofday(&peer->ps, NULL);
	peer->pokeexpire = ast_sched_add(sched, DEFAULT_MAXMS * 2, sip_poke_noanswer, peer);

	return 0;
}

static int sip_devicestate(void *data)
{
	char *ext, *host;
	char tmp[256] = "";
	char *dest = data;

	struct hostent *hp;
	struct ast_hostent ahp;
	struct sip_peer *p;
	int found = 0;

	int res = AST_DEVICE_INVALID;

	strncpy(tmp, dest, sizeof(tmp) - 1);
	host = strchr(tmp, '@');
	if (host) {
		*host = '\0';
		host++;
		ext = tmp;
	} else {
		host = tmp;
		ext = NULL;
	}

	ast_mutex_lock(&peerl.lock);
	p = find_peer(host, NULL);
	if (p) {
		found++;
		res = AST_DEVICE_UNAVAILABLE;
		if ((p->addr.sin_addr.s_addr || p->defaddr.sin_addr.s_addr) &&
			(!p->maxms || ((p->lastms > -1)  && (p->lastms <= p->maxms)))) {
			/* peer found and valid */
			res = AST_DEVICE_UNKNOWN;
		}
	}
	ast_mutex_unlock(&peerl.lock);
	if (!p && !found) {
		hp = ast_gethostbyname(host, &ahp);
		if (hp)
			res = AST_DEVICE_UNKNOWN;
	}
	return res;
}

static struct ast_channel *sip_request(char *type, int format, void *data)
{
	int oldformat;
	struct sip_pvt *p;
	struct ast_channel *tmpc = NULL;
	char *ext, *host;
	char tmp[256] = "";
	char *dest = data;

	oldformat = format;
	format &= ((AST_FORMAT_MAX_AUDIO << 1) - 1);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format %s while capability is %s\n", ast_getformatname(oldformat), ast_getformatname(capability));
		return NULL;
	}
	p = sip_alloc(NULL, NULL, 0);
	if (!p) {
		ast_log(LOG_WARNING, "Unable to build sip pvt data for '%s'\n", (char *)data);
		return NULL;
	}

	strncpy(tmp, dest, sizeof(tmp) - 1);
	host = strchr(tmp, '@');
	if (host) {
		*host = '\0';
		host++;
		ext = tmp;
	} else {
		ext = strchr(tmp, '/');
		if (ext) {
			*ext++ = '\0';
			host = tmp;
		}
		else {
			host = tmp;
			ext = NULL;
		}
	}

	/* Assign a default capability */
	p->capability = capability;

	if (create_addr(p, host)) {
		sip_destroy(p);
		return NULL;
	}
	if (ast_strlen_zero(p->peername) && ext)
		strncpy(p->peername, ext, sizeof(p->peername) - 1);
	/* Recalculate our side, and recalculate Call ID */
	if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	/* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", inet_ntoa(p->ourip), ourport, p->branch);
	build_callid(p->callid, sizeof(p->callid), p->ourip);
	if (ext)
		strncpy(p->username, ext, sizeof(p->username) - 1);
#if 0
	printf("Setting up to call extension '%s' at '%s'\n", ext ? ext : "<none>", host);
#endif
	p->prefcodec = format;
	tmpc = sip_new(p, AST_STATE_DOWN, host);
	if (!tmpc)
		sip_destroy(p);
	ast_update_use_count();
	restart_monitor();
	return tmpc;
}

static struct sip_user *build_user(char *name, struct ast_variable *v)
{
	struct sip_user *user;
	int format;
	struct ast_ha *oldha = NULL;
	user = (struct sip_user *)malloc(sizeof(struct sip_user));
	if (user) {
		memset(user, 0, sizeof(struct sip_user));
		strncpy(user->name, name, sizeof(user->name)-1);
		oldha = user->ha;
		user->ha = NULL;
		/* set the usage flag to a sane staring value*/
		user->inUse = 0;
		user->outUse = 0;
		user->capability = capability;

		user->canreinvite = REINVITE_INVITE;
		/* JK02: set default context */
		strcpy(user->context, context);
		strcpy(user->language, language);
		while(v) {
			if (!strcasecmp(v->name, "context")) {
				strncpy(user->context, v->value, sizeof(user->context));
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny")) {
				user->ha = ast_append_ha(v->name, v->value, user->ha);
			} else if (!strcasecmp(v->name, "auth")) {
				strncpy(user->methods, v->value, sizeof(user->methods)-1);
			} else if (!strcasecmp(v->name, "secret")) {
				strncpy(user->secret, v->value, sizeof(user->secret)-1); 
			} else if (!strcasecmp(v->name, "md5secret")) {
				strncpy(user->md5secret, v->value, sizeof(user->secret)-1); 
			} else if (!strcasecmp(v->name, "dtmfmode")) {
				if (!strcasecmp(v->value, "inband"))
					user->dtmfmode=SIP_DTMF_INBAND;
				else if (!strcasecmp(v->value, "rfc2833"))
					user->dtmfmode = SIP_DTMF_RFC2833;
				else if (!strcasecmp(v->value, "info"))
					user->dtmfmode = SIP_DTMF_INFO;
				else {
					ast_log(LOG_WARNING, "Unknown dtmf mode '%s', using rfc2833\n", v->value);
					user->dtmfmode = SIP_DTMF_RFC2833;
				}
			} else if (!strcasecmp(v->name, "canreinvite")) {
				if (!strcasecmp(v->value, "update"))
					user->canreinvite = REINVITE_UPDATE;
				else
					user->canreinvite = ast_true(v->value);
			} else if (!strcasecmp(v->name, "nat")) {
				user->nat = ast_true(v->value);
			} else if (!strcasecmp(v->name, "callerid")) {
				strncpy(user->callerid, v->value, sizeof(user->callerid)-1);
				user->hascallerid=1;
			} else if (!strcasecmp(v->name, "callgroup")) {
				user->callgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "pickupgroup")) {
				user->pickupgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "language")) {
				strncpy(user->language, v->value, sizeof(user->language)-1);
			} else if (!strcasecmp(v->name, "accountcode")) {
				strncpy(user->accountcode, v->value, sizeof(user->accountcode)-1);
			} else if (!strcasecmp(v->name, "incominglimit")) {
				user->incominglimit = atoi(v->value);
				if (user->incominglimit < 0)
				   user->incominglimit = 0;
			} else if (!strcasecmp(v->name, "outgoinglimit")) {
				user->outgoinglimit = atoi(v->value);
				if (user->outgoinglimit < 0)
				   user->outgoinglimit = 0;
			} else if (!strcasecmp(v->name, "amaflags")) {
				format = ast_cdr_amaflags2int(v->value);
				if (format < 0) {
					ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
				} else {
					user->amaflags = format;
				}
			} else if (!strcasecmp(v->name, "allow")) {
				format = ast_getformatbyname(v->value);
				if (format < 1) 
					ast_log(LOG_WARNING, "Cannot allow unknown format '%s'\n", v->value);
				else
					user->capability |= format;
			} else if (!strcasecmp(v->name, "disallow")) {
				format = ast_getformatbyname(v->value);
				if (format < 1) 
					ast_log(LOG_WARNING, "Cannot disallow unknown format '%s'\n", v->value);
				else
					user->capability &= ~format;
			} else if (!strcasecmp(v->name, "insecure")) {
				user->insecure = ast_true(v->value);
			} else if (!strcasecmp(v->name, "restrictcid")) {
				user->restrictcid = ast_true(v->value);
			}
			/*else if (strcasecmp(v->name,"type"))
			 *	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
			 */
			v = v->next;
		}
	}
	if (ast_strlen_zero(user->methods)) {
		if (!ast_strlen_zero(user->secret)) 
			strncpy(user->methods, "md5,plaintext", sizeof(user->methods) - 1);
		else if (!ast_strlen_zero(user->md5secret))
		        strncpy(user->methods, "md5", sizeof(user->methods) - 1);
	}
	if (oldha)
		ast_free_ha(oldha);
	return user;
}

static struct sip_peer *temp_peer(char *name)
{
	struct sip_peer *peer;
	peer = malloc(sizeof(struct sip_peer));
	memset(peer, 0, sizeof(struct sip_peer));
	peer->expire = -1;
	peer->pokeexpire = -1;
	strncpy(peer->name, name, sizeof(peer->name)-1);
	strncpy(peer->context, context, sizeof(peer->context)-1);
	peer->addr.sin_port = htons(DEFAULT_SIP_PORT);
	peer->expiry = expiry;
	peer->capability = capability;
	/* Assume can reinvite */
	peer->canreinvite = globalcanreinvite;
	peer->dtmfmode = globaldtmfmode;
	peer->nat = globalnat;
	peer->selfdestruct = 1;
	peer->dynamic = 1;
	strcpy(peer->methods, "md5,plaintext");
	reg_source_db(peer);
	return peer;
}

static struct sip_peer *build_peer(char *name, struct ast_variable *v)
{
	struct sip_peer *peer;
	struct sip_peer *prev;
	struct ast_ha *oldha = NULL;
	int maskfound=0;
	int format;
	int found=0;
	prev = NULL;
	ast_mutex_lock(&peerl.lock);
	peer = peerl.peers;
	while(peer) {
		if (!strcasecmp(peer->name, name)) {	
			break;
		}
		prev = peer;
		peer = peer->next;
	}
	if (peer) {
		found++;
		/* Already in the list, remove it and it will be added back (or FREE'd) */
		if (prev) {
			prev->next = peer->next;
		} else {
			peerl.peers = peer->next;
		}
		ast_mutex_unlock(&peerl.lock);
 	} else {
		ast_mutex_unlock(&peerl.lock);
		peer = malloc(sizeof(struct sip_peer));
		memset(peer, 0, sizeof(struct sip_peer));
		peer->expire = -1;
		peer->pokeexpire = -1;
	}
	peer->lastmsgssent = -1;
	if (peer) {
		if (!found) {
			strncpy(peer->name, name, sizeof(peer->name)-1);
			strncpy(peer->context, context, sizeof(peer->context)-1);
			peer->addr.sin_port = htons(DEFAULT_SIP_PORT);
			peer->expiry = expiry;
		}
		oldha = peer->ha;
		peer->ha = NULL;
		peer->capability = capability;
		/* Assume can reinvite */
		peer->canreinvite = REINVITE_INVITE;
		peer->dtmfmode = 0;
		while(v) {
			if (!strcasecmp(v->name, "secret")) 
				strncpy(peer->secret, v->value, sizeof(peer->secret)-1);
			else if (!strcasecmp(v->name, "md5secret")) 
				strncpy(peer->md5secret, v->value, sizeof(peer->md5secret)-1);
			else if (!strcasecmp(v->name, "auth")) 
				strncpy(peer->methods, v->value, sizeof(peer->methods)-1);
			else if (!strcasecmp(v->name, "canreinvite")) {
				if (!strcasecmp(v->value, "update"))
					peer->canreinvite = REINVITE_UPDATE;
				else
					peer->canreinvite = ast_true(v->value);
			} else if (!strcasecmp(v->name, "nat")) 
				peer->nat = ast_true(v->value);
			else if (!strcasecmp(v->name, "context"))
				strncpy(peer->context, v->value, sizeof(peer->context)-1);
			else if (!strcasecmp(v->name, "fromdomain"))
				strncpy(peer->fromdomain, v->value, sizeof(peer->fromdomain)-1);
			else if (!strcasecmp(v->name, "fromuser"))
				strncpy(peer->fromuser, v->value, sizeof(peer->fromuser)-1);
            else if (!strcasecmp(v->name, "dtmfmode")) {
				if (!strcasecmp(v->value, "inband"))
					peer->dtmfmode=SIP_DTMF_INBAND;
				else if (!strcasecmp(v->value, "rfc2833"))
					peer->dtmfmode = SIP_DTMF_RFC2833;
				else if (!strcasecmp(v->value, "info"))
					peer->dtmfmode = SIP_DTMF_INFO;
				else {
					ast_log(LOG_WARNING, "Unknown dtmf mode '%s', using rfc2833\n", v->value);
					peer->dtmfmode = SIP_DTMF_RFC2833;
				}
			} else if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					/* They'll register with us */
					peer->dynamic = 1;
					if (!found) {
						/* Initialize stuff iff we're not found, otherwise
						   we keep going with what we had */
						memset(&peer->addr.sin_addr, 0, 4);
						if (peer->addr.sin_port) {
							/* If we've already got a port, make it the default rather than absolute */
							peer->defaddr.sin_port = peer->addr.sin_port;
							peer->addr.sin_port = 0;
						}
					}
				} else {
					/* Non-dynamic.  Make sure we become that way if we're not */
					if (peer->expire > -1)
						ast_sched_del(sched, peer->expire);
					peer->expire = -1;
					peer->dynamic = 0;
					if (ast_get_ip(&peer->addr, v->value)) {
						free(peer);
						return NULL;
					}
					strncpy(peer->tohost, v->value, sizeof(peer->tohost) - 1);
				}
				if (!maskfound)
					inet_aton("255.255.255.255", &peer->mask);
			} else if (!strcasecmp(v->name, "defaultip")) {
				if (ast_get_ip(&peer->defaddr, v->value)) {
					free(peer);
					return NULL;
				}
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny")) {
				peer->ha = ast_append_ha(v->name, v->value, peer->ha);
			} else if (!strcasecmp(v->name, "mask")) {
				maskfound++;
				inet_aton(v->value, &peer->mask);
			} else if (!strcasecmp(v->name, "port")) {
				if (peer->dynamic)
					peer->defaddr.sin_port = htons(atoi(v->value));
				else
					peer->addr.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "username")) {
				strncpy(peer->username, v->value, sizeof(peer->username)-1);
			} else if (!strcasecmp(v->name, "mailbox")) {
				strncpy(peer->mailbox, v->value, sizeof(peer->mailbox)-1);
			} else if (!strcasecmp(v->name, "callgroup")) {
				peer->callgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "pickupgroup")) {
				peer->pickupgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "allow")) {
				format = ast_getformatbyname(v->value);
				if (format < 1) 
					ast_log(LOG_WARNING, "Cannot allow unknown format '%s'\n", v->value);
				else
					peer->capability |= format;
			} else if (!strcasecmp(v->name, "disallow")) {
				format = ast_getformatbyname(v->value);
				if (format < 1) 
					ast_log(LOG_WARNING, "Cannot disallow unknown format '%s'\n", v->value);
				else
					peer->capability &= ~format;
			} else if (!strcasecmp(v->name, "insecure")) {
				if (!strcasecmp(v->value, "very")) {
					peer->insecure = 2;
				} else if (ast_true(v->value))
					peer->insecure = 1;
				else
					peer->insecure = 0;
			} else if (!strcasecmp(v->name, "qualify")) {
				if (!strcasecmp(v->value, "no")) {
					peer->maxms = 0;
				} else if (!strcasecmp(v->value, "yes")) {
					peer->maxms = DEFAULT_MAXMS;
				} else if (sscanf(v->value, "%d", &peer->maxms) != 1) {
					ast_log(LOG_WARNING, "Qualification of peer '%s' should be 'yes', 'no', or a number of milliseconds at line %d of sip.conf\n", peer->name, v->lineno);
					peer->maxms = 0;
				}
			}
			/* else if (strcasecmp(v->name,"type"))
			 *	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
			 */
			v=v->next;
		}
		if (ast_strlen_zero(peer->methods))
			strcpy(peer->methods, "md5,plaintext");
		if (!found && peer->dynamic)
			reg_source_db(peer);
		peer->delme = 0;
	}
	if (oldha)
		ast_free_ha(oldha);
	return peer;
}

static int reload_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct sip_peer *peer;
	struct sip_user *user;
	struct ast_hostent ahp;
	char *cat;
    char *utype;
	struct hostent *hp;
	int format;
	int oldport = ntohs(bindaddr.sin_port);

	globaldtmfmode = SIP_DTMF_RFC2833;
	
	if (gethostname(ourhost, sizeof(ourhost))) {
		ast_log(LOG_WARNING, "Unable to get hostname, SIP disabled\n");
		return 0;
	}
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, SIP disabled\n", config);
		return 0;
	}
	
	globalnat = 0;
	
	sip_prefs_free();
	
	memset(&bindaddr, 0, sizeof(bindaddr));
	memset(&localnet, 0, sizeof(localnet));
	memset(&localmask, 0, sizeof(localmask));
	memset(&externip, 0, sizeof(externip));

	/* Initialize some reasonable defaults */
	strncpy(context, "default", sizeof(context) - 1);
	strcpy(language, "");
	strcpy(fromdomain, "");
	globalcanreinvite = REINVITE_INVITE;
	videosupport = 0;
	relaxdtmf = 0;
	pedanticsipchecking=0;
	v = ast_variable_browse(cfg, "general");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "relaxdtmf")) {
			relaxdtmf = ast_true(v->value);
		} else if (!strcasecmp(v->name, "dtmfmode")) {
			if (!strcasecmp(v->value, "inband"))
				globaldtmfmode=SIP_DTMF_INBAND;
			else if (!strcasecmp(v->value, "rfc2833"))
				globaldtmfmode = SIP_DTMF_RFC2833;
			else if (!strcasecmp(v->value, "info"))
				globaldtmfmode = SIP_DTMF_INFO;
			else {
				ast_log(LOG_WARNING, "Unknown dtmf mode '%s', using rfc2833\n", v->value);
				globaldtmfmode = SIP_DTMF_RFC2833;
			}
		} else if (!strcasecmp(v->name, "videosupport")) {
			videosupport = ast_true(v->value);
		} else if (!strcasecmp(v->name, "notifymimetype")) {
			strncpy(notifymime, v->value, sizeof(notifymime) - 1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
		} else if (!strcasecmp(v->name, "callerid")) {
			strncpy(callerid, v->value, sizeof(callerid)-1);
		} else if (!strcasecmp(v->name, "fromdomain")) {
			strncpy(fromdomain, v->value, sizeof(fromdomain)-1);
		} else if (!strcasecmp(v->name, "nat")) {
			globalnat = ast_true(v->value);
		} else if (!strcasecmp(v->name, "autocreatepeer")) {
			autocreatepeer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "srvlookup")) {
			srvlookup = ast_true(v->value);
		} else if (!strcasecmp(v->name, "pedantic")) {
			pedanticsipchecking = ast_true(v->value);
		} else if (!strcasecmp(v->name, "canreinvite")) {
			if (!strcasecmp(v->value, "update"))
				globalcanreinvite = REINVITE_UPDATE;
			else
				globalcanreinvite = ast_true(v->value);
		} else if (!strcasecmp(v->name, "maxexpirey") || !strcasecmp(v->name, "maxexpiry")) {
			max_expiry = atoi(v->value);
			if (max_expiry < 1)
				max_expiry = DEFAULT_MAX_EXPIRY;
		} else if (!strcasecmp(v->name, "defaultexpiry") || !strcasecmp(v->name, "defaultexpirey")) {
			default_expiry = atoi(v->value);
			if (default_expiry < 1)
				default_expiry = DEFAULT_DEFAULT_EXPIRY;
		} else if (!strcasecmp(v->name, "bindaddr")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
			} else {
				memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
			}
		} else if (!strcasecmp(v->name, "localnet")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp)))
				ast_log(LOG_WARNING, "Invalid localnet keyword: %s\n", v->value);
			else 
				memcpy(&localnet.sin_addr, hp->h_addr, sizeof(localnet.sin_addr));
		} else if (!strcasecmp(v->name, "localmask")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp)))
				ast_log(LOG_WARNING, "Invalid localmask keyword: %s\n", v->value);
			else
				memcpy(&localmask.sin_addr, hp->h_addr, sizeof(localmask.sin_addr));
		} else if (!strcasecmp(v->name, "externip")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) 
				ast_log(LOG_WARNING, "Invalid address for externip keyword: %s\n", v->value);
			else
				memcpy(&externip.sin_addr, hp->h_addr, sizeof(externip.sin_addr));
		} else if (!strcasecmp(v->name, "allow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot allow unknown format '%s'\n", v->value);
			else {
				capability |= format;
				sip_pref_append(format);
			}
		} else if (!strcasecmp(v->name, "disallow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot disallow unknown format '%s'\n", v->value);
			else {
				capability &= ~format;
				sip_pref_remove(format);
			}
		} else if (!strcasecmp(v->name, "register")) {
			sip_register(v->value, v->lineno);
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%i", &format) == 1)
				tos = format & 0xff;
			else if (!strcasecmp(v->value, "lowdelay"))
				tos = IPTOS_LOWDELAY;
			else if (!strcasecmp(v->value, "throughput"))
				tos = IPTOS_THROUGHPUT;
			else if (!strcasecmp(v->value, "reliability"))
				tos = IPTOS_RELIABILITY;
			else if (!strcasecmp(v->value, "mincost"))
				tos = IPTOS_MINCOST;
			else if (!strcasecmp(v->value, "none"))
				tos = 0;
			else
				ast_log(LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
		} else if (!strcasecmp(v->name, "port")) {
			if (sscanf(v->value, "%i", &ourport) == 1) {
				bindaddr.sin_port = htons(ourport);
			} else {
				ast_log(LOG_WARNING, "Invalid port number '%s' at line %d of %s\n", v->value, v->lineno, config);
			}
#ifdef MYSQL_FRIENDS
		} else if (!strcasecmp(v->name, "dbuser")) {
			strncpy(mydbuser, v->value, sizeof(mydbuser) - 1);
		} else if (!strcasecmp(v->name, "dbpass")) {
			strncpy(mydbpass, v->value, sizeof(mydbpass) - 1);
		} else if (!strcasecmp(v->name, "dbhost")) {
			strncpy(mydbhost, v->value, sizeof(mydbhost) - 1);
		} else if (!strcasecmp(v->name, "dbname")) {
			strncpy(mydbname, v->value, sizeof(mydbname) - 1);
#endif
		}
		/* else if (strcasecmp(v->name,"type"))
		 *	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
		 */
		 v = v->next;
	}
	
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (utype) {
				if (!strcasecmp(utype, "user") || !strcasecmp(utype, "friend")) {
					user = build_user(cat, ast_variable_browse(cfg, cat));
					if (user) {
						ast_mutex_lock(&userl.lock);
						user->next = userl.users;
						userl.users = user;
						ast_mutex_unlock(&userl.lock);
					}
				}
				if (!strcasecmp(utype, "peer") || !strcasecmp(utype, "friend")) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat));
					if (peer) {
						ast_mutex_lock(&peerl.lock);
						peer->next = peerl.peers;
						peerl.peers = peer;
						ast_mutex_unlock(&peerl.lock);
					}
				} else if (strcasecmp(utype, "user")) {
					ast_log(LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, "sip.conf");
				}
			} else
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
		}
		cat = ast_category_browse(cfg, cat);
	}
	
	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(&__ourip, &bindaddr.sin_addr, sizeof(__ourip));
	} else {
		hp = ast_gethostbyname(ourhost, &ahp);
		if (!hp) {
			ast_log(LOG_WARNING, "Unable to get IP address for %s, SIP disabled\n", ourhost);
			if (!__ourip.s_addr)
				return 0;
		} else
			memcpy(&__ourip, hp->h_addr, sizeof(__ourip));
	}
	if (!ntohs(bindaddr.sin_port))
		bindaddr.sin_port = ntohs(DEFAULT_SIP_PORT);
	bindaddr.sin_family = AF_INET;
	ast_mutex_lock(&netlock);
	if ((sipsock > -1) && (ntohs(bindaddr.sin_port) != oldport)) {
		close(sipsock);
		sipsock = -1;
	}
	if (sipsock < 0) {
		sipsock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sipsock < 0) {
			ast_log(LOG_WARNING, "Unable to create SIP socket: %s\n", strerror(errno));
		} else {
		        /* Allow SIP clients on the same host to access us: */
		        const int reuseFlag = 1;
			setsockopt(sipsock, SOL_SOCKET, SO_REUSEADDR,
				   (const char*)&reuseFlag,
				   sizeof reuseFlag);

			if (bind(sipsock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
				ast_log(LOG_WARNING, "Failed to bind to %s:%d: %s\n",
						inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port),
							strerror(errno));
				close(sipsock);
				sipsock = -1;
			} else {
				if (option_verbose > 1) { 
						ast_verbose(VERBOSE_PREFIX_2 "SIP Listening on %s:%d\n", 
					inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port));
					ast_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);
				}
				if (setsockopt(sipsock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))) 
					ast_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
			}
		}
	}
	ast_mutex_unlock(&netlock);

	ast_destroy(cfg);
#ifdef MYSQL_FRIENDS
	/* Connect to db if appropriate */
	if (!mysql && !ast_strlen_zero(mydbname)) {
		mysql = mysql_init(NULL);
		if (!mysql_real_connect(mysql, mydbhost[0] ? mydbhost : NULL, mydbuser, mydbpass, mydbname, 0, NULL, 0)) {
			memset(mydbpass, '*', strlen(mydbpass));
			ast_log(LOG_WARNING, "Database connection failed (db=%s, host=%s, user=%s, pass=%s)!\n",
				mydbname, mydbhost, mydbuser, mydbpass);
			free(mysql);
			mysql = NULL;
		} else
			ast_verbose(VERBOSE_PREFIX_1 "Connected to database '%s' on '%s' as '%s'\n",
				mydbname, mydbhost, mydbuser);
	}
#endif
	return 0;
}

static struct ast_rtp *sip_get_rtp_peer(struct ast_channel *chan)
{
	struct sip_pvt *p;
	p = chan->pvt->pvt;
	if (p && p->rtp && p->canreinvite)
		return p->rtp;
	return NULL;
}

static struct ast_rtp *sip_get_vrtp_peer(struct ast_channel *chan)
{
	struct sip_pvt *p;
	p = chan->pvt->pvt;
	if (p && p->vrtp && p->canreinvite)
		return p->vrtp;
	return NULL;
}

static int sip_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, struct ast_rtp *vrtp)
{
	struct sip_pvt *p;
	p = chan->pvt->pvt;
	if (p) {
		if (rtp)
			ast_rtp_get_peer(rtp, &p->redirip);
		else
			memset(&p->redirip, 0, sizeof(p->redirip));
		if (vrtp)
			ast_rtp_get_peer(vrtp, &p->vredirip);
		else
			memset(&p->vredirip, 0, sizeof(p->vredirip));
		if (!p->gotrefer) {
			transmit_reinvite_with_sdp(p, rtp, vrtp);
			p->outgoing = 1;
		}
		return 0;
	}
	return -1;
}

static char *synopsis_dtmfmode = "Change the dtmfmode for a SIP call";
static char *descrip_dtmfmode = "SIPDtmfMode(inband|info|rfc2833): Changes the dtmfmode for a SIP call\n";
static char *app_dtmfmode = "SIPDtmfMode";
static int sip_dtmfmode(struct ast_channel *chan, void *data)
{
	struct sip_pvt *p;
	char *mode;
	if (data)
		mode = (char *)data;
	else {
		ast_log(LOG_WARNING, "This application requires the argument: info, inband, rfc2833\n");
		return 0;
	}
	ast_mutex_lock(&chan->lock);
	if (chan->type != type) {
		ast_log(LOG_WARNING, "Call this application only on SIP incoming calls\n");
		ast_mutex_unlock(&chan->lock);
		return 0;
	}
	p = chan->pvt->pvt;
	if (p) {
		ast_mutex_lock(&p->lock);
		if (!strcasecmp(mode,"info"))
			p->dtmfmode = SIP_DTMF_INFO;
		else if (!strcasecmp(mode,"rfc2833"))
			p->dtmfmode = SIP_DTMF_RFC2833;
		else if (!strcasecmp(mode,"inband"))
			p->dtmfmode = SIP_DTMF_INBAND;
		else
			ast_log(LOG_WARNING, "I don't know about this dtmf mode: %s\n",mode);
        if (p->dtmfmode & SIP_DTMF_INBAND) {
				if (!p->vad) {
	               p->vad = ast_dsp_new();
	               ast_dsp_set_features(p->vad, DSP_FEATURE_DTMF_DETECT);
				}
        } else {
			if (p->vad) {
				ast_dsp_free(p->vad);
				p->vad = NULL;
			}
		}
		ast_mutex_unlock(&p->lock);
	}
	ast_mutex_unlock(&chan->lock);
	return 0;
}

static int sip_get_codec(struct ast_channel *chan)
{
	struct sip_pvt *p = chan->pvt->pvt;
	return p->capability;	
}

static struct ast_rtp_protocol sip_rtp = {
	get_rtp_info: sip_get_rtp_peer,
	get_vrtp_info: sip_get_vrtp_peer,
	set_rtp_peer: sip_set_rtp_peer,
	get_codec: sip_get_codec,
};

static void delete_users(void)
{
	struct sip_user *user, *userlast;
	struct sip_peer *peer;
	struct sip_registry *reg, *regn;

	/* Delete all users */
	ast_mutex_lock(&userl.lock);
	for (user=userl.users;user;) {
		ast_free_ha(user->ha);
		userlast = user;
		user=user->next;
		free(userlast);
	}
	userl.users=NULL;
	ast_mutex_unlock(&userl.lock);

	ast_mutex_lock(&regl.lock);
	for (reg = regl.registrations;reg;) {
		regn = reg->next;
		/* Really delete */
		if (reg->call) {
			/* Clear registry before destroying to ensure
			   we don't get reentered trying to grab the registry lock */
			reg->call->registry = NULL;
			sip_destroy(reg->call);
		}
		if (reg->expire > -1)
			ast_sched_del(sched, reg->expire);
		if (reg->timeout > -1)
			ast_sched_del(sched, reg->timeout);
		free(reg);
		reg = regn;
	}
	regl.registrations = NULL;
	ast_mutex_unlock(&regl.lock);
	
	ast_mutex_lock(&peerl.lock);
	for (peer=peerl.peers;peer;) {
		/* Assume all will be deleted, and we'll find out for sure later */
		peer->delme = 1;
		peer = peer->next;
	}
	ast_mutex_unlock(&peerl.lock);
}

static void prune_peers(void)
{
	/* Prune peers who still are supposed to be deleted */
	struct sip_peer *peer, *peerlast, *peernext;
	ast_mutex_lock(&peerl.lock);
	peerlast = NULL;
	for (peer=peerl.peers;peer;) {
		peernext = peer->next;
		if (peer->delme) {
			/* Delete it, it needs to disappear */
			if (peer->call)
				sip_destroy(peer->call);
			if (peer->expire > -1)
				ast_sched_del(sched, peer->expire);
			if (peer->pokeexpire > -1)
				ast_sched_del(sched, peer->pokeexpire);
			free(peer);
			if (peerlast)
				peerlast->next = peernext;
			else
				peerl.peers = peernext;
		} else
			peerlast = peer;
		peer=peernext;
	}
	ast_mutex_unlock(&peerl.lock);
}

static int sip_do_reload(void)
{
	struct sip_registry *reg;
	struct sip_peer *peer;
	delete_users();
	reload_config();

	prune_peers();
	/* And start the monitor for the first time */
	ast_mutex_lock(&regl.lock);
	for (reg = regl.registrations; reg; reg = reg->next) 
		__sip_do_register(reg);
	ast_mutex_unlock(&regl.lock);
	ast_mutex_lock(&peerl.lock);
	for (peer = peerl.peers; peer; peer = peer->next)
		sip_poke_peer(peer);
	ast_mutex_unlock(&peerl.lock);

	return 0;
}

static int sip_reload(int fd, int argc, char *argv[])
{

	ast_mutex_lock(&sip_reload_lock);
	if (sip_reloading) {
		ast_verbose("Previous SIP reload not yet done\n");
	} else
		sip_reloading = 1;
	ast_mutex_unlock(&sip_reload_lock);
	restart_monitor();
	return 0;
}

int reload(void)
{
	return sip_reload(0, 0, NULL);
}

static struct ast_cli_entry  cli_sip_reload =
	{ { "sip", "reload", NULL }, sip_reload, "Reload SIP configuration", sip_reload_usage };

int load_module()
{
	int res;
	struct sip_peer *peer;
	struct sip_registry *reg;
	sched = sched_context_create();
	if (!sched) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
	}
	io = io_context_create();
	if (!io) {
		ast_log(LOG_WARNING, "Unable to create I/O context\n");
	}
	
	res = reload_config();
	if (!res) {
		/* Make sure we can register our sip channel type */
		if (ast_channel_register_ex(type, tdesc, ((AST_FORMAT_MAX_AUDIO << 1) - 1), sip_request, sip_devicestate)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
			return -1;
		}
		ast_cli_register(&cli_show_users);
		ast_cli_register(&cli_show_channels);
		ast_cli_register(&cli_show_channel);
		ast_cli_register(&cli_show_peers);
		ast_cli_register(&cli_show_peers_begin);
		ast_cli_register(&cli_show_peers_include);
		ast_cli_register(&cli_show_peers_exclude);
		ast_cli_register(&cli_show_registry);
		ast_cli_register(&cli_debug);
		ast_cli_register(&cli_debug_ip);
		ast_cli_register(&cli_debug_peer);
		ast_cli_register(&cli_no_debug);
		ast_cli_register(&cli_sip_reload);
		ast_cli_register(&cli_inuse_show);
		sip_rtp.type = type;
		ast_rtp_proto_register(&sip_rtp);
		ast_register_application(app_dtmfmode, sip_dtmfmode, synopsis_dtmfmode, descrip_dtmfmode);
		ast_mutex_lock(&peerl.lock);
		for (peer = peerl.peers; peer; peer = peer->next)
			sip_poke_peer(peer);
		ast_mutex_unlock(&peerl.lock);

		ast_mutex_lock(&regl.lock);
		for (reg = regl.registrations; reg; reg = reg->next) 
			__sip_do_register(reg);
		ast_mutex_unlock(&regl.lock);
		
		/* And start the monitor for the first time */
		restart_monitor();
	}
	return res;
}

int unload_module()
{
	struct sip_pvt *p, *pl;
	
	/* First, take us out of the channel loop */
	ast_unregister_application(app_dtmfmode);
	ast_cli_unregister(&cli_show_users);
	ast_cli_unregister(&cli_show_channels);
	ast_cli_unregister(&cli_show_channel);
	ast_cli_unregister(&cli_show_peers);
	ast_cli_unregister(&cli_show_peers_include);
	ast_cli_unregister(&cli_show_peers_exclude);
	ast_cli_unregister(&cli_show_peers_begin);
	ast_cli_unregister(&cli_show_registry);
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_debug_ip);
	ast_cli_unregister(&cli_debug_peer);
	ast_cli_unregister(&cli_no_debug);
	ast_cli_unregister(&cli_sip_reload);
	ast_cli_unregister(&cli_inuse_show);
	ast_rtp_proto_unregister(&sip_rtp);
	ast_channel_unregister(type);
	if (!ast_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}
	if (!ast_mutex_lock(&monlock)) {
		if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP)) {
			pthread_cancel(monitor_thread);
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = AST_PTHREADT_STOP;
		ast_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	if (!ast_mutex_lock(&iflock)) {
		/* Destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			pl = p;
			p = p->next;
			/* Free associated memory */
			free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}
		
	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

char *description()
{
	return desc;
}

#if 0
/* XXX What is this?
 */
static char *getsipuri(char *header)
{
	char *c, *d, *retval;
	int n;

	if (!(c=strstr(header, "sip"))) {
		return NULL;
	}

	if (!(d=strchr(c, '@'))) {
		return NULL;
	}

	n=d-c;

	retval=(char *)malloc(n+1);
	strncpy(retval, c, n);
	*(retval+n)='\0';

	return retval;
}
#endif 

