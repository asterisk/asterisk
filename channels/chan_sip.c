/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Session Initiation Protocol
 * 
 * Copyright (C) 2004 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */


#include <stdio.h>
#include <ctype.h>
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
#include <asterisk/manager.h>
#include <asterisk/callerid.h>
#include <asterisk/cli.h>
#include <asterisk/md5.h>
#include <asterisk/app.h>
#include <asterisk/musiconhold.h>
#include <asterisk/dsp.h>
#include <asterisk/features.h>
#include <asterisk/acl.h>
#include <asterisk/srv.h>
#include <asterisk/astdb.h>
#include <asterisk/causes.h>
#include <asterisk/utils.h>
#include <asterisk/file.h>
#include <asterisk/astobj.h>
#ifdef OSP_SUPPORT
#include <asterisk/astosp.h>
#endif
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/signal.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <regex.h>

#ifndef DEFAULT_USERAGENT
#define DEFAULT_USERAGENT "Asterisk PBX"
#endif
 
#define VIDEO_CODEC_MASK        0x1fc0000 /* Video codecs from H.261 thru AST_FORMAT_MAX_VIDEO */
#ifndef IPTOS_MINCOST
#define IPTOS_MINCOST 0x02
#endif

/* #define VOCAL_DATA_HACK */

#define SIPDUMPER
#define DEFAULT_DEFAULT_EXPIRY  120
#define DEFAULT_MAX_EXPIRY      3600
#define DEFAULT_REGISTRATION_TIMEOUT	20

/* guard limit must be larger than guard secs */
/* guard min must be < 1000, and should be >= 250 */
#define EXPIRY_GUARD_SECS	15	/* How long before expiry do we reregister */
#define EXPIRY_GUARD_LIMIT      30	/* Below here, we use EXPIRY_GUARD_PCT instead of 
					   EXPIRY_GUARD_SECS */
#define EXPIRY_GUARD_MIN	500	/* This is the minimum guard time applied. If 
					   GUARD_PCT turns out to be lower than this, it 
					   will use this time instead.
					   This is in milliseconds. */
#define EXPIRY_GUARD_PCT        0.20	/* Percentage of expires timeout to use when 
					   below EXPIRY_GUARD_LIMIT */

static int max_expiry = DEFAULT_MAX_EXPIRY;
static int default_expiry = DEFAULT_DEFAULT_EXPIRY;

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define CALLERID_UNKNOWN	"Unknown"



#define DEFAULT_MAXMS		2000		/* Must be faster than 2 seconds by default */
#define DEFAULT_FREQ_OK		60 * 1000	/* How often to check for the host to be up */
#define DEFAULT_FREQ_NOTOK	10 * 1000	/* How often to check, if the host is down... */

#define DEFAULT_RETRANS		1000		/* How frequently to retransmit */
#define MAX_RETRANS		5		/* Try only 5 times for retransmissions */


#define DEBUG_READ	0			/* Recieved data	*/
#define DEBUG_SEND	1			/* Transmit data	*/

static char *desc = "Session Initiation Protocol (SIP)";
static char *channeltype = "SIP";
static char *tdesc = "Session Initiation Protocol (SIP)";
static char *config = "sip.conf";
static char *notify_config = "sip_notify.conf";

#define DEFAULT_SIP_PORT	5060	/* From RFC 2543 */
#define SIP_MAX_PACKET		4096	/* Also from RFC 2543, should sub headers tho */

#define ALLOWED_METHODS "INVITE, ACK, CANCEL, OPTIONS, BYE, REFER"

static char default_useragent[AST_MAX_EXTENSION] = DEFAULT_USERAGENT;

#define DEFAULT_CONTEXT "default"
static char default_context[AST_MAX_EXTENSION] = DEFAULT_CONTEXT;

static char default_language[MAX_LANGUAGE] = "";

#define DEFAULT_CALLERID "asterisk"
static char default_callerid[AST_MAX_EXTENSION] = DEFAULT_CALLERID;

static char default_fromdomain[AST_MAX_EXTENSION] = "";

#define DEFAULT_NOTIFYMIME "application/simple-message-summary"
static char default_notifymime[AST_MAX_EXTENSION] = DEFAULT_NOTIFYMIME;

static struct ast_flags global_flags = {0};		/* global SIP_ flags */
static struct ast_flags global_flags_page2 = {0};		/* more global SIP_ flags */

static int srvlookup = 0;		/* SRV Lookup on or off. Default is off, RFC behavior is on */

static int pedanticsipchecking = 0;	/* Extra checking ?  Default off */

static int autocreatepeer = 0;		/* Auto creation of peers at registration? Default off. */

static int relaxdtmf = 0;

static int global_rtptimeout = 0;

static int global_rtpholdtimeout = 0;

static int global_rtpkeepalive = 0;

static int global_reg_timeout = DEFAULT_REGISTRATION_TIMEOUT;

/* Object counters */
static int suserobjs = 0;
static int ruserobjs = 0;
static int speerobjs = 0;
static int rpeerobjs = 0;
static int apeerobjs = 0;
static int regobjs = 0;

static int global_allowguest = 0;    /* allow unauthenticated users/peers to connect? */

#define DEFAULT_MWITIME 10
static int global_mwitime = DEFAULT_MWITIME;	/* Time between MWI checks for peers */

static int usecnt =0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);


/* Protect the interface list (of sip_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(netlock);

AST_MUTEX_DEFINE_STATIC(monlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

/* Codecs that we support by default: */
static int global_capability = AST_FORMAT_ULAW | AST_FORMAT_ALAW | AST_FORMAT_GSM | AST_FORMAT_H263;
static int noncodeccapability = AST_RTP_DTMF;

static struct in_addr __ourip;
static struct sockaddr_in outboundproxyip;
static int ourport;

static int sipdebug = 0;
static struct sockaddr_in debugaddr;

static int tos = 0;

static int videosupport = 0;

static int compactheaders = 0;				/* send compact sip headers */

static int recordhistory = 0;				/* Record SIP history. Off by default */

static char global_musicclass[MAX_LANGUAGE] = "";	/* Global music on hold class */
#define DEFAULT_REALM	"asterisk"
static char global_realm[AST_MAX_EXTENSION] = DEFAULT_REALM; 	/* Default realm */
static char regcontext[AST_MAX_EXTENSION] = "";		/* Context for auto-extensions */

/* Expire slowly */
#define DEFAULT_EXPIRY 900
static int expiry = DEFAULT_EXPIRY;

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

static struct ast_codec_pref prefs;


/* sip_request: The data grabbed from the UDP socket */
struct sip_request {
	char *rlPart1; 		/* SIP Method Name or "SIP/2.0" protocol version */
	char *rlPart2; 		/* The Request URI or Response Status */
	int len;		/* Length */
	int headers;		/* # of SIP Headers */
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

struct sip_history {
	char event[80];
	struct sip_history *next;
};

#define SIP_ALREADYGONE		(1 << 0)	/* Whether or not we've already been destroyed by our peer */
#define SIP_NEEDDESTROY		(1 << 1)	/* if we need to be destroyed */
#define SIP_NOVIDEO		(1 << 2)	/* Didn't get video in invite, don't offer */
#define SIP_RINGING		(1 << 3)	/* Have sent 180 ringing */
#define SIP_PROGRESS_SENT	(1 << 4)	/* Have sent 183 message progress */
#define SIP_NEEDREINVITE	(1 << 5)	/* Do we need to send another reinvite? */
#define SIP_PENDINGBYE		(1 << 6)	/* Need to send bye after we ack? */
#define SIP_GOTREFER		(1 << 7)	/* Got a refer? */
#define SIP_PROMISCREDIR	(1 << 8)	/* Promiscuous redirection */
#define SIP_TRUSTRPID		(1 << 9)	/* Trust RPID headers? */
#define SIP_USEREQPHONE		(1 << 10)	/* Add user=phone to numeric URI. Default off */
#define SIP_REALTIME		(1 << 11)	/* Flag for realtime users */
#define SIP_USECLIENTCODE	(1 << 12)	/* Trust X-ClientCode info message */
#define SIP_OUTGOING		(1 << 13)	/* Is this an outgoing call? */
#define SIP_SELFDESTRUCT	(1 << 14)	
#define SIP_DYNAMIC		(1 << 15)	/* Is this a dynamic peer? */
/* --- Choices for DTMF support in SIP channel */
#define SIP_DTMF		(3 << 16)	/* three settings, uses two bits */
#define SIP_DTMF_RFC2833	(0 << 16)	/* RTP DTMF */
#define SIP_DTMF_INBAND		(1 << 16)	/* Inband audio, only for ULAW/ALAW */
#define SIP_DTMF_INFO		(2 << 16)	/* SIP Info messages */
/* NAT settings */
#define SIP_NAT			(3 << 18)	/* four settings, uses two bits */
#define SIP_NAT_NEVER		(0 << 18)	/* No nat support */
#define SIP_NAT_RFC3581		(1 << 18)
#define SIP_NAT_ROUTE		(2 << 18)
#define SIP_NAT_ALWAYS		(3 << 18)
/* re-INVITE related settings */
#define SIP_REINVITE		(3 << 20)	/* two bits used */
#define SIP_CAN_REINVITE	(1 << 20)	/* allow peers to be reinvited to send media directly p2p */
#define SIP_REINVITE_UPDATE	(2 << 20)	/* use UPDATE (RFC3311) when reinviting this peer */
/* "insecure" settings */
#define SIP_INSECURE		(3 << 22)	/* three settings, uses two bits */
#define SIP_SECURE		(0 << 22)
#define SIP_INSECURE_NORMAL	(1 << 22)
#define SIP_INSECURE_VERY	(2 << 22)
/* Sending PROGRESS in-band settings */
#define SIP_PROG_INBAND		(3 << 24)	/* three settings, uses two bits */
#define SIP_PROG_INBAND_NEVER	(0 << 24)
#define SIP_PROG_INBAND_NO	(1 << 24)
#define SIP_PROG_INBAND_YES	(2 << 24)
/* Open Settlement Protocol authentication */
#define SIP_OSPAUTH		(3 << 26)	/* three settings, uses two bits */
#define SIP_OSPAUTH_NO		(0 << 26)
#define SIP_OSPAUTH_YES		(1 << 26)
#define SIP_OSPAUTH_EXCLUSIVE	(2 << 26)
/* Call states */
#define SIP_CALL_ONHOLD		(1 << 28)

/* a new page of flags */
#define SIP_PAGE2_RTCACHEFRIENDS 	(1 << 0)
#define SIP_PAGE2_RTNOUPDATE 		(1 << 1)
#define SIP_PAGE2_RTAUTOCLEAR 		(1 << 2)

static int global_rtautoclear = 120;

/* sip_pvt: PVT structures are used for each SIP conversation, ie. a call  */
static struct sip_pvt {
	ast_mutex_t lock;			/* Channel private lock */
	char callid[80];			/* Global CallID */
	char randdata[80];			/* Random data */
	struct ast_codec_pref prefs;		/* codec prefs */
	unsigned int ocseq;			/* Current outgoing seqno */
	unsigned int icseq;			/* Current incoming seqno */
	ast_group_t callgroup;			/* Call group */
	ast_group_t pickupgroup;		/* Pickup group */
	int lastinvite;				/* Last Cseq of invite */
	unsigned int flags;			/* SIP_ flags */	
	int capability;				/* Special capability (codec) */
	int jointcapability;			/* Supported capability at both ends (codecs ) */
	int peercapability;			/* Supported peer capability */
	int prefcodec;				/* Preferred codec (outbound only) */
	int noncodeccapability;
	int callingpres;			/* Calling presentation */
	int authtries;				/* Times we've tried to authenticate */
	int expiry;				/* How long we take to expire */
	int branch;				/* One random number */
	int tag;				/* Another random number */
	int sessionid;				/* SDP Session ID */
	int sessionversion;			/* SDP Session Version */
	struct sockaddr_in sa;			/* Our peer */
	struct sockaddr_in redirip;		/* Where our RTP should be going if not to us */
	struct sockaddr_in vredirip;		/* Where our Video RTP should be going if not to us */
	int redircodecs;			/* Redirect codecs */
	struct sockaddr_in recv;		/* Received as */
	struct in_addr ourip;			/* Our IP */
	struct ast_channel *owner;		/* Who owns us */
	char exten[AST_MAX_EXTENSION];		/* Extension where to start */
	char refer_to[AST_MAX_EXTENSION];	/* Place to store REFER-TO extension */
	char referred_by[AST_MAX_EXTENSION];	/* Place to store REFERRED-BY extension */
	char refer_contact[AST_MAX_EXTENSION];	/* Place to store Contact info from a REFER extension */
	struct sip_pvt *refer_call;		/* Call we are referring */
	struct sip_route *route;		/* Head of linked list of routing steps (fm Record-Route) */
	int route_persistant;			/* Is this the "real" route? */
	char from[256];				/* The From: header */
	char useragent[256];			/* User agent in SIP request */
	char context[AST_MAX_EXTENSION];	/* Context for this call */
	char fromdomain[AST_MAX_EXTENSION];	/* Domain to show in the from field */
	char fromuser[AST_MAX_EXTENSION];	/* User to show in the user field */
	char fromname[AST_MAX_EXTENSION];	/* Name to show in the user field */
	char tohost[AST_MAX_EXTENSION];		/* Host we should put in the "to" field */
	char language[MAX_LANGUAGE];		/* Default language for this call */
	char musicclass[MAX_LANGUAGE];          /* Music on Hold class */
	char rdnis[256];			/* Referring DNIS */
	char theirtag[256];			/* Their tag */
	char username[256];
	char peername[256];
	char authname[256];			/* Who we use for authentication */
	char uri[256];				/* Original requested URI */
	char okcontacturi[256];			/* URI from the 200 OK on INVITE */
	char peersecret[256];			/* Password */
	char peermd5secret[256];
	char cid_num[256];			/* Caller*ID */
	char cid_name[256];			/* Caller*ID */
	char via[256];				/* Via: header */
	char fullcontact[128];			/* The Contact: that the UA registers with us */
	char accountcode[20];			/* Account code */
	char our_contact[256];			/* Our contact header */
	char realm[256];			/* Authorization realm */
	char nonce[256];			/* Authorization nonce */
	char opaque[256];			/* Opaque nonsense */
	char qop[80];				/* Quality of Protection, since SIP wasn't complicated enough yet. */
	char domain[256];			/* Authorization domain */
	char lastmsg[256];			/* Last Message sent/received */
	int amaflags;				/* AMA Flags */
	int pendinginvite;			/* Any pending invite */
#ifdef OSP_SUPPORT
	int osphandle;				/* OSP Handle for call */
	time_t ospstart;			/* OSP Start time */
#endif
	struct sip_request initreq;		/* Initial request */
	
	int maxtime;				/* Max time for first response */
	int initid;				/* Auto-congest ID if appropriate */
	int autokillid;				/* Auto-kill ID */
	time_t lastrtprx;			/* Last RTP received */
	time_t lastrtptx;			/* Last RTP sent */
	int rtptimeout;				/* RTP timeout time */
	int rtpholdtimeout;			/* RTP timeout when on hold */
	int rtpkeepalive;			/* Send RTP packets for keepalive */

	int subscribed;				/* Is this call a subscription?  */
	int stateid;
	int dialogver;
	
	struct ast_dsp *vad;			/* Voice Activation Detection dsp */
	
	struct sip_peer *peerpoke;		/* If this calls is to poke a peer, which one */
	struct sip_registry *registry;		/* If this is a REGISTER call, to which registry */
	struct ast_rtp *rtp;			/* RTP Session */
	struct ast_rtp *vrtp;			/* Video RTP session */
	struct sip_pkt *packets;		/* Packets scheduled for re-transmission */
	struct sip_history *history;		/* History of this SIP dialog */
	struct ast_variable *chanvars;
	struct sip_pvt *next;			/* Next call in chain */
} *iflist = NULL;

#define FLAG_RESPONSE (1 << 0)
#define FLAG_FATAL (1 << 1)

/* sip packet - read in sipsock_read, transmitted in send_request */
struct sip_pkt {
	struct sip_pkt *next;			/* Next packet */
	int retrans;				/* Retransmission number */
	int seqno;				/* Sequence number */
	unsigned int flags;			/* non-zero if this is a response packet (e.g. 200 OK) */
	struct sip_pvt *owner;			/* Owner call */
	int retransid;				/* Retransmission ID */
	int packetlen;				/* Length of packet */
	char data[0];
};	

/* Structure for SIP user data. User's place calls to us */
struct sip_user {
	/* Users who can access various contexts */
	ASTOBJ_COMPONENTS(struct sip_user);
	char secret[80];		/* Password */
	char md5secret[80];		/* Password in md5 */
	char context[80];		/* Default context for incoming calls */
	char cid_num[80];		/* Caller ID num */
	char cid_name[80];		/* Caller ID name */
	char accountcode[20];		/* Account code */
	char language[MAX_LANGUAGE];	/* Default language for this user */
	char musicclass[MAX_LANGUAGE];  /* Music on Hold class */
	char useragent[256];		/* User agent in SIP request */
	struct ast_codec_pref prefs;	/* codec prefs */
	ast_group_t callgroup;		/* Call group */
	ast_group_t pickupgroup;	/* Pickup Group */
	unsigned int flags;		/* SIP_ flags */	
	int amaflags;			/* AMA flags for billing */
	int callingpres;		/* Calling id presentation */
	int capability;			/* Codec capability */
	int inUse;			/* Number of calls in use */
	int incominglimit;		/* Limit of incoming calls */
	int outUse;			/* disabled */
	int outgoinglimit;		/* disabled */
	struct ast_ha *ha;		/* ACL setting */
	struct ast_variable *chanvars;	/* Variables to set for channel created by user */
};

/* Structure for SIP peer data, we place calls to peers if registred  or fixed IP address (host) */
struct sip_peer {
	ASTOBJ_COMPONENTS(struct sip_peer);	/* name, refcount, objflags,  object pointers */
					/* peer->name is the unique name of this object */
	char secret[80];		/* Password */
	char md5secret[80];		/* Password in MD5 */
	char context[80];		/* Default context for incoming calls */
	char username[80];		/* Temporary username until registration */ 
	char accountcode[20];		/* Account code */
	int amaflags;			/* AMA Flags (for billing) */
	char tohost[80];		/* If not dynamic, IP address */
	char regexten[AST_MAX_EXTENSION]; /* Extension to register (if regcontext is used) */
	char fromuser[80];		/* From: user when calling this peer */
	char fromdomain[80];		/* From: domain when calling this peer */
	char fullcontact[128];		/* Contact registred with us (not in sip.conf) */
	char cid_num[80];		/* Caller ID num */
	char cid_name[80];		/* Caller ID name */
	int callingpres;		/* Calling id presentation */
	int inUse;			/* Number of calls in use */
	int incominglimit;		/* Limit of incoming calls */
	int outUse;			/* disabled */
	int outgoinglimit;		/* disabled */
	char mailbox[AST_MAX_EXTENSION]; /* Mailbox setting for MWI checks */
	char language[MAX_LANGUAGE];	/* Default language for prompts */
	char musicclass[MAX_LANGUAGE];  /* Music on Hold class */
	char useragent[256];		/* User agent in SIP request (saved from registration) */
	struct ast_codec_pref prefs;	/* codec prefs */
	int lastmsgssent;
	time_t	lastmsgcheck;		/* Last time we checked for MWI */
	unsigned int flags;		/* SIP_ flags */	
	struct ast_flags flags_page2;	/* SIP_PAGE2 flags */
	int expire;			/* Registration expiration */
	int expiry;
	int capability;			/* Codec capability */
	int rtptimeout;			/* RTP timeout */
	int rtpholdtimeout;		/* RTP Hold Timeout */
	int rtpkeepalive;		/* Send RTP packets for keepalive */
	ast_group_t callgroup;		/* Call group */
	ast_group_t pickupgroup;	/* Pickup group */
	struct sockaddr_in addr;	/* IP address of peer */
	struct in_addr mask;

	/* Qualification */
	struct sip_pvt *call;		/* Call pointer */
	int pokeexpire;			/* When to expire poke (qualify= checking) */
	int lastms;			/* How long last response took (in ms), or -1 for no response */
	int maxms;			/* Max ms we will accept for the host to be up, 0 to not monitor */
	struct timeval ps;		/* Ping send time */
	
	struct sockaddr_in defaddr;	/* Default IP address, used until registration */
	struct ast_ha *ha;		/* Access control list */
	struct ast_variable *chanvars;	/* Variables to set for channel created by user */
	int lastmsg;
};

AST_MUTEX_DEFINE_STATIC(sip_reload_lock);
static int sip_reloading = 0;

/* States for outbound registrations (with register= lines in sip.conf */
#define REG_STATE_UNREGISTERED		0
#define REG_STATE_REGSENT		1
#define REG_STATE_AUTHSENT		2
#define REG_STATE_REGISTERED   		3
#define REG_STATE_REJECTED	   	4
#define REG_STATE_TIMEOUT	   	5
#define REG_STATE_NOAUTH	   	6


/* sip_registry: Registrations with other SIP proxies */
struct sip_registry {
	ASTOBJ_COMPONENTS_FULL(struct sip_registry,1,1);
	int portno;			/* Optional port override */
	char username[80];		/* Who we are registering as */
	char authuser[80];		/* Who we *authenticate* as */
	char hostname[80];		/* Domain or host we register to */
	char secret[80];		/* Password or key name in []'s */	
	char md5secret[80];
	char contact[80];		/* Contact extension */
	char random[80];
	int expire;			/* Sched ID of expiration */
	int timeout; 			/* sched id of sip_reg_timeout */
	int refresh;			/* How often to refresh */
	struct sip_pvt *call;		/* create a sip_pvt structure for each outbound "registration call" in progress */
	int regstate;			/* Registration state (see above) */
	int callid_valid;		/* 0 means we haven't chosen callid for this registry yet. */
	char callid[80];		/* Global CallID for this registry */
	unsigned int ocseq;		/* Sequence number we got to for REGISTERs for this registry */
	struct sockaddr_in us;		/* Who the server thinks we are */
 	
 					/* Saved headers */
 	char realm[256];		/* Authorization realm */
 	char nonce[256];		/* Authorization nonce */
 	char domain[256];		/* Authorization domain */
 	char opaque[256];		/* Opaque nonsense */
 	char qop[80];			/* Quality of Protection. */
 
 	char lastmsg[256];		/* Last Message sent/received */
};

/*--- The user list: Users and friends ---*/
static struct ast_user_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct sip_user);
} userl;

/*--- The peer list: Peers and Friends ---*/
static struct ast_peer_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct sip_peer);
} peerl;

/*--- The register list: Other SIP proxys we register with and call ---*/
static struct ast_register_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct sip_registry);
	int recheck;
} regl;


static int __sip_do_register(struct sip_registry *r);

static int sipsock  = -1;


static struct sockaddr_in bindaddr;
static struct sockaddr_in externip;
static char externhost[256] = "";
static time_t externexpire = 0;
static int externrefresh = 10;
static struct ast_ha *localaddr;

/* The list of manual NOTIFY types we know how to send */
struct ast_config *notify_types;

static struct ast_frame  *sip_read(struct ast_channel *ast);
static int transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req);
static int transmit_response_with_sdp(struct sip_pvt *p, char *msg, struct sip_request *req, int retrans);
static int transmit_response_with_auth(struct sip_pvt *p, char *msg, struct sip_request *req, char *rand, int reliable, char *header);
static int transmit_request(struct sip_pvt *p, char *msg, int inc, int reliable, int newbranch);
static int transmit_request_with_auth(struct sip_pvt *p, char *msg, int inc, int reliable, int newbranch);
static int transmit_invite(struct sip_pvt *p, char *msg, int sendsdp, char *auth, char *authheader, char *vxml_url, char *distinctive_ring, char *osptoken, int addsipheaders, int init);
static int transmit_reinvite_with_sdp(struct sip_pvt *p);
static int transmit_info_with_digit(struct sip_pvt *p, char digit);
static int transmit_message_with_text(struct sip_pvt *p, char *text);
static int transmit_refer(struct sip_pvt *p, char *dest);
static struct sip_peer *temp_peer(char *name);
static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader, char *msg, int init);
static void free_old_route(struct sip_route *route);
static int build_reply_digest(struct sip_pvt *p, char *orig_header, char *digest, int digest_len);
static int update_user_counter(struct sip_pvt *fup, int event);
static void prune_peers(void);
static struct sip_peer *build_peer(const char *name, struct ast_variable *v, int realtime);
static struct sip_user *build_user(const char *name, struct ast_variable *v, int realtime);
static int sip_do_reload(void);
static int expire_register(void *data);
static int callevents = 0;

/*--- sip_debug_test_addr: See if we pass debug IP filter */
static inline int sip_debug_test_addr(struct sockaddr_in *addr) 
{
	if (sipdebug == 0)
		return 0;
	if (debugaddr.sin_addr.s_addr) {
		if (((ntohs(debugaddr.sin_port) != 0)
			&& (debugaddr.sin_port != addr->sin_port))
			|| (debugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
			return 0;
	}
	return 1;
}

static inline int sip_debug_test_pvt(struct sip_pvt *p) 
{
	if (sipdebug == 0)
		return 0;
	return sip_debug_test_addr(((ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE) ? &p->recv : &p->sa));
}


/*--- __sip_xmit: Transmit SIP message ---*/
static int __sip_xmit(struct sip_pvt *p, char *data, int len)
{
	int res;
	char iabuf[INET_ADDRSTRLEN];
	if (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE)
	    res=sendto(sipsock, data, len, 0, (struct sockaddr *)&p->recv, sizeof(struct sockaddr_in));
	else
	    res=sendto(sipsock, data, len, 0, (struct sockaddr *)&p->sa, sizeof(struct sockaddr_in));
	if (res != len) {
		ast_log(LOG_WARNING, "sip_xmit of %p (len %d) to %s returned %d: %s\n", data, len, ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr), res, strerror(errno));
	}
	return res;
}

static void sip_destroy(struct sip_pvt *p);

/*--- build_via: Build a Via header for a request ---*/
static void build_via(struct sip_pvt *p, char *buf, int len)
{
	char iabuf[INET_ADDRSTRLEN];

	/* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
	if (ast_test_flag(p, SIP_NAT) != SIP_NAT_NEVER)
		snprintf(buf, len, "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x;rport", ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip), ourport, p->branch);
	else /* Work around buggy UNIDEN UIP200 firmware */
		snprintf(buf, len, "SIP/2.0/UDP %s:%d;branch=z9hG4bK%08x", ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip), ourport, p->branch);
}

/*--- ast_sip_ouraddrfor: NAT fix - decide which IP address to use for ASterisk server? ---*/
/* Only used for outbound registrations */
static int ast_sip_ouraddrfor(struct in_addr *them, struct in_addr *us)
{
	/*
	 * Using the localaddr structure built up with localnet statements
	 * apply it to their address to see if we need to substitute our
	 * externip or can get away with our internal bindaddr
	 */
	struct sockaddr_in theirs;
	theirs.sin_addr = *them;
	if (localaddr && externip.sin_addr.s_addr &&
	   ast_apply_ha(localaddr, &theirs)) {
		char iabuf[INET_ADDRSTRLEN];
		if (externexpire && (time(NULL) >= externexpire)) {
			struct ast_hostent ahp;
			struct hostent *hp;
			time(&externexpire);
			externexpire += externrefresh;
			if ((hp = ast_gethostbyname(externhost, &ahp))) {
				memcpy(&externip.sin_addr, hp->h_addr, sizeof(externip.sin_addr));
			} else
				ast_log(LOG_NOTICE, "Warning: Re-lookup of '%s' failed!\n", externhost);
		}
		memcpy(us, &externip.sin_addr, sizeof(struct in_addr));
		ast_inet_ntoa(iabuf, sizeof(iabuf), *(struct in_addr *)&them->s_addr);
		ast_log(LOG_DEBUG, "Target address %s is not local, substituting externip\n", iabuf);
	}
	else if (bindaddr.sin_addr.s_addr)
		memcpy(us, &bindaddr.sin_addr, sizeof(struct in_addr));
	else
		return ast_ouraddrfor(them, us);
	return 0;
}

static int append_history(struct sip_pvt *p, char *event, char *data)
{
	struct sip_history *hist, *prev;
	char *c;
	if (!recordhistory)
		return 0;
	hist = malloc(sizeof(struct sip_history));
	if (hist) {
		memset(hist, 0, sizeof(struct sip_history));
		snprintf(hist->event, sizeof(hist->event), "%-15s %s", event, data);
		/* Trim up nicely */
		c = hist->event;
		while(*c) {
			if ((*c == '\r') || (*c == '\n')) {
				*c = '\0';
				break;
			}
			c++;
		}
		/* Enqueue into history */
		prev = p->history;
		if (prev) {
			while(prev->next)
				prev = prev->next;
			prev->next = hist;
		} else {
			p->history = hist;
		}
	}
	return 0;
}

/*--- retrans_pkt: Retransmit SIP message if no answer ---*/
static int retrans_pkt(void *data)
{
	struct sip_pkt *pkt=data, *prev, *cur;
	int res = 0;
	char iabuf[INET_ADDRSTRLEN];
	ast_mutex_lock(&pkt->owner->lock);
	if (pkt->retrans < MAX_RETRANS) {
		pkt->retrans++;
		if (sip_debug_test_pvt(pkt->owner)) {
			if (ast_test_flag(pkt->owner, SIP_NAT) & SIP_NAT_ROUTE)
				ast_verbose("Retransmitting #%d (NAT):\n%s\n to %s:%d\n", pkt->retrans, pkt->data, ast_inet_ntoa(iabuf, sizeof(iabuf), pkt->owner->recv.sin_addr), ntohs(pkt->owner->recv.sin_port));
			else
				ast_verbose("Retransmitting #%d (no NAT):\n%s\n to %s:%d\n", pkt->retrans, pkt->data, ast_inet_ntoa(iabuf, sizeof(iabuf), pkt->owner->sa.sin_addr), ntohs(pkt->owner->sa.sin_port));
		}
		append_history(pkt->owner, "ReTx", pkt->data);
		__sip_xmit(pkt->owner, pkt->data, pkt->packetlen);
		res = 1;
	} else {
		ast_log(LOG_WARNING, "Maximum retries exceeded on call %s for seqno %d (%s %s)\n", pkt->owner->callid, pkt->seqno, (ast_test_flag(pkt, FLAG_FATAL)) ? "Critical" : "Non-critical", (ast_test_flag(pkt, FLAG_RESPONSE)) ? "Response" : "Request");
		append_history(pkt->owner, "MaxRetries", (ast_test_flag(pkt, FLAG_FATAL)) ? "(Critical)" : "(Non-critical)");
		pkt->retransid = -1;
		if (ast_test_flag(pkt, FLAG_FATAL)) {
			while(pkt->owner->owner && ast_mutex_trylock(&pkt->owner->owner->lock)) {
				ast_mutex_unlock(&pkt->owner->lock);
				usleep(1);
				ast_mutex_lock(&pkt->owner->lock);
			}
			if (pkt->owner->owner) {
				ast_set_flag(pkt->owner, SIP_ALREADYGONE);
				ast_queue_hangup(pkt->owner->owner);
				ast_mutex_unlock(&pkt->owner->owner->lock);
			} else {
				/* If no owner, destroy now */
				ast_set_flag(pkt->owner, SIP_NEEDDESTROY);	
			}
		}
		/* In any case, go ahead and remove the packet */
		prev = NULL;
		cur = pkt->owner->packets;
		while(cur) {
			if (cur == pkt)
				break;
			prev = cur;
			cur = cur->next;
		}
		if (cur) {
			if (prev)
				prev->next = cur->next;
			else
				pkt->owner->packets = cur->next;
			ast_mutex_unlock(&pkt->owner->lock);
			free(cur);
			pkt = NULL;
		} else
			ast_log(LOG_WARNING, "Weird, couldn't find packet owner!\n");
	}
	if (pkt)
		ast_mutex_unlock(&pkt->owner->lock);
	return res;
}

/*--- __sip_reliable_xmit: transmit packet with retransmits ---*/
static int __sip_reliable_xmit(struct sip_pvt *p, int seqno, int resp, char *data, int len, int fatal)
{
	struct sip_pkt *pkt;
	pkt = malloc(sizeof(struct sip_pkt) + len + 1);
	if (!pkt)
		return -1;
	memset(pkt, 0, sizeof(struct sip_pkt));
	memcpy(pkt->data, data, len);
	pkt->packetlen = len;
	pkt->next = p->packets;
	pkt->owner = p;
	pkt->seqno = seqno;
	pkt->flags = resp;
	pkt->data[len] = '\0';
	if (fatal)
		ast_set_flag(pkt, FLAG_FATAL);
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

/*--- __sip_autodestruct: Kill a call (called by scheduler) ---*/
static int __sip_autodestruct(void *data)
{
	struct sip_pvt *p = data;
	p->autokillid = -1;
	ast_log(LOG_DEBUG, "Auto destroying call '%s'\n", p->callid);
	append_history(p, "AutoDestroy", "");
	if (p->owner) {
		ast_log(LOG_WARNING, "Autodestruct on call '%s' with owner in place\n", p->callid);
		ast_queue_hangup(p->owner);
	} else {
		sip_destroy(p);
	}
	return 0;
}

/*--- sip_scheddestroy: Schedule destruction of SIP call ---*/
static int sip_scheddestroy(struct sip_pvt *p, int ms)
{
	char tmp[80];
	if (sip_debug_test_pvt(p))
		ast_verbose("Scheduling destruction of call '%s' in %d ms\n", p->callid, ms);
	if (recordhistory) {
		snprintf(tmp, sizeof(tmp), "%d ms", ms);
		append_history(p, "SchedDestroy", tmp);
	}
	if (p->autokillid > -1)
		ast_sched_del(sched, p->autokillid);
	p->autokillid = ast_sched_add(sched, ms, __sip_autodestruct, p);
	return 0;
}

/*--- sip_cancel_destroy: Cancel destruction of SIP call ---*/
static int sip_cancel_destroy(struct sip_pvt *p)
{
	if (p->autokillid > -1)
		ast_sched_del(sched, p->autokillid);
	append_history(p, "CancelDestroy", "");
	p->autokillid = -1;
	return 0;
}

/*--- __sip_ack: Acknowledges receipt of a packet and stops retransmission ---*/
static int __sip_ack(struct sip_pvt *p, int seqno, int resp, const char *msg)
{
	struct sip_pkt *cur, *prev = NULL;
	int res = -1;
	int resetinvite = 0;
	/* Just in case... */
	if (!msg) msg = "___NEVER___";
	cur = p->packets;
	while(cur) {
		if ((cur->seqno == seqno) && ((ast_test_flag(cur, FLAG_RESPONSE)) == resp) &&
			((ast_test_flag(cur, FLAG_RESPONSE)) || 
			 (!strncasecmp(msg, cur->data, strlen(msg)) && (cur->data[strlen(msg)] < 33)))) {
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

/* Pretend to ack all packets */
static int __sip_pretend_ack(struct sip_pvt *p)
{
	while(p->packets) {
		__sip_ack(p, p->packets->seqno, (ast_test_flag(p->packets, FLAG_RESPONSE)), p->packets->data);
	}
	return 0;
}

/*--- __sip_semi_ack: Acks receipt of packet, keep it around (used for provisional responses) ---*/
static int __sip_semi_ack(struct sip_pvt *p, int seqno, int resp, const char *msg)
{
	struct sip_pkt *cur;
	int res = -1;
	cur = p->packets;
	while(cur) {
		if ((cur->seqno == seqno) && ((ast_test_flag(cur, FLAG_RESPONSE)) == resp) &&
			((ast_test_flag(cur, FLAG_RESPONSE)) || 
			 (!strncasecmp(msg, cur->data, strlen(msg)) && (cur->data[strlen(msg)] < 33)))) {
			/* this is our baby */
			if (cur->retransid > -1)
				ast_sched_del(sched, cur->retransid);
			cur->retransid = -1;
			res = 0;
			break;
		}
		cur = cur->next;
	}
	ast_log(LOG_DEBUG, "(Provisional) Stopping retransmission (but retaining packet) on '%s' %s %d: %s\n", p->callid, resp ? "Response" : "Request", seqno, res ? "Not Found" : "Found");
	return res;
}

static void parse(struct sip_request *req);
static char *get_header(struct sip_request *req, char *name);
static void copy_request(struct sip_request *dst,struct sip_request *src);

static void parse_copy(struct sip_request *dst, struct sip_request *src)
{
	memset(dst, 0, sizeof(*dst));
	memcpy(dst->data, src->data, sizeof(dst->data));
	dst->len = src->len;
	parse(dst);
}
/*--- send_response: Transmit response on SIP request---*/
static int send_response(struct sip_pvt *p, struct sip_request *req, int reliable, int seqno)
{
	int res;
	char iabuf[INET_ADDRSTRLEN];
	struct sip_request tmp;
	char tmpmsg[80];
	if (sip_debug_test_pvt(p)) {
		if (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE)
			ast_verbose("%sTransmitting (NAT):\n%s\n to %s:%d\n", reliable ? "Reliably " : "", req->data, ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr), ntohs(p->recv.sin_port));
		else
			ast_verbose("%sTransmitting (no NAT):\n%s\n to %s:%d\n", reliable ? "Reliably " : "", req->data, ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr), ntohs(p->sa.sin_port));
	}
	if (reliable) {
		if (recordhistory) {
			parse_copy(&tmp, req);
			snprintf(tmpmsg, sizeof(tmpmsg), "%s / %s", tmp.data, get_header(&tmp, "CSeq"));
			append_history(p, "TxRespRel", tmpmsg);
		}
		res = __sip_reliable_xmit(p, seqno, 1, req->data, req->len, (reliable > 1));
	} else {
		if (recordhistory) {
			parse_copy(&tmp, req);
			snprintf(tmpmsg, sizeof(tmpmsg), "%s / %s", tmp.data, get_header(&tmp, "CSeq"));
			append_history(p, "TxResp", tmpmsg);
		}
		res = __sip_xmit(p, req->data, req->len);
	}
	if (res > 0)
		res = 0;
	return res;
}

/*--- send_request: Send SIP Request to the other part of the dialogue ---*/
static int send_request(struct sip_pvt *p, struct sip_request *req, int reliable, int seqno)
{
	int res;
	char iabuf[INET_ADDRSTRLEN];
	struct sip_request tmp;
	char tmpmsg[80];
	if (sip_debug_test_pvt(p)) {
		if (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE)
			ast_verbose("%sTransmitting:\n%s (NAT) to %s:%d\n", reliable ? "Reliably " : "", req->data, ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr), ntohs(p->recv.sin_port));
		else
			ast_verbose("%sTransmitting:\n%s (no NAT) to %s:%d\n", reliable ? "Reliably " : "", req->data, ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr), ntohs(p->sa.sin_port));
	}
	if (reliable) {
		if (recordhistory) {
			parse_copy(&tmp, req);
			snprintf(tmpmsg, sizeof(tmpmsg), "%s / %s", tmp.data, get_header(&tmp, "CSeq"));
			append_history(p, "TxReqRel", tmpmsg);
		}
		res = __sip_reliable_xmit(p, seqno, 0, req->data, req->len, (reliable > 1));
	} else {
		if (recordhistory) {
			parse_copy(&tmp, req);
			snprintf(tmpmsg, sizeof(tmpmsg), "%s / %s", tmp.data, get_header(&tmp, "CSeq"));
			append_history(p, "TxReq", tmpmsg);
		}
		res = __sip_xmit(p, req->data, req->len);
	}
	return res;
}

/*--- url_decode: Decode SIP URL  ---*/
static void url_decode(char *s) 
{
	char *o = s;
	unsigned int tmp;
	while(*s) {
		switch(*s) {
		case '%':
			if (strlen(s) > 2) {
				if (sscanf(s + 1, "%2x", &tmp) == 1) {
					*o = tmp;
					s += 2;	/* Will be incremented once more when we break out */
					break;
				}
			}
			/* Fall through if something wasn't right with the formatting */
		default:
			*o = *s;
		}
		s++;
		o++;
	}
	*o = '\0';
}

/*--- ditch_braces: Pick out text in braces from character string  ---*/
static char *ditch_braces(char *tmp)
{
	char *c = tmp;
	char *n;
	char *q;
	if ((q = strchr(tmp, '"')) ) {
		c = q + 1;
		if ((q = strchr(c, '"')) )
			c = q + 1;
		else {
			ast_log(LOG_WARNING, "No closing quote in '%s'\n", tmp);
			c = tmp;
		}
	}
	if ((n = strchr(c, '<')) ) {
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

/*--- sip_sendtext: Send SIP MESSAGE text within a call ---*/
/*      Called from PBX core text message functions */
static int sip_sendtext(struct ast_channel *ast, char *text)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int debug=sip_debug_test_pvt(p);

	if (debug)
		ast_verbose("Sending text %s on %s\n", text, ast->name);
	if (!p)
		return -1;
	if (!text || ast_strlen_zero(text))
		return 0;
	if (debug)
		ast_verbose("Really sending text %s on %s\n", text, ast->name);
	transmit_message_with_text(p, text);
	return 0;	
}

/*--- realtime_update_peer: Update peer object in realtime storage ---*/
static void realtime_update_peer(const char *peername, struct sockaddr_in *sin, const char *username, int expirey)
{
	char port[10];
	char ipaddr[20];
	char regseconds[20];
	time_t nowtime;
	
	time(&nowtime);
	nowtime += expirey;
	snprintf(regseconds, sizeof(regseconds), "%ld", nowtime);
	ast_inet_ntoa(ipaddr, sizeof(ipaddr), sin->sin_addr);
	snprintf(port, sizeof(port), "%d", ntohs(sin->sin_port));
	ast_update_realtime("sippeers", "name", peername, "ipaddr", ipaddr, "port", port, "regseconds", regseconds, "username", username, NULL);
}

/*--- register_peer_exten: Automatically add peer extension to dial plan ---*/
static void register_peer_exten(struct sip_peer *peer, int onoff)
{
	unsigned char multi[256]="";
	char *stringp, *ext;
	if (!ast_strlen_zero(regcontext)) {
		strncpy(multi, ast_strlen_zero(peer->regexten) ? peer->name : peer->regexten, sizeof(multi) - 1);
		stringp = multi;
		while((ext = strsep(&stringp, "&"))) {
			if (onoff)
				ast_add_extension(regcontext, 1, ext, 1, NULL, NULL, "Noop", strdup(peer->name), free, channeltype);
			else
				ast_context_remove_extension(regcontext, ext, 1, NULL);
		}
	}
}

/*--- sip_destroy_peer: Destroy peer object from memory */
static void sip_destroy_peer(struct sip_peer *peer)
{
	/* Delete it, it needs to disappear */
	if (peer->call)
		sip_destroy(peer->call);
	if(peer->chanvars) {
		ast_variables_destroy(peer->chanvars);
		peer->chanvars = NULL;
	}
	if (peer->expire > -1)
		ast_sched_del(sched, peer->expire);
	if (peer->pokeexpire > -1)
		ast_sched_del(sched, peer->pokeexpire);
	register_peer_exten(peer, 0);
	ast_free_ha(peer->ha);
	if (ast_test_flag(peer, SIP_SELFDESTRUCT))
		apeerobjs--;
	else if (ast_test_flag(peer, SIP_REALTIME))
		rpeerobjs--;
	else
		speerobjs--;
	free(peer);
}

/*--- update_peer: Update peer data in database (if used) ---*/
static void update_peer(struct sip_peer *p, int expiry)
{
	if (!ast_test_flag((&global_flags_page2), SIP_PAGE2_RTNOUPDATE) && 
		(ast_test_flag(p, SIP_REALTIME) || 
		 ast_test_flag(&(p->flags_page2), SIP_PAGE2_RTCACHEFRIENDS)))
		realtime_update_peer(p->name, &p->addr, p->username, expiry);
}


/*--- realtime_peer: Get peer from realtime storage ---*/
/* Checks the "sippeers" realtime family from extconfig.conf */
static struct sip_peer *realtime_peer(const char *peername, struct sockaddr_in *sin)
{
	struct sip_peer *peer=NULL;
	struct ast_variable *var;
	struct ast_variable *tmp;

	/* First check on peer name */
	if (peername) 
		var = ast_load_realtime("sippeers", "name", peername, NULL);
	else if (sin) {	/* Then check on IP address */
		char iabuf[80];

		ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr);
		var = ast_load_realtime("sippeers", "ipaddr", iabuf, NULL);
	} else
		return NULL;

	if (!var)
		return NULL;

	tmp = var;
	/* If this is type=user, then skip this object. */
	while(tmp) {
		if (!strcasecmp(tmp->name, "type") &&
		    !strcasecmp(tmp->value, "user")) {
			ast_variables_destroy(var);
			return NULL;
		}
		tmp = tmp->next;
	}

	peer = build_peer(peername, var, ast_test_flag((&global_flags_page2), SIP_PAGE2_RTCACHEFRIENDS) ? 0 : 1);

	if (peer) {
		if(ast_test_flag((&global_flags_page2), SIP_PAGE2_RTCACHEFRIENDS)) {
			ast_copy_flags((&peer->flags_page2),(&global_flags_page2), SIP_PAGE2_RTAUTOCLEAR|SIP_PAGE2_RTCACHEFRIENDS);
			if(ast_test_flag((&global_flags_page2), SIP_PAGE2_RTAUTOCLEAR)) {
				peer->expire = ast_sched_add(sched, (global_rtautoclear) * 1000, expire_register, (void *)peer);
			}
			ASTOBJ_CONTAINER_LINK(&peerl,peer);
		} else {
			ast_set_flag(peer, SIP_REALTIME);
		}
	}
	ast_variables_destroy(var);
	return peer;
}

/*--- sip_addrcmp: Support routine for find_peer ---*/
static int sip_addrcmp(char *name, struct sockaddr_in *sin)
{
	/* We know name is the first field, so we can cast */
	struct sip_peer *p = (struct sip_peer *)name;
	return 	!(!inaddrcmp(&p->addr, sin) || 
					(ast_test_flag(p, SIP_INSECURE) &&
					(p->addr.sin_addr.s_addr == sin->sin_addr.s_addr)));
}

/*--- find_peer: Locate peer by name or ip address */
static struct sip_peer *find_peer(const char *peer, struct sockaddr_in *sin, int realtime)
{
	struct sip_peer *p = NULL;

	if (peer)
		p = ASTOBJ_CONTAINER_FIND(&peerl,peer);
	else
		p = ASTOBJ_CONTAINER_FIND_FULL(&peerl,sin,name,sip_addr_hashfunc,1,sip_addrcmp);

	if (!p && realtime) {
		p = realtime_peer(peer, sin);
	}

	return(p);
}

/*--- sip_destroy_user: Remove user object from in-memory storage ---*/
static void sip_destroy_user(struct sip_user *user)
{
	ast_free_ha(user->ha);
	if(user->chanvars) {
		ast_variables_destroy(user->chanvars);
		user->chanvars = NULL;
	}
	if (ast_test_flag(user, SIP_REALTIME))
		ruserobjs--;
	else
		suserobjs--;
	free(user);
}

/*--- realtime_user: Load user from realtime storage ---*/
/* Loads user from "sipusers" category in realtime (extconfig.conf) */
/* Users are matched on From: user name (the domain in skipped) */
static struct sip_user *realtime_user(const char *username)
{
	struct ast_variable *var;
	struct ast_variable *tmp;
	struct sip_user *user = NULL;

	var = ast_load_realtime("sipusers", "name", username, NULL);

	if (!var)
		return NULL;

	tmp = var;
	while (tmp) {
		if (!strcasecmp(tmp->name, "type") &&
			!strcasecmp(tmp->value, "peer")) {
			ast_variables_destroy(var);
			return NULL;
		}
		tmp = tmp->next;
	}
	


	user = build_user(username, var, !ast_test_flag((&global_flags_page2), SIP_PAGE2_RTCACHEFRIENDS));
	
	if (user) {
		/* Add some finishing touches, addresses, etc */
		if(ast_test_flag((&global_flags_page2), SIP_PAGE2_RTCACHEFRIENDS)) {
			suserobjs++;

            ASTOBJ_CONTAINER_LINK(&userl,user);
        } else {
			/* Move counter from s to r... */
			suserobjs--;
			ruserobjs++;
            ast_set_flag(user, SIP_REALTIME);
        }
	}
	ast_variables_destroy(var);
	return user;
}

/*--- find_user: Locate user by name ---*/
/* Locates user by name (From: sip uri user name part) first
   from in-memory list (static configuration) then from 
   realtime storage (defined in extconfig.conf) */
static struct sip_user *find_user(const char *name, int realtime)
{
	struct sip_user *u = NULL;
	u = ASTOBJ_CONTAINER_FIND(&userl,name);
	if (!u && realtime) {
		u = realtime_user(name);
	}
	return(u);
}

/*--- create_addr: create address structure from peer definition ---*/
/*      Or, if peer not found, find it in the global DNS */
/*      returns TRUE on failure, FALSE on success */
static int create_addr(struct sip_pvt *r, char *opeer)
{
	struct hostent *hp;
	struct ast_hostent ahp;
	struct sip_peer *p;
	int found=0;
	char *port;
	char *callhost;
	int portno;
	char host[256], *hostn;
	char peer[256]="";

	strncpy(peer, opeer, sizeof(peer) - 1);
	port = strchr(peer, ':');
	if (port) {
		*port = '\0';
		port++;
	}
	r->sa.sin_family = AF_INET;
	p = find_peer(peer, NULL, 1);

	if (p) {
		found++;
		ast_copy_flags(r, p, SIP_PROMISCREDIR | SIP_USEREQPHONE | SIP_DTMF | SIP_NAT | SIP_REINVITE | SIP_INSECURE);
		r->capability = p->capability;
		if (r->rtp) {
			ast_log(LOG_DEBUG, "Setting NAT on RTP to %d\n", (ast_test_flag(r, SIP_NAT) & SIP_NAT_ROUTE));
			ast_rtp_setnat(r->rtp, (ast_test_flag(r, SIP_NAT) & SIP_NAT_ROUTE));
		}
		if (r->vrtp) {
			ast_log(LOG_DEBUG, "Setting NAT on VRTP to %d\n", (ast_test_flag(r, SIP_NAT) & SIP_NAT_ROUTE));
			ast_rtp_setnat(r->vrtp, (ast_test_flag(r, SIP_NAT) & SIP_NAT_ROUTE));
		}
		strncpy(r->peername, p->username, sizeof(r->peername)-1);
		strncpy(r->authname, p->username, sizeof(r->authname)-1);
		strncpy(r->username, p->username, sizeof(r->username)-1);
		strncpy(r->peersecret, p->secret, sizeof(r->peersecret)-1);
		strncpy(r->peermd5secret, p->md5secret, sizeof(r->peermd5secret)-1);
		strncpy(r->tohost, p->tohost, sizeof(r->tohost)-1);
		strncpy(r->fullcontact, p->fullcontact, sizeof(r->fullcontact)-1);
		if (!r->initreq.headers && !ast_strlen_zero(p->fromdomain)) {
			if ((callhost = strchr(r->callid, '@'))) {
				strncpy(callhost + 1, p->fromdomain, sizeof(r->callid) - (callhost - r->callid) - 2);
			}
		}
		if (ast_strlen_zero(r->tohost)) {
			if (p->addr.sin_addr.s_addr)
				ast_inet_ntoa(r->tohost, sizeof(r->tohost), p->addr.sin_addr);
			else
				ast_inet_ntoa(r->tohost, sizeof(r->tohost), p->defaddr.sin_addr);
		}
		if (!ast_strlen_zero(p->fromdomain))
			strncpy(r->fromdomain, p->fromdomain, sizeof(r->fromdomain)-1);
		if (!ast_strlen_zero(p->fromuser))
			strncpy(r->fromuser, p->fromuser, sizeof(r->fromuser)-1);
		r->maxtime = p->maxms;
		r->callgroup = p->callgroup;
		r->pickupgroup = p->pickupgroup;
		if (ast_test_flag(r, SIP_DTMF) == SIP_DTMF_RFC2833)
			r->noncodeccapability |= AST_RTP_DTMF;
		else
			r->noncodeccapability &= ~AST_RTP_DTMF;
		strncpy(r->context, p->context,sizeof(r->context)-1);
		if ((p->addr.sin_addr.s_addr || p->defaddr.sin_addr.s_addr) &&
		    (!p->maxms || ((p->lastms >= 0)  && (p->lastms <= p->maxms)))) {
			if (p->addr.sin_addr.s_addr) {
				r->sa.sin_addr = p->addr.sin_addr;
				r->sa.sin_port = p->addr.sin_port;
			} else {
				r->sa.sin_addr = p->defaddr.sin_addr;
				r->sa.sin_port = p->defaddr.sin_port;
			}
			memcpy(&r->recv, &r->sa, sizeof(r->recv));
		} else {
			ASTOBJ_UNREF(p,sip_destroy_peer);
		}
	}
	if (!p && !found) {
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
		ASTOBJ_UNREF(p,sip_destroy_peer);
		return 0;
	}
}

/*--- auto_congest: Scheduled congestion on a call ---*/
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




/*--- sip_call: Initiate SIP call from PBX ---*/
/*      used from the dial() application      */
static int sip_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res;
	struct sip_pvt *p;
	char *vxml_url = NULL;
	char *distinctive_ring = NULL;
	char *osptoken = NULL;
#ifdef OSP_SUPPORT
	char *osphandle = NULL;
#endif	
	struct varshead *headp;
	struct ast_var_t *current;
	int addsipheaders = 0;
	
	p = ast->pvt->pvt;
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "sip_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* Check whether there is vxml_url, distinctive ring variables */

	headp=&ast->varshead;
	AST_LIST_TRAVERSE(headp,current,entries) {
		/* Check whether there is a VXML_URL variable */
		if (!vxml_url && !strcasecmp(ast_var_name(current),"VXML_URL")) {
			vxml_url = ast_var_value(current);
		} else if (!distinctive_ring && !strcasecmp(ast_var_name(current),"ALERT_INFO")) {
			/* Check whether there is a ALERT_INFO variable */
			distinctive_ring = ast_var_value(current);
		} else if (!addsipheaders && !strncasecmp(ast_var_name(current),"SIPADDHEADER",strlen("SIPADDHEADER"))) {
			/* Check whether there is a variable with a name starting with SIPADDHEADER */
			addsipheaders = 1;
		}

		
#ifdef OSP_SUPPORT
		  else if (!osptoken && !strcasecmp(ast_var_name(current), "OSPTOKEN")) {
			osptoken = ast_var_value(current);
		} else if (!osphandle && !strcasecmp(ast_var_name(current), "OSPHANDLE")) {
			osphandle = ast_var_value(current);
		}
#endif
	}
	
	res = 0;
	ast_set_flag(p, SIP_OUTGOING);
#ifdef OSP_SUPPORT
	if (!osptoken || !osphandle || (sscanf(osphandle, "%i", &p->osphandle) != 1)) {
		/* Force Disable OSP support */
		ast_log(LOG_DEBUG, "Disabling OSP support for this call. osptoken = %s, osphandle = %s\n", osptoken, osphandle);
		osptoken = NULL;
		osphandle = NULL;
		p->osphandle = -1;
	}
#endif
	ast_log(LOG_DEBUG, "Outgoing Call for %s\n", p->username);
	res = update_user_counter(p,INC_OUT_USE);
	if ( res != -1 ) {
		p->callingpres = ast->cid.cid_pres;
		p->jointcapability = p->capability;
		transmit_invite(p, "INVITE", 1, NULL, NULL, vxml_url,distinctive_ring, osptoken, addsipheaders, 1);
		if (p->maxtime) {
			/* Initialize auto-congest time */
			p->initid = ast_sched_add(sched, p->maxtime * 4, auto_congest, p);
		}
	}
	return res;
}

/*--- sip_registry_destroy: Destroy registry object ---*/
/* Objects created with the register= statement in static configuration */
static void sip_registry_destroy(struct sip_registry *reg)
{
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
	regobjs--;
	free(reg);
	
}

/*---  __sip_destroy: Execute destrucion of call structure, release memory---*/
static void __sip_destroy(struct sip_pvt *p, int lockowner)
{
	struct sip_pvt *cur, *prev = NULL;
	struct sip_pkt *cp;
	struct sip_history *hist;

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
		if (p->registry->call == p)
			p->registry->call = NULL;
		ASTOBJ_UNREF(p->registry,sip_registry_destroy);
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
	/* Clear history */
	while(p->history) {
		hist = p->history;
		p->history = p->history->next;
		free(hist);
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
		ast_mutex_destroy(&p->lock);
		if(p->chanvars) {
			ast_variables_destroy(p->chanvars);
			p->chanvars = NULL;
		}
		free(p);
	}
}

/*--- update_user_counter: Handle incominglimit and outgoinglimit for SIP users ---*/
/* Note: This is going to be replaced by app_groupcount */
/* Thought: For realtime, we should propably update storage with inuse counter... */
static int update_user_counter(struct sip_pvt *fup, int event)
{
	char name[256] = "";
	struct sip_user *u;
	struct sip_peer *p;
	int *inuse, *incominglimit;

	strncpy(name, fup->username, sizeof(name) - 1);

	/* Check the list of users */
	u = find_user(name, 1);
	if (u) {
		inuse = &u->inUse;
		incominglimit = &u->incominglimit;
		p = NULL;
	} else {
		/* Try to find peer */
		p = find_peer(fup->peername, NULL, 1);
		if (p) {
			inuse = &p->inUse;
			incominglimit = &p->incominglimit;
			strncpy(name, fup->peername, sizeof(name) -1);
		} else {
			ast_log(LOG_DEBUG, "%s is not a local user\n", name);
			return 0;
		}
	}
	switch(event) {
		/* incoming and outgoing affects the inUse counter */
		case DEC_OUT_USE:
		case DEC_IN_USE:
			if ( *inuse > 0 ) {
				(*inuse)--;
			} else {
				*inuse = 0;
			}
			break;
		case INC_IN_USE:
		case INC_OUT_USE:
			if (*incominglimit > 0 ) {
				if (*inuse >= *incominglimit) {
					ast_log(LOG_ERROR, "Call from %s '%s' rejected due to usage limit of %d\n", u?"user":"peer", name, *incominglimit);
					/* inc inUse as well */
					if ( event == INC_OUT_USE ) {
						(*inuse)++;
					}
					if (u)
						ASTOBJ_UNREF(u,sip_destroy_user);
					else
						ASTOBJ_UNREF(p,sip_destroy_peer);
					return -1; 
				}
			}
			u->inUse++;
			ast_log(LOG_DEBUG, "Call from %s '%s' is %d out of %d\n", u?"user":"peer", name, *inuse, *incominglimit);
			break;
#ifdef DISABLED_CODE
		/* we don't use these anymore */
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
					if (u->temponly) {
						destroy_user(u);
					}
					return -1;
				}
			}
			u->outUse++;
			break;
#endif
		default:
			ast_log(LOG_ERROR, "update_user_counter(%s,%d) called with no event!\n",name,event);
	}
	if (u)
		ASTOBJ_UNREF(u,sip_destroy_user);
	else
		ASTOBJ_UNREF(p,sip_destroy_peer);
	return 0;
}

/*--- sip_destroy: Destroy SIP call structure ---*/
static void sip_destroy(struct sip_pvt *p)
{
	ast_mutex_lock(&iflock);
	__sip_destroy(p, 1);
	ast_mutex_unlock(&iflock);
}


static int transmit_response_reliable(struct sip_pvt *p, char *msg, struct sip_request *req, int fatal);

/*--- hangup_sip2cause: Convert SIP hangup causes to Asterisk hangup causes ---*/
static int hangup_sip2cause(int cause)
{
/* Possible values from causes.h
        AST_CAUSE_NOTDEFINED    AST_CAUSE_NORMAL        AST_CAUSE_BUSY
        AST_CAUSE_FAILURE       AST_CAUSE_CONGESTION    AST_CAUSE_UNALLOCATED
*/

	switch(cause) {
		case 404:       /* Not found */
			return AST_CAUSE_UNALLOCATED;
		case 483:       /* Too many hops */
			return AST_CAUSE_FAILURE;
		case 486:
			return AST_CAUSE_BUSY;
		default:
			return AST_CAUSE_NORMAL;
	}
	/* Never reached */
	return 0;
}

/*--- hangup_cause2sip: Convert Asterisk hangup causes to SIP codes ---*/
static char *hangup_cause2sip(int cause)
{
	switch(cause)
	{
	   	case AST_CAUSE_FAILURE:
                        return "500 Server internal failure";
                case AST_CAUSE_CONGESTION:
                        return "503 Service Unavailable";
		case AST_CAUSE_BUSY:
			return "486 Busy";
		default:
			return NULL;
	}
	/* Never reached */
	return 0;
}

/*--- sip_hangup: Hangup SIP call ---*/
/* Part of PBX interface */
static int sip_hangup(struct ast_channel *ast)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int needcancel = 0;
	struct ast_flags locflags = {0};
	if (option_debug)
		ast_log(LOG_DEBUG, "sip_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_DEBUG, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_mutex_lock(&p->lock);
#ifdef OSP_SUPPORT
	if ((p->osphandle > -1) && (ast->_state == AST_STATE_UP)) {
		ast_osp_terminate(p->osphandle, AST_CAUSE_NORMAL, p->ospstart, time(NULL) - p->ospstart);
	}
#endif	
	if (ast_test_flag(p, SIP_OUTGOING)) {
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

	ast_set_flag(&locflags, SIP_NEEDDESTROY);	
	/* Start the process if it's not already started */
	if (!ast_test_flag(p, SIP_ALREADYGONE) && !ast_strlen_zero(p->initreq.data)) {
		if (needcancel) {
			if (ast_test_flag(p, SIP_OUTGOING)) {
				transmit_request_with_auth(p, "CANCEL", p->ocseq, 1, 0);
				/* Actually don't destroy us yet, wait for the 487 on our original 
				   INVITE, but do set an autodestruct just in case we never get it. */
				ast_clear_flag(&locflags, SIP_NEEDDESTROY);
				sip_scheddestroy(p, 15000);
				if ( p->initid != -1 ) {
					/* channel still up - reverse dec of inUse counter
					   only if the channel is not auto-congested */
					if (ast_test_flag(p, SIP_OUTGOING)) {
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
				ast_set_flag(p, SIP_PENDINGBYE);	
				ast_clear_flag(p, SIP_NEEDREINVITE);	
			}
		}
	}
	ast_copy_flags(p, (&locflags), SIP_NEEDDESTROY);	
	ast_mutex_unlock(&p->lock);
	return 0;
}

/*--- sip_answer: Answer SIP call , send 200 OK on Invite ---*/
/* Part of PBX interface */
static int sip_answer(struct ast_channel *ast)
{
	int res = 0,fmt;
	char *codec;
	struct sip_pvt *p = ast->pvt->pvt;

	ast_mutex_lock(&p->lock);
	if (ast->_state != AST_STATE_UP) {
#ifdef OSP_SUPPORT	
		time(&p->ospstart);
#endif
	
		codec=pbx_builtin_getvar_helper(p->owner,"SIP_CODEC");
		if (codec) {
			fmt=ast_getformatbyname(codec);
			if (fmt) {
				ast_log(LOG_NOTICE, "Changing codec to '%s' for this call because of ${SIP_CODEC) variable\n",codec);
				if (p->jointcapability & fmt) {
					p->jointcapability &= fmt;
					p->capability &= fmt;
				} else
					ast_log(LOG_NOTICE, "Ignoring ${SIP_CODEC} variable because it is not shared by both ends.\n");
			} else ast_log(LOG_NOTICE, "Ignoring ${SIP_CODEC} variable because of unrecognized/not configured codec (check allow/disallow in sip.conf): %s\n",codec);
		}

		ast_setstate(ast, AST_STATE_UP);
		if (option_debug)
			ast_log(LOG_DEBUG, "sip_answer(%s)\n", ast->name);
		res = transmit_response_with_sdp(p, "200 OK", &p->initreq, 1);
	}
	ast_mutex_unlock(&p->lock);
	return res;
}

/*--- sip_write: Send response, support audio media ---*/
static int sip_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int res = 0;
	if (frame->frametype == AST_FRAME_VOICE) {
		if (!(frame->subclass & ast->nativeformats)) {
			ast_log(LOG_WARNING, "Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
				frame->subclass, ast->nativeformats, ast->readformat, ast->writeformat);
			return 0;
		}
		if (p) {
			ast_mutex_lock(&p->lock);
			if (p->rtp) {
				if ((ast->_state != AST_STATE_UP) && !ast_test_flag(p, SIP_PROGRESS_SENT) && !ast_test_flag(p, SIP_OUTGOING)) {
					transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
					ast_set_flag(p, SIP_PROGRESS_SENT);	
				}
				time(&p->lastrtptx);
				res =  ast_rtp_write(p->rtp, frame);
			}
			ast_mutex_unlock(&p->lock);
		}
	} else if (frame->frametype == AST_FRAME_VIDEO) {
		if (p) {
			ast_mutex_lock(&p->lock);
			if (p->vrtp) {
				if ((ast->_state != AST_STATE_UP) && !ast_test_flag(p, SIP_PROGRESS_SENT) && !ast_test_flag(p, SIP_OUTGOING)) {
					transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
					ast_set_flag(p, SIP_PROGRESS_SENT);	
				}
				time(&p->lastrtptx);
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

/*--- sip_fixup: Fix up a channel:  If a channel is consumed, this is called.
        Basically update any ->owner links ----*/
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

/*--- sip_senddigit: Send DTMF character on SIP channel */
/*    within one call, we're able to transmit in many methods simultaneously */
static int sip_senddigit(struct ast_channel *ast, char digit)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int res = 0;
	ast_mutex_lock(&p->lock);
	switch (ast_test_flag(p, SIP_DTMF)) {
	case SIP_DTMF_INFO:
		transmit_info_with_digit(p, digit);
		break;
	case SIP_DTMF_RFC2833:
		if (p->rtp)
			ast_rtp_senddigit(p->rtp, digit);
		break;
	case SIP_DTMF_INBAND:
		res = -1;
		break;
	}
	ast_mutex_unlock(&p->lock);
	return res;
}


/*--- sip_transfer: Transfer SIP call */
static int sip_transfer(struct ast_channel *ast, char *dest)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int res;

	ast_mutex_lock(&p->lock);
	res = transmit_refer(p, dest);
	ast_mutex_unlock(&p->lock);
	return res;
}

/*--- sip_indicate: Play indication to user */
/* With SIP a lot of indications is sent as messages, letting the device play
   the indication - busy signal, congestion etc */
static int sip_indicate(struct ast_channel *ast, int condition)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int res = 0;

	ast_mutex_lock(&p->lock);
	switch(condition) {
	case AST_CONTROL_RINGING:
		if (ast->_state == AST_STATE_RING) {
			if (!ast_test_flag(p, SIP_PROGRESS_SENT) ||
			    (ast_test_flag(p, SIP_PROG_INBAND) == SIP_PROG_INBAND_NEVER)) {
				/* Send 180 ringing if out-of-band seems reasonable */
				transmit_response(p, "180 Ringing", &p->initreq);
				ast_set_flag(p, SIP_RINGING);
				if (ast_test_flag(p, SIP_PROG_INBAND) != SIP_PROG_INBAND_YES)
					break;
			} else {
				/* Well, if it's not reasonable, just send in-band */
			}
		}
		res = -1;
		break;
	case AST_CONTROL_BUSY:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "486 Busy Here", &p->initreq);
			ast_set_flag(p, SIP_ALREADYGONE);	
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
			break;
		}
		res = -1;
		break;
	case AST_CONTROL_CONGESTION:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "503 Service Unavailable", &p->initreq);
			ast_set_flag(p, SIP_ALREADYGONE);	
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
			break;
		}
		res = -1;
		break;
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
		if ((ast->_state != AST_STATE_UP) && !ast_test_flag(p, SIP_PROGRESS_SENT) && !ast_test_flag(p, SIP_OUTGOING)) {
			transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
			ast_set_flag(p, SIP_PROGRESS_SENT);	
			break;
		}
		res = -1;
		break;
	case -1:
		res = -1;
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", condition);
		res = -1;
		break;
	}
	ast_mutex_unlock(&p->lock);
	return res;
}



/*--- sip_new: Initiate a call in the SIP channel */
/*      called from sip_request_call (calls from the pbx ) */
static struct ast_channel *sip_new(struct sip_pvt *i, int state, char *title)
{
	struct ast_channel *tmp;
	struct ast_variable *v = NULL;
	int fmt;
	
	ast_mutex_unlock(&i->lock);
	/* Don't hold a sip pvt lock while we allocate a channel */
	tmp = ast_channel_alloc(1);
	ast_mutex_lock(&i->lock);
	if (tmp) {
		/* Select our native format based on codec preference until we receive
		   something from another device to the contrary. */
		ast_mutex_lock(&i->lock);
		if (i->jointcapability)
			tmp->nativeformats = ast_codec_choose(&i->prefs, i->jointcapability, 1);
		else if (i->capability)
			tmp->nativeformats = ast_codec_choose(&i->prefs, i->capability, 1);
		else
			tmp->nativeformats = ast_codec_choose(&i->prefs, global_capability, 1);
		ast_mutex_unlock(&i->lock);
		fmt = ast_best_codec(tmp->nativeformats);
		if (title)
			snprintf(tmp->name, sizeof(tmp->name), "SIP/%s-%04x", title, rand() & 0xffff);
		else
			if (strchr(i->fromdomain,':'))
			{
				snprintf(tmp->name, sizeof(tmp->name), "SIP/%s-%08x", strchr(i->fromdomain,':')+1, (int)(long)(i));
			}
			else
			{
				snprintf(tmp->name, sizeof(tmp->name), "SIP/%s-%08x", i->fromdomain, (int)(long)(i));
			}
		tmp->type = channeltype;
                if (ast_test_flag(i, SIP_DTMF) ==  SIP_DTMF_INBAND) {
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
		tmp->cid.cid_pres = i->callingpres;
		if (!ast_strlen_zero(i->accountcode))
			strncpy(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode)-1);
		if (i->amaflags)
			tmp->amaflags = i->amaflags;
		if (!ast_strlen_zero(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		if (!ast_strlen_zero(i->musicclass))
			strncpy(tmp->musicclass, i->musicclass, sizeof(tmp->musicclass)-1);
		i->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, i->exten, sizeof(tmp->exten)-1);
		if (!ast_strlen_zero(i->cid_num)) 
			tmp->cid.cid_num = strdup(i->cid_num);
		if (!ast_strlen_zero(i->cid_name))
			tmp->cid.cid_name = strdup(i->cid_name);
		if (!ast_strlen_zero(i->rdnis))
			tmp->cid.cid_rdnis = strdup(i->rdnis);
		if (!ast_strlen_zero(i->exten) && strcmp(i->exten, "s"))
			tmp->cid.cid_dnid = strdup(i->exten);
		tmp->priority = 1;
		if (!ast_strlen_zero(i->uri)) {
			pbx_builtin_setvar_helper(tmp, "SIPURI", i->uri);
		}
		if (!ast_strlen_zero(i->domain)) {
			pbx_builtin_setvar_helper(tmp, "SIPDOMAIN", i->domain);
		}
		if (!ast_strlen_zero(i->useragent)) {
			pbx_builtin_setvar_helper(tmp, "SIPUSERAGENT", i->useragent);
		}
		if (!ast_strlen_zero(i->callid)) {
			pbx_builtin_setvar_helper(tmp, "SIPCALLID", i->callid);
		}
		ast_setstate(tmp, state);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
		/* Set channel variables for this call from configuration */
		for (v = i->chanvars ; v ; v = v->next)
			pbx_builtin_setvar_helper(tmp,v->name,v->value);
				
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}

/* Structure for conversion between compressed SIP and "normal" SIP */
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
	{ "Supported", "k" },
	{ "Refer-To", "r" },
	{ "Allow-Events", "u" },
	{ "Event", "o" },
	{ "Via", "v" },
};

/*--- get_sdp_by_line: Reads one line of SIP message body */
static char* get_sdp_by_line(char* line, char *name, int nameLen) {
  if (strncasecmp(line, name, nameLen) == 0 && line[nameLen] == '=') {
    char* r = line + nameLen + 1;
    while (*r && (*r < 33)) ++r;
    return r;
  }

  return "";
}

/*--- get_sdp: Gets all kind of SIP message bodies, including SDP,
   but the name wrongly applies _only_ sdp */
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
	if (pedanticsipchecking) {
		/* Technically you can place arbitrary whitespace both before and after the ':' in
		   a header, although RFC3261 clearly says you shouldn't before, and place just
		   one afterwards.  If you shouldn't do it, what absolute idiot decided it was 
		   a good idea to say you can do it, and if you can do it, why in the hell would 
		   you say you shouldn't.  */
		for (x=*start;x<req->headers;x++) {
			if (!strncasecmp(req->header[x], name, len)) {
				r = req->header[x] + len;
				while(*r && (*r < 33))
					r++;
				if (*r == ':') {
					r++ ;
					while(*r && (*r < 33))
						r++;
					*start = x+1;
					return r;
				}
			}
		}
	} else {
		/* We probably shouldn't even bother counting whitespace afterwards but
		   I guess for backwards compatibility we will */
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
	}
	/* Try aliases */
	for (x=0;x<sizeof(aliases) / sizeof(aliases[0]); x++) 
		if (!strcasecmp(aliases[x].fullname, name))
			return __get_header(req, aliases[x].shortname, start);

	/* Don't return NULL, so get_header is always a valid pointer */
	return "";
}

/*--- get_header: Get header from SIP request ---*/
static char *get_header(struct sip_request *req, char *name)
{
	int start = 0;
	return __get_header(req, name, &start);
}

/*--- sip_rtp_read: Read RTP from network ---*/
static struct ast_frame *sip_rtp_read(struct ast_channel *ast, struct sip_pvt *p)
{
	/* Retrieve audio/etc from channel.  Assumes p->lock is already held. */
	struct ast_frame *f;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	switch(ast->fdno) {
	case 0:
		f = ast_rtp_read(p->rtp);	/* RTP Audio */
		break;
	case 1:
		f = ast_rtcp_read(p->rtp);	/* RTCP Control Channel */
		break;
	case 2:
		f = ast_rtp_read(p->vrtp);	/* RTP Video */
		break;
	case 3:
		f = ast_rtcp_read(p->vrtp);	/* RTCP Control Channel for video */
		break;
	default:
		f = &null_frame;
	}
	/* Don't send RFC2833 if we're not supposed to */
	if (f && (f->frametype == AST_FRAME_DTMF) && (ast_test_flag(p, SIP_DTMF) != SIP_DTMF_RFC2833))
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
            if ((ast_test_flag(p, SIP_DTMF) == SIP_DTMF_INBAND) && p->vad) {
                   f = ast_dsp_process(p->owner,p->vad,f);
		   if (f && (f->frametype == AST_FRAME_DTMF)) 
			ast_log(LOG_DEBUG, "Detected DTMF '%c'\n", f->subclass);
            }
		}
	}
	return f;
}

/*--- sip_read: Read SIP RTP from channel */
static struct ast_frame *sip_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct sip_pvt *p = ast->pvt->pvt;
	ast_mutex_lock(&p->lock);
	fr = sip_rtp_read(ast, p);
	time(&p->lastrtprx);
	ast_mutex_unlock(&p->lock);
	return fr;
}

/*--- build_callid: Build SIP CALLID header ---*/
static void build_callid(char *callid, int len, struct in_addr ourip, char *fromdomain)
{
	int res;
	int val;
	int x;
	char iabuf[INET_ADDRSTRLEN];
	for (x=0;x<4;x++) {
		val = rand();
		res = snprintf(callid, len, "%08x", val);
		len -= res;
		callid += res;
	}
	if (!ast_strlen_zero(fromdomain))
		snprintf(callid, len, "@%s", fromdomain);
	else
	/* It's not important that we really use our right IP here... */
		snprintf(callid, len, "@%s", ast_inet_ntoa(iabuf, sizeof(iabuf), ourip));
}

/*--- sip_alloc: Allocate SIP_PVT structure and set defaults ---*/
static struct sip_pvt *sip_alloc(char *callid, struct sockaddr_in *sin, int useglobal_nat)
{
	struct sip_pvt *p;

	p = malloc(sizeof(struct sip_pvt));
	if (!p)
		return NULL;
	/* Keep track of stuff */
	memset(p, 0, sizeof(struct sip_pvt));
        ast_mutex_init(&p->lock);

	p->initid = -1;
	p->autokillid = -1;
	p->stateid = -1;
	p->prefs = prefs;
#ifdef OSP_SUPPORT
	p->osphandle = -1;
#endif	
	if (sin) {
		memcpy(&p->sa, sin, sizeof(p->sa));
		if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
			memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	} else {
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	}
	p->rtp = ast_rtp_new_with_bindaddr(sched, io, 1, 0, bindaddr.sin_addr);
	if (videosupport)
		p->vrtp = ast_rtp_new_with_bindaddr(sched, io, 1, 0, bindaddr.sin_addr);
	p->branch = rand();	
	p->tag = rand();
	
	/* Start with 101 instead of 1 */
	p->ocseq = 101;
	if (!p->rtp) {
		ast_log(LOG_WARNING, "Unable to create RTP session: %s\n", strerror(errno));
                ast_mutex_destroy(&p->lock);
		if(p->chanvars) {
			ast_variables_destroy(p->chanvars);
			p->chanvars = NULL;
		}
		free(p);
		return NULL;
	}
	ast_rtp_settos(p->rtp, tos);
	if (p->vrtp)
		ast_rtp_settos(p->vrtp, tos);
	if (useglobal_nat && sin) {
		/* Setup NAT structure according to global settings if we have an address */
		ast_copy_flags(p, &global_flags, SIP_NAT);
		memcpy(&p->recv, sin, sizeof(p->recv));
		ast_rtp_setnat(p->rtp, (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
		if (p->vrtp)
			ast_rtp_setnat(p->vrtp, (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
	}

	strncpy(p->fromdomain, default_fromdomain, sizeof(p->fromdomain) - 1);
	build_via(p, p->via, sizeof(p->via));
	if (!callid)
		build_callid(p->callid, sizeof(p->callid), p->ourip, p->fromdomain);
	else
		strncpy(p->callid, callid, sizeof(p->callid) - 1);
	ast_copy_flags(p, (&global_flags), SIP_PROMISCREDIR | SIP_TRUSTRPID | SIP_DTMF | SIP_REINVITE | SIP_PROG_INBAND | SIP_OSPAUTH);
	/* Assign default music on hold class */
	strncpy(p->musicclass, global_musicclass, sizeof(p->musicclass) - 1);
	p->rtptimeout = global_rtptimeout;
	p->rtpholdtimeout = global_rtpholdtimeout;
	p->rtpkeepalive = global_rtpkeepalive;
	p->capability = global_capability;
	if (ast_test_flag(p, SIP_DTMF) == SIP_DTMF_RFC2833)
		p->noncodeccapability |= AST_RTP_DTMF;
	strncpy(p->context, default_context, sizeof(p->context) - 1);
	/* Add to list */
	ast_mutex_lock(&iflock);
	p->next = iflist;
	iflist = p;
	ast_mutex_unlock(&iflock);
	if (option_debug)
		ast_log(LOG_DEBUG, "Allocating new SIP call for %s\n", callid);
	return p;
}

/*--- find_call: Connect incoming SIP message to current call or create new call structure */
/*               Called by handle_request ,sipsock_read */
static struct sip_pvt *find_call(struct sip_request *req, struct sockaddr_in *sin)
{
	struct sip_pvt *p;
	char *callid;
	char tmp[256] = "";
	char iabuf[INET_ADDRSTRLEN];
	char *cmd;
	char *tag = "", *c;

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
		if (!strcasecmp(cmd, "SIP/2.0"))
			strncpy(tmp, get_header(req, "To"), sizeof(tmp) - 1);
		else
			strncpy(tmp, get_header(req, "From"), sizeof(tmp) - 1);
		tag = strstr(tmp, "tag=");
		if (tag) {
			tag += 4;
			c = strchr(tag, ';');
			if (c)
				*c = '\0';
		}
			
	}
		
	if (ast_strlen_zero(callid)) {
		ast_log(LOG_WARNING, "Call missing call ID from '%s'\n", ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr));
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

/*--- sip_register: Parse register=> line in sip.conf and add to registry */
static int sip_register(char *value, int lineno)
{
	struct sip_registry *reg;
	char copy[256] = "";
	char *username=NULL, *hostname=NULL, *secret=NULL, *authuser=NULL;
	char *porta=NULL;
	char *contact=NULL;
	char *stringp=NULL;
	
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
	if (!username || ast_strlen_zero(username) || !hostname || ast_strlen_zero(hostname)) {
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
	reg = malloc(sizeof(struct sip_registry));
	if (reg) {
		memset(reg, 0, sizeof(struct sip_registry));
		regobjs++;
		ASTOBJ_INIT(reg);
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
		reg->portno = porta ? atoi(porta) : 0;
		reg->callid_valid = 0;
		reg->ocseq = 101;
		ASTOBJ_CONTAINER_LINK(&regl, reg);
		ASTOBJ_UNREF(reg,sip_registry_destroy);
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	return 0;
}

/*--- lws2sws: Parse multiline SIP headers into one header */
/* This is enabled if pedanticsipchecking is enabled */
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
			if (msgbuf[h + 1] == ' ' || msgbuf[h + 1] == '\t') { 
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

/*--- parse: Parse a SIP message ----*/
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

/*--- process_sdp: Process SIP SDP ---*/
static int process_sdp(struct sip_pvt *p, struct sip_request *req)
{
	char *m;
	char *c;
	char *a;
	char host[258];
	char iabuf[INET_ADDRSTRLEN];
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
	int destiterator = 0;
	int iterator;
	int sendonly = 0;
	int x,y;
	int debug=sip_debug_test_pvt(p);

	/* Update our last rtprx when we receive an SDP, too */
	time(&p->lastrtprx);
	time(&p->lastrtptx);

	/* Get codec and RTP info from SDP */
	if (strcasecmp(get_header(req, "Content-Type"), "application/sdp")) {
		ast_log(LOG_NOTICE, "Content is '%s', not 'application/sdp'\n", get_header(req, "Content-Type"));
		return -1;
	}
	m = get_sdp(req, "m");
	sdpLineNum_iterator_init(&destiterator);
	c = get_sdp_iterate(&destiterator, req, "c");
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
	ast_set_flag(p, SIP_NOVIDEO);	
	while ((m = get_sdp_iterate(&iterator, req, "m"))[0] != '\0') {
		if ((sscanf(m, "audio %d RTP/AVP %n", &x, &len) == 1) ||
		    (sscanf(m, "audio %d/%d RTP/AVP %n", &x, &y, &len) == 2)) {
			portno = x;
			/* Scan through the RTP payload types specified in a "m=" line: */
			ast_rtp_pt_clear(p->rtp);
			codecs = m + len;
			while(!ast_strlen_zero(codecs)) {
				if (sscanf(codecs, "%d%n", &codec, &len) != 1) {
					ast_log(LOG_WARNING, "Error in codec string '%s'\n", codecs);
					return -1;
				}
				if (debug)
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
			ast_clear_flag(p, SIP_NOVIDEO);	
			vportno = x;
			/* Scan through the RTP payload types specified in a "m=" line: */
			codecs = m + len;
			while(!ast_strlen_zero(codecs)) {
				if (sscanf(codecs, "%d%n", &codec, &len) != 1) {
					ast_log(LOG_WARNING, "Error in codec string '%s'\n", codecs);
					return -1;
				}
				if (debug)
					ast_verbose("Found video format %s\n", ast_getformatname(codec));
				ast_rtp_set_m_type(p->vrtp, codec);
				codecs += len;
				/* Skip over any whitespace */
				while(*codecs && (*codecs < 33)) codecs++;
			}
		}
	}
	/* Check for Media-description-level-address for audio */
	if (pedanticsipchecking) {
		c = get_sdp_iterate(&destiterator, req, "c");
		if (!ast_strlen_zero(c)) {
			if (sscanf(c, "IN IP4 %256s", host) != 1) {
				ast_log(LOG_WARNING, "Invalid secondary host in c= line, '%s'\n", c);
			} else {
				/* XXX This could block for a long time, and block the main thread! XXX */
				hp = ast_gethostbyname(host, &ahp);
				if (!hp) {
					ast_log(LOG_WARNING, "Unable to lookup host in secondary c= line, '%s'\n", c);
				}
			}
		}
	}
	/* RTP addresses and ports for audio and video */
	sin.sin_family = AF_INET;
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));

	/* Setup audio port number */
	sin.sin_port = htons(portno);
	if (p->rtp && sin.sin_port) {
		ast_rtp_set_peer(p->rtp, &sin);
		if (debug) {
			ast_verbose("Peer audio RTP is at port %s:%d\n", ast_inet_ntoa(iabuf,sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port));
			ast_log(LOG_DEBUG,"Peer audio RTP is at port %s:%d\n",ast_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port));
		}
	}
	/* Check for Media-description-level-address for video */
	if (pedanticsipchecking) {
		c = get_sdp_iterate(&destiterator, req, "c");
		if (!ast_strlen_zero(c)) {
			if (sscanf(c, "IN IP4 %256s", host) != 1) {
				ast_log(LOG_WARNING, "Invalid secondary host in c= line, '%s'\n", c);
			} else {
				/* XXX This could block for a long time, and block the main thread! XXX */
				hp = ast_gethostbyname(host, &ahp);
				if (!hp) {
					ast_log(LOG_WARNING, "Unable to lookup host in secondary c= line, '%s'\n", c);
				}
			}
		}
	}
	/* Setup video port number */
	sin.sin_port = htons(vportno);
	if (p->vrtp && sin.sin_port) {
		ast_rtp_set_peer(p->vrtp, &sin);
		if (debug) {
			ast_verbose("Peer video RTP is at port %s:%d\n", ast_inet_ntoa(iabuf,sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port));
			ast_log(LOG_DEBUG,"Peer video RTP is at port %s:%d\n",ast_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port));
		}
	}

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
	  if (debug)
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
	p->peercapability = (peercapability | vpeercapability);
	p->noncodeccapability = noncodeccapability & peernoncodeccapability;
	
	if (debug) {
		/* shame on whoever coded this.... */
		const unsigned slen=512;
		char s1[slen], s2[slen], s3[slen], s4[slen];

		ast_verbose("Capabilities: us - %s, peer - audio=%s/video=%s, combined - %s\n",
			ast_getformatname_multiple(s1, slen, p->capability),
			ast_getformatname_multiple(s2, slen, peercapability),
			ast_getformatname_multiple(s3, slen, vpeercapability),
			ast_getformatname_multiple(s4, slen, p->jointcapability));

		ast_verbose("Non-codec capabilities: us - %s, peer - %s, combined - %s\n",
			ast_getformatname_multiple(s1, slen, noncodeccapability),
			ast_getformatname_multiple(s2, slen, peernoncodeccapability),
			ast_getformatname_multiple(s3, slen, p->noncodeccapability));
	}
	if (!p->jointcapability) {
		ast_log(LOG_NOTICE, "No compatible codecs!\n");
		return -1;
	}
	if (p->owner) {
		if (!(p->owner->nativeformats & p->jointcapability)) {
			const unsigned slen=512;
			char s1[slen], s2[slen];
			ast_log(LOG_DEBUG, "Oooh, we need to change our formats since our peer supports only %s and not %s\n", 
					ast_getformatname_multiple(s1, slen, p->jointcapability),
					ast_getformatname_multiple(s2, slen, p->owner->nativeformats));
			p->owner->nativeformats = ast_codec_choose(&p->prefs, p->jointcapability, 1);
			ast_set_read_format(p->owner, p->owner->readformat);
			ast_set_write_format(p->owner, p->owner->writeformat);
		}
		if (ast_bridged_channel(p->owner)) {
			/* Turn on/off music on hold if we are holding/unholding */
			if (sin.sin_addr.s_addr && !sendonly) {
				ast_moh_stop(ast_bridged_channel(p->owner));
				if (callevents && ast_test_flag(p, SIP_CALL_ONHOLD)) {
					manager_event(EVENT_FLAG_CALL, "Unhold",
						"Channel: %s\r\n"
						"Uniqueid: %s\r\n",
						p->owner->name, 
						p->owner->uniqueid);
					ast_clear_flag(p, SIP_CALL_ONHOLD);
				}
			} else {
				if (callevents && !ast_test_flag(p, SIP_CALL_ONHOLD)) {
					manager_event(EVENT_FLAG_CALL, "Hold",
						"Channel: %s\r\n"
						"Uniqueid: %s\r\n",
						p->owner->name, 
						p->owner->uniqueid);
						ast_set_flag(p, SIP_CALL_ONHOLD);
				}
				ast_moh_start(ast_bridged_channel(p->owner), NULL);
				if (sendonly)
					ast_rtp_stop(p->rtp);
			}
		}
	}
	return 0;
	
}

/*--- add_header: Add header to SIP message */
static int add_header(struct sip_request *req, char *var, char *value)
{
	int x = 0;
	char *shortname = "";
	if (req->len >= sizeof(req->data) - 4) {
		ast_log(LOG_WARNING, "Out of space, can't add anymore (%s:%s)\n", var, value);
		return -1;
	}
	if (req->lines) {
		ast_log(LOG_WARNING, "Can't add more headers when lines have been added\n");
		return -1;
	}

	req->header[req->headers] = req->data + req->len;
	if (compactheaders) {
		for (x=0;x<sizeof(aliases) / sizeof(aliases[0]); x++)
			if (!strcasecmp(aliases[x].fullname, var))
				shortname = aliases[x].shortname;
	}
	if(!ast_strlen_zero(shortname)) {
		snprintf(req->header[req->headers], sizeof(req->data) - req->len - 4, "%s: %s\r\n", shortname, value);
	} else {
		snprintf(req->header[req->headers], sizeof(req->data) - req->len - 4, "%s: %s\r\n", var, value);
	}
	req->len += strlen(req->header[req->headers]);
	if (req->headers < SIP_MAX_HEADERS)
		req->headers++;
	else {
		ast_log(LOG_WARNING, "Out of header space\n");
		return -1;
	}
	return 0;	
}

/*--- add_blank_header: Add blank header to SIP message */
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

/*--- add_line: Add content (not header) to SIP message */
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

/*--- copy_header: Copy one header field from one request to another */
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

/*--- copy_all_header: Copy all headers from one request to another ---*/
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

/*--- copy_via_headers: Copy SIP VIA Headers from one request to another ---*/
static int copy_via_headers(struct sip_pvt *p, struct sip_request *req, struct sip_request *orig, char *field)
{
	char tmp[256]="", *oh, *end;
	int start = 0;
	int copied = 0;
	char new[256];
	char iabuf[INET_ADDRSTRLEN];
	for (;;) {
		oh = __get_header(orig, field, &start);
		if (!ast_strlen_zero(oh)) {
			/* Strip ;rport */
			strncpy(tmp, oh, sizeof(tmp) - 1);
			oh = strstr(tmp, ";rport");
			if (oh) {
				end = strchr(oh + 1, ';');
				if (end)
					memmove(oh, end, strlen(end) + 1);
				else
					*oh = '\0';
			}
			if (!copied && (ast_test_flag(p, SIP_NAT) == SIP_NAT_ALWAYS)) {
				/* Whoo hoo!  Now we can indicate port address translation too!  Just
				   another RFC (RFC3581). I'll leave the original comments in for
				   posterity.  */
				snprintf(new, sizeof(new), "%s;received=%s;rport=%d", tmp, ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr), ntohs(p->recv.sin_port));
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

/*--- add_route: Add route header into request per learned route ---*/
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
		strncpy(p, route->hop, rem);  p += n;
		*p++ = '>';
		rem -= (n+2);
		route = route->next;
	}
	*p = '\0';
	add_header(req, "Route", r);
}

/*--- set_destination: Set destination from SIP URI ---*/
static void set_destination(struct sip_pvt *p, char *uri)
{
	char *h, *maddr, hostname[256] = "";
	char iabuf[INET_ADDRSTRLEN];
	int port, hn;
	struct hostent *hp;
	struct ast_hostent ahp;
	int debug=sip_debug_test_pvt(p);

	/* Parse uri to h (host) and port - uri is already just the part inside the <> */
	/* general form we are expecting is sip[s]:username[:password]@host[:port][;...] */

	if (debug)
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
	if (hn > (sizeof(hostname) - 1)) hn = sizeof(hostname) - 1;
	strncpy(hostname, h, hn);  hostname[hn] = '\0'; /* safe */
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
		if (hn > (sizeof(hostname) - 1)) hn = sizeof(hostname) - 1;
		strncpy(hostname, maddr, hn);  hostname[hn] = '\0'; /* safe */
	}
	
	hp = ast_gethostbyname(hostname, &ahp);
	if (hp == NULL)  {
		ast_log(LOG_WARNING, "Can't find address for host '%s'\n", hostname);
		return;
	}
	p->sa.sin_family = AF_INET;
	memcpy(&p->sa.sin_addr, hp->h_addr, sizeof(p->sa.sin_addr));
	p->sa.sin_port = htons(port);
	if (debug)
		ast_verbose("set_destination: set destination to %s, port %d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr), port);
}

/*--- init_resp: Initialize SIP response, based on SIP request ---*/
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

/*--- init_req: Initialize SIP request ---*/
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


/*--- respprep: Prepare SIP response packet ---*/
static int respprep(struct sip_request *resp, struct sip_pvt *p, char *msg, struct sip_request *req)
{
	char newto[256] = "", *ot;

	memset(resp, 0, sizeof(*resp));
	init_resp(resp, msg, req);
	copy_via_headers(p, resp, req, "Via");
	if (msg[0] == '2')
		copy_all_header(resp, req, "Record-Route");
	copy_header(resp, req, "From");
	ot = get_header(req, "To");
	if (!strstr(ot, "tag=")) {
		/* Add the proper tag if we don't have it already.  If they have specified
		   their tag, use it.  Otherwise, use our own tag */
		if (!ast_strlen_zero(p->theirtag) && ast_test_flag(p, SIP_OUTGOING))
			snprintf(newto, sizeof(newto), "%s;tag=%s", ot, p->theirtag);
		else if (p->tag && !ast_test_flag(p, SIP_OUTGOING))
			snprintf(newto, sizeof(newto), "%s;tag=as%08x", ot, p->tag);
		else {
			strncpy(newto, ot, sizeof(newto) - 1);
			newto[sizeof(newto) - 1] = '\0';
		}
		ot = newto;
	}
	add_header(resp, "To", ot);
	copy_header(resp, req, "Call-ID");
	copy_header(resp, req, "CSeq");
	add_header(resp, "User-Agent", default_useragent);
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

/*--- reqprep: Initialize a SIP request packet ---*/
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
		build_via(p, p->via, sizeof(p->via));
	}
	if (!strcasecmp(msg, "CANCEL")) {
		c = p->initreq.rlPart2;	/* Use original URI */
	} else if (!strcasecmp(msg, "ACK")) {
		/* Use URI from Contact: in 200 OK (if INVITE) 
		(we only have the contacturi on INVITEs) */
		if (!ast_strlen_zero(p->okcontacturi))
			c = p->okcontacturi;
		else
			c = p->initreq.rlPart2;
	} else if (!ast_strlen_zero(p->okcontacturi)) {
		c = p->okcontacturi; /* Use for BYE or REINVITE */
	} else if (!ast_strlen_zero(p->uri)) {
		c = p->uri;
	} else {
		/* We have no URI, use To: or From:  header as URI (depending on direction) */
		if (ast_test_flag(p, SIP_OUTGOING))
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
		if (ast_test_flag(p, SIP_OUTGOING) && !ast_strlen_zero(p->theirtag))
			snprintf(newto, sizeof(newto), "%s;tag=%s", ot, p->theirtag);
		else if (!ast_test_flag(p, SIP_OUTGOING))
			snprintf(newto, sizeof(newto), "%s;tag=as%08x", ot, p->tag);
		else
			snprintf(newto, sizeof(newto), "%s", ot);
		ot = newto;
	}

	if (ast_test_flag(p, SIP_OUTGOING)) {
		add_header(req, "From", of);
		add_header(req, "To", ot);
	} else {
		add_header(req, "From", ot);
		add_header(req, "To", of);
	}
	add_header(req, "Contact", p->our_contact);
	copy_header(req, orig, "Call-ID");
	add_header(req, "CSeq", tmp);

	add_header(req, "User-Agent", default_useragent);
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

/*--- transmit_response: Transmit response, no retransmits */
static int transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req) 
{
	return __transmit_response(p, msg, req, 0);
}

/*--- transmit_response: Transmit response, Make sure you get a reply */
static int transmit_response_reliable(struct sip_pvt *p, char *msg, struct sip_request *req, int fatal)
{
	return __transmit_response(p, msg, req, fatal ? 2 : 1);
}

/*--- append_date: Append date to SIP message ---*/
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

/*--- transmit_response_with_date: Append date and content length before transmitting response ---*/
static int transmit_response_with_date(struct sip_pvt *p, char *msg, struct sip_request *req)
{
	struct sip_request resp;
	respprep(&resp, p, msg, req);
	append_date(&resp);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp, 0, 0);
}

/*--- transmit_response_with_allow: Append Accept header, content length before transmitting response ---*/
static int transmit_response_with_allow(struct sip_pvt *p, char *msg, struct sip_request *req, int reliable)
{
	struct sip_request resp;
	respprep(&resp, p, msg, req);
	add_header(&resp, "Accept", "application/sdp");
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp, reliable, 0);
}

/* transmit_response_with_auth: Respond with authorization request */
static int transmit_response_with_auth(struct sip_pvt *p, char *msg, struct sip_request *req, char *randdata, int reliable, char *header)
{
	struct sip_request resp;
	char tmp[256];
	int seqno = 0;
	if (reliable && (sscanf(get_header(req, "CSeq"), "%i ", &seqno) != 1)) {
		ast_log(LOG_WARNING, "Unable to determine sequence number from '%s'\n", get_header(req, "CSeq"));
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "Digest realm=\"%s\", nonce=\"%s\"", global_realm, randdata);
	respprep(&resp, p, msg, req);
	add_header(&resp, header, tmp);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp, reliable, seqno);
}

/*--- add_text: Add text body to SIP message ---*/
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

/*--- add_digit: add DTMF INFO tone to sip message ---*/
/* Always adds default duration 250 ms, regardless of what came in over the line */
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

/*--- add_sdp: Add Session Description Protocol message ---*/
static int add_sdp(struct sip_request *resp, struct sip_pvt *p)
{
	int len = 0;
	int codec = 0;
	int pref_codec = 0;
	int alreadysent = 0;
	char costr[80];
	struct sockaddr_in sin;
	struct sockaddr_in vsin;
	char v[256] = "";
	char s[256] = "";
	char o[256] = "";
	char c[256] = "";
	char t[256] = "";
	char m[256] = "";
	char m2[256] = "";
	char a[1024] = "";
	char a2[1024] = "";
	char iabuf[INET_ADDRSTRLEN];
	int x = 0;
	int capability = 0 ;
	struct sockaddr_in dest;
	struct sockaddr_in vdest = { 0, };
	int debug=0;
	
	debug = sip_debug_test_pvt(p);

	/* XXX We break with the "recommendation" and send our IP, in order that our
	       peer doesn't have to ast_gethostbyname() us XXX */
	len = 0;
	if (!p->rtp) {
		ast_log(LOG_WARNING, "No way to add SDP without an RTP structure\n");
		return -1;
	}
	capability = p->capability;
		
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
		if (p->redircodecs)
			capability = p->redircodecs;
	} else {
		dest.sin_addr = p->ourip;
		dest.sin_port = sin.sin_port;
	}

	/* Determine video destination */
	if (p->vrtp) {
		if (p->vredirip.sin_addr.s_addr) {
			vdest.sin_port = p->vredirip.sin_port;
			vdest.sin_addr = p->vredirip.sin_addr;
		} else {
			vdest.sin_addr = p->ourip;
			vdest.sin_port = vsin.sin_port;
		}
	}
	if (debug){
		ast_verbose("We're at %s port %d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip), ntohs(sin.sin_port));	
		if (p->vrtp)
			ast_verbose("Video is at %s port %d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip), ntohs(vsin.sin_port));	
	}
	snprintf(v, sizeof(v), "v=0\r\n");
	snprintf(o, sizeof(o), "o=root %d %d IN IP4 %s\r\n", p->sessionid, p->sessionversion, ast_inet_ntoa(iabuf, sizeof(iabuf), dest.sin_addr));
	snprintf(s, sizeof(s), "s=session\r\n");
	snprintf(c, sizeof(c), "c=IN IP4 %s\r\n", ast_inet_ntoa(iabuf, sizeof(iabuf), dest.sin_addr));
	snprintf(t, sizeof(t), "t=0 0\r\n");
	snprintf(m, sizeof(m), "m=audio %d RTP/AVP", ntohs(dest.sin_port));
	snprintf(m2, sizeof(m2), "m=video %d RTP/AVP", ntohs(vdest.sin_port));
	/* Prefer the codec we were requested to use, first, no matter what */
	if (capability & p->prefcodec) {
		if (debug)
			ast_verbose("Answering/Requesting with root capability 0x%x (%s)\n", p->prefcodec, ast_getformatname(p->prefcodec));
		codec = ast_rtp_lookup_code(p->rtp, 1, p->prefcodec);
		if (codec > -1) {
			snprintf(costr, sizeof(costr), " %d", codec);
			if (p->prefcodec <= AST_FORMAT_MAX_AUDIO) {
				strncat(m, costr, sizeof(m) - strlen(m) - 1);
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(1, p->prefcodec));
				strncpy(a, costr, sizeof(a) - 1);
			} else {
				strncat(m2, costr, sizeof(m2) - strlen(m2) - 1);
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/90000\r\n", codec, ast_rtp_lookup_mime_subtype(1, p->prefcodec));
				strncpy(a2, costr, sizeof(a2) - 1);
			}
		}
		alreadysent |= p->prefcodec;
	}
	/* Start by sending our preferred codecs */
	for (x = 0 ; x < 32 ; x++) {
		if(!(pref_codec = ast_codec_pref_index(&p->prefs,x)))
			break; 
		if ((capability & pref_codec) && !(alreadysent & pref_codec)) {
			if (debug)
				ast_verbose("Answering with preferred capability 0x%x (%s)\n", pref_codec, ast_getformatname(pref_codec));
			codec = ast_rtp_lookup_code(p->rtp, 1, pref_codec);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				if (pref_codec <= AST_FORMAT_MAX_AUDIO) {
					strncat(m, costr, sizeof(m) - strlen(m) - 1);
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(1, pref_codec));
					strncat(a, costr, sizeof(a) - strlen(a) - 1);
				} else {
					strncat(m2, costr, sizeof(m2) - strlen(m2) - 1);
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/90000\r\n", codec, ast_rtp_lookup_mime_subtype(1, pref_codec));
					strncat(a2, costr, sizeof(a2) - strlen(a) - 1);
				}
			}
		}
		alreadysent |= pref_codec;
	}

	/* Now send any other common codecs, and non-codec formats: */
	for (x = 1; x <= ((videosupport && p->vrtp) ? AST_FORMAT_MAX_VIDEO : AST_FORMAT_MAX_AUDIO); x <<= 1) {
		if ((capability & x) && !(alreadysent & x)) {
			if (debug)
				ast_verbose("Answering with capability 0x%x (%s)\n", x, ast_getformatname(x));
			codec = ast_rtp_lookup_code(p->rtp, 1, x);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				if (x <= AST_FORMAT_MAX_AUDIO) {
					strncat(m, costr, sizeof(m) - strlen(m) - 1);
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(1, x));
					strncat(a, costr, sizeof(a) - strlen(a) - 1);
				} else {
					strncat(m2, costr, sizeof(m2) - strlen(m2) - 1);
					snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/90000\r\n", codec, ast_rtp_lookup_mime_subtype(1, x));
					strncat(a2, costr, sizeof(a2) - strlen(a2) - 1);
				}
			}
		}
	}
	for (x = 1; x <= AST_RTP_MAX; x <<= 1) {
		if (p->noncodeccapability & x) {
			if (debug)
				ast_verbose("Answering with non-codec capability 0x%x (%s)\n", x, ast_rtp_lookup_mime_subtype(0, x));
			codec = ast_rtp_lookup_code(p->rtp, 0, x);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				strncat(m, costr, sizeof(m) - strlen(m) - 1);
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype(0, x));
				strncat(a, costr, sizeof(a) - strlen(a) - 1);
				if (x == AST_RTP_DTMF) {
				  /* Indicate we support DTMF and FLASH... */
				  snprintf(costr, sizeof costr, "a=fmtp:%d 0-16\r\n",
					   codec);
				  strncat(a, costr, sizeof(a) - strlen(a) - 1);
				}
			}
		}
	}
	strncat(a, "a=silenceSupp:off - - - -\r\n", sizeof(a) - strlen(a) - 1);
	if (strlen(m) < sizeof(m) - 2)
		strncat(m, "\r\n", sizeof(m) - strlen(m) - 1);
	if (strlen(m2) < sizeof(m2) - 2)
		strncat(m2, "\r\n", sizeof(m2) - strlen(m2) - 1);
	if ((sizeof(m) <= strlen(m) - 2) || (sizeof(m2) <= strlen(m2) - 2) || (sizeof(a) == strlen(a)) || (sizeof(a2) == strlen(a2)))
		ast_log(LOG_WARNING, "SIP SDP may be truncated due to undersized buffer!!\n");
	len = strlen(v) + strlen(s) + strlen(o) + strlen(c) + strlen(t) + strlen(m) + strlen(a);
	if ((p->vrtp) && (!ast_test_flag(p, SIP_NOVIDEO)) && (capability & VIDEO_CODEC_MASK)) /* only if video response is appropriate */
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
	if ((p->vrtp) && (!ast_test_flag(p, SIP_NOVIDEO)) && (capability & VIDEO_CODEC_MASK)) { /* only if video response is appropriate */
		add_line(resp, m2);
		add_line(resp, a2);
	}
	/* Update lastrtprx when we send our SDP */
	time(&p->lastrtprx);
	time(&p->lastrtptx);
	return 0;
}

/*--- copy_request: copy SIP request (mostly used to save request for responses) ---*/
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

/*--- transmit_response_with_sdp: Used for 200 OK and 183 early media ---*/
static int transmit_response_with_sdp(struct sip_pvt *p, char *msg, struct sip_request *req, int retrans)
{
	struct sip_request resp;
	int seqno;
	if (sscanf(get_header(req, "CSeq"), "%i ", &seqno) != 1) {
		ast_log(LOG_WARNING, "Unable to get seqno from '%s'\n", get_header(req, "CSeq"));
		return -1;
	}
	respprep(&resp, p, msg, req);
	ast_rtp_offered_from_local(p->rtp, 0);
	add_sdp(&resp, p);
	return send_response(p, &resp, retrans, seqno);
}

/*--- determine_firstline_parts: parse first line of incoming SIP request */
static int determine_firstline_parts( struct sip_request *req ) 
{
	char *e, *cmd;
	int len;
  
	cmd = req->header[0];
	while(*cmd && (*cmd < 33)) {
		cmd++;
	}
	if (!*cmd) {
		return -1;
	}
	e = cmd;
	while(*e && (*e > 32)) {
		e++;
	}
	/* Get the command */
	if (*e) {
		*e = '\0';
		e++;
	}
	req->rlPart1 = cmd;
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
		if( len < 2 ) { 
			return -1;
		}
		e+= len - 1;
		while( *e && *e<33 ) {
			e--; 
		}
		*(++e)= '\0';
	} else {
		/* We have a request */
		if( *e == '<' ) { 
			e++;
			if( !*e ) { 
				return -1; 
			}  
		}
		req->rlPart2= e;	/* URI */
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

/*--- transmit_reinvite_with_sdp: Transmit reinvite with SDP :-) ---*/
/* 	A re-invite is basically a new INVITE with the same CALL-ID and TAG as the
	INVITE that opened the SIP dialogue 
	We reinvite so that the audio stream (RTP) go directly between
	the SIP UAs. SIP Signalling stays with * in the path.
*/
static int transmit_reinvite_with_sdp(struct sip_pvt *p)
{
	struct sip_request req;
	if (ast_test_flag(p, SIP_REINVITE_UPDATE))
		reqprep(&req, p, "UPDATE", 0, 1);
	else 
		reqprep(&req, p, "INVITE", 0, 1);
	
	add_header(&req, "Allow", ALLOWED_METHODS);
	ast_rtp_offered_from_local(p->rtp, 1);
	add_sdp(&req, p);
	/* Use this as the basis */
	copy_request(&p->initreq, &req);
	parse(&p->initreq);
	if (sip_debug_test_pvt(p))
		ast_verbose("%d headers, %d lines\n", p->initreq.headers, p->initreq.lines);
	determine_firstline_parts(&p->initreq);
	p->lastinvite = p->ocseq;
	ast_set_flag(p, SIP_OUTGOING);
	return send_request(p, &req, 1, p->ocseq);
}

/*--- extract_uri: Check Contact: URI of SIP message ---*/
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

/*--- build_contact: Build contact header - the contact header we send out ---*/
static void build_contact(struct sip_pvt *p)
{
	char iabuf[INET_ADDRSTRLEN];
	/* Construct Contact: header */
	if (ourport != 5060)
		snprintf(p->our_contact, sizeof(p->our_contact), "<sip:%s%s%s:%d>", p->exten, ast_strlen_zero(p->exten) ? "" : "@", ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip), ourport);
	else
		snprintf(p->our_contact, sizeof(p->our_contact), "<sip:%s%s%s>", p->exten, ast_strlen_zero(p->exten) ? "" : "@", ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip));
}

/*--- initreqprep: Initiate SIP request to peer/user ---*/
static void initreqprep(struct sip_request *req, struct sip_pvt *p, char *cmd, char *vxml_url)
{
	char invite[256]="";
	char from[256];
	char to[256];
	char tmp[80];
	char iabuf[INET_ADDRSTRLEN];
	char *l = default_callerid, *n=NULL;
	int x;
	char urioptions[256]="";

	if (ast_test_flag(p, SIP_USEREQPHONE)) {
        	char onlydigits = 1;
        	x=0;

        	/* Test p->username against allowed characters in AST_DIGIT_ANY
        	If it matches the allowed characters list, then sipuser = ";user=phone"

        	If not, then sipuser = ""
        	*/
        	/* + is allowed in first position in a tel: uri */
        	if (p->username && p->username[0] == '+')
                	x=1;

        	for (; x<strlen(p->username); x++) {
                	if (!strchr(AST_DIGIT_ANYNUM, p->username[x])) {
                        	onlydigits = 0;
                        	break;
                	}
        	}

        	/* If we have only digits, add ;user=phone to the uri */
        	if (onlydigits)
                	strcpy(urioptions, ";user=phone");
	}


	snprintf(p->lastmsg, sizeof(p->lastmsg), "Init: %s", cmd);

	if (p->owner) {
		l = p->owner->cid.cid_num;
		n = p->owner->cid.cid_name;
	}
	if (!l || (!ast_isphonenumber(l) && default_callerid[0]))
			l = default_callerid;
	/* if user want's his callerid restricted */
	if (p->callingpres & AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED) {
		l = CALLERID_UNKNOWN;
		n = l;
	}
	if (!n || ast_strlen_zero(n))
		n = l;
	/* Allow user to be overridden */
	if (!ast_strlen_zero(p->fromuser))
		l = p->fromuser;
	else /* Save for any further attempts */
		strncpy(p->fromuser, l, sizeof(p->fromuser) - 1);

	/* Allow user to be overridden */
	if (!ast_strlen_zero(p->fromname))
		n = p->fromname;
	else /* Save for any further attempts */
		strncpy(p->fromname, n, sizeof(p->fromname) - 1);

	if ((ourport != 5060) && ast_strlen_zero(p->fromdomain))
		snprintf(from, sizeof(from), "\"%s\" <sip:%s@%s:%d>;tag=as%08x", n, l, ast_strlen_zero(p->fromdomain) ? ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip) : p->fromdomain, ourport, p->tag);
	else
		snprintf(from, sizeof(from), "\"%s\" <sip:%s@%s>;tag=as%08x", n, l, ast_strlen_zero(p->fromdomain) ? ast_inet_ntoa(iabuf, sizeof(iabuf), p->ourip) : p->fromdomain, p->tag);

	/* If we're calling a registred SIP peer, use the fullcontact to dial to the peer */
	if (!ast_strlen_zero(p->fullcontact)) {
		/* If we have full contact, trust it */
		strncpy(invite, p->fullcontact, sizeof(invite) - 1);
	/* Otherwise, use the username while waiting for registration */
	} else if (!ast_strlen_zero(p->username)) {
		if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
			snprintf(invite, sizeof(invite), "sip:%s@%s:%d%s",p->username, p->tohost, ntohs(p->sa.sin_port), urioptions);
		} else {
			snprintf(invite, sizeof(invite), "sip:%s@%s%s",p->username, p->tohost, urioptions);
		}
	} else if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
		snprintf(invite, sizeof(invite), "sip:%s:%d%s", p->tohost, ntohs(p->sa.sin_port), urioptions);
	} else {
		snprintf(invite, sizeof(invite), "sip:%s%s", p->tohost, urioptions);
	}
	strncpy(p->uri, invite, sizeof(p->uri) - 1);
	/* If there is a VXML URL append it to the SIP URL */
	if (vxml_url)
	{
		snprintf(to, sizeof(to), "<%s>;%s", invite, vxml_url);
	}
	else
	{
		snprintf(to, sizeof(to), "<%s>", invite);
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
	add_header(req, "User-Agent", default_useragent);
}

        
/*--- transmit_invite: Build REFER/INVITE/OPTIONS message and transmit it ---*/
static int transmit_invite(struct sip_pvt *p, char *cmd, int sdp, char *auth, char *authheader, char *vxml_url, char *distinctive_ring, char *osptoken, int addsipheaders, int init)
{
	struct sip_request req;
	
	if (init) {
		/* Bump branch even on initial requests */
		p->branch ^= rand();
		build_via(p, p->via, sizeof(p->via));
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
#ifdef OSP_SUPPORT
	if (osptoken && !ast_strlen_zero(osptoken)) {
		ast_log(LOG_DEBUG,"Adding OSP Token: %s\n", osptoken);
		add_header(&req, "P-OSP-Auth-Token", osptoken);
	}	
	else
	{
		ast_log(LOG_DEBUG,"NOT Adding OSP Token\n");
	}
#endif
	if (distinctive_ring && !ast_strlen_zero(distinctive_ring))
	{
		add_header(&req, "Alert-Info",distinctive_ring);
	}
	add_header(&req, "Allow", ALLOWED_METHODS);
	if (addsipheaders && init) {
		struct ast_channel *ast;
		char *header = (char *) NULL;
		char *content = (char *) NULL;
		char *end = (char *) NULL;
		struct varshead *headp = (struct varshead *) NULL;
		struct ast_var_t *current;

		ast = p->owner;	/* The owner channel */
		if (ast) {
	 		headp=&ast->varshead;
			if (!headp)
				ast_log(LOG_WARNING,"No Headp for the channel...ooops!\n");
			else {
				AST_LIST_TRAVERSE(headp,current,entries) {  
					/* SIPADDHEADER: Add SIP header to outgoing call        */
					if (!strncasecmp(ast_var_name(current),"SIPADDHEADER",strlen("SIPADDHEADER"))) {
						header = ast_var_value(current);
						/* Strip of the starting " (if it's there) */
						if (*header == '"')
					 		header++;
		    			if ((content = strchr(header, ':'))) {
							*content = '\0';
							content++;	/* Move pointer ahead */
							/* Skip white space */
							while (*content == ' ')
						  		content++;
							/* Strip the ending " (if it's there) */
					 		end = content + strlen(content) -1;	
							if (*end == '"')
						   		*end = '\0';
						
	                        			add_header(&req, header, content);
							if (sipdebug)
								ast_log(LOG_DEBUG, "Adding SIP Header \"%s\" with content :%s: \n", header, content);
						}
					}
				}
			}
		}
	}
	if (sdp) {
		ast_rtp_offered_from_local(p->rtp, 1);
		add_sdp(&req, p);
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

/*--- transmit_state_notify: Used in the SUBSCRIBE notification subsystem ----*/
static int transmit_state_notify(struct sip_pvt *p, int state, int full)
{
	char tmp[4000];
	int maxbytes = 0;
	int bytes = 0;
	char from[256], to[256];
	char *t, *c, *a;
	char *mfrom, *mto;
	struct sip_request req;
	char clen[20];

	memset(from, 0, sizeof(from));
	memset(to, 0, sizeof(to));
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

		add_header(&req, "Event", "presence");
		add_header(&req, "Subscription-State", "active");
		add_header(&req, "Content-Type", "application/xpidf+xml");

		if ((state==AST_EXTENSION_UNAVAILABLE) || (state==AST_EXTENSION_BUSY))
			state = 2;
		else if (state==AST_EXTENSION_INUSE)
			state = 1;
		else
			state = 0;

		t = tmp;		
		maxbytes = sizeof(tmp);
		bytes = snprintf(t, maxbytes, "<?xml version=\"1.0\"?>\n");
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<!DOCTYPE presence PUBLIC \"-//IETF//DTD RFCxxxx XPIDF 1.0//EN\" \"xpidf.dtd\">\n");
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<presence>\n");
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<presentity uri=\"%s;method=SUBSCRIBE\" />\n", mfrom);
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<atom id=\"%s\">\n", p->exten);
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<address uri=\"%s;user=ip\" priority=\"0,800000\">\n", mto);
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<status status=\"%s\" />\n", !state ? "open" : (state==1) ? "inuse" : "closed");
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<msnsubstatus substatus=\"%s\" />\n", !state ? "online" : (state==1) ? "onthephone" : "offline");
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "</address>\n</atom>\n</presence>\n");	    	
	} else {
		add_header(&req, "Event", "dialog");
		add_header(&req, "Content-Type", "application/dialog-info+xml");

		t = tmp;		
		maxbytes = sizeof(tmp);
		bytes = snprintf(t, maxbytes, "<?xml version=\"1.0\"?>\n");
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\"%d\" state=\"%s\" entity=\"%s\">\n", p->dialogver++, full ? "full":"partial", mfrom);
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<dialog id=\"%s\">\n", p->exten);
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "<state>%s</state>\n", state ? "confirmed" : "terminated");
		t += bytes;
		maxbytes -= bytes;
		bytes = snprintf(t, maxbytes, "</dialog>\n</dialog-info>\n");	
	}
	if (t > tmp + sizeof(tmp))
		ast_log(LOG_WARNING, "Buffer overflow detected!!  (Please file a bug report)\n");

	snprintf(clen, sizeof(clen), "%d", (int)strlen(tmp));
	add_header(&req, "Content-Length", clen);
	add_line(&req, tmp);

	return send_request(p, &req, 1, p->ocseq);
}

/*--- transmit_notify_with_mwi: Notify user of messages waiting in voicemail ---*/
/*      Notification only works for registred peers with mailbox= definitions
 *      in sip.conf
 *      We use the SIP Event package message-summary
 *      MIME type defaults to  "application/simple-message-summary";
 */
static int transmit_notify_with_mwi(struct sip_pvt *p, int newmsgs, int oldmsgs)
{
	struct sip_request req;
	char tmp[256];
	char tmp2[256];
	char clen[20];
	initreqprep(&req, p, "NOTIFY", NULL);
	add_header(&req, "Event", "message-summary");
	add_header(&req, "Content-Type", default_notifymime);

	snprintf(tmp, sizeof(tmp), "Messages-Waiting: %s\r\n", newmsgs ? "yes" : "no");
	snprintf(tmp2, sizeof(tmp2), "Voice-Message: %d/%d\r\n", newmsgs, oldmsgs);
	snprintf(clen, sizeof(clen), "%d", (int)(strlen(tmp) + strlen(tmp2)));
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

static int transmit_sip_request(struct sip_pvt *p,struct sip_request *req)
{
	if (!p->initreq.headers) {
		/* Use this as the basis */
		copy_request(&p->initreq, req);
		parse(&p->initreq);
		if (sip_debug_test_pvt(p))
			ast_verbose("%d headers, %d lines\n", p->initreq.headers, p->initreq.lines);
		determine_firstline_parts(&p->initreq);
	}

	return send_request(p, req, 0, p->ocseq);
}

/*--- transmit_notify_with_sipfrag: Notify a transferring party of the status of trasnfer ---*/
/*      Apparently the draft SIP REFER structure was too simple, so it was decided that the
 *      status of transfers also needed to be sent via NOTIFY instead of just the 202 Accepted
 *      that had worked heretofore.
 */
static int transmit_notify_with_sipfrag(struct sip_pvt *p, int cseq)
{
	struct sip_request req;
	char tmp[256];
	char clen[20];
	reqprep(&req, p, "NOTIFY", 0, 1);
	snprintf(tmp, sizeof(tmp), "refer;id=%d", cseq);
	add_header(&req, "Event", tmp);
	add_header(&req, "Subscription-state", "terminated;reason=noresource");
	add_header(&req, "Content-Type", "message/sipfrag;version=2.0");

	strncpy(tmp, "SIP/2.0 200 OK", sizeof(tmp) - 1);
	snprintf(clen, sizeof(clen), "%d", (int)(strlen(tmp)));
	add_header(&req, "Content-Length", clen);
	add_line(&req, tmp);

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

static int transmit_register(struct sip_registry *r, char *cmd, char *auth, char *authheader);

/*--- sip_reregister: Update registration with SIP Proxy---*/
static int sip_reregister(void *data) 
{
	/* if we are here, we know that we need to reregister. */
	struct sip_registry *r= ASTOBJ_REF((struct sip_registry *) data);

	/* if we couldn't get a reference to the registry object, punt */
	if (!r)
		return 0;

	/* Since registry's are only added/removed by the the monitor thread, this
	   may be overkill to reference/dereference at all here */
	if (sipdebug)
		ast_log(LOG_NOTICE, "   -- Re-registration for  %s@%s\n", r->username, r->hostname);

	r->expire = -1;
	__sip_do_register(r);
	ASTOBJ_UNREF(r,sip_registry_destroy);
	return 0;
}

/*--- __sip_do_register: Register with SIP proxy ---*/
static int __sip_do_register(struct sip_registry *r)
{
	int res;
	res=transmit_register(r, "REGISTER", NULL, NULL);
	return res;
}

/*--- sip_reg_timeout: Registration timeout, register again */
static int sip_reg_timeout(void *data)
{

	/* if we are here, our registration timed out, so we'll just do it over */
	struct sip_registry *r = ASTOBJ_REF((struct sip_registry *) data);
	struct sip_pvt *p;
	int res;

	/* if we couldn't get a reference to the registry object, punt */
	if (!r)
		return 0;

	ast_log(LOG_NOTICE, "   -- Registration for '%s@%s' timed out, trying again\n", r->username, r->hostname); 
	if (r->call) {
		/* Unlink us, destroy old call.  Locking is not relevent here because all this happens
		   in the single SIP manager thread. */
		p = r->call;
		if (p->registry)
			ASTOBJ_UNREF(p->registry, sip_registry_destroy);
		r->call = NULL;
		ast_set_flag(p, SIP_NEEDDESTROY);	
		/* Pretend to ACK anything just in case */
		__sip_pretend_ack(p);
	}
	r->regstate=REG_STATE_UNREGISTERED;
	manager_event(EVENT_FLAG_SYSTEM, "Registry", "Channel: SIP\r\nUser: %s\r\nDomain: %s\r\nStatus: %s\r\n", r->username, r->hostname, regstate2str(r->regstate));
	r->timeout = -1;
	res=transmit_register(r, "REGISTER", NULL, NULL);
	ASTOBJ_UNREF(r,sip_registry_destroy);
	return 0;
}

/*--- transmit_register: Transmit register to SIP proxy or UA ---*/
static int transmit_register(struct sip_registry *r, char *cmd, char *auth, char *authheader)
{
	struct sip_request req;
	char from[256];
	char to[256];
	char tmp[80];
	char via[80];
	char addr[80];
	struct sip_pvt *p;

	/* exit if we are already in process with this registrar ?*/
	if ( r == NULL || ((auth==NULL) && (r->regstate==REG_STATE_REGSENT || r->regstate==REG_STATE_AUTHSENT))) {
		ast_log(LOG_NOTICE, "Strange, trying to register when registration already pending\n");
		return 0;
	}

	if (r->call) {
		if (!auth) {
			ast_log(LOG_WARNING, "Already have a call??\n");
			return 0;
		} else {
			p = r->call;
			p->tag = rand();	/* create a new local tag for every register attempt */
			p->theirtag[0]='\0';	/* forget their old tag, so we don't match tags when getting response */
		}
	} else {
		/* Build callid for registration if we haven't registred before */
		if (!r->callid_valid) {
			build_callid(r->callid, sizeof(r->callid), __ourip, default_fromdomain);
			r->callid_valid = 1;
		}
		/* Allocate SIP packet for registration */
		p=sip_alloc( r->callid, NULL, 0);
		if (!p) {
			ast_log(LOG_WARNING, "Unable to allocate registration call\n");
			return 0;
		}
		/* Find address to hostname */
		if (create_addr(p,r->hostname)) {
			/* we have what we hope is a temporary network error,
			 * probably DNS.  We need to reschedule a registration try */
			sip_destroy(p);
			if (r->timeout > -1) {
				ast_log(LOG_WARNING, "Still have a registration timeout (create_addr() error), %d\n", r->timeout);
				ast_sched_del(sched, r->timeout);
			}
			r->timeout = ast_sched_add(sched, global_reg_timeout*1000, sip_reg_timeout, r);
			return 0;
		}

		/* Copy back Call-ID in case create_addr changed it */
		strncpy(r->callid, p->callid, sizeof(r->callid) - 1);
		if (r->portno)
			p->sa.sin_port = htons(r->portno);
		ast_set_flag(p, SIP_OUTGOING);	/* Registration is outgoing call */
		r->call=p;			/* Save pointer to SIP packet */
		p->registry=ASTOBJ_REF(r);	/* Add pointer to registry in packet */
		if (!ast_strlen_zero(r->secret))	/* Secret (password) */
			strncpy(p->peersecret, r->secret, sizeof(p->peersecret)-1);
		if (!ast_strlen_zero(r->md5secret))
			strncpy(p->peermd5secret, r->md5secret, sizeof(p->peermd5secret)-1);
		/* User name in this realm  
		- if authuser is set, use that, otherwise use username */
		if (!ast_strlen_zero(r->authuser)) {	
			strncpy(p->peername, r->authuser, sizeof(p->peername)-1);
			strncpy(p->authname, r->authuser, sizeof(p->authname)-1);
		} else {
			if (!ast_strlen_zero(r->username)) {
				strncpy(p->peername, r->username, sizeof(p->peername)-1);
				strncpy(p->authname, r->username, sizeof(p->authname)-1);
				strncpy(p->fromuser, r->username, sizeof(p->fromuser)-1);
			}
		}
		if (!ast_strlen_zero(r->username))
			strncpy(p->username, r->username, sizeof(p->username)-1);
		/* Save extension in packet */
		strncpy(p->exten, r->contact, sizeof(p->exten) - 1);

		/*
		  check which address we should use in our contact header 
		  based on whether the remote host is on the external or
		  internal network so we can register through nat
		 */
		if (ast_sip_ouraddrfor(&p->sa.sin_addr, &p->ourip))
			memcpy(&p->ourip, &bindaddr.sin_addr, sizeof(p->ourip));
		build_contact(p);
	}

	/* set up a timeout */
	if (auth==NULL)  {
		if (r->timeout > -1) {
			ast_log(LOG_WARNING, "Still have a registration timeout, %d\n", r->timeout);
			ast_sched_del(sched, r->timeout);
		}
		r->timeout = ast_sched_add(sched, global_reg_timeout*1000, sip_reg_timeout, r);
		ast_log(LOG_DEBUG, "Scheduled a registration timeout # %d\n", r->timeout);
	}

	if (strchr(r->username, '@')) {
		snprintf(from, sizeof(from), "<sip:%s>;tag=as%08x", r->username, p->tag);
		if (!ast_strlen_zero(p->theirtag))
			snprintf(to, sizeof(to), "<sip:%s>;tag=%s", r->username, p->theirtag);
		else
			snprintf(to, sizeof(to), "<sip:%s>", r->username);
	} else {
		snprintf(from, sizeof(from), "<sip:%s@%s>;tag=as%08x", r->username, p->tohost, p->tag);
		if (!ast_strlen_zero(p->theirtag))
			snprintf(to, sizeof(to), "<sip:%s@%s>;tag=%s", r->username, p->tohost, p->theirtag);
		else
			snprintf(to, sizeof(to), "<sip:%s@%s>", r->username, p->tohost);
	}
	
	snprintf(addr, sizeof(addr), "sip:%s", r->hostname);
	strncpy(p->uri, addr, sizeof(p->uri) - 1);

	p->branch ^= rand();

	memset(&req, 0, sizeof(req));
	init_req(&req, cmd, addr);

	/* Add to CSEQ */
	snprintf(tmp, sizeof(tmp), "%u %s", ++r->ocseq, cmd);
	p->ocseq = r->ocseq;

	build_via(p, via, sizeof(via));
	add_header(&req, "Via", via);
	add_header(&req, "From", from);
	add_header(&req, "To", to);
	add_header(&req, "Call-ID", p->callid);
	add_header(&req, "CSeq", tmp);
	add_header(&req, "User-Agent", default_useragent);

	
	if (auth) 	/* Add auth header */
		add_header(&req, authheader, auth);
	else if ( !ast_strlen_zero(r->nonce) ) {
		char digest[1024];

		/* We have auth data to reuse, build a digest header! */
		if (sipdebug)
			ast_log(LOG_DEBUG, "   >>> Re-using Auth data for %s@%s\n", r->username, r->hostname);
		strncpy(p->realm, r->realm, sizeof(p->realm)-1);
		strncpy(p->nonce, r->nonce, sizeof(p->nonce)-1);
		strncpy(p->domain, r->domain, sizeof(p->domain)-1);
		strncpy(p->opaque, r->opaque, sizeof(p->opaque)-1);
		strncpy(p->qop, r->qop, sizeof(p->qop)-1);

		memset(digest,0,sizeof(digest));
		build_reply_digest(p, "REGISTER", digest, sizeof(digest));
		add_header(&req, "Authorization", digest);
	
	}

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

/*--- transmit_message_with_text: Transmit text with SIP MESSAGE method ---*/
static int transmit_message_with_text(struct sip_pvt *p, char *text)
{
	struct sip_request req;
	reqprep(&req, p, "MESSAGE", 0, 1);
	add_text(&req, text);
	return send_request(p, &req, 1, p->ocseq);
}

/*--- transmit_refer: Transmit SIP REFER message ---*/
static int transmit_refer(struct sip_pvt *p, char *dest)
{
	struct sip_request req;
	char from[256];
	char *of, *c;
	char referto[256];
	if (ast_test_flag(p, SIP_OUTGOING)) 
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

/*--- transmit_info_with_digit: Send SIP INFO dtmf message, see Cisco documentation on cisco.co
m ---*/
static int transmit_info_with_digit(struct sip_pvt *p, char digit)
{
	struct sip_request req;
	reqprep(&req, p, "INFO", 0, 1);
	add_digit(&req, digit);
	return send_request(p, &req, 1, p->ocseq);
}

/*--- transmit_request: transmit generic SIP request ---*/
static int transmit_request(struct sip_pvt *p, char *msg, int seqno, int reliable, int newbranch)
{
	struct sip_request resp;
	reqprep(&resp, p, msg, seqno, newbranch);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_request(p, &resp, reliable, seqno ? seqno : p->ocseq);
}

/*--- transmit_request_with_auth: Transmit SIP request, auth added ---*/
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

/*--- expire_register: Expire registration of SIP peer ---*/
static int expire_register(void *data)
{
	struct sip_peer *p = data;
	memset(&p->addr, 0, sizeof(p->addr));
	ast_db_del("SIP/Registry", p->name);
	manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "Peer: SIP/%s\r\nPeerStatus: Unregistered\r\nCause: Expired\r\n", p->name);
	register_peer_exten(p, 0);
	p->expire = -1;
	ast_device_state_changed("SIP/%s", p->name);
	if (ast_test_flag(p, SIP_SELFDESTRUCT) || ast_test_flag((&p->flags_page2), SIP_PAGE2_RTAUTOCLEAR)) {
		ASTOBJ_MARK(p);	
		prune_peers();
	}
	return 0;
}

static int sip_poke_peer(struct sip_peer *peer);

static int sip_poke_peer_s(void *data)
{
	struct sip_peer *peer = data;
	peer->pokeexpire = -1;
	sip_poke_peer(peer);
	return 0;
}

/*--- reg_source_db: Save registration in Asterisk DB ---*/
static void reg_source_db(struct sip_peer *p)
{
	char data[256];
	char iabuf[INET_ADDRSTRLEN];
	struct in_addr in;
	char *c, *d, *u, *e;
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
						e = strchr(u, ':');
						if (e) {
							*e = '\0';
							e++;
							strncpy(p->fullcontact, e, sizeof(p->fullcontact) - 1);
						}
						strncpy(p->username, u, sizeof(p->username) - 1);
						
					}
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "SIP Seeding peers from Astdb: '%s' at %s@%s:%d for %d\n", p->name, 
									p->username, ast_inet_ntoa(iabuf, sizeof(iabuf), in), atoi(c), atoi(d));
					expiry = atoi(d);
					memset(&p->addr, 0, sizeof(p->addr));
					p->addr.sin_family = AF_INET;
					p->addr.sin_addr = in;
					p->addr.sin_port = htons(atoi(c));
					if (sipsock < 0) {
						/* SIP isn't up yet, so schedule a poke only, pretty soon */
						if (p->pokeexpire > -1)
							ast_sched_del(sched, p->pokeexpire);
						p->pokeexpire = ast_sched_add(sched, rand() % 5000 + 1, sip_poke_peer_s, p);
					} else
						sip_poke_peer(p);
					if (p->expire > -1)
						ast_sched_del(sched, p->expire);
					p->expire = ast_sched_add(sched, (expiry + 10) * 1000, expire_register, (void *)p);
					register_peer_exten(p, 1);
				}					
					
			}
		}
	}
}

/*--- parse_ok_contact: Parse contact header for 200 OK on INVITE ---*/
static int parse_ok_contact(struct sip_pvt *pvt, struct sip_request *req)
{
	char contact[250]= ""; 
	char *c, *n, *pt;
	int port;
	struct hostent *hp;
	struct ast_hostent ahp;
	struct sockaddr_in oldsin;

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


	/* Save full contact to call pvt for later bye or re-invite */
	strncpy(pvt->fullcontact, c, sizeof(pvt->fullcontact) - 1);	

	/* Save URI for later ACKs, BYE or RE-invites */
	strncpy(pvt->okcontacturi, c, sizeof(pvt->okcontacturi) - 1);
	
	/* Make sure it's a SIP URL */
	if (strncasecmp(c, "sip:", 4)) {
		ast_log(LOG_NOTICE, "'%s' is not a valid SIP contact (missing sip:) trying to use anyway\n", c);
	} else
		c += 4;

	/* Ditch arguments */
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

	memcpy(&oldsin, &pvt->sa, sizeof(oldsin));

	if (!(ast_test_flag(pvt, SIP_NAT) & SIP_NAT_ROUTE)) {
		/* XXX This could block for a long time XXX */
		/* We should only do this if it's a name, not an IP */
		hp = ast_gethostbyname(n, &ahp);
		if (!hp)  {
			ast_log(LOG_WARNING, "Invalid host '%s'\n", n);
			return -1;
		}
		pvt->sa.sin_family = AF_INET;
		memcpy(&pvt->sa.sin_addr, hp->h_addr, sizeof(pvt->sa.sin_addr));
		pvt->sa.sin_port = htons(port);
	} else {
		/* Don't trust the contact field.  Just use what they came to us
		   with. */
		memcpy(&pvt->sa, &pvt->recv, sizeof(pvt->sa));
	}
	return 0;
}


/*--- parse_contact: Parse contact header and save registration ---*/
static int parse_contact(struct sip_pvt *pvt, struct sip_peer *p, struct sip_request *req)
{
	char contact[80]= ""; 
	char data[256];
	char iabuf[INET_ADDRSTRLEN];
	char *expires = get_header(req, "Expires");
	int expiry = atoi(expires);
	char *c, *n, *pt;
	int port;
	char *useragent;
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
		register_peer_exten(p, 0);
		p->fullcontact[0] = '\0';
		p->useragent[0] = '\0';
		p->lastms = 0;
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Unregistered SIP '%s'\n", p->name);
			manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "Peer: SIP/%s\r\nPeerStatus: Unregistered\r\n", p->name);
		return 0;
	}
	strncpy(p->fullcontact, c, sizeof(p->fullcontact) - 1);
	/* For the 200 OK, we should use the received contact */
	snprintf(pvt->our_contact, sizeof(pvt->our_contact) - 1, "<%s>", c);
	/* Make sure it's a SIP URL */
	if (strncasecmp(c, "sip:", 4)) {
		ast_log(LOG_NOTICE, "'%s' is not a valid SIP contact (missing sip:) trying to use anyway\n", c);
	} else
		c += 4;
	/* Ditch q */
	n = strchr(c, ';');
	if (n) {
		*n = '\0';
	}
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
	if (!(ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE)) {
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

	if (c)	/* Overwrite the default username from config at registration */
		strncpy(p->username, c, sizeof(p->username) - 1);
	else
		p->username[0] = '\0';

	if (p->expire > -1)
		ast_sched_del(sched, p->expire);
	if ((expiry < 1) || (expiry > max_expiry))
		expiry = max_expiry;
	if (!ast_test_flag(p, SIP_REALTIME))
		p->expire = ast_sched_add(sched, (expiry + 10) * 1000, expire_register, p);
	else
		p->expire = -1;
	pvt->expiry = expiry;
	snprintf(data, sizeof(data), "%s:%d:%d:%s:%s", ast_inet_ntoa(iabuf, sizeof(iabuf), p->addr.sin_addr), ntohs(p->addr.sin_port), expiry, p->username, p->fullcontact);
	ast_db_put("SIP/Registry", p->name, data);
	manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "Peer: SIP/%s\r\nPeerStatus: Registered\r\n", p->name);
	if (inaddrcmp(&p->addr, &oldsin)) {
		sip_poke_peer(p);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Registered SIP '%s' at %s port %d expires %d\n", p->name, ast_inet_ntoa(iabuf, sizeof(iabuf), p->addr.sin_addr), ntohs(p->addr.sin_port), expiry);
		register_peer_exten(p, 1);
	}

	/* Save User agent */
	useragent = get_header(req, "User-Agent");
	if(useragent && strcasecmp(useragent, p->useragent)) {
		strncpy(p->useragent, useragent, sizeof(p->useragent) - 1);
		if (option_verbose > 3) {
			ast_verbose(VERBOSE_PREFIX_3 "Saved useragent \"%s\" for peer %s\n",p->useragent,p->name);  
		}
	}
	return 0;
}

/*--- free_old_route: Remove route from route list ---*/
static void free_old_route(struct sip_route *route)
{
	struct sip_route *next;
	while (route) {
		next = route->next;
		free(route);
		route = next;
	}
}

/*--- list_route: List all routes - mostly for debugging ---*/
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

/*--- build_route: Build route list from Record-Route header ---*/
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
				strncpy(thishop->hop, rr, len); /* safe */
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
			strncpy(thishop->hop, c, len); /* safe */
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

/*--- md5_hash: Produce MD5 hash of value. Used for authentication ---*/
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

/*--- check_auth: Check user authorization from peer definition ---*/
/*      Some actions, like REGISTER and INVITEs from peers require
        authentication (if peer have secret set) */
static int check_auth(struct sip_pvt *p, struct sip_request *req, char *randdata, int randlen, char *username, char *secret, char *md5secret, char *method, char *uri, int reliable, int ignore)
{
	int res = -1;
	char *response = "407 Proxy Authentication Required";
	char *reqheader = "Proxy-Authorization";
	char *respheader = "Proxy-Authenticate";
	char *authtoken;
#ifdef OSP_SUPPORT
	char tmp[80];
	char *osptoken;
	unsigned int osptimelimit;
#endif
	/* Always OK if no secret */
	if (ast_strlen_zero(secret) && ast_strlen_zero(md5secret)
#ifdef OSP_SUPPORT
	    && ast_test_flag(p, SIP_OSPAUTH)
#endif
		)
		return 0;
	if (!strcasecmp(method, "REGISTER")) {
		/* On a REGISTER, we have to use 401 and its family of headers instead of 407 and its family
		   of headers -- GO SIP!  Whoo hoo!  Two things that do the same thing but are used in
		   different circumstances! What a surprise. */
		response = "401 Unauthorized";
		reqheader = "Authorization";
		respheader = "WWW-Authenticate";
	}
#ifdef OSP_SUPPORT
	else if (ast_test_flag(p, SIP_OSPAUTH)) {
		ast_log(LOG_DEBUG, "Checking OSP Authentication!\n");
		osptoken = get_header(req, "P-OSP-Auth-Token");
		/* Check for token existence */
		if (!strlen(osptoken))
			return -1;
		/* Validate token */
		if (ast_osp_validate(NULL, osptoken, &p->osphandle, &osptimelimit, p->cid_num, p->sa.sin_addr, p->exten) < 1)
			return -1;
		
		snprintf(tmp, sizeof(tmp), "%d", p->osphandle);
		pbx_builtin_setvar_helper(p->owner, "_OSPHANDLE", tmp);


		/* If ospauth is 'exclusive' don't require further authentication */
		if ((ast_test_flag(p, SIP_OSPAUTH) == SIP_OSPAUTH_EXCLUSIVE) ||
		    (ast_strlen_zero(secret) && ast_strlen_zero(md5secret)))
			return 0;
	}
#endif	
	authtoken =  get_header(req, reqheader);	
	if (ignore && !ast_strlen_zero(randdata) && ast_strlen_zero(authtoken)) {
		/* This is a retransmitted invite/register/etc, don't reconstruct authentication
		   information */
		if (!ast_strlen_zero(randdata)) {
			if (!reliable) {
				/* Resend message if this was NOT a reliable delivery.   Otherwise the
				   retransmission should get it */
				transmit_response_with_auth(p, response, req, randdata, reliable, respheader);
				/* Schedule auto destroy in 15 seconds */
				sip_scheddestroy(p, 15000);
			}
			res = 1;
		}
	} else if (ast_strlen_zero(randdata) || ast_strlen_zero(authtoken)) {
		snprintf(randdata, randlen, "%08x", rand());
		transmit_response_with_auth(p, response, req, randdata, reliable, respheader);
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
		strncpy(tmp, authtoken, sizeof(tmp) - 1);
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
		snprintf(a1, sizeof(a1), "%s:%s:%s", username, global_realm, secret);
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

/*--- cb_extensionstate: Part of thte SUBSCRIBE support subsystem ---*/
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

/*--- register_verify: Verify registration of user */
static int register_verify(struct sip_pvt *p, struct sockaddr_in *sin, struct sip_request *req, char *uri, int ignore)
{
	int res = -1;
	struct sip_peer *peer;
	char tmp[256] = "";
	char iabuf[INET_ADDRSTRLEN];
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
		ast_log(LOG_NOTICE, "Invalid to address: '%s' from %s (missing sip:) trying to use anyway...\n", c, ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr));
	}
	c = strchr(name, '@');
	if (c) 
		*c = '\0';
	strncpy(p->exten, name, sizeof(p->exten) - 1);
	build_contact(p);
	peer = find_peer(name, NULL, 1);
	if (!(peer && ast_apply_ha(peer->ha, sin))) {
		if (peer)
			ASTOBJ_UNREF(peer,sip_destroy_peer);
	}
	if (peer) {
			if (!ast_test_flag(peer, SIP_DYNAMIC)) {
				ast_log(LOG_NOTICE, "Peer '%s' is trying to register, but not configured as host=dynamic\n", peer->name);
			} else {
				ast_copy_flags(p, peer, SIP_NAT);
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
			ASTOBJ_CONTAINER_LINK(&peerl, peer);
			peer->lastmsgssent = -1;
			sip_cancel_destroy(p);
			if (parse_contact(p, peer, req)) {
				ast_log(LOG_WARNING, "Failed to parse contact info\n");
			} else {
				/* Say OK and ask subsystem to retransmit msg counter */
				manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "Peer: SIP/%s\r\nPeerStatus: Registered\r\n", peer->name);
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
		transmit_response(p, "403 Forbidden", &p->initreq);
	if (peer)
		ASTOBJ_UNREF(peer,sip_destroy_peer);
	return res;
}

/*--- get_rdnis: get referring dnis ---*/
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

/*--- get_destination: Find out who the call is for --*/
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
	if (pedanticsipchecking)
		url_decode(c);
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

/*--- hex2int: Convert hex code to integer ---*/
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

/*--- get_refer_info: Call transfer support (new standard) ---*/
static int get_refer_info(struct sip_pvt *p, struct sip_request *oreq)
{
	char tmp[256] = "", *c, *a;
	char tmp2[256] = "", *c2, *a2;
	char tmp3[256];
	char tmp4[256];
	char tmp5[256] = "";		/* CallID to replace */
	struct sip_request *req;
	struct sip_pvt *p2;
	struct ast_channel *chan = NULL, *peer = NULL;

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
		p->refer_call = NULL;
		ast_mutex_lock(&iflock);
		/* Search interfaces and find the match */
		p2 = iflist;
		while(p2) {
			if (!strcmp(p2->callid, tmp5)) {
				/* Go ahead and lock it (and its owner) before returning */
				ast_mutex_lock(&p2->lock);
				if (p2->owner) {
					while(ast_mutex_trylock(&p2->owner->lock)) {
						ast_mutex_unlock(&p2->lock);
						usleep(1);
						ast_mutex_lock(&p2->lock);
						if (!p2->owner)
							break;
					}
				}
				p->refer_call = p2;
				break;
			}
			p2 = p2->next;
		}
		ast_mutex_unlock(&iflock);
		if (p->refer_call) {
			if (p->refer_call == p) {
				ast_log(LOG_NOTICE, "Supervised transfer attempted to transfer into same call id (%s == %s)!\n", tmp5, p->callid);
				p->refer_call = NULL;
			} else
				return 0;
		} else
			ast_log(LOG_NOTICE, "Supervised transfer requested, but unable to find callid '%s'\n", tmp5);
	} else if (ast_exists_extension(NULL, p->context, c, 1, NULL) || !strcmp(c, ast_parking_ext())) {
		/* This is an unsupervised transfer */
		
		ast_log(LOG_DEBUG,"Assigning Extension %s to REFER-TO\n", c);
		ast_log(LOG_DEBUG,"Assigning Extension %s to REFERRED-BY\n", c2);
		ast_log(LOG_DEBUG,"Assigning Contact Info %s to REFER_CONTACT\n", tmp3);
		strncpy(p->refer_to, c, sizeof(p->refer_to) - 1);
		strncpy(p->referred_by, c2, sizeof(p->referred_by) - 1);
		strncpy(p->refer_contact, tmp3, sizeof(p->refer_contact) - 1);
		p->refer_call = NULL;
		if((chan = p->owner) && (peer = ast_bridged_channel(p->owner))) {
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", peer->name);
			pbx_builtin_setvar_helper(peer, "BLINDTRANSFER", chan->name);
		}
		return 0;
	} else if (ast_canmatch_extension(NULL, p->context, c, 1, NULL)) {
		return 1;
	}

	return -1;
}

/*--- get_also_info: Call transfer support (old way, depreciated)--*/
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
		p->refer_call = NULL;
		return 0;
	} else if (ast_canmatch_extension(NULL, p->context, c, 1, NULL)) {
		return 1;
	}

	return -1;
}

/*--- check_via: check Via: headers ---*/
static int check_via(struct sip_pvt *p, struct sip_request *req)
{
	char via[256] = "";
	char iabuf[INET_ADDRSTRLEN];
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
		c = strstr(via, ";rport");
		if (c && (c[6] != '='))
			ast_set_flag(p, SIP_NAT_ROUTE);
		if (sip_debug_test_pvt(p)) {
			if (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE)
				ast_verbose("Sending to %s : %d (NAT)\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr), ntohs(p->sa.sin_port));
			else
				ast_verbose("Sending to %s : %d (non-NAT)\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr), ntohs(p->sa.sin_port));
		}
	}
	return 0;
}

/*--- get_calleridname: Get caller id name from SIP headers ---*/
static char *get_calleridname(char *input,char *output, size_t outputsize)
{
	char *end = strchr(input,'<');
	char *tmp = strchr(input,'\"');
	int bytes = 0;
	int maxbytes = outputsize - 1;

	if (!end || (end == input)) return NULL;
	/* move away from "<" */
	end--;
	/* we found "name" */
	if (tmp && tmp < end) {
		end = strchr(tmp+1,'\"');
		if (!end) return NULL;
		bytes = (int)(end-tmp-1);
		/* protect the output buffer */
		if (bytes > maxbytes) {
			bytes = maxbytes;
		}
		strncpy(output, tmp+1, bytes); /* safe */
		output[maxbytes] = '\0';
	} else {
		/* we didn't find "name" */
		/* clear the empty characters in the begining*/
		while(*input && (*input < 33))
			input++;
		/* clear the empty characters in the end */
		while(*end && (*end < 33) && end > input)
			end--;
		if (end >= input) {
			bytes = (int)(end-input)+1;
			/* protect the output buffer */
			if (bytes > maxbytes) {
				bytes = maxbytes;
			}
			strncpy(output, input, bytes); /* safe */
			output[maxbytes] = '\0';
		}
		else
			return(NULL);
	}
	return output;
}

/*--- get_rpid_num: Get caller id number from Remote-Party-ID header field 
 *	Returns true if number should be restricted (privacy setting found)
 *	output is set to NULL if no number found
 */
static int get_rpid_num(char *input,char *output, int maxlen)
{
	char *start;
	char *end;

	start = strchr(input,':');
	if (!start) {
		output[0] = '\0';
		return 0;
	}
	start++;

	/* we found "number" */
	strncpy(output,start,maxlen-1);
	output[maxlen-1] = '\0';

	end = strchr(output,'@');
	if (end)
		*end = '\0';
	else
		output[0] = '\0';
	if(strstr(input,"privacy=full") || strstr(input,"privacy=uri"))
		return AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;

	return 0;
}


/*--- check_user: Check if matching user or peer is defined ---*/
static int check_user_full(struct sip_pvt *p, struct sip_request *req, char *cmd, char *uri, int reliable, struct sockaddr_in *sin, int ignore, char *mailbox, int mailboxlen)
{
	struct sip_user *user;
	struct sip_peer *peer;
	char *of, from[256] = "", *c;
	char *rpid,rpid_num[50];
	char iabuf[INET_ADDRSTRLEN];
	int res = 0;
	char *t;
	char calleridname[50];
	int debug=sip_debug_test_addr(sin);
	struct ast_variable *tmpvar = NULL, *v = NULL;

	/* Terminate URI */
	t = uri;
	while(*t && (*t > 32) && (*t != ';'))
		t++;
	*t = '\0';
	of = get_header(req, "From");
	strncpy(from, of, sizeof(from) - 1);
	memset(calleridname,0,sizeof(calleridname));
	get_calleridname(from, calleridname, sizeof(calleridname));

	rpid = get_header(req, "Remote-Party-ID");
	memset(rpid_num,0,sizeof(rpid_num));
	if(!ast_strlen_zero(rpid)) 
		p->callingpres = get_rpid_num(rpid,rpid_num, sizeof(rpid_num));

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
	strncpy(p->cid_num, of, sizeof(p->cid_num) - 1);
	ast_shrink_phone_number(p->cid_num);
	if (*calleridname)
		strncpy(p->cid_name, calleridname, sizeof(p->cid_name) - 1);
	if (ast_strlen_zero(of))
		return 0;
	user = find_user(of, 1);
	/* Find user based on user name in the from header */
	if (!mailbox && user && ast_apply_ha(user->ha, sin)) {
		ast_copy_flags(p, user, SIP_TRUSTRPID | SIP_USECLIENTCODE | SIP_NAT | SIP_PROG_INBAND | SIP_OSPAUTH);
		/* copy channel vars */
		for (v = user->chanvars ; v ; v = v->next) {
			if((tmpvar = ast_variable_new(v->name, v->value))) {
				tmpvar->next = p->chanvars; 
				p->chanvars = tmpvar;
			}
		}
		p->prefs = user->prefs;
		/* replace callerid if rpid found, and not restricted */
		if(!ast_strlen_zero(rpid_num) && ast_test_flag(p, SIP_TRUSTRPID)) {
			if (*calleridname)
				strncpy(p->cid_name, calleridname, sizeof(p->cid_name) - 1);
			strncpy(p->cid_num, rpid_num, sizeof(p->cid_num) - 1);
			ast_shrink_phone_number(p->cid_num);
		}

		if (p->rtp) {
			ast_log(LOG_DEBUG, "Setting NAT on RTP to %d\n", (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
			ast_rtp_setnat(p->rtp, (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
		}
		if (p->vrtp) {
			ast_log(LOG_DEBUG, "Setting NAT on VRTP to %d\n", (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
			ast_rtp_setnat(p->vrtp, (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
		}
		if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), user->name, user->secret, user->md5secret, cmd, uri, reliable, ignore))) {
			sip_cancel_destroy(p);
			ast_copy_flags(p, user, SIP_PROMISCREDIR | SIP_DTMF | SIP_REINVITE);
			if (!ast_strlen_zero(user->context))
				strncpy(p->context, user->context, sizeof(p->context) - 1);
			if (!ast_strlen_zero(user->cid_num) && !ast_strlen_zero(p->cid_num))  {
				strncpy(p->cid_num, user->cid_num, sizeof(p->cid_num) - 1);
				ast_shrink_phone_number(p->cid_num);
			}
			if (!ast_strlen_zero(user->cid_name) && !ast_strlen_zero(p->cid_num)) 
				strncpy(p->cid_name, user->cid_name, sizeof(p->cid_name) - 1);
			strncpy(p->username, user->name, sizeof(p->username) - 1);
			strncpy(p->peersecret, user->secret, sizeof(p->peersecret) - 1);
			strncpy(p->peermd5secret, user->md5secret, sizeof(p->peermd5secret) - 1);
			strncpy(p->accountcode, user->accountcode, sizeof(p->accountcode)  -1);
			strncpy(p->language, user->language, sizeof(p->language)  -1);
			strncpy(p->musicclass, user->musicclass, sizeof(p->musicclass)  -1);
			p->amaflags = user->amaflags;
			p->callgroup = user->callgroup;
			p->pickupgroup = user->pickupgroup;
			p->callingpres = user->callingpres;
			p->capability = user->capability;
			p->jointcapability = user->capability;
			if (p->peercapability)
				p->jointcapability &= p->peercapability;
			if (ast_test_flag(p, SIP_DTMF) == SIP_DTMF_RFC2833)
				p->noncodeccapability |= AST_RTP_DTMF;
			else
				p->noncodeccapability &= ~AST_RTP_DTMF;
		}
		if (user && debug)
			ast_verbose("Found user '%s'\n", user->name);
	} else {
		if (user) {
			if (!mailbox && debug)
				ast_verbose("Found user '%s', but fails host access\n", user->name);
			ASTOBJ_UNREF(user,sip_destroy_user);
		}
		user = NULL;
	}

	if (!user) {
		/* If we didn't find a user match, check for peers */
		/* Look for peer based on the IP address we received data from */
		/* If peer is registred from this IP address or have this as a default
		   IP address, this call is from the peer 
 		*/
		peer = find_peer(NULL, &p->recv, 1);
		if (peer) {
			if (debug)
				ast_verbose("Found peer '%s'\n", peer->name);
			/* Take the peer */
			ast_copy_flags(p, peer, SIP_TRUSTRPID | SIP_USECLIENTCODE | SIP_NAT | SIP_PROG_INBAND | SIP_OSPAUTH);
			/* replace callerid if rpid found, and not restricted */
			if(!ast_strlen_zero(rpid_num) && ast_test_flag(p, SIP_TRUSTRPID)) {
				if (*calleridname)
					strncpy(p->cid_name, calleridname, sizeof(p->cid_name) - 1);
				strncpy(p->cid_num, rpid_num, sizeof(p->cid_num) - 1);
				ast_shrink_phone_number(p->cid_num);
			}
			if (p->rtp) {
				ast_log(LOG_DEBUG, "Setting NAT on RTP to %d\n", (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
				ast_rtp_setnat(p->rtp, (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
			}
			if (p->vrtp) {
				ast_log(LOG_DEBUG, "Setting NAT on VRTP to %d\n", (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
				ast_rtp_setnat(p->vrtp, (ast_test_flag(p, SIP_NAT) & SIP_NAT_ROUTE));
			}
			strncpy(p->peersecret, peer->secret, sizeof(p->peersecret)-1);
			p->peersecret[sizeof(p->peersecret)-1] = '\0';
			strncpy(p->peermd5secret, peer->md5secret, sizeof(p->peermd5secret)-1);
			p->peermd5secret[sizeof(p->peermd5secret)-1] = '\0';
			p->callingpres = peer->callingpres;
			if (ast_test_flag(peer, SIP_INSECURE) == SIP_INSECURE_VERY) {
				/* Pretend there is no required authentication if insecure is "very" */
				p->peersecret[0] = '\0';
				p->peermd5secret[0] = '\0';
			}
			if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), peer->name, p->peersecret, p->peermd5secret, cmd, uri, reliable, ignore))) {
				ast_copy_flags(p, peer, SIP_PROMISCREDIR | SIP_DTMF | SIP_REINVITE);
				strncpy(p->peername, peer->name, sizeof(p->peername) - 1);
				strncpy(p->authname, peer->name, sizeof(p->authname) - 1);
				/* copy channel vars */
				for (v = peer->chanvars ; v ; v = v->next) {
					if((tmpvar = ast_variable_new(v->name, v->value))) {
						tmpvar->next = p->chanvars; 
						p->chanvars = tmpvar;
					}
				}
				if (mailbox)
					snprintf(mailbox, mailboxlen, ",%s,", peer->mailbox);
				if (!ast_strlen_zero(peer->username)) {
					strncpy(p->username, peer->username, sizeof(p->username) - 1);
					/* Use the default username for authentication on outbound calls */
					strncpy(p->authname, peer->username, sizeof(p->authname) - 1);
				}
				if (!ast_strlen_zero(peer->cid_num) && !ast_strlen_zero(p->cid_num))  {
					strncpy(p->cid_num, peer->cid_num, sizeof(p->cid_num) - 1);
					ast_shrink_phone_number(p->cid_num);
				}
				if (!ast_strlen_zero(peer->cid_name) && !ast_strlen_zero(p->cid_name)) 
					strncpy(p->cid_name, peer->cid_name, sizeof(p->cid_name) - 1);
				strncpy(p->fullcontact, peer->fullcontact, sizeof(p->fullcontact) - 1);
				if (!ast_strlen_zero(peer->context))
					strncpy(p->context, peer->context, sizeof(p->context) - 1);
				strncpy(p->peersecret, peer->secret, sizeof(p->peersecret) - 1);
				strncpy(p->peermd5secret, peer->md5secret, sizeof(p->peermd5secret) - 1);
				strncpy(p->language, peer->language, sizeof(p->language)  -1);
				strncpy(p->accountcode, peer->accountcode, sizeof(p->accountcode) - 1);
				p->amaflags = peer->amaflags;
				p->callgroup = peer->callgroup;
				p->pickupgroup = peer->pickupgroup;
				p->capability = peer->capability;
				p->jointcapability = peer->capability;
				if (p->peercapability)
					p->jointcapability &= p->peercapability;
				if (ast_test_flag(p, SIP_DTMF) == SIP_DTMF_RFC2833)
					p->noncodeccapability |= AST_RTP_DTMF;
				else
					p->noncodeccapability &= ~AST_RTP_DTMF;
			}
			ASTOBJ_UNREF(peer,sip_destroy_peer);
		} else { 
			if (debug)
				ast_verbose("Found no matching peer or user for '%s:%d'\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr), ntohs(p->recv.sin_port));

			/* do we allow guests? */
			if (!global_allowguest)
				res = -1;  /* we don't want any guests, authentication will fail */
#ifdef OSP_SUPPORT			
			else if (global_allowguest == 2) {
				ast_copy_flags(p, &global_flags, SIP_OSPAUTH);
				res = check_auth(p, req, p->randdata, sizeof(p->randdata), "", "", "", cmd, uri, reliable, ignore); 
			}
#endif
		}

	}

	if (user)
		ASTOBJ_UNREF(user,sip_destroy_user);
	return res;
}

/*--- check_user: Find user ---*/
static int check_user(struct sip_pvt *p, struct sip_request *req, char *cmd, char *uri, int reliable, struct sockaddr_in *sin, int ignore)
{
	return check_user_full(p, req, cmd, uri, reliable, sin, ignore, NULL, 0);
}

/*--- get_msg_text: Get text out of a SIP MESSAGE packet ---*/
static int get_msg_text(char *buf, int len, struct sip_request *req)
{
	int x;
	int y;

	buf[0] = '\0';
	y = len - strlen(buf) - 5;
	if (y < 0)
		y = 0;
	for (x=0;x<req->lines;x++) {
		strncat(buf, req->line[x], y); /* safe */
		y -= strlen(req->line[x]) + 1;
		if (y < 0)
			y = 0;
		if (y != 0)
			strcat(buf, "\n"); /* safe */
	}
	return 0;
}

                
/*--- receive_message: Receive SIP MESSAGE method messages ---*/
/*   we handle messages within current calls currently */
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

/*--- sip_show_inuse: CLI Command to show calls within limits set by 
      outgoinglimit and incominglimit ---*/
static int sip_show_inuse(int fd, int argc, char *argv[]) {
#define FORMAT  "%-25.25s %-15.15s %-15.15s %-15.15s %-15.15s\n"
#define FORMAT2 "%-25.25s %-15.15s %-15.15s %-15.15s %-15.15s\n"
	char ilimits[40] = "";
	char olimits[40] = "";
	char iused[40];
	char oused[40];

	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT, "Username", "incoming", "Limit","outgoing","Limit");
	ASTOBJ_CONTAINER_TRAVERSE(&userl, 1, do {
		ASTOBJ_RDLOCK(iterator);
		if (iterator->incominglimit)
			snprintf(ilimits, sizeof(ilimits), "%d", iterator->incominglimit);
		else
			strncpy(ilimits, "N/A", sizeof(ilimits) - 1);
		if (iterator->outgoinglimit)
			snprintf(olimits, sizeof(olimits), "%d", iterator->outgoinglimit);
		else
			strncpy(olimits, "N/A", sizeof(olimits) - 1);
		snprintf(iused, sizeof(iused), "%d", iterator->inUse);
		snprintf(oused, sizeof(oused), "%d", iterator->outUse);
		ast_cli(fd, FORMAT2, iterator->name, iused, ilimits,oused,olimits);
		ASTOBJ_UNLOCK(iterator);
	} while (0) );
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char *nat2str(int nat)
{
	switch(nat) {
	case SIP_NAT_NEVER:
		return "No";
	case SIP_NAT_ROUTE:
		return "Route";
	case SIP_NAT_ALWAYS:
		return "Always";
	case SIP_NAT_RFC3581:
		return "RFC3581";
	default:
		return "Unknown";
	}
}
                           
/*--- sip_show_users: CLI Command 'SIP Show Users' ---*/
static int sip_show_users(int fd, int argc, char *argv[])
{
	regex_t regexbuf;
	int havepattern = 0;

#define FORMAT  "%-25.25s  %-15.15s  %-15.15s  %-15.15s  %-5.5s%-10.10s\n"

	if (argc > 4)
		return RESULT_SHOWUSAGE;
	
	if (argc == 4) {
		if (regcomp(&regexbuf, argv[3], REG_EXTENDED | REG_NOSUB))
			return RESULT_SHOWUSAGE;

		havepattern = 1;
	}

	ast_cli(fd, FORMAT, "Username", "Secret", "Accountcode", "Def.Context", "ACL", "NAT");
	ASTOBJ_CONTAINER_TRAVERSE(&userl, 1, do {
		ASTOBJ_RDLOCK(iterator);

		if (havepattern && regexec(&regexbuf, iterator->name, 0, NULL, 0)) {
			ASTOBJ_UNLOCK(iterator);
			continue;
		}

		ast_cli(fd, FORMAT, iterator->name, 
			iterator->secret, 
			iterator->accountcode,
			iterator->context,
			iterator->ha ? "Yes" : "No",
			nat2str(ast_test_flag(iterator, SIP_NAT)));
		ASTOBJ_UNLOCK(iterator);
	} while (0)
	);

	if (havepattern)
		regfree(&regexbuf);

	return RESULT_SUCCESS;
#undef FORMAT
}

/*--- sip_show_peers: CLI Show Peers command */
static int sip_show_peers(int fd, int argc, char *argv[])
{
	regex_t regexbuf;
	int havepattern = 0;

#define FORMAT2 "%-25.25s  %-15.15s %-3.3s %-3.3s %-3.3s %-15.15s  %-8s %-10s\n"
#define FORMAT  "%-25.25s  %-15.15s %-3.3s %-3.3s %-3.3s %-15.15s  %-8d %-10s\n"

	char name[256] = "";
	char iabuf[INET_ADDRSTRLEN];
	int total_peers = 0;
	int peers_online = 0;
	int peers_offline = 0;

	
	if (argc > 4)
		return RESULT_SHOWUSAGE;
	
	if (argc == 4) {
		if (regcomp(&regexbuf, argv[3], REG_EXTENDED | REG_NOSUB))
			return RESULT_SHOWUSAGE;

		havepattern = 1;
	}

	ast_cli(fd, FORMAT2, "Name/username", "Host", "Dyn", "Nat", "ACL", "Mask", "Port", "Status");
	
	ASTOBJ_CONTAINER_TRAVERSE(&peerl, 1, do {
		char nm[20] = "";
		char status[20] = "";
		char srch[2000];
		
		ASTOBJ_RDLOCK(iterator);

		if (havepattern && regexec(&regexbuf, iterator->name, 0, NULL, 0)) {
			ASTOBJ_UNLOCK(iterator);
			continue;
		}

		ast_inet_ntoa(nm, sizeof(nm), iterator->mask);
		if (!ast_strlen_zero(iterator->username))
			snprintf(name, sizeof(name), "%s/%s", iterator->name, iterator->username);
		else
			strncpy(name, iterator->name, sizeof(name) - 1);
		if (iterator->maxms) {
			if (iterator->lastms < 0) {
				strncpy(status, "UNREACHABLE", sizeof(status) - 1);
				peers_offline++;
			} else if (iterator->lastms > iterator->maxms) {
				snprintf(status, sizeof(status), "LAGGED (%d ms)", iterator->lastms);
				peers_online++;
			} else if (iterator->lastms) {
				snprintf(status, sizeof(status), "OK (%d ms)", iterator->lastms);
				peers_online++;
			} else {
				/* Checking if port is 0 */
				if ( ntohs(iterator->addr.sin_port) == 0 ) { 
					peers_offline++;
				} else {
					peers_online++;
				}
				strncpy(status, "UNKNOWN", sizeof(status) - 1);
			}
		} else { 
			strncpy(status, "Unmonitored", sizeof(status) - 1);
			/* Checking if port is 0 */
			if ( ntohs(iterator->addr.sin_port) == 0 ) {
				peers_offline++;
			} else {
				peers_online++;
			}
		}			
		
		snprintf(srch, sizeof(srch), FORMAT, name,
			iterator->addr.sin_addr.s_addr ? ast_inet_ntoa(iabuf, sizeof(iabuf), iterator->addr.sin_addr) : "(Unspecified)",
			ast_test_flag(iterator, SIP_DYNAMIC) ? " D " : "   ", 	/* Dynamic or not? */
			(ast_test_flag(iterator, SIP_NAT) & SIP_NAT_ROUTE) ? " N " : "   ",	/* NAT=yes? */
			iterator->ha ? " A " : "   ", 	/* permit/deny */
			nm, ntohs(iterator->addr.sin_port), status);

		ast_cli(fd, FORMAT, name, 
			iterator->addr.sin_addr.s_addr ? ast_inet_ntoa(iabuf, sizeof(iabuf), iterator->addr.sin_addr) : "(Unspecified)",
			ast_test_flag(iterator, SIP_DYNAMIC) ? " D " : "   ",  /* Dynamic or not? */
			(ast_test_flag(iterator, SIP_NAT) & SIP_NAT_ROUTE) ? " N " : "   ",	/* NAT=yes? */
			iterator->ha ? " A " : "   ",       /* permit/deny */
			nm,
			ntohs(iterator->addr.sin_port), status);

		ASTOBJ_UNLOCK(iterator);

		total_peers++;
	} while(0) );

	ast_cli(fd,"%d sip peers [%d online , %d offline]\n",total_peers,peers_online,peers_offline);

	if (havepattern)
		regfree(&regexbuf);

	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int sip_show_objects(int fd, int argc, char *argv[])
{
	char tmp[256];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "-= User objects: %d static, %d realtime =-\n\n", suserobjs, ruserobjs);
	ASTOBJ_CONTAINER_DUMP(fd, tmp, sizeof(tmp), &userl);
	ast_cli(fd, "-= Peer objects: %d static, %d realtime, %d autocreate =-\n\n", speerobjs, rpeerobjs, apeerobjs);
	ASTOBJ_CONTAINER_DUMP(fd, tmp, sizeof(tmp), &peerl);
	ast_cli(fd, "-= Registry objects: %d =-\n\n", regobjs);
	ASTOBJ_CONTAINER_DUMP(fd, tmp, sizeof(tmp), &regl);
	return RESULT_SUCCESS;
}
/*--- print_group: Print call group and pickup group ---*/
static void  print_group(int fd, unsigned int group) 
{
	char buf[256];
	ast_cli(fd, ast_print_group(buf, sizeof(buf), group) );
}

/*--- dtmfmode2str: Convert DTMF mode to printable string ---*/
static const char *dtmfmode2str(int mode)
{
	switch (mode) {
	case SIP_DTMF_RFC2833:
		return "rfc2833";
	case SIP_DTMF_INFO:
		return "info";
	case SIP_DTMF_INBAND:
		return "inband";
	}
	return "<error>";
}

/*--- insecure2str: Convert Insecure setting to printable string ---*/
static const char *insecure2str(int mode)
{
	switch (mode) {
	case SIP_SECURE:
		return "no";
	case SIP_INSECURE_NORMAL:
		return "yes";
	case SIP_INSECURE_VERY:
		return "very";
	}
	return "<error>";
}

/*--- sip_prune_realtime: Remove temporary realtime object from memory (CLI) ---*/
static int sip_prune_realtime(int fd, int argc, char *argv[])
{
	struct sip_peer *peer;

	if (argc != 4)
        	return RESULT_SHOWUSAGE;

	if (!strcmp(argv[3],"all")) {
		sip_do_reload();
		ast_cli(fd, "OK. Cache is flushed.\n");
	} else if ((peer = find_peer(argv[3], NULL, 0))) {
		if(ast_test_flag((&peer->flags_page2), SIP_PAGE2_RTCACHEFRIENDS)) {
			ast_set_flag((&peer->flags_page2), SIP_PAGE2_RTAUTOCLEAR);
			expire_register(peer);
			ast_cli(fd, "OK. Peer %s was removed from the cache.\n", argv[3]);
		} else {
			ast_cli(fd, "SORRY. Peer %s is not eligible for this operation.\n", argv[3]);
		}
	} else {
		ast_cli(fd, "SORRY. Peer %s was not found in the cache.\n", argv[3]);
	}
	
	return RESULT_SUCCESS;
}

/*--- sip_show_peer: Show one peer in detail ---*/
static int sip_show_peer(int fd, int argc, char *argv[])
{
	char status[30] = "";
	char cbuf[256];
	char iabuf[INET_ADDRSTRLEN];
	struct sip_peer *peer;
	char codec_buf[512];
	struct ast_codec_pref *pref;
	struct ast_variable *v;
	int x = 0, codec = 0, load_realtime = 0;

	if (argc < 4)

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	load_realtime = (argc == 5 && !strcmp(argv[4], "load")) ? 1 : 0;
	peer = find_peer(argv[3], NULL, load_realtime);
	if (peer) {
		ast_cli(fd,"\n\n");
		ast_cli(fd, "  * Name       : %s\n", peer->name);
		ast_cli(fd, "  Secret       : %s\n", ast_strlen_zero(peer->secret)?"<Not set>":"<Set>");
		ast_cli(fd, "  MD5Secret    : %s\n", ast_strlen_zero(peer->md5secret)?"<Not set>":"<Set>");
		ast_cli(fd, "  Context      : %s\n", peer->context);
		ast_cli(fd, "  Language     : %s\n", peer->language);
		if (!ast_strlen_zero(peer->accountcode))
			ast_cli(fd, "  Accountcode  : %s\n", peer->accountcode);
		ast_cli(fd, "  AMA flag     : %s\n", ast_cdr_flags2str(peer->amaflags));
		if (!ast_strlen_zero(peer->fromuser))
			ast_cli(fd, "  FromUser     : %s\n", peer->fromuser);
		if (!ast_strlen_zero(peer->fromdomain))
			ast_cli(fd, "  FromDomain   : %s\n", peer->fromdomain);
		ast_cli(fd, "  Callgroup    : ");
		print_group(fd, peer->callgroup);
		ast_cli(fd, "  Pickupgroup  : ");
		print_group(fd, peer->pickupgroup);
		ast_cli(fd, "  Mailbox      : %s\n", peer->mailbox);
		ast_cli(fd, "  LastMsgsSent : %d\n", peer->lastmsgssent);
		ast_cli(fd, "  Inc. limit   : %d\n", peer->incominglimit);
		ast_cli(fd, "  Outg. limit  : %d\n", peer->outgoinglimit);
		ast_cli(fd, "  Dynamic      : %s\n", (ast_test_flag(peer, SIP_DYNAMIC)?"Yes":"No"));
		ast_cli(fd, "  Callerid     : %s\n", ast_callerid_merge(cbuf, sizeof(cbuf), peer->cid_name, peer->cid_num, "<unspecified>"));
		ast_cli(fd, "  Expire       : %d\n", peer->expire);
		ast_cli(fd, "  Expiry       : %d\n", peer->expiry);
		ast_cli(fd, "  Insecure     : %s\n", insecure2str(ast_test_flag(peer, SIP_INSECURE)));
		ast_cli(fd, "  Nat          : %s\n", nat2str(ast_test_flag(peer, SIP_NAT)));
		ast_cli(fd, "  ACL          : %s\n", (peer->ha?"Yes":"No"));
		ast_cli(fd, "  CanReinvite  : %s\n", (ast_test_flag(peer, SIP_CAN_REINVITE)?"Yes":"No"));
		ast_cli(fd, "  PromiscRedir : %s\n", (ast_test_flag(peer, SIP_PROMISCREDIR)?"Yes":"No"));
		ast_cli(fd, "  User=Phone   : %s\n", (ast_test_flag(peer, SIP_USEREQPHONE)?"Yes":"No"));

		/* - is enumerated */
		ast_cli(fd, "  DTMFmode     : %s\n", dtmfmode2str(ast_test_flag(peer, SIP_DTMF)));
		ast_cli(fd, "  LastMsg      : %d\n", peer->lastmsg);
		ast_cli(fd, "  ToHost       : %s\n", peer->tohost);
		ast_cli(fd, "  Addr->IP     : %s Port %d\n",  peer->addr.sin_addr.s_addr ? ast_inet_ntoa(iabuf, sizeof(iabuf), peer->addr.sin_addr) : "(Unspecified)", ntohs(peer->addr.sin_port));
		ast_cli(fd, "  Defaddr->IP  : %s Port %d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), peer->defaddr.sin_addr), ntohs(peer->defaddr.sin_port));
		ast_cli(fd, "  Def. Username: %s\n", peer->username);
		ast_cli(fd, "  Codecs       : ");
		ast_getformatname_multiple(codec_buf, sizeof(codec_buf) -1, peer->capability);
		ast_cli(fd, "%s\n", codec_buf);
		ast_cli(fd, "  Codec Order  : (");
		pref = &peer->prefs;
		for(x = 0; x < 32 ; x++) {
			codec = ast_codec_pref_index(pref,x);
			if(!codec)
				break;
			ast_cli(fd, "%s", ast_getformatname(codec));
			if(x < 31 && ast_codec_pref_index(pref,x+1))
				ast_cli(fd, "|");
		}

		if (!x)
			ast_cli(fd, "none");
		ast_cli(fd, ")\n");

		ast_cli(fd, "  Status       : ");
		if (peer->lastms < 0)
			strncpy(status, "UNREACHABLE", sizeof(status) - 1);
		else if (peer->lastms > peer->maxms)
			snprintf(status, sizeof(status), "LAGGED (%d ms)", peer->lastms);
		else if (peer->lastms)
			snprintf(status, sizeof(status), "OK (%d ms)", peer->lastms);
		else
			strncpy(status, "UNKNOWN", sizeof(status) - 1);
		ast_cli(fd, "%s\n",status);
 		ast_cli(fd, "  Useragent    : %s\n", peer->useragent);
 		ast_cli(fd, "  Full Contact : %s\n", peer->fullcontact);
		if (peer->chanvars) {
 			ast_cli(fd, "  Variables    :\n");
			for (v = peer->chanvars ; v ; v = v->next)
 				ast_cli(fd, "                 %s = %s\n", v->name, v->value);
		}
		ast_cli(fd,"\n");
		ASTOBJ_UNREF(peer,sip_destroy_peer);
	} else {
		ast_cli(fd,"Peer %s not found.\n", argv[3]);
		ast_cli(fd,"\n");
	}

	return RESULT_SUCCESS;
}

/*--- sip_show_user: Show one user in detail ---*/
static int sip_show_user(int fd, int argc, char *argv[])
{
	char cbuf[256];
	struct sip_user *user;
	struct ast_codec_pref *pref;
	struct ast_variable *v;
	int x = 0, codec = 0, load_realtime = 0;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	/* Load from realtime storage? */
	load_realtime = (argc == 5 && !strcmp(argv[4], "load")) ? 1 : 0;

	user = find_user(argv[3], load_realtime);
	if (user) {
		ast_cli(fd,"\n\n");
		ast_cli(fd, "  * Name       : %s\n", user->name);
		ast_cli(fd, "  Secret       : %s\n", ast_strlen_zero(user->secret)?"<Not set>":"<Set>");
		ast_cli(fd, "  MD5Secret    : %s\n", ast_strlen_zero(user->md5secret)?"<Not set>":"<Set>");
		ast_cli(fd, "  Context      : %s\n", user->context);
		ast_cli(fd, "  Language     : %s\n", user->language);
		if (!ast_strlen_zero(user->accountcode))
			ast_cli(fd, "  Accountcode  : %s\n", user->accountcode);
		ast_cli(fd, "  AMA flag     : %s\n", ast_cdr_flags2str(user->amaflags));
		ast_cli(fd, "  Inc. limit   : %d\n", user->incominglimit);
		ast_cli(fd, "  Outg. limit  : %d\n", user->outgoinglimit);
		ast_cli(fd, "  Callgroup    : ");
		print_group(fd, user->callgroup);
		ast_cli(fd, "  Pickupgroup  : ");
		print_group(fd, user->pickupgroup);
		ast_cli(fd, "  Callerid     : %s\n", ast_callerid_merge(cbuf, sizeof(cbuf), user->cid_name, user->cid_num, "<unspecified>"));
		ast_cli(fd, "  ACL          : %s\n", (user->ha?"Yes":"No"));
		ast_cli(fd, "  Codec Order  : (");
		pref = &user->prefs;
		for(x = 0; x < 32 ; x++) {
			codec = ast_codec_pref_index(pref,x);
			if(!codec)
				break;
			ast_cli(fd, "%s", ast_getformatname(codec));
			if(x < 31 && ast_codec_pref_index(pref,x+1))
				ast_cli(fd, "|");
		}

		if (!x)
			ast_cli(fd, "none");
		ast_cli(fd, ")\n");

		if (user->chanvars) {
 			ast_cli(fd, "  Variables    :\n");
			for (v = user->chanvars ; v ; v = v->next)
 				ast_cli(fd, "                 %s = %s\n", v->name, v->value);
		}
		ast_cli(fd,"\n");
		ASTOBJ_UNREF(user,sip_destroy_user);
	} else {
		ast_cli(fd,"User %s not found.\n", argv[3]);
		ast_cli(fd,"\n");
	}

	return RESULT_SUCCESS;
}

/*--- sip_show_registry: Show SIP Registry (registrations with other SIP proxies ---*/
static int sip_show_registry(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-30.30s  %-12.12s  %8.8s %-20.20s\n"
#define FORMAT  "%-30.30s  %-12.12s  %8d %-20.20s\n"
	char host[80];

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT2, "Host", "Username", "Refresh", "State");
	ASTOBJ_CONTAINER_TRAVERSE(&regl, 1, do {
		ASTOBJ_RDLOCK(iterator);
		snprintf(host, sizeof(host), "%s:%d", iterator->hostname, iterator->portno ? iterator->portno : DEFAULT_SIP_PORT);
		ast_cli(fd, FORMAT, host, iterator->username, iterator->refresh, regstate2str(iterator->regstate));
		ASTOBJ_UNLOCK(iterator);
	} while(0));
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

/* Forward declaration */
static int __sip_show_channels(int fd, int argc, char *argv[], int subscriptions);

/*--- sip_show_channels: Show active SIP channels ---*/
static int sip_show_channels(int fd, int argc, char *argv[])  
{
        return __sip_show_channels(fd, argc, argv, 0);
}
 
/*--- sip_show_subscriptions: Show active SIP subscriptions ---*/
static int sip_show_subscriptions(int fd, int argc, char *argv[])
{
        return __sip_show_channels(fd, argc, argv, 1);
}

static int __sip_show_channels(int fd, int argc, char *argv[], int subscriptions)
{
#define FORMAT3 "%-15.15s  %-10.10s  %-21.21s  %-15.15s\n"
#define FORMAT2 "%-15.15s  %-10.10s  %-11.11s  %-11.11s   %s	%s\n"
#define FORMAT  "%-15.15s  %-10.10s  %-11.11s  %5.5d/%5.5d   %-6.6s%s	%s\n"
	struct sip_pvt *cur;
	char iabuf[INET_ADDRSTRLEN];
	int numchans = 0;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&iflock);
	cur = iflist;
	if (!subscriptions)
		ast_cli(fd, FORMAT2, "Peer", "User/ANR", "Call ID", "Seq (Tx/Rx)", "Format", "Last Msg");
	else
        	ast_cli(fd, FORMAT3, "Peer", "User", "Call ID", "URI");
	while (cur) {
		if (!cur->subscribed && !subscriptions) {
			ast_cli(fd, FORMAT, ast_inet_ntoa(iabuf, sizeof(iabuf), cur->sa.sin_addr), 
				ast_strlen_zero(cur->username) ? ( ast_strlen_zero(cur->cid_num) ? "(None)" : cur->cid_num ) : cur->username, 
				cur->callid, 
				cur->ocseq, cur->icseq, 
				ast_getformatname(cur->owner ? cur->owner->nativeformats : 0), 
				ast_test_flag(cur, SIP_NEEDDESTROY) ? "(d)" : "",
				cur->lastmsg );
			numchans++;
		}
		if (cur->subscribed && subscriptions) {
                	ast_cli(fd, FORMAT3, ast_inet_ntoa(iabuf, sizeof(iabuf), cur->sa.sin_addr),
				ast_strlen_zero(cur->username) ? ( ast_strlen_zero(cur->cid_num) ? "(None)" : cur->cid_num ) : cur->username, 
                        	cur->callid, cur->uri);

                }
		cur = cur->next;
	}
	ast_mutex_unlock(&iflock);
	if (!subscriptions)
		ast_cli(fd, "%d active SIP channel(s)\n", numchans);
	else
		ast_cli(fd, "%d active SIP subscriptions(s)\n", numchans);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
#undef FORMAT3
}

/*--- complete_sipch: Support routine for 'sip show channel' CLI ---*/
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

/*--- complete_sip_peer: Do completion on peer name ---*/
static char *complete_sip_peer(char *word, int state)
{
	char *result = NULL;
	int wordlen = strlen(word);
	int which = 0;

	ASTOBJ_CONTAINER_TRAVERSE(&peerl, !result, do {
		/* locking of the object is not required because only the name is being compared */
		if (!strncasecmp(word, iterator->name, wordlen)) {
			if (++which > state) {
				result = strdup(iterator->name);
			}
		}
	} while(0) );
	return result;
}

/*--- complete_sip_show_peer: Support routine for 'sip show peer' CLI ---*/
static char *complete_sip_show_peer(char *line, char *word, int pos, int state)
{
	if (pos == 3)
		return complete_sip_peer(word, state);

	return NULL;
}

/*--- complete_sip_debug_peer: Support routine for 'sip debug peer' CLI ---*/
static char *complete_sip_debug_peer(char *line, char *word, int pos, int state)
{
	if (pos == 3)
		return complete_sip_peer(word, state);

	return NULL;
}

/*--- complete_sip_user: Do completion on user name ---*/
static char *complete_sip_user(char *word, int state)
{
	char *result = NULL;
	int wordlen = strlen(word);
	int which = 0;

	ASTOBJ_CONTAINER_TRAVERSE(&userl, !result, do {
		/* locking of the object is not required because only the name is being compared */
		if (!strncasecmp(word, iterator->name, wordlen)) {
			if (++which > state) {
				result = strdup(iterator->name);
			}
		}
	} while(0) );
	return result;
}

/*--- complete_sip_show_user: Support routine for 'sip show user' CLI ---*/
static char *complete_sip_show_user(char *line, char *word, int pos, int state)
{
	if (pos == 3)
		return complete_sip_user(word, state);

	return NULL;
}

/*--- complete_sipnotify: Support routine for 'sip notify' CLI ---*/
static char *complete_sipnotify(char *line, char *word, int pos, int state)
{
	char *c = NULL;

	if (pos == 2) {
		int which = 0;
		char *cat;

		/* do completion for notify type */

		if (!notify_types)
			return NULL;
		
		cat = ast_category_browse(notify_types, NULL);
		while(cat) {
			if (!strncasecmp(word, cat, strlen(word))) {
				if (++which > state) {
					c = strdup(cat);
					break;
				}
			}
			cat = ast_category_browse(notify_types, cat);
		}
		return c;
	}

	if (pos > 2)
		return complete_sip_peer(word, state);

	return NULL;
}

/*--- sip_show_channel: Show details of one call ---*/
static int sip_show_channel(int fd, int argc, char *argv[])
{
	struct sip_pvt *cur;
	char iabuf[INET_ADDRSTRLEN];
	size_t len;
	int found = 0;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	len = strlen(argv[3]);
	ast_mutex_lock(&iflock);
	cur = iflist;
	while(cur) {
		if (!strncasecmp(cur->callid, argv[3],len)) {
			ast_cli(fd,"\n");
			if (cur->subscribed)
				ast_cli(fd, "  * Subscription\n");
			else
				ast_cli(fd, "  * SIP Call\n");
			ast_cli(fd, "  Direction:              %s\n", ast_test_flag(cur, SIP_OUTGOING)?"Outgoing":"Incoming");
			ast_cli(fd, "  Call-ID:                %s\n", cur->callid);
			ast_cli(fd, "  Our Codec Capability:   %d\n", cur->capability);
			ast_cli(fd, "  Non-Codec Capability:   %d\n", cur->noncodeccapability);
			ast_cli(fd, "  Their Codec Capability:   %d\n", cur->peercapability);
			ast_cli(fd, "  Joint Codec Capability:   %d\n", cur->jointcapability);
			ast_cli(fd, "  Format                  %s\n", ast_getformatname(cur->owner ? cur->owner->nativeformats : 0) );
			ast_cli(fd, "  Theoretical Address:    %s:%d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), cur->sa.sin_addr), ntohs(cur->sa.sin_port));
			ast_cli(fd, "  Received Address:       %s:%d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), cur->recv.sin_addr), ntohs(cur->recv.sin_port));
			ast_cli(fd, "  NAT Support:            %s\n", nat2str(ast_test_flag(cur, SIP_NAT)));
			ast_cli(fd, "  Our Tag:                %08d\n", cur->tag);
			ast_cli(fd, "  Their Tag:              %s\n", cur->theirtag);
			ast_cli(fd, "  SIP User agent:         %s\n", cur->useragent);
			if (!ast_strlen_zero(cur->username))
				ast_cli(fd, "  Username:               %s\n", cur->username);
			if (!ast_strlen_zero(cur->peername))
				ast_cli(fd, "  Peername:               %s\n", cur->peername);
			if (!ast_strlen_zero(cur->uri))
				ast_cli(fd, "  Original uri:           %s\n", cur->uri);
			if (!ast_strlen_zero(cur->cid_num))
				ast_cli(fd, "  Caller-ID:              %s\n", cur->cid_num);
			ast_cli(fd, "  Need Destroy:           %d\n", ast_test_flag(cur, SIP_NEEDDESTROY));
			ast_cli(fd, "  Last Message:           %s\n", cur->lastmsg);
			ast_cli(fd, "  Promiscuous Redir:      %s\n", ast_test_flag(cur, SIP_PROMISCREDIR) ? "Yes" : "No");
			ast_cli(fd, "  Route:                  %s\n", cur->route ? cur->route->hop : "N/A");
			ast_cli(fd, "  DTMF Mode:              %s\n\n", dtmfmode2str(ast_test_flag(cur, SIP_DTMF)));
			found++;
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&iflock);
	if (!found) 
		ast_cli(fd, "No such SIP Call ID starting with '%s'\n", argv[3]);
	return RESULT_SUCCESS;
}

/*--- sip_show_channel: Show details of one call ---*/
static int sip_show_history(int fd, int argc, char *argv[])
{
	struct sip_pvt *cur;
	struct sip_history *hist;
	size_t len;
	int x;
	int found = 0;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (!recordhistory)
		ast_cli(fd, "\n***Note: History recording is currently DISABLED.  Use 'sip history' to ENABLE.\n");
	len = strlen(argv[3]);
	ast_mutex_lock(&iflock);
	cur = iflist;
	while(cur) {
		if (!strncasecmp(cur->callid, argv[3],len)) {
			ast_cli(fd,"\n");
			if (cur->subscribed)
				ast_cli(fd, "  * Subscription\n");
			else
				ast_cli(fd, "  * SIP Call\n");
			x = 0;
			hist = cur->history;
			while(hist) {
				x++;
				ast_cli(fd, "%d. %s\n", x, hist->event);
				hist = hist->next;
			}
			if (!x)
				ast_cli(fd, "Call '%s' has no history\n", cur->callid);
			found++;
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&iflock);
	if (!found) 
		ast_cli(fd, "No such SIP Call ID starting with '%s'\n", argv[3]);
	return RESULT_SUCCESS;
}


/*--- receive_info: Receive SIP INFO Message ---*/
/*    Doesn't read the duration of the DTMF signal */
static void receive_info(struct sip_pvt *p, struct sip_request *req)
{
	char buf[1024] = "";
	unsigned int event;
	char resp = 0;
	struct ast_frame f;
	char *c;
	
	/* Need to check the media/type */
	if (!strcasecmp(get_header(req, "Content-Type"), "application/dtmf-relay") ||
	    !strcasecmp(get_header(req, "Content-Type"), "application/vnd.nortelnetworks.digits")) {

		/* Try getting the "signal=" part */
		if (ast_strlen_zero(c = get_sdp(req, "Signal")) && ast_strlen_zero(c = get_sdp(req, "d"))) {
			ast_log(LOG_WARNING, "Unable to retrieve DTMF signal from INFO message from %s\n", p->callid);
			transmit_response(p, "200 OK", req); /* Should return error */
			return;
		} else {
			strncpy(buf, c, sizeof(buf) - 1);
		}
	
		if (p->owner) {	/* PBX call */
			if (!ast_strlen_zero(buf)) {
				if (sipdebug)
					ast_verbose("* DTMF received: '%c'\n", buf[0]);
				if (buf[0] == '*')
					event = 10;
				else if (buf[0] == '#')
					event = 11;
				else if ((buf[0] >= 'A') && (buf[0] <= 'D'))
					event = 12 + buf[0] - 'A';
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
				/* Build DTMF frame and deliver to PBX for transmission to other call leg*/
				memset(&f, 0, sizeof(f));
				f.frametype = AST_FRAME_DTMF;
				f.subclass = resp;
				f.offset = 0;
				f.data = NULL;
				f.datalen = 0;
				ast_queue_frame(p->owner, &f);
			}
		   	transmit_response(p, "200 OK", req);
		   	return;
		} else {
			transmit_response(p, "481 Call leg/transaction does not exist", req);
			ast_set_flag(p, SIP_NEEDDESTROY);
		}
		return;
	} else if ((c = get_header(req, "X-ClientCode"))) {
		/* Client code (from SNOM phone) */
		if (ast_test_flag(p, SIP_USECLIENTCODE)) {
			if (p->owner && p->owner->cdr)
				ast_cdr_setuserfield(p->owner, c);
			if (p->owner && ast_bridged_channel(p->owner) && ast_bridged_channel(p->owner)->cdr)
				ast_cdr_setuserfield(ast_bridged_channel(p->owner), c);
			transmit_response(p, "200 OK", req);
		} else {
			transmit_response(p, "403 Unauthorized", req);
		}
		return;
	}
	/* Other type of INFO message, not really understood by Asterisk */
	/* if (get_msg_text(buf, sizeof(buf), req)) { */

	ast_log(LOG_WARNING, "Unable to parse INFO message from %s. Content %s\n", p->callid, buf);
	transmit_response(p, "415 Unsupported media type", req);
	return;
}

/*--- sip_do_debug: Enable SIP Debugging in CLI ---*/
static int sip_do_debug_ip(int fd, int argc, char *argv[])
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
	if (hp == NULL)  {
		return RESULT_SHOWUSAGE;
	}
	debugaddr.sin_family = AF_INET;
	memcpy(&debugaddr.sin_addr, hp->h_addr, sizeof(debugaddr.sin_addr));
	debugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(fd, "SIP Debugging Enabled for IP: %s\n", ast_inet_ntoa(iabuf, sizeof(iabuf), debugaddr.sin_addr));
	else
		ast_cli(fd, "SIP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), debugaddr.sin_addr), port);
	sipdebug = 1;
	return RESULT_SUCCESS;
}

static int sip_do_debug_peer(int fd, int argc, char *argv[])
{
	struct sip_peer *peer;
	char iabuf[INET_ADDRSTRLEN];
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	peer = find_peer(argv[3], NULL, 1);
	if (peer) {
		if (peer->addr.sin_addr.s_addr) {
			debugaddr.sin_family = AF_INET;
			memcpy(&debugaddr.sin_addr, &peer->addr.sin_addr, sizeof(debugaddr.sin_addr));
			debugaddr.sin_port = peer->addr.sin_port;
			ast_cli(fd, "SIP Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), debugaddr.sin_addr), ntohs(debugaddr.sin_port));
			sipdebug = 1;
		} else
			ast_cli(fd, "Unable to get IP address of peer '%s'\n", argv[3]);
		ASTOBJ_UNREF(peer,sip_destroy_peer);
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

static int sip_notify(int fd, int argc, char *argv[])
{
	struct ast_variable *varlist;
	int i;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (!notify_types) {
		ast_cli(fd, "No %s file found, or no types listed there\n", notify_config);
		return RESULT_FAILURE;
	}

	varlist = ast_variable_browse(notify_types, argv[2]);

	if (!varlist) {
		ast_cli(fd, "Unable to find notify type '%s'\n", argv[2]);
		return RESULT_FAILURE;
	}

	for (i = 3; i < argc; i++) {
		struct sip_pvt *p;
		struct sip_request req;
		struct ast_variable *var;

		p = sip_alloc(NULL, NULL, 0);
		if (!p) {
			ast_log(LOG_WARNING, "Unable to build sip pvt data for notify\n");
			return RESULT_FAILURE;
		}

		if (create_addr(p, argv[i])) {
			/* Maybe they're not registered, etc. */
			sip_destroy(p);
			ast_cli(fd, "Could not create address fo '%s'\n", argv[i]);
			continue;
		}

		initreqprep(&req, p, "NOTIFY", NULL);

		for (var = varlist; var; var = var->next)
			add_header(&req, var->name, var->value);

		/* Recalculate our side, and recalculate Call ID */
		if (ast_sip_ouraddrfor(&p->sa.sin_addr, &p->ourip))
			memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
		build_via(p, p->via, sizeof(p->via));
		build_callid(p->callid, sizeof(p->callid), p->ourip, p->fromdomain);
		ast_cli(fd, "Sending NOTIFY of type '%s' to '%s'\n", argv[2], argv[i]);
		transmit_sip_request(p, &req);
		sip_scheddestroy(p, 15000);
	}

	return RESULT_SUCCESS;
}
/*--- sip_do_history: Enable SIP History logging (CLI) ---*/
static int sip_do_history(int fd, int argc, char *argv[])
{
	if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}
	recordhistory = 1;
	ast_cli(fd, "SIP History Recording Enabled (use 'sip show history')\n");
	return RESULT_SUCCESS;
}

/*--- sip_no_history: Disable SIP History logging (CLI) ---*/
static int sip_no_history(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	recordhistory = 0;
	ast_cli(fd, "SIP History Recording Disabled\n");
	return RESULT_SUCCESS;
}

/*--- sip_no_debug: Disable SIP Debugging in CLI ---*/
static int sip_no_debug(int fd, int argc, char *argv[])

{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	sipdebug = 0;
	ast_cli(fd, "SIP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int reply_digest(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader, char *digest, int digest_len);

/*--- do_register_auth: Authenticate for outbound registration ---*/
static int do_register_auth(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader) 
{
	char digest[1024];
	p->authtries++;
	memset(digest,0,sizeof(digest));
	if (reply_digest(p, req, header, "REGISTER", digest, sizeof(digest))) {
		/* There's nothing to use for authentication */
 		/* No digest challenge in request */
 		if (sip_debug_test_pvt(p) && p->registry)
 			ast_verbose("No authentication challenge, sending blank registration to domain/host name %s\n", p->registry->hostname);
 			/* No old challenge */
		return -1;
	}
 	if (sip_debug_test_pvt(p) && p->registry)
 		ast_verbose("Responding to challenge, registration to domain/host name %s\n", p->registry->hostname);
	return transmit_register(p->registry,"REGISTER",digest, respheader); 
}

/*--- do_proxy_auth: Add authentication on outbound SIP packet ---*/
static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req, char *header, char *respheader, char *msg, int init) 
{
	char digest[1024];
	p->authtries++;
	memset(digest,0,sizeof(digest));
	if (reply_digest(p, req, header, msg, digest, sizeof(digest) )) {
		/* No way to authenticate */
		return -1;
	}
	/* Now we have a reply digest */
	return transmit_invite(p,msg,!strcasecmp(msg, "INVITE"),digest, respheader, NULL,NULL,NULL, 0, init); 
}

/*--- reply_digest: reply to authentication for outbound registrations ---*/
/*      This is used for register= servers in sip.conf, SIP proxies we register
        with  for receiving calls from.  */
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

	/* Save auth data for following registrations */
	if (p->registry) {
		strncpy(p->registry->realm, realm, sizeof(p->realm)-1);
		strncpy(p->registry->nonce, nonce, sizeof(p->nonce)-1);
		strncpy(p->registry->domain, domain, sizeof(p->domain)-1);
		strncpy(p->registry->opaque, opaque, sizeof(p->opaque)-1);
		strncpy(p->registry->qop, qop, sizeof(p->qop)-1);
	}
	build_reply_digest(p, orig_header, digest, digest_len); 
	return 0;
}

/*--- build_reply_digest:  Build reply digest ---*/
/*      Build digest challenge for authentication of peers (for registration) 
	and users (for calls). Also used for authentication of CANCEL and BYE */
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
	char iabuf[INET_ADDRSTRLEN];

	if (!ast_strlen_zero(p->domain))
		strncpy(uri, p->domain, sizeof(uri) - 1);
	else if (!ast_strlen_zero(p->uri))
		strncpy(uri, p->uri, sizeof(uri) - 1);
	else
		snprintf(uri, sizeof(uri), "sip:%s@%s",p->username, ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr));

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
		snprintf(digest,digest_len,"Digest username=\"%s\", realm=\"%s\", algorithm=MD5, uri=\"%s\", nonce=\"%s\", response=\"%s\", opaque=\"%s\", qop=\"%s\", cnonce=\"%s\", nc=%s",p->authname,p->realm,uri,p->nonce,resp_hash, p->opaque, "auth", cnonce, "00000001");
	else
		snprintf(digest,digest_len,"Digest username=\"%s\", realm=\"%s\", algorithm=MD5, uri=\"%s\", nonce=\"%s\", response=\"%s\", opaque=\"%s\"",p->authname,p->realm,uri,p->nonce,resp_hash, p->opaque);

	return 0;
}
	


static char notify_usage[] =
"Usage: sip notify <type> <peer> [<peer>...]\n"
"       Send a NOTIFY message to a SIP peer or peers\n"
"       Message types are defined in sip_notify.conf\n";

static char show_users_usage[] = 
"Usage: sip show users [pattern]\n"
"       Lists all known SIP users.\n"
"       Optional regular expression pattern is used to filter the user list.\n";

static char show_user_usage[] =
"Usage: sip show user <name> [load]\n"
"       Lists all details on one SIP user and the current status.\n"
"       Option \"load\" forces lookup of peer in realtime storage.\n";

static char show_inuse_usage[] = 
"Usage: sip show inuse\n"
"       List all users known to the SIP (Session Initiation Protocol) subsystem usage counters and limits.\n";

static char show_channels_usage[] = 
"Usage: sip show channels\n"
"       Lists all currently active SIP channels.\n";

static char show_channel_usage[] = 
"Usage: sip show channel <channel>\n"
"       Provides detailed status on a given SIP channel.\n";

static char show_history_usage[] = 
"Usage: sip show history <channel>\n"
"       Provides detailed dialog history on a given SIP channel.\n";

static char show_peers_usage[] = 
"Usage: sip show peers [pattern]\n"
"       Lists all known SIP peers.\n"
"       Optional regular expression pattern is used to filter the peer list.\n";

static char show_peer_usage[] =
"Usage: sip show peer <name> [load]\n"
"       Lists all details on one SIP peer and the current status.\n"
"       Option \"load\" forces lookup of peer in realtime storage.\n";

static char prune_realtime_usage[] =
"Usage: sip prune realtime [<peername>|all]\n"
"       Prunes object(s) from the cache\n";

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

static char no_history_usage[] = 
"Usage: sip no history\n"
"       Disables recording of SIP dialog history for debugging purposes\n";

static char history_usage[] = 
"Usage: sip history\n"
"       Enables recording of SIP dialog history for debugging purposes.\n"
"Use 'sip show history' to view the history of a call number.\n";

static char sip_reload_usage[] =
"Usage: sip reload\n"
"       Reloads SIP configuration from sip.conf\n";

static char show_subscriptions_usage[] =
"Usage: sip show subscriptions\n" 
"       Shows active SIP subscriptions for extension states\n";

static char show_objects_usage[] =
"Usage: sip show objects\n" 
"       Shows status of known SIP objects\n";




static struct ast_cli_entry  cli_notify =
	{ { "sip", "notify", NULL }, sip_notify, "Send a notify packet to a SIP peer", notify_usage, complete_sipnotify };
static struct ast_cli_entry  cli_show_objects = 
	{ { "sip", "show", "objects", NULL }, sip_show_objects, "Show all SIP object allocations", show_objects_usage };
static struct ast_cli_entry  cli_show_users = 
	{ { "sip", "show", "users", NULL }, sip_show_users, "Show defined SIP users", show_users_usage };
static struct ast_cli_entry  cli_show_user =
	{ { "sip", "show", "user", NULL }, sip_show_user, "Show details on specific SIP user", show_user_usage, complete_sip_show_user };
static struct ast_cli_entry  cli_show_subscriptions =
	{ { "sip", "show", "subscriptions", NULL }, sip_show_subscriptions, "Show active SIP subscriptions", show_subscriptions_usage};
static struct ast_cli_entry  cli_show_channels =
	{ { "sip", "show", "channels", NULL }, sip_show_channels, "Show active SIP channels", show_channels_usage};
static struct ast_cli_entry  cli_show_channel =
	{ { "sip", "show", "channel", NULL }, sip_show_channel, "Show detailed SIP channel info", show_channel_usage, complete_sipch  };
static struct ast_cli_entry  cli_show_history =
	{ { "sip", "show", "history", NULL }, sip_show_history, "Show SIP dialog history", show_history_usage, complete_sipch  };
static struct ast_cli_entry  cli_debug_ip =
	{ { "sip", "debug", "ip", NULL }, sip_do_debug, "Enable SIP debugging on IP", debug_usage };
static struct ast_cli_entry  cli_debug_peer =
	{ { "sip", "debug", "peer", NULL }, sip_do_debug, "Enable SIP debugging on Peername", debug_usage, complete_sip_debug_peer };
static struct ast_cli_entry  cli_show_peer =
	{ { "sip", "show", "peer", NULL }, sip_show_peer, "Show details on specific SIP peer", show_peer_usage, complete_sip_show_peer };
static struct ast_cli_entry  cli_show_peers =
	{ { "sip", "show", "peers", NULL }, sip_show_peers, "Show defined SIP peers", show_peers_usage };
static struct ast_cli_entry  cli_prune_realtime =
	{ { "sip", "prune", "realtime", NULL }, sip_prune_realtime, "Prune a cached realtime lookup", prune_realtime_usage, complete_sip_show_peer };
static struct ast_cli_entry  cli_inuse_show =
	{ { "sip", "show", "inuse", NULL }, sip_show_inuse, "List all inuse/limit", show_inuse_usage };
static struct ast_cli_entry  cli_show_registry =
	{ { "sip", "show", "registry", NULL }, sip_show_registry, "Show SIP registration status", show_reg_usage };
static struct ast_cli_entry  cli_debug =
	{ { "sip", "debug", NULL }, sip_do_debug, "Enable SIP debugging", debug_usage };
static struct ast_cli_entry  cli_history =
	{ { "sip", "history", NULL }, sip_do_history, "Enable SIP history", history_usage };
static struct ast_cli_entry  cli_no_history =
	{ { "sip", "no", "history", NULL }, sip_no_history, "Disable SIP history", no_history_usage };
static struct ast_cli_entry  cli_no_debug =
	{ { "sip", "no", "debug", NULL }, sip_no_debug, "Disable SIP debugging", no_debug_usage };

/*--- parse_moved_contact: Parse 302 Moved temporalily response */
static void parse_moved_contact(struct sip_pvt *p, struct sip_request *req)
{
	char tmp[256] = "";
	char *s, *e;
	strncpy(tmp, get_header(req, "Contact"), sizeof(tmp) - 1);
	s = ditch_braces(tmp);
	e = strchr(s, ';');
	if (e)
		*e = '\0';
	if (ast_test_flag(p, SIP_PROMISCREDIR)) {
		if (!strncasecmp(s, "sip:", 4))
			s += 4;
		e = strchr(s, '/');
		if (e)
			*e = '\0';
		ast_log(LOG_DEBUG, "Found promiscuous redirection to 'SIP/%s'\n", s);
		if (p->owner)
			snprintf(p->owner->call_forward, sizeof(p->owner->call_forward), "SIP/%s", s);
	} else {
		e = strchr(tmp, '@');
		if (e)
			*e = '\0';
		e = strchr(tmp, '/');
		if (e)
			*e = '\0';
		if (!strncasecmp(s, "sip:", 4))
			s += 4;
		ast_log(LOG_DEBUG, "Found 302 Redirect to extension '%s'\n", s);
		if (p->owner)
			strncpy(p->owner->call_forward, s, sizeof(p->owner->call_forward) - 1);
	}
}

/*--- check_pendings: Check pending actions on SIP call ---*/
static void check_pendings(struct sip_pvt *p)
{
	/* Go ahead and send bye at this point */
	if (ast_test_flag(p, SIP_PENDINGBYE)) {
		transmit_request_with_auth(p, "BYE", 0, 1, 1);
		ast_set_flag(p, SIP_NEEDDESTROY);	
		ast_clear_flag(p, SIP_NEEDREINVITE);	
	} else if (ast_test_flag(p, SIP_NEEDREINVITE)) {
		ast_log(LOG_DEBUG, "Sending pending reinvite on '%s'\n", p->callid);
		/* Didn't get to reinvite yet, so do it now */
		transmit_reinvite_with_sdp(p);
		ast_clear_flag(p, SIP_NEEDREINVITE);	
	}
}

/*--- handle_response: Handle SIP response in dialogue ---*/
static void handle_response(struct sip_pvt *p, int resp, char *rest, struct sip_request *req, int ignore)
{
	char *to;
	char *msg, *c;
	struct ast_channel *owner;
	struct sip_peer *peer;
	int pingtime;
	struct timeval tv;
	int seqno=0;
	char iabuf[INET_ADDRSTRLEN];

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
		__sip_semi_ack(p, seqno, 0, msg);
	else
		__sip_ack(p, seqno, 0, msg);
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
			int newstate = 0;
			peer = p->peerpoke;
			gettimeofday(&tv, NULL);
			pingtime = (tv.tv_sec - peer->ps.tv_sec) * 1000 +
						(tv.tv_usec - peer->ps.tv_usec) / 1000;
			if (pingtime < 1)
				pingtime = 1;
			if ((peer->lastms < 0)  || (peer->lastms > peer->maxms)) {
				if (pingtime <= peer->maxms) {
					ast_log(LOG_NOTICE, "Peer '%s' is now REACHABLE! (%dms / %dms)\n", peer->name, pingtime, peer->maxms);
					statechanged = 1;
					newstate = 1;
				}
			} else if ((peer->lastms > 0) && (peer->lastms <= peer->maxms)) {
				if (pingtime > peer->maxms) {
					ast_log(LOG_NOTICE, "Peer '%s' is now TOO LAGGED! (%dms / %dms)\n", peer->name, pingtime, peer->maxms);
					statechanged = 1;
					newstate = 2;
				}
			}
			if (!peer->lastms)
			    statechanged = 1;
			peer->lastms = pingtime;
			peer->call = NULL;
			if (statechanged) {
				ast_device_state_changed("SIP/%s", peer->name);
				if (newstate == 2) {
					manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "Peer: SIP/%s\r\nPeerStatus: Lagged\r\nTime: %d\r\n", peer->name, pingtime);
				} else {
					manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "Peer: SIP/%s\r\nPeerStatus: Reachable\r\nTime: %d\r\n", peer->name, pingtime);
				}
			}

			if (peer->pokeexpire > -1)
				ast_sched_del(sched, peer->pokeexpire);
			if (!strcasecmp(msg, "INVITE"))
				transmit_request(p, "ACK", seqno, 0, 0);
			ast_set_flag(p, SIP_NEEDDESTROY);	
			/* Try again eventually */
			if ((peer->lastms < 0)  || (peer->lastms > peer->maxms))
    				peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_NOTOK, sip_poke_peer_s, peer);
			else
				peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_OK, sip_poke_peer_s, peer);
		}
	} else if (ast_test_flag(p, SIP_OUTGOING)) {
		/* Acknowledge sequence number */
		if (p->initid > -1) {
			/* Don't auto congest anymore since we've gotten something useful back */
			ast_sched_del(sched, p->initid);
			p->initid = -1;
		}
		switch(resp) {
		case 100:	/* 100 Trying */
			if (!strcasecmp(msg, "INVITE")) {
				sip_cancel_destroy(p);
			}
			break;
		case 183:	/* 183 Session Progress */
			if (!strcasecmp(msg, "INVITE")) {
				sip_cancel_destroy(p);
				if (!ast_strlen_zero(get_header(req, "Content-Type")))
					process_sdp(p, req);
				if (p->owner) {
					/* Queue a progress frame */
					ast_queue_control(p->owner, AST_CONTROL_PROGRESS);
				}
			}
			break;
		case 180:	/* 180 Ringing */
			if (!strcasecmp(msg, "INVITE")) {
				sip_cancel_destroy(p);
				if (p->owner) {
					ast_queue_control(p->owner, AST_CONTROL_RINGING);
					if (p->owner->_state != AST_STATE_UP)
						ast_setstate(p->owner, AST_STATE_RINGING);
				}
			}
			break;
		case 200:	/* 200 OK */
			if (!strcasecmp(msg, "NOTIFY")) {
				/* They got the notify, this is the end */
				if (p->owner) {
					ast_log(LOG_WARNING, "Notify answer on an owned channel?\n");
					ast_queue_hangup(p->owner);
				} else {
					if (!p->subscribed) {
					   	ast_set_flag(p, SIP_NEEDDESTROY); 
					}
				}
			} else if (!strcasecmp(msg, "INVITE")) {
				/* 200 OK on invite - someone's answering our call */
				sip_cancel_destroy(p);
				if (!ast_strlen_zero(get_header(req, "Content-Type")))
					process_sdp(p, req);

				/* Parse contact header for continued conversation */
				/* When we get 200 OK, we now which device (and IP) to contact for this call */
				/* This is important when we have a SIP proxy between us and the phone */
				parse_ok_contact(p, req);
				/* Save Record-Route for any later requests we make on this dialogue */
				build_route(p, req, 1);
				if (p->owner) {
					if (p->owner->_state != AST_STATE_UP) {
#ifdef OSP_SUPPORT	
						time(&p->ospstart);
#endif
						ast_queue_control(p->owner, AST_CONTROL_ANSWER);
					} else {
						struct ast_frame af = { AST_FRAME_NULL, };
						ast_queue_frame(p->owner, &af);
					}
				} else /* It's possible we're getting an ACK after we've tried to disconnect
						  by sending CANCEL */
					ast_set_flag(p, SIP_PENDINGBYE);	
				p->authtries = 0;
				/* If I understand this right, the branch is different for a non-200 ACK only */
				transmit_request(p, "ACK", seqno, 0, 1);
				check_pendings(p);
			} else if (!strcasecmp(msg, "REGISTER")) {
				/* char *exp; */
				int expires, expires_ms;
				struct sip_registry *r;
				r=p->registry;
				if (r) {
					r->regstate=REG_STATE_REGISTERED;
					manager_event(EVENT_FLAG_SYSTEM, "Registry", "Channel: SIP\r\nDomain: %s\r\nStatus: %s\r\n", r->hostname, regstate2str(r->regstate));
					ast_log(LOG_DEBUG, "Registration successful\n");
					if (r->timeout > -1) {
						ast_log(LOG_DEBUG, "Cancelling timeout %d\n", r->timeout);
						ast_sched_del(sched, r->timeout);
					}
					r->timeout=-1;
					r->call = NULL;
					p->registry = NULL;
					ast_set_flag(p, SIP_NEEDDESTROY);	
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
								if( (tmptmp=strstr(contact, p->our_contact))) {
									contact=tmptmp;
									break;
								}
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
					if (sipdebug)
						ast_log(LOG_NOTICE, "Outbound Registration: Expiry for %s is %d sec (Scheduling reregistration in %d ms)\n", r->hostname, expires, expires_ms); 

					r->refresh= (int) expires_ms / 1000;

					/* Schedule re-registration before we expire */
					r->expire=ast_sched_add(sched, expires_ms, sip_reregister, r); 
					ASTOBJ_UNREF(r, sip_registry_destroy);
				} else
					ast_log(LOG_WARNING, "Got 200 OK on REGISTER that isn't a register\n");

			}
			break;
		case 401: /* Not www-authorized on REGISTER */
			if (!strcasecmp(msg, "INVITE")) {
				/* First we ACK */
				transmit_request(p, "ACK", seqno, 0, 0);
				/* Then we AUTH */
				if ((p->authtries > 1) || do_proxy_auth(p, req, "WWW-Authenticate", "Authorization", "INVITE", 1)) {
					ast_log(LOG_NOTICE, "Failed to authenticate on INVITE to '%s'\n", get_header(&p->initreq, "From"));
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
			} else if (p->registry && !strcasecmp(msg, "REGISTER")) {
				if ((p->authtries > 1) || do_register_auth(p, req, "WWW-Authenticate", "Authorization")) {
					ast_log(LOG_NOTICE, "Failed to authenticate on REGISTER to '%s'\n", get_header(&p->initreq, "From"));
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
			} else
				ast_set_flag(p, SIP_NEEDDESTROY);	
			break;
		case 403: /* Forbidden - we failed authentication */
			if (!strcasecmp(msg, "INVITE")) {
				/* First we ACK */
				transmit_request(p, "ACK", seqno, 0, 0);
				ast_log(LOG_WARNING, "Forbidden - wrong password on authentication for INVITE to '%s'\n", get_header(&p->initreq, "From"));
				if (owner)
					ast_queue_control(p->owner, AST_CONTROL_CONGESTION);
				ast_set_flag(p, SIP_NEEDDESTROY);	
			} else if (p->registry && !strcasecmp(msg, "REGISTER")) {
				ast_log(LOG_WARNING, "Forbidden - wrong password on authentication for REGISTER for '%s' to '%s'\n", p->registry->username, p->registry->hostname);
				ast_set_flag(p, SIP_NEEDDESTROY);	
			} else {
				ast_log(LOG_WARNING, "Forbidden - wrong password on authentication for %s\n", msg);
			}
			break;
		case 407: /* Proxy auth required */
			if (!strcasecmp(msg, "INVITE")) {
				/* First we ACK */
				transmit_request(p, "ACK", seqno, 0, 0);
				/* Then we AUTH */
				/* But only if the packet wasn't marked as ignore in handle_request */
				if(!ignore){
					if ((p->authtries > 1) || do_proxy_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization", "INVITE", 1)) {
						ast_log(LOG_NOTICE, "Failed to authenticate on INVITE to '%s'\n", get_header(&p->initreq, "From"));
						ast_set_flag(p, SIP_NEEDDESTROY);	
					}
				}
			} else if (!strcasecmp(msg, "BYE") || !strcasecmp(msg, "REFER")) {
				if (ast_strlen_zero(p->authname))
					ast_log(LOG_WARNING, "Asked to authenticate %s, to %s:%d but we have no matching peer!\n",
							msg, ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr), ntohs(p->recv.sin_port));
				if ((p->authtries > 1) || do_proxy_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization", msg, 0)) {
					ast_log(LOG_NOTICE, "Failed to authenticate on %s to '%s'\n", msg, get_header(&p->initreq, "From"));
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
			} else if (p->registry && !strcasecmp(msg, "REGISTER")) {
				if ((p->authtries > 1) || do_register_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization")) {
					ast_log(LOG_NOTICE, "Failed to authenticate on REGISTER to '%s' (tries '%d')\n", get_header(&p->initreq, "From"), p->authtries);
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
			} else
				ast_set_flag(p, SIP_NEEDDESTROY);	

			break;
		case 501: /* Not Implemented */
			if (!strcasecmp(msg, "INVITE")) {
				if (p->owner)
					ast_queue_control(p->owner, AST_CONTROL_CONGESTION);
			} else
				ast_log(LOG_WARNING, "Host '%s' does not implement '%s'\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr), msg);
			break;
		default:
			if ((resp >= 300) && (resp < 700)) {
				if ((option_verbose > 2) && (resp != 487))
					ast_verbose(VERBOSE_PREFIX_3 "Got SIP response %d \"%s\" back from %s\n", resp, rest, ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr));
				ast_set_flag(p, SIP_ALREADYGONE);	
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
					if (ast_test_flag(p, SIP_OUTGOING)) {
						update_user_counter(p, DEC_OUT_USE);
					}
					else {
						update_user_counter(p, DEC_IN_USE);
					}
					break;
				case 482: /* SIP is incapable of performing a hairpin call, which
				             is yet another failure of not having a layer 2 (again, YAY
							 IETF for thinking ahead).  So we treat this as a call
							 forward and hope we end up at the right place... */
					ast_log(LOG_DEBUG, "Hairpin detected, setting up call forward for what it's worth\n");
					if (p->owner)
						snprintf(p->owner->call_forward, sizeof(p->owner->call_forward), "Local/%s@%s", p->username, p->context);
					/* Fall through */
				case 486: /* Busy here */
				case 600: /* Busy everywhere */
				case 603: /* Decline */
					if (p->owner)
						ast_queue_control(p->owner, AST_CONTROL_BUSY);
					break;
				case 480: /* Temporarily Unavailable */
				case 404: /* Not Found */
				case 410: /* Gone */
				case 400: /* Bad Request */
				case 500: /* Server error */
				case 503: /* Service Unavailable */
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
				ast_set_flag(p, SIP_ALREADYGONE);	
				if (!p->owner)
					ast_set_flag(p, SIP_NEEDDESTROY);	
			} else if ((resp >= 100) && (resp < 200)) {
				if (!strcasecmp(msg, "INVITE")) {
					sip_cancel_destroy(p);
					if (!ast_strlen_zero(get_header(req, "Content-Type")))
						process_sdp(p, req);
					if (p->owner) {
						/* Queue a progress frame */
						ast_queue_control(p->owner, AST_CONTROL_PROGRESS);
					}
				}
			} else
				ast_log(LOG_NOTICE, "Dunno anything about a %d %s response from %s\n", resp, rest, p->owner ? p->owner->name : ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr));
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
							msg, ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr), ntohs(p->recv.sin_port));
				if ((p->authtries > 1) || do_proxy_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization", msg, 0)) {
					ast_log(LOG_NOTICE, "Failed to authenticate on %s to '%s'\n", msg, get_header(&p->initreq, "From"));
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
			}
			break;
		}
	}
}

struct sip_dual {
	struct ast_channel *chan1;
	struct ast_channel *chan2;
	struct sip_request req;
};

static void *sip_park_thread(void *stuff)
{
	struct ast_channel *chan1, *chan2;
	struct sip_dual *d;
	struct sip_request req;
	int ext;
	int res;
	d = stuff;
	chan1 = d->chan1;
	chan2 = d->chan2;
	copy_request(&req, &d->req);
	free(d);
	ast_mutex_lock(&chan1->lock);
	ast_do_masquerade(chan1);
	ast_mutex_unlock(&chan1->lock);
	res = ast_park_call(chan1, chan2, 0, &ext);
	/* Then hangup */
	ast_hangup(chan2);
	ast_log(LOG_DEBUG, "Parked on extension '%d'\n", ext);
	return NULL;
}

/*--- sip_park: Park a call ---*/
static int sip_park(struct ast_channel *chan1, struct ast_channel *chan2, struct sip_request *req)
{
	struct sip_dual *d;
	struct ast_channel *chan1m, *chan2m;
	pthread_t th;
	chan1m = ast_channel_alloc(0);
	chan2m = ast_channel_alloc(0);
	if (chan2m && chan1m) {
		snprintf(chan1m->name, sizeof(chan1m->name), "Parking/%s", chan1->name);
		/* Make formats okay */
		chan1m->readformat = chan1->readformat;
		chan1m->writeformat = chan1->writeformat;
		ast_channel_masquerade(chan1m, chan1);
		/* Setup the extensions and such */
		strncpy(chan1m->context, chan1->context, sizeof(chan1m->context) - 1);
		strncpy(chan1m->exten, chan1->exten, sizeof(chan1m->exten) - 1);
		chan1m->priority = chan1->priority;
		
		/* We make a clone of the peer channel too, so we can play
		   back the announcement */
		snprintf(chan2m->name, sizeof (chan2m->name), "SIPPeer/%s",chan2->name);
		/* Make formats okay */
		chan2m->readformat = chan2->readformat;
		chan2m->writeformat = chan2->writeformat;
		ast_channel_masquerade(chan2m, chan2);
		/* Setup the extensions and such */
		strncpy(chan2m->context, chan2->context, sizeof(chan2m->context) - 1);
		strncpy(chan2m->exten, chan2->exten, sizeof(chan2m->exten) - 1);
		chan2m->priority = chan2->priority;
		ast_mutex_lock(&chan2m->lock);
		if (ast_do_masquerade(chan2m)) {
			ast_log(LOG_WARNING, "Masquerade failed :(\n");
			ast_mutex_unlock(&chan2m->lock);
			ast_hangup(chan2m);
			return -1;
		}
		ast_mutex_unlock(&chan2m->lock);
	} else {
		if (chan1m)
			ast_hangup(chan1m);
		if (chan2m)
			ast_hangup(chan2m);
		return -1;
	}
	d = malloc(sizeof(struct sip_dual));
	if (d) {
		memset(d, 0, sizeof(*d));
		/* Save original request for followup */
		copy_request(&d->req, req);
		d->chan1 = chan1m;
		d->chan2 = chan2m;
		if (!ast_pthread_create(&th, NULL, sip_park_thread, d))
			return 0;
		free(d);
	}
	return -1;
}

static void ast_quiet_chan(struct ast_channel *chan) 
{
	if (chan && chan->_state == AST_STATE_UP) {
		if (chan->generatordata)
			ast_deactivate_generator(chan);
	}
}

/*--- attempt_transfer: Attempt transfer of SIP call ---*/
static int attempt_transfer(struct sip_pvt *p1, struct sip_pvt *p2)
{
	int res = 0;
	struct ast_channel 
		*chana = NULL,
		*chanb = NULL,
		*bridgea = NULL,
		*bridgeb = NULL,
		*peera = NULL,
		*peerb = NULL,
		*peerc = NULL,
		*peerd = NULL;

	if (!p1->owner || !p2->owner) {
		ast_log(LOG_WARNING, "Transfer attempted without dual ownership?\n");
		return -1;
	}
	chana = p1->owner;
	chanb = p2->owner;
	bridgea = ast_bridged_channel(chana);
	bridgeb = ast_bridged_channel(chanb);
	
	if (bridgea) {
		peera = chana;
		peerb = chanb;
		peerc = bridgea;
		peerd = bridgeb;
	} else if (bridgeb) {
		peera = chanb;
		peerb = chana;
		peerc = bridgeb;
		peerd = bridgea;
	}
	
	if (peera && peerb && peerc && (peerb != peerc)) {
		ast_quiet_chan(peera);
		ast_quiet_chan(peerb);
		ast_quiet_chan(peerc);
		ast_quiet_chan(peerd);

		if (peera->cdr && peerb->cdr) {
			peerb->cdr = ast_cdr_append(peerb->cdr, peera->cdr);
		} else if(peera->cdr) {
			peerb->cdr = peera->cdr;
		}
		peera->cdr = NULL;

		if (peerb->cdr && peerc->cdr) {
			peerb->cdr = ast_cdr_append(peerb->cdr, peerc->cdr);
		} else if(peerc->cdr) {
			peerb->cdr = peerc->cdr;
		}
		peerc->cdr = NULL;
		
		if (ast_channel_masquerade(peerb, peerc)) {
			ast_log(LOG_WARNING, "Failed to masquerade %s into %s\n", peerb->name, peerc->name);
			res = -1;
		}
		return res;
	} else {
		ast_log(LOG_NOTICE, "Transfer attempted with no appropriate bridged calls to transfer\n");
		if (chana)
			ast_softhangup_nolock(chana, AST_SOFTHANGUP_DEV);
		if (chanb)
			ast_softhangup_nolock(chanb, AST_SOFTHANGUP_DEV);
		return -1;
	}
	return 0;
}

/*--- handle_request: Handle SIP requests (methods) ---*/
/*      this is where all incoming requests go first   */
static int handle_request(struct sip_pvt *p, struct sip_request *req, struct sockaddr_in *sin, int *recount, int *nounlock)
{
	/* Called with p->lock held, as well as p->owner->lock if appropriate, keeping things
	   relatively static */
	struct sip_request resp;
	char *cmd;
	char *cseq;
	char *from;
	char *e;
	char *useragent;
	struct ast_channel *c=NULL;
	struct ast_channel *transfer_to;
	int seqno;
	int len;
	int ignore=0;
	int respid;
	int res;
	int gotdest;
	char iabuf[INET_ADDRSTRLEN];
	struct ast_frame af = { AST_FRAME_NULL, };
	int debug = sip_debug_test_pvt(p);

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

	/* Save useragent of the client */
	useragent = get_header(req, "User-Agent");
	strncpy(p->useragent, useragent, sizeof(p->useragent)-1);

	
	if (strcasecmp(cmd, "SIP/2.0")) {
		/* Request coming in */			
		if (p->icseq && (p->icseq > seqno)) {
			ast_log(LOG_DEBUG, "Ignoring too old packet packet %d (expecting >= %d)\n", seqno, p->icseq);
			return -1;
		} else if (p->icseq && (p->icseq == seqno) && (strcasecmp(cmd, "CANCEL") || ast_test_flag(p, SIP_ALREADYGONE))) {
			/* ignore means "don't do anything with it" but still have to 
			   respond appropriately.  We do this if we receive a repeat of
			   the last sequence number  */
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
			ast_set_flag(p, SIP_NEEDDESTROY);	
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
		/* Next should follow monotonically (but not necessarily 
		   incrementally -- thanks again to the genius authors of SIP --
		   increasing */
		p->icseq = seqno;

	/* Initialize the context if it hasn't been already */
	if (!strcasecmp(cmd, "OPTIONS")) {
		res = get_destination(p, req);
		build_contact(p);
		/* XXX Should we authenticate OPTIONS? XXX */
		if (ast_strlen_zero(p->context))
			strncpy(p->context, default_context, sizeof(p->context) - 1);
		if (res < 0)
			transmit_response_with_allow(p, "404 Not Found", req, 0);
		else if (res > 0)
			transmit_response_with_allow(p, "484 Address Incomplete", req, 0);
		else 
			transmit_response_with_allow(p, "200 OK", req, 0);
		/* Destroy if this OPTIONS was the opening request, but not if
		   it's in the middle of a normal call flow. */
		if (!p->lastinvite)
			ast_set_flag(p, SIP_NEEDDESTROY);	
	} else if (!strcasecmp(cmd, "INVITE")) {
		if (ast_test_flag(p, SIP_OUTGOING) && p->owner && (p->owner->_state != AST_STATE_UP)) {
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
			if (debug)
				ast_verbose("Using latest request as basis request\n");
			sip_cancel_destroy(p);
			/* This call is no longer outgoing if it ever was */
			ast_clear_flag(p, SIP_OUTGOING);
			/* This also counts as a pending invite */
			p->pendinginvite = seqno;
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
		} else if (debug)
			ast_verbose("Ignoring this request\n");
		if (!p->lastinvite && !ignore && !p->owner) {
			/* Handle authentication if this is our first invite */
			res = check_user(p, req, cmd, e, 1, sin, ignore);
			if (res) {
				if (res < 0) {
					ast_log(LOG_NOTICE, "Failed to authenticate user %s\n", get_header(req, "From"));
					if (ignore)
						transmit_response(p, "403 Forbidden", req);
					else
						transmit_response_reliable(p, "403 Forbidden", req, 1);
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
				return 0;
			}
			/* Initialize the context if it hasn't been already */
			if (ast_strlen_zero(p->context))
				strncpy(p->context, default_context, sizeof(p->context) - 1);
			/* Check number of concurrent calls -vs- incoming limit HERE */
			ast_log(LOG_DEBUG, "Check for res for %s\n", p->username);
			res = update_user_counter(p,INC_IN_USE);
			if (res) {
				if (res < 0) {
					ast_log(LOG_DEBUG, "Failed to place call for user %s, too many calls\n", p->username);
					ast_set_flag(p, SIP_NEEDDESTROY);	
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
					if (ignore)
						transmit_response(p, "404 Not Found", req);
					else
						transmit_response_reliable(p, "404 Not Found", req, 1);
					update_user_counter(p,DEC_IN_USE);
				} else {
					if (ignore)
						transmit_response(p, "484 Address Incomplete", req);
					else
						transmit_response_reliable(p, "484 Address Incomplete", req, 1);
					update_user_counter(p,DEC_IN_USE);
				}
				ast_set_flag(p, SIP_NEEDDESTROY);		
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
						if (ignore)
							transmit_response(p, "503 Unavailable", req);
						else
							transmit_response_reliable(p, "503 Unavailable", req, 1);
						c = NULL;
					}
				} else {
					ast_mutex_unlock(&c->lock);
					if (ast_pickup_call(c)) {
						ast_log(LOG_NOTICE, "Nothing to pick up\n");
						if (ignore)
							transmit_response(p, "503 Unavailable", req);
						else
							transmit_response_reliable(p, "503 Unavailable", req, 1);
						ast_set_flag(p, SIP_ALREADYGONE);	
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
			if (p && !ast_test_flag(p, SIP_NEEDDESTROY)) {
				if (!p->jointcapability) {
					if (ignore)
						transmit_response(p, "488 Not Acceptable Here", req);
					else
						transmit_response_reliable(p, "488 Not Acceptable Here", req, 1);
					ast_set_flag(p, SIP_NEEDDESTROY);	
				} else {
					ast_log(LOG_NOTICE, "Unable to create/find channel\n");
					if (ignore)
						transmit_response(p, "503 Unavailable", req);
					else
						transmit_response_reliable(p, "503 Unavailable", req, 1);
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
			}
		}
	} else if (!strcasecmp(cmd, "REFER")) {
		ast_log(LOG_DEBUG, "We found a REFER!\n");
		if (ast_strlen_zero(p->context))
			strncpy(p->context, default_context, sizeof(p->context) - 1);
		res = get_refer_info(p, req);
		if (res < 0)
			transmit_response_with_allow(p, "404 Not Found", req, 1);
		else if (res > 0)
			transmit_response_with_allow(p, "484 Address Incomplete", req, 1);
		else {
			int nobye = 0;
			if (!ignore) {
				if (p->refer_call) {
					ast_log(LOG_DEBUG,"202 Accepted (supervised)\n");
					attempt_transfer(p, p->refer_call);
					if (p->refer_call->owner)
						ast_mutex_unlock(&p->refer_call->owner->lock);
					ast_mutex_unlock(&p->refer_call->lock);
					p->refer_call = NULL;
					ast_set_flag(p, SIP_GOTREFER);	
				} else {
					ast_log(LOG_DEBUG,"202 Accepted (blind)\n");
					c = p->owner;
					if (c) {
						transfer_to = ast_bridged_channel(c);
						if (transfer_to) {
							ast_moh_stop(transfer_to);
							if (!strcmp(p->refer_to, ast_parking_ext())) {
								/* Must release c's lock now, because it will not longer
								    be accessible after the transfer! */
								*nounlock = 1;
								ast_mutex_unlock(&c->lock);
								sip_park(transfer_to, c, req);
								nobye = 1;
							} else {
								/* Must release c's lock now, because it will not longer
								    be accessible after the transfer! */
								*nounlock = 1;
								ast_mutex_unlock(&c->lock);
								ast_async_goto(transfer_to,p->context, p->refer_to,1);
							}
						} else {
							ast_queue_hangup(p->owner);
						}
					}
					ast_set_flag(p, SIP_GOTREFER);	
				}
				transmit_response(p, "202 Accepted", req);
				transmit_notify_with_sipfrag(p, seqno);
				/* Always increment on a BYE */
				if (!nobye) {
					transmit_request_with_auth(p, "BYE", 0, 1, 1);
					ast_set_flag(p, SIP_ALREADYGONE);	
				}
			}
		}
	} else if (!strcasecmp(cmd, "CANCEL")) {
		check_via(p, req);
		ast_set_flag(p, SIP_ALREADYGONE);	
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
			ast_set_flag(p, SIP_NEEDDESTROY);	
		if (p->initreq.len > 0) {
			if (!ignore)
				transmit_response_reliable(p, "487 Request Terminated", &p->initreq, 1);
			transmit_response(p, "200 OK", req);
		} else {
			transmit_response(p, "481 Call Leg Does Not Exist", req);
		}
	} else if (!strcasecmp(cmd, "BYE")) {
		copy_request(&p->initreq, req);
		check_via(p, req);
		ast_set_flag(p, SIP_ALREADYGONE);	
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
				ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr));
			if (ast_strlen_zero(p->context))
				strncpy(p->context, default_context, sizeof(p->context) - 1);
			res = get_also_info(p, req);
			if (!res) {
				c = p->owner;
				if (c) {
					transfer_to = ast_bridged_channel(c);
					if (transfer_to) {
						/* Don't actually hangup here... */
						ast_moh_stop(transfer_to);
						ast_async_goto(transfer_to,p->context, p->refer_to,1);
					} else
						ast_queue_hangup(p->owner);
				}
			} else {
				ast_log(LOG_WARNING, "Invalid transfer information from '%s'\n", ast_inet_ntoa(iabuf, sizeof(iabuf), p->recv.sin_addr));
				ast_queue_hangup(p->owner);
			}
		} else if (p->owner)
			ast_queue_hangup(p->owner);
		else
			ast_set_flag(p, SIP_NEEDDESTROY);	
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "MESSAGE")) {
		if (p->lastinvite) {
			if (!ignore) {
				if (debug)
					ast_verbose("Receiving message!\n");
				receive_message(p, req);
			}
			transmit_response(p, "200 OK", req);
		} else {
			transmit_response(p, "405 Method Not Allowed", req);
			ast_set_flag(p, SIP_NEEDDESTROY);
		}
	} else if (!strcasecmp(cmd, "SUBSCRIBE")) {
		if (!ignore) {
			/* Use this as the basis */
			if (debug)
				ast_verbose("Using latest SUBSCRIBE request as basis request\n");
			/* This call is no longer outgoing if it ever was */
			ast_clear_flag(p, SIP_OUTGOING);
			copy_request(&p->initreq, req);
			check_via(p, req);
		} else if (debug)
			ast_verbose("Ignoring this request\n");

		if (!p->lastinvite) {
			char mailbox[256]="";
			int found = 0;

			/* Handle authentication if this is our first subscribe */
			res = check_user_full(p, req, cmd, e, 0, sin, ignore, mailbox, sizeof(mailbox));
			if (res) {
				if (res < 0) {
					ast_log(LOG_NOTICE, "Failed to authenticate user %s for SUBSCRIBE\n", get_header(req, "From"));
					ast_set_flag(p, SIP_NEEDDESTROY);	
				}
				return 0;
			}
			/* Initialize the context if it hasn't been already */
			if (ast_strlen_zero(p->context))
				strncpy(p->context, default_context, sizeof(p->context) - 1);
			/* Get destination right away */
			gotdest = get_destination(p, NULL);
			build_contact(p);
			if (gotdest) {
				if (gotdest < 0)
					transmit_response(p, "404 Not Found", req);
				else
					transmit_response(p, "484 Address Incomplete", req);
				ast_set_flag(p, SIP_NEEDDESTROY);	
			} else {
				/* Initialize tag */	
				p->tag = rand();
				if (!strcmp(get_header(req, "Accept"), "application/dialog-info+xml"))
				    p->subscribed = 2;
				else if (!strcmp(get_header(req, "Accept"), "application/simple-message-summary")) {
					/* Looks like they actually want a mailbox */

					/* At this point, we should check if they subscribe to a mailbox that
					  has the same extension as the peer or the mailbox id. If we configure
					  the context to be the same as a SIP domain, we could check mailbox
					  context as well. To be able to securely accept subscribes on mailbox
					  IDs, not extensions, we need to check the digest auth user to make
					  sure that the user has access to the mailbox.
					 
					  Since we do not act on this subscribe anyway, we might as well 
					  accept any authenticated peer with a mailbox definition in their 
					  config section.
					
					*/
					if (!ast_strlen_zero(mailbox)) {
						found++;
					}

					if (found){
						transmit_response(p, "200 OK", req);
						ast_set_flag(p, SIP_NEEDDESTROY);	
					} else {
						transmit_response(p, "403 Forbidden", req);
						ast_set_flag(p, SIP_NEEDDESTROY);	
					}
					return 0;
				} else
				    p->subscribed = 1;
				if (p->subscribed)
					p->stateid = ast_extension_state_add(p->context, p->exten, cb_extensionstate, p);
			}
			
		} else 
			c = p->owner;

		if (!ignore && p)
			p->lastinvite = seqno;
		if (p && !ast_test_flag(p, SIP_NEEDDESTROY)) {
		    if (!(p->expiry = atoi(get_header(req, "Expires")))) {
				transmit_response(p, "200 OK", req);
				ast_set_flag(p, SIP_NEEDDESTROY);	
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
			if (debug)
				ast_verbose("Receiving DTMF!\n");
			receive_info(p, req);
		} else { /* if ignoring, transmit response */
			transmit_response(p, "200 OK", req);
		}
	} else if (!strcasecmp(cmd, "NOTIFY")) {
		/* XXX we get NOTIFY's from some servers. WHY?? Maybe we should
			look into this someday XXX */
		transmit_response(p, "200 OK", req);
		if (!p->lastinvite) 
			ast_set_flag(p, SIP_NEEDDESTROY);	
	} else if (!strcasecmp(cmd, "REGISTER")) {
		/* Use this as the basis */
		if (debug)
			ast_verbose("Using latest request as basis request\n");
		copy_request(&p->initreq, req);
		check_via(p, req);
		if ((res = register_verify(p, sin, req, e, ignore)) < 0) 
			ast_log(LOG_NOTICE, "Registration from '%s' failed for '%s'\n", get_header(req, "To"), ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr));
		if (res < 1) {
			/* Destroy the session, but keep us around for just a bit in case they don't
			   get our 200 OK */
		    sip_scheddestroy(p, 15*1000);
		}
	} else if (!strcasecmp(cmd, "ACK")) {
		/* Make sure we don't ignore this */
		if (seqno == p->pendinginvite) {
			p->pendinginvite = 0;
			__sip_ack(p, seqno, FLAG_RESPONSE, NULL);
			if (!ast_strlen_zero(get_header(req, "Content-Type"))) {
				if (process_sdp(p, req))
					return -1;
			} 
			check_pendings(p);
		}
		if (!p->lastinvite && ast_strlen_zero(p->randdata))
			ast_set_flag(p, SIP_NEEDDESTROY);	
	} else if (!strcasecmp(cmd, "SIP/2.0")) {
		extract_uri(p, req);
		while(*e && (*e < 33)) e++;
		if (sscanf(e, "%i %n", &respid, &len) != 1) {
			ast_log(LOG_WARNING, "Invalid response: '%s'\n", e);
		} else {
			handle_response(p, respid, e + len, req,ignore);
		}
	} else {
		transmit_response_with_allow(p, "405 Method Not Allowed", req, 0);
		ast_log(LOG_NOTICE, "Unknown SIP command '%s' from '%s'\n", 
			cmd, ast_inet_ntoa(iabuf, sizeof(iabuf), p->sa.sin_addr));
		/* If this is some new method, and we don't have a call, destroy it now */
		if (!p->initreq.headers)
			ast_set_flag(p, SIP_NEEDDESTROY);	
	}
	return 0;
}

/*--- sipsock_read: Read data from SIP socket ---*/
/*    Successful messages is connected to SIP call and forwarded to handle_request() */
static int sipsock_read(int *id, int fd, short events, void *ignore)
{
	struct sip_request req;
	struct sockaddr_in sin = { 0, };
	struct sip_pvt *p;
	int res;
	int len;
	int nounlock;
	int recount = 0;
	int debug;

	len = sizeof(sin);
	memset(&req, 0, sizeof(req));
	res = recvfrom(sipsock, req.data, sizeof(req.data) - 1, 0, (struct sockaddr *)&sin, &len);
	if (res < 0) {
		if (errno == EAGAIN)
			ast_log(LOG_NOTICE, "SIP: Received packet with bad UDP checksum\n");
		else if (errno != ECONNREFUSED)
			ast_log(LOG_WARNING, "Recv error: %s\n", strerror(errno));
		return 1;
	}
	req.data[res] = '\0';
	req.len = res;
	debug = sip_debug_test_addr(&sin);
	if (debug)
		ast_verbose("\n\nSip read: \n%s\n", req.data);
	if (pedanticsipchecking)
		req.len = lws2sws(req.data, req.len);
	parse(&req);
	if (debug)
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
		if (recordhistory) {
			char tmp[80] = "";
			/* This is a response, note what it was for */
			snprintf(tmp, sizeof(tmp), "%s / %s", req.data, get_header(&req, "CSeq"));
			append_history(p, "Rx", tmp);
		}
		nounlock = 0;
		handle_request(p, &req, &sin, &recount, &nounlock);
		if (p->owner && !nounlock)
			ast_mutex_unlock(&p->owner->lock);
		ast_mutex_unlock(&p->lock);
	}
	ast_mutex_unlock(&netlock);
	if (recount)
		ast_update_use_count();

	return 1;
}

/*--- sip_send_mwi_to_peer: Send message waiting indication ---*/
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
		return 0;
	}
	
	p = sip_alloc(NULL, NULL, 0);
	if (!p) {
		ast_log(LOG_WARNING, "Unable to build sip pvt data for MWI\n");
		return -1;
	}
	strncpy(name, peer->name, sizeof(name) - 1);
	peer->lastmsgssent = ((newmsgs << 8) | (oldmsgs));
	if (create_addr(p, name)) {
		/* Maybe they're not registered, etc. */
		sip_destroy(p);
		return 0;
	}
	/* Recalculate our side, and recalculate Call ID */
	if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	build_via(p, p->via, sizeof(p->via));
	build_callid(p->callid, sizeof(p->callid), p->ourip, p->fromdomain);
	/* Send MWI */
	ast_set_flag(p, SIP_OUTGOING);
	transmit_notify_with_mwi(p, newmsgs, oldmsgs);
	sip_scheddestroy(p, 15000);
	return 0;
}

/*--- do_monitor: The SIP monitoring thread ---*/
static void *do_monitor(void *data)
{
	int res;
	struct sip_pvt *sip;
	struct sip_peer *peer = NULL;
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
		time(&t);
		sip = iflist;
		while(sip) {
			ast_mutex_lock(&sip->lock);
			if (sip->rtp && sip->owner && (sip->owner->_state == AST_STATE_UP) && !sip->redirip.sin_addr.s_addr) {
				if (sip->lastrtptx && sip->rtpkeepalive && t > sip->lastrtptx + sip->rtpkeepalive) {
					/* Need to send an empty RTP packet */
					time(&sip->lastrtptx);
					ast_rtp_sendcng(sip->rtp, 0);
				}
				if (sip->lastrtprx && (sip->rtptimeout || sip->rtpholdtimeout) && t > sip->lastrtprx + sip->rtptimeout) {
					/* Might be a timeout now -- see if we're on hold */
					struct sockaddr_in sin;
					ast_rtp_get_peer(sip->rtp, &sin);
					if (sin.sin_addr.s_addr || 
							(sip->rtpholdtimeout && 
							  (t > sip->lastrtprx + sip->rtpholdtimeout))) {
						/* Needs a hangup */
						if (sip->rtptimeout) {
							while(sip->owner && ast_mutex_trylock(&sip->owner->lock)) {
								ast_mutex_unlock(&sip->lock);
								usleep(1);
								ast_mutex_lock(&sip->lock);
							}
							if (sip->owner) {
								ast_log(LOG_NOTICE, "Disconnecting call '%s' for lack of RTP activity in %ld seconds\n", sip->owner->name, (long)(t - sip->lastrtprx));
								/* Issue a softhangup */
								ast_softhangup(sip->owner, AST_SOFTHANGUP_DEV);
								ast_mutex_unlock(&sip->owner->lock);
							}
						}
					}
				}
			}
			if (ast_test_flag(sip, SIP_NEEDDESTROY) && !sip->packets && !sip->owner) {
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

		/* needs work to send mwi to realtime peers */
		time(&t);
		fastrestart = 0;
		curpeernum = 0;
		peer = NULL;
		ASTOBJ_CONTAINER_TRAVERSE(&peerl, !peer, do {
			if ((curpeernum > lastpeernum) && !ast_strlen_zero(iterator->mailbox) && ((t - iterator->lastmsgcheck) > global_mwitime)) {
				fastrestart = 1;
				lastpeernum = curpeernum;
				peer = ASTOBJ_REF(iterator);
			};
			curpeernum++;
		} while (0)
		);
		if (peer) {
			ASTOBJ_WRLOCK(peer);
			sip_send_mwi_to_peer(peer);
			ASTOBJ_UNLOCK(peer);
			ASTOBJ_UNREF(peer,sip_destroy_peer);
		} else {
			/* Reset where we come from */
			lastpeernum = -1;
		}
		ast_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
	
}

/*--- restart_monitor: Start the channel monitor thread ---*/
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
		if (ast_pthread_create(&monitor_thread, &attr, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

/*--- sip_poke_noanswer: No answer to Qualify poke ---*/
static int sip_poke_noanswer(void *data)
{
	struct sip_peer *peer = data;
	
	peer->pokeexpire = -1;
	if (peer->lastms > -1) {
		ast_log(LOG_NOTICE, "Peer '%s' is now UNREACHABLE!  Last qualify: %d\n", peer->name, peer->lastms);
		manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "Peer: SIP/%s\r\nPeerStatus: Unreachable\r\nTime: %d\r\n", peer->name, -1);
	}
	if (peer->call)
		sip_destroy(peer->call);
	peer->call = NULL;
	peer->lastms = -1;
	ast_device_state_changed("SIP/%s", peer->name);
	/* Try again quickly */
	peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_NOTOK, sip_poke_peer_s, peer);
	return 0;
}

/*--- sip_poke_peer: Check availability of peer, also keep NAT open ---*/
/*	This is done with the interval in qualify= option in sip.conf */
/*	Default is 2 seconds */
static int sip_poke_peer(struct sip_peer *peer)
{
	struct sip_pvt *p;
	if (!peer->maxms || !peer->addr.sin_addr.s_addr) {
		/* IF we have no IP, or this isn't to be monitored, return
		  imeediately after clearing things out */
		if (peer->pokeexpire > -1)
			ast_sched_del(sched, peer->pokeexpire);
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

	/* Send options to peer's fullcontact */
	if (!ast_strlen_zero(peer->fullcontact)) {
		strncpy (p->fullcontact, peer->fullcontact, sizeof(p->fullcontact));
	}

	if (!ast_strlen_zero(p->tohost))
		strncpy(p->tohost, peer->tohost, sizeof(p->tohost) - 1);
	else
		ast_inet_ntoa(p->tohost, sizeof(p->tohost), peer->addr.sin_addr);

	/* Recalculate our side, and recalculate Call ID */
	if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	build_via(p, p->via, sizeof(p->via));
	build_callid(p->callid, sizeof(p->callid), p->ourip, p->fromdomain);

	if (peer->pokeexpire > -1)
		ast_sched_del(sched, peer->pokeexpire);
	p->peerpoke = peer;
	ast_set_flag(p, SIP_OUTGOING);
#ifdef VOCAL_DATA_HACK
	strncpy(p->username, "__VOCAL_DATA_SHOULD_READ_THE_SIP_SPEC__", sizeof(p->username) - 1);
	transmit_invite(p, "INVITE", 0, NULL, NULL, NULL,NULL,NULL, 0, 1);
#else
	transmit_invite(p, "OPTIONS", 0, NULL, NULL, NULL,NULL,NULL, 0, 1);
#endif
	gettimeofday(&peer->ps, NULL);
	peer->pokeexpire = ast_sched_add(sched, DEFAULT_MAXMS * 2, sip_poke_noanswer, peer);

	return 0;
}

/*--- sip_devicestate: Part of PBX channel interface ---*/
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

	p = find_peer(host, NULL, 1);
	if (p) {
		found++;
		res = AST_DEVICE_UNAVAILABLE;
		if ((p->addr.sin_addr.s_addr || p->defaddr.sin_addr.s_addr) &&
			(!p->maxms || ((p->lastms > -1)  && (p->lastms <= p->maxms)))) {
			/* peer found and valid */
			res = AST_DEVICE_UNKNOWN;
		}
	}
	if (!p && !found) {
		hp = ast_gethostbyname(host, &ahp);
		if (hp)
			res = AST_DEVICE_UNKNOWN;
	}

	if (p)
		ASTOBJ_UNREF(p,sip_destroy_peer);
	return res;
}

/*--- sip_request: PBX interface function -build SIP pvt structure ---*/
/* SIP calls initiated by the PBX arrive here */
static struct ast_channel *sip_request(const char *type, int format, void *data, int *cause)
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
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format %s while capability is %s\n", ast_getformatname(oldformat), ast_getformatname(global_capability));
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
	p->capability = global_capability;

	if (create_addr(p, host)) {
		*cause = AST_CAUSE_UNREGISTERED;
		sip_destroy(p);
		return NULL;
	}
	if (ast_strlen_zero(p->peername) && ext)
		strncpy(p->peername, ext, sizeof(p->peername) - 1);
	/* Recalculate our side, and recalculate Call ID */
	if (ast_sip_ouraddrfor(&p->sa.sin_addr,&p->ourip))
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	build_via(p, p->via, sizeof(p->via));
	build_callid(p->callid, sizeof(p->callid), p->ourip, p->fromdomain);
	
	/* We have an extension to call, don't use the full contact here */
	/* This to enable dialling registred peers with extension dialling,
	   like SIP/peername/extension 	
	   SIP/peername will still use the full contact */
	if (ext) {
		strncpy(p->username, ext, sizeof(p->username) - 1);
		p->fullcontact[0] = 0;	
	}
#if 0
	printf("Setting up to call extension '%s' at '%s'\n", ext ? ext : "<none>", host);
#endif
	p->prefcodec = format;
	ast_mutex_lock(&p->lock);
	tmpc = sip_new(p, AST_STATE_DOWN, host);
	ast_mutex_unlock(&p->lock);
	if (!tmpc)
		sip_destroy(p);
	ast_update_use_count();
	restart_monitor();
	return tmpc;
}

/*--- handle_common_options: Handle flag-type options common to users and peers ---*/
static int handle_common_options(struct ast_flags *flags, struct ast_flags *mask, struct ast_variable *v)
{
	int res = 0;

	if (!strcasecmp(v->name, "trustrpid")) {
		ast_set_flag(mask, SIP_TRUSTRPID);
		ast_set2_flag(flags, ast_true(v->value), SIP_TRUSTRPID);
		res = 1;
	} else if (!strcasecmp(v->name, "useclientcode")) {
		ast_set_flag(mask, SIP_USECLIENTCODE);
		ast_set2_flag(flags, ast_true(v->value), SIP_USECLIENTCODE);
		res = 1;
	} else if (!strcasecmp(v->name, "dtmfmode")) {
		ast_set_flag(mask, SIP_DTMF);
		ast_clear_flag(flags, SIP_DTMF);
		if (!strcasecmp(v->value, "inband"))
			ast_set_flag(flags, SIP_DTMF_INBAND);
		else if (!strcasecmp(v->value, "rfc2833"))
			ast_set_flag(flags, SIP_DTMF_RFC2833);
		else if (!strcasecmp(v->value, "info"))
			ast_set_flag(flags, SIP_DTMF_INFO);
		else {
			ast_log(LOG_WARNING, "Unknown dtmf mode '%s' on line %d, using rfc2833\n", v->value, v->lineno);
			ast_set_flag(flags, SIP_DTMF_RFC2833);
		}
	} else if (!strcasecmp(v->name, "nat")) {
		ast_set_flag(mask, SIP_NAT);
		ast_clear_flag(flags, SIP_NAT);
		if (!strcasecmp(v->value, "never"))
			ast_set_flag(flags, SIP_NAT_NEVER);
		else if (!strcasecmp(v->value, "route"))
			ast_set_flag(flags, SIP_NAT_ROUTE);
		else if (ast_true(v->value))
			ast_set_flag(flags, SIP_NAT_ALWAYS);
		else
			ast_set_flag(flags, SIP_NAT_RFC3581);
	} else if (!strcasecmp(v->name, "canreinvite")) {
		ast_set_flag(mask, SIP_REINVITE);
		ast_clear_flag(flags, SIP_REINVITE);
		if (!strcasecmp(v->value, "update"))
			ast_set_flag(flags, SIP_REINVITE_UPDATE | SIP_CAN_REINVITE);
		else
			ast_set2_flag(flags, ast_true(v->value), SIP_CAN_REINVITE);
	} else if (!strcasecmp(v->name, "insecure")) {
		ast_set_flag(mask, SIP_INSECURE);
		ast_clear_flag(flags, SIP_INSECURE);
		if (!strcasecmp(v->value, "very"))
			ast_set_flag(flags, SIP_INSECURE_VERY);
		else
			ast_set2_flag(flags, ast_true(v->value), SIP_INSECURE_NORMAL);
	} else if (!strcasecmp(v->name, "progressinband")) {
		ast_set_flag(mask, SIP_PROG_INBAND);
		ast_clear_flag(flags, SIP_PROG_INBAND);
		if (strcasecmp(v->value, "never"))
			ast_set_flag(flags, SIP_PROG_INBAND_NO);
		else if (ast_true(v->value))
			ast_set_flag(flags, SIP_PROG_INBAND_YES);
#ifdef OSP_SUPPORT
	} else if (!strcasecmp(v->name, "ospauth")) {
		ast_set_flag(mask, SIP_OSPAUTH);
		ast_clear_flag(flags, SIP_OSPAUTH);
		if (!strcasecmp(v->value, "exclusive"))
			ast_set_flag(flags, SIP_OSPAUTH_EXCLUSIVE);
		else
			ast_set2_flag(flags, ast_true(v->value), SIP_OSPAUTH_YES);
#endif
	} else if (!strcasecmp(v->name, "promiscredir")) {
		ast_set_flag(mask, SIP_PROMISCREDIR);
		ast_set2_flag(flags, ast_true(v->value), SIP_PROMISCREDIR);
		res = 1;
	}

	return res;
}

/*--- build_user: Initiate a SIP user structure from sip.conf ---*/
static struct sip_user *build_user(const char *name, struct ast_variable *v, int realtime)
{
	struct sip_user *user;
	int format;
	struct ast_ha *oldha = NULL;
	char *varname = NULL, *varval = NULL;
	struct ast_variable *tmpvar = NULL;

	user = (struct sip_user *)malloc(sizeof(struct sip_user));
	if (user) {
		struct ast_flags userflags = {(0)};
		struct ast_flags mask = {(0)};

		memset(user, 0, sizeof(struct sip_user));
		suserobjs++;
		ASTOBJ_INIT(user);
		strncpy(user->name, name, sizeof(user->name)-1);
		oldha = user->ha;
		user->ha = NULL;
		/* set the usage flag to a sane staring value*/
		user->inUse = 0;
		user->outUse = 0;
		ast_copy_flags(user, &global_flags, SIP_PROMISCREDIR | SIP_TRUSTRPID | SIP_USECLIENTCODE | SIP_DTMF | SIP_NAT | SIP_REINVITE | SIP_INSECURE | SIP_PROG_INBAND | SIP_OSPAUTH);
		user->capability = global_capability;
		user->prefs = prefs;
		/* set default context */
		strncpy(user->context, default_context, sizeof(user->context)-1);
		strncpy(user->language, default_language, sizeof(user->language)-1);
		strncpy(user->musicclass, global_musicclass, sizeof(user->musicclass)-1);
		while(v) {
			if (handle_common_options(&userflags, &mask, v)) {
				v = v->next;
				continue;
			}

			if (!strcasecmp(v->name, "context")) {
				strncpy(user->context, v->value, sizeof(user->context) - 1);
			} else if (!strcasecmp(v->name, "setvar")) {
				varname = ast_strdupa(v->value);
				if (varname && (varval = strchr(varname,'='))) {
					*varval = '\0';
					varval++;
					if((tmpvar = ast_variable_new(varname, varval))) {
						tmpvar->next = user->chanvars;
						user->chanvars = tmpvar;
					}

				}
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny")) {
				user->ha = ast_append_ha(v->name, v->value, user->ha);
			} else if (!strcasecmp(v->name, "secret")) {
				strncpy(user->secret, v->value, sizeof(user->secret)-1); 
			} else if (!strcasecmp(v->name, "md5secret")) {
				strncpy(user->md5secret, v->value, sizeof(user->md5secret)-1);
			} else if (!strcasecmp(v->name, "callerid")) {
				ast_callerid_split(v->value, user->cid_name, sizeof(user->cid_name), user->cid_num, sizeof(user->cid_num));
			} else if (!strcasecmp(v->name, "callgroup")) {
				user->callgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "pickupgroup")) {
				user->pickupgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "language")) {
				strncpy(user->language, v->value, sizeof(user->language)-1);
			} else if (!strcasecmp(v->name, "musiconhold")) {
				strncpy(user->musicclass, v->value, sizeof(user->musicclass)-1);
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
				ast_parse_allow_disallow(&user->prefs, &user->capability, v->value, 1);
			} else if (!strcasecmp(v->name, "disallow")) {
				ast_parse_allow_disallow(&user->prefs, &user->capability, v->value, 0);
			} else if (!strcasecmp(v->name, "callingpres")) {
				user->callingpres = atoi(v->value);
			}
			/*else if (strcasecmp(v->name,"type"))
			 *	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
			 */
			v = v->next;
		}
		ast_copy_flags(user, &userflags, mask.flags);
	}
	ast_free_ha(oldha);
	return user;
}

/*--- temp_peer: Create temporary peer (used in autocreatepeer mode) ---*/
static struct sip_peer *temp_peer(char *name)
{
	struct sip_peer *peer;
	peer = malloc(sizeof(struct sip_peer));
	if (!peer)
		return NULL;

	memset(peer, 0, sizeof(struct sip_peer));
	apeerobjs++;
	ASTOBJ_INIT(peer);

	peer->expire = -1;
	peer->pokeexpire = -1;
	strncpy(peer->name, name, sizeof(peer->name)-1);
	ast_copy_flags(peer, &global_flags, SIP_PROMISCREDIR | SIP_USEREQPHONE | SIP_TRUSTRPID | SIP_USECLIENTCODE | SIP_DTMF | SIP_NAT | SIP_REINVITE | SIP_INSECURE | SIP_PROG_INBAND | SIP_OSPAUTH);
	strncpy(peer->context, default_context, sizeof(peer->context)-1);
	strncpy(peer->language, default_language, sizeof(peer->language)-1);
	strncpy(peer->musicclass, global_musicclass, sizeof(peer->musicclass)-1);
	peer->addr.sin_port = htons(DEFAULT_SIP_PORT);
	peer->addr.sin_family = AF_INET;
	peer->expiry = expiry;
	peer->capability = global_capability;
	peer->rtptimeout = global_rtptimeout;
	peer->rtpholdtimeout = global_rtpholdtimeout;
	peer->rtpkeepalive = global_rtpkeepalive;
	ast_set_flag(peer, SIP_SELFDESTRUCT);
	ast_set_flag(peer, SIP_DYNAMIC);
	peer->prefs = prefs;
	reg_source_db(peer);
	return peer;
}

/*--- build_peer: Build peer from config file ---*/
static struct sip_peer *build_peer(const char *name, struct ast_variable *v, int realtime)
{
	struct sip_peer *peer = NULL;
	struct ast_ha *oldha = NULL;
	int maskfound=0;
	int obproxyfound=0;
	int found=0;
	int format=0;		/* Ama flags */
	time_t regseconds;
	char *varname = NULL, *varval = NULL;
	struct ast_variable *tmpvar = NULL;

	if (!realtime)
		/* Note we do NOT use find_peer here, to avoid realtime recursion */
		peer = ASTOBJ_CONTAINER_FIND_UNLINK(&peerl, name);

	if (peer) {
		/* Already in the list, remove it and it will be added back (or FREE'd)  */
		found++;
 	} else {
		peer = malloc(sizeof(struct sip_peer));
		if (peer) {
			memset(peer, 0, sizeof(struct sip_peer));
			if (realtime)
				rpeerobjs++;
			else
				speerobjs++;
			ASTOBJ_INIT(peer);
			peer->expire = -1;
			peer->pokeexpire = -1;
		}
	}
	/* Note that our peer HAS had its reference count incrased */
	if (peer) {
		struct ast_flags peerflags = {(0)};
		struct ast_flags mask = {(0)};

		peer->lastmsgssent = -1;
		if (!found) {
			if (name)
				strncpy(peer->name, name, sizeof(peer->name)-1);
			peer->addr.sin_port = htons(DEFAULT_SIP_PORT);
			peer->addr.sin_family = AF_INET;
			peer->defaddr.sin_family = AF_INET;
			peer->expiry = expiry;
		}
		strncpy(peer->context, default_context, sizeof(peer->context)-1);
		strncpy(peer->language, default_language, sizeof(peer->language)-1);
		strncpy(peer->musicclass, global_musicclass, sizeof(peer->musicclass)-1);
		ast_copy_flags(peer, &global_flags, SIP_USEREQPHONE);
		peer->secret[0] = '\0';
		peer->md5secret[0] = '\0';
		peer->cid_num[0] = '\0';
		peer->cid_name[0] = '\0';
		peer->fromdomain[0] = '\0';
		peer->fromuser[0] = '\0';
		peer->regexten[0] = '\0';
		peer->mailbox[0] = '\0';
		peer->callgroup = 0;
		peer->pickupgroup = 0;
		peer->rtpkeepalive = global_rtpkeepalive;
		peer->maxms = 0;
		peer->prefs = prefs;
		oldha = peer->ha;
		peer->ha = NULL;
		peer->addr.sin_family = AF_INET;
		ast_copy_flags(peer, &global_flags, SIP_PROMISCREDIR | SIP_TRUSTRPID | SIP_USECLIENTCODE | SIP_DTMF | SIP_REINVITE | SIP_INSECURE | SIP_PROG_INBAND | SIP_OSPAUTH);
		peer->capability = global_capability;
		peer->rtptimeout = global_rtptimeout;
		peer->rtpholdtimeout = global_rtpholdtimeout;
		while(v) {
			if (handle_common_options(&peerflags, &mask, v)) {
				v = v->next;
				continue;
			}

			if (realtime && !strcasecmp(v->name, "regseconds")) {
				if (sscanf(v->value, "%li", &regseconds) != 1)
					regseconds = 0;
			} else if (realtime && !strcasecmp(v->name, "ipaddr")) {
				inet_aton(v->value, &(peer->addr.sin_addr));
			} else if (realtime && !strcasecmp(v->name, "name"))
				strncpy(peer->name, v->value, sizeof(peer->name)-1);
			else if (!strcasecmp(v->name, "secret")) 
				strncpy(peer->secret, v->value, sizeof(peer->secret)-1);
			else if (!strcasecmp(v->name, "md5secret")) 
				strncpy(peer->md5secret, v->value, sizeof(peer->md5secret)-1);
			else if (!strcasecmp(v->name, "callerid")) {
				ast_callerid_split(v->value, peer->cid_name, sizeof(peer->cid_name), peer->cid_num, sizeof(peer->cid_num));
			} else if (!strcasecmp(v->name, "context"))
				strncpy(peer->context, v->value, sizeof(peer->context)-1);
			else if (!strcasecmp(v->name, "fromdomain"))
				strncpy(peer->fromdomain, v->value, sizeof(peer->fromdomain)-1);
			else if (!strcasecmp(v->name, "usereqphone"))
				ast_set2_flag(peer, ast_true(v->value), SIP_USEREQPHONE);
			else if (!strcasecmp(v->name, "fromuser"))
				strncpy(peer->fromuser, v->value, sizeof(peer->fromuser)-1);
			else if (!strcasecmp(v->name, "host") || !strcasecmp(v->name, "outboundproxy")) {
				if (!strcasecmp(v->value, "dynamic")) {
					if (!strcasecmp(v->name, "outboundproxy") || obproxyfound) {
						ast_log(LOG_WARNING, "You can't have a dynamic outbound proxy, you big silly head at line %d.\n", v->lineno);
					} else {
						/* They'll register with us */
						ast_set_flag(peer, SIP_DYNAMIC);
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
					}
				} else {
					/* Non-dynamic.  Make sure we become that way if we're not */
					if (peer->expire > -1)
						ast_sched_del(sched, peer->expire);
					peer->expire = -1;
					ast_clear_flag(peer, SIP_DYNAMIC);	
					if (!obproxyfound || !strcasecmp(v->name, "outboundproxy")) {
						if (ast_get_ip_or_srv(&peer->addr, v->value, "_sip._udp")) {
							ASTOBJ_UNREF(peer, sip_destroy_peer);
							return NULL;
						}
					}
					if (!strcasecmp(v->name, "outboundproxy"))
						obproxyfound=1;
					else
						strncpy(peer->tohost, v->value, sizeof(peer->tohost) - 1);
				}
				if (!maskfound)
					inet_aton("255.255.255.255", &peer->mask);
			} else if (!strcasecmp(v->name, "defaultip")) {
				if (ast_get_ip(&peer->defaddr, v->value)) {
					ASTOBJ_UNREF(peer, sip_destroy_peer);
					return NULL;
				}
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny")) {
				peer->ha = ast_append_ha(v->name, v->value, peer->ha);
			} else if (!strcasecmp(v->name, "mask")) {
				maskfound++;
				inet_aton(v->value, &peer->mask);
			} else if (!strcasecmp(v->name, "port") || !strcasecmp(v->name, "bindport")) {
				if (!realtime && ast_test_flag(peer, SIP_DYNAMIC))
					peer->defaddr.sin_port = htons(atoi(v->value));
				else
					peer->addr.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "callingpres")) {
				peer->callingpres = atoi(v->value);
			} else if (!strcasecmp(v->name, "username")) {
				strncpy(peer->username, v->value, sizeof(peer->username)-1);
			} else if (!strcasecmp(v->name, "language")) {
				strncpy(peer->language, v->value, sizeof(peer->language)-1);
			} else if (!strcasecmp(v->name, "regexten")) {
				strncpy(peer->regexten, v->value, sizeof(peer->regexten)-1);
			} else if (!strcasecmp(v->name, "incominglimit")) {
				peer->incominglimit = atoi(v->value);
				if (peer->incominglimit < 0)
					peer->incominglimit = 0;
			} else if (!strcasecmp(v->name, "outgoinglimit")) {
				peer->outgoinglimit = atoi(v->value);
				if (peer->outgoinglimit < 0)
					peer->outgoinglimit = 0;
			} else if (!strcasecmp(v->name, "amaflags")) {
				format = ast_cdr_amaflags2int(v->value);
				if (format < 0) {
					ast_log(LOG_WARNING, "Invalid AMA Flags for peer: %s at line %d\n", v->value, v->lineno);
				} else {
					peer->amaflags = format;
				}
			} else if (!strcasecmp(v->name, "accountcode")) {
				strncpy(peer->accountcode, v->value, sizeof(peer->accountcode)-1);
			} else if (!strcasecmp(v->name, "musiconhold")) {
				strncpy(peer->musicclass, v->value, sizeof(peer->musicclass)-1);
			} else if (!strcasecmp(v->name, "mailbox")) {
				strncpy(peer->mailbox, v->value, sizeof(peer->mailbox)-1);
			} else if (!strcasecmp(v->name, "callgroup")) {
				peer->callgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "pickupgroup")) {
				peer->pickupgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "allow")) {
				ast_parse_allow_disallow(&peer->prefs, &peer->capability, v->value, 1);
			} else if (!strcasecmp(v->name, "disallow")) {
				ast_parse_allow_disallow(&peer->prefs, &peer->capability, v->value, 0);
			} else if (!strcasecmp(v->name, "rtptimeout")) {
				if ((sscanf(v->value, "%d", &peer->rtptimeout) != 1) || (peer->rtptimeout < 0)) {
					ast_log(LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
					peer->rtptimeout = global_rtptimeout;
				}
			} else if (!strcasecmp(v->name, "rtpholdtimeout")) {
				if ((sscanf(v->value, "%d", &peer->rtpholdtimeout) != 1) || (peer->rtpholdtimeout < 0)) {
					ast_log(LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
					peer->rtpholdtimeout = global_rtpholdtimeout;
				}
			} else if (!strcasecmp(v->name, "rtpkeepalive")) {
				if ((sscanf(v->value, "%d", &peer->rtpkeepalive) != 1) || (peer->rtpkeepalive < 0)) {
					ast_log(LOG_WARNING, "'%s' is not a valid RTP keepalive time at line %d.  Using default.\n", v->value, v->lineno);
					peer->rtpkeepalive = global_rtpkeepalive;
				}
			} else if (!strcasecmp(v->name, "setvar")) {
				/* Set peer channel variable */
				varname = ast_strdupa(v->value);
				if (varname && (varval = strchr(varname,'='))) {
					*varval = '\0';
					varval++;
					if((tmpvar = ast_variable_new(varname, varval))) {
						tmpvar->next = peer->chanvars;
						peer->chanvars = tmpvar;
					}
				}
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
		if (realtime && ast_test_flag(peer, SIP_DYNAMIC)) {
			time_t nowtime;

			time(&nowtime);
			if ((nowtime - regseconds) > 0) {
				memset(&peer->addr, 0, sizeof(peer->addr));
				if (option_debug)
					ast_log(LOG_DEBUG, "Bah, we're expired (%ld/%ld/%ld)!\n", nowtime - regseconds, regseconds, nowtime);
			}
		}
		ast_copy_flags(peer, &peerflags, mask.flags);
		if (!found && ast_test_flag(peer, SIP_DYNAMIC))
			reg_source_db(peer);
		ASTOBJ_UNMARK(peer);
	}
	ast_free_ha(oldha);
	return peer;
}

/*--- reload_config: Re-read SIP.conf config file ---*/
/*	This function reloads all config data, except for
	active peers (with registrations). They will only
	change configuration data at restart, not at reload.
	SIP debug and recordhistory state will not change
 */
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
	char iabuf[INET_ADDRSTRLEN];
	struct ast_flags dummy;
	
	cfg = ast_config_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, SIP disabled\n", config);
		return 0;
	}
	
	
	/* Reset IP addresses  */
	memset(&bindaddr, 0, sizeof(bindaddr));
	memset(&localaddr, 0, sizeof(localaddr));
	memset(&externip, 0, sizeof(externip));
	memset(&prefs, 0 , sizeof(struct ast_codec_pref));

	/* Initialize some reasonable defaults at SIP reload */
	strncpy(default_context, DEFAULT_CONTEXT, sizeof(default_context) - 1);
	default_language[0] = '\0';
	default_fromdomain[0] = '\0';
	externhost[0] = '\0';
	externexpire = 0;
	externrefresh = 10;
	strncpy(default_useragent, DEFAULT_USERAGENT, sizeof(default_useragent) - 1);
	strncpy(default_notifymime, DEFAULT_NOTIFYMIME, sizeof(default_notifymime) - 1);
	strncpy(global_realm, DEFAULT_REALM, sizeof(global_realm) - 1);
	strncpy(global_musicclass, "default", sizeof(global_musicclass) - 1);
	strncpy(default_callerid, DEFAULT_CALLERID, sizeof(default_callerid) - 1);
	memset(&outboundproxyip, 0, sizeof(outboundproxyip));
	outboundproxyip.sin_port = htons(DEFAULT_SIP_PORT);
	outboundproxyip.sin_family = AF_INET;	/* Type of address: IPv4 */
	videosupport = 0;
	compactheaders = 0;
	relaxdtmf = 0;
	callevents = 0;
	ourport = DEFAULT_SIP_PORT;
	global_rtptimeout = 0;
	global_rtpholdtimeout = 0;
	global_rtpkeepalive = 0;
	pedanticsipchecking = 0;
	ast_clear_flag(&global_flags, AST_FLAGS_ALL);
	ast_set_flag(&global_flags, SIP_DTMF_RFC2833);
	ast_set_flag(&global_flags, SIP_NAT_RFC3581);
	ast_set_flag(&global_flags, SIP_CAN_REINVITE);
	global_mwitime = DEFAULT_MWITIME;
	srvlookup = 0;
	autocreatepeer = 0;
	regcontext[0] = '\0';
	tos = 0;
	expiry = DEFAULT_EXPIRY;
	global_allowguest = 1;

	/* Read the [general] config section of sip.conf (or from realtime config) */
	v = ast_variable_browse(cfg, "general");
	while(v) {
		if (handle_common_options(&global_flags, &dummy, v)) {
			v = v->next;
			continue;
		}

		/* Create the interface list */
		if (!strcasecmp(v->name, "context")) {
			strncpy(default_context, v->value, sizeof(default_context)-1);
		} else if (!strcasecmp(v->name, "realm")) {
			strncpy(global_realm, v->value, sizeof(global_realm)-1);
			global_realm[sizeof(global_realm)-1] = '\0';
		} else if (!strcasecmp(v->name, "useragent")) {
			strncpy(default_useragent, v->value, sizeof(default_useragent)-1);
			ast_log(LOG_DEBUG, "Setting User Agent Name to %s\n",
				default_useragent);
		} else if (!strcasecmp(v->name, "rtcachefriends")) {
			ast_set2_flag((&global_flags_page2), ast_true(v->value), SIP_PAGE2_RTCACHEFRIENDS);	
		} else if (!strcasecmp(v->name, "rtnoupdate")) {
			ast_set2_flag((&global_flags_page2), ast_true(v->value), SIP_PAGE2_RTNOUPDATE);	
		} else if (!strcasecmp(v->name, "rtautoclear")) {
			int i = atoi(v->value);
			if(i > 0)
				global_rtautoclear = i;
			else
				i = 0;
			ast_set2_flag((&global_flags_page2), i || ast_true(v->value), SIP_PAGE2_RTAUTOCLEAR);
		} else if (!strcasecmp(v->name, "usereqphone")) {
			ast_set2_flag((&global_flags), ast_true(v->value), SIP_USEREQPHONE);	
		} else if (!strcasecmp(v->name, "relaxdtmf")) {
			relaxdtmf = ast_true(v->value);
		} else if (!strcasecmp(v->name, "checkmwi")) {
			if ((sscanf(v->value, "%d", &global_mwitime) != 1) || (global_mwitime < 0)) {
				ast_log(LOG_WARNING, "'%s' is not a valid MWI time setting at line %d.  Using default (10).\n", v->value, v->lineno);
				global_mwitime = DEFAULT_MWITIME;
			}
		} else if (!strcasecmp(v->name, "rtptimeout")) {
			if ((sscanf(v->value, "%d", &global_rtptimeout) != 1) || (global_rtptimeout < 0)) {
				ast_log(LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
				global_rtptimeout = 0;
			}
		} else if (!strcasecmp(v->name, "rtpholdtimeout")) {
			if ((sscanf(v->value, "%d", &global_rtpholdtimeout) != 1) || (global_rtpholdtimeout < 0)) {
				ast_log(LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
				global_rtpholdtimeout = 0;
			}
		} else if (!strcasecmp(v->name, "rtpkeepalive")) {
			if ((sscanf(v->value, "%d", &global_rtpkeepalive) != 1) || (global_rtpkeepalive < 0)) {
				ast_log(LOG_WARNING, "'%s' is not a valid RTP keepalive time at line %d.  Using default.\n", v->value, v->lineno);
				global_rtpkeepalive = 0;
			}
		} else if (!strcasecmp(v->name, "videosupport")) {
			videosupport = ast_true(v->value);
		} else if (!strcasecmp(v->name, "compactheaders")) {
			compactheaders = ast_true(v->value);
		} else if (!strcasecmp(v->name, "notifymimetype")) {
			strncpy(default_notifymime, v->value, sizeof(default_notifymime) - 1);
		} else if (!strcasecmp(v->name, "musicclass") || !strcasecmp(v->name, "musiconhold")) {
			strncpy(global_musicclass, v->value, sizeof(global_musicclass) - 1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(default_language, v->value, sizeof(default_language)-1);
		} else if (!strcasecmp(v->name, "regcontext")) {
			strncpy(regcontext, v->value, sizeof(regcontext) - 1);
			/* Create context if it doesn't exist already */
			if (!ast_context_find(regcontext))
				ast_context_create(NULL, regcontext, channeltype);
		} else if (!strcasecmp(v->name, "callerid")) {
			strncpy(default_callerid, v->value, sizeof(default_callerid)-1);
		} else if (!strcasecmp(v->name, "fromdomain")) {
			strncpy(default_fromdomain, v->value, sizeof(default_fromdomain)-1);
		} else if (!strcasecmp(v->name, "outboundproxy")) {
			if (ast_get_ip_or_srv(&outboundproxyip, v->value, "_sip._udp") < 0)
				ast_log(LOG_WARNING, "Unable to locate host '%s'\n", v->value);
		} else if (!strcasecmp(v->name, "outboundproxyport")) {
			/* Port needs to be after IP */
			sscanf(v->value, "%i", &format);
			outboundproxyip.sin_port = htons(format);
		} else if (!strcasecmp(v->name, "autocreatepeer")) {
			autocreatepeer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "srvlookup")) {
			srvlookup = ast_true(v->value);
		} else if (!strcasecmp(v->name, "pedantic")) {
			pedanticsipchecking = ast_true(v->value);
		} else if (!strcasecmp(v->name, "maxexpirey") || !strcasecmp(v->name, "maxexpiry")) {
			max_expiry = atoi(v->value);
			if (max_expiry < 1)
				max_expiry = DEFAULT_MAX_EXPIRY;
		} else if (!strcasecmp(v->name, "defaultexpiry") || !strcasecmp(v->name, "defaultexpirey")) {
			default_expiry = atoi(v->value);
			if (default_expiry < 1)
				default_expiry = DEFAULT_DEFAULT_EXPIRY;
		} else if (!strcasecmp(v->name, "registertimeout")){
			global_reg_timeout = atoi(v->value);
			if (global_reg_timeout < 1)
				global_reg_timeout = DEFAULT_REGISTRATION_TIMEOUT;
		} else if (!strcasecmp(v->name, "bindaddr")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
			} else {
				memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
			}
		} else if (!strcasecmp(v->name, "localnet")) {
			struct ast_ha *na;
			if (!(na = ast_append_ha("d", v->value, localaddr)))
				ast_log(LOG_WARNING, "Invalid localnet value: %s\n", v->value);
			else
				localaddr = na;
		} else if (!strcasecmp(v->name, "localmask")) {
			ast_log(LOG_WARNING, "Use of localmask is no long supported -- use localnet with mask syntax\n");
		} else if (!strcasecmp(v->name, "externip")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) 
				ast_log(LOG_WARNING, "Invalid address for externip keyword: %s\n", v->value);
			else
				memcpy(&externip.sin_addr, hp->h_addr, sizeof(externip.sin_addr));
			externexpire = 0;
		} else if (!strcasecmp(v->name, "externhost")) {
			strncpy(externhost, v->value, sizeof(externhost) - 1);
			if (!(hp = ast_gethostbyname(externhost, &ahp))) 
				ast_log(LOG_WARNING, "Invalid address for externhost keyword: %s\n", externhost);
			else
				memcpy(&externip.sin_addr, hp->h_addr, sizeof(externip.sin_addr));
			time(&externexpire);
		} else if (!strcasecmp(v->name, "externrefresh")) {
			if (sscanf(v->value, "%i", &externrefresh) != 1) {
				ast_log(LOG_WARNING, "Invalid externrefresh value '%s', must be an integer >0 at line %d\n", v->value, v->lineno);
				externrefresh = 10;
			}
		} else if (!strcasecmp(v->name, "allow")) {
			ast_parse_allow_disallow(&prefs, &global_capability, v->value, 1);
		} else if (!strcasecmp(v->name, "disallow")) {
			ast_parse_allow_disallow(&prefs, &global_capability, v->value, 0);
		} else if (!strcasecmp(v->name, "register")) {
			sip_register(v->value, v->lineno);
		} else if (!strcasecmp(v->name, "recordhistory")) {
			recordhistory = ast_true(v->value);
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
		} else if (!strcasecmp(v->name, "callevents")) {
			callevents = ast_true(v->value);
		}
		/* else if (strcasecmp(v->name,"type"))
		 *	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
		 */
		 v = v->next;
	}
	
	/* Load peers, users and friends */
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (utype) {
				if (!strcasecmp(utype, "user") || !strcasecmp(utype, "friend")) {
					user = build_user(cat, ast_variable_browse(cfg, cat), 0);
					if (user) {
						ASTOBJ_CONTAINER_LINK(&userl,user);
						ASTOBJ_UNREF(user, sip_destroy_user);
					}
				}
				if (!strcasecmp(utype, "peer") || !strcasecmp(utype, "friend")) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat), 0);
					if (peer) {
						ASTOBJ_CONTAINER_LINK(&peerl,peer);
						ASTOBJ_UNREF(peer, sip_destroy_peer);
					}
				} else if (strcasecmp(utype, "user")) {
					ast_log(LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, "sip.conf");
				}
			} else
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
		}
		cat = ast_category_browse(cfg, cat);
	}
	if (ast_find_ourip(&__ourip, bindaddr)) {
		ast_log(LOG_WARNING, "Unable to get own IP address, SIP disabled\n");
		return 0;
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
						ast_inet_ntoa(iabuf, sizeof(iabuf), bindaddr.sin_addr), ntohs(bindaddr.sin_port),
							strerror(errno));
				close(sipsock);
				sipsock = -1;
			} else {
				if (option_verbose > 1) { 
						ast_verbose(VERBOSE_PREFIX_2 "SIP Listening on %s:%d\n", 
					ast_inet_ntoa(iabuf, sizeof(iabuf), bindaddr.sin_addr), ntohs(bindaddr.sin_port));
					ast_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);
				}
				if (setsockopt(sipsock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))) 
					ast_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
			}
		}
	}
	ast_mutex_unlock(&netlock);

	/* Release configuration from memory */
	ast_config_destroy(cfg);

	/* Load the list of manual NOTIFY types to support */
	if (notify_types)
		ast_config_destroy(notify_types);
	notify_types = ast_config_load(notify_config);

	return 0;
}

/*--- sip_get_rtp_peer: Returns null if we can't reinvite */
static struct ast_rtp *sip_get_rtp_peer(struct ast_channel *chan)
{
	struct sip_pvt *p;
	struct ast_rtp *rtp = NULL;
	p = chan->pvt->pvt;
	if (p) {
		ast_mutex_lock(&p->lock);
		if (p->rtp && ast_test_flag(p, SIP_CAN_REINVITE))
			rtp =  p->rtp;
		ast_mutex_unlock(&p->lock);
	}
	return rtp;
}

static struct ast_rtp *sip_get_vrtp_peer(struct ast_channel *chan)
{
	struct sip_pvt *p;
	struct ast_rtp *rtp = NULL;
	p = chan->pvt->pvt;
	if (p) {
		ast_mutex_lock(&p->lock);
		if (p->vrtp && ast_test_flag(p, SIP_CAN_REINVITE))
			rtp = p->vrtp;
		ast_mutex_unlock(&p->lock);
	}
	return rtp;
}

/*--- sip_set_rtp_peer: Set the RTP peer for this call ---*/
static int sip_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, struct ast_rtp *vrtp, int codecs)
{
	struct sip_pvt *p;
	p = chan->pvt->pvt;
	if (p) {
		ast_mutex_lock(&p->lock);
		if (rtp)
			ast_rtp_get_peer(rtp, &p->redirip);
		else
			memset(&p->redirip, 0, sizeof(p->redirip));
		if (vrtp)
			ast_rtp_get_peer(vrtp, &p->vredirip);
		else
			memset(&p->vredirip, 0, sizeof(p->vredirip));
		p->redircodecs = codecs;
		if (!ast_test_flag(p, SIP_GOTREFER)) {
			if (!p->pendinginvite)
				transmit_reinvite_with_sdp(p);
			else if (!ast_test_flag(p, SIP_PENDINGBYE)) {
				ast_log(LOG_DEBUG, "Deferring reinvite on '%s'\n", p->callid);
				ast_set_flag(p, SIP_NEEDREINVITE);	
			}
		}
		/* Reset lastrtprx timer */
		time(&p->lastrtprx);
		time(&p->lastrtptx);
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	return -1;
}

static char *synopsis_dtmfmode = "Change the dtmfmode for a SIP call";
static char *descrip_dtmfmode = "SIPDtmfMode(inband|info|rfc2833): Changes the dtmfmode for a SIP call\n";
static char *app_dtmfmode = "SIPDtmfMode";

static char *app_sipaddheader = "SIPAddHeader";
static char *synopsis_sipaddheader = "Add a SIP header to the outbound call";


static char *descrip_sipaddheader = ""
"  SIPAddHeader(Header: Content)\n"
"Adds a header to a SIP call placed with DIAL.\n"
"Remember to user the X-header if you are adding non-standard SIP\n"
"headers, like \"X-Asterisk-Accuntcode:\". Use this with care.\n"
"Adding the wrong headers may jeopardize the SIP dialog.\n"
"Always returns 0\n";

static char *app_sipgetheader = "SIPGetHeader";
static char *synopsis_sipgetheader = "Get a SIP header from an incoming call";
 
static char *descrip_sipgetheader = ""
"  SIPGetHeader(var=headername): \n"
"Sets a channel variable to the content of a SIP header\n"
"Skips to priority+101 if header does not exist\n"
"Otherwise returns 0\n";


/*--- sip_dtmfmode: change the DTMFmode for a SIP call (application) ---*/
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
	if (chan->type != channeltype) {
		ast_log(LOG_WARNING, "Call this application only on SIP incoming calls\n");
		ast_mutex_unlock(&chan->lock);
		return 0;
	}
	p = chan->pvt->pvt;
	if (p) {
		ast_mutex_lock(&p->lock);
		if (!strcasecmp(mode,"info")) {
			ast_clear_flag(p, SIP_DTMF);
			ast_set_flag(p, SIP_DTMF_INFO);
		}
		else if (!strcasecmp(mode,"rfc2833")) {
			ast_clear_flag(p, SIP_DTMF);
			ast_set_flag(p, SIP_DTMF_RFC2833);
		}
		else if (!strcasecmp(mode,"inband")) { 
			ast_clear_flag(p, SIP_DTMF);
			ast_set_flag(p, SIP_DTMF_INBAND);
		}
		else
			ast_log(LOG_WARNING, "I don't know about this dtmf mode: %s\n",mode);
		if (ast_test_flag(p, SIP_DTMF) == SIP_DTMF_INBAND) {
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

/*--- sip_addheader: Add a SIP header ---*/
static int sip_addheader(struct ast_channel *chan, void *data)
{
	int arglen;
	int no = 0;
	int ok = 0;
	char *content = (char *) NULL;
	char varbuf[128];
	
	arglen = strlen(data);
	if (!arglen) {
		ast_log(LOG_WARNING, "This application requires the argument: Header\n");
		return 0;
	}
       ast_mutex_lock(&chan->lock);
       if (chan->type != channeltype) {
               ast_log(LOG_WARNING, "Call this application only on incoming SIP calls\n");
               ast_mutex_unlock(&chan->lock);
               return 0;
       }

	/* Check for headers */
	while (!ok && no <= 50) {
		no++;
		snprintf(varbuf, sizeof(varbuf), "_SIPADDHEADER%.2d", no);
		content = pbx_builtin_getvar_helper(chan, varbuf);

		if (!content)
			ok = 1;
	}
	if (ok) {
		pbx_builtin_setvar_helper (chan, varbuf, data);
		if (sipdebug)
			ast_log(LOG_DEBUG,"SIP Header added \"%s\" as %s\n", (char *) data, varbuf);
	} else {
               ast_log(LOG_WARNING, "Too many SIP headers added, max 50\n");
	}
        ast_mutex_unlock(&chan->lock);
	return 0;
}


/*--- sip_getheader: Get a SIP header (dialplan app) ---*/
static int sip_getheader(struct ast_channel *chan, void *data)
{
	struct sip_pvt *p;
	char *argv, *varname = NULL, *header = NULL, *content;
	
	argv = ast_strdupa(data);
	if (!argv) {
		ast_log(LOG_DEBUG, "Memory allocation failed\n");
		return 0;
	}

	if (strchr (argv, '=') ) {	/* Pick out argumenet */
		varname = strsep (&argv, "=");
		header = strsep (&argv, "\0");
	}

	if (!varname || !header) {
		ast_log(LOG_DEBUG, "SipGetHeader: Ignoring command, Syntax error in argument\n");
		return 0;
	}

	ast_mutex_lock(&chan->lock);
	if (chan->type != channeltype) {
		ast_log(LOG_WARNING, "Call this application only on incoming SIP calls\n");
		ast_mutex_unlock(&chan->lock);
		return 0;
	}

	p = chan->pvt->pvt;
	content = get_header(&p->initreq, header);	/* Get the header */
	if (!ast_strlen_zero(content)) {
		pbx_builtin_setvar_helper(chan, varname, content);
	} else {
		ast_log(LOG_WARNING,"SIP Header %s not found for channel variable %s\n", header, varname);
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
	}
	
	ast_mutex_unlock(&chan->lock);
	return 0;
}

/*--- sip_get_codec: Return peers codec ---*/
static int sip_get_codec(struct ast_channel *chan)
{
	struct sip_pvt *p = chan->pvt->pvt;
	return p->peercapability;	
}

/*--- sip_rtp: Interface structure with callbacks used to connect to rtp module --*/
static struct ast_rtp_protocol sip_rtp = {
	get_rtp_info: sip_get_rtp_peer,
	get_vrtp_info: sip_get_vrtp_peer,
	set_rtp_peer: sip_set_rtp_peer,
	get_codec: sip_get_codec,
};

/*--- delete_users: Delete all registred users ---*/
/*      Also, check registations with other SIP proxies */
static void delete_users(void)
{
	/* Delete all users */
	ASTOBJ_CONTAINER_DESTROYALL(&userl,sip_destroy_user);
	ASTOBJ_CONTAINER_DESTROYALL(&regl,sip_registry_destroy);
	ASTOBJ_CONTAINER_MARKALL(&peerl);
}

/*--- prune_peers: Delete all peers marked for deletion ---*/
static void prune_peers(void)
{
	/* Prune peers who still are supposed to be deleted */
	ASTOBJ_CONTAINER_PRUNE_MARKED(&peerl,sip_destroy_peer);
}

/*--- sip_poke_all_peers: Send a poke to all known peers */
static void sip_poke_all_peers(void)
{
	ASTOBJ_CONTAINER_TRAVERSE(&peerl, 1, do {
		ASTOBJ_WRLOCK(iterator);
		sip_poke_peer(iterator);
		ASTOBJ_UNLOCK(iterator);
	} while (0)
	);
}

/*--- sip_send_all_registers: Send all known registrations */
static void sip_send_all_registers(void)
{
	ASTOBJ_CONTAINER_TRAVERSE(&regl, 1, do {
		ASTOBJ_WRLOCK(iterator);
		__sip_do_register(iterator);
		ASTOBJ_UNLOCK(iterator);
	} while (0)
	);
}

/*--- sip_do_reload: Reload module */
static int sip_do_reload(void)
{
	delete_users();
	reload_config();
	prune_peers();
	sip_poke_all_peers();
	sip_send_all_registers();

	return 0;
}

/*--- sip_reload: Force reload of module from cli ---*/
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

/*--- load_module: PBX load module - initialization ---*/
int load_module()
{
	int res;

	ASTOBJ_CONTAINER_INIT(&userl);
	ASTOBJ_CONTAINER_INIT(&peerl);
	ASTOBJ_CONTAINER_INIT(&regl);
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
		if (ast_channel_register_ex(channeltype, tdesc, ((AST_FORMAT_MAX_AUDIO << 1) - 1), sip_request, sip_devicestate)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", channeltype);
			return -1;
		}
		ast_cli_register(&cli_notify);
		ast_cli_register(&cli_show_users);
		ast_cli_register(&cli_show_user);
		ast_cli_register(&cli_show_objects);
		ast_cli_register(&cli_show_subscriptions);
		ast_cli_register(&cli_show_channels);
		ast_cli_register(&cli_show_channel);
		ast_cli_register(&cli_show_history);
		ast_cli_register(&cli_prune_realtime);
		ast_cli_register(&cli_show_peer);
		ast_cli_register(&cli_show_peers);
		ast_cli_register(&cli_show_registry);
		ast_cli_register(&cli_debug);
		ast_cli_register(&cli_debug_ip);
		ast_cli_register(&cli_debug_peer);
		ast_cli_register(&cli_no_debug);
		ast_cli_register(&cli_history);
		ast_cli_register(&cli_no_history);
		ast_cli_register(&cli_sip_reload);
		ast_cli_register(&cli_inuse_show);
		sip_rtp.type = channeltype;
		ast_rtp_proto_register(&sip_rtp);
		ast_register_application(app_dtmfmode, sip_dtmfmode, synopsis_dtmfmode, descrip_dtmfmode);
		ast_register_application(app_sipaddheader, sip_addheader, synopsis_sipaddheader, descrip_sipaddheader);
		ast_register_application(app_sipgetheader, sip_getheader, synopsis_sipgetheader, descrip_sipgetheader);
		sip_poke_all_peers();
		sip_send_all_registers();
		
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
	ast_unregister_application(app_sipaddheader);
	ast_unregister_application(app_sipgetheader);
	ast_cli_unregister(&cli_notify);
	ast_cli_unregister(&cli_show_users);
	ast_cli_unregister(&cli_show_user);
	ast_cli_unregister(&cli_show_objects);
	ast_cli_unregister(&cli_show_channels);
	ast_cli_unregister(&cli_show_channel);
	ast_cli_unregister(&cli_show_history);
	ast_cli_unregister(&cli_prune_realtime);
	ast_cli_unregister(&cli_show_peer);
	ast_cli_unregister(&cli_show_peers);
	ast_cli_unregister(&cli_show_registry);
	ast_cli_unregister(&cli_show_subscriptions);
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_debug_ip);
	ast_cli_unregister(&cli_debug_peer);
	ast_cli_unregister(&cli_no_debug);
	ast_cli_unregister(&cli_history);
	ast_cli_unregister(&cli_no_history);
	ast_cli_unregister(&cli_sip_reload);
	ast_cli_unregister(&cli_inuse_show);
	ast_rtp_proto_unregister(&sip_rtp);
	ast_channel_unregister(channeltype);
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
			ast_mutex_destroy(&pl->lock);
			if(pl->chanvars) {
				ast_variables_destroy(pl->chanvars);
				pl->chanvars = NULL;
			}
			free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}
	/* Free memory for local network address mask */
	ast_free_ha(localaddr);
	ASTOBJ_CONTAINER_DESTROY(&userl);
	ASTOBJ_CONTAINER_DESTROY(&peerl);
	ASTOBJ_CONTAINER_DESTROY(&regl);
		
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


