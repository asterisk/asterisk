/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Inter-Asterisk eXchange Version 2
 *
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/frame.h> 
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/sched.h>
#include <asterisk/io.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/translate.h>
#include <asterisk/md5.h>
#include <asterisk/cdr.h>
#include <asterisk/crypto.h>
#include <asterisk/acl.h>
#include <asterisk/manager.h>
#include <asterisk/callerid.h>
#include <asterisk/app.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#ifdef IAX_TRUNKING
#include <sys/ioctl.h>
#include <linux/zaptel.h>
#endif

#include "iax2.h"

/*
 * Uncomment to try experimental IAX bridge optimization,
 * designed to reduce latency when IAX calls cannot
 * be trasnferred
 */

#define BRIDGE_OPTIMIZATION 


#define DEFAULT_RETRY_TIME 1000
#define MEMORY_SIZE 100
#define DEFAULT_DROP 3
/* Flag to use with trunk calls, keeping these calls high up.  It halves our effective use
   but keeps the division between trunked and non-trunked better. */
#define TRUNK_CALL_START	0x4000

#define DEBUG_SUPPORT

#define MIN_REUSE_TIME		60	/* Don't reuse a call number within 60 seconds */

/* Sample over last 100 units to determine historic jitter */
#define GAMMA (0.01)

static char *desc = "Inter Asterisk eXchange (Ver 2)";
static char *tdesc = "Inter Asterisk eXchange Driver (Ver 2)";
static char *type = "IAX2";

static char context[80] = "default";

static int max_retries = 4;
static int ping_time = 20;
static int lagrq_time = 10;
static int maxtrunkcall = TRUNK_CALL_START;
static int maxnontrunkcall = 1;
static int maxjitterbuffer=3000;
static int trunkfreq = 20;

static int iaxdefaultdpcache=10 * 60;	/* Cache dialplan entries for 10 minutes by default */

static int iaxdefaulttimeout = 5;		/* Default to wait no more than 5 seconds for a reply to come back */

static int netsocket = -1;

static int tos = 0;

static int expirey = AST_DEFAULT_REG_EXPIRE;

static int timingfd = -1;				/* Timing file descriptor */

static int usecnt;
static pthread_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;

int (*regfunk)(char *username, int onoff) = NULL;

/* Ethernet, etc */
#define IAX_CAPABILITY_FULLBANDWIDTH 	0xFFFF
/* T1, maybe ISDN */
#define IAX_CAPABILITY_MEDBANDWIDTH 	(IAX_CAPABILITY_FULLBANDWIDTH & \
									~AST_FORMAT_SLINEAR & \
									~AST_FORMAT_ULAW & \
									~AST_FORMAT_ALAW) 
/* A modem */
#define IAX_CAPABILITY_LOWBANDWIDTH		(IAX_CAPABILITY_MEDBANDWIDTH & \
									~AST_FORMAT_MP3 & \
									~AST_FORMAT_ADPCM)

#define IAX_CAPABILITY_LOWFREE		(IAX_CAPABILITY_LOWBANDWIDTH & \
									 ~AST_FORMAT_G723_1)


#define DEFAULT_MAXMS		2000		/* Must be faster than 2 seconds by default */
#define DEFAULT_FREQ_OK		60 * 1000		/* How often to check for the host to be up */
#define DEFAULT_FREQ_NOTOK	10 * 1000		/* How often to check, if the host is down... */

static	struct io_context *io;
static	struct sched_context *sched;

static int iax2_capability = IAX_CAPABILITY_FULLBANDWIDTH;

static int iax2_dropcount = DEFAULT_DROP;

static int use_jitterbuffer = 1;

static int iaxdebug = 0;

static char accountcode[20];
static int amaflags = 0;

static pthread_t netthreadid;

#define IAX_STATE_STARTED		(1 << 0)
#define IAX_STATE_AUTHENTICATED (1 << 1)
#define IAX_STATE_TBD			(1 << 2)

struct iax2_context {
	char context[AST_MAX_EXTENSION];
	struct iax2_context *next;
};

struct iax2_user {
	char name[80];
	char secret[80];
	int authmethods;
	char accountcode[20];
	char inkeys[80];				/* Key(s) this user can use to authenticate to us */
	int amaflags;
	int hascallerid;
	int trunk;
	char callerid[AST_MAX_EXTENSION];
	struct ast_ha *ha;
	struct iax2_context *contexts;
	struct iax2_user *next;
};

struct iax2_peer {
	char name[80];
	char username[80];		
	char secret[80];
	char outkey[80];		/* What key we use to talk to this peer */
	char context[AST_MAX_EXTENSION];	/* Default context (for transfer really) */
	char mailbox[AST_MAX_EXTENSION];	/* Mailbox */
	struct sockaddr_in addr;
	int formats;
	struct in_addr mask;

	/* Dynamic Registration fields */
	int dynamic;					/* If this is a dynamic peer */
	struct sockaddr_in defaddr;		/* Default address if there is one */
	char challenge[80];				/* Challenge used to authenticate the secret */
	int authmethods;				/* Authentication methods (IAX_AUTH_*) */
	char inkeys[80];				/* Key(s) this peer can use to authenticate to us */

	int hascallerid;
	/* Suggested caller id if registering */
	char callerid[AST_MAX_EXTENSION];
	/* Whether or not to send ANI */
	int sendani;
	int expire;						/* Schedule entry for expirey */
	int expirey;					/* How soon to expire */
	int capability;					/* Capability */
	int delme;						/* I need to be deleted */
	int trunk;						/* Treat as an IAX trunking */
	struct timeval txtrunktime;		/* Transmit trunktime */
	struct timeval rxtrunktime;		/* Receive trunktime */
	struct timeval lasttxtime;		/* Last transmitted trunktime */
	unsigned int lastsent;			/* Last sent time */

	/* Qualification */
	int callno;					/* Call number of POKE request */
	int pokeexpire;					/* When to expire poke */
	int lastms;					/* How long last response took (in ms), or -1 for no response */
	int maxms;					/* Max ms we will accept for the host to be up, 0 to not monitor */
	
	struct ast_ha *ha;
	struct iax2_peer *next;
};

#define REG_STATE_UNREGISTERED 0
#define REG_STATE_REGSENT	   1
#define REG_STATE_AUTHSENT 	   2
#define REG_STATE_REGISTERED   3
#define REG_STATE_REJECTED	   4
#define REG_STATE_TIMEOUT	   5
#define REG_STATE_NOAUTH	   6

#define TRANSFER_NONE			0
#define TRANSFER_BEGIN			1
#define TRANSFER_READY			2
#define TRANSFER_RELEASED		3
#define TRANSFER_PASSTHROUGH	4

struct iax2_registry {
	struct sockaddr_in addr;		/* Who we connect to for registration purposes */
	char username[80];
	char secret[80];			/* Password or key name in []'s */
	char random[80];
	int expire;						/* Sched ID of expiration */
	int refresh;					/* How often to refresh */
	int regstate;
	int messages;					/* Message count */
	int callno;						/* Associated call number if applicable */
	struct sockaddr_in us;			/* Who the server thinks we are */
	struct iax2_registry *next;
};

struct iax2_registry *registrations;

/* Don't retry more frequently than every 10 ms, or less frequently than every 5 seconds */
#define MIN_RETRY_TIME	10
#define MAX_RETRY_TIME  10000
#define MAX_JITTER_BUFFER 50

#define MAX_TRUNKDATA	640		/* 40ms, uncompressed linear */

/* If we have more than this much excess real jitter buffer, srhink it. */
static int max_jitter_buffer = MAX_JITTER_BUFFER;

struct chan_iax2_pvt {
	/* Pipes for communication.  pipe[1] belongs to the
	   network thread (write), and pipe[0] belongs to the individual 
	   channel (read) */
	/* Whether or not we Quelch audio */
	int quelch;
	/* Last received voice format */
	int voiceformat;
	/* Last sent voice format */
	int svoiceformat;
	/* What we are capable of sending */
	int capability;
	/* Last received timestamp */
	unsigned int last;
	/* Last sent timestamp - never send the same timestamp twice in a single call */
	unsigned int lastsent;
	/* Ping time */
	unsigned int pingtime;
	/* Max time for initial response */
	int maxtime;
	/* Peer Address */
	struct sockaddr_in addr;
	/* Our call number */
	unsigned short callno;
	/* Peer callno */
	unsigned short peercallno;
	/* Peer selected format */
	int peerformat;
	/* Peer capability */
	int peercapability;
	/* timeval that we base our transmission on */
	struct timeval offset;
	/* timeval that we base our delivery on */
	struct timeval rxcore;
	/* Historical delivery time */
	int history[MEMORY_SIZE];
	/* Current base jitterbuffer */
	int jitterbuffer;
	/* Current jitter measure */
	int jitter;
	/* Historic jitter value */
	int historicjitter;
	/* LAG */
	int lag;
	/* Error, as discovered by the manager */
	int error;
	/* Owner if we have one */
	struct ast_channel *owner;
	/* What's our state? */
	int state;
	/* Expirey (optional) */
	int expirey;
	/* Next outgoing sequence number */
	unsigned char oseqno;
	/* Next sequence number they have not yet acknowledged */
	unsigned char rseqno;
	/* Next incoming sequence number */
	unsigned char iseqno;
	/* Last incoming sequence number we have acknowledged */
	unsigned char aseqno;
	/* Peer name */
	char peer[80];
	/* Default Context */
	char context[80];
	/* Caller ID if available */
	char callerid[80];
	/* Hidden Caller ID (i.e. ANI) if appropriate */
	char ani[80];
	/* Whether or not ani should be transmitted in addition to Caller*ID */
	int sendani;
	/* DNID */
	char dnid[80];
	/* Requested Extension */
	char exten[AST_MAX_EXTENSION];
	/* Expected Username */
	char username[80];
	/* Expected Secret */
	char secret[80];
	/* permitted authentication methods */
	int authmethods;
	/* MD5 challenge */
	char challenge[10];
	/* Public keys permitted keys for incoming authentication */
	char inkeys[80];
	/* Private key for outgoing authentication */
	char outkey[80];
	/* Preferred language */
	char language[80];
	/* Associated registry */
	struct iax2_registry *reg;
	/* Associated peer for poking */
	struct iax2_peer *peerpoke;

	/* Transferring status */
	int transferring;
	/* Already disconnected */
	int alreadygone;
	/* Who we are IAX transfering to */
	struct sockaddr_in transfer;
	/* What's the new call number for the transfer */
	unsigned short transfercallno;

	/* Status of knowledge of peer ADSI capability */
	int peeradsicpe;
	
	/* Who we are bridged to */
	unsigned short bridgecallno;
	int pingid;			/* Transmit PING request */
	int lagid;			/* Retransmit lag request */
	int autoid;			/* Auto hangup for Dialplan requestor */
	int initid;			/* Initial peer auto-congest ID (based on qualified peers) */
	char dproot[AST_MAX_EXTENSION];
	char accountcode[20];
	int amaflags;
	/* This is part of a trunk interface */
	int trunk;
	/* Trunk data and length */
	unsigned char trunkdata[MAX_TRUNKDATA];
	unsigned int trunkdatalen;
	int trunkerror;
	struct iax2_dpcache *dpentries;
};

#define DIRECTION_INGRESS 1
#define DIRECTION_OUTGRESS 2

struct ast_iax2_frame {
	/* /Our/ call number */
	unsigned short callno;
	/* /Their/ call number */
	unsigned short dcallno;
	/* Start of raw frame (outgoing only) */
	void *data;
	/* Length of frame (outgoing only) */
	int datalen;
	/* How many retries so far? */
	int retries;
	/* Outgoing relative timestamp (ms) */
	unsigned int ts;
	/* How long to wait before retrying */
	int retrytime;
	/* Are we received out of order?  */
	int outoforder;
	/* Have we been sent at all yet? */
	int sentyet;
	/* Outgoing Packet sequence number */
	int oseqno;
	/* Next expected incoming packet sequence number */
	int iseqno;
	/* Non-zero if should be sent to transfer peer */
	int transfer;
	/* Non-zero if this is the final message */
	int final;
	/* Ingress or outgres */
	int direction;
	/* Retransmission ID */
	int retrans;
	/* Easy linking */
	struct ast_iax2_frame *next;
	struct ast_iax2_frame *prev;
	/* Actual, isolated frame header */
	struct ast_frame af;
	unsigned char unused[AST_FRIENDLY_OFFSET];
	unsigned char afdata[0];	/* Data for frame */
};

struct iax_ies {
	char *called_number;
	char *calling_number;
	char *calling_ani;
	char *calling_name;
	char *called_context;
	char *username;
	char *password;
	unsigned int capability;
	unsigned int format;
	char *language;
	int version;
	unsigned short adsicpe;
	char *dnid;
	unsigned int authmethods;
	char *challenge;
	char *md5_result;
	char *rsa_result;
	struct sockaddr_in *apparent_addr;
	unsigned short refresh;
	unsigned short dpstatus;
	unsigned short callno;
	char *cause;
	unsigned char iax_unknown;
	int msgcount;
};

struct iax_ie_data {
	unsigned char buf[1024];
	int pos;
};

static struct ast_iax2_queue {
	struct ast_iax2_frame *head;
	struct ast_iax2_frame *tail;
	int count;
	pthread_mutex_t lock;
} iaxq;

static struct ast_user_list {
	struct iax2_user *users;
	pthread_mutex_t lock;
} userl;

static struct ast_peer_list {
	struct iax2_peer *peers;
	pthread_mutex_t lock;
} peerl;

/* Extension exists */
#define CACHE_FLAG_EXISTS		(1 << 0)
/* Extension is non-existant */
#define CACHE_FLAG_NONEXISTANT	(1 << 1)
/* Extension can exist */
#define CACHE_FLAG_CANEXIST		(1 << 2)
/* Waiting to hear back response */
#define CACHE_FLAG_PENDING		(1 << 3)
/* Timed out */
#define CACHE_FLAG_TIMEOUT		(1 << 4)
/* Request transmitted */
#define CACHE_FLAG_TRANSMITTED	(1 << 5)
/* Timeout */
#define CACHE_FLAG_UNKNOWN		(1 << 6)
/* Matchmore */
#define CACHE_FLAG_MATCHMORE	(1 << 7)

static struct iax2_dpcache {
	char peercontext[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];
	struct timeval orig;
	struct timeval expirey;
	int flags;
	unsigned short callno;
	int waiters[256];
	struct iax2_dpcache *next;
	struct iax2_dpcache *peer;	/* For linking in peers */
} *dpcache;

pthread_mutex_t dpcache_lock;

static void dump_addr(char *output, int maxlen, void *value, int len)
{
	struct sockaddr_in sin;
	if (len == sizeof(sin)) {
		memcpy(&sin, value, len);
		snprintf(output, maxlen, "IPV4 %s:%d", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	} else {
		snprintf(output, maxlen, "Invalid Address");
	}
}

static void dump_string(char *output, int maxlen, void *value, int len)
{
	maxlen--;
	if (maxlen > len)
		maxlen = len;
	strncpy(output,value, maxlen);
	output[maxlen] = '\0';
}

static void dump_int(char *output, int maxlen, void *value, int len)
{
	if (len == sizeof(unsigned int))
		snprintf(output, maxlen, "%d", ntohl(*((unsigned int *)value)));
	else
		snprintf(output, maxlen, "Invalid INT");
}

static void dump_short(char *output, int maxlen, void *value, int len)
{
	if (len == sizeof(unsigned short))
		snprintf(output, maxlen, "%d", ntohs(*((unsigned short *)value)));
	else
		snprintf(output, maxlen, "Invalid SHORT");
}

static void dump_byte(char *output, int maxlen, void *value, int len)
{
	if (len == sizeof(unsigned char))
		snprintf(output, maxlen, "%d", ntohs(*((unsigned char *)value)));
	else
		snprintf(output, maxlen, "Invalid BYTE");
}

static struct iax2_ie {
	int ie;
	char *name;
	void (*dump)(char *output, int maxlen, void *value, int len);
} ies[] = {
	{ IAX_IE_CALLED_NUMBER, "CALLED NUMBER", dump_string },
	{ IAX_IE_CALLING_NUMBER, "CALLING NUMBER", dump_string },
	{ IAX_IE_CALLING_NUMBER, "ANI", dump_string },
	{ IAX_IE_CALLING_NAME, "CALLING NAME", dump_string },
	{ IAX_IE_CALLED_CONTEXT, "CALLED CONTEXT", dump_string },
	{ IAX_IE_USERNAME, "USERNAME", dump_string },
	{ IAX_IE_PASSWORD, "PASSWORD", dump_string },
	{ IAX_IE_CAPABILITY, "CAPABILITY", dump_int },
	{ IAX_IE_FORMAT, "FORMAT", dump_int },
	{ IAX_IE_LANGUAGE, "LANGUAGE", dump_string },
	{ IAX_IE_VERSION, "VERSION", dump_short },
	{ IAX_IE_ADSICPE, "ADSICPE", dump_short },
	{ IAX_IE_DNID, "DNID", dump_string },
	{ IAX_IE_AUTHMETHODS, "AUTHMETHODS", dump_short },
	{ IAX_IE_CHALLENGE, "CHALLENGE", dump_string },
	{ IAX_IE_MD5_RESULT, "MD5 RESULT", dump_string },
	{ IAX_IE_RSA_RESULT, "RSA RESULT", dump_string },
	{ IAX_IE_APPARENT_ADDR, "APPARENT ADDRESS", dump_addr },
	{ IAX_IE_REFRESH, "REFRESH", dump_short },
	{ IAX_IE_DPSTATUS, "DIALPLAN STATUS", dump_short },
	{ IAX_IE_CALLNO, "CALL NUMBER", dump_short },
	{ IAX_IE_CAUSE, "CAUSE", dump_string },
	{ IAX_IE_IAX_UNKNOWN, "UNKNOWN IAX CMD", dump_byte },
	{ IAX_IE_MSGCOUNT, "MESSAGE COUNT", dump_short },
};

static char *ie2str(int ie)
{
	int x;
	for (x=0;x<sizeof(ies) / sizeof(ies[0]); x++) {
		if (ies[x].ie == ie)
			return ies[x].name;
	}
	return "Unknown IE";
}

static void dump_ies(unsigned char *iedata, int len)
{
	int ielen;
	int ie;
	int x;
	int found;
	char interp[80];
	if (len < 2)
		return;
	while(len > 2) {
		ie = iedata[0];
		ielen = iedata[1];
		if (ielen + 2> len) {
			ast_verbose("Total IE length of %d bytes exceeds remaining frame length of %d bytes\n", ielen + 2, len);
			return;
		}
		found = 0;
		for (x=0;x<sizeof(ies) / sizeof(ies[0]); x++) {
			if (ies[x].ie == ie) {
				if (ies[x].dump) {
					ies[x].dump(interp, sizeof(interp), iedata + 2, ielen);
					ast_verbose("   %-15.15s : %s\n", ies[x].name, interp);
				} else {
					ast_verbose("   %-15.15s : Present\n", ies[x].name);
				}
				found++;
			}
		}
		if (!found)
			ast_verbose("   Unknown IE %d : Present\n", ie);
		iedata += (2 + ielen);
		len -= (2 + ielen);
	}
	ast_verbose("\n");
}

#ifdef DEBUG_SUPPORT
void showframe(struct ast_iax2_frame *f, struct ast_iax2_full_hdr *fhi, int rx, struct sockaddr_in *sin, int datalen)
{
	char *frames[] = {
		"(0?)",
		"DTMF   ",
		"VOICE  ",
		"VIDEO  ",
		"CONTROL",
		"NULL   ",
		"IAX    ",
		"TEXT   ",
		"IMAGE  " };
	char *iaxs[] = {
		"(0?)",
		"NEW    ",
		"PING   ",
		"PONG   ",
		"ACK    ",
		"HANGUP ",
		"REJECT ",
		"ACCEPT ",
		"AUTHREQ",
		"AUTHREP",
		"INVAL  ",
		"LAGRQ  ",
		"LAGRP  ",
		"REGREQ ",
		"REGAUTH",
		"REGACK ",
		"REGREJ ",
		"REGREL ",
		"VNAK   ",
		"DPREQ  ",
		"DPREP  ",
		"DIAL   ",
		"TXREQ  ",
		"TXCNT  ",
		"TXACC  ",
		"TXREADY",
		"TXREL  ",
		"TXREJ  ",
		"QUELCH ",
		"UNQULCH",
		"POKE",
		"PAGE",
		"MWI",
		"UNSUPPORTED",
	};
	char *cmds[] = {
		"(0?)",
		"HANGUP ",
		"RING   ",
		"RINGING",
		"ANSWER ",
		"BUSY   ",
		"TKOFFHK ",
		"OFFHOOK" };
	struct ast_iax2_full_hdr *fh;
	char retries[20];
	char class2[20];
	char subclass2[20];
	char *class;
	char *subclass;
	if (f) {
		fh = f->data;
		snprintf(retries, sizeof(retries), "%03d", f->retries);
	} else {
		fh = fhi;
		if (ntohs(fh->dcallno) & AST_FLAG_RETRANS)
			strcpy(retries, "Yes");
		else
			strcpy(retries, "No");
	}
	if (!(ntohs(fh->scallno) & AST_FLAG_FULL)) {
		/* Don't mess with mini-frames */
		return;
	}
	if (fh->type > sizeof(frames)/sizeof(char *)) {
		snprintf(class2, sizeof(class2), "(%d?)", fh->type);
		class = class2;
	} else {
		class = frames[(int)fh->type];
	}
	if (fh->type == AST_FRAME_DTMF) {
		sprintf(subclass2, "%c", fh->csub);
		subclass = subclass2;
	} else if (fh->type == AST_FRAME_IAX) {
		if (fh->csub >= sizeof(iaxs)/sizeof(iaxs[0])) {
			snprintf(subclass2, sizeof(subclass2), "(%d?)", fh->csub);
			subclass = subclass2;
		} else {
			subclass = iaxs[(int)fh->csub];
		}
	} else if (fh->type == AST_FRAME_CONTROL) {
		if (fh->csub > sizeof(cmds)/sizeof(char *)) {
			snprintf(subclass2, sizeof(subclass2), "(%d?)", fh->csub);
			subclass = subclass2;
		} else {
			subclass = cmds[(int)fh->csub];
		}
	} else {
		snprintf(subclass2, sizeof(subclass2), "%d", fh->csub);
		subclass = subclass2;
	}
	ast_verbose(
"%s-Frame Retry[%s] -- OSeqno: %3.3d ISeqno: %3.3d Type: %s Subclass: %s\n",
	(rx ? "Rx" : "Tx"),
	retries, fh->oseqno, fh->iseqno, class, subclass);
		fprintf(stderr,
"   Timestamp: %05dms  SCall: %5.5d  DCall: %5.5d [%s:%d]\n",
	ntohl(fh->ts),
	ntohs(fh->scallno) & ~AST_FLAG_FULL, ntohs(fh->dcallno) & ~AST_FLAG_RETRANS,
		inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
	if (fh->type == AST_FRAME_IAX)
		dump_ies(fh->iedata, datalen);
}
#endif

/* XXX We probably should use a mutex when working with this XXX */
static struct chan_iax2_pvt *iaxs[AST_IAX2_MAX_CALLS];
static pthread_mutex_t iaxsl[AST_IAX2_MAX_CALLS];
static struct timeval lastused[AST_IAX2_MAX_CALLS];


static int send_command(struct chan_iax2_pvt *, char, int, unsigned int, char *, int, int);
static int send_command_immediate(struct chan_iax2_pvt *, char, int, unsigned int, char *, int, int);
static int send_command_final(struct chan_iax2_pvt *, char, int, unsigned int, char *, int, int);
static int send_command_transfer(struct chan_iax2_pvt *, char, int, unsigned int, char *, int);

static unsigned int calc_timestamp(struct chan_iax2_pvt *p, unsigned int ts);

static int send_ping(void *data)
{
	int callno = (long)data;
	/* Ping only if it's real, not if it's bridged */
	if (iaxs[callno]) {
#ifdef BRIDGE_OPTIMIZATION
		if (!iaxs[callno]->bridgecallno)
#endif
			send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_PING, 0, NULL, 0, -1);
		return 1;
	} else
		return 0;
}

static int send_lagrq(void *data)
{
	int callno = (long)data;
	/* Ping only if it's real not if it's bridged */
	if (iaxs[callno]) {
#ifdef BRIDGE_OPTIMIZATION
		if (!iaxs[callno]->bridgecallno)
#endif		
			send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_LAGRQ, 0, NULL, 0, -1);
		return 1;
	} else
		return 0;
}

static unsigned char compress_subclass(int subclass)
{
	int x;
	int power=-1;
	/* If it's 128 or smaller, just return it */
	if (subclass < AST_FLAG_SC_LOG)
		return subclass;
	/* Otherwise find its power */
	for (x = 0; x < AST_MAX_SHIFT; x++) {
		if (subclass & (1 << x)) {
			if (power > -1) {
				ast_log(LOG_WARNING, "Can't compress subclass %d\n", subclass);
				return 0;
			} else
				power = x;
		}
	}
	return power | AST_FLAG_SC_LOG;
}

static int uncompress_subclass(unsigned char csub)
{
	/* If the SC_LOG flag is set, return 2^csub otherwise csub */
	if (csub & AST_FLAG_SC_LOG) {
		/* special case for 'compressed' -1 */
		if (csub == 0xff)
			return -1;
		else
			return 1 << (csub & ~AST_FLAG_SC_LOG & AST_MAX_SHIFT);
	}
	else
		return csub;
}

static struct chan_iax2_pvt *new_iax(void)
{
	struct chan_iax2_pvt *tmp;
	tmp = malloc(sizeof(struct chan_iax2_pvt));
	if (tmp) {
		memset(tmp, 0, sizeof(struct chan_iax2_pvt));
		tmp->callno = 0;
		tmp->peercallno = 0;
		tmp->transfercallno = 0;
		tmp->bridgecallno = 0;
		tmp->pingid = -1;
		tmp->lagid = -1;
		tmp->autoid = -1;
		tmp->initid = -1;
		/* strncpy(tmp->context, context, sizeof(tmp->context)-1); */
		strncpy(tmp->exten, "s", sizeof(tmp->exten)-1);
	}
	return tmp;
}

static int get_samples(struct ast_frame *f)
{
	int samples=0;
	switch(f->subclass) {
	case AST_FORMAT_G723_1:
		samples = 240 /* XXX Not necessarily true XXX */;
		break;
	case AST_FORMAT_GSM:
		samples = 160 * (f->datalen / 33);
		break;
	case AST_FORMAT_SLINEAR:
		samples = f->datalen / 2;
		break;
	case AST_FORMAT_LPC10:
		samples = 22 * 8;
		samples += (((char *)(f->data))[7] & 0x1) * 8;
		break;
	case AST_FORMAT_ULAW:
		samples = f->datalen;
		break;
	case AST_FORMAT_ALAW:
		samples = f->datalen;
		break;
	case AST_FORMAT_ADPCM:
		samples = f->datalen *2;
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to calculate samples on %d packets\n", f->subclass);
	}
	return samples;
}

static int frames = 0;
static int iframes = 0;
static int oframes = 0;

static void ast_iax2_frame_wrap(struct ast_iax2_frame *fr, struct ast_frame *f)
{
	fr->af.frametype = f->frametype;
	fr->af.subclass = f->subclass;
	fr->af.mallocd = 0;				/* Our frame is static relative to the container */
	fr->af.datalen = f->datalen;
	fr->af.samples = f->samples;
	fr->af.offset = AST_FRIENDLY_OFFSET;
	fr->af.src = f->src;
	fr->af.data = fr->afdata;
	if (fr->af.datalen) 
		memcpy(fr->af.data, f->data, fr->af.datalen);
}

static struct ast_iax2_frame *ast_iax2_frame_new(int direction, int datalen)
{
	struct ast_iax2_frame *fr;
	fr = malloc(sizeof(struct ast_iax2_frame) + datalen);
	if (fr) {
		fr->direction = direction;
		fr->retrans = -1;
		frames++;
		if (fr->direction == DIRECTION_INGRESS)
			iframes++;
		else
			oframes++;
	}
	return fr;
}

static void ast_iax2_frame_free(struct ast_iax2_frame *fr)
{
	if (fr->retrans > -1)
		ast_sched_del(sched, fr->retrans);
	if (fr->direction == DIRECTION_INGRESS)
		iframes--;
	else if (fr->direction == DIRECTION_OUTGRESS)
		oframes--;
	else {
		ast_log(LOG_WARNING, "Attempt to double free frame detected\n");
		CRASH;
		return;
	}
	fr->direction = 0;
	free(fr);
	frames--;
}

static struct ast_iax2_frame *iaxfrdup2(struct ast_iax2_frame *fr)
{
	/* Malloc() a copy of a frame */
	struct ast_iax2_frame *new = ast_iax2_frame_new(DIRECTION_INGRESS, fr->af.datalen);
	if (new) {
		memcpy(new, fr, sizeof(struct ast_iax2_frame));	
		ast_iax2_frame_wrap(new, &fr->af);
		new->data = NULL;
		new->datalen = 0;
		new->direction = DIRECTION_INGRESS;
		new->retrans = -1;
	}
	return new;
}

#define NEW_PREVENT 0
#define NEW_ALLOW 	1
#define NEW_FORCE 	2

static int match(struct sockaddr_in *sin, unsigned short callno, unsigned short dcallno, struct chan_iax2_pvt *cur)
{
	if ((cur->addr.sin_addr.s_addr == sin->sin_addr.s_addr) &&
		(cur->addr.sin_port == sin->sin_port)) {
		/* This is the main host */
		if ((cur->peercallno == callno) ||
			((dcallno == cur->callno) && !cur->peercallno)) {
			/* That's us.  Be sure we keep track of the peer call number */
			return 1;
		}
	}
	if ((cur->transfer.sin_addr.s_addr == sin->sin_addr.s_addr) &&
	    (cur->transfer.sin_port == sin->sin_port) && (cur->transferring)) {
		/* We're transferring */
		if (dcallno == cur->callno)
			return 1;
	}
	return 0;
}

static void update_max_trunk(void)
{
	int max = TRUNK_CALL_START;
	int x;
	/* XXX Prolly don't need locks here XXX */
	for (x=TRUNK_CALL_START;x<AST_IAX2_MAX_CALLS - 1; x++) {
		if (iaxs[x])
			max = x + 1;
	}
	maxtrunkcall = max;
	if (option_debug)
		ast_log(LOG_DEBUG, "New max trunk callno is %d\n", max);
}

static void update_max_nontrunk(void)
{
	int max = 1;
	int x;
	/* XXX Prolly don't need locks here XXX */
	for (x=1;x<TRUNK_CALL_START - 1; x++) {
		if (iaxs[x])
			max = x + 1;
	}
	maxnontrunkcall = max;
	if (option_debug)
		ast_log(LOG_DEBUG, "New max nontrunk callno is %d\n", max);
}

static int make_trunk(unsigned short callno, int locked)
{
	int x;
	int res= 0;
	struct timeval now;
	if (iaxs[callno]->oseqno) {
		ast_log(LOG_WARNING, "Can't make trunk once a call has started!\n");
		return -1;
	}
	if (callno & TRUNK_CALL_START) {
		ast_log(LOG_WARNING, "Call %d is already a trunk\n", callno);
		return -1;
	}
	gettimeofday(&now, NULL);
	for (x=TRUNK_CALL_START;x<AST_IAX2_MAX_CALLS - 1; x++) {
		ast_pthread_mutex_lock(&iaxsl[x]);
		if (!iaxs[x] && ((now.tv_sec - lastused[x].tv_sec) > MIN_REUSE_TIME)) {
			iaxs[x] = iaxs[callno];
			iaxs[x]->callno = x;
			iaxs[callno] = NULL;
			/* Update the two timers that should have been started */
			if (iaxs[x]->pingid > -1)
				ast_sched_del(sched, iaxs[x]->pingid);
			if (iaxs[x]->lagid > -1)
				ast_sched_del(sched, iaxs[x]->lagid);
			iaxs[x]->pingid = ast_sched_add(sched, ping_time * 1000, send_ping, (void *)x);
			iaxs[x]->lagid = ast_sched_add(sched, lagrq_time * 1000, send_lagrq, (void *)x);
			if (locked)
				ast_pthread_mutex_unlock(&iaxsl[callno]);
			res = x;
			if (!locked)
				ast_pthread_mutex_unlock(&iaxsl[x]);
			break;
		}
		ast_pthread_mutex_unlock(&iaxsl[x]);
	}
	if (x >= AST_IAX2_MAX_CALLS - 1) {
		ast_log(LOG_WARNING, "Unable to trunk call: Insufficient space\n");
		return -1;
	}
	ast_log(LOG_DEBUG, "Made call %d into trunk call %d\n", callno, x);
	/* We move this call from a non-trunked to a trunked call */
	update_max_trunk();
	update_max_nontrunk();
	return res;
}

static int find_callno(unsigned short callno, unsigned short dcallno, struct sockaddr_in *sin, int new)
{
	int res = 0;
	int x;
	struct timeval now;
	if (new <= NEW_ALLOW) {
		/* Look for an existing connection first */
		for (x=1;(res < 1) && (x<maxnontrunkcall);x++) {
			ast_pthread_mutex_lock(&iaxsl[x]);
			if (iaxs[x]) {
				/* Look for an exact match */
				if (match(sin, callno, dcallno, iaxs[x])) {
					res = x;
				}
			}
			ast_pthread_mutex_unlock(&iaxsl[x]);
		}
		for (x=TRUNK_CALL_START;(res < 1) && (x<maxtrunkcall);x++) {
			ast_pthread_mutex_lock(&iaxsl[x]);
			if (iaxs[x]) {
				/* Look for an exact match */
				if (match(sin, callno, dcallno, iaxs[x])) {
					res = x;
				}
			}
			ast_pthread_mutex_unlock(&iaxsl[x]);
		}
	}
	if ((res < 1) && (new >= NEW_ALLOW)) {
		gettimeofday(&now, NULL);
		for (x=1;x<TRUNK_CALL_START;x++) {
			/* Find first unused call number that hasn't been used in a while */
			ast_pthread_mutex_lock(&iaxsl[x]);
			if (!iaxs[x] && ((now.tv_sec - lastused[x].tv_sec) > MIN_REUSE_TIME)) break;
			ast_pthread_mutex_unlock(&iaxsl[x]);
		}
		/* We've still got lock held if we found a spot */
		if (x >= TRUNK_CALL_START) {
			ast_log(LOG_WARNING, "No more space\n");
			return -1;
		}
		iaxs[x] = new_iax();
		ast_pthread_mutex_unlock(&iaxsl[x]);
		update_max_nontrunk();
		if (iaxs[x]) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Creating new call structure %d\n", x);
			iaxs[x]->addr.sin_port = sin->sin_port;
			iaxs[x]->addr.sin_family = sin->sin_family;
			iaxs[x]->addr.sin_addr.s_addr = sin->sin_addr.s_addr;
			iaxs[x]->peercallno = callno;
			iaxs[x]->callno = x;
			iaxs[x]->pingtime = DEFAULT_RETRY_TIME;
			iaxs[x]->expirey = expirey;
			iaxs[x]->pingid = ast_sched_add(sched, ping_time * 1000, send_ping, (void *)x);
			iaxs[x]->lagid = ast_sched_add(sched, lagrq_time * 1000, send_lagrq, (void *)x);
			iaxs[x]->amaflags = amaflags;
			strncpy(iaxs[x]->accountcode, accountcode, sizeof(iaxs[x]->accountcode)-1);
		} else {
			ast_log(LOG_WARNING, "Out of resources\n");
			return 0;
		}
		res = x;
	}
	return res;
}

static int iax2_queue_frame(int callno, struct ast_frame *f)
{
	int pass =0;
	/* Assumes lock for callno is already held... */
	for (;;) {
		pass++;
		if (!pthread_mutex_trylock(&iaxsl[callno])) {
			ast_log(LOG_WARNING, "Lock is not held on pass %d of iax2_queue_frame\n", pass);
			CRASH;
		}
		if (iaxs[callno] && iaxs[callno]->owner) {
			if (pthread_mutex_trylock(&iaxs[callno]->owner->lock)) {
				/* Avoid deadlock by pausing and trying again */
				ast_pthread_mutex_unlock(&iaxsl[callno]);
				usleep(1);
				ast_pthread_mutex_lock(&iaxsl[callno]);
			} else {
				ast_queue_frame(iaxs[callno]->owner, f, 0);
				ast_pthread_mutex_unlock(&iaxs[callno]->owner->lock);
				break;
			}
		} else
			break;
	}
	return 0;
}

static int iax2_send(struct chan_iax2_pvt *pvt, struct ast_frame *f, unsigned int ts, int seqno, int now, int transfer, int final);

static int __do_deliver(void *data)
{
	/* Just deliver the packet by using queueing.  This is called by
	  the IAX thread with the iaxsl lock held. */
	struct ast_iax2_frame *fr = data;
	unsigned int ts;
	fr->retrans = -1;
	if (iaxs[fr->callno] && !iaxs[fr->callno]->alreadygone) {
		if (fr->af.frametype == AST_FRAME_IAX) {
			/* We have to treat some of these packets specially because
			   they're LAG measurement packets */
			if (fr->af.subclass == AST_IAX2_COMMAND_LAGRQ) {
				/* If we got a queued request, build a reply and send it */
				fr->af.subclass = AST_IAX2_COMMAND_LAGRP;
				iax2_send(iaxs[fr->callno], &fr->af, fr->ts, -1, 0, 0, 0);
			} else if (fr->af.subclass == AST_IAX2_COMMAND_LAGRP) {
				/* This is a reply we've been given, actually measure the difference */
				ts = calc_timestamp(iaxs[fr->callno], 0);
				iaxs[fr->callno]->lag = ts - fr->ts;
			}
		} else {
			iax2_queue_frame(fr->callno, &fr->af);
		}
	}
	/* Free our iax frame */
	ast_iax2_frame_free(fr);
	/* And don't run again */
	return 0;
}

static int do_deliver(void *data)
{
	/* Locking version of __do_deliver */
	struct ast_iax2_frame *fr = data;
	int callno = fr->callno;
	int res;
	ast_pthread_mutex_lock(&iaxsl[callno]);
	res = __do_deliver(data);
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	return res;
}

static int handle_error(void)
{
	/* XXX Ideally we should figure out why an error occured and then abort those
	   rather than continuing to try.  Unfortunately, the published interface does
	   not seem to work XXX */
#if 0
	struct sockaddr_in *sin;
	int res;
	struct msghdr m;
	struct sock_extended_err e;
	m.msg_name = NULL;
	m.msg_namelen = 0;
	m.msg_iov = NULL;
	m.msg_control = &e;
	m.msg_controllen = sizeof(e);
	m.msg_flags = 0;
	res = recvmsg(netsocket, &m, MSG_ERRQUEUE);
	if (res < 0)
		ast_log(LOG_WARNING, "Error detected, but unable to read error: %s\n", strerror(errno));
	else {
		if (m.msg_controllen) {
			sin = (struct sockaddr_in *)SO_EE_OFFENDER(&e);
			if (sin) 
				ast_log(LOG_WARNING, "Receive error from %s\n", inet_ntoa(sin->sin_addr));
			else
				ast_log(LOG_WARNING, "No address detected??\n");
		} else {
			ast_log(LOG_WARNING, "Local error: %s\n", strerror(e.ee_errno));
		}
	}
#endif
	return 0;
}

static int send_packet(struct ast_iax2_frame *f)
{
	int res;
	/* Called with iaxsl held */
	if (option_debug)
		ast_log(LOG_DEBUG, "Sending %d on %d/%d to %s:%d\n", f->ts, f->callno, iaxs[f->callno]->peercallno, inet_ntoa(iaxs[f->callno]->addr.sin_addr), ntohs(iaxs[f->callno]->addr.sin_port));
	/* Don't send if there was an error, but return error instead */
	if (!f->callno) {
		ast_log(LOG_WARNING, "Call number = %d\n", f->callno);
		return -1;
	}
	if (!iaxs[f->callno])
		return -1;
	if (iaxs[f->callno]->error)
		return -1;
	if (f->transfer) {
#ifdef DEBUG_SUPPORT
		if (iaxdebug)
			showframe(f, NULL, 0, &iaxs[f->callno]->transfer, f->datalen - sizeof(struct ast_iax2_full_hdr));
#endif
		res = sendto(netsocket, f->data, f->datalen, 0,(struct sockaddr *)&iaxs[f->callno]->transfer,
					sizeof(iaxs[f->callno]->transfer));
	} else {
#ifdef DEBUG_SUPPORT
		if (iaxdebug)
			showframe(f, NULL, 0, &iaxs[f->callno]->addr, f->datalen - sizeof(struct ast_iax2_full_hdr));
#endif
		res = sendto(netsocket, f->data, f->datalen, 0,(struct sockaddr *)&iaxs[f->callno]->addr,
					sizeof(iaxs[f->callno]->addr));
	}
	if (res < 0) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Received error: %s\n", strerror(errno));
		handle_error();
	} else
		res = 0;
	return res;
}


static int iax2_predestroy(int callno)
{
	struct ast_channel *c;
	struct chan_iax2_pvt *pvt;
	ast_pthread_mutex_lock(&iaxsl[callno]);
	pvt = iaxs[callno];
	if (!pvt) {
		ast_pthread_mutex_unlock(&iaxsl[callno]);
		return -1;
	}
	if (!pvt->alreadygone) {
		/* No more pings or lagrq's */
		if (pvt->pingid > -1)
			ast_sched_del(sched, pvt->pingid);
		if (pvt->lagid > -1)
			ast_sched_del(sched, pvt->lagid);
		if (pvt->autoid > -1)
			ast_sched_del(sched, pvt->autoid);
		if (pvt->initid > -1)
			ast_sched_del(sched, pvt->initid);
		pvt->pingid = -1;
		pvt->lagid = -1;
		pvt->autoid = -1;
		pvt->initid = -1;
		pvt->alreadygone = 1;
	}
	c = pvt->owner;
	if (c) {
		c->_softhangup |= AST_SOFTHANGUP_DEV;
		c->pvt->pvt = NULL;
		ast_queue_hangup(c, 0);
		pvt->owner = NULL;
		ast_pthread_mutex_lock(&usecnt_lock);
		usecnt--;
		if (usecnt < 0) 
			ast_log(LOG_WARNING, "Usecnt < 0???\n");
		ast_pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
	}
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	return 0;
}

static int iax2_predestroy_nolock(int callno)
{
	int res;
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	res = iax2_predestroy(callno);
	ast_pthread_mutex_lock(&iaxsl[callno]);
	return res;
}

static void iax2_destroy(int callno)
{
	struct chan_iax2_pvt *pvt;
	struct ast_iax2_frame *cur;
	struct ast_channel *owner;

retry:
	ast_pthread_mutex_lock(&iaxsl[callno]);
	pvt = iaxs[callno];
	iaxs[callno] = NULL;
	gettimeofday(&lastused[callno], NULL);

	if (pvt)
		owner = pvt->owner;
	else
		owner = NULL;
	if (owner) {
		if (pthread_mutex_trylock(&owner->lock)) {
			ast_log(LOG_NOTICE, "Avoiding IAX destroy deadlock\n");
			ast_pthread_mutex_unlock(&iaxsl[callno]);
			usleep(1);
			goto retry;
		}
	}
	if (pvt) {
		pvt->owner = NULL;
		/* No more pings or lagrq's */
		if (pvt->pingid > -1)
			ast_sched_del(sched, pvt->pingid);
		if (pvt->lagid > -1)
			ast_sched_del(sched, pvt->lagid);
		if (pvt->autoid > -1)
			ast_sched_del(sched, pvt->autoid);
		if (pvt->initid > -1)
			ast_sched_del(sched, pvt->initid);
		pvt->pingid = -1;
		pvt->lagid = -1;
		pvt->autoid = -1;
		pvt->initid = -1;

		/* Already gone */
		pvt->alreadygone = 1;

		if (owner) {
			/* If there's an owner, prod it to give up */
			owner->pvt->pvt = NULL;
			owner->_softhangup |= AST_SOFTHANGUP_DEV;
			ast_queue_hangup(owner, 0);
		}

		for (cur = iaxq.head; cur ; cur = cur->next) {
			/* Cancel any pending transmissions */
			if (cur->callno == pvt->callno) 
				cur->retries = -1;
		}
		if (pvt->reg) {
			pvt->reg->callno = 0;
		}
		free(pvt);
	}
	if (owner) {
		ast_pthread_mutex_unlock(&owner->lock);
	}
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	if (callno & 0x4000)
		update_max_trunk();
}
static void iax2_destroy_nolock(int callno)
{	
	/* Actually it's easier to unlock, kill it, and relock */
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	iax2_destroy(callno);
	ast_pthread_mutex_lock(&iaxsl[callno]);
}

static int update_packet(struct ast_iax2_frame *f)
{
	/* Called with iaxsl lock held, and iaxs[callno] non-NULL */
	struct ast_iax2_full_hdr *fh = f->data;
	/* Mark this as a retransmission */
	fh->dcallno = ntohs(AST_FLAG_RETRANS | f->dcallno);
	/* Update iseqno */
	f->iseqno = iaxs[f->callno]->iseqno;
	fh->iseqno = f->iseqno;
	return 0;
}

static int attempt_transmit(void *data)
{
	/* Attempt to transmit the frame to the remote peer...
	   Called without iaxsl held. */
	struct ast_iax2_frame *f = data;
	int freeme=0;
	int callno = f->callno;
	/* Make sure this call is still active */
	if (callno) 
		ast_pthread_mutex_lock(&iaxsl[callno]);
	if ((f->callno) && iaxs[f->callno]) {
		if ((f->retries < 0) /* Already ACK'd */ ||
		    (f->retries >= max_retries) /* Too many attempts */) {
				/* Record an error if we've transmitted too many times */
				if (f->retries >= max_retries) {
					if (f->transfer) {
						/* Transfer timeout */
						send_command(iaxs[f->callno], AST_FRAME_IAX, AST_IAX2_COMMAND_TXREJ, 0, NULL, 0, -1);
					} else if (f->final) {
						if (f->final) 
							iax2_destroy_nolock(f->callno);
					} else {
						if (iaxs[f->callno]->owner)
							ast_log(LOG_WARNING, "Max retries exceeded to host %s on %s (type = %d, subclass = %d, ts=%d, seqno=%d)\n", inet_ntoa(iaxs[f->callno]->addr.sin_addr),iaxs[f->callno]->owner->name , f->af.frametype, f->af.subclass, f->ts, f->oseqno);
						iaxs[f->callno]->error = ETIMEDOUT;
						if (iaxs[f->callno]->owner) {
							struct ast_frame fr = { 0, };
							/* Hangup the fd */
							fr.frametype = AST_FRAME_CONTROL;
							fr.subclass = AST_CONTROL_HANGUP;
							iax2_queue_frame(f->callno, &fr);
						} else {
							if (iaxs[f->callno]->reg) {
								memset(&iaxs[f->callno]->reg->us, 0, sizeof(iaxs[f->callno]->reg->us));
								iaxs[f->callno]->reg->regstate = REG_STATE_TIMEOUT;
								iaxs[f->callno]->reg->refresh = AST_DEFAULT_REG_EXPIRE;
							}
							iax2_destroy_nolock(f->callno);
						}
					}

				}
				freeme++;
		} else {
			/* Update it if it needs it */
			update_packet(f);
			/* Attempt transmission */
			send_packet(f);
			f->retries++;
			/* Try again later after 10 times as long */
			f->retrytime *= 10;
			if (f->retrytime > MAX_RETRY_TIME)
				f->retrytime = MAX_RETRY_TIME;
			/* Transfer messages max out at one second */
			if (f->transfer && (f->retrytime > 1000))
				f->retrytime = 1000;
			f->retrans = ast_sched_add(sched, f->retrytime, attempt_transmit, f);
		}
	} else {
		/* Make sure it gets freed */
		f->retries = -1;
		freeme++;
	}
	if (callno)
		ast_pthread_mutex_unlock(&iaxsl[callno]);
	/* Do not try again */
	if (freeme) {
		/* Don't attempt delivery, just remove it from the queue */
		ast_pthread_mutex_lock(&iaxq.lock);
		if (f->prev) 
			f->prev->next = f->next;
		else
			iaxq.head = f->next;
		if (f->next)
			f->next->prev = f->prev;
		else
			iaxq.tail = f->prev;
		iaxq.count--;
		ast_pthread_mutex_unlock(&iaxq.lock);
		f->retrans = -1;
		/* Free the IAX2 frame */
		ast_iax2_frame_free(f);
	}
	return 0;
}

static int iax2_set_jitter(int fd, int argc, char *argv[])
{
	if ((argc != 4) && (argc != 5))
		return RESULT_SHOWUSAGE;
	if (argc == 4) {
		max_jitter_buffer = atoi(argv[3]);
		if (max_jitter_buffer < 0)
			max_jitter_buffer = 0;
	} else {
		if (argc == 5) {
			if ((atoi(argv[3]) >= 0) && (atoi(argv[3]) < AST_IAX2_MAX_CALLS)) {
				if (iaxs[atoi(argv[3])]) {
					iaxs[atoi(argv[3])]->jitterbuffer = atoi(argv[4]);
					if (iaxs[atoi(argv[3])]->jitterbuffer < 0)
						iaxs[atoi(argv[3])]->jitterbuffer = 0;
				} else
					ast_cli(fd, "No such call '%d'\n", atoi(argv[3]));
			} else
				ast_cli(fd, "%d is not a valid call number\n", atoi(argv[3]));
		}
	}
	return RESULT_SUCCESS;
}

static char jitter_usage[] = 
"Usage: iax set jitter [callid] <value>\n"
"       If used with a callid, it sets the jitter buffer to the given static\n"
"value (until its next calculation).  If used without a callid, the value is used\n"
"to establish the maximum excess jitter buffer that is permitted before the jitter\n"
"buffer size is reduced.";

static int iax2_show_stats(int fd, int argc, char *argv[])
{
	struct ast_iax2_frame *cur;
	int cnt = 0, dead=0, final=0;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	for (cur = iaxq.head; cur ; cur = cur->next) {
		if (cur->retries < 0)
			dead++;
		if (cur->final)
			final++;
		cnt++;
	}
	ast_cli(fd, "    IAX Statistics\n");
	ast_cli(fd, "---------------------\n");
	ast_cli(fd, "Outstanding frames: %d (%d ingress, %d outgress)\n", frames, iframes, oframes);
	ast_cli(fd, "Packets in transmit queue: %d dead, %d final, %d total\n", dead, final, cnt);
	return RESULT_SUCCESS;
}

static int iax2_show_cache(int fd, int argc, char *argv[])
{
	struct iax2_dpcache *dp;
	char tmp[1024], *pc;
	int s;
	int x,y;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ast_pthread_mutex_lock(&dpcache_lock);
	dp = dpcache;
	ast_cli(fd, "%-20.20s %-12.12s %-9.9s %-8.8s %s\n", "Peer/Context", "Exten", "Exp.", "Wait.", "Flags");
	while(dp) {
		s = dp->expirey.tv_sec - tv.tv_sec;
		strcpy(tmp, "");
		if (dp->flags & CACHE_FLAG_EXISTS)
			strcat(tmp, "EXISTS|");
		if (dp->flags & CACHE_FLAG_NONEXISTANT)
			strcat(tmp, "NONEXISTANT|");
		if (dp->flags & CACHE_FLAG_CANEXIST)
			strcat(tmp, "CANEXIST|");
		if (dp->flags & CACHE_FLAG_PENDING)
			strcat(tmp, "PENDING|");
		if (dp->flags & CACHE_FLAG_TIMEOUT)
			strcat(tmp, "TIMEOUT|");
		if (dp->flags & CACHE_FLAG_TRANSMITTED)
			strcat(tmp, "TRANSMITTED|");
		if (dp->flags & CACHE_FLAG_MATCHMORE)
			strcat(tmp, "MATCHMORE|");
		if (dp->flags & CACHE_FLAG_UNKNOWN)
			strcat(tmp, "UNKNOWN|");
		/* Trim trailing pipe */
		if (strlen(tmp))
			tmp[strlen(tmp) - 1] = '\0';
		else
			strcpy(tmp, "(none)");
		y=0;
		pc = strchr(dp->peercontext, '@');
		if (!pc)
			pc = dp->peercontext;
		else
			pc++;
		for (x=0;x<sizeof(dp->waiters) / sizeof(dp->waiters[0]); x++)
			if (dp->waiters[x] > -1)
				y++;
		if (s > 0)
			ast_cli(fd, "%-20.20s %-12.12s %-9d %-8d %s\n", pc, dp->exten, s, y, tmp);
		else
			ast_cli(fd, "%-20.20s %-12.12s %-9.9s %-8d %s\n", pc, dp->exten, "(expired)", y, tmp);
		dp = dp->next;
	}
	ast_pthread_mutex_unlock(&dpcache_lock);
	return RESULT_SUCCESS;
}

static char show_stats_usage[] =
"Usage: iax show stats\n"
"       Display statistics on IAX channel driver.\n";


static char show_cache_usage[] =
"Usage: iax show cache\n"
"       Display currently cached IAX Dialplan results.\n";

static struct ast_cli_entry cli_set_jitter = 
{ { "iax2", "set", "jitter", NULL }, iax2_set_jitter, "Sets IAX jitter buffer", jitter_usage };

static struct ast_cli_entry cli_show_stats =
{ { "iax2", "show", "stats", NULL }, iax2_show_stats, "Display IAX statistics", show_stats_usage };

static struct ast_cli_entry cli_show_cache =
{ { "iax2", "show", "cache", NULL }, iax2_show_cache, "Display IAX cached dialplan", show_cache_usage };

static unsigned int calc_rxstamp(struct chan_iax2_pvt *p);

#ifdef BRIDGE_OPTIMIZATION
static unsigned int calc_fakestamp(struct chan_iax2_pvt *from, struct chan_iax2_pvt *to, unsigned int ts);

static int forward_delivery(struct ast_iax2_frame *fr)
{
	struct chan_iax2_pvt *p1, *p2;
	p1 = iaxs[fr->callno];
	p2 = iaxs[p1->bridgecallno];
	if (!p1)
		return -1;
	if (!p2)
		return -1;
	/* Fix relative timestamp */
	fr->ts = calc_fakestamp(p1, p2, fr->ts);
	/* Now just send it send on the 2nd one 
	   with adjusted timestamp */
	return iax2_send(p2, &fr->af, fr->ts, -1, 0, 0, 0);
}
#endif

static int schedule_delivery(struct ast_iax2_frame *fr, int reallydeliver, int updatehistory)
{
	int ms,x;
	int drops[MEMORY_SIZE];
	int min, max=0, maxone=0,y,z, match;
	/* ms is a measure of the "lateness" of the packet relative to the first
	   packet we received, which always has a lateness of 1.  Called by
	   IAX thread, with iaxsl lock held. */
	ms = calc_rxstamp(iaxs[fr->callno]) - fr->ts;

	if (ms > 32767) {
		/* What likely happened here is that our counter has circled but we haven't
		   gotten the update from the main packet.  We'll just pretend that we did, and
		   update the timestamp appropriately. */
		ms -= 65536;
	}

	if (ms < -32768) {
		/* We got this packet out of order.  Lets add 65536 to it to bring it into our new
		   time frame */
		ms += 65536;
	}
	
	/* Rotate our history queue of "lateness".  Don't worry about those initial
	   zeros because the first entry will always be zero */
	if (updatehistory) {
		for (x=0;x<MEMORY_SIZE - 1;x++) 
			iaxs[fr->callno]->history[x] = iaxs[fr->callno]->history[x+1];
		/* Add a history entry for this one */
		iaxs[fr->callno]->history[x] = ms;
	}

	/* Initialize the minimum to reasonable values.  It's too much
	   work to do the same for the maximum, repeatedly */
	min=iaxs[fr->callno]->history[0];
	for (z=0;z < iax2_dropcount + 1;z++) {
		/* Start very optimistic ;-) */
		max=-999999999;
		for (x=0;x<MEMORY_SIZE;x++) {
			if (max < iaxs[fr->callno]->history[x]) {
				/* We have a candidate new maximum value.  Make
				   sure it's not in our drop list */
				match = 0;
				for (y=0;!match && (y<z);y++)
					match |= (drops[y] == x);
				if (!match) {
					/* It's not in our list, use it as the new maximum */
					max = iaxs[fr->callno]->history[x];
					maxone = x;
				}
				
			}
			if (!z) {
				/* On our first pass, find the minimum too */
				if (min > iaxs[fr->callno]->history[x])
					min = iaxs[fr->callno]->history[x];
			}
		}
#if 1
		drops[z] = maxone;
#endif
	}
	/* Just for reference, keep the "jitter" value, the difference between the
	   earliest and the latest. */
	iaxs[fr->callno]->jitter = max - min;	
	
	/* IIR filter for keeping track of historic jitter, but always increase
	   historic jitter immediately for increase */
	
	if (iaxs[fr->callno]->jitter > iaxs[fr->callno]->historicjitter )
		iaxs[fr->callno]->historicjitter = iaxs[fr->callno]->jitter;
	else
		iaxs[fr->callno]->historicjitter = GAMMA * (double)iaxs[fr->callno]->jitter + (1-GAMMA) * 
			iaxs[fr->callno]->historicjitter;

	/* If our jitter buffer is too big (by a significant margin), then we slowly
	   shrink it by about 1 ms each time to avoid letting the change be perceived */
	if (max < iaxs[fr->callno]->jitterbuffer - max_jitter_buffer)
		iaxs[fr->callno]->jitterbuffer -= 2;


#if 1
	/* Constrain our maximum jitter buffer appropriately */
	if (max > min + maxjitterbuffer) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Constraining buffer from %d to %d + %d\n", max, min , maxjitterbuffer);
		max = min + maxjitterbuffer;
	}
#endif

	/* If our jitter buffer is smaller than our maximum delay, grow the jitter
	   buffer immediately to accomodate it (and a little more).  */
	if (max > iaxs[fr->callno]->jitterbuffer)
		iaxs[fr->callno]->jitterbuffer = max 
			/* + ((float)iaxs[fr->callno]->jitter) * 0.1 */;
		

	if (option_debug)
		ast_log(LOG_DEBUG, "min = %d, max = %d, jb = %d, lateness = %d\n", min, max, iaxs[fr->callno]->jitterbuffer, ms);
	
	/* Subtract the lateness from our jitter buffer to know how long to wait
	   before sending our packet.  */
	ms = iaxs[fr->callno]->jitterbuffer - ms;
	
	if (!use_jitterbuffer)
		ms = 0;

	/* If the caller just wanted us to update, return now */
	if (!reallydeliver)
		return 0;
		
	if (ms < 1) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Calculated ms is %d\n", ms);
		/* Don't deliver it more than 4 ms late */
		if ((ms > -4) || (fr->af.frametype != AST_FRAME_VOICE)) {
			__do_deliver(fr);
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "Dropping voice packet since %d ms is, too old\n", ms);
			/* Free our iax frame */
			ast_iax2_frame_free(fr);
		}
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Scheduling delivery in %d ms\n", ms);
		fr->retrans = ast_sched_add(sched, ms, do_deliver, fr);
	}
	return 0;
}

static int iax2_transmit(struct ast_iax2_frame *fr)
{
	/* Lock the queue and place this packet at the end */
	fr->next = NULL;
	fr->prev = NULL;
	/* By setting this to 0, the network thread will send it for us, and
	   queue retransmission if necessary */
	fr->sentyet = 0;
	ast_pthread_mutex_lock(&iaxq.lock);
	if (!iaxq.head) {
		/* Empty queue */
		iaxq.head = fr;
		iaxq.tail = fr;
	} else {
		/* Double link */
		iaxq.tail->next = fr;
		fr->prev = iaxq.tail;
		iaxq.tail = fr;
	}
	iaxq.count++;
	ast_pthread_mutex_unlock(&iaxq.lock);
	/* Wake up the network thread */
	pthread_kill(netthreadid, SIGURG);
	return 0;
}



static int iax2_digit(struct ast_channel *c, char digit)
{
	return send_command(c->pvt->pvt, AST_FRAME_DTMF, digit, 0, NULL, 0, -1);
}

static int iax2_sendtext(struct ast_channel *c, char *text)
{
	
	return send_command(c->pvt->pvt, AST_FRAME_TEXT,
		0, 0, text, strlen(text) + 1, -1);
}

static int iax2_sendimage(struct ast_channel *c, struct ast_frame *img)
{
	return send_command(c->pvt->pvt, AST_FRAME_IMAGE, img->subclass, 0, img->data, img->datalen, -1);
}

static int iax2_sendhtml(struct ast_channel *c, int subclass, char *data, int datalen)
{
	return send_command(c->pvt->pvt, AST_FRAME_HTML, subclass, 0, data, datalen, -1);
}

static int iax2_fixup(struct ast_channel *oldchannel, struct ast_channel *newchan)
{
	struct chan_iax2_pvt *pvt = newchan->pvt->pvt;
	pvt->owner = newchan;
	return 0;
}

static int create_addr(struct sockaddr_in *sin, int *capability, int *sendani, int *maxtime, char *peer, char *context, int *trunk)
{
	struct hostent *hp;
	struct iax2_peer *p;
	int found=0;
	if (sendani)
		*sendani = 0;
	if (maxtime)
		*maxtime = 0;
	if (trunk)
		*trunk = 0;
	sin->sin_family = AF_INET;
	ast_pthread_mutex_lock(&peerl.lock);
	p = peerl.peers;
	while(p) {
		if (!strcasecmp(p->name, peer)) {
			found++;
			if ((p->addr.sin_addr.s_addr || p->defaddr.sin_addr.s_addr) &&
				(!p->maxms || ((p->lastms > 0)  && (p->lastms <= p->maxms)))) {
				if (sendani)
					*sendani = p->sendani;		/* Whether we transmit ANI */
				if (maxtime)
					*maxtime = p->maxms;		/* Max time they should take */
				if (context)
					strncpy(context, p->context, AST_MAX_EXTENSION - 1);
				if (trunk)
					*trunk = p->trunk;
				if (capability)
					*capability = p->capability;
				if (p->addr.sin_addr.s_addr) {
					sin->sin_addr = p->addr.sin_addr;
					sin->sin_port = p->addr.sin_port;
				} else {
					sin->sin_addr = p->defaddr.sin_addr;
					sin->sin_port = p->defaddr.sin_port;
				}
				break;
			}
		}
		p = p->next;
	}
	ast_pthread_mutex_unlock(&peerl.lock);
	if (!p && !found) {
		hp = gethostbyname(peer);
		if (hp) {
			memcpy(&sin->sin_addr, hp->h_addr, sizeof(sin->sin_addr));
			sin->sin_port = htons(AST_DEFAULT_IAX_PORTNO);
			return 0;
		} else {
			ast_log(LOG_WARNING, "No such host: %s\n", peer);
			return -1;
		}
	} else if (!p)
		return -1;
	else
		return 0;
}

static int auto_congest(void *nothing)
{
	int callno = (int)(long)(nothing);
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_CONGESTION };
	ast_pthread_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		iaxs[callno]->initid = -1;
		iax2_queue_frame(callno, &f);
		ast_log(LOG_NOTICE, "Auto-congesting call due to slow response\n");
	}
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	return 0;
}

static int iax_ie_append_raw(struct iax_ie_data *ied, unsigned char ie, void *data, int datalen)
{
	if (datalen > (sizeof(ied->buf) - ied->pos)) {
		ast_log(LOG_WARNING, "Out of space for ie '%s' (%d), need %d have %d\n", ie2str(ie), ie, datalen, sizeof(ied->buf) - ied->pos);
		return -1;
	}
	ied->buf[ied->pos++] = ie;
	ied->buf[ied->pos++] = datalen;
	memcpy(ied->buf + ied->pos, data, datalen);
	ied->pos += datalen;
	return 0;
}

static int iax_ie_append_addr(struct iax_ie_data *ied, unsigned char ie, struct sockaddr_in *sin)
{
	return iax_ie_append_raw(ied, ie, sin, sizeof(struct sockaddr_in));
}

static int iax_ie_append_int(struct iax_ie_data *ied, unsigned char ie, unsigned int value) 
{
	unsigned int newval;
	newval = htonl(value);
	return iax_ie_append_raw(ied, ie, &newval, sizeof(newval));
}

static int iax_ie_append_short(struct iax_ie_data *ied, unsigned char ie, unsigned short value) 
{
	unsigned short newval;
	newval = htons(value);
	return iax_ie_append_raw(ied, ie, &newval, sizeof(newval));
}

static int iax_ie_append_str(struct iax_ie_data *ied, unsigned char ie, unsigned char *str)
{
	return iax_ie_append_raw(ied, ie, str, strlen(str));
}

static int iax_ie_append_byte(struct iax_ie_data *ied, unsigned char ie, unsigned char dat)
{
	return iax_ie_append_raw(ied, ie, &dat, 1);
}

static int iax2_call(struct ast_channel *c, char *dest, int timeout)
{
	struct sockaddr_in sin;
	char host[256];
	char *rdest;
	char *rcontext;
	char *username;
	char *secret = NULL;
	char *hname;
	char cid[256] = "";
	char *l=NULL, *n=NULL;
	struct iax_ie_data ied;
	char myrdest [5] = "s";
	char context[AST_MAX_EXTENSION] ="";
	char *portno = NULL;
	struct chan_iax2_pvt *p = c->pvt->pvt;
	char *stringp=NULL;
	if ((c->_state != AST_STATE_DOWN) && (c->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "Line is already in use (%s)?\n", c->name);
		return -1;
	}
	strncpy(host, dest, sizeof(host)-1);
	stringp=host;
	strsep(&stringp, "/");
	/* If no destination extension specified, use 's' */
	rdest = strsep(&stringp, "/");
	if (!rdest) 
		rdest = myrdest;
	stringp=rdest;
	strsep(&stringp, "@");
	rcontext = strsep(&stringp, "@");
	stringp=host;
	strsep(&stringp, "@");
	username = strsep(&stringp, "@");
	if (username) {
		/* Really the second argument is the host, not the username */
		hname = username;
		username = host;
	} else {
		hname = host;
	}
	if (username) {
		stringp=username;
		username = strsep(&stringp, ":");
		secret = strsep(&stringp, ":");
	}
	stringp=hname;
	if (strsep(&stringp, ":")) {
		stringp=hname;
		strsep(&stringp, ":");
		portno = strsep(&stringp, ":");
	}
	if (create_addr(&sin, NULL, NULL, NULL, hname, context, NULL)) {
		ast_log(LOG_WARNING, "No address associated with '%s'\n", hname);
		return -1;
	}
	/* Keep track of the context for outgoing calls too */
	strncpy(c->context, context, sizeof(c->context) - 1);
	if (portno) {
		sin.sin_port = htons(atoi(portno));
	}
	if (c->callerid) {
		strncpy(cid, c->callerid, sizeof(cid) - 1);
		ast_callerid_parse(cid, &n, &l);
		if (l)
			ast_shrink_phone_number(l);
	}
	/* Now build request */	
	memset(&ied, 0, sizeof(ied));
	/* On new call, first IE MUST be IAX version of caller */
	iax_ie_append_short(&ied, IAX_IE_VERSION, AST_IAX2_PROTO_VERSION);
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, rdest);
	if (l)
		iax_ie_append_str(&ied, IAX_IE_CALLING_NUMBER, l);
	if (n)
		iax_ie_append_str(&ied, IAX_IE_CALLING_NAME, n);
	if (p->sendani && c->ani) {
		l = n = NULL;
		strncpy(cid, c->ani, sizeof(cid) - 1);
		ast_callerid_parse(cid, &n, &l);
		if (l) {
			ast_shrink_phone_number(l);
			iax_ie_append_str(&ied, IAX_IE_CALLING_ANI, l);
		}
	}
	if (c->language && strlen(c->language))
		iax_ie_append_str(&ied, IAX_IE_LANGUAGE, c->language);
	if (c->dnid && strlen(c->dnid))
		iax_ie_append_str(&ied, IAX_IE_DNID, c->dnid);
	if (rcontext)
		iax_ie_append_str(&ied, IAX_IE_CALLED_CONTEXT, rcontext);
	if (username)
		iax_ie_append_str(&ied, IAX_IE_USERNAME, username);
	if (secret) {
		if (secret[0] == '[') {
			/* This is an RSA key, not a normal secret */
			strncpy(p->outkey, secret + 1, sizeof(p->secret)-1);
			if (strlen(p->outkey)) {
				p->outkey[strlen(p->outkey) - 1] = '\0';
			}
		} else
			strncpy(p->secret, secret, sizeof(p->secret)-1);
	}
	iax_ie_append_int(&ied, IAX_IE_FORMAT, c->nativeformats);
	iax_ie_append_int(&ied, IAX_IE_CAPABILITY, p->capability);
	iax_ie_append_short(&ied, IAX_IE_ADSICPE, c->adsicpe);
	/* Transmit the string in a "NEW" request */
#if 0
	/* XXX We have no equivalent XXX */
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Calling using options '%s'\n", requeststr);
#endif		
	if (p->maxtime) {
		/* Initialize pingtime and auto-congest time */
		p->pingtime = p->maxtime / 2;
		p->initid = ast_sched_add(sched, p->maxtime * 2, auto_congest, (void *)(long)p->callno);
	}
	send_command(p, AST_FRAME_IAX,
		AST_IAX2_COMMAND_NEW, 0, ied.buf, ied.pos, -1);
	ast_setstate(c, AST_STATE_RINGING);
	return 0;
}

static int iax2_hangup(struct ast_channel *c) 
{
	struct chan_iax2_pvt *pvt = c->pvt->pvt;
	int alreadygone;
	int callno;
	if (pvt) {
		callno = pvt->callno;
		ast_pthread_mutex_lock(&iaxsl[callno]);
		ast_log(LOG_DEBUG, "We're hanging up %s now...\n", c->name);
		alreadygone = pvt->alreadygone;
		/* Send the hangup unless we have had a transmission error or are already gone */
		if (!pvt->error && !alreadygone) 
			send_command_final(pvt, AST_FRAME_IAX, AST_IAX2_COMMAND_HANGUP, 0, NULL, 0, -1);
		/* Explicitly predestroy it */
		iax2_predestroy_nolock(callno);
		/* If we were already gone to begin with, destroy us now */
		if (alreadygone) {
			ast_log(LOG_DEBUG, "Really destroying %s now...\n", c->name);
			iax2_destroy_nolock(callno);
		}
		ast_pthread_mutex_unlock(&iaxsl[callno]);
	}
	if (option_verbose > 2) 
		ast_verbose(VERBOSE_PREFIX_3 "Hungup '%s'\n", c->name);
	return 0;
}

static int iax2_setoption(struct ast_channel *c, int option, void *data, int datalen)
{
	struct ast_option_header *h;
	int res;
	h = malloc(datalen + sizeof(struct ast_option_header));
	if (h) {
		h->flag = AST_OPTION_FLAG_REQUEST;
		h->option = htons(option);
		memcpy(h->data, data, datalen);
		res = send_command((struct chan_iax2_pvt *)c->pvt->pvt, AST_FRAME_CONTROL,
			AST_CONTROL_OPTION, 0, (char *)h, datalen + sizeof(struct ast_option_header), -1);
		free(h);
		return res;
	} else 
		ast_log(LOG_WARNING, "Out of memory\n");
	return -1;
}
static struct ast_frame *iax2_read(struct ast_channel *c) 
{
	static struct ast_frame f = { AST_FRAME_NULL, };
	ast_log(LOG_NOTICE, "I should never be called!\n");
	return &f;
}

static int iax2_start_transfer(struct ast_channel *c0, struct ast_channel *c1)
{
	int res;
	struct iax_ie_data ied0;
	struct iax_ie_data ied1;
	struct chan_iax2_pvt *p0 = c0->pvt->pvt;
	struct chan_iax2_pvt *p1 = c1->pvt->pvt;
	memset(&ied0, 0, sizeof(ied0));
	iax_ie_append_addr(&ied0, IAX_IE_APPARENT_ADDR, &p1->addr);
	iax_ie_append_short(&ied0, IAX_IE_CALLNO, p1->peercallno);

	memset(&ied1, 0, sizeof(ied1));
	iax_ie_append_addr(&ied1, IAX_IE_APPARENT_ADDR, &p0->addr);
	iax_ie_append_short(&ied1, IAX_IE_CALLNO, p0->peercallno);
	
	res = send_command(p0, AST_FRAME_IAX, AST_IAX2_COMMAND_TXREQ, 0, ied0.buf, ied0.pos, -1);
	if (res)
		return -1;
	res = send_command(p1, AST_FRAME_IAX, AST_IAX2_COMMAND_TXREQ, 0, ied1.buf, ied1.pos, -1);
	if (res)
		return -1;
	p0->transferring = TRANSFER_BEGIN;
	p1->transferring = TRANSFER_BEGIN;
	return 0;
}

static int iax2_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	struct ast_channel *cs[3];
	struct ast_channel *who;
	int to = -1;
	int res;
	int transferstarted=0;
	struct ast_frame *f;
	struct chan_iax2_pvt *p0 = c0->pvt->pvt;
	struct chan_iax2_pvt *p1 = c1->pvt->pvt;

	/* Put them in native bridge mode */
	p0->bridgecallno = p1->callno;
	p1->bridgecallno = p0->callno;

	/* If not, try to bridge until we can execute a transfer, if we can */
	cs[0] = c0;
	cs[1] = c1;
	for (/* ever */;;) {
		/* Check in case we got masqueraded into */
		if ((c0->type != type) || (c1->type != type)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Can't masquerade, we're different...\n");
			return -2;
		}
		if (c0->nativeformats != c1->nativeformats) {
			ast_verbose(VERBOSE_PREFIX_3 "Operating with different codecs, can't native bridge...\n");
			return -2;
		}
		if (!transferstarted) {
			/* Try the transfer */
			if (iax2_start_transfer(c0, c1))
				ast_log(LOG_WARNING, "Unable to start the transfer\n");
			transferstarted = 1;
		}
		
		if ((p0->transferring == TRANSFER_RELEASED) && (p1->transferring == TRANSFER_RELEASED)) {
			/* Call has been transferred.  We're no longer involved */
			sleep(1);
			c0->_softhangup |= AST_SOFTHANGUP_DEV;
			c1->_softhangup |= AST_SOFTHANGUP_DEV;
			*fo = NULL;
			*rc = c0;
			res = 0;
			break;
		}
		to = 1000;
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			res = 0;
			break;
		}
		if ((f->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			*fo = f;
			*rc = who;
			res =  0;
			break;
		}
		if ((f->frametype == AST_FRAME_VOICE) ||
			(f->frametype == AST_FRAME_TEXT) ||
			(f->frametype == AST_FRAME_VIDEO) || 
			(f->frametype == AST_FRAME_IMAGE) ||
			(f->frametype == AST_FRAME_DTMF)) {
			if ((f->frametype == AST_FRAME_DTMF) && 
				(flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))) {
				if ((who == c0)) {
					if  ((flags & AST_BRIDGE_DTMF_CHANNEL_0)) {
						*rc = c0;
						*fo = f;
						/* Take out of conference mode */
						res = 0;
						break;
					} else 
						goto tackygoto;
				} else
				if ((who == c1)) {
					if (flags & AST_BRIDGE_DTMF_CHANNEL_1) {
						*rc = c1;
						*fo = f;
						res =  0;
						break;
					} else
						goto tackygoto;
				}
			} else {
#if 0
				ast_log(LOG_DEBUG, "Read from %s\n", who->name);
				if (who == last) 
					ast_log(LOG_DEBUG, "Servicing channel %s twice in a row?\n", last->name);
				last = who;
#endif
tackygoto:
				if (who == c0) 
					ast_write(c1, f);
				else 
					ast_write(c0, f);
			}
			ast_frfree(f);
		} else
			ast_frfree(f);
		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
	p0->bridgecallno = 0;
	p1->bridgecallno = 0;
	return res;
}

static int iax2_answer(struct ast_channel *c)
{
	struct chan_iax2_pvt *pvt = c->pvt->pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "Answering\n");
	return send_command(pvt, AST_FRAME_CONTROL, AST_CONTROL_ANSWER, 0, NULL, 0, -1);
}

static int iax2_indicate(struct ast_channel *c, int condition)
{
	struct chan_iax2_pvt *pvt = c->pvt->pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "Indicating condition %d\n", condition);
	return send_command(pvt, AST_FRAME_CONTROL, condition, 0, NULL, 0, -1);
}
	

static int iax2_write(struct ast_channel *c, struct ast_frame *f);

static int iax2_getpeername(struct sockaddr_in sin, char *host, int len)
{
	struct iax2_peer *peer;
	int res = 0;
	ast_pthread_mutex_lock(&peerl.lock);
	peer = peerl.peers;
	while(peer) {
		if ((peer->addr.sin_addr.s_addr == sin.sin_addr.s_addr) &&
				(peer->addr.sin_port == sin.sin_port)) {
					strncpy(host, peer->name, len-1);
					res = 1;
					break;
		}
		peer = peer->next;
	}
	ast_pthread_mutex_unlock(&peerl.lock);
	return res;
}

static struct ast_channel *ast_iax2_new(struct chan_iax2_pvt *i, int state, int capability)
{
	char host[256];
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		if (!iax2_getpeername(i->addr, host, sizeof(host)))
			snprintf(host, sizeof(host), "%s:%d", inet_ntoa(i->addr.sin_addr), ntohs(i->addr.sin_port));
		if (strlen(i->username))
			snprintf(tmp->name, sizeof(tmp->name), "IAX2[%s@%s]/%d", i->username, host, i->callno);
		else
			snprintf(tmp->name, sizeof(tmp->name), "IAX2[%s]/%d", host, i->callno);
		tmp->type = type;
		/* We can support any format by default, until we get restricted */
		tmp->nativeformats = capability;
		tmp->readformat = 0;
		tmp->writeformat = 0;
		tmp->pvt->pvt = i;
		tmp->pvt->send_digit = iax2_digit;
		tmp->pvt->send_text = iax2_sendtext;
		tmp->pvt->send_image = iax2_sendimage;
		tmp->pvt->send_html = iax2_sendhtml;
		tmp->pvt->call = iax2_call;
		tmp->pvt->hangup = iax2_hangup;
		tmp->pvt->answer = iax2_answer;
		tmp->pvt->read = iax2_read;
		tmp->pvt->write = iax2_write;
		tmp->pvt->indicate = iax2_indicate;
		tmp->pvt->setoption = iax2_setoption;
		tmp->pvt->bridge = iax2_bridge;
		if (strlen(i->callerid))
			tmp->callerid = strdup(i->callerid);
		if (strlen(i->ani))
			tmp->ani = strdup(i->ani);
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		if (strlen(i->dnid))
			tmp->dnid = strdup(i->dnid);
		if (strlen(i->accountcode))
			strncpy(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode)-1);
		if (i->amaflags)
			tmp->amaflags = i->amaflags;
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, i->exten, sizeof(tmp->exten)-1);
		tmp->adsicpe = i->peeradsicpe;
		tmp->pvt->fixup = iax2_fixup;
		i->owner = tmp;
		i->capability = capability;
		ast_setstate(tmp, state);
		ast_pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	}
	return tmp;
}

static unsigned int calc_txpeerstamp(struct iax2_peer *peer)
{
	struct timeval tv;
	unsigned int mssincetx;
	unsigned int ms;
	gettimeofday(&tv, NULL);
	mssincetx = (tv.tv_sec - peer->lasttxtime.tv_sec) * 1000 + (tv.tv_usec - peer->lasttxtime.tv_usec) / 1000;
	if (mssincetx > 5000) {
		/* If it's been at least 5 seconds since the last time we transmitted on this trunk, reset our timers */
		peer->txtrunktime.tv_sec = tv.tv_sec;
		peer->txtrunktime.tv_usec = tv.tv_usec;
	}
	/* Update last transmit time now */
	peer->lasttxtime.tv_sec = tv.tv_sec;
	peer->lasttxtime.tv_usec = tv.tv_usec;
	
	/* Calculate ms offset */
	ms = (tv.tv_sec - peer->txtrunktime.tv_sec) * 1000 + (tv.tv_usec - peer->txtrunktime.tv_usec) / 1000;
	
	/* We never send the same timestamp twice, so fudge a little if we must */
	if (ms == peer->lastsent)
		ms = peer->lastsent + 1;
	peer->lastsent = ms;
	return ms;
}

static unsigned int fix_peerts(struct iax2_peer *peer, int callno, unsigned int ts)
{
	long ms;	/* NOT unsigned */
	if (!iaxs[callno]->rxcore.tv_sec && !iaxs[callno]->rxcore.tv_usec) {
		/* Initialize rxcore time if appropriate */
		gettimeofday(&iaxs[callno]->rxcore, NULL);
	}
	/* Calculate difference between trunk and channel */
	ms = (peer->rxtrunktime.tv_sec - iaxs[callno]->rxcore.tv_sec) * 1000 + 
		(peer->rxtrunktime.tv_usec - iaxs[callno]->rxcore.tv_usec) / 1000;
	/* Return as the sum of trunk time and the difference between trunk and real time */
	return ms + ts;
}

static unsigned int calc_timestamp(struct chan_iax2_pvt *p, unsigned int ts)
{
	struct timeval tv;
	unsigned int ms;
	if (!p->offset.tv_sec && !p->offset.tv_usec)
		gettimeofday(&p->offset, NULL);
	/* If the timestamp is specified, just send it as is */
	if (ts)
		return ts;
	gettimeofday(&tv, NULL);
	ms = (tv.tv_sec - p->offset.tv_sec) * 1000 + (tv.tv_usec - p->offset.tv_usec) / 1000;
	/* We never send the same timestamp twice, so fudge a little if we must */
	if (ms <= p->lastsent)
		ms = p->lastsent + 1;
	p->lastsent = ms;
	return ms;
}

#ifdef BRIDGE_OPTIMIZATION
static unsigned int calc_fakestamp(struct chan_iax2_pvt *p1, struct chan_iax2_pvt *p2, unsigned int fakets)
{
	int ms;
	/* Receive from p1, send to p2 */
	
	/* Setup rxcore if necessary on outgoing channel */
	if (!p1->rxcore.tv_sec && !p1->rxcore.tv_usec)
		gettimeofday(&p1->rxcore, NULL);

	/* Setup txcore if necessary on outgoing channel */
	if (!p2->offset.tv_sec && !p2->offset.tv_usec)
		gettimeofday(&p2->offset, NULL);
	
	/* Now, ts is the timestamp of the original packet in the orignal context.
	   Adding rxcore to it gives us when we would want the packet to be delivered normally.
	   Subtracting txcore of the outgoing channel gives us what we'd expect */
	
	ms = (p1->rxcore.tv_sec - p2->offset.tv_sec) * 1000 + (p1->rxcore.tv_usec - p2->offset.tv_usec) / 1000;
	fakets += ms;
	if (fakets <= p2->lastsent)
		fakets = p2->lastsent + 1;
	p2->lastsent = fakets;
	return fakets;
}
#endif

static unsigned int calc_rxstamp(struct chan_iax2_pvt *p)
{
	/* Returns where in "receive time" we are */
	struct timeval tv;
	unsigned int ms;
	/* Setup rxcore if necessary */
	if (!p->rxcore.tv_sec && !p->rxcore.tv_usec)
		gettimeofday(&p->rxcore, NULL);

	gettimeofday(&tv, NULL);
	ms = (tv.tv_sec - p->rxcore.tv_sec) * 1000 + (tv.tv_usec - p->rxcore.tv_usec) / 1000;
	return ms;
}

static int iax2_send(struct chan_iax2_pvt *pvt, struct ast_frame *f, unsigned int ts, int seqno, int now, int transfer, int final)
{
	/* Queue a packet for delivery on a given private structure.  Use "ts" for
	   timestamp, or calculate if ts is 0.  Send immediately without retransmission
	   or delayed, with retransmission */
	struct ast_iax2_full_hdr *fh;
	struct ast_iax2_mini_hdr *mh;
	struct ast_iax2_frame *fr, fr2;
	int res;
	unsigned int lastsent;
	/* Allocate an ast_iax2_frame */
	if (now) {
		fr = &fr2;
	} else
		fr = ast_iax2_frame_new(DIRECTION_OUTGRESS, f->datalen);
	if (!fr) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	if (!pvt) {
		ast_log(LOG_WARNING, "No private structure for packet (%d)?\n", fr->callno);
		if (!now)
			ast_iax2_frame_free(fr);
		return -1;
	}
	/* Copy our prospective frame into our immediate or retransmitted wrapper */
	ast_iax2_frame_wrap(fr, f);

	lastsent = pvt->lastsent;
	fr->ts = calc_timestamp(pvt, ts);
	if (!fr->ts) {
		ast_log(LOG_WARNING, "timestamp is 0?\n");
		if (!now)
			ast_iax2_frame_free(fr);
		return -1;
	}
	fr->callno = pvt->callno;
	fr->transfer = transfer;
	fr->final = final;
	if (((fr->ts & 0xFFFF0000L) != (lastsent & 0xFFFF0000L))
		/* High two bits of timestamp differ */ ||
	    (fr->af.frametype != AST_FRAME_VOICE) 
		/* or not a voice frame */ || 
		(fr->af.subclass != pvt->svoiceformat) 
		/* or new voice format */ ) {
		/* We need a full frame */
		if (seqno > -1)
			fr->oseqno = seqno;
		else
			fr->oseqno = pvt->oseqno++;
		fr->iseqno = pvt->iseqno;
		fh = (struct ast_iax2_full_hdr *)(fr->af.data - sizeof(struct ast_iax2_full_hdr));
		fh->scallno = htons(fr->callno | AST_FLAG_FULL);
		fh->ts = htonl(fr->ts);
		fh->oseqno = fr->oseqno;
		fh->iseqno = fr->iseqno;
		/* Keep track of the last thing we've acknowledged */
		pvt->aseqno = fr->iseqno;
		fh->type = fr->af.frametype & 0xFF;
		fh->csub = compress_subclass(fr->af.subclass);
		if (transfer) {
			fr->dcallno = pvt->transfercallno;
		} else
			fr->dcallno = pvt->peercallno;
		fh->dcallno = htons(fr->dcallno);
		fr->datalen = fr->af.datalen + sizeof(struct ast_iax2_full_hdr);
		fr->data = fh;
		fr->retries = 0;
		/* Retry after 2x the ping time has passed */
		fr->retrytime = pvt->pingtime * 2;
		if (fr->retrytime < MIN_RETRY_TIME)
			fr->retrytime = MIN_RETRY_TIME;
		if (fr->retrytime > MAX_RETRY_TIME)
			fr->retrytime = MAX_RETRY_TIME;
		/* Acks' don't get retried */
		if ((f->frametype == AST_FRAME_IAX) && (f->subclass == AST_IAX2_COMMAND_ACK))
			fr->retries = -1;
		if (f->frametype == AST_FRAME_VOICE) {
			pvt->svoiceformat = f->subclass;
		}
		if (now) {
			res = send_packet(fr);
		} else
			res = iax2_transmit(fr);
	} else {
		if (pvt->trunk) {
			/* Queue for transmission in a meta frame */
			if ((sizeof(pvt->trunkdata) - pvt->trunkdatalen) >= fr->af.datalen) {
				memcpy(pvt->trunkdata + pvt->trunkdatalen, fr->af.data, fr->af.datalen);
				pvt->trunkdatalen += fr->af.datalen;
				res = 0;
				pvt->trunkerror = 0;
			} else {
				if (!pvt->trunkerror)
					ast_log(LOG_WARNING, "Out of trunk data space on call number %d, dropping\n", pvt->callno);
				pvt->trunkerror = 1;
			}
		} else {
			/* Mini-frames have no sequence number */
			fr->oseqno = -1;
			fr->iseqno = -1;
			/* Mini frame will do */
			mh = (struct ast_iax2_mini_hdr *)(fr->af.data - sizeof(struct ast_iax2_mini_hdr));
			mh->callno = htons(fr->callno);
			mh->ts = htons(fr->ts & 0xFFFF);
			fr->datalen = fr->af.datalen + sizeof(struct ast_iax2_mini_hdr);
			fr->data = mh;
			fr->retries = -1;
			if (now) {
				res = send_packet(fr);
			} else
				res = iax2_transmit(fr);
		}
	}
	return res;
}



static int iax2_show_users(int fd, int argc, char *argv[])
{
#define FORMAT "%-15.15s  %-15.15s  %-15.15s  %-15.15s  %-5.5s\n"
#define FORMAT2 "%-15.15s  %-15.15s  %-15.15d  %-15.15s  %-5.5s\n"
	struct iax2_user *user;
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&userl.lock);
	ast_cli(fd, FORMAT, "Username", "Secret", "Authen", "Def.Context", "A/C");
	for(user=userl.users;user;user=user->next) {
		ast_cli(fd, FORMAT2, user->name, user->secret, user->authmethods, 
				user->contexts ? user->contexts->context : context,
				user->ha ? "Yes" : "No");
	}
	ast_pthread_mutex_unlock(&userl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int iax2_show_peers(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-15.15s %s  %-15.15s  %-8s  %-10s\n"
#define FORMAT "%-15.15s  %-15.15s %s  %-15.15s  %-5d%s  %-10s\n"
	struct iax2_peer *peer;
	char name[256] = "";
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&peerl.lock);
	ast_cli(fd, FORMAT2, "Name/Username", "Host", "   ", "Mask", "Port", "Status");
	for (peer = peerl.peers;peer;peer = peer->next) {
		char nm[20];
		char status[20];
		if (strlen(peer->username))
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
		strncpy(nm, inet_ntoa(peer->mask), sizeof(nm)-1);
		ast_cli(fd, FORMAT, name, 
					peer->addr.sin_addr.s_addr ? inet_ntoa(peer->addr.sin_addr) : "(Unspecified)",
					peer->dynamic ? "(D)" : "(S)",
					nm,
					ntohs(peer->addr.sin_port), peer->trunk ? "(T)" : "   ", status);
	}
	ast_pthread_mutex_unlock(&peerl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

/* JDG: callback to display iax peers in manager */
static int manager_iax2_show_peers( struct mansession *s, struct message *m )
{
	char *a[] = { "iax2", "show", "users" };
	int ret;
	ret = iax2_show_peers( s->fd, 3, a );
	ast_cli( s->fd, "\r\n" );
	return ret;
} /* /JDG */

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

static int iax2_show_registry(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-20.20s  %-10.10s  %-20.20s %8.8s  %s\n"
#define FORMAT "%-20.20s  %-10.10s  %-20.20s %8d  %s\n"
	struct iax2_registry *reg;
	char host[80];
	char perceived[80];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&peerl.lock);
	ast_cli(fd, FORMAT2, "Host", "Username", "Perceived", "Refresh", "State");
	for (reg = registrations;reg;reg = reg->next) {
		snprintf(host, sizeof(host), "%s:%d", inet_ntoa(reg->addr.sin_addr), ntohs(reg->addr.sin_port));
		if (reg->us.sin_addr.s_addr) 
			snprintf(perceived, sizeof(perceived), "%s:%d", inet_ntoa(reg->us.sin_addr), ntohs(reg->us.sin_port));
		else
			strcpy(perceived, "<Unregistered>");
		ast_cli(fd, FORMAT, host, 
					reg->username, perceived, reg->refresh, regstate2str(reg->regstate));
	}
	ast_pthread_mutex_unlock(&peerl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int iax2_show_channels(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-10.10s  %-11.11s  %-11.11s  %-7.7s  %-6.6s  %s\n"
#define FORMAT  "%-15.15s  %-10.10s  %5.5d/%5.5d  %5.5d/%5.5d  %-5.5dms  %-4.4dms  %d\n"
	int x;
	int numchans = 0;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT2, "Peer", "Username", "ID (Lo/Rem)", "Seq (Tx/Rx)", "Lag", "Jitter", "Format");
	for (x=0;x<AST_IAX2_MAX_CALLS;x++) {
		ast_pthread_mutex_lock(&iaxsl[x]);
		if (iaxs[x]) {
			ast_cli(fd, FORMAT, inet_ntoa(iaxs[x]->addr.sin_addr), 
						strlen(iaxs[x]->username) ? iaxs[x]->username : "(None)", 
						iaxs[x]->callno, iaxs[x]->peercallno, 
						iaxs[x]->oseqno, iaxs[x]->iseqno, 
						iaxs[x]->lag,
						iaxs[x]->jitter,
						iaxs[x]->voiceformat);
			numchans++;
		}
		ast_pthread_mutex_unlock(&iaxsl[x]);
	}
	ast_cli(fd, "%d active IAX channel(s)\n", numchans);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int iax2_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	iaxdebug = 1;
	ast_cli(fd, "IAX2 Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int iax2_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	iaxdebug = 0;
	ast_cli(fd, "IAX2 Debugging Disabled\n");
	return RESULT_SUCCESS;
}



static char show_users_usage[] = 
"Usage: iax2 show users\n"
"       Lists all users known to the IAX2 (Inter-Asterisk eXchange rev 2) subsystem.\n";

static char show_channels_usage[] = 
"Usage: iax2 show channels\n"
"       Lists all currently active IAX2 channels.\n";

static char show_peers_usage[] = 
"Usage: iax2 show peers\n"
"       Lists all known IAX peers.\n";

static char show_reg_usage[] =
"Usage: iax2 show registry\n"
"       Lists all registration requests and status.\n";

#ifdef DEBUG_SUPPORT

static char debug_usage[] = 
"Usage: iax2 debug\n"
"       Enables dumping of IAX2 packets for debugging purposes\n";

static char no_debug_usage[] = 
"Usage: iax2 no debug\n"
"       Disables dumping of IAX2 packets for debugging purposes\n";

#endif

static struct ast_cli_entry  cli_show_users = 
	{ { "iax2", "show", "users", NULL }, iax2_show_users, "Show defined IAX2 users", show_users_usage };
static struct ast_cli_entry  cli_show_channels =
	{ { "iax2", "show", "channels", NULL }, iax2_show_channels, "Show active IAX2 channels", show_channels_usage };
static struct ast_cli_entry  cli_show_peers =
	{ { "iax2", "show", "peers", NULL }, iax2_show_peers, "Show defined IAX2 peers", show_peers_usage };
static struct ast_cli_entry  cli_show_registry =
	{ { "iax2", "show", "registry", NULL }, iax2_show_registry, "Show IAX2 registration status", show_reg_usage };
static struct ast_cli_entry  cli_debug =
	{ { "iax2", "debug", NULL }, iax2_do_debug, "Enable IAX2 debugging", debug_usage };
static struct ast_cli_entry  cli_no_debug =
	{ { "iax2", "no", "debug", NULL }, iax2_no_debug, "Disable IAX2 debugging", no_debug_usage };

static int iax2_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_iax2_pvt *i = c->pvt->pvt;
	if (!i)
		return -1;
	/* If there's an outstanding error, return failure now */
	if (i->error) {
		ast_log(LOG_DEBUG, "Write error: %s\n", strerror(errno));
		return -1;
	}
	/* If it's already gone, just return */
	if (i->alreadygone)
		return 0;
	/* Don't waste bandwidth sending null frames */
	if (f->frametype == AST_FRAME_NULL)
		return 0;
	/* If we're quelching voice, don't bother sending it */
	if ((f->frametype == AST_FRAME_VOICE) && i->quelch)
		return 0;
	/* Simple, just queue for transmission */
	return iax2_send(i, f, 0, -1, 0, 0, 0);
}

static int __send_command(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno, 
		int now, int transfer, int final)
{
	struct ast_frame f;
	f.frametype = type;
	f.subclass = command;
	f.datalen = datalen;
	f.samples = 0;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __FUNCTION__;
	f.data = data;
	return iax2_send(i, &f, ts, seqno, now, transfer, final);
}

static int send_command(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
	return __send_command(i, type, command, ts, data, datalen, seqno, 0, 0, 0);
}

#ifdef BRIDGE_OPTIMIZATION
static int forward_command(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
	return __send_command(iaxs[i->bridgecallno], type, command, ts, data, datalen, seqno, 0, 0, 0);
}
#endif

static int send_command_final(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
	/* It is assumed that the callno has already been locked */
	iax2_predestroy_nolock(i->callno);
	return __send_command(i, type, command, ts, data, datalen, seqno, 0, 0, 1);
}

static int send_command_immediate(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
	return __send_command(i, type, command, ts, data, datalen, seqno, 1, 0, 0);
}

static int send_command_transfer(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, char *data, int datalen)
{
	return __send_command(i, type, command, ts, data, datalen, 0, 0, 1, 0);
}

static int apply_context(struct iax2_context *con, char *context)
{
	while(con) {
		if (!strcmp(con->context, context))
			return -1;
		con = con->next;
	}
	return 0;
}


static int check_access(int callno, struct sockaddr_in *sin, struct iax_ies *ies)
{
	/* Start pessimistic */
	int res = -1;
	int version = 2;
	struct iax2_user *user;
	int gotcapability=0;
	if (!iaxs[callno])
		return res;
	if (ies->called_number)
		strncpy(iaxs[callno]->exten, ies->called_number, sizeof(iaxs[callno]->exten) - 1);
	if (ies->calling_number) {
		if (ies->calling_name)
			snprintf(iaxs[callno]->callerid, sizeof(iaxs[callno]->callerid), "\"%s\" <%s>", ies->calling_name, ies->calling_number);
		else
			strncpy(iaxs[callno]->callerid, ies->calling_number, sizeof(iaxs[callno]->callerid) - 1);
	} else if (ies->calling_name)
		strncpy(iaxs[callno]->callerid, ies->calling_name, sizeof(iaxs[callno]->callerid) - 1);
	if (ies->calling_ani)
		strncpy(iaxs[callno]->ani, ies->calling_ani, sizeof(iaxs[callno]->ani) - 1);
	if (ies->dnid)
		strncpy(iaxs[callno]->dnid, ies->dnid, sizeof(iaxs[callno]->dnid)-1);
	if (ies->called_context)
		strncpy(iaxs[callno]->context, ies->called_context, sizeof(iaxs[callno]->context)-1);
	if (ies->language)
		strncpy(iaxs[callno]->language, ies->language, sizeof(iaxs[callno]->language)-1);
	if (ies->username)
		strncpy(iaxs[callno]->username, ies->username, sizeof(iaxs[callno]->username)-1);
	if (ies->format)
		iaxs[callno]->peerformat = ies->format;
	if (ies->adsicpe)
		iaxs[callno]->peeradsicpe = ies->adsicpe;
	if (ies->capability) {
		gotcapability = 1;
		iaxs[callno]->peercapability = ies->capability;
	} 
	if (ies->version)
		version = ies->version;
	if (!gotcapability) 
		iaxs[callno]->peercapability = iaxs[callno]->peerformat;
	if (version > AST_IAX2_PROTO_VERSION) {
		ast_log(LOG_WARNING, "Peer '%s' has too new a protocol version (%d) for me\n", 
			inet_ntoa(sin->sin_addr), version);
		return res;
	}
	ast_pthread_mutex_lock(&userl.lock);
	/* Search the userlist for a compatible entry, and fill in the rest */
	user = userl.users;
	while(user) {
		if ((!strlen(iaxs[callno]->username) ||				/* No username specified */
			!strcmp(iaxs[callno]->username, user->name))	/* Or this username specified */
			&& ast_apply_ha(user->ha, sin) 	/* Access is permitted from this IP */
			&& (!strlen(iaxs[callno]->context) ||			/* No context specified */
			     apply_context(user->contexts, iaxs[callno]->context))) {			/* Context is permitted */
			/* We found our match (use the first) */
			
			/* Store the requested username if not specified */
			if (!strlen(iaxs[callno]->username))
				strncpy(iaxs[callno]->username, user->name, sizeof(iaxs[callno]->username)-1);
			/* Store whether this is a trunked call, too, of course, and move if appropriate */
			iaxs[callno]->trunk = user->trunk;
			/* And use the default context */
			if (!strlen(iaxs[callno]->context)) {
				if (user->contexts)
					strncpy(iaxs[callno]->context, user->contexts->context, sizeof(iaxs[callno]->context)-1);
				else
					strncpy(iaxs[callno]->context, context, sizeof(iaxs[callno]->context)-1);
			}
			/* Copy the secret */
			strncpy(iaxs[callno]->secret, user->secret, sizeof(iaxs[callno]->secret)-1);
			/* And any input keys */
			strncpy(iaxs[callno]->inkeys, user->inkeys, sizeof(iaxs[callno]->inkeys));
			/* And the permitted authentication methods */
			iaxs[callno]->authmethods = user->authmethods;
			/* If they have callerid, override the given caller id.  Always store the ANI */
			if (strlen(iaxs[callno]->callerid)) {
				if (user->hascallerid)
					strncpy(iaxs[callno]->callerid, user->callerid, sizeof(iaxs[callno]->callerid)-1);
				strncpy(iaxs[callno]->ani, user->callerid, sizeof(iaxs[callno]->ani)-1);
			}
			if (strlen(user->accountcode))
				strncpy(iaxs[callno]->accountcode, user->accountcode, sizeof(iaxs[callno]->accountcode)-1);
			if (user->amaflags)
				iaxs[callno]->amaflags = user->amaflags;
			res = 0;
			break;
		}
		user = user->next;	
	}
	ast_pthread_mutex_unlock(&userl.lock);
	return res;
}

static int raw_hangup(struct sockaddr_in *sin, unsigned short src, unsigned short dst)
{
	struct ast_iax2_full_hdr fh;
	fh.scallno = htons(src | AST_FLAG_FULL);
	fh.dcallno = htons(dst);
	fh.ts = 0;
	fh.oseqno = 0;
	fh.iseqno = 0;
	fh.type = AST_FRAME_IAX;
	fh.csub = compress_subclass(AST_IAX2_COMMAND_INVAL);
#if 0
	if (option_debug)
#endif	
		ast_log(LOG_DEBUG, "Raw Hangup %s:%d, src=%d, dst=%d\n",
			inet_ntoa(sin->sin_addr), ntohs(sin->sin_port), src, dst);
	return sendto(netsocket, &fh, sizeof(fh), 0, (struct sockaddr *)sin, sizeof(*sin));
}

static int authenticate_request(struct chan_iax2_pvt *p)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_short(&ied, IAX_IE_AUTHMETHODS, p->authmethods);
	if (p->authmethods & (IAX_AUTH_MD5 | IAX_AUTH_RSA)) {
		snprintf(p->challenge, sizeof(p->challenge), "%d", rand());
		iax_ie_append_str(&ied, IAX_IE_CHALLENGE, p->challenge);
	}
	iax_ie_append_str(&ied,IAX_IE_USERNAME, p->username);
	return send_command(p, AST_FRAME_IAX, AST_IAX2_COMMAND_AUTHREQ, 0, ied.buf, ied.pos, -1);
}

static int authenticate_verify(struct chan_iax2_pvt *p, struct iax_ies *ies)
{
	char requeststr[256] = "";
	char md5secret[256] = "";
	char secret[256] = "";
	char rsasecret[256] = "";
	int res = -1; 
	int x;
	
	if (!(p->state & IAX_STATE_AUTHENTICATED))
		return res;
	if (ies->password)
		strncpy(secret, ies->password, sizeof(secret) - 1);
	if (ies->md5_result)
		strncpy(md5secret, ies->md5_result, sizeof(md5secret)-1);
	if (ies->rsa_result)
		strncpy(rsasecret, ies->rsa_result, sizeof(rsasecret)-1);
	printf("Auth methods: %d, rsasecret: %s, inkeys: %s\n", p->authmethods, rsasecret, p->inkeys);
	if ((p->authmethods & IAX_AUTH_RSA) && strlen(rsasecret) && strlen(p->inkeys)) {
		struct ast_key *key;
		char *keyn;
		char tmpkey[256];
		char *stringp=NULL;
		printf("Checking RSA methods\n");
		strncpy(tmpkey, p->inkeys, sizeof(tmpkey));
		stringp=tmpkey;
		keyn = strsep(&stringp, ":");
		while(keyn) {
			key = ast_key_get(keyn, AST_KEY_PUBLIC);
			if (key && !ast_check_signature(key, p->challenge, rsasecret)) {
				res = 0;
				break;
			} else if (!key)
				ast_log(LOG_WARNING, "requested inkey '%s' for RSA authentication does not exist\n", keyn);
			keyn = strsep(&stringp, ":");
		}
	} else if (p->authmethods & IAX_AUTH_MD5) {
		struct MD5Context md5;
		unsigned char digest[16];
		MD5Init(&md5);
		MD5Update(&md5, p->challenge, strlen(p->challenge));
		MD5Update(&md5, p->secret, strlen(p->secret));
		MD5Final(digest, &md5);
		/* If they support md5, authenticate with it.  */
		for (x=0;x<16;x++)
			sprintf(requeststr + (x << 1), "%2.2x", digest[x]);
		if (!strcasecmp(requeststr, md5secret))
			res = 0;
	} else if (p->authmethods & IAX_AUTH_PLAINTEXT) {
		if (!strcmp(secret, p->secret))
			res = 0;
	}
	return res;
}

static int register_verify(int callno, struct sockaddr_in *sin, struct iax_ies *ies)
{
	char requeststr[256] = "";
	char peer[256] = "";
	char md5secret[256] = "";
	char rsasecret[256] = "";
	char secret[256] = "";
	struct iax2_peer *p;
	struct ast_key *key;
	char *keyn;
	int x;
	int expire = 0;

	iaxs[callno]->state &= ~IAX_STATE_AUTHENTICATED;
	strcpy(iaxs[callno]->peer, "");
	if (ies->username)
		strncpy(peer, ies->username, sizeof(peer) - 1);
	if (ies->password)
		strncpy(secret, ies->password, sizeof(secret) - 1);
	if (ies->md5_result)
		strncpy(md5secret, ies->md5_result, sizeof(md5secret)-1);
	if (ies->rsa_result)
		strncpy(rsasecret, ies->rsa_result, sizeof(rsasecret)-1);
	if (ies->refresh)
		expire = ies->refresh;

	if (!strlen(peer)) {
		ast_log(LOG_NOTICE, "Empty registration from %s\n", inet_ntoa(sin->sin_addr));
		return -1;
	}

	for (p = peerl.peers; p ; p = p->next) 
		if (!strcasecmp(p->name, peer))
			break;

	if (!p) {
		ast_log(LOG_NOTICE, "No registration for peer '%s' (from %s)\n", peer, inet_ntoa(sin->sin_addr));
		return -1;
	}

	if (!p->dynamic) {
		ast_log(LOG_NOTICE, "Peer '%s' is not dynamic (from %s)\n", peer, inet_ntoa(sin->sin_addr));
		return -1;
	}

	if (!ast_apply_ha(p->ha, sin)) {
		ast_log(LOG_NOTICE, "Host %s denied access to register peer '%s'\n", inet_ntoa(sin->sin_addr), p->name);
		return -1;
	}
	strncpy(iaxs[callno]->secret, p->secret, sizeof(iaxs[callno]->secret)-1);
	strncpy(iaxs[callno]->inkeys, p->inkeys, sizeof(iaxs[callno]->inkeys)-1);
	/* Check secret against what we have on file */
	if (strlen(rsasecret) && (p->authmethods & IAX_AUTH_RSA) && strlen(p->challenge)) {
		if (strlen(p->inkeys)) {
			char tmpkeys[256];
			char *stringp=NULL;
			strncpy(tmpkeys, p->inkeys, sizeof(tmpkeys));
			stringp=tmpkeys;
			keyn = strsep(&stringp, ":");
			while(keyn) {
				key = ast_key_get(keyn, AST_KEY_PUBLIC);
				if (key && !ast_check_signature(key, p->challenge, rsasecret)) {
					iaxs[callno]->state |= IAX_STATE_AUTHENTICATED;
					break;
				} else if (!key) 
					ast_log(LOG_WARNING, "requested inkey '%s' does not exist\n", keyn);
				keyn = strsep(&stringp, ":");
			}
			if (!keyn) {
				ast_log(LOG_NOTICE, "Host %s failed RSA authentication with inkeys '%s'\n", peer, p->inkeys);
				return -1;
			}
		} else {
			ast_log(LOG_NOTICE, "Host '%s' trying to do RSA authentication, but we have no inkeys\n", peer);
			return -1;
		}
	} else if (strlen(secret) && (p->authmethods & IAX_AUTH_PLAINTEXT)) {
		/* They've provided a plain text password and we support that */
		if (strcmp(secret, p->secret)) {
			ast_log(LOG_NOTICE, "Host %s did not provide proper plaintext password for '%s'\n", inet_ntoa(sin->sin_addr), p->name);
			return -1;
		} else
			iaxs[callno]->state |= IAX_STATE_AUTHENTICATED;
	} else if (strlen(md5secret) && (p->authmethods & IAX_AUTH_MD5) && strlen(p->challenge)) {
		struct MD5Context md5;
		unsigned char digest[16];
		MD5Init(&md5);
		MD5Update(&md5, p->challenge, strlen(p->challenge));
		MD5Update(&md5, p->secret, strlen(p->secret));
		MD5Final(digest, &md5);
		for (x=0;x<16;x++)
			sprintf(requeststr + (x << 1), "%2.2x", digest[x]);
		if (strcasecmp(requeststr, md5secret)) {
			ast_log(LOG_NOTICE, "Host %s failed MD5 authentication for '%s' (%s != %s)\n", inet_ntoa(sin->sin_addr), p->name, requeststr, md5secret);
			return -1;
		} else
			iaxs[callno]->state |= IAX_STATE_AUTHENTICATED;
	} else if (strlen(md5secret) || strlen(secret)) {
		ast_log(LOG_NOTICE, "Inappropriate authentication received\n");
		return -1;
	}
	strncpy(iaxs[callno]->peer, peer, sizeof(iaxs[callno]->peer)-1);
	/* Choose lowest expirey number */
	if (expire && (expire < iaxs[callno]->expirey)) 
		iaxs[callno]->expirey = expire;
	return 0;
	
}

static int authenticate(char *challenge, char *secret, char *keyn, int authmethods, struct iax_ie_data *ied, struct sockaddr_in *sin)
{
	int res = -1;
	int x;
	if (keyn && strlen(keyn)) {
		if (!(authmethods & IAX_AUTH_RSA)) {
			if (!secret || !strlen(secret)) 
				ast_log(LOG_NOTICE, "Asked to authenticate to %s with an RSA key, but they don't allow RSA authentication\n", inet_ntoa(sin->sin_addr));
		} else if (!strlen(challenge)) {
			ast_log(LOG_NOTICE, "No challenge provided for RSA authentication to %s\n", inet_ntoa(sin->sin_addr));
		} else {
			char sig[256];
			struct ast_key *key;
			key = ast_key_get(keyn, AST_KEY_PRIVATE);
			if (!key) {
				ast_log(LOG_NOTICE, "Unable to find private key '%s'\n", keyn);
			} else {
				if (ast_sign(key, challenge, sig)) {
					ast_log(LOG_NOTICE, "Unable to sign challenge withy key\n");
					res = -1;
				} else {
					iax_ie_append_str(ied, IAX_IE_RSA_RESULT, sig);
					res = 0;
				}
			}
		}
	} 
	/* Fall back */
	if (res && secret && strlen(secret)) {
		if ((authmethods & IAX_AUTH_MD5) && strlen(challenge)) {
			struct MD5Context md5;
			unsigned char digest[16];
			char digres[128] = "";
			MD5Init(&md5);
			MD5Update(&md5, challenge, strlen(challenge));
			MD5Update(&md5, secret, strlen(secret));
			MD5Final(digest, &md5);
			/* If they support md5, authenticate with it.  */
			for (x=0;x<16;x++)
				sprintf(digres + (x << 1),  "%2.2x", digest[x]);
			iax_ie_append_str(ied, IAX_IE_MD5_RESULT, digres);
			res = 0;
		} else if (authmethods & IAX_AUTH_PLAINTEXT) {
			iax_ie_append_str(ied, IAX_IE_PASSWORD, secret);
			res = 0;
		} else
			ast_log(LOG_NOTICE, "No way to send secret to peer '%s' (their methods: %d)\n", inet_ntoa(sin->sin_addr), authmethods);
	}
	return res;
}

static int authenticate_reply(struct chan_iax2_pvt *p, struct sockaddr_in *sin, struct iax_ies *ies, char *override, char *okey)
{
	struct iax2_peer *peer;
	/* Start pessimistic */
	int res = -1;
	int authmethods = 0;
	struct iax_ie_data ied;
	
	memset(&ied, 0, sizeof(ied));
	
	if (ies->username)
		strncpy(p->username, ies->username, sizeof(p->username) - 1);
	if (ies->challenge)
		strncpy(p->challenge, ies->challenge, sizeof(p->challenge)-1);
	if (ies->authmethods)
		authmethods = ies->authmethods;

	/* Check for override RSA authentication first */
	if ((override && strlen(override)) || (okey && strlen(okey))) {
		/* Normal password authentication */
		res = authenticate(p->challenge, override, okey, authmethods, &ied, sin);
	} else {
		ast_pthread_mutex_lock(&peerl.lock);
		peer = peerl.peers;
		while(peer) {
			if ((!strlen(p->peer) || !strcmp(p->peer, peer->name)) 
								/* No peer specified at our end, or this is the peer */
			 && (!strlen(peer->username) || (!strcmp(peer->username, p->username)))
			 					/* No username specified in peer rule, or this is the right username */
			 && (!peer->addr.sin_addr.s_addr || ((sin->sin_addr.s_addr & peer->mask.s_addr) == (peer->addr.sin_addr.s_addr & peer->mask.s_addr)))
			 					/* No specified host, or this is our host */
			) {
				res = authenticate(p->challenge, peer->secret, peer->outkey, authmethods, &ied, sin);
				if (!res)
					break;	
			}
			peer = peer->next;
		}
		ast_pthread_mutex_unlock(&peerl.lock);
	}
	if (!res)
		res = send_command(p, AST_FRAME_IAX, AST_IAX2_COMMAND_AUTHREP, 0, ied.buf, ied.pos, -1);
	return res;
}

static int iax2_do_register(struct iax2_registry *reg);

static int iax2_do_register_s(void *data)
{
	struct iax2_registry *reg = data;
	reg->expire = -1;
	iax2_do_register(reg);
	return 0;
}

static int try_transfer(struct chan_iax2_pvt *pvt, struct iax_ies *ies)
{
	int newcall = 0;
	char newip[256] = "";
	
	struct sockaddr_in new;
	
	if (ies->apparent_addr)
		memcpy(&new, ies->apparent_addr, sizeof(new));
	if (ies->callno)
		newcall = ies->callno;
	if (!newcall || !new.sin_addr.s_addr || !new.sin_port) {
		ast_log(LOG_WARNING, "Invalid transfer request\n");
		return -1;
	}
	pvt->transfercallno = newcall;
	memcpy(&pvt->transfer, &new, sizeof(pvt->transfer));
	inet_aton(newip, &pvt->transfer.sin_addr);
	pvt->transfer.sin_family = AF_INET;
	pvt->transferring = TRANSFER_BEGIN;
	send_command_transfer(pvt, AST_FRAME_IAX, AST_IAX2_COMMAND_TXCNT, 0, NULL, 0);
	return 0; 
}

static int complete_dpreply(struct chan_iax2_pvt *pvt, struct iax_ies *ies)
{
	char exten[256] = "";
	int status = CACHE_FLAG_UNKNOWN;
	int expirey = iaxdefaultdpcache;
	int x;
	int matchmore = 0;
	struct iax2_dpcache *dp, *prev;
	
	if (ies->called_number)
		strncpy(exten, ies->called_number, sizeof(exten)-1);

	if (ies->dpstatus & IAX_DPSTATUS_EXISTS)
		status = CACHE_FLAG_EXISTS;
	else if (ies->dpstatus & IAX_DPSTATUS_CANEXIST)
		status = CACHE_FLAG_CANEXIST;
	else if (ies->dpstatus & IAX_DPSTATUS_NONEXISTANT)
		status = CACHE_FLAG_NONEXISTANT;

	if (ies->dpstatus & IAX_DPSTATUS_IGNOREPAT) {
		/* Don't really do anything with this */
	}
	if (ies->refresh)
		expirey = ies->refresh;
	if (ies->dpstatus & IAX_DPSTATUS_MATCHMORE)
		matchmore = CACHE_FLAG_MATCHMORE;
	ast_pthread_mutex_lock(&dpcache_lock);
	prev = NULL;
	dp = pvt->dpentries;
	while(dp) {
		if (!strcmp(dp->exten, exten)) {
			/* Let them go */
			if (prev)
				prev->peer = dp->peer;
			else
				pvt->dpentries = dp->peer;
			dp->peer = NULL;
			dp->callno = 0;
			dp->expirey.tv_sec = dp->orig.tv_sec + expirey;
			if (dp->flags & CACHE_FLAG_PENDING) {
				dp->flags &= ~CACHE_FLAG_PENDING;
				dp->flags |= status;
				dp->flags |= CACHE_FLAG_MATCHMORE;
			}
			/* Wake up waiters */
			for (x=0;x<sizeof(dp->waiters) / sizeof(dp->waiters[0]); x++)
				if (dp->waiters[x] > -1)
					write(dp->waiters[x], "asdf", 4);
		}
		prev = dp;
		dp = dp->peer;
	}
	ast_pthread_mutex_unlock(&dpcache_lock);
	return 0;
}

static int complete_transfer(int callno, struct iax_ies *ies)
{
	int peercallno = 0;
	struct chan_iax2_pvt *pvt = iaxs[callno];
	struct ast_iax2_frame *cur;

	if (ies->callno)
		peercallno = ies->callno;

	if (peercallno < 1) {
		ast_log(LOG_WARNING, "Invalid transfer request\n");
		return -1;
	}
	memcpy(&pvt->addr, &pvt->transfer, sizeof(pvt->addr));
	memset(&pvt->transfer, 0, sizeof(pvt->transfer));
	/* Reset sequence numbers */
	pvt->oseqno = 0;
	pvt->iseqno = 0;
	pvt->peercallno = peercallno;
	pvt->transferring = TRANSFER_NONE;
	pvt->svoiceformat = -1;
	pvt->voiceformat = 0;
	pvt->transfercallno = -1;
	memset(&pvt->rxcore, 0, sizeof(pvt->rxcore));
	memset(&pvt->offset, 0, sizeof(pvt->offset));
	memset(&pvt->history, 0, sizeof(pvt->history));
	pvt->jitterbuffer = 0;
	pvt->jitter = 0;
	pvt->historicjitter = 0;
	pvt->lag = 0;
	pvt->last = 0;
	pvt->lastsent = 0;
	pvt->pingtime = DEFAULT_RETRY_TIME;
	ast_pthread_mutex_lock(&iaxq.lock);
	for (cur = iaxq.head; cur ; cur = cur->next) {
		/* We must cancel any packets that would have been transmitted
		   because now we're talking to someone new.  It's okay, they
		   were transmitted to someone that didn't care anyway. */
		if (callno == cur->callno) 
			cur->retries = -1;
	}
	ast_pthread_mutex_unlock(&iaxq.lock);
	return 0; 
}

static int iax2_ack_registry(struct iax_ies *ies, struct sockaddr_in *sin, int callno)
{
	struct iax2_registry *reg;
	/* Start pessimistic */
	char peer[256] = "";
	char msgstatus[40] = "";
	int refresh = 0;
	char ourip[256] = "<Unspecified>";
	struct sockaddr_in oldus;
	struct sockaddr_in us;
	int oldmsgs;

	memset(&us, 0, sizeof(us));
	if (ies->apparent_addr)
		memcpy(&us, ies->apparent_addr, sizeof(us));
	if (ies->username)
		strncpy(peer, ies->username, sizeof(peer) - 1);
	if (ies->refresh)
		refresh = ies->refresh;
	if (ies->calling_number) {
		/* We don't do anything with it really, but maybe we should */
	}
	reg = iaxs[callno]->reg;
	if (!reg) {
		ast_log(LOG_WARNING, "Registry acknowledge on unknown registery '%s'\n", peer);
		return -1;
	}
	memcpy(&oldus, &reg->us, sizeof(oldus));
	oldmsgs = reg->messages;
	if (memcmp(&reg->addr, sin, sizeof(&reg->addr))) {
		ast_log(LOG_WARNING, "Received unsolicited registry ack from '%s'\n", inet_ntoa(sin->sin_addr));
		return -1;
	}
	memcpy(&reg->us, &us, sizeof(reg->us));
	reg->messages = ies->msgcount - 1;
	if (refresh && (reg->refresh < refresh)) {
		/* Refresh faster if necessary */
		reg->refresh = refresh;
		if (reg->expire > -1)
			ast_sched_del(sched, reg->expire);
		reg->expire = ast_sched_add(sched, (5 * reg->refresh / 6) * 1000, iax2_do_register_s, reg);
	}
	if ((memcmp(&oldus, &reg->us, sizeof(oldus)) || (reg->messages != oldmsgs)) && (option_verbose > 2)) {
		if (reg->messages > 65534)
			snprintf(msgstatus, sizeof(msgstatus), " with message(s) waiting\n");
		else if (reg->messages > 1)
			snprintf(msgstatus, sizeof(msgstatus), " with %d messages waiting\n", reg->messages);
		else if (reg->messages > 0)
			snprintf(msgstatus, sizeof(msgstatus), " with 1 message waiting\n");
		else if (reg->messages > -1)
			snprintf(msgstatus, sizeof(msgstatus), " with no messages waiting\n");
		snprintf(ourip, sizeof(ourip), "%s:%d", inet_ntoa(reg->us.sin_addr), ntohs(reg->us.sin_port));
		ast_verbose(VERBOSE_PREFIX_3 "Registered to '%s', who sees us as %s%s\n", inet_ntoa(sin->sin_addr), ourip, msgstatus);
	}
	reg->regstate = REG_STATE_REGISTERED;
	return 0;
}

static int iax2_register(char *value, int lineno)
{
	struct iax2_registry *reg;
	char copy[256];
	char *username, *hostname, *secret;
	char *porta;
	char *stringp=NULL;
	
	struct hostent *hp;
	if (!value)
		return -1;
	strncpy(copy, value, sizeof(copy)-1);
	stringp=copy;
	username = strsep(&stringp, "@");
	hostname = strsep(&stringp, "@");
	if (!hostname) {
		ast_log(LOG_WARNING, "Format for registration is user[:secret]@host[:port] at line %d", lineno);
		return -1;
	}
	stringp=username;
	username = strsep(&stringp, ":");
	secret = strsep(&stringp, ":");
	stringp=hostname;
	hostname = strsep(&stringp, ":");
	porta = strsep(&stringp, ":");
	
	if (porta && !atoi(porta)) {
		ast_log(LOG_WARNING, "%s is not a valid port number at line %d\n", porta, lineno);
		return -1;
	}
	hp = gethostbyname(hostname);
	if (!hp) {
		ast_log(LOG_WARNING, "Host '%s' not found at line %d\n", hostname, lineno);
		return -1;
	}
	reg = malloc(sizeof(struct iax2_registry));
	if (reg) {
		memset(reg, 0, sizeof(struct iax2_registry));
		strncpy(reg->username, username, sizeof(reg->username)-1);
		if (secret)
			strncpy(reg->secret, secret, sizeof(reg->secret)-1);
		reg->expire = -1;
		reg->refresh = AST_DEFAULT_REG_EXPIRE;
		reg->addr.sin_family = AF_INET;
		memcpy(&reg->addr.sin_addr, hp->h_addr, sizeof(&reg->addr.sin_addr));
		reg->addr.sin_port = porta ? htons(atoi(porta)) : htons(AST_DEFAULT_IAX_PORTNO);
		reg->next = registrations;
		reg->callno = 0;
		registrations = reg;
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	return 0;
}

static int expire_registry(void *data)
{
	struct iax2_peer *p = data;
	/* Reset the address */
	memset(&p->addr, 0, sizeof(p->addr));
	/* Reset expire notice */
	p->expire = -1;
	/* Reset expirey value */
	p->expirey = expirey;
	if (regfunk)
		regfunk(p->name, 0);
	return 0;
}


static int iax2_poke_peer(struct iax2_peer *peer);


static int update_registry(char *name, struct sockaddr_in *sin, int callno)
{
	/* Called from IAX thread only, with proper iaxsl lock */
	struct iax_ie_data ied;
	struct iax2_peer *p;
	int msgcount;
	memset(&ied, 0, sizeof(ied));
	for (p = peerl.peers;p;p = p->next) {
		if (!strcasecmp(name, p->name)) {
			if (memcmp(&p->addr, sin, sizeof(p->addr))) {
				if (regfunk)
					regfunk(p->name, 1);
				if  (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Registered '%s' (%s) at %s:%d\n", p->name, 
					iaxs[callno]->state & IAX_STATE_AUTHENTICATED ? "AUTHENTICATED" : "UNAUTHENTICATED", inet_ntoa(sin->sin_addr), htons(sin->sin_port));
				iax2_poke_peer(p);
			}		
			/* Update the host */
			memcpy(&p->addr, sin, sizeof(p->addr));
			/* Setup the expirey */
			if (p->expire > -1)
				ast_sched_del(sched, p->expire);
			p->expire = ast_sched_add(sched, p->expirey * 1000, expire_registry, (void *)p);
			iax_ie_append_str(&ied, IAX_IE_USERNAME, p->name);
			iax_ie_append_short(&ied, IAX_IE_REFRESH, p->expirey);
			iax_ie_append_addr(&ied, IAX_IE_APPARENT_ADDR, &p->addr);
			if (strlen(p->mailbox)) {
				msgcount = ast_app_has_voicemail(p->mailbox);
				if (msgcount)
					msgcount = 65535;
				iax_ie_append_short(&ied, IAX_IE_MSGCOUNT, msgcount);
			}
			if (p->hascallerid)
				iax_ie_append_str(&ied, IAX_IE_CALLING_NAME, p->callerid);
			return send_command_final(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REGACK, 0, ied.buf, ied.pos, -1);;
		}
	}
	ast_log(LOG_WARNING, "No such peer '%s'\n", name);
	return -1;
}

static int registry_authrequest(char *name, int callno)
{
	struct iax_ie_data ied;
	struct iax2_peer *p;
	for (p = peerl.peers;p;p = p->next) {
		if (!strcasecmp(name, p->name)) {
			memset(&ied, 0, sizeof(ied));
			iax_ie_append_short(&ied, IAX_IE_AUTHMETHODS, p->authmethods);
			if (p->authmethods & (IAX_AUTH_RSA | IAX_AUTH_MD5)) {
				/* Build the challenge */
				snprintf(p->challenge, sizeof(p->challenge), "%d", rand());
				iax_ie_append_str(&ied, IAX_IE_CHALLENGE, p->challenge);
			}
			iax_ie_append_str(&ied, IAX_IE_USERNAME, name);
			return send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REGAUTH, 0, ied.buf, ied.pos, -1);;
		}
	}
	ast_log(LOG_WARNING, "No such peer '%s'\n", name);
	return 0;
}

static int registry_rerequest(struct iax_ies *ies, int callno, struct sockaddr_in *sin)
{
	struct iax2_registry *reg;
	/* Start pessimistic */
	struct iax_ie_data ied;
	char peer[256] = "";
	char challenge[256] = "";
	int res;
	int authmethods = 0;
	if (ies->authmethods)
		authmethods = ies->authmethods;
	if (ies->username)
		strncpy(peer, ies->username, sizeof(peer) - 1);
	if (ies->challenge)
		strncpy(challenge, ies->challenge, sizeof(challenge) - 1);
	memset(&ied, 0, sizeof(ied));
	reg = iaxs[callno]->reg;
			if (memcmp(&reg->addr, sin, sizeof(&reg->addr))) {
				ast_log(LOG_WARNING, "Received unsolicited registry authenticate request from '%s'\n", inet_ntoa(sin->sin_addr));
				return -1;
			}
			if (!strlen(reg->secret)) {
				ast_log(LOG_NOTICE, "No secret associated with peer '%s'\n", reg->username);
				reg->regstate = REG_STATE_NOAUTH;
				return -1;
			}
			iax_ie_append_str(&ied, IAX_IE_USERNAME, reg->username);
			iax_ie_append_short(&ied, IAX_IE_REFRESH, reg->refresh);
			if (reg->secret[0] == '[') {
				char tmpkey[256];
				strncpy(tmpkey, reg->secret + 1, sizeof(tmpkey) - 1);
				tmpkey[strlen(tmpkey) - 1] = '\0';
				res = authenticate(challenge, NULL, tmpkey, authmethods, &ied, sin);
			} else
				res = authenticate(challenge, reg->secret, NULL, authmethods, &ied, sin);
			if (!res) {
				reg->regstate = REG_STATE_AUTHSENT;
				return send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REGREQ, 0, ied.buf, ied.pos, -1);
			} else
				return -1;
	ast_log(LOG_WARNING, "Registry acknowledge on unknown registery '%s'\n", peer);
	return -1;
}

static int stop_stuff(int callno)
{
		if (iaxs[callno]->lagid > -1)
			ast_sched_del(sched, iaxs[callno]->lagid);
		iaxs[callno]->lagid = -1;
		if (iaxs[callno]->pingid > -1)
			ast_sched_del(sched, iaxs[callno]->pingid);
		iaxs[callno]->pingid = -1;
		if (iaxs[callno]->autoid > -1)
			ast_sched_del(sched, iaxs[callno]->autoid);
		iaxs[callno]->autoid = -1;
		if (iaxs[callno]->initid > -1)
			ast_sched_del(sched, iaxs[callno]->initid);
		iaxs[callno]->initid = -1;
		return 0;
}

static int auto_hangup(void *nothing)
{
	/* Called from IAX thread only, without iaxs lock */
	int callno = (int)(long)(nothing);
	struct iax_ie_data ied;
	ast_pthread_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		iaxs[callno]->autoid = -1;
		memset(&ied, 0, sizeof(ied));
		iax_ie_append_str(&ied, IAX_IE_CAUSE, "Timeout");
		send_command_final(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_HANGUP, 0, ied.buf, ied.pos, -1);
	}
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	return 0;
}

static void iax2_dprequest(struct iax2_dpcache *dp, int callno)
{
	struct iax_ie_data ied;
	/* Auto-hangup with 30 seconds of inactivity */
	if (iaxs[callno]->autoid > -1)
		ast_sched_del(sched, iaxs[callno]->autoid);
	iaxs[callno]->autoid = ast_sched_add(sched, 30000, auto_hangup, (void *)callno);
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, dp->exten);
	send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_DPREQ, 0, ied.buf, ied.pos, -1);
	dp->flags |= CACHE_FLAG_TRANSMITTED;
}

static int iax2_vnak(int callno)
{
	return send_command_immediate(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_VNAK, 0, NULL, 0, iaxs[callno]->iseqno);
}

static void vnak_retransmit(int callno, int last)
{
	struct ast_iax2_frame *f;
	ast_pthread_mutex_lock(&iaxq.lock);
	f = iaxq.head;
	while(f) {
		/* Send a copy immediately */
		if ((f->callno == callno) && iaxs[f->callno] &&
			(f->oseqno >= last)) {
			send_packet(f);
		}
		f = f->next;
	}
	ast_pthread_mutex_unlock(&iaxq.lock);
}

static int iax2_poke_peer_s(void *data)
{
	struct iax2_peer *peer = data;
	peer->pokeexpire = -1;
	iax2_poke_peer(peer);
	return 0;
}

static int parse_ies(struct iax_ies *ies, unsigned char *data, int datalen)
{
	/* Parse data into information elements */
	int len;
	int ie;
	memset(ies, 0, sizeof(struct iax_ies));
	while(datalen >= 2) {
		ie = data[0];
		len = data[1];
		if (len > datalen - 2) {
			ast_log(LOG_WARNING, "Information element length exceeds message size\n");
			return -1;
		}
		switch(ie) {
		case IAX_IE_CALLED_NUMBER:
			ies->called_number = data + 2;
			break;
		case IAX_IE_CALLING_NUMBER:
			ies->calling_number = data + 2;
			break;
		case IAX_IE_CALLING_ANI:
			ies->calling_ani = data + 2;
			break;
		case IAX_IE_CALLING_NAME:
			ies->calling_name = data + 2;
			break;
		case IAX_IE_CALLED_CONTEXT:
			ies->called_context = data + 2;
			break;
		case IAX_IE_USERNAME:
			ies->username = data + 2;
			break;
		case IAX_IE_PASSWORD:
			ies->password = data + 2;
			break;
		case IAX_IE_CAPABILITY:
			if (len != sizeof(unsigned int)) 
				ast_log(LOG_WARNING, "Expecting capability to be %d bytes long but was %d\n", sizeof(unsigned int), len);
			else
				ies->capability = ntohl(*((unsigned int *)(data + 2)));
			break;
		case IAX_IE_FORMAT:
			if (len != sizeof(unsigned int)) 
				ast_log(LOG_WARNING, "Expecting format to be %d bytes long but was %d\n", sizeof(unsigned int), len);
			else
				ies->format = ntohl(*((unsigned int *)(data + 2)));
			break;
		case IAX_IE_LANGUAGE:
			ies->language = data + 2;
			break;
		case IAX_IE_VERSION:
			if (len != sizeof(unsigned short)) 
				ast_log(LOG_WARNING, "Expecting version to be %d bytes long but was %d\n", sizeof(unsigned short), len);
			else
				ies->version = ntohs(*((unsigned short *)(data + 2)));
			break;
		case IAX_IE_ADSICPE:
			if (len != sizeof(unsigned short)) 
				ast_log(LOG_WARNING, "Expecting adsicpe to be %d bytes long but was %d\n", sizeof(unsigned short), len);
			else
				ies->adsicpe = ntohs(*((unsigned short *)(data + 2)));
			break;
		case IAX_IE_DNID:
			ies->dnid = data + 2;
			break;
		case IAX_IE_AUTHMETHODS:
			if (len != sizeof(unsigned short)) 
				ast_log(LOG_WARNING, "Expecting authmethods to be %d bytes long but was %d\n", sizeof(unsigned short), len);
			else
				ies->authmethods = ntohs(*((unsigned short *)(data + 2)));
			break;
		case IAX_IE_CHALLENGE:
			ies->challenge = data + 2;
			break;
		case IAX_IE_MD5_RESULT:
			ies->md5_result = data + 2;
			break;
		case IAX_IE_RSA_RESULT:
			ies->rsa_result = data + 2;
			break;
		case IAX_IE_APPARENT_ADDR:
			ies->apparent_addr = ((struct sockaddr_in *)(data + 2));
			break;
		case IAX_IE_REFRESH:
			if (len != sizeof(unsigned short)) 
				ast_log(LOG_WARNING, "Expecting refresh to be %d bytes long but was %d\n", sizeof(unsigned short), len);
			else
				ies->refresh = ntohs(*((unsigned short *)(data + 2)));
			break;
		case IAX_IE_DPSTATUS:
			if (len != sizeof(unsigned short)) 
				ast_log(LOG_WARNING, "Expecting dpstatus to be %d bytes long but was %d\n", sizeof(unsigned short), len);
			else
				ies->dpstatus = ntohs(*((unsigned short *)(data + 2)));
			break;
		case IAX_IE_CALLNO:
			if (len != sizeof(unsigned short)) 
				ast_log(LOG_WARNING, "Expecting callno to be %d bytes long but was %d\n", sizeof(unsigned short), len);
			else
				ies->callno = ntohs(*((unsigned short *)(data + 2)));
			break;
		case IAX_IE_CAUSE:
			ies->cause = data + 2;
			break;
		case IAX_IE_IAX_UNKNOWN:
			if (len == 1)
				ies->iax_unknown = data[2];
			else
				ast_log(LOG_WARNING, "Expected single byte Unknown command, but was %d long\n", len);
			break;
		case IAX_IE_MSGCOUNT:
			if (len != sizeof(unsigned short)) 
				ast_log(LOG_WARNING, "Expecting msgcount to be %d bytes long but was %d\n", sizeof(unsigned short), len);
			else
				ies->msgcount = ntohs(*((unsigned short *)(data + 2))) + 1;	/* Add 1 to know if we got it */
			break;
		default:
			ast_log(LOG_NOTICE, "Ignoring unknown information element '%s' (%d) of length %d\n", ie2str(ie), ie, len);
		}
		/* Overwrite information element with 0, to null terminate previous portion */
		data[0] = 0;
		datalen -= (len + 2);
		data += (len + 2);
	}
	/* Null-terminate last field */
	*data = '\0';
	if (datalen) {
		ast_log(LOG_WARNING, "Invalid information element contents, strange boundary\n");
		return -1;
	}
	return 0;
}

static int send_trunk(struct iax2_peer *peer)
{
	int x;
	int calls = 0;
	int res = 0;
	int firstcall = 0;
	unsigned char buf[65536 + sizeof(struct ast_iax2_frame)], *ptr;
	int len = 65536;
	struct ast_iax2_frame *fr;
	struct ast_iax2_meta_hdr *meta;
	struct ast_iax2_meta_trunk_hdr *mth;
	struct ast_iax2_meta_trunk_entry *met;
	
	/* Point to frame */
	fr = (struct ast_iax2_frame *)buf;
	/* Point to meta data */
	meta = (struct ast_iax2_meta_hdr *)fr->afdata;
	mth = (struct ast_iax2_meta_trunk_hdr *)meta->data;
	/* Point past meta data for first meta trunk entry */
	ptr = fr->afdata + sizeof(struct ast_iax2_meta_hdr) + sizeof(struct ast_iax2_meta_trunk_hdr);
	len -= sizeof(struct ast_iax2_meta_hdr) + sizeof(struct ast_iax2_meta_trunk_hdr);
	
	/* Search through trunked calls for a match with this peer */
	for (x=TRUNK_CALL_START;x<maxtrunkcall; x++) {
		ast_pthread_mutex_lock(&iaxsl[x]);
		if (iaxs[x] && iaxs[x]->trunk && iaxs[x]->trunkdatalen && !memcmp(&iaxs[x]->addr, &peer->addr, sizeof(iaxs[x]->addr))) {
			if (len >= iaxs[x]->trunkdatalen + sizeof(struct ast_iax2_meta_trunk_entry)) {
				met = (struct ast_iax2_meta_trunk_entry *)ptr;
				/* Store call number and length in meta header */
				met->callno = htons(x);
				met->len = htons(iaxs[x]->trunkdatalen);
				/* Advance pointers/decrease length past trunk entry header */
				ptr += sizeof(struct ast_iax2_meta_trunk_entry);
				len -= sizeof(struct ast_iax2_meta_trunk_entry);
				/* Copy actual trunk data */
				memcpy(ptr, iaxs[x]->trunkdata, iaxs[x]->trunkdatalen);
				/* Advance pointeres/decrease length for actual data */
				ptr += iaxs[x]->trunkdatalen;
				len -= iaxs[x]->trunkdatalen;
			} else 
				ast_log(LOG_WARNING, "Out of space in frame for trunking call %d\n", x);
			iaxs[x]->trunkdatalen = 0;
			calls++;
			if (!firstcall)
				firstcall = x;
		}
		ast_pthread_mutex_unlock(&iaxsl[x]);
	}
	if (calls) {
		/* We're actually sending a frame, so fill the meta trunk header and meta header */
		meta->zeros = 0;
		meta->metacmd = IAX_META_TRUNK;
		meta->cmddata = 0;
		mth->ts = htonl(calc_txpeerstamp(peer));
		/* And the rest of the ast_iax2 header */
		fr->direction = DIRECTION_OUTGRESS;
		fr->retrans = -1;
		fr->transfer = 0;
		/* Any appropriate call will do */
		fr->callno = firstcall;
		fr->data = fr->afdata;
		fr->datalen = 65536 - len;
#if 0
		ast_log(LOG_DEBUG, "Trunking %d calls in %d bytes, ts=%d\n", calls, fr->datalen, ntohl(mth->ts));
#endif		
		res = send_packet(fr);
	}
	return res;
}

static int timing_read(int *id, int fd, short events, void *cbdata)
{
	char buf[1024];
	int res;
	struct iax2_peer *peer;
	/* Read and ignore from the pseudo channel for timing */
	res = read(fd, buf, sizeof(buf));
	if (res > 0) {
		/* For each peer that supports trunking... */
		ast_pthread_mutex_lock(&peerl.lock);
		peer = peerl.peers;
		while(peer) {
			if (peer->trunk) {
				send_trunk(peer);
			}
			peer = peer->next;
		}
		ast_pthread_mutex_unlock(&peerl.lock);
	}
	return 1;
}

static int socket_read(int *id, int fd, short events, void *cbdata)
{
	struct sockaddr_in sin;
	int res;
	int updatehistory=1;
	int new = NEW_PREVENT;
	char buf[4096], *ptr;
	int len = sizeof(sin);
	int dcallno = 0;
	struct ast_iax2_full_hdr *fh = (struct ast_iax2_full_hdr *)buf;
	struct ast_iax2_mini_hdr *mh = (struct ast_iax2_mini_hdr *)buf;
	struct ast_iax2_meta_hdr *meta = (struct ast_iax2_meta_hdr *)buf;
	struct ast_iax2_meta_trunk_hdr *mth;
	struct ast_iax2_meta_trunk_entry *mte;
	char dblbuf[4096];	/* Declaration of dblbuf must immediately *preceed* fr  on the stack */
	struct ast_iax2_frame fr;
	struct ast_iax2_frame *cur;
	struct ast_frame f;
	struct ast_channel *c;
	struct iax2_dpcache *dp;
	struct iax2_peer *peer;
	struct iax_ies ies;
	struct iax_ie_data ied0, ied1;
	int format;
	int exists;
	int mm;
	unsigned int ts;
	char empty[32]="";		/* Safety measure */
	dblbuf[0] = 0;	/* Keep GCC from whining */
	res = recvfrom(netsocket, buf, sizeof(buf), 0,(struct sockaddr *) &sin, &len);
	if (res < 0) {
		if (errno != ECONNREFUSED)
			ast_log(LOG_WARNING, "Error: %s\n", strerror(errno));
		handle_error();
		return 1;
	}
	if (res < sizeof(struct ast_iax2_mini_hdr)) {
		ast_log(LOG_WARNING, "midget packet received (%d of %d min)\n", res, sizeof(struct ast_iax2_mini_hdr));
		return 1;
	}
	if (meta->zeros == 0) {
		/* This is a a meta header */
		switch(meta->metacmd) {
		case IAX_META_TRUNK:
			if (res < sizeof(struct ast_iax2_meta_hdr) + sizeof(struct ast_iax2_meta_trunk_hdr)) {
				ast_log(LOG_WARNING, "midget meta trunk packet received (%d of %d min)\n", res, sizeof(struct ast_iax2_mini_hdr));
				return 1;
			}
			mth = (struct ast_iax2_meta_trunk_hdr *)(meta->data);
			ts = ntohl(mth->ts);
			res -= (sizeof(struct ast_iax2_meta_hdr) + sizeof(struct ast_iax2_meta_trunk_hdr));
			ptr = mth->data;
			ast_pthread_mutex_lock(&peerl.lock);
			peer = peerl.peers;
			while(peer) {
				if (!memcmp(&peer->addr, &sin, sizeof(peer->addr)))
					break;
				peer = peer->next;
			}
			ast_pthread_mutex_unlock(&peerl.lock);
			if (!peer) {
				ast_log(LOG_WARNING, "Unable to accept trunked packet from '%s:%d': No matching peer\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
				return 1;
			}
			if (!ts || (!peer->rxtrunktime.tv_sec && !peer->rxtrunktime.tv_usec)) {
				gettimeofday(&peer->rxtrunktime, NULL);
			}
			while(res >= sizeof(struct ast_iax2_meta_trunk_entry)) {
				/* Process channels */
				mte = (struct ast_iax2_meta_trunk_entry *)ptr;
				ptr += sizeof(struct ast_iax2_meta_trunk_entry);
				res -= sizeof(struct ast_iax2_meta_trunk_entry);
				len = ntohs(mte->len);
				/* Stop if we don't have enough data */
				if (len > res)
					break;
				fr.callno = find_callno(ntohs(mte->callno) & ~AST_FLAG_FULL, 0, &sin, NEW_PREVENT);
				if (fr.callno) {
					ast_pthread_mutex_lock(&iaxsl[fr.callno]);
					/* If it's a valid call, deliver the contents.  If not, we
					   drop it, since we don't have a scallno to use for an INVAL */
					/* Process as a mini frame */
					f.frametype = AST_FRAME_VOICE;
					if (iaxs[fr.callno]->voiceformat > 0) {
						f.subclass = iaxs[fr.callno]->voiceformat;
						f.datalen = len;
						if (f.datalen >= 0) {
							if (f.datalen)
								f.data = ptr;
							else
								f.data = NULL;
							fr.ts = fix_peerts(peer, fr.callno, ts);
							/* Don't pass any packets until we're started */
							if ((iaxs[fr.callno]->state & IAX_STATE_STARTED)) {
								/* Common things */
								f.src = "IAX2";
								f.mallocd = 0;
								f.offset = 0;
								if (f.datalen && (f.frametype == AST_FRAME_VOICE)) 
									f.samples = get_samples(&f);
								else
									f.samples = 0;
								fr.outoforder = 0;
								ast_iax2_frame_wrap(&fr, &f);
#ifdef BRIDGE_OPTIMIZATION
								if (iaxs[fr.callno]->bridgecallno) {
									forward_delivery(&fr);
								} else {
									schedule_delivery(iaxfrdup2(&fr), 1, updatehistory);
								}
#else
								schedule_delivery(iaxfrdup2(&fr), 1, updatehistory);
#endif
							}
						} else {
							ast_log(LOG_WARNING, "Datalen < 0?\n");
						}
					} else {
						ast_log(LOG_WARNING, "Received trunked frame before first full voice frame\n ");
						iax2_vnak(fr.callno);
					}
					ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
				}
				ptr += len;
				res -= len;
			}
			
		}
		return 1;
	}
#ifdef DEBUG_SUPPORT
	if (iaxdebug)
		showframe(NULL, fh, 1, &sin, res - sizeof(struct ast_iax2_full_hdr));
#endif
	if (ntohs(mh->callno) & AST_FLAG_FULL) {
		/* Get the destination call number */
		dcallno = ntohs(fh->dcallno) & ~AST_FLAG_RETRANS;
		/* Retrieve the type and subclass */
		f.frametype = fh->type;
		f.subclass = uncompress_subclass(fh->csub);
#if 0
		f.subclass = fh->subclasshigh << 16;
		f.subclass += ntohs(fh->subclasslow);
#endif
		if ((f.frametype == AST_FRAME_IAX) && ((f.subclass == AST_IAX2_COMMAND_NEW) || (f.subclass == AST_IAX2_COMMAND_REGREQ)
				|| (f.subclass == AST_IAX2_COMMAND_POKE)))
			new = NEW_ALLOW;
	} else {
		/* Don't knwo anything about it yet */
		f.frametype = AST_FRAME_NULL;
		f.subclass = 0;
	}

	fr.callno = find_callno(ntohs(mh->callno) & ~AST_FLAG_FULL, dcallno, &sin, new);

	if (fr.callno > 0) 
		ast_pthread_mutex_lock(&iaxsl[fr.callno]);

	if (!fr.callno || !iaxs[fr.callno]) {
		/* A call arrived for a non-existant destination.  Unless it's an "inval"
		   frame, reply with an inval */
		if (ntohs(mh->callno) & AST_FLAG_FULL) {
			/* We can only raw hangup control frames */
			if (((f.subclass != AST_IAX2_COMMAND_INVAL) &&
				 (f.subclass != AST_IAX2_COMMAND_TXCNT) &&
				 (f.subclass != AST_IAX2_COMMAND_TXACC))||
			    (f.frametype != AST_FRAME_IAX))
				raw_hangup(&sin, ntohs(fh->dcallno) & ~AST_FLAG_RETRANS, ntohs(mh->callno) & ~AST_FLAG_FULL
				);
		}
		if (fr.callno > 0) 
			ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
		return 1;
	}
	if (((f.subclass != AST_IAX2_COMMAND_TXCNT) &&
	     (f.subclass != AST_IAX2_COMMAND_TXACC)) || (f.frametype != AST_FRAME_IAX))
		iaxs[fr.callno]->peercallno = (short)(ntohs(mh->callno) & ~AST_FLAG_FULL);
	if (ntohs(mh->callno) & AST_FLAG_FULL) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Received packet %d, (%d, %d)\n", fh->oseqno, f.frametype, f.subclass);
		/* Check if it's out of order (and not an ACK or INVAL) */
		fr.oseqno = fh->oseqno;
		fr.iseqno = fh->iseqno;
		fr.ts = ntohl(fh->ts);
		if (ntohs(fh->dcallno) & AST_FLAG_RETRANS)
			updatehistory = 0;
		if ((iaxs[fr.callno]->iseqno != fr.oseqno) &&
			(iaxs[fr.callno]->iseqno ||
				((f.subclass != AST_IAX2_COMMAND_TXCNT) &&
				(f.subclass != AST_IAX2_COMMAND_TXACC)) ||
				(f.subclass != AST_FRAME_IAX))) {
			if (
			 ((f.subclass != AST_IAX2_COMMAND_ACK) &&
			  (f.subclass != AST_IAX2_COMMAND_INVAL) &&
			  (f.subclass != AST_IAX2_COMMAND_TXCNT) &&
			  (f.subclass != AST_IAX2_COMMAND_TXACC) &&
			  (f.subclass != AST_IAX2_COMMAND_VNAK)) ||
			  (f.frametype != AST_FRAME_IAX)) {
			 	/* If it's not an ACK packet, it's out of order. */
				if (option_debug)
					ast_log(LOG_DEBUG, "Packet arrived out of order (expecting %d, got %d) (frametype = %d, subclass = %d)\n", 
					iaxs[fr.callno]->iseqno, fr.oseqno, f.frametype, f.subclass);
				if (iaxs[fr.callno]->iseqno > fr.oseqno) {
					/* If we've already seen it, ack it XXX There's a border condition here XXX */
					if ((f.frametype != AST_FRAME_IAX) || 
							((f.subclass != AST_IAX2_COMMAND_ACK) && (f.subclass != AST_IAX2_COMMAND_INVAL))) {
						if (option_debug)
							ast_log(LOG_DEBUG, "Acking anyway\n");
						/* XXX Maybe we should handle its ack to us, but then again, it's probably outdated anyway, and if
						   we have anything to send, we'll retransmit and get an ACK back anyway XXX */
						send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
					}
				} else {
					/* Send a VNAK requesting retransmission */
					iax2_vnak(fr.callno);
				}
				ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
				return 1;
			}
		} else {
			/* Increment unless it's an ACK or VNAK */
			if (((f.subclass != AST_IAX2_COMMAND_ACK) &&
			    (f.subclass != AST_IAX2_COMMAND_INVAL) &&
			    (f.subclass != AST_IAX2_COMMAND_TXCNT) &&
			    (f.subclass != AST_IAX2_COMMAND_TXACC) &&
				(f.subclass != AST_IAX2_COMMAND_VNAK)) ||
			    (f.frametype != AST_FRAME_IAX))
				iaxs[fr.callno]->iseqno++;
		}
		/* A full frame */
		if (res < sizeof(struct ast_iax2_full_hdr)) {
			ast_log(LOG_WARNING, "midget packet received (%d of %d min)\n", res, sizeof(struct ast_iax2_full_hdr));
			ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
			return 1;
		}
		f.datalen = res - sizeof(struct ast_iax2_full_hdr);

		/* Handle implicit ACKing unless this is an INVAL */
		if (((f.subclass != AST_IAX2_COMMAND_INVAL)) ||
			(f.frametype != AST_FRAME_IAX)) {
			unsigned char x;
			/* XXX This code is not very efficient.  Surely there is a better way which still
			       properly handles boundary conditions? XXX */
			/* First we have to qualify that the ACKed value is within our window */
			for (x=iaxs[fr.callno]->rseqno; x != iaxs[fr.callno]->oseqno; x++)
				if (fr.iseqno == x)
					break;
			if ((x != iaxs[fr.callno]->oseqno) || (iaxs[fr.callno]->oseqno == fr.iseqno)) {
				/* The acknowledgement is within our window.  Time to acknowledge everything
				   that it says to */
				for (x=iaxs[fr.callno]->rseqno; x != fr.iseqno; x++) {
					/* Ack the packet with the given timestamp */
					if (option_debug)
						ast_log(LOG_DEBUG, "Cancelling transmission of packet %d\n", x);
					ast_pthread_mutex_lock(&iaxq.lock);
					for (cur = iaxq.head; cur ; cur = cur->next) {
						/* If it's our call, and our timestamp, mark -1 retries */
						if ((fr.callno == cur->callno) && (x == cur->oseqno)) {
							cur->retries = -1;
							/* Destroy call if this is the end */
							if (cur->final) { 
								if (option_debug)
									ast_log(LOG_DEBUG, "Really destroying %d, having been acked on final message\n", fr.callno);
								iax2_destroy_nolock(fr.callno);
							}
						}
					}
					ast_pthread_mutex_unlock(&iaxq.lock);
				}
				/* Note how much we've received acknowledgement for */
				if (iaxs[fr.callno])
					iaxs[fr.callno]->rseqno = fr.iseqno;
				else {
					/* Stop processing now */
					ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
					return 1;
				}
			} else
				ast_log(LOG_DEBUG, "Received iseqno %d not within window %d->%d\n", fr.iseqno, iaxs[fr.callno]->rseqno, iaxs[fr.callno]->oseqno);
		}

		if (f.datalen) {
			if (f.frametype == AST_FRAME_IAX) {
				if (parse_ies(&ies, buf + sizeof(struct ast_iax2_full_hdr), f.datalen)) {
					ast_log(LOG_WARNING, "undecodable frame received\n");
					ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
					return 1;
				}
				f.data = NULL;
			} else
				f.data = buf + sizeof(struct ast_iax2_full_hdr);
		} else {
			if (f.frametype == AST_FRAME_IAX)
				f.data = NULL;
			else
				f.data = empty;
			memset(&ies, 0, sizeof(ies));
		}
		if (f.frametype == AST_FRAME_VOICE) {
			if (f.subclass != iaxs[fr.callno]->voiceformat) {
					iaxs[fr.callno]->voiceformat = f.subclass;
					ast_log(LOG_DEBUG, "Ooh, voice format changed to %d\n", f.subclass);
					if (iaxs[fr.callno]->owner) {
						int orignative;
						ast_pthread_mutex_lock(&iaxs[fr.callno]->owner->lock);
						orignative = iaxs[fr.callno]->owner->nativeformats;
						iaxs[fr.callno]->owner->nativeformats = f.subclass;
						if (iaxs[fr.callno]->owner->readformat)
							ast_set_read_format(iaxs[fr.callno]->owner, iaxs[fr.callno]->owner->readformat);
						iaxs[fr.callno]->owner->nativeformats = orignative;
						ast_pthread_mutex_unlock(&iaxs[fr.callno]->owner->lock);
					}
			}
		}
		if (f.frametype == AST_FRAME_IAX) {
			if (iaxs[fr.callno]->initid > -1) {
				/* Don't auto congest anymore since we've gotten something usefulb ack */
				ast_sched_del(sched, iaxs[fr.callno]->initid);
				iaxs[fr.callno]->initid = -1;
			}
			/* Handle the IAX pseudo frame itself */
			if (option_debug)
				ast_log(LOG_DEBUG, "IAX subclass %d received\n", f.subclass);
			/* Go through the motions of delivering the packet without actually doing so,
			   unless this is a lag request since it will be done for real */
			if (f.subclass != AST_IAX2_COMMAND_LAGRQ)
				schedule_delivery(&fr, 0, updatehistory);
			switch(f.subclass) {
			case AST_IAX2_COMMAND_ACK:
				/* Do nothing */
				break;
			case AST_IAX2_COMMAND_QUELCH:
				if (iaxs[fr.callno]->state & IAX_STATE_STARTED)
					iaxs[fr.callno]->quelch = 1;
				break;
			case AST_IAX2_COMMAND_UNQUELCH:
				if (iaxs[fr.callno]->state & IAX_STATE_STARTED)
					iaxs[fr.callno]->quelch = 0;
				break;
			case AST_IAX2_COMMAND_TXACC:
				if (iaxs[fr.callno]->transferring == TRANSFER_BEGIN) {
					/* Ack the packet with the given timestamp */
					ast_pthread_mutex_lock(&iaxq.lock);
					for (cur = iaxq.head; cur ; cur = cur->next) {
						/* Cancel any outstanding txcnt's */
						if ((fr.callno == cur->callno) && (cur->transfer))
							cur->retries = -1;
					}
					ast_pthread_mutex_unlock(&iaxq.lock);
					memset(&ied1, 0, sizeof(ied1));
					iax_ie_append_short(&ied1, IAX_IE_CALLNO, iaxs[fr.callno]->callno);
					send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_TXREADY, 0, ied1.buf, ied1.pos, -1);
					iaxs[fr.callno]->transferring = TRANSFER_READY;
				}
				break;
			case AST_IAX2_COMMAND_NEW:
				/* Ignore if it's already up */
				if (iaxs[fr.callno]->state & (IAX_STATE_STARTED | IAX_STATE_TBD))
					break;
				if (check_access(fr.callno, &sin, &ies)) {
					/* They're not allowed on */
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No authority found");
					send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
					ast_log(LOG_NOTICE, "Rejected connect attempt from %s\n", inet_ntoa(sin.sin_addr));
					break;
				}
				/* If we're in trunk mode, do it now, and update the trunk number in our frame before continuing */
				if (iaxs[fr.callno]->trunk) {
					fr.callno = make_trunk(fr.callno, 1);
				}
				/* This might re-enter the IAX code and need the lock */
				exists = ast_exists_extension(NULL, iaxs[fr.callno]->context, iaxs[fr.callno]->exten, 1, iaxs[fr.callno]->callerid);
				if (!strlen(iaxs[fr.callno]->secret) && !strlen(iaxs[fr.callno]->inkeys)) {
					if (strcmp(iaxs[fr.callno]->exten, "TBD") && !exists) {
						memset(&ied0, 0, sizeof(ied0));
						iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No such context/extension");
						send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
						ast_log(LOG_NOTICE, "Rejected connect attempt from %s, request '%s@%s' does not exist\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->exten, iaxs[fr.callno]->context);
					} else {
						/* Select an appropriate format */
						format = iaxs[fr.callno]->peerformat & iax2_capability;
						if (!format) {
							format = iaxs[fr.callno]->peercapability & iax2_capability;
							if (!format) {
								memset(&ied0, 0, sizeof(ied0));
								iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
								send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
								ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability 0x%x/0x%x incompatible  with our capability 0x%x.\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat, iaxs[fr.callno]->peercapability, iax2_capability);
							} else {
								/* Pick one... */
								format = ast_best_codec(iaxs[fr.callno]->peercapability & iax2_capability);
								if (!format) {
									memset(&ied0, 0, sizeof(ied0));
									iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
									ast_log(LOG_ERROR, "No best format in 0x%x???\n", iaxs[fr.callno]->peercapability & iax2_capability);
									send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
									ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability 0x%x/0x%x incompatible  with our capability 0x%x.\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat, iaxs[fr.callno]->peercapability, iax2_capability);
									iaxs[fr.callno]->alreadygone = 1;
									break;
								}
							}
						}
						if (format) {
							/* No authentication required, let them in */
							memset(&ied1, 0, sizeof(ied1));
							iax_ie_append_int(&ied1, IAX_IE_FORMAT, format);
							send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACCEPT, 0, ied1.buf, ied1.pos, -1);
							if (strcmp(iaxs[fr.callno]->exten, "TBD")) {
								iaxs[fr.callno]->state |= IAX_STATE_STARTED;
								if (option_verbose > 2) 
									ast_verbose(VERBOSE_PREFIX_3 "Accepting unauthenticated call from %s, requested format = %d, actual format = %d\n", 
										inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat,format);
								if(!(c = ast_iax2_new(iaxs[fr.callno], AST_STATE_RING, format)))
									iax2_destroy_nolock(fr.callno);
							} else {
								iaxs[fr.callno]->state |= IAX_STATE_TBD;
								/* If this is a TBD call, we're ready but now what...  */
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Accepted unauthenticated TBD call from %s\n", inet_ntoa(sin.sin_addr));
							}
						}
					}
					break;
				}
				authenticate_request(iaxs[fr.callno]);
				iaxs[fr.callno]->state |= IAX_STATE_AUTHENTICATED;
				break;
			case AST_IAX2_COMMAND_DPREQ:
				/* Request status in the dialplan */
				if ((iaxs[fr.callno]->state & IAX_STATE_TBD) && 
					!(iaxs[fr.callno]->state & IAX_STATE_STARTED) && ies.called_number) {
					unsigned short dpstatus = 0;
					memset(&ied1, 0, sizeof(ied1));
					mm = ast_matchmore_extension(NULL, iaxs[fr.callno]->context, ies.called_number, 1, iaxs[fr.callno]->callerid);
					/* Must be started */
					if (ast_exists_extension(NULL, iaxs[fr.callno]->context, ies.called_number, 1, iaxs[fr.callno]->callerid)) {
						dpstatus = IAX_DPSTATUS_EXISTS;
					} else if (ast_canmatch_extension(NULL, iaxs[fr.callno]->context, ies.called_number, 1, iaxs[fr.callno]->callerid)) {
						dpstatus = IAX_DPSTATUS_CANEXIST;
					} else {
						dpstatus = IAX_DPSTATUS_NONEXISTANT;
					}
					if (ast_ignore_pattern(iaxs[fr.callno]->context, ies.called_number))
						dpstatus |= IAX_DPSTATUS_IGNOREPAT;
					if (mm)
						dpstatus |= IAX_DPSTATUS_MATCHMORE;
					iax_ie_append_str(&ied1, IAX_IE_CALLED_NUMBER, ies.called_number);
					iax_ie_append_short(&ied1, IAX_IE_DPSTATUS, dpstatus);
					iax_ie_append_short(&ied1, IAX_IE_REFRESH, iaxdefaultdpcache);
					send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_DPREP, 0, ied1.buf, ied1.pos, -1);
				}
				break;
			case AST_IAX2_COMMAND_HANGUP:
				iaxs[fr.callno]->alreadygone = 1;
				ast_log(LOG_DEBUG, "Immediately destroying %d, having received hangup\n", fr.callno);
				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
				iax2_destroy_nolock(fr.callno);
				break;
			case AST_IAX2_COMMAND_REJECT:
				if (iaxs[fr.callno]->owner)
					ast_log(LOG_WARNING, "Call rejected by %s: %s\n", inet_ntoa(iaxs[fr.callno]->addr.sin_addr), ies.cause ? ies.cause : "<Unknown>");
				iaxs[fr.callno]->error = EPERM;
				ast_log(LOG_DEBUG, "Immediately destroying %d, having received reject\n", fr.callno);
				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
				iax2_destroy_nolock(fr.callno);
				break;
			case AST_IAX2_COMMAND_ACCEPT:
				/* Ignore if call is already up or needs authentication or is a TBD */
				if (iaxs[fr.callno]->state & (IAX_STATE_STARTED | IAX_STATE_TBD | IAX_STATE_AUTHENTICATED))
					break;
				if (ies.format) {
					iaxs[fr.callno]->peerformat = ies.format;
				} else {
					if (iaxs[fr.callno]->owner)
						iaxs[fr.callno]->peerformat = iaxs[fr.callno]->owner->nativeformats;
					else
						iaxs[fr.callno]->peerformat = iax2_capability;
				}
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Call accepted by %s (format %d)\n", inet_ntoa(iaxs[fr.callno]->addr.sin_addr), iaxs[fr.callno]->peerformat);
				if (!(iaxs[fr.callno]->peerformat & iaxs[fr.callno]->capability)) {
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
					send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
					ast_log(LOG_NOTICE, "Rejected call to %s, format 0x%x incompatible with our capability 0x%x.\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat, iaxs[fr.callno]->capability);
				} else {
					iaxs[fr.callno]->state |= IAX_STATE_STARTED;
					if (iaxs[fr.callno]->owner) {
						/* Switch us to use a compatible format */
						iaxs[fr.callno]->owner->nativeformats = iaxs[fr.callno]->peerformat;
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Format for call is %d\n", iaxs[fr.callno]->owner->nativeformats);
						/* Setup read/write formats properly. */
						if (iaxs[fr.callno]->owner->writeformat)
							ast_set_write_format(iaxs[fr.callno]->owner, iaxs[fr.callno]->owner->writeformat);	
						if (iaxs[fr.callno]->owner->readformat)
							ast_set_read_format(iaxs[fr.callno]->owner, iaxs[fr.callno]->owner->readformat);	
					}
				}
				ast_pthread_mutex_lock(&dpcache_lock);
				dp = iaxs[fr.callno]->dpentries;
				while(dp) {
					if (!(dp->flags & CACHE_FLAG_TRANSMITTED)) {
						iax2_dprequest(dp, fr.callno);
					}
					dp = dp->peer;
				}
				ast_pthread_mutex_unlock(&dpcache_lock);
				break;
			case AST_IAX2_COMMAND_POKE:
				/* Send back a pong packet with the original timestamp */
				send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_PONG, fr.ts, NULL, 0, -1);
				break;
			case AST_IAX2_COMMAND_PING:
#ifdef BRIDGE_OPTIMIZATION
				if (iaxs[fr.callno]->bridgecallno) {
					/* If we're in a bridged call, just forward this */
					forward_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_PING, fr.ts, NULL, 0, -1);
				} else {
					/* Send back a pong packet with the original timestamp */
					send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_PONG, fr.ts, NULL, 0, -1);
				}
#else				
				/* Send back a pong packet with the original timestamp */
				send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_PONG, fr.ts, NULL, 0, -1);
#endif			
				break;
			case AST_IAX2_COMMAND_PONG:
#ifdef BRIDGE_OPTIMIZATION
				if (iaxs[fr.callno]->bridgecallno) {
					/* Forward to the other side of the bridge */
					forward_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_PONG, fr.ts, NULL, 0, -1);
				} else {
					/* Calculate ping time */
					iaxs[fr.callno]->pingtime =  calc_timestamp(iaxs[fr.callno], 0) - fr.ts;
				}
#else
				/* Calculate ping time */
				iaxs[fr.callno]->pingtime =  calc_timestamp(iaxs[fr.callno], 0) - fr.ts;
#endif
				if (iaxs[fr.callno]->peerpoke) {
					peer = iaxs[fr.callno]->peerpoke;
					if ((peer->lastms < 0)  || (peer->lastms > peer->maxms)) {
						if (iaxs[fr.callno]->pingtime <= peer->maxms)
							ast_log(LOG_NOTICE, "Peer '%s' is now REACHABLE!\n", peer->name);
					} else if ((peer->lastms > 0) && (peer->lastms <= peer->maxms)) {
						if (iaxs[fr.callno]->pingtime > peer->maxms)
							ast_log(LOG_NOTICE, "Peer '%s' is now TOO LAGGED (%d ms)!\n", peer->name, iaxs[fr.callno]->pingtime);
					}
					peer->lastms = iaxs[fr.callno]->pingtime;
					if (peer->pokeexpire > -1)
						ast_sched_del(sched, peer->pokeexpire);
					send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
					iax2_destroy_nolock(fr.callno);
					peer->callno = 0;
					/* Try again eventually */
					if ((peer->lastms < 0)  || (peer->lastms > peer->maxms))
						peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_NOTOK, iax2_poke_peer_s, peer);
					else
						peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_OK, iax2_poke_peer_s, peer);
				}
				break;
			case AST_IAX2_COMMAND_LAGRQ:
			case AST_IAX2_COMMAND_LAGRP:
#ifdef BRIDGE_OPTIMIZATION
				if (iaxs[fr.callno]->bridgecallno) {
					forward_command(iaxs[fr.callno], AST_FRAME_IAX, f.subclass, fr.ts, NULL, 0, -1);
				} else {
#endif				
					/* A little strange -- We have to actually go through the motions of
					   delivering the packet.  In the very last step, it will be properly
					   handled by do_deliver */
					f.src = "LAGRQ";
					f.mallocd = 0;
					f.offset = 0;
					f.samples = 0;
					ast_iax2_frame_wrap(&fr, &f);
					schedule_delivery(iaxfrdup2(&fr), 1, updatehistory);
#ifdef BRIDGE_OPTIMIZATION
				}
#endif				
				break;
			case AST_IAX2_COMMAND_AUTHREQ:
				if (iaxs[fr.callno]->state & (IAX_STATE_STARTED | IAX_STATE_TBD)) {
					ast_log(LOG_WARNING, "Call on %s is already up, can't start on it\n", iaxs[fr.callno]->owner ? iaxs[fr.callno]->owner->name : "<Unknown>");
					break;
				}
				if (authenticate_reply(iaxs[fr.callno], &iaxs[fr.callno]->addr, &ies, iaxs[fr.callno]->secret, iaxs[fr.callno]->outkey)) {
					ast_log(LOG_WARNING, 
						"I don't know how to authenticate %s to %s\n", 
						ies.username ? ies.username : "<unknown>", inet_ntoa(iaxs[fr.callno]->addr.sin_addr));
				}
				break;
			case AST_IAX2_COMMAND_AUTHREP:
				/* Ignore once we've started */
				if (iaxs[fr.callno]->state & (IAX_STATE_STARTED | IAX_STATE_TBD)) {
					ast_log(LOG_WARNING, "Call on %s is already up, can't start on it\n", iaxs[fr.callno]->owner ? iaxs[fr.callno]->owner->name : "<Unknown>");
					break;
				}
				if (authenticate_verify(iaxs[fr.callno], &ies)) {
					ast_log(LOG_NOTICE, "Host %s failed to authenticate as %s\n", inet_ntoa(iaxs[fr.callno]->addr.sin_addr), iaxs[fr.callno]->username);
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No authority found");
					send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
					break;
				}
				/* This might re-enter the IAX code and need the lock */
				exists = ast_exists_extension(NULL, iaxs[fr.callno]->context, iaxs[fr.callno]->exten, 1, iaxs[fr.callno]->callerid);
				if (strcmp(iaxs[fr.callno]->exten, "TBD") && !exists) {
					ast_log(LOG_NOTICE, "Rejected connect attempt from %s, request '%s@%s' does not exist\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->exten, iaxs[fr.callno]->context);
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No such context/extension");
					send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
				} else {
					/* Select an appropriate format */
					format = iaxs[fr.callno]->peerformat & iax2_capability;
					if (!format) {
						ast_log(LOG_DEBUG, "We don't do requested format %d, falling back to peer capability %d\n", iaxs[fr.callno]->peerformat, iaxs[fr.callno]->peercapability);
						format = iaxs[fr.callno]->peercapability & iax2_capability;
						if (!format) {
							ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability 0x%x/0x%x incompatible  with our capability 0x%x.\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat, iaxs[fr.callno]->peercapability, iax2_capability);
							memset(&ied0, 0, sizeof(ied0));
							iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
							send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
						} else {
							/* Pick one... */
							format = ast_best_codec(iaxs[fr.callno]->peercapability & iax2_capability);
							if (!format) {
								ast_log(LOG_ERROR, "No best format in 0x%x???\n", iaxs[fr.callno]->peercapability & iax2_capability);
								ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability 0x%x/0x%x incompatible  with our capability 0x%x.\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat, iaxs[fr.callno]->peercapability, iax2_capability);
								memset(&ied0, 0, sizeof(ied0));
								iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
								send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
							}
						}
					}
					if (format) {
						/* Authentication received */
						memset(&ied1, 0, sizeof(ied1));
						iax_ie_append_int(&ied1, IAX_IE_FORMAT, format);
						send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACCEPT, 0, ied1.buf, ied1.pos, -1);
						if (strcmp(iaxs[fr.callno]->exten, "TBD")) {
							iaxs[fr.callno]->state |= IAX_STATE_STARTED;
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Accepting AUTHENTICATED call from %s, requested format = %d, actual format = %d\n", 
									inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat,format);
							iaxs[fr.callno]->state |= IAX_STATE_STARTED;
							if(!(c = ast_iax2_new(iaxs[fr.callno], AST_STATE_RING, format)))
								iax2_destroy_nolock(fr.callno);
						} else {
							iaxs[fr.callno]->state |= IAX_STATE_TBD;
							/* If this is a TBD call, we're ready but now what...  */
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Accepted AUTHENTICATED TBD call from %s\n", inet_ntoa(sin.sin_addr));
						}
					}
				}
				break;
			case AST_IAX2_COMMAND_DIAL:
				if (iaxs[fr.callno]->state & IAX_STATE_TBD) {
					iaxs[fr.callno]->state &= ~IAX_STATE_TBD;
					strncpy(iaxs[fr.callno]->exten, ies.called_number ? ies.called_number : "s", sizeof(iaxs[fr.callno]->exten)-1);	
					if (!ast_exists_extension(NULL, iaxs[fr.callno]->context, iaxs[fr.callno]->exten, 1, iaxs[fr.callno]->callerid)) {
						ast_log(LOG_NOTICE, "Rejected dial attempt from %s, request '%s@%s' does not exist\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->exten, iaxs[fr.callno]->context);
						memset(&ied0, 0, sizeof(ied0));
						iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No such context/extension");
						send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
					} else {
						iaxs[fr.callno]->state |= IAX_STATE_STARTED;
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Accepting DIAL from %s, formats = 0x%x\n", inet_ntoa(sin.sin_addr), iaxs[fr.callno]->peerformat);
						iaxs[fr.callno]->state |= IAX_STATE_STARTED;
						if(!(c = ast_iax2_new(iaxs[fr.callno], AST_STATE_RING, iaxs[fr.callno]->peerformat)))
							iax2_destroy_nolock(fr.callno);
					}
				}
				break;
			case AST_IAX2_COMMAND_INVAL:
				iaxs[fr.callno]->error = ENOTCONN;
				ast_log(LOG_DEBUG, "Immediately destroying %d, having received INVAL\n", fr.callno);
				iax2_destroy_nolock(fr.callno);
				if (option_debug)
					ast_log(LOG_DEBUG, "Destroying call %d\n", fr.callno);
				break;
			case AST_IAX2_COMMAND_VNAK:
				ast_log(LOG_DEBUG, "Sending VNAK\n");
				/* Force retransmission */
				vnak_retransmit(fr.callno, fr.iseqno);
				break;
			case AST_IAX2_COMMAND_REGREQ:
				if (register_verify(fr.callno, &sin, &ies)) {
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Registration Refused");
					send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REGREJ, 0, ied0.buf, ied0.pos, -1);
					break;
				}
				if ((!strlen(iaxs[fr.callno]->secret) && !strlen(iaxs[fr.callno]->inkeys)) || (iaxs[fr.callno]->state & IAX_STATE_AUTHENTICATED)) {
					if (update_registry(iaxs[fr.callno]->peer, &sin, fr.callno))
						ast_log(LOG_WARNING, "Registry error\n");
					break;
				}
				registry_authrequest(iaxs[fr.callno]->peer, fr.callno);
				break;
			case AST_IAX2_COMMAND_REGACK:
				if (iax2_ack_registry(&ies, &sin, fr.callno)) 
					ast_log(LOG_WARNING, "Registration failure\n");
				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
				iax2_destroy_nolock(fr.callno);
				break;
			case AST_IAX2_COMMAND_REGREJ:
				if (iaxs[fr.callno]->reg) {
					ast_log(LOG_NOTICE, "Registration of '%s' rejected: %s\n", iaxs[fr.callno]->reg->username, ies.cause ? ies.cause : "<unknown>");
					iaxs[fr.callno]->reg->regstate = REG_STATE_REJECTED;
				}
				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
				iax2_destroy_nolock(fr.callno);
				break;
			case AST_IAX2_COMMAND_REGAUTH:
				/* Authentication request */
				if (registry_rerequest(&ies, fr.callno, &sin)) {
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No authority found");
					send_command_final(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
				}
				break;
			case AST_IAX2_COMMAND_TXREJ:
				iaxs[fr.callno]->transferring = 0;
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Channel '%s' unable to transfer\n", iaxs[fr.callno]->owner ? iaxs[fr.callno]->owner->name : "<Unknown>");
				memset(&iaxs[fr.callno]->transfer, 0, sizeof(iaxs[fr.callno]->transfer));
				if (iaxs[fr.callno]->bridgecallno) {
					if (iaxs[iaxs[fr.callno]->bridgecallno]->transferring) {
						iaxs[iaxs[fr.callno]->bridgecallno]->transferring = 0;
						send_command(iaxs[iaxs[fr.callno]->bridgecallno], AST_FRAME_IAX, AST_IAX2_COMMAND_TXREJ, 0, NULL, 0, -1);
					}
				}
				break;
			case AST_IAX2_COMMAND_TXREADY:
				if (iaxs[fr.callno]->transferring == TRANSFER_BEGIN) {
					iaxs[fr.callno]->transferring = TRANSFER_READY;
					if (option_verbose > 2) 
						ast_verbose(VERBOSE_PREFIX_3 "Channel '%s' ready to transfer\n", iaxs[fr.callno]->owner ? iaxs[fr.callno]->owner->name : "<Unknown>");
					if (iaxs[fr.callno]->bridgecallno) {
						if (iaxs[iaxs[fr.callno]->bridgecallno]->transferring == TRANSFER_READY) {
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Releasing %s and %s\n", iaxs[fr.callno]->owner ? iaxs[fr.callno]->owner->name : "<Unknown>",
										iaxs[iaxs[fr.callno]->bridgecallno]->owner ? iaxs[iaxs[fr.callno]->bridgecallno]->owner->name : "<Unknown>");

							/* They're both ready, now release them. */
							iaxs[iaxs[fr.callno]->bridgecallno]->transferring = TRANSFER_RELEASED;
							iaxs[fr.callno]->transferring = TRANSFER_RELEASED;
							iaxs[iaxs[fr.callno]->bridgecallno]->alreadygone = 1;
							iaxs[fr.callno]->alreadygone = 1;

							/* Stop doing lag & ping requests */
							stop_stuff(fr.callno);
							stop_stuff(iaxs[fr.callno]->bridgecallno);

							memset(&ied0, 0, sizeof(ied0));
							memset(&ied1, 0, sizeof(ied1));
							iax_ie_append_short(&ied0, IAX_IE_CALLNO, iaxs[iaxs[fr.callno]->bridgecallno]->peercallno);
							iax_ie_append_short(&ied1, IAX_IE_CALLNO, iaxs[fr.callno]->peercallno);
							send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_TXREL, 0, ied0.buf, ied0.pos, -1);
							send_command(iaxs[iaxs[fr.callno]->bridgecallno], AST_FRAME_IAX, AST_IAX2_COMMAND_TXREL, 0, ied1.buf, ied1.pos, -1);

						}
					}
				}
				break;
			case AST_IAX2_COMMAND_TXREQ:
				try_transfer(iaxs[fr.callno], &ies);
				break;
			case AST_IAX2_COMMAND_TXCNT:
				if (iaxs[fr.callno]->transferring)
					send_command_transfer(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_TXACC, 0, NULL, 0);
				break;
			case AST_IAX2_COMMAND_TXREL:
				complete_transfer(fr.callno, &ies);
				break;	
			case AST_IAX2_COMMAND_DPREP:
				complete_dpreply(iaxs[fr.callno], &ies);
				break;
			case AST_IAX2_COMMAND_UNSUPPORT:
				ast_log(LOG_NOTICE, "Peer did not understand our iax command '%d'\n", ies.iax_unknown);
				break;
			default:
				ast_log(LOG_DEBUG, "Unknown IAX command %d on %d/%d\n", f.subclass, fr.callno, iaxs[fr.callno]->peercallno);
				memset(&ied0, 0, sizeof(ied0));
				iax_ie_append_byte(&ied0, IAX_IE_IAX_UNKNOWN, f.subclass);
				send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_UNSUPPORT, 0, ied0.buf, ied0.pos, -1);
			}
			/* Don't actually pass these frames along */
			if ((f.subclass != AST_IAX2_COMMAND_ACK) && 
			  (f.subclass != AST_IAX2_COMMAND_TXCNT) && 
			  (f.subclass != AST_IAX2_COMMAND_TXACC) && 
			  (f.subclass != AST_IAX2_COMMAND_INVAL) &&
			  (f.subclass != AST_IAX2_COMMAND_VNAK)) { 
			  	if (iaxs[fr.callno] && iaxs[fr.callno]->aseqno != iaxs[fr.callno]->iseqno)
					send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
			}
			ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
			return 1;
		}
		/* Unless this is an ACK or INVAL frame, ack it */
		if (iaxs[fr.callno]->aseqno != iaxs[fr.callno]->iseqno)
			send_command_immediate(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX2_COMMAND_ACK, fr.ts, NULL, 0,fr.iseqno);
	} else {
		/* A mini frame */
		f.frametype = AST_FRAME_VOICE;
		if (iaxs[fr.callno]->voiceformat > 0)
			f.subclass = iaxs[fr.callno]->voiceformat;
		else {
			ast_log(LOG_WARNING, "Received mini frame before first full voice frame\n ");
			iax2_vnak(fr.callno);
			ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
			return 1;
		}
		f.datalen = res - sizeof(struct ast_iax2_mini_hdr);
		if (f.datalen < 0) {
			ast_log(LOG_WARNING, "Datalen < 0?\n");
			ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
			return 1;
		}
		if (f.datalen)
			f.data = buf + sizeof(struct ast_iax2_mini_hdr);
		else
			f.data = NULL;
		fr.ts = (iaxs[fr.callno]->last & 0xFFFF0000L) | ntohs(mh->ts);
	}
	/* Don't pass any packets until we're started */
	if (!(iaxs[fr.callno]->state & IAX_STATE_STARTED)) {
		ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
		return 1;
	}
	/* Common things */
	f.src = "IAX2";
	f.mallocd = 0;
	f.offset = 0;
	if (f.datalen && (f.frametype == AST_FRAME_VOICE)) 
		f.samples = get_samples(&f);
	else
		f.samples = 0;
	ast_iax2_frame_wrap(&fr, &f);

	/* If this is our most recent packet, use it as our basis for timestamping */
	if (iaxs[fr.callno]->last < fr.ts) {
		iaxs[fr.callno]->last = fr.ts;
		fr.outoforder = 0;
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Received out of order packet... (type=%d, subclass %d, ts = %d, last = %d)\n", f.frametype, f.subclass, fr.ts, iaxs[fr.callno]->last);
		fr.outoforder = -1;
	}
#ifdef BRIDGE_OPTIMIZATION
	if (iaxs[fr.callno]->bridgecallno) {
		forward_delivery(&fr);
	} else {
		schedule_delivery(iaxfrdup2(&fr), 1, updatehistory);
	}
#else
	schedule_delivery(iaxfrdup2(&fr), 1, updatehistory);
#endif
	/* Always run again */
	ast_pthread_mutex_unlock(&iaxsl[fr.callno]);
	return 1;
}

static int iax2_do_register(struct iax2_registry *reg)
{
	struct iax_ie_data ied;
	if (option_debug)
		ast_log(LOG_DEBUG, "Sending registration request for '%s'\n", reg->username);
	if (!reg->callno) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Allocate call number\n");
		reg->callno = find_callno(0, 0, &reg->addr, NEW_FORCE);
		if (reg->callno < 1) {
			ast_log(LOG_WARNING, "Unable to create call for registration\n");
			return -1;
		} else if (option_debug)
			ast_log(LOG_DEBUG, "Registration created on call %d\n", reg->callno);
		iaxs[reg->callno]->reg = reg;
	}
	/* Schedule the next registration attempt */
	if (reg->expire > -1)
		ast_sched_del(sched, reg->expire);
	/* Setup the registration a little early */
	reg->expire  = ast_sched_add(sched, (5 * reg->refresh / 6) * 1000, iax2_do_register_s, reg);
	/* Send the request */
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_USERNAME, reg->username);
	iax_ie_append_short(&ied, IAX_IE_REFRESH, reg->refresh);
	send_command(iaxs[reg->callno],AST_FRAME_IAX, AST_IAX2_COMMAND_REGREQ, 0, ied.buf, ied.pos, -1);
	reg->regstate = REG_STATE_REGSENT;
	return 0;
}


static int iax2_poke_noanswer(void *data)
{
	struct iax2_peer *peer = data;
	peer->pokeexpire = -1;
	if (peer->lastms > -1)
		ast_log(LOG_NOTICE, "Peer '%s' is now UNREACHABLE!\n", peer->name);
	if (peer->callno > 0)
		iax2_destroy(peer->callno);
	peer->callno = 0;
	peer->lastms = -1;
	/* Try again quickly */
	peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_NOTOK, iax2_poke_peer_s, peer);
	return 0;
}

static int iax2_poke_peer(struct iax2_peer *peer)
{
	if (!peer->maxms || !peer->addr.sin_addr.s_addr) {
		/* IF we have no IP, or this isn't to be monitored, return
		  imeediately after clearing things out */
		peer->lastms = 0;
		peer->pokeexpire = -1;
		peer->callno = 0;
		return 0;
	}
	if (peer->callno > 0) {
		ast_log(LOG_NOTICE, "Still have a callno...\n");
		iax2_destroy(peer->callno);
	}
	peer->callno = find_callno(0, 0, &peer->addr, NEW_FORCE);
	if (peer->callno < 1) {
		ast_log(LOG_WARNING, "Unable to allocate call for poking peer '%s'\n", peer->name);
		return -1;
	}
	if (peer->pokeexpire > -1)
		ast_sched_del(sched, peer->pokeexpire);
	/* Speed up retransmission times */
	iaxs[peer->callno]->pingtime = peer->maxms / 4 + 1;
	iaxs[peer->callno]->peerpoke = peer;
	send_command(iaxs[peer->callno], AST_FRAME_IAX, AST_IAX2_COMMAND_POKE, 0, NULL, 0, -1);
	peer->pokeexpire = ast_sched_add(sched, DEFAULT_MAXMS * 2, iax2_poke_noanswer, peer);
	return 0;
}

static void free_context(struct iax2_context *con)
{
	struct iax2_context *conl;
	while(con) {
		conl = con;
		con = con->next;
		free(conl);
	}
}

static struct ast_channel *iax2_request(char *type, int format, void *data)
{
	int callno;
	int res;
	int sendani;
	int maxtime;
	int fmt, native;
	struct sockaddr_in sin;
	char s[256];
	char *st;
	struct ast_channel *c;
	char *stringp=NULL;
	int capability = iax2_capability;
	int trunk;
	strncpy(s, (char *)data, sizeof(s)-1);
	/* FIXME The next two lines seem useless */
	stringp=s;
	strsep(&stringp, "/");

	stringp=s;
	strsep(&stringp, "@");
	st = strsep(&stringp, "@");
	if (!st)
		st = s;
	/* Populate our address from the given */
	if (create_addr(&sin, &capability, &sendani, &maxtime, st, NULL, &trunk)) {
		return NULL;
	}
	callno = find_callno(0, 0, &sin, NEW_FORCE);
	if (callno < 1) {
		ast_log(LOG_WARNING, "Unable to create call\n");
		return NULL;
	}
	ast_pthread_mutex_lock(&iaxsl[callno]);
	/* If this is a trunk, update it now */
	iaxs[callno]->trunk = trunk;
	if (trunk) 
		callno = make_trunk(callno, 1);
	/* Keep track of sendani flag */
	iaxs[callno]->sendani = sendani;
	iaxs[callno]->maxtime = maxtime;
	c = ast_iax2_new(iaxs[callno], AST_STATE_DOWN, capability);
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	if (c) {
		/* Choose a format we can live with */
		if (c->nativeformats & format)
			c->nativeformats &= format;
		else {
			native = c->nativeformats;
			fmt = format;
			res = ast_translator_best_choice(&fmt, &native);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to create translator path for %d to %d on %s\n", c->nativeformats, fmt, c->name);
				ast_hangup(c);
				return NULL;
			}
			c->nativeformats = native;
		}
	}
	return c;
}

static void *network_thread(void *ignore)
{
	/* Our job is simple: Send queued messages, retrying if necessary.  Read frames 
	   from the network, and queue them for delivery to the channels */
	int res;
	struct ast_iax2_frame *f, *freeme;
	/* Establish I/O callback for socket read */
	ast_io_add(io, netsocket, socket_read, AST_IO_IN, NULL);
	if (timingfd > -1)
		ast_io_add(io, timingfd, timing_read, AST_IO_IN, NULL);
	for(;;) {
		/* Go through the queue, sending messages which have not yet been
		   sent, and scheduling retransmissions if appropriate */
		ast_pthread_mutex_lock(&iaxq.lock);
		f = iaxq.head;
		while(f) {
			freeme = NULL;
			if (!f->sentyet) {
				f->sentyet++;
				/* Send a copy immediately -- errors here are ok, so don't bother locking */
				if (iaxs[f->callno]) {
					send_packet(f);
				} 
				if (f->retries < 0) {
					/* This is not supposed to be retransmitted */
					if (f->prev) 
						f->prev->next = f->next;
					else
						iaxq.head = f->next;
					if (f->next)
						f->next->prev = f->prev;
					else
						iaxq.tail = f->prev;
					iaxq.count--;
					/* Free the iax frame */
					freeme = f;
				} else {
					/* We need reliable delivery.  Schedule a retransmission */
					f->retries++;
					f->retrans = ast_sched_add(sched, f->retrytime, attempt_transmit, f);
				}
			}
			f = f->next;
			if (freeme)
				ast_iax2_frame_free(freeme);
		}
		ast_pthread_mutex_unlock(&iaxq.lock);
		res = ast_sched_wait(sched);
		if ((res > 1000) || (res < 0))
			res = 1000;
		res = ast_io_wait(io, res);
		if (res >= 0) {
			ast_sched_runq(sched);
		}
	}
	return NULL;
}

static int start_network_thread(void)
{
	return pthread_create(&netthreadid, NULL, network_thread, NULL);
}

static struct iax2_context *build_context(char *context)
{
	struct iax2_context *con = malloc(sizeof(struct iax2_context));
	if (con) {
		strncpy(con->context, context, sizeof(con->context)-1);
		con->next = NULL;
	}
	return con;
}

static int get_auth_methods(char *value)
{
	int methods = 0;
	if (strstr(value, "rsa"))
		methods |= IAX_AUTH_RSA;
	if (strstr(value, "md5"))
		methods |= IAX_AUTH_MD5;
	if (strstr(value, "plaintext"))
		methods |= IAX_AUTH_RSA;
	return methods;
}

static struct iax2_peer *build_peer(char *name, struct ast_variable *v)
{
	struct iax2_peer *peer;
	struct iax2_peer *prev;
	int maskfound=0;
	int format;
	int found=0;
	prev = NULL;
	ast_pthread_mutex_lock(&peerl.lock);
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
		ast_pthread_mutex_unlock(&peerl.lock);
 	} else {
		ast_pthread_mutex_unlock(&peerl.lock);
		peer = malloc(sizeof(struct iax2_peer));
		memset(peer, 0, sizeof(struct iax2_peer));
		peer->expire = -1;
		peer->pokeexpire = -1;
	}
	if (peer) {
		if (!found) {
			strncpy(peer->name, name, sizeof(peer->name)-1);
			peer->addr.sin_port = htons(AST_DEFAULT_IAX_PORTNO);
			peer->expirey = expirey;
		}
		peer->capability = iax2_capability;
		while(v) {
			if (!strcasecmp(v->name, "secret")) 
				strncpy(peer->secret, v->value, sizeof(peer->secret)-1);
			else if (!strcasecmp(v->name, "mailbox"))
				strncpy(peer->mailbox, v->value, sizeof(peer->mailbox) - 1);
			else if (!strcasecmp(v->name, "trunk")) {
				peer->trunk = ast_true(v->value);
				if (peer->trunk && (timingfd < 0)) {
					ast_log(LOG_WARNING, "Unable to support trunking on peer '%s' without zaptel timing\n", peer->name);
					peer->trunk = 0;
				}
			} else if (!strcasecmp(v->name, "auth")) {
				peer->authmethods = get_auth_methods(v->value);
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
			} else if (!strcasecmp(v->name, "context")) {
				if (!strlen(peer->context))
					strncpy(peer->context, v->value, sizeof(peer->context) - 1);
			} else if (!strcasecmp(v->name, "port")) {
				if (peer->dynamic)
					peer->defaddr.sin_port = htons(atoi(v->value));
				else
					peer->addr.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "username")) {
				strncpy(peer->username, v->value, sizeof(peer->username)-1);
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
			} else if (!strcasecmp(v->name, "callerid")) {
				strncpy(peer->callerid, v->value, sizeof(peer->callerid)-1);
				peer->hascallerid=1;
			} else if (!strcasecmp(v->name, "sendani")) {
				peer->sendani = ast_true(v->value);
			} else if (!strcasecmp(v->name, "inkeys")) {
				strncpy(peer->inkeys, v->value, sizeof(peer->inkeys));
			} else if (!strcasecmp(v->name, "outkey")) {
				strncpy(peer->outkey, v->value, sizeof(peer->outkey));
			} else if (!strcasecmp(v->name, "qualify")) {
				if (!strcasecmp(v->value, "no")) {
					peer->maxms = 0;
				} else if (!strcasecmp(v->value, "yes")) {
					peer->maxms = DEFAULT_MAXMS;
				} else if (sscanf(v->value, "%d", &peer->maxms) != 1) {
					ast_log(LOG_WARNING, "Qualification of peer '%s' should be 'yes', 'no', or a number of milliseconds at line %d of iax.conf\n", peer->name, v->lineno);
					peer->maxms = 0;
				}
			}// else if (strcasecmp(v->name,"type"))
			//	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
			v=v->next;
		}
		if (!peer->authmethods)
			peer->authmethods = IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT;
		peer->delme = 0;
	}
	return peer;
}

static struct iax2_user *build_user(char *name, struct ast_variable *v)
{
	struct iax2_user *user;
	struct iax2_context *con, *conl = NULL;
	int format;
	user = (struct iax2_user *)malloc(sizeof(struct iax2_user));
	if (user) {
		memset(user, 0, sizeof(struct iax2_user));
		strncpy(user->name, name, sizeof(user->name)-1);
		while(v) {
			if (!strcasecmp(v->name, "context")) {
				con = build_context(v->value);
				if (con) {
					if (conl)
						conl->next = con;
					else
						user->contexts = con;
					conl = con;
				}
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny")) {
				user->ha = ast_append_ha(v->name, v->value, user->ha);
			} else if (!strcasecmp(v->name, "trunk")) {
				user->trunk = ast_true(v->value);
				if (user->trunk && (timingfd < 0)) {
					ast_log(LOG_WARNING, "Unable to support trunking on user '%s' without zaptel timing\n", user->name);
					user->trunk = 0;
				}
			} else if (!strcasecmp(v->name, "auth")) {
				user->authmethods = get_auth_methods(v->value);
			} else if (!strcasecmp(v->name, "secret")) {
				strncpy(user->secret, v->value, sizeof(user->secret)-1);
			} else if (!strcasecmp(v->name, "callerid")) {
				strncpy(user->callerid, v->value, sizeof(user->callerid)-1);
				user->hascallerid=1;
			} else if (!strcasecmp(v->name, "accountcode")) {
				strncpy(user->accountcode, v->value, sizeof(user->accountcode)-1);
			} else if (!strcasecmp(v->name, "amaflags")) {
				format = ast_cdr_amaflags2int(v->value);
				if (format < 0) {
					ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
				} else {
					user->amaflags = format;
				}
			} else if (!strcasecmp(v->name, "inkeys")) {
				strncpy(user->inkeys, v->value, sizeof(user->inkeys));
			}// else if (strcasecmp(v->name,"type"))
			//	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
			v = v->next;
		}
	}
	if (!user->authmethods) {
		if (strlen(user->secret)) {
			user->authmethods = IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT;
			if (strlen(user->inkeys))
				user->authmethods |= IAX_AUTH_RSA;
		} else if (strlen(user->inkeys)) {
			user->authmethods = IAX_AUTH_RSA;
		} else {
			user->authmethods = IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT;
		}
	}
	return user;
}


void delete_users(void){
	struct iax2_user *user, *userlast;
	struct iax2_peer *peer;
	struct iax2_registry *reg, *regl;

	/* Delete all users */
	ast_pthread_mutex_lock(&userl.lock);
	for (user=userl.users;user;) {
		ast_free_ha(user->ha);
		free_context(user->contexts);
		userlast = user;
		user=user->next;
		free(userlast);
	}
	userl.users=NULL;
	ast_pthread_mutex_unlock(&userl.lock);

	for (reg = registrations;reg;) {
		regl = reg;
		reg = reg->next;
		if (regl->expire > -1)
			ast_sched_del(sched, regl->expire);
		free(regl);
	}
	registrations = NULL;
	ast_pthread_mutex_lock(&peerl.lock);
	for (peer=peerl.peers;peer;) {
		/* Assume all will be deleted, and we'll find out for sure later */
		peer->delme = 1;
		peer = peer->next;
	}
	ast_pthread_mutex_unlock(&peerl.lock);
}

void prune_peers(void){
	/* Prune peers who still are supposed to be deleted */
	struct iax2_peer *peer, *peerlast, *peernext;
	int x;
	ast_pthread_mutex_lock(&peerl.lock);
	peerlast = NULL;
	for (peer=peerl.peers;peer;) {
		peernext = peer->next;
		if (peer->delme) {
			for (x=0;x<AST_IAX2_MAX_CALLS;x++) {
				ast_pthread_mutex_lock(&iaxsl[x]);
				if (iaxs[x] && (iaxs[x]->peerpoke == peer)) {
					iax2_destroy(x);
				}
				ast_pthread_mutex_unlock(&iaxsl[x]);
			}
			/* Delete it, it needs to disappear */
			if (peer->expire > -1)
				ast_sched_del(sched, peer->expire);
			if (peer->pokeexpire > -1)
				ast_sched_del(sched, peer->pokeexpire);
			if (peer->callno > 0)
				iax2_destroy(peer->callno);
			free(peer);
			if (peerlast)
				peerlast->next = peernext;
			else
				peerl.peers = peernext;
		} else
			peerlast = peer;
		peer=peernext;
	}
	ast_pthread_mutex_unlock(&peerl.lock);
}

static void set_timing(void)
{
#ifdef IAX_TRUNKING
	int bs = trunkfreq * 8;
	if (timingfd > -1) {
		if (ioctl(timingfd, ZT_SET_BLOCKSIZE, &bs))
			ast_log(LOG_WARNING, "Unable to set blocksize on timing source\n");
	}
#endif
}


static int set_config(char *config_file, struct sockaddr_in* sin){
	struct ast_config *cfg;
	int capability=iax2_capability;
	struct ast_variable *v;
	char *cat;
	char *utype;
	int format;
	struct iax2_user *user;
	struct iax2_peer *peer;
	static unsigned short int last_port=0;

	cfg = ast_load(config_file);
	
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config_file);
		return -1;
	}
	v = ast_variable_browse(cfg, "general");
	while(v) {
		if (!strcasecmp(v->name, "port")){ 
				ast_log(LOG_WARNING, "Ignoring port for now\n");
#if 0				
			sin->sin_port = ntohs(atoi(v->value));
			if(last_port==0){
				last_port=sin->sin_port;
#if	0
				ast_verbose("setting last port\n");
#endif
			}
			else if(sin->sin_port != last_port)
				ast_log(LOG_WARNING, "change to port ignored until next asterisk re-start\n");
#endif				
		}
		else if (!strcasecmp(v->name, "pingtime")) 
			ping_time = atoi(v->value);
		else if (!strcasecmp(v->name, "maxjitterbuffer")) 
			maxjitterbuffer = atoi(v->value);
		else if (!strcasecmp(v->name, "maxexcessbuffer")) 
			max_jitter_buffer = atoi(v->value);
		else if (!strcasecmp(v->name, "lagrqtime")) 
			lagrq_time = atoi(v->value);
		else if (!strcasecmp(v->name, "dropcount")) 
			iax2_dropcount = atoi(v->value);
		else if (!strcasecmp(v->name, "bindaddr"))
			inet_aton(v->value, &sin->sin_addr);
		else if (!strcasecmp(v->name, "jitterbuffer"))
			use_jitterbuffer = ast_true(v->value);
		else if (!strcasecmp(v->name, "trunkfreq")) {
			trunkfreq = atoi(v->value);
			if (trunkfreq < 10)
				trunkfreq = 10;
		} else if (!strcasecmp(v->name, "bandwidth")) {
			if (!strcasecmp(v->value, "low")) {
				capability = IAX_CAPABILITY_LOWBANDWIDTH;
			} else if (!strcasecmp(v->value, "medium")) {
				capability = IAX_CAPABILITY_MEDBANDWIDTH;
			} else if (!strcasecmp(v->value, "high")) {
				capability = IAX_CAPABILITY_FULLBANDWIDTH;
			} else
				ast_log(LOG_WARNING, "bandwidth must be either low, medium, or high\n");
		} else if (!strcasecmp(v->name, "allow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot allow unknown format '%s'\n", v->value);
			else
				capability |= format;
		} else if (!strcasecmp(v->name, "disallow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot disallow unknown format '%s'\n", v->value);
			else
				capability &= ~format;
		} else if (!strcasecmp(v->name, "register")) {
			iax2_register(v->value, v->lineno);
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
		} else if (!strcasecmp(v->name, "accountcode")) {
			strncpy(accountcode, v->value, sizeof(accountcode)-1);
		} else if (!strcasecmp(v->name, "amaflags")) {
			format = ast_cdr_amaflags2int(v->value);
			if (format < 0) {
				ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
			} else {
				amaflags = format;
			}
		} //else if (strcasecmp(v->name,"type"))
		//	ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
		v = v->next;
	}
	iax2_capability = capability;
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (utype) {
				if (!strcasecmp(utype, "user") || !strcasecmp(utype, "friend")) {
					user = build_user(cat, ast_variable_browse(cfg, cat));
					if (user) {
						ast_pthread_mutex_lock(&userl.lock);
						user->next = userl.users;
						userl.users = user;
						ast_pthread_mutex_unlock(&userl.lock);
					}
				}
				if (!strcasecmp(utype, "peer") || !strcasecmp(utype, "friend")) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat));
					if (peer) {
						ast_pthread_mutex_lock(&peerl.lock);
						peer->next = peerl.peers;
						peerl.peers = peer;
						ast_pthread_mutex_unlock(&peerl.lock);
					}
				} else if (strcasecmp(utype, "user")) {
					ast_log(LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, config_file);
				}
			} else
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_destroy(cfg);
	set_timing();
	return capability;
}

static int reload_config(void)
{
	char *config = "iax.conf";
	struct iax2_registry *reg;
	struct sockaddr_in dead_sin;
	strncpy(accountcode, "", sizeof(accountcode)-1);
	amaflags = 0;
	srand(time(NULL));
	delete_users();
	set_config(config,&dead_sin);
	prune_peers();
	for (reg = registrations; reg; reg = reg->next)
		iax2_do_register(reg);
	return 0;
}

int reload(void)
{
	return reload_config();
}

static int cache_get_callno(char *data)
{
	struct sockaddr_in sin;
	int x;
	char st[256], *s;
	char *host;
	char *username=NULL;
	char *password=NULL;
	char *context=NULL;
	int callno;
	struct iax_ie_data ied;
	for (x=0;x<AST_IAX2_MAX_CALLS; x++) {
		/* Look for an *exact match* call.  Once a call is negotiated, it can only
		   look up entries for a single context */
		if (!pthread_mutex_trylock(&iaxsl[x])) {
			if (iaxs[x] && !strcasecmp(data, iaxs[x]->dproot)) {
				ast_pthread_mutex_unlock(&iaxsl[x]);
				return x;
			}
			ast_pthread_mutex_unlock(&iaxsl[x]);
		}
	}
	memset(&ied, 0, sizeof(ied));
	/* No match found, we need to create a new one */
	strncpy(st, data, sizeof(st)-1);
	/* Grab the host */
	s = strchr(st, '/');
	if (s) {
		*s = '\0';
		s++;
		context = s;
	}
	s = strchr(st, '@');
	if (s) {
		/* Get username/password if there is one */
		*s='\0';
		username=st;
		password = strchr(username, ':');
		if (password) {
			*password = '\0';
			password++;
		}
		s++;
		host = s;
	} else {
		/* Just a hostname */
		host = st;
	}
	/* Populate our address from the given */
	if (create_addr(&sin, NULL, NULL, NULL, host, NULL, NULL)) {
		return -1;
	}
	ast_log(LOG_DEBUG, "host: %s, user: %s, password: %s, context: %s\n", host, username, password, context);
	callno = find_callno(0, 0, &sin, NEW_FORCE);
	if (callno < 1) {
		ast_log(LOG_WARNING, "Unable to create call\n");
		return -1;
	}
	ast_pthread_mutex_lock(&iaxsl[callno]);
	strncpy(iaxs[callno]->dproot, data, sizeof(iaxs[callno]->dproot)-1);
	iaxs[callno]->capability = IAX_CAPABILITY_FULLBANDWIDTH;

	iax_ie_append_short(&ied, IAX_IE_VERSION, AST_IAX2_PROTO_VERSION);
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, "TBD");
	if (context)
		iax_ie_append_str(&ied, IAX_IE_CALLED_CONTEXT, context);
	if (username)
		iax_ie_append_str(&ied, IAX_IE_USERNAME, username);
	iax_ie_append_int(&ied, IAX_IE_FORMAT, IAX_CAPABILITY_FULLBANDWIDTH);
	iax_ie_append_int(&ied, IAX_IE_CAPABILITY, IAX_CAPABILITY_FULLBANDWIDTH);
	/* Keep password handy */
	if (password)
		strncpy(iaxs[callno]->secret, password, sizeof(iaxs[callno]->secret)-1);
#if 0
	/* XXX Need equivalent XXX */
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Calling TBD using options '%s'\n", requeststr);
#endif		
	/* Start the call going */
	send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX2_COMMAND_NEW, 0, ied.buf, ied.pos, -1);
	ast_pthread_mutex_unlock(&iaxsl[callno]);
	return callno;
}

static struct iax2_dpcache *find_cache(struct ast_channel *chan, char *data, char *context, char *exten, int priority)
{
	struct iax2_dpcache *dp, *prev = NULL, *next;
	struct timeval tv;
	int x;
	int com[2];
	int timeout;
	int old=0;
	int outfd;
	int abort;
	int callno;
	struct ast_channel *c;
	struct ast_frame *f;
	gettimeofday(&tv, NULL);
	dp = dpcache;
	while(dp) {
		next = dp->next;
		/* Expire old caches */
		if ((tv.tv_sec > dp->expirey.tv_sec) ||
				((tv.tv_sec == dp->expirey.tv_sec) && (tv.tv_usec > dp->expirey.tv_usec)))  {
				/* It's expired, let it disappear */
				if (prev)
					prev->next = dp->next;
				else
					dpcache = dp->next;
				if (!dp->peer && !(dp->flags & CACHE_FLAG_PENDING) && !dp->callno) {
					/* Free memory and go again */
					free(dp);
				} else {
					ast_log(LOG_WARNING, "DP still has peer field or pending or callno (flags = %d, peer = %p callno = %d)\n", dp->flags, dp->peer, dp->callno);
				}
				dp = next;
				continue;
		}
		/* We found an entry that matches us! */
		if (!strcmp(dp->peercontext, data) && !strcmp(dp->exten, exten)) 
			break;
		dp = next;
	}
	if (!dp) {
		/* No matching entry.  Create a new one. */
		/* First, can we make a callno? */
		callno = cache_get_callno(data);
		if (callno < 0) {
			ast_log(LOG_WARNING, "Unable to generate call for '%s'\n", data);
			return NULL;
		}
		dp = malloc(sizeof(struct iax2_dpcache));
		if (!dp)
			return NULL;
		memset(dp, 0, sizeof(struct iax2_dpcache));
		strncpy(dp->peercontext, data, sizeof(dp->peercontext)-1);
		strncpy(dp->exten, exten, sizeof(dp->exten)-1);
		gettimeofday(&dp->expirey, NULL);
		dp->orig = dp->expirey;
		/* Expires in 30 mins by default */
		dp->expirey.tv_sec += iaxdefaultdpcache;
		dp->next = dpcache;
		dp->flags = CACHE_FLAG_PENDING;
		for (x=0;x<sizeof(dp->waiters) / sizeof(dp->waiters[0]); x++)
			dp->waiters[x] = -1;
		dpcache = dp;
		dp->peer = iaxs[callno]->dpentries;
		iaxs[callno]->dpentries = dp;
		/* Send the request if we're already up */
		if (iaxs[callno]->state & IAX_STATE_STARTED)
			iax2_dprequest(dp, callno);
	}
	/* By here we must have a dp */
	if (dp->flags & CACHE_FLAG_PENDING) {
		/* Okay, here it starts to get nasty.  We need a pipe now to wait
		   for a reply to come back so long as it's pending */
		for (x=0;x<sizeof(dp->waiters) / sizeof(dp->waiters[0]); x++) {
			/* Find an empty slot */
			if (dp->waiters[x] < 0)
				break;
		}
		if (x >= sizeof(dp->waiters) / sizeof(dp->waiters[0])) {
			ast_log(LOG_WARNING, "No more waiter positions available\n");
			return NULL;
		}
		if (pipe(com)) {
			ast_log(LOG_WARNING, "Unable to create pipe for comm\n");
			return NULL;
		}
		dp->waiters[x] = com[1];
		/* Okay, now we wait */
		timeout = iaxdefaulttimeout * 1000;
		/* Temporarily unlock */
		ast_pthread_mutex_unlock(&dpcache_lock);
		/* Defer any dtmf */
		if (chan)
			old = ast_channel_defer_dtmf(chan);
		abort = 0;
		while(timeout) {
			c = ast_waitfor_nandfds(&chan, chan ? 1 : 0, &com[0], 1, NULL, &outfd, &timeout);
			if (outfd > -1) {
				break;
			}
			if (c) {
				f = ast_read(c);
				if (f)
					ast_frfree(f);
				else {
					/* Got hung up on, abort! */
					break;
					abort = 1;
				}
			}
		}
		if (!timeout) {
			ast_log(LOG_WARNING, "Timeout waiting for %s exten %s\n", data, exten);
		}
		ast_pthread_mutex_lock(&dpcache_lock);
		dp->waiters[x] = -1;
		close(com[1]);
		close(com[0]);
		if (abort) {
			/* Don't interpret anything, just abort.  Not sure what th epoint
			  of undeferring dtmf on a hung up channel is but hey whatever */
			if (!old && chan)
				ast_channel_undefer_dtmf(chan);
			return NULL;
		}
		if (!(dp->flags & CACHE_FLAG_TIMEOUT)) {
			/* Now to do non-independent analysis the results of our wait */
			if (dp->flags & CACHE_FLAG_PENDING) {
				/* Still pending... It's a timeout.  Wake everybody up.  Consider it no longer
				   pending.  Don't let it take as long to timeout. */
				dp->flags &= ~CACHE_FLAG_PENDING;
				dp->flags |= CACHE_FLAG_TIMEOUT;
				/* Expire after only 60 seconds now.  This is designed to help reduce backlog in heavily loaded
				   systems without leaving it unavailable once the server comes back online */
				dp->expirey.tv_sec = dp->orig.tv_sec + 60;
				for (x=0;x<sizeof(dp->waiters) / sizeof(dp->waiters[0]); x++)
					if (dp->waiters[x] > -1)
						write(dp->waiters[x], "asdf", 4);
			}
		}
		/* Our caller will obtain the rest */
		if (!old && chan)
			ast_channel_undefer_dtmf(chan);
	}
	return dp;	
}

static int iax2_exists(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, char *data)
{
	struct iax2_dpcache *dp;
	int res = 0;
#if 0
	ast_log(LOG_NOTICE, "iax2_exists: con: %s, exten: %s, pri: %d, cid: %s, data: %s\n", context, exten, priority, callerid ? callerid : "<unknown>", data);
#endif
	if (priority != 1)
		return 0;
	ast_pthread_mutex_lock(&dpcache_lock);
	dp = find_cache(chan, data, context, exten, priority);
	if (dp) {
		if (dp->flags & CACHE_FLAG_EXISTS)
			res= 1;
	}
	ast_pthread_mutex_unlock(&dpcache_lock);
	if (!dp) {
		ast_log(LOG_WARNING, "Unable to make DP cache\n");
	}
	return res;
}

static int iax2_canmatch(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, char *data)
{
	int res = 0;
	struct iax2_dpcache *dp;
#if 0
	ast_log(LOG_NOTICE, "iax2_canmatch: con: %s, exten: %s, pri: %d, cid: %s, data: %s\n", context, exten, priority, callerid ? callerid : "<unknown>", data);
#endif
	if (priority != 1)
		return 0;
	ast_pthread_mutex_lock(&dpcache_lock);
	dp = find_cache(chan, data, context, exten, priority);
	if (dp) {
		if (dp->flags & CACHE_FLAG_CANEXIST)
			res= 1;
	}
	ast_pthread_mutex_unlock(&dpcache_lock);
	if (!dp) {
		ast_log(LOG_WARNING, "Unable to make DP cache\n");
	}
	return res;
}

static int iax2_matchmore(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, char *data)
{
	int res = 0;
	struct iax2_dpcache *dp;
#if 0
	ast_log(LOG_NOTICE, "iax2_matchmore: con: %s, exten: %s, pri: %d, cid: %s, data: %s\n", context, exten, priority, callerid ? callerid : "<unknown>", data);
#endif
	if (priority != 1)
		return 0;
	ast_pthread_mutex_lock(&dpcache_lock);
	dp = find_cache(chan, data, context, exten, priority);
	if (dp) {
		if (dp->flags & CACHE_FLAG_MATCHMORE)
			res= 1;
	}
	ast_pthread_mutex_unlock(&dpcache_lock);
	if (!dp) {
		ast_log(LOG_WARNING, "Unable to make DP cache\n");
	}
	return res;
}

static int iax2_exec(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, int newstack, char *data)
{
	char odata[256];
	char req[256];
	char *ncontext;
	struct iax2_dpcache *dp;
	struct ast_app *dial;
#if 0
	ast_log(LOG_NOTICE, "iax2_exec: con: %s, exten: %s, pri: %d, cid: %s, data: %s, newstack: %d\n", context, exten, priority, callerid ? callerid : "<unknown>", data, newstack);
#endif
	if (priority != 1)
		return -1;
	ast_pthread_mutex_lock(&dpcache_lock);
	dp = find_cache(chan, data, context, exten, priority);
	if (dp) {
		if (dp->flags & CACHE_FLAG_EXISTS) {
			strncpy(odata, data, sizeof(odata)-1);
			ncontext = strchr(odata, '/');
			if (ncontext) {
				*ncontext = '\0';
				ncontext++;
				snprintf(req, sizeof(req), "IAX/%s/%s@%s", odata, exten, ncontext);
			} else {
				snprintf(req, sizeof(req), "IAX/%s/%s", odata, exten);
			}
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Executing Dial('%s')\n", req);
		} else {
			ast_pthread_mutex_unlock(&dpcache_lock);
			ast_log(LOG_WARNING, "Can't execute non-existant extension '%s[@%s]' in data '%s'\n", exten, context, data);
			return -1;
		}
	}
	ast_pthread_mutex_unlock(&dpcache_lock);
	dial = pbx_findapp("Dial");
	if (dial) {
		pbx_exec(chan, dial, req, newstack);
	} else {
		ast_log(LOG_WARNING, "No dial application registered\n");
	}
	return -1;
}

static struct ast_switch iax2_switch = 
{
	name: 			"IAX2",
	description: 	"IAX2 Remote Dialplan Switch",
	exists:			iax2_exists,
	canmatch:		iax2_canmatch,
	exec:			iax2_exec,
	matchmore:		iax2_matchmore,
};

int load_module(void)
{
	char *config = "iax.conf";
	int res = 0;
	int x;
	struct iax2_registry *reg;
	struct iax2_peer *peer;
	
	struct sockaddr_in sin;
	
	/* Seed random number generator */
	srand(time(NULL));
	
	sin.sin_family = AF_INET;
	sin.sin_port = ntohs(AST_DEFAULT_IAX_PORTNO);
	sin.sin_addr.s_addr = INADDR_ANY;

#ifdef IAX_TRUNKING
	timingfd = open("/dev/zap/pseudo", O_RDWR);
	if (timingfd < 0) 
		ast_log(LOG_WARNING, "Unable to open IAX timing interface: %s\n", strerror(errno));
#endif		

	for (x=0;x<AST_IAX2_MAX_CALLS;x++)
		ast_pthread_mutex_init(&iaxsl[x]);
	
	io = io_context_create();
	sched = sched_context_create();
	
	if (!io || !sched) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}

	ast_pthread_mutex_init(&iaxq.lock);
	ast_pthread_mutex_init(&userl.lock);

	ast_cli_register(&cli_show_users);
	ast_cli_register(&cli_show_channels);
	ast_cli_register(&cli_show_peers);
	ast_cli_register(&cli_show_registry);
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_no_debug);
	ast_cli_register(&cli_set_jitter);
	ast_cli_register(&cli_show_stats);
	ast_cli_register(&cli_show_cache);

	ast_manager_register( "IAX2peers", 0, manager_iax2_show_peers, "List IAX Peers" );

	set_config(config,&sin);

	if (ast_channel_register(type, tdesc, iax2_capability, iax2_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		unload_module();
		return -1;
	}

	if (ast_register_switch(&iax2_switch)) 
		ast_log(LOG_ERROR, "Unable to register IAX switch\n");
	
	/* Make a UDP socket */
	netsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	
	if (netsocket < 0) {
		ast_log(LOG_ERROR, "Unable to create network socket: %s\n", strerror(errno));
		return -1;
	}
	if (bind(netsocket,(struct sockaddr *)&sin, sizeof(sin))) {
		ast_log(LOG_ERROR, "Unable to bind to %s port %d: %s\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), strerror(errno));
		return -1;
	}

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);

	if (setsockopt(netsocket, SOL_IP, IP_TOS, &tos, sizeof(tos))) 
		ast_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
	
	if (!res) {
		res = start_network_thread();
		if (option_verbose > 1) 
			ast_verbose(VERBOSE_PREFIX_2 "IAX Ready and Listening on %s port %d\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	} else {
		ast_log(LOG_ERROR, "Unable to start network thread\n");
		close(netsocket);
	}
	for (reg = registrations; reg; reg = reg->next)
		iax2_do_register(reg);
	ast_pthread_mutex_lock(&peerl.lock);
	for (peer = peerl.peers; peer; peer = peer->next)
		iax2_poke_peer(peer);
	ast_pthread_mutex_unlock(&peerl.lock);
	return res;
}

char *description()
{
	return desc;
}

int unload_module()
{
	int x;
	/* Cancel the network thread, close the net socket */
	pthread_cancel(netthreadid);
	pthread_join(netthreadid, NULL);
	close(netsocket);
	for (x=0;x<AST_IAX2_MAX_CALLS;x++)
		if (iaxs[x])
			iax2_destroy(x);
	ast_manager_unregister( "IAXpeers" );
	ast_cli_unregister(&cli_show_users);
	ast_cli_unregister(&cli_show_channels);
	ast_cli_unregister(&cli_show_peers);
	ast_cli_unregister(&cli_set_jitter);
	ast_cli_unregister(&cli_show_stats);
	ast_cli_unregister(&cli_show_cache);
	ast_unregister_switch(&iax2_switch);
	delete_users();
	return 0;
}

int usecount()
{
	int res;
	ast_pthread_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_pthread_mutex_unlock(&usecnt_lock);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
