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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <netinet/ip.h>

/* #define VOCAL_DATA_HACK */

#define SIPDUMPER
#define DEFAULT_EXPIREY 120
#define MAX_EXPIREY     3600
#define DEFAULT_MAXMS		2000		/* Must be faster than 2 seconds by default */

#define DEFAULT_MAXMS		2000		/* Must be faster than 2 seconds by default */
#define DEFAULT_FREQ_OK		60 * 1000		/* How often to check for the host to be up */
#define DEFAULT_FREQ_NOTOK	10 * 1000		/* How often to check, if the host is down... */

static char *desc = "Session Initiation Protocol (SIP)";
static char *type = "sip";
static char *tdesc = "Session Initiation Protocol (SIP)";
static char *config = "sip.conf";

#define DEFAULT_SIP_PORT	5060	/* From RFC 2543 */
#define SIP_MAX_PACKET	1500		/* Also from RFC 2543, should sub headers tho */

static char context[AST_MAX_EXTENSION] = "default";

static char language[MAX_LANGUAGE] = "";

static int usecnt =0;
static pthread_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;

/* Protect the interface list (of sip_pvt's) */
static pthread_mutex_t iflock = AST_MUTEX_INITIALIZER;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
static pthread_mutex_t netlock = AST_MUTEX_INITIALIZER;

static pthread_mutex_t monlock = AST_MUTEX_INITIALIZER;

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = 0;

static int restart_monitor(void);

/* Just about everybody seems to support ulaw, so make it a nice default */
static int capability = AST_FORMAT_ULAW | AST_FORMAT_ALAW | AST_FORMAT_GSM;

static char ourhost[256];
static struct in_addr __ourip;
static int ourport;

static int sipdebug = 0;

static int tos = 0;

/* Expire slowly */
static int expirey = 900;

static struct sched_context *sched;
static struct io_context *io;
/* The private structures of the  sip channels are linked for
   selecting outgoing channels */
   
#define SIP_MAX_HEADERS		64
#define SIP_MAX_LINES 		64

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

static struct sip_pvt {
	pthread_mutex_t lock;				/* Channel private lock */
	char callid[80];					/* Global CallID */
	char randdata[80];	/* Random data */
	unsigned int ocseq;					/* Current outgoing seqno */
	unsigned int icseq;					/* Current incoming seqno */
	int lastinvite;						/* Last Cseq of invite */
	int alreadygone;					/* Whether or not we've already been destroyed by or peer */
	int needdestroy;					/* if we need to be destroyed */
	int capability;						/* Special capability */
	int outgoing;						/* Outgoing or incoming call? */
	int insecure;						/* Don't check source port/ip */
	int expirey;						/* How long we take to expire */
	int branch;							/* One random number */
	int canreinvite;					/* Do we support reinvite */
	int progress;						/* Have sent 183 message progress */
	int tag;							/* Another random number */
	struct sockaddr_in sa;				/* Our peer */
	struct in_addr ourip;				/* Our IP */
	struct ast_channel *owner;			/* Who owns us */
	char exten[AST_MAX_EXTENSION];		/* Extention where to start */
	char refer_to[AST_MAX_EXTENSION];	/* Place to store REFER-TO extension */
	char referred_by[AST_MAX_EXTENSION];/* Place to store REFERRED-BY extension */
	char refer_contact[AST_MAX_EXTENSION];/* Place to store Contact info from a REFER extension */
	char record_route[256];
	char record_route_info[256];
	char remote_party_id[256];
	char context[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char theirtag[256];				/* Their tag */
	char username[81];
	char peername[81];
	char peersecret[81];
	char callerid[256];					/* Caller*ID */
	char via[256];
	char accountcode[256];				/* Account code */
	char mailbox[AST_MAX_EXTENSION];		/* Associated mailbox */
	int amaflags;						/* AMA Flags */
	struct sip_request initreq;			/* Initial request */
	
	int maxtime;						/* Max time for first response */
	int initid;							/* Auto-congest ID if appropriate */
	
	struct sip_peer *peerpoke;			/* If this calls is to poke a peer, which one */
	struct sip_registry *registry;			/* If this is a REGISTER call, to which registry */
	struct ast_rtp *rtp;				/* RTP Session */
	struct sip_pvt *next;
} *iflist = NULL;

static struct sip_pkt {
	int retrans;
	struct sip_pvt *owner;
	int packetlen;
	char data[SIP_MAX_PACKET];
	struct sip_pkt *next;
} *packets = NULL;	

struct sip_user {
	/* Users who can access various contexts */
	char name[80];
	char secret[80];
	char context[80];
	char callerid[80];
	char methods[80];
	char accountcode[80];
	char mailbox[AST_MAX_EXTENSION];
	int hascallerid;
	int amaflags;
	int insecure;
	int canreinvite;
	struct ast_ha *ha;
	struct sip_user *next;
};

struct sip_peer {
	char name[80];
	char secret[80];
	char context[80];		/* JK02: peers need context too to allow parking etc */
	char methods[80];
	char username[80];
	char mailbox[AST_MAX_EXTENSION];
	int dynamic;
	int expire;
	int expirey;
	int capability;
	int insecure;
	int canreinvite;
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
	int lastmsg;
	struct sip_peer *next;
};

static struct ast_user_list {
	struct sip_user *users;
	pthread_mutex_t lock;
} userl = { NULL, AST_MUTEX_INITIALIZER };

static struct ast_peer_list {
	struct sip_peer *peers;
	pthread_mutex_t lock;
} peerl = { NULL, AST_MUTEX_INITIALIZER };


#define REG_STATE_UNREGISTERED 0
#define REG_STATE_REGSENT	   1
#define REG_STATE_AUTHSENT 	   2
#define REG_STATE_REGISTERED   3
#define REG_STATE_REJECTED	   4
#define REG_STATE_TIMEOUT	   5
#define REG_STATE_NOAUTH	   6

struct sip_registry {
	pthread_mutex_t lock;				/* Channel private lock */
	struct sockaddr_in addr;		/* Who we connect to for registration purposes */
	char username[80];
	char secret[80];			/* Password or key name in []'s */
	char random[80];
	int expire;					/* Sched ID of expiration */
	int timeout; 					/* sched id of sip_reg_timeout */
	int refresh;					/* How often to refresh */
	struct sip_pvt *call;				/* create a sip_pvt structure for each outbound "registration call" in progress */
	int regstate;
	int callid_valid;		/* 0 means we haven't chosen callid for this registry yet. */
	char callid[80];		/* Global CallID for this registry */
	struct sockaddr_in us;			/* Who the server thinks we are */
	struct sip_registry *next;
};

static int sip_do_register(struct sip_registry *r);
struct sip_registry *registrations;

static int sipsock  = -1;

static struct sockaddr_in bindaddr;

static struct ast_frame  *sip_read(struct ast_channel *ast);
static int transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req);
static int transmit_response_with_sdp(struct sip_pvt *p, char *msg, struct sip_request *req);
static int transmit_response_with_auth(struct sip_pvt *p, char *msg, struct sip_request *req, char *rand);
static int transmit_request(struct sip_pvt *p, char *msg, int inc);
static int transmit_invite(struct sip_pvt *p, char *msg, int sendsdp, char *auth, char *vxml_url);
static int transmit_reinvite_with_sdp(struct sip_pvt *p, struct ast_rtp *rtp);
static int transmit_message_with_text(struct sip_pvt *p, char *text);
static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req);
static int sip_send_mwi(struct sip_pvt *p);

static int __sip_xmit(struct sip_pvt *p, char *data, int len)
{
	int res;
    res=sendto(sipsock, data, len, 0, (struct sockaddr *)&p->sa, sizeof(struct sockaddr_in));
	if (res != len) {
		ast_log(LOG_WARNING, "sip_xmit of %p (len %d) to %s returned %d: %s\n", data, len, inet_ntoa(p->sa.sin_addr), res, strerror(errno));
	}
	return res;
}

static int send_response(struct sip_pvt *p, struct sip_request *req)
{
	int res;
	if (sipdebug)
		ast_verbose("Transmitting:\n%s\n to %s:%d\n", req->data, inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	res = __sip_xmit(p, req->data, req->len);
	if (res > 0)
		res = 0;
	return res;
}

static int send_request(struct sip_pvt *p, struct sip_request *req)
{
	int res;
	if (sipdebug)
		ast_verbose("XXX Need to handle Retransmitting XXX:\n%s to %s:%d\n", req->data, inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	res = __sip_xmit(p, req->data, req->len);
	return res;
}

static char *ditch_braces(char *tmp)
{
	char *c = tmp;
	char *n;
	c = tmp;
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
	if (sipdebug) 
		ast_verbose("Sending text %s on %s\n", text, ast->name);
	if (!p)
		return -1;
	if (!text || !strlen(text))
		return 0;
	if (sipdebug)
		ast_verbose("Really sending text %s on %s\n", text, ast->name);
	transmit_message_with_text(p, text);
	return 0;	
}

static int create_addr(struct sip_pvt *r, char *peer)
{
	struct hostent *hp;
	struct sip_peer *p;
	int found=0;
	r->sa.sin_family = AF_INET;
	ast_pthread_mutex_lock(&peerl.lock);
	p = peerl.peers;
	while(p) {
		if (!strcasecmp(p->name, peer)) {
			found++;
			r->capability = p->capability;
			strncpy(r->peername, p->username, sizeof(r->peername)-1);
			strncpy(r->peersecret, p->secret, sizeof(r->peersecret)-1);
			strncpy(r->username, p->username, sizeof(r->username)-1);
			r->insecure = p->insecure;
			r->canreinvite = p->canreinvite;
			r->maxtime = p->maxms;
			strncpy(r->context, p->context,sizeof(r->context)-1);
			strncpy(r->mailbox, p->mailbox,sizeof(r->mailbox)-1);
			if ((p->addr.sin_addr.s_addr || p->defaddr.sin_addr.s_addr) &&
				(!p->maxms || ((p->lastms > 0)  && (p->lastms <= p->maxms)))) {
				if (p->addr.sin_addr.s_addr) {
					r->sa.sin_addr = p->addr.sin_addr;
					r->sa.sin_port = p->addr.sin_port;
				} else {
					r->sa.sin_addr = p->defaddr.sin_addr;
					r->sa.sin_port = p->defaddr.sin_port;
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
			memcpy(&r->sa.sin_addr, hp->h_addr, sizeof(r->sa.sin_addr));
			r->sa.sin_port = htons(DEFAULT_SIP_PORT);
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
	struct sip_pvt *p = nothing;
	ast_pthread_mutex_lock(&p->lock);
	p->initid = -1;
	if (p->owner) {
		if (!pthread_mutex_trylock(&p->owner->lock)) {
			ast_log(LOG_NOTICE, "Auto-congesting %s\n", p->owner->name);
			ast_queue_control(p->owner, AST_CONTROL_CONGESTION, 0);
			ast_pthread_mutex_unlock(&p->owner->lock);
		}
	}
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
}

static int sip_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res;
	struct sip_pvt *p;
	char *vxml_url = NULL;
	struct varshead *headp;
	struct ast_var_t *current;
	
	p = ast->pvt->pvt;
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "sip_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	/* Check whether there is a VXML_URL variable */
	headp=&ast->varshead;
	AST_LIST_TRAVERSE(headp,current,entries) {
		if (strcasecmp(ast_var_name(current),"VXML_URL")==0)
	        {
			vxml_url = ast_var_value(current);
			break;
		}
	}
	
	res = 0;
	p->outgoing = 1;
	transmit_invite(p, "INVITE", 1, NULL, vxml_url);
	if (p->maxtime) {
		/* Initialize auto-congest time */
		p->initid = ast_sched_add(sched, p->maxtime * 2, auto_congest, p);
	}
	return res;
}

static void __sip_destroy(struct sip_pvt *p, int lockowner)
{
	struct sip_pvt *cur, *prev = NULL;
	if (p->rtp) {
		ast_rtp_destroy(p->rtp);
	}
	/* Unlink us from the owner if we have one */
	if (p->owner) {
		if (lockowner)
			ast_pthread_mutex_lock(&p->owner->lock);
		ast_log(LOG_DEBUG, "Detaching from %s\n", p->owner->name);
		p->owner->pvt->pvt = NULL;
		if (lockowner)
			ast_pthread_mutex_unlock(&p->owner->lock);
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
		free(p);
	}
}
static void sip_destroy(struct sip_pvt *p)
{
	ast_pthread_mutex_lock(&iflock);
	__sip_destroy(p, 1);
	ast_pthread_mutex_unlock(&iflock);
}

/* Interface lookup code courtesy Tilghman of DrunkCoder.com.  Thanks! */

struct my_ifreq {
    union
      {
	char ifrn_name[IFNAMSIZ];	/* Interface name, e.g. "en0".  */
      } ifr_ifrn;

    union
      {
	struct sockaddr_in ifru_addr;
	char ifru_data[512];
      } ifr_ifru;
};

struct in_addr *lookup_iface(char *iface) {
	int mysock;
	int res;
	static struct  my_ifreq ifreq;
	memset(&ifreq, 0, sizeof(ifreq));
	strncpy(ifreq.ifr_ifrn.ifrn_name,iface,sizeof(ifreq.ifr_ifrn.ifrn_name) - 1);

	mysock = socket(PF_INET,SOCK_DGRAM,IPPROTO_IP);
	res = ioctl(mysock,SIOCGIFADDR,&ifreq);
	
	close(mysock);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to get IP of %s: %s\n", iface, strerror(errno));
		return &__ourip;
	}
	return( (struct in_addr *) &ifreq.ifr_ifru.ifru_addr.sin_addr );
}

static struct in_addr *myaddrfor(struct in_addr *them)
{
	FILE *PROC;
	struct in_addr *temp = NULL;
	unsigned int remote_ip;
	char line[256];
	remote_ip = them->s_addr;
	
	PROC = fopen("/proc/net/route","r");
	if (!PROC) {
		/* If /proc/net/route doesn't exist, fall back to the old method */
		return &__ourip;
	}
	/* First line contains headers */
	fgets(line,sizeof(line),PROC);

	while (!feof(PROC)) {
		char iface[8];
		unsigned int dest, gateway, mask;
		int i,aoffset;
		char *fields[40];

		fgets(line,sizeof(line),PROC);

		aoffset = 0;
		for (i=0;i<sizeof(line);i++) {
			char *boffset;

			fields[aoffset++] = line + i;
			boffset = strchr(line + i,'\t');
			if (boffset == NULL) {
				/* Exit loop */
				break;
			} else {
				*boffset = '\0';
				i = boffset - line;
			}
		}

		sscanf(fields[0],"%s",iface);
		sscanf(fields[1],"%x",&dest);
		sscanf(fields[2],"%x",&gateway);
		sscanf(fields[7],"%x",&mask);
#if 0
		printf("Addr: %s %08x Dest: %08x Mask: %08x\n", inet_ntoa(*them), remote_ip, dest, mask);
#endif		
		if (((remote_ip & mask) ^ dest) == 0) {
			if (sipdebug)
				ast_verbose("Interface is %s\n",iface);
			temp = lookup_iface(iface);
			if (sipdebug)
				ast_verbose("IP Address is %s\n",inet_ntoa(*temp));
			break;
		}
	}
	fclose(PROC);
	if (!temp) {
		ast_log(LOG_WARNING, "Couldn't figure out how to get to %s.  Using default\n", inet_ntoa(*them));
 		temp = &__ourip;
 	}
	return temp;
}


static int sip_hangup(struct ast_channel *ast)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int needcancel = 0;
	if (option_debug)
		ast_log(LOG_DEBUG, "sip_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_DEBUG, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_pthread_mutex_lock(&p->lock);
	/* Determine how to disconnect */
	if (p->owner != ast) {
		ast_log(LOG_WARNING, "Huh?  We aren't the owner?\n");
		ast_pthread_mutex_unlock(&p->lock);
		return 0;
	}
	if (!ast || (ast->_state != AST_STATE_UP))
		needcancel = 1;
	/* Disconnect */
	p = ast->pvt->pvt;
	p->owner = NULL;
	ast->pvt->pvt = NULL;

	p->needdestroy = 1;
#if 0
	/* Invert sense of outgoing */
	p->outgoing = 1 - p->outgoing;
#endif	
	/* Start the process if it's not already started */
	if (!p->alreadygone && strlen(p->initreq.data)) {
		if (needcancel) {
			transmit_request(p, "CANCEL", 0);
		} else {
			/* Send a hangup */
			transmit_request(p, "BYE", p->outgoing);
		}
	}
#if 0
	/* Restore sense of outgoing */
	p->outgoing = 1 - p->outgoing;
#endif	
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
}

static int sip_answer(struct ast_channel *ast)
{
	int res = 0;
	struct sip_pvt *p = ast->pvt->pvt;
	if (ast->_state != AST_STATE_UP) {
		ast_setstate(ast, AST_STATE_UP);
		if (option_debug)
			ast_log(LOG_DEBUG, "sip_answer(%s)\n", ast->name);
		res = transmit_response_with_sdp(p, "200 OK", &p->initreq);
	}
	return res;
}

static struct ast_frame  *sip_read(struct ast_channel *ast)
{
	static struct ast_frame f = { AST_FRAME_NULL, };
	ast_log(LOG_DEBUG, "I should never get called but am on %s!\n", ast->name);
	return &f;
}

static int sip_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct sip_pvt *p = ast->pvt->pvt;
	int res = 0;
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE)
			return 0;
		else {
			ast_log(LOG_WARNING, "Can't send %d type frames with SIP write\n", frame->frametype);
			return 0;
		}
	} else {
		if (!(frame->subclass & ast->nativeformats)) {
			ast_log(LOG_WARNING, "Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
				frame->subclass, ast->nativeformats, ast->readformat, ast->writeformat);
			return -1;
		}
	}
	if (p) {
		ast_pthread_mutex_lock(&p->lock);
		if (p->rtp) {
			if ((ast->_state != AST_STATE_UP) && !p->progress && !p->outgoing) {
				transmit_response_with_sdp(p, "183 Session Progress", &p->initreq);
				p->progress = 1;
			}
			res =  ast_rtp_write(p->rtp, frame);
		}
		ast_pthread_mutex_unlock(&p->lock);
	}
	return res;
}

static int sip_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct sip_pvt *p = newchan->pvt->pvt;
	ast_pthread_mutex_lock(&p->lock);
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
		ast_pthread_mutex_unlock(&p->lock);
		return -1;
	}
	p->owner = newchan;
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
}

static int sip_senddigit(struct ast_channel *ast, char digit)
{
	struct sip_pvt *p = ast->pvt->pvt;
	if (p && p->rtp) {
		ast_rtp_senddigit(p->rtp, digit);
		return 0;
	}
	return -1;
}

static int sip_indicate(struct ast_channel *ast, int condition)
{
	struct sip_pvt *p = ast->pvt->pvt;
	switch(condition) {
	case AST_CONTROL_RINGING:
		if (ast->_state == AST_STATE_RING) {
			transmit_response(p, "180 Ringing", &p->initreq);
			break;
		}
		return -1;
	case AST_CONTROL_BUSY:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "600 Busy everywhere", &p->initreq);
			p->alreadygone = 1;
			ast_softhangup(ast, AST_SOFTHANGUP_DEV);
			break;
		}
		return -1;
	case AST_CONTROL_CONGESTION:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "486 Busy here", &p->initreq);
			p->alreadygone = 1;
			ast_softhangup(ast, AST_SOFTHANGUP_DEV);
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


static int sip_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	struct sip_pvt *p0, *p1;
	struct ast_frame *f;
	struct ast_channel *who, *cs[3];
	int to;

	/* if need DTMF, cant native bridge */
	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return -2;
	ast_pthread_mutex_lock(&c0->lock);
	ast_pthread_mutex_lock(&c1->lock);
	p0 = c0->pvt->pvt;
	p1 = c1->pvt->pvt;
	if (!p0->canreinvite || !p1->canreinvite) {
		/* Not gonna support reinvite */
		ast_pthread_mutex_unlock(&c0->lock);
		ast_pthread_mutex_unlock(&c1->lock);
		return -2;
	}
	transmit_reinvite_with_sdp(p0, p1->rtp);
	transmit_reinvite_with_sdp(p1, p0->rtp);
	ast_pthread_mutex_unlock(&c0->lock);
	ast_pthread_mutex_unlock(&c1->lock);
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	for (;;) {
		if ((c0->pvt->pvt != p0)  ||
			(c1->pvt->pvt != p1) ||
			(c0->masq || c0->masqr || c1->masq || c1->masqr)) {
				ast_log(LOG_DEBUG, "Oooh, something is weird, backing out\n");
				if (c0->pvt->pvt == p0)
					transmit_reinvite_with_sdp(p0, NULL);
				if (c1->pvt->pvt == p1)
					transmit_reinvite_with_sdp(p1, NULL);
				/* Tell it to try again later */
				return -3;
		}
		to = -1;
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_DEBUG, "Ooh, empty read...\n");
			continue;
		}
		f = ast_read(who);
		if (!f || ((f->frametype == AST_FRAME_DTMF) &&
				   (((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) || 
			       ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1))))) {
			*fo = f;
			*rc = who;
			ast_log(LOG_DEBUG, "Oooh, got a %s\n", f ? "digit" : "hangup");
			if (c0->pvt->pvt == p0 && !c0->_softhangup)
				transmit_reinvite_with_sdp(p0, NULL);
			if (c1->pvt->pvt == p1 && !c1->_softhangup)
				transmit_reinvite_with_sdp(p1, NULL);
			/* That's all we needed */
			return 0;
		} else 
			ast_frfree(f);
		/* Swap priority not that it's a big deal at this point */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
		
	}
	return -1;
}

static struct ast_channel *sip_new(struct sip_pvt *i, int state)
{
	struct ast_channel *tmp;
	int fmt;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		tmp->nativeformats = i->capability;
		if (!tmp->nativeformats)
			tmp->nativeformats = capability;
		fmt = ast_best_codec(tmp->nativeformats);
		snprintf(tmp->name, sizeof(tmp->name), "SIP/%s:%d", inet_ntoa(i->sa.sin_addr), ntohs(i->sa.sin_port));
		tmp->type = type;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
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
		tmp->pvt->indicate = sip_indicate;
		tmp->pvt->fixup = sip_fixup;
		tmp->pvt->send_digit = sip_senddigit;
		tmp->pvt->bridge = sip_bridge;
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		i->owner = tmp;
		ast_pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, i->exten, sizeof(tmp->exten)-1);
		if (strlen(i->callerid))
			tmp->callerid = strdup(i->callerid);
		tmp->priority = 1;
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

static char *get_sdp(struct sip_request *req, char *name)
{
	int x;
	int len = strlen(name);
	char *r;
	for (x=0;x<req->lines;x++) {
		if (!strncasecmp(req->line[x], name, len) && 
				(req->line[x][len] == '=')) {
					r = req->line[x] + len + 1;
					while(*r && (*r < 33))
							r++;
					return r;
		}
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

static int rtpready(struct ast_rtp *rtp, struct ast_frame *f, void *data)
{
	/* Just deliver the audio directly */
	struct sip_pvt *p = data;
	ast_pthread_mutex_lock(&p->lock);
	if (p->owner) {
		/* Generally, you lock in the order channel lock, followed by private
		   lock.  Since here we are doing the reverse, there is the possibility
		   of deadlock.  As a result, in the case of a deadlock, we simply fail out
		   here. */
		if (!pthread_mutex_trylock(&p->owner->lock)) {
			if (f->frametype == AST_FRAME_VOICE) {
				if (f->subclass != p->owner->nativeformats) {
					ast_log(LOG_DEBUG, "Oooh, format changed to %d\n", f->subclass);
					p->owner->nativeformats = f->subclass;
					ast_set_read_format(p->owner, p->owner->readformat);
					ast_set_write_format(p->owner, p->owner->writeformat);
				}
			}
			ast_queue_frame(p->owner, f, 0);
			pthread_mutex_unlock(&p->owner->lock);
		}
	}
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
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

static struct sip_pvt *sip_alloc(char *callid, struct sockaddr_in *sin)
{
	struct sip_pvt *p;

	p = malloc(sizeof(struct sip_pvt));
	if (!p)
		return NULL;
	/* Keep track of stuff */
	memset(p, 0, sizeof(struct sip_pvt));
	p->initid = -1;
	p->rtp = ast_rtp_new(sched, io);
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
	ast_pthread_mutex_init(&p->lock);
	ast_rtp_set_data(p->rtp, p);
	ast_rtp_set_callback(p->rtp, rtpready);
	if (sin) {
		memcpy(&p->sa, sin, sizeof(p->sa));
		memcpy(&p->ourip, myaddrfor(&p->sa.sin_addr), sizeof(p->ourip));
	} else {
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	}
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=%08x", inet_ntoa(p->ourip), ourport, p->branch);
	if (!callid)
		build_callid(p->callid, sizeof(p->callid), p->ourip);
	else
		strncpy(p->callid, callid, sizeof(p->callid) - 1);
	/* Assume reinvite OK */
	p->canreinvite = 1;
	/* Add to list */
	ast_pthread_mutex_lock(&iflock);
	p->next = iflist;
	iflist = p;
	ast_pthread_mutex_unlock(&iflock);
	if (option_debug)
		ast_log(LOG_DEBUG, "Allocating new SIP call for %s\n", callid);
	return p;
}

static struct sip_pvt *find_call(struct sip_request *req, struct sockaddr_in *sin)
{
	struct sip_pvt *p;
	char *callid;
	callid = get_header(req, "Call-ID");
	if (!strlen(callid)) {
		ast_log(LOG_WARNING, "Call missing call ID from '%s'\n", inet_ntoa(sin->sin_addr));
		return NULL;
	}
	ast_pthread_mutex_lock(&iflock);
	p = iflist;
	while(p) {
		if (!strcmp(p->callid, callid)) {
			/* Found the call */
#if 0
			if (!p->insecure && ((p->sa.sin_addr.s_addr != sin->sin_addr.s_addr) ||
			    (p->sa.sin_port != sin->sin_port))) {
					char orig[80];
					char new[80];
					snprintf(orig, sizeof(orig), "%s:%d", inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
					snprintf(new, sizeof(new), "%s:%d", inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
					ast_log(LOG_WARNING, "Looks like %s is trying to steal call '%s' from %s?\n", new, p->callid, orig);
					ast_pthread_mutex_unlock(&iflock);
					return NULL;
			}
#endif
			ast_pthread_mutex_unlock(&iflock);
			return p;
		}
		p = p->next;
	}
	ast_pthread_mutex_unlock(&iflock);
	return sip_alloc(callid, sin);
}

static int sip_register(char *value, int lineno)
{
	struct sip_registry *reg;
	char copy[256] = "";
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
	porta = strsep(&stringp, ";");
	
	if (porta && !atoi(porta)) {
		ast_log(LOG_WARNING, "%s is not a valid port number at line %d\n", porta, lineno);
		return -1;
	}
	hp = gethostbyname(hostname);
	if (!hp) {
		ast_log(LOG_WARNING, "Host '%s' not found at line %d\n", hostname, lineno);
		return -1;
	}
	reg = malloc(sizeof(struct sip_registry));
	if (reg) {
		memset(reg, 0, sizeof(struct sip_registry));
		strncpy(reg->username, username, sizeof(reg->username)-1);
		if (secret)
			strncpy(reg->secret, secret, sizeof(reg->secret)-1);
		reg->expire = -1;
		reg->refresh = DEFAULT_EXPIREY;
		reg->addr.sin_family = AF_INET;
		memcpy(&reg->addr.sin_addr, hp->h_addr, sizeof(&reg->addr.sin_addr));
		reg->addr.sin_port = porta ? htons(atoi(porta)) : htons(DEFAULT_SIP_PORT);
		reg->next = registrations;
		reg->callid_valid = 0;
		registrations = reg;
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	return 0;
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
			if (!strlen(req->header[f])) {
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
	if (strlen(req->header[f])) 
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
	if (strlen(req->line[f])) 
		f++;
	req->lines = f;
	if (sipdebug)
		ast_verbose("%d headers, %d lines\n", req->headers, req->lines);
	if (*c) 
		ast_log(LOG_WARNING, "Odd content, extra stuff left over ('%s')\n", c);
}

static int process_sdp(struct sip_pvt *p, struct sip_request *req)
{
	char *m;
	char *c;
	char host[258];
	int len = -1;
	int portno;
	int peercapability;
	struct sockaddr_in sin;
	char *codecs;
	struct hostent *hp;
	int codec;
	/* Get codec and RTP info from SDP */
	if (strcasecmp(get_header(req, "Content-Type"), "application/sdp")) {
		ast_log(LOG_NOTICE, "Content is '%s', not 'application/sdp'\n", get_header(req, "Content-Type"));
		return -1;
	}
	m = get_sdp(req, "m");
	c = get_sdp(req, "c");
	if (!strlen(m) || !strlen(c)) {
		ast_log(LOG_WARNING, "Insufficient information for SDP (m = '%s', c = '%s')\n", m, c);
		return -1;
	}
	if (sscanf(c, "IN IP4 %256s", host) != 1) {
		ast_log(LOG_WARNING, "Invalid host in c= line, '%s'\n", c);
		return -1;
	}
	/* XXX This could block for a long time, and block the main thread! XXX */
	hp = gethostbyname(host);
	if (!hp) {
		ast_log(LOG_WARNING, "Unable to lookup host in c= line, '%s'\n", c);
		return -1;
	}
	if ((sscanf(m, "audio %d RTP/AVP %n", &portno, &len) != 1) || (len < 0)) {
		ast_log(LOG_WARNING, "Unable to determine port number for RTP in '%s'\n", m); 
		return -1;
	}
	sin.sin_family = AF_INET;
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	sin.sin_port = htons(portno);
	if (p->rtp)
		ast_rtp_set_peer(p->rtp, &sin);
#if 0
	printf("Peer RTP is at port %s:%d\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
#endif	
	peercapability = 0;
	codecs = m + len;
	while(strlen(codecs)) {
		if (sscanf(codecs, "%d %n", &codec, &len) != 1) {
			ast_log(LOG_WARNING, "Error in codec string '%s'\n", codecs);
			return -1;
		}
#if 0
		printf("Codec: %d\n", codec);
#endif		
		codec = rtp2ast(codec);
		if (codec  > -1)
			peercapability |= codec;
		codecs += len;
	}
	p->capability = capability & peercapability;
	if (sipdebug)
		ast_verbose("Capabilities: us - %d, them - %d, combined - %d\n",
		capability, peercapability, p->capability);
	if (!p->capability) {
		ast_log(LOG_WARNING, "No compatible codecs!\n");
		return -1;
	}
	if (p->owner && (p->owner->nativeformats != p->capability)) {
		ast_log(LOG_DEBUG, "Oooh, we need to change our formats since our peer supports only %d\n", p->capability);
		p->owner->nativeformats = p->capability;
		ast_set_read_format(p->owner, p->owner->readformat);
		ast_set_write_format(p->owner, p->owner->writeformat);
	}
	return 0;
	
}

static int add_header(struct sip_request *req, char *var, char *value)
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
	if (strlen(tmp)) {
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
		if (strlen(tmp)) {
			/* Add what we're responding to */
			add_header(req, field, tmp);
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
	copy_all_header(resp, req, "Via");
	copy_header(resp, req, "From");
	ot = get_header(req, "To");
	if (!strstr(ot, "tag=")) {
		/* Add the proper tag if we don't have it already.  If they have specified
		   their tag, use it.  Otherwise, use our own tag */
		if (strlen(p->theirtag))
			snprintf(newto, sizeof(newto), "%s;tag=%s", ot, p->theirtag);
		else if (p->tag)
			snprintf(newto, sizeof(newto), "%s;tag=%08x", ot, p->tag);
		else
			strncpy(newto, ot, sizeof(newto) - 1);
		ot = newto;
	}
	add_header(resp, "To", ot);
	copy_header(resp, req, "Call-ID");
	copy_header(resp, req, "CSeq");
	add_header(resp, "User-Agent", "Asterisk PBX");
	if (p->expirey) {
		/* For registration responses, we also need expirey and
		   contact info */
		char tmp[80];
		char contact2[256] = "", *c, contact[256];
		snprintf(tmp, sizeof(tmp), "%d", p->expirey);
		strncpy(contact2, get_header(req, "Contact"), sizeof(contact2)-1);
		c = ditch_braces(contact2);
		snprintf(contact, sizeof(contact), "<%s>", c);
		add_header(resp, "Expires", tmp);
		add_header(resp, "Contact", contact);
	} else {
		char contact2[256] = "", *c, contact[256];
		/* XXX This isn't exactly right and it's implemented
		       very stupidly *sigh* XXX */
		strncpy(contact2, get_header(req, "To"), sizeof(contact2)-1);
		c = ditch_braces(contact2);
		snprintf(contact, sizeof(contact), "<%s>", c);
		add_header(resp, "Contact", contact);
	}
	return 0;
}

static int reqprep(struct sip_request *req, struct sip_pvt *p, char *msg, int inc)
{
	struct sip_request *orig = &p->initreq;
	char stripped[80] ="";
	char tmp[80];
	char newto[256];
	char *c, *n;
	char *ot, *of;

	memset(req, 0, sizeof(struct sip_request));
	
	if (inc)
		p->ocseq++;

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
	
	init_req(req, msg, c);

	snprintf(tmp, sizeof(tmp), "%d %s", p->ocseq, msg);

	add_header(req, "Via", p->via);

	ot = get_header(orig, "To");
	of = get_header(orig, "From");

	if (!strstr(ot, "tag=")) {
		/* Add the proper tag if we don't have it already.  If they have specified
		   their tag, use it.  Otherwise, use our own tag */
		if (strlen(p->theirtag))
			snprintf(newto, sizeof(newto), "%s;tag=%s", ot, p->theirtag);
		else
			snprintf(newto, sizeof(newto), "%s;tag=%08x", ot, p->tag);
		ot = newto;
	}

	if (p->outgoing) {
		add_header(req, "From", of);
		add_header(req, "To", ot);
	} else {
		add_header(req, "From", ot);
		add_header(req, "To", of);
	}
	copy_header(req, orig, "Call-ID");
	add_header(req, "CSeq", tmp);

	add_header(req, "User-Agent", "Asterisk PBX");
	return 0;
}

static int transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req)
{
	struct sip_request resp;
	respprep(&resp, p, msg, req);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp);
}

static int transmit_response_with_allow(struct sip_pvt *p, char *msg, struct sip_request *req)
{
	struct sip_request resp;
	respprep(&resp, p, msg, req);
	add_header(&resp, "Allow", "INVITE, ACK, CANCEL, OPTIONS, BYE, REFER");
	add_header(&resp, "Accept", "application/sdp");
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp);
}

static int transmit_response_with_auth(struct sip_pvt *p, char *msg, struct sip_request *req, char *randdata)
{
	struct sip_request resp;
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "DIGEST realm=\"asterisk\", nonce=\"%s\"", randdata);
	respprep(&resp, p, msg, req);
	add_header(&resp, "Proxy-Authenticate", tmp);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_response(p, &resp);
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

static int add_sdp(struct sip_request *resp, struct sip_pvt *p, struct ast_rtp *rtp)
{
	int len;
	int codec;
	char costr[80];
	struct sockaddr_in sin;
	char v[256];
	char s[256];
	char o[256];
	char c[256];
	char t[256];
	char m[256];
	char a[1024] = "";
	int x;
	struct sockaddr_in dest;
	/* XXX We break with the "recommendation" and send our IP, in order that our
	       peer doesn't have to gethostbyname() us XXX */
	len = 0;
	if (!p->rtp) {
		ast_log(LOG_WARNING, "No way to add SDP without an RTP structure\n");
		return -1;
	}
	ast_rtp_get_us(p->rtp, &sin);
	if (rtp) {
		ast_rtp_get_peer(rtp, &dest);
	} else {
		dest.sin_addr = p->ourip;
		dest.sin_port = sin.sin_port;
	}
	if (sipdebug)
		ast_verbose("We're at %s port %d\n", inet_ntoa(p->ourip), ntohs(sin.sin_port));	
	snprintf(v, sizeof(v), "v=0\r\n");
	snprintf(o, sizeof(o), "o=root %d %d IN IP4 %s\r\n", getpid(), getpid(), inet_ntoa(dest.sin_addr));
	snprintf(s, sizeof(s), "s=session\r\n");
	snprintf(c, sizeof(c), "c=IN IP4 %s\r\n", inet_ntoa(dest.sin_addr));
	snprintf(t, sizeof(t), "t=0 0\r\n");
	snprintf(m, sizeof(m), "m=audio %d RTP/AVP", ntohs(dest.sin_port));
	for (x=1;x<= AST_FORMAT_MAX_AUDIO; x <<= 1) {
		if (p->capability & x) {
			if (sipdebug)
				ast_verbose("Answering with capability %d\n", x);
			if ((codec = ast2rtp(x)) > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				strcat(m, costr);
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast2rtpn(x));
				strcat(a, costr);
			}
		}
	}
	strcat(m, " 101\r\n");
	strcat(a, "a=rtpmap:101 telephone-event/8000\r\n");
	/* Indicate we support DTMF only...  Not sure about 16, but MSN supports it so dang it, we will too... */
	strcat(a, "a=fmtp:101 0-16\r\n");
	len = strlen(v) + strlen(s) + strlen(o) + strlen(c) + strlen(t) + strlen(m) + strlen(a);
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

static int transmit_response_with_sdp(struct sip_pvt *p, char *msg, struct sip_request *req)
{
	struct sip_request resp;
	respprep(&resp, p, msg, req);
	add_sdp(&resp, p, NULL);
	return send_response(p, &resp);
}

static int transmit_reinvite_with_sdp(struct sip_pvt *p, struct ast_rtp *rtp)
{
	struct sip_request resp;
	reqprep(&resp, p, "INVITE", 1);
	add_sdp(&resp, p, rtp);
	return send_response(p, &resp);
}

static int transmit_invite(struct sip_pvt *p, char *cmd, int sdp, char *auth, char *vxml_url)
{
	struct sip_request req;
	char invite[256];
	char from[256];
	char to[256];
	char tmp[80];
	char cid[256];
	char *l = "asterisk", *n=NULL;
	if (p->owner && p->owner->callerid) {
		strcpy(cid, p->owner->callerid);
		ast_callerid_parse(cid, &n, &l);
		if (l) 
			ast_shrink_phone_number(l);
		if (!l || !ast_isphonenumber(l))
				l = "asterisk";
	}
	if (!n)
		n = "asterisk";
	snprintf(from, sizeof(from), "\"%s\" <sip:%s@%s>;tag=%08x", n, l, inet_ntoa(p->ourip), p->tag);
	if (strlen(p->username)) {
		if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
			snprintf(invite, sizeof(invite), "sip:%s@%s:%d",p->username, inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
		} else {
			snprintf(invite, sizeof(invite), "sip:%s@%s",p->username, inet_ntoa(p->sa.sin_addr));
		}
	} else if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
		snprintf(invite, sizeof(invite), "sip:%s:%d", inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	} else {
		snprintf(invite, sizeof(invite), "sip:%s", inet_ntoa(p->sa.sin_addr));
	}
	/* If there is a VXML URL append it to the SIP URL */
	if (vxml_url)
	{
		snprintf(to, sizeof(to), "<%s>;%s", invite, vxml_url);
	}
	else
	{
		snprintf(to, sizeof(to), "<%s>", invite );
	}
	memset(&req, 0, sizeof(req));
	init_req(&req, cmd, invite);
	snprintf(tmp, sizeof(tmp), "%d %s", ++p->ocseq, cmd);

	add_header(&req, "Via", p->via);
	add_header(&req, "From", from);
	{
		char contact2[256] ="", *c, contact[256];
		/* XXX This isn't exactly right and it's implemented
		       very stupidly *sigh* XXX */
		strncpy(contact2, from, sizeof(contact2)-1);
		c = ditch_braces(contact2);
		snprintf(contact, sizeof(contact), "<%s>", c);
		add_header(&req, "Contact", contact);
	}
	add_header(&req, "To", to);
	add_header(&req, "Call-ID", p->callid);
	add_header(&req, "CSeq", tmp);
	add_header(&req, "User-Agent", "Asterisk PBX");
	if (auth)
		add_header(&req, "Proxy-Authorization", auth);
	if (sdp) {
		add_sdp(&req, p, NULL);
	} else {
		add_header(&req, "Content-Length", "0");
		add_blank_header(&req);
	}
	if (!p->initreq.headers) {
		/* Use this as the basis */
		copy_request(&p->initreq, &req);
		parse(&p->initreq);
	}
	p->lastinvite = p->ocseq;
	return send_request(p, &req);
}

static int transmit_register(struct sip_registry *r, char *cmd, char *auth);

static int sip_reregister(void *data) 
{
	/* if we are here, we know that we need to reregister. */
	struct sip_registry *r=(struct sip_registry *)data;
	return sip_do_register(r);
	
}


static int sip_do_register(struct sip_registry *r)
{
	int res;
	ast_pthread_mutex_lock(&r->lock);
	res=transmit_register(r, "REGISTER", NULL);
	ast_pthread_mutex_unlock(&r->lock);
	return res;
}

static int sip_reg_timeout(void *data)
{
	/* if we are here, our registration timed out, so we'll just do it over */
	struct sip_registry *r=data;
	int res;
	ast_pthread_mutex_lock(&r->lock);
	ast_log(LOG_NOTICE, "Registration timed out, trying again\n"); 
	r->regstate=REG_STATE_UNREGISTERED;
	/* cancel ourselves first!!! */
	/* ast_sched_del(sched,r->timeout); */
	res=transmit_register(r, "REGISTER", NULL);
	ast_pthread_mutex_unlock(&r->lock);
	return res;
}

static int transmit_register(struct sip_registry *r, char *cmd, char *auth)
{
	struct sip_request req;
	char from[256];
	char to[256];
	char tmp[80];
	char via[80];
	char addr[80];
	struct sip_pvt *p;
	/* exit if we are already in process with this registrar ?*/
	if ( (auth==NULL && r->regstate==REG_STATE_REGSENT) || r->regstate==REG_STATE_AUTHSENT) {
		ast_log(LOG_NOTICE, "Strange, trying to register when registration already pending\n");
		return 0;
	}


	if (!(p=r->call)) {
		if (!r->callid_valid) {
		  build_callid(r->callid, sizeof(r->callid), __ourip);
		  r->callid_valid=1;
		}
		p=sip_alloc( r->callid, &r->addr );
		p->outgoing = 1;
		r->call=p;
		p->registry=r;
		strncpy(p->peersecret, r->secret, sizeof(p->peersecret)-1);
		strncpy(p->peername, r->username, sizeof(p->peername)-1);
		strncpy(p->username, r->username, sizeof(p->username)-1);
	}

	/* set up a timeout */
	if (auth==NULL && !r->timeout)  {
		r->timeout = ast_sched_add(sched, 10*1000, sip_reg_timeout, r);
		ast_log(LOG_NOTICE, "Scheduled a timeout # %d\n", r->timeout);
	}

	snprintf(from, sizeof(from), "<sip:%s@%s>;tag=%08x", r->username, inet_ntoa(r->addr.sin_addr), p->tag);
	snprintf(to, sizeof(to),     "<sip:%s@%s>;tag=%08x", r->username, inet_ntoa(r->addr.sin_addr), p->tag);
	
	snprintf(addr, sizeof(addr), "sip:%s", inet_ntoa(r->addr.sin_addr));

	memset(&req, 0, sizeof(req));
	init_req(&req, cmd, addr);

	snprintf(tmp, sizeof(tmp), "%d %s", ++p->ocseq, cmd);

	snprintf(via, sizeof(via), "SIP/2.0/UDP %s:%d;branch=%08x", inet_ntoa(p->ourip), ourport, p->branch);
	add_header(&req, "Via", via);
	add_header(&req, "From", from);
	add_header(&req, "To", to);
	{
		char contact[256];
		snprintf(contact, sizeof(contact), "<sip:s@%s:%d;transport=udp>", inet_ntoa(p->ourip), ourport);
		add_header(&req, "Contact", contact);
	}
	add_header(&req, "Call-ID", p->callid);
	add_header(&req, "CSeq", tmp);
	add_header(&req, "User-Agent", "Asterisk PBX");
	if (auth) 
		add_header(&req, "Authorization", auth);
#define EXPIRE_TIMEOUT "Thu, 01 Dec 2003 16:00:00 GMT"


	add_header(&req, "expires", EXPIRE_TIMEOUT);
	add_header(&req, "Event", "registration");
	copy_request(&p->initreq, &req);
	r->regstate=auth?REG_STATE_AUTHSENT:REG_STATE_REGSENT;
	return send_request(p, &req);
}

static int transmit_message_with_text(struct sip_pvt *p, char *text)
{
	struct sip_request req;
	reqprep(&req, p, "MESSAGE", 1);
	add_text(&req, text);
	return send_request(p, &req);
}

static int transmit_request(struct sip_pvt *p, char *msg, int inc)
{
	struct sip_request resp;
	reqprep(&resp, p, msg, inc);
	add_header(&resp, "Content-Length", "0");
	add_blank_header(&resp);
	return send_request(p, &resp);
}

static int expire_register(void *data)
{
	struct sip_peer *p = data;
	memset(&p->addr, 0, sizeof(p->addr));
	p->expire = -1;
	return 0;
}

static int sip_poke_peer(struct sip_peer *peer);

static int parse_contact(struct sip_pvt *pvt, struct sip_peer *p, struct sip_request *req)
{
	char contact[80]= ""; 
	char *expires = get_header(req, "Expires");
	int expirey = atoi(expires);
	char *c, *n, *pt;
	int port;
	struct hostent *hp;
	struct sockaddr_in oldsin;
	if (!strlen(expires)) {
		expires = strstr(get_header(req, "Contact"), "expires=");
		if (expires) 
			if (sscanf(expires + 8, "%d;", &expirey) != 1)
				expirey = 0;
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
	/* Make sure it's a SIP URL */
	if (strncasecmp(c, "sip:", 4)) {
		ast_log(LOG_NOTICE, "'%s' is not a valid SIP contcact\n", c);
		return -1;
	}
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
	/* XXX This could block for a long time XXX */
	hp = gethostbyname(n);
	if (!hp)  {
		ast_log(LOG_WARNING, "Invalid host '%s'\n", n);
		return -1;
	}
	memcpy(&oldsin, &p->addr, sizeof(oldsin));
	p->addr.sin_family = AF_INET;
	memcpy(&p->addr.sin_addr, hp->h_addr, sizeof(p->addr.sin_addr));
	p->addr.sin_port = htons(port);
	if (c)
		strncpy(p->username, c, sizeof(p->username) - 1);
	else
		strcpy(p->username, "");
	if (p->mailbox)
		strncpy(pvt->mailbox, p->mailbox,sizeof(pvt->mailbox)-1);
	if (p->expire > -1)
		ast_sched_del(sched, p->expire);
	if ((expirey < 1) || (expirey > MAX_EXPIREY))
		expirey = DEFAULT_EXPIREY;
	p->expire = ast_sched_add(sched, expirey * 1000, expire_register, p);
	pvt->expirey = expirey;
	if (memcmp(&p->addr, &oldsin, sizeof(oldsin))) {
		sip_poke_peer(p);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Registered SIP '%s' at %s port %d expires %d\n", p->username, inet_ntoa(p->addr.sin_addr), ntohs(p->addr.sin_port), expirey);
	}
	return 0;
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

static int check_auth(struct sip_pvt *p, struct sip_request *req, char *randdata, int randlen, char *username, char *secret, char *method, char *uri)
{
	int res = -1;
	/* Always OK if no secret */
	if (!strlen(secret))
		return 0;
	if (!strlen(randdata)) {
		snprintf(randdata, randlen, "%08x", rand());
		transmit_response_with_auth(p, "407 Proxy Authentication Required", req, randdata);
		res = 1;
	} else {
		/* Whoever came up with the authentication section of SIP can suck my %&#$&* for not putting
		   an example in the spec of just what it is you're doing a hash on. */
		char a1[256];
		char a2[256];
		char a1_hash[256];
		char a2_hash[256];
		char resp[256];
		char resp_hash[256];
		char tmp[256] = "";
		char *c;
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
		                c = strchr(c, ',');
			if (c)
				c++;
		}
		snprintf(a1, sizeof(a1), "%s:%s:%s", username, "asterisk", secret);
		if(strlen(resp_uri))
			snprintf(a2, sizeof(a2), "%s:%s", method, resp_uri);
		else
			snprintf(a2, sizeof(a2), "%s:%s", method, uri);
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
		/* Eliminate random data */
		strcpy(randdata, "");
	}
	return res;
}

static int register_verify(struct sip_pvt *p, struct sockaddr_in *sin, struct sip_request *req, char *uri)
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
	if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_NOTICE, "Invalid to address: '%s' from %s\n", tmp, inet_ntoa(sin->sin_addr));
		return -1;
	}
	name = c + 4;
	c = strchr(name, '@');
	if (c) 
		*c = '\0';
	ast_pthread_mutex_lock(&peerl.lock);
	peer = peerl.peers;
	while(peer) {
		if (!strcasecmp(peer->name, name) && peer->dynamic) {
			if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), peer->name, peer->secret, "REGISTER", uri))) {
				if (parse_contact(p, peer, req)) {
					ast_log(LOG_WARNING, "Failed to parse contact info\n");
				} else {
					transmit_response(p, "200 OK", req);
					res = 0;
				}
			} 
			break;
		}	
		peer = peer->next;
	}
	ast_pthread_mutex_unlock(&peerl.lock);
	if (res < 0)
		transmit_response(p, "401 Unauthorized", &p->initreq);
	return res;
}

static int get_destination(struct sip_pvt *p, struct sip_request *oreq)
{
	char tmp[256] = "", *c, *a;
	struct sip_request *req;
	
	req = oreq;
	if (!req)
		req = &p->initreq;
	strncpy(tmp, req->rlPart2, sizeof(tmp) - 1);
	c = ditch_braces(tmp);
	if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return -1;
	}
	c += 4;
	if ((a = strchr(c, '@')) || (a = strchr(c, ';'))) {
		*a = '\0';
	}
	if (sipdebug)
		ast_verbose("Looking for %s in %s\n", c, p->context);
	if (ast_exists_extension(NULL, p->context, c, 1, NULL)) {
		if (!oreq)
			strncpy(p->exten, c, sizeof(p->exten) - 1);
		return 0;
	}

	if (ast_canmatch_extension(NULL, p->context, c, 1, NULL)) {
		return 1;
	}
	
	return -1;
}

static int get_refer_info(struct sip_pvt *p, struct sip_request *oreq)
{
	char tmp[256] = "", *c, *a;
	char tmp2[256] = "", *c2, *a2;
	char tmp3[256];
	char tmp4[256];
	struct sip_request *req;
	
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
	if ((a = strchr(c, '@')) || (a = strchr(c, ';'))) {
		*a = '\0';
	}
	if ((a2 = strchr(c2, '@')) || (a2 = strchr(c2, ';'))) {	
		*a2 = '\0';
	}
	
	if (sipdebug)
		ast_verbose("Looking for %s in %s\n", c, p->context);
		ast_verbose("Looking for %s in %s\n", c2, p->context);
	
	if (ast_exists_extension(NULL, p->context, c, 1, NULL) && ast_exists_extension(NULL, p->context, c2, 1, NULL)) {
		if (!oreq)
			ast_log(LOG_DEBUG,"Something is wrong with this line.\n");	//This line is ignored for some reason....
			ast_log(LOG_DEBUG,"Assigning Extension %s to REFER-TO\n", c);
			ast_log(LOG_DEBUG,"Assigning Extension %s to REFERRED-BY\n", c2);
			ast_log(LOG_DEBUG,"Assigning Contact Info %s to REFER_CONTACT\n", tmp3);
			ast_log(LOG_DEBUG,"Assigning Remote-Party-ID Info %s to REMOTE_PARTY_ID\n",tmp4);
			strncpy(p->refer_to, c, sizeof(p->refer_to) - 1);
			strncpy(p->referred_by, c2, sizeof(p->referred_by) - 1);
			strncpy(p->refer_contact, tmp3, sizeof(p->refer_contact) - 1);
			strncpy(p->remote_party_id, tmp4, sizeof(p->remote_party_id) - 1);
			return 0;
	}

	if (ast_canmatch_extension(NULL, p->context, c, 1, NULL)) {
		return 1;
	}

	return -1;
}


static int check_via(struct sip_pvt *p, struct sip_request *req)
{
	char via[256] = "";
	char *c, *pt;
	struct hostent *hp;

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
		hp = gethostbyname(c);
		if (!hp) {
			ast_log(LOG_WARNING, "'%s' is not a valid host\n", c);
			return -1;
		}
		memset(&p->sa, 0, sizeof(p->sa));
		p->sa.sin_family = AF_INET;
		p->sa.sin_port = htons(pt ? atoi(pt) : DEFAULT_SIP_PORT);
		memcpy(&p->sa.sin_addr, hp->h_addr, sizeof(p->sa.sin_addr));
		if (sipdebug)
			ast_verbose("Sending to %s : %d\n", inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	}
	return 0;
}

static int check_user(struct sip_pvt *p, struct sip_request *req, char *cmd, char *uri)
{
	struct sip_user *user;
	char *of, from[256] = "", *c;
	int res = 0;
	char *t;
	/* Terminate URI */
	t = uri;
	while(*t && (*t > 32) && (*t != ';'))
		t++;
	*t = '\0';
	of = get_header(req, "From");
	strncpy(from, of, sizeof(from) - 1);
	of = ditch_braces(from);
	if (strncmp(of, "sip:", 4))
		return 0;
	else
		of += 4;
	/* Get just the username part */
	if ((c = strchr(of, '@')))
		*c = '\0';
	if ((c = strchr(of, ':')))
		*c = '\0';
	strncpy(p->callerid, of, sizeof(p->callerid) - 1);
	if (!strlen(of))
			return 0;
	ast_pthread_mutex_lock(&userl.lock);
	user = userl.users;
	while(user) {
		if (!strcasecmp(user->name, of)) {
			if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), user->name, user->secret, cmd, uri))) {
				strncpy(p->context, user->context, sizeof(p->context) - 1);
				strncpy(p->mailbox, user->mailbox, sizeof(p->mailbox) - 1);
				if (strlen(user->callerid) && strlen(p->callerid)) 
					strncpy(p->callerid, user->callerid, sizeof(p->callerid) - 1);
				strncpy(p->accountcode, user->accountcode, sizeof(p->accountcode)  -1);
				p->canreinvite = user->canreinvite;
				p->amaflags = user->amaflags;
			}
			break;
		}
		user = user->next;
	}
	ast_pthread_mutex_unlock(&userl.lock);
	return res;
}

static int get_msg_text(char *buf, int len, struct sip_request *req)
{
	int x;
	strcpy(buf, "");
	for (x=0;x<req->lines;x++) {
		strncat(buf, req->line[x], len - strlen(buf) - 5);
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
		if (sipdebug)
			ast_verbose("Message received: '%s'\n", buf);
		memset(&f, 0, sizeof(f));
		f.frametype = AST_FRAME_TEXT;
		f.subclass = 0;
		f.offset = 0;
		f.data = buf;
		f.datalen = strlen(buf);
		ast_queue_frame(p->owner, &f, 1);
	}
}

static int sip_show_users(int fd, int argc, char *argv[])
{
#define FORMAT "%-15.15s  %-15.15s  %-15.15s  %-15.15s  %-5.5s\n"
	struct sip_user *user;
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&userl.lock);
	ast_cli(fd, FORMAT, "Username", "Secret", "Authen", "Def.Context", "A/C");
	for(user=userl.users;user;user=user->next) {
		ast_cli(fd, FORMAT, user->name, user->secret, user->methods, 
				user->context,
				user->ha ? "Yes" : "No");
	}
	ast_pthread_mutex_unlock(&userl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
}

static int sip_show_peers(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-15.15s %s  %-15.15s  %-8s %-10s\n"
#define FORMAT "%-15.15s  %-15.15s %s  %-15.15s  %-8d %-10s\n"
	struct sip_peer *peer;
	char name[256] = "";
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&peerl.lock);
	ast_cli(fd, FORMAT2, "Name/username", "Host", "   ", "Mask", "Port", "Status");
	for (peer = peerl.peers;peer;peer = peer->next) {
		char nm[20] = "";
		char status[20];
		strncpy(nm, inet_ntoa(peer->mask), sizeof(nm)-1);
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
		ast_cli(fd, FORMAT, name, 
					peer->addr.sin_addr.s_addr ? inet_ntoa(peer->addr.sin_addr) : "(Unspecified)",
					peer->dynamic ? "(D)" : "   ",
					nm,
					ntohs(peer->addr.sin_port), status);
	}
	ast_pthread_mutex_unlock(&peerl.lock);
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
#define FORMAT2 "%-20.20s  %-10.10s  %-20.20s %8.8s  %s\n"
#define FORMAT "%-20.20s  %-10.10s  %-20.20s %8d  %s\n"
	struct sip_registry *reg;
	char host[80];
	char state[20];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&peerl.lock);
	ast_cli(fd, FORMAT2, "Host", "Username", "Refresh", "State");
	for (reg = registrations;reg;reg = reg->next) {
		snprintf(host, sizeof(host), "%s:%d", inet_ntoa(reg->addr.sin_addr), ntohs(reg->addr.sin_port));
		snprintf(state, sizeof(state), "%s", regstate2str(reg->regstate));
		ast_cli(fd, FORMAT, host, 
					reg->username, state, reg->refresh, regstate2str(reg->regstate));
	}
	ast_pthread_mutex_unlock(&peerl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int sip_show_channels(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-10.10s  %-11.11s  %-11.11s  %-7.7s  %-6.6s  %s\n"
#define FORMAT  "%-15.15s  %-10.10s  %-11.11s  %5.5d/%5.5d  %-5.5dms  %-4.4dms  %d\n"
	struct sip_pvt *cur;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&iflock);
	cur = iflist;
	ast_cli(fd, FORMAT2, "Peer", "Username", "Call ID", "Seq (Tx/Rx)", "Lag", "Jitter", "Format");
	while (cur) {
			ast_cli(fd, FORMAT, inet_ntoa(cur->sa.sin_addr), 
						strlen(cur->username) ? cur->username : "(None)", 
						cur->callid, 
						cur->ocseq, cur->icseq, 
						0,
						0,
						cur->owner ? cur->owner->nativeformats : 0);
		cur = cur->next;
	}
	ast_pthread_mutex_unlock(&iflock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static void receive_info(struct sip_pvt *p, struct sip_request *req)
{
	char buf[1024] = "";
	struct ast_frame f;
	char *c;
	/* Try getting the "signal=" part */
	if ((c = get_sdp(req, "Signal"))) {
		strncpy(buf, c, sizeof(buf) - 1);
	} else if (get_msg_text(buf, sizeof(buf), req)) {
		/* Normal INFO method */
		ast_log(LOG_WARNING, "Unable to retrieve text from %s\n", p->callid);
		return;
	}
	
	if (p->owner) {
		if (strlen(buf)) {
			if (sipdebug)
				ast_verbose("DTMF received: '%c'\n", buf[0]);
			memset(&f, 0, sizeof(f));
			f.frametype = AST_FRAME_DTMF;
			f.subclass = buf[0];
			f.offset = 0;
			f.data = NULL;
			f.datalen = 0;
			ast_queue_frame(p->owner, &f, 1);
		}
	}
}

static int sip_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	sipdebug = 1;
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

static int reply_digest(struct sip_pvt *p, struct sip_request *req, char *header, char *orig_header, char *digest, int digest_len);

static int do_register_auth(struct sip_pvt *p, struct sip_request *req) {
	char digest[256];
	memset(digest,0,sizeof(digest));
	reply_digest(p,req, "WWW-Authenticate", "REGISTER", (char *)&digest, sizeof(digest) );
	return transmit_register(p->registry,"REGISTER",(char *)&digest); 
}

static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req) {
	char digest[256];
	memset(digest,0,sizeof(digest));
	reply_digest(p,req, "Proxy-Authenticate", "INVITE", (char *)&digest, sizeof(digest) );
	return transmit_invite(p,"INVITE",1,(char *)&digest, NULL); 
}

static int reply_digest(struct sip_pvt *p, struct sip_request *req, char *header, char *orig_header, char *digest, int digest_len) {

	char tmp[256] = "";
	char *realm = "";
	char *nonce = "";
	char *c;
	char a1[256];
	char a2[256];
	char a1_hash[256];
	char a2_hash[256];
	char resp[256];
	char resp_hash[256];
	char uri[256] = "";


	strncpy(tmp, get_header(req, header),sizeof(tmp) - 1);
	c = tmp;
	c+=strlen("DIGEST ");
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
			} else
				c = strchr(c,',');
			if (c)
				c++;
			}

	/* Okay.  We've got the realm and nonce from the server.  Now lets build the MD5 digest. */
	snprintf(uri, sizeof(uri), "sip:%s@%s",p->username, inet_ntoa(p->sa.sin_addr));

	snprintf(a1,sizeof(a1),"%s:%s:%s",p->peername,realm,p->peersecret);
	snprintf(a2,sizeof(a2),"%s:%s",orig_header,uri);
	md5_hash(a1_hash,a1);
	md5_hash(a2_hash,a2);
	snprintf(resp,sizeof(resp),"%s:%s:%s",a1_hash,nonce,a2_hash);
	md5_hash(resp_hash,resp);

	snprintf(digest,digest_len,"DIGEST username=\"%s\", realm=\"%s\", algorithm=\"MD5\", uri=\"%s\", nonce=\"%s\", response=\"%s\"",p->peername,realm,uri,nonce,resp_hash);

	return 0;
}
	

	
	


static char show_users_usage[] = 
"Usage: sip show users\n"
"       Lists all users known to the SIP (Session Initiation Protocol) subsystem.\n";

static char show_channels_usage[] = 
"Usage: sip show channels\n"
"       Lists all currently active SIP channels.\n";

static char show_peers_usage[] = 
"Usage: sip show peers\n"
"       Lists all known SIP peers.\n";

static char show_reg_usage[] =
"Usage: sip show registry\n"
"       Lists all registration requests and status.\n";

static char debug_usage[] = 
"Usage: sip debug\n"
"       Enables dumping of SIP packets for debugging purposes\n";

static char no_debug_usage[] = 
"Usage: sip no debug\n"
"       Disables dumping of SIP packets for debugging purposes\n";

static struct ast_cli_entry  cli_show_users = 
	{ { "sip", "show", "users", NULL }, sip_show_users, "Show defined SIP users", show_users_usage };
static struct ast_cli_entry  cli_show_channels =
	{ { "sip", "show", "channels", NULL }, sip_show_channels, "Show active SIP channels", show_channels_usage };
static struct ast_cli_entry  cli_show_peers =
	{ { "sip", "show", "peers", NULL }, sip_show_peers, "Show defined SIP peers", show_peers_usage };
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

static void handle_response(struct sip_pvt *p, int resp, char *rest, struct sip_request *req)
{
	char *to;
	char *msg, *c;
	struct ast_rtp *rtp;
	struct ast_channel *owner;
	struct sip_peer *peer;
	int pingtime;
	struct timeval tv;
	c = get_header(req, "Cseq");
	msg = strchr(c, ' ');
	if (!msg) msg = ""; else msg++;
retrylock:
	ast_pthread_mutex_lock(&p->lock);
	/* Go ahead and lock the owner if it has one -- we may need it */
	if (p->owner && pthread_mutex_trylock(&p->owner->lock)) {
		ast_log(LOG_DEBUG, "Failed to grab lock, trying again...\n");
		ast_pthread_mutex_unlock(&p->lock);
		/* Sleep infintismly short amount of time */
		usleep(1);
		goto retrylock;
	}
	owner = p->owner;
	if (p->peerpoke) {
		/* We don't really care what the response is, just that it replied back. 
		   Well, as long as it's not a 100 response...  since we might
		   need to hang around for something more "difinitive" */
		if (resp != 100) {
			peer = p->peerpoke;
			gettimeofday(&tv, NULL);
			pingtime = (tv.tv_sec - peer->ps.tv_sec) * 1000 +
						(tv.tv_usec - peer->ps.tv_usec) / 1000;
			if (pingtime < 1)
				pingtime = 1;
			if ((peer->lastms < 0)  || (peer->lastms > peer->maxms)) {
				if (pingtime <= peer->maxms)
				ast_log(LOG_NOTICE, "Peer '%s' is now REACHABLE!\n", peer->name);
			} else if ((peer->lastms > 0) && (peer->lastms <= peer->maxms)) {
				if (pingtime > peer->maxms)
					ast_log(LOG_NOTICE, "Peer '%s' is now TOO LAGGED!\n", peer->name);
			}
			peer->lastms = pingtime;
			peer->call = NULL;
			if (peer->pokeexpire > -1)
				ast_sched_del(sched, peer->pokeexpire);
			if (!strcasecmp(msg, "INVITE"))
				transmit_request(p, "ACK", 0);
			sip_destroy(p);
			p = NULL;
			/* Try again eventually */
			if ((peer->lastms < 0)  || (peer->lastms > peer->maxms))
				peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_NOTOK, sip_poke_peer_s, peer);
			else
				peer->pokeexpire = ast_sched_add(sched, DEFAULT_FREQ_OK, sip_poke_peer_s, peer);
		}
	} else if (p->outgoing) {
		if (p->initid > -1) {
			/* Don't auto congest anymore since we've gotten something useful back */
			ast_sched_del(sched, p->initid);
			p->initid = -1;
		}
		/* Get their tag if we haven't already */
		if (!strlen(p->theirtag)) {
			to = get_header(req, "To");
			to = strstr(to, "tag=");
			if (to) {
				to += 4;
				strncpy(p->theirtag, to, sizeof(p->theirtag) - 1);
				to = strchr(p->theirtag, ';');
				if (to)
					*to = '\0';
			}
		}
		
		switch(resp) {
		case 100:
			break;
		case 183:	/* We don't really need this since we pass in-band audio anyway */
			/* Not important */
			if (strlen(get_header(req, "Content-Type")))
				process_sdp(p, req);
			break;
		case 180:
			if (p->owner) {
				ast_queue_control(p->owner, AST_CONTROL_RINGING, 0);
				if (p->owner->_state != AST_STATE_UP)
					ast_setstate(p->owner, AST_STATE_RINGING);
			}
			break;
		case 200:
			if (strlen(get_header(req, "Content-Type")))
				process_sdp(p, req);
			if (p->owner) {
				if (p->owner->_state != AST_STATE_UP) {
					ast_setstate(p->owner, AST_STATE_UP);
					ast_queue_control(p->owner, AST_CONTROL_ANSWER, 0);
				}
			}
			if (!strcasecmp(msg, "INVITE"))
				transmit_request(p, "ACK", 0);
			else if (!strcasecmp(msg, "REGISTER"))
			{
				/* char *exp; */
				int expires;
				struct sip_registry *r;
				transmit_request(p, "ACK", 0);
				r=p->registry;
				r->regstate=REG_STATE_REGISTERED;
				ast_log(LOG_NOTICE, "Registration successful\n");
				ast_log(LOG_NOTICE, "Cancelling timeout %d\n", r->timeout);
				if (r->timeout) 
					ast_sched_del(sched, r->timeout);
				r->timeout=0;
				/* set us up for re-registering */
				/* figure out how long we got registered for */
				if (r->expire != -1)
					ast_sched_del(sched, r->expire);
				expires=atoi(get_header(req, "expires"));
				if (!expires) expires=20;
				r->expire=ast_sched_add(sched, (expires-2)*1000, sip_reregister, r); 

			}
			break;
		case 401: /* Not authorized on REGISTER */
			/* XXX: Do I need to ACK the 401? 
			transmit_request(p, "ACK", 0);
			*/
			do_register_auth(p, req);
			break;
		case 407:
			/* First we ACK */
			transmit_request(p, "ACK", 0);
			/* Then we AUTH */
			do_proxy_auth(p, req);
			/* This is just a hack to kill the channel while testing */
			/* 
			p->alreadygone = 1;
			if (p->rtp) {
				rtp = p->rtp;
				p->rtp = NULL;
				ast_rtp_destroy(rtp);
			}
			if (p->owner)
				ast_queue_hangup(p->owner,0);
			transmit_request(p,"ACK",0);
			sip_destroy(p);
			p = NULL;
			*/
			break;
		default:
			if ((resp >= 400) && (resp < 700)) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Got SIP response %d \"%s\" back from %s\n", resp, rest, inet_ntoa(p->sa.sin_addr));
				p->alreadygone = 1;
				if (p->rtp) {
					rtp = p->rtp;
					p->rtp = NULL;
					/* Immediately stop RTP */
					ast_rtp_destroy(rtp);
				}
				/* XXX Locking issues?? XXX */
				switch(resp) {
				case 486: /* Busy here */
				case 600: /* Busy everywhere */
					if (p->owner)
						ast_queue_control(p->owner, AST_CONTROL_BUSY, 0);
					break;
				case 480: /* Temporarily Unavailable */
				case 404: /* Not Found */
				case 410: /* Gone */
				case 500: /* Server error */
				case 501: /* Not Implemented */
					if (owner)
						ast_queue_control(p->owner, AST_CONTROL_CONGESTION, 0);
					break;
				default:
					/* Send hangup */	
					if (owner)
						ast_queue_hangup(p->owner, 0);
					break;
				}
				transmit_request(p, "ACK", 0);
				__sip_destroy(p, 0);
				p = NULL;
			} else
				ast_log(LOG_NOTICE, "Dunno anything about a %d %s response from %s\n", resp, rest, p->owner ? p->owner->name : inet_ntoa(p->sa.sin_addr));
		}
	} else {
		if (sipdebug)
			ast_verbose("Message is %s\n", msg);
		switch(resp) {
		case 200:
			if (!strcasecmp(msg, "INVITE") || !strcasecmp(msg, "REGISTER") )
				transmit_request(p, "ACK", 0);
			break;
		}
	}
	if (owner)
		ast_pthread_mutex_unlock(&owner->lock);
	if (p)
		ast_pthread_mutex_unlock(&p->lock);
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

static int handle_request(struct sip_pvt *p, struct sip_request *req, struct sockaddr_in *sin)
{
	struct sip_request resp;
	char *cmd;
	char *cseq;
	char *e;
	struct ast_channel *c;
	int seqno;
	int len;
	int ignore=0;
	int respid;
	int res;
	/* Clear out potential response */
	memset(&resp, 0, sizeof(resp));
	/* Get Method and Cseq */
	cseq = get_header(req, "Cseq");
	cmd = req->header[0];
	/* Must have Cseq */
	if (!strlen(cmd) || !strlen(cseq))
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
	} else {
		/* Response to our request -- Do some sanity checks */	
		if (!p->initreq.headers) {
			ast_log(LOG_DEBUG, "That's odd...  Got a response on a call we dont know about.\n");
			sip_destroy(p);
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
	
	if (strcmp(cmd, "SIP/2.0"))
		/* Next should follow monotonically increasing */
		p->icseq = seqno + 1;

	/* Initialize the context if it hasn't been already */
	if (!strcasecmp(cmd, "OPTIONS")) {
		if (!strlen(p->context))
			strncpy(p->context, context, sizeof(p->context) - 1);
		res = get_destination(p, req);
		if (res < 0)
			transmit_response_with_allow(p, "404 Not Found", req);
		else if (res > 0)
			transmit_response_with_allow(p, "484 Address Incomplete", req);
		else 
			transmit_response_with_allow(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "INVITE")) {
		/* Process the SDP portion */
		if (!ignore) {
			/* Use this as the basis */
			if (sipdebug)
				ast_verbose("Using latest request as basis request\n");
			copy_request(&p->initreq, req);
			check_via(p, req);
			if (strlen(get_header(req, "Content-Type"))) {
				if (process_sdp(p, req))
					return -1;
			} else {
				p->capability = capability;
				ast_log(LOG_DEBUG, "Hm....  No sdp for the moemnt\n");
			}
		} else if (sipdebug)
			ast_verbose("Ignoring this request\n");
		if (!p->lastinvite) {
			/* Handle authentication if this is our first invite */
			res = check_user(p, req, cmd, e);
			if (res) {
				if (res < 0) {
					ast_log(LOG_NOTICE, "Failed to authenticate user %s\n", get_header(req, "From"));
					sip_destroy(p);
				}
				return 0;
			}
			/* Initialize the context if it hasn't been already */
			if (!strlen(p->context))
				strncpy(p->context, context, sizeof(p->context) - 1);
			if ((res = get_destination(p, NULL))) {
				if (res < 0)
					transmit_response(p, "404 Not Found", req);
				else
					transmit_response(p, "484 Address Incomplete", req);
				sip_destroy(p);
				p = NULL;
				c = NULL;
			} else {
				/* If no extension was specified, use the s one */
				if (!strlen(p->exten))
					strncpy(p->exten, "s", sizeof(p->exten) - 1);
				/* Initialize tag */	
				p->tag = rand();
				/* First invitation */
				c = sip_new(p, AST_STATE_DOWN);
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
				if (ast_pbx_start(c)) {
					ast_log(LOG_WARNING, "Failed to start PBX :(\n");
					ast_hangup(c);
					transmit_response(p, "503 Unavailable", req);
					sip_destroy(p);
				}
				break;
			case AST_STATE_RING:
				transmit_response(p, "100 Trying", req);
				break;
			case AST_STATE_RINGING:
				transmit_response(p, "180 Ringing", req);
				break;
			case AST_STATE_UP:
				transmit_response_with_sdp(p, "200 OK", req);
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to handle INVITE in state %d\n", c->_state);
				transmit_response(p, "100 Trying", req);
			}
		} else {
			if (p) {
				ast_log(LOG_NOTICE, "Unable to create/find channel\n");
				transmit_response(p, "503 Unavailable", req);
				sip_destroy(p);
		}
		}
		} else if (!strcasecmp(cmd, "REFER")) {
		        struct ast_channel *transfer_to;
		        ast_log(LOG_DEBUG, "We found a REFER!\n");
		        if (!strlen(p->context))
			        strncpy(p->context, context, sizeof(p->context) - 1);
			    res = get_refer_info(p, req);
			    if (res < 0)
				    transmit_response_with_allow(p, "404 Not Found", req);
			    else if (res > 0)
				   transmit_response_with_allow(p, "484 Address Incomplete", req);
			    else
				   transmit_response(p, "202 Accepted", req);
			    ast_log(LOG_DEBUG,"202 Accepted\n");
			    transfer_to = c->bridge;
				if (transfer_to)
				   ast_async_goto(transfer_to,"", p->refer_to,1, 1);
			
	} else if (!strcasecmp(cmd, "CANCEL") || !strcasecmp(cmd, "BYE")) {
		copy_request(&p->initreq, req);
		p->alreadygone = 1;
		if (p->rtp) {
			/* Immediately stop RTP */
			ast_rtp_destroy(p->rtp);
			p->rtp = NULL;
		}
		if (p->owner)
			ast_queue_hangup(p->owner, 1);
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "MESSAGE")) {
		if (sipdebug)
			ast_verbose("Receiving message!\n");
		receive_message(p, req);
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "INFO")) {
		if (sipdebug)
			ast_verbose("Receiving DTMF!\n");
		receive_info(p, req);
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "REGISTER")) {
		/* Use this as the basis */
		if (sipdebug)
			ast_verbose("Using latest request as basis request\n");
		copy_request(&p->initreq, req);
		check_via(p, req);
		transmit_response(p, "100 Trying", req);
		if ((res = register_verify(p, sin, req, e)) < 0) 
			ast_log(LOG_NOTICE, "Registration from '%s' failed for '%s'\n", get_header(req, "To"), inet_ntoa(sin->sin_addr));
		sip_send_mwi(p);
		if (res < 1)
			sip_destroy(p);
	} else if (!strcasecmp(cmd, "ACK")) {
		/* Uhm, I haven't figured out the point of the ACK yet.  Are we
		   supposed to retransmit responses until we get an ack? 
		   Make sure this is on a valid call */
		if (strlen(get_header(req, "Content-Type"))) {
			if (process_sdp(p, req))
				return -1;
		} 
		if (!p->lastinvite && !strlen(p->randdata))
			sip_destroy(p);
	} else if (!strcasecmp(cmd, "SIP/2.0")) {
		while(*e && (*e < 33)) e++;
		if (sscanf(e, "%i %n", &respid, &len) != 1) {
			ast_log(LOG_WARNING, "Invalid response: '%s'\n", e);
		} else {
			handle_response(p, respid, e + len, req);
		}
	} else {
		transmit_response_with_allow(p, "405 Method Not Allowed", req);
		ast_log(LOG_NOTICE, "Unknown SIP command '%s' from '%s'\n", 
			cmd, inet_ntoa(p->sa.sin_addr));
	}
	return 0;
}

static int sipsock_read(int *id, int fd, short events, void *ignore)
{
	struct sip_request req;
	struct sockaddr_in sin;
	struct sip_pvt *p;
	int res;
	int len;
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
	if (sipdebug)
		ast_verbose("Sip read: \n%s\n", req.data);
	parse(&req);
	if (req.headers < 2) {
		/* Must have at least two headers */
		return 1;
	}
	/* Process request, with iflock held */
	ast_pthread_mutex_lock(&netlock);
	p = find_call(&req, &sin);
	if (p) {
		handle_request(p, &req, &sin);
	}
	ast_pthread_mutex_unlock(&netlock);
	return 1;
}

static void *do_monitor(void *data)
{
	int res;
	struct sip_pkt *p;
	struct sip_pvt *sip;
	/* Add an I/O event to our UDP socket */
	if (sipsock > -1) 
		ast_io_add(io, sipsock, sipsock_read, AST_IO_IN, NULL);
	
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
	for(;;) {
		/* Check for interfaces needing to be killed */
		ast_pthread_mutex_lock(&iflock);
restartsearch:		
		sip = iflist;
		while(sip) {
			if (sip->needdestroy) {
				__sip_destroy(sip, 1);
				goto restartsearch;
			}
			sip = sip->next;
		}
		ast_pthread_mutex_unlock(&iflock);
		/* Don't let anybody kill us right away.  Nobody should lock the interface list
		   and wait for the monitor list, but the other way around is okay. */
		ast_pthread_mutex_lock(&monlock);
		/* Lock the network interface */
		ast_pthread_mutex_lock(&netlock);
		p = packets;
		while(p) {
			/* Handle any retransmissions */
			p = p->next;
		}
		/* Okay, now that we know what to do, release the network lock */
		ast_pthread_mutex_unlock(&netlock);
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		ast_pthread_mutex_unlock(&monlock);
		pthread_testcancel();
		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		if ((res < 0) || (res > 1000))
			res = 1000;
		res = ast_io_wait(io, res);
		ast_pthread_mutex_lock(&monlock);
		if (res >= 0) 
			ast_sched_runq(sched);
		ast_pthread_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor(void)
{
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == -2)
		return 0;
	if (ast_pthread_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		ast_pthread_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread) {
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else {
		/* Start a new monitor */
		if (pthread_create(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			ast_pthread_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_pthread_mutex_unlock(&monlock);
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
	p = peer->call = sip_alloc(NULL, NULL);
	if (!peer->call) {
		ast_log(LOG_WARNING, "Unable to allocate call for poking peer '%s'\n", peer->name);
		return -1;
	}
	memcpy(&p->sa, &peer->addr, sizeof(p->sa));

	/* Recalculate our side, and recalculate Call ID */
	memcpy(&p->ourip, myaddrfor(&p->sa.sin_addr), sizeof(p->ourip));
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=%08x", inet_ntoa(p->ourip), ourport, p->branch);
	build_callid(p->callid, sizeof(p->callid), p->ourip);

	if (peer->pokeexpire > -1)
		ast_sched_del(sched, peer->pokeexpire);
	p->peerpoke = peer;
	p->outgoing = 1;
#ifdef VOCAL_DATA_HACK
	strncpy(p->username, "__VOCAL_DATA_SHOULD_READ_THE_SIP_SPEC__", sizeof(p->username));
	transmit_invite(p, "INVITE", 0, NULL, NULL);
#else
	transmit_invite(p, "OPTIONS", 0, NULL, NULL);
#endif
	gettimeofday(&peer->ps, NULL);
	peer->pokeexpire = ast_sched_add(sched, DEFAULT_MAXMS * 2, sip_poke_noanswer, peer);

	return 0;
}


static int sip_send_mwi(struct sip_pvt *p)
{
	struct sip_request req;
	int res;

	if(strlen(p->mailbox)) {
		ast_log(LOG_NOTICE, "mwi: check mailbox: %s\n", p->mailbox);
		res = ast_app_has_voicemail(p->mailbox);
		if(res) {
			ast_log(LOG_NOTICE, "mwi: mailbox has messages\n");
			reqprep(&req, p, "NOTIFY", 1);
			add_header(&req, "Event", "message-summary");
			add_header(&req, "Content-Type", "text/plain");
			add_line(&req, "Message-Waiting: yes\n");
			send_request(p, &req);

		} else {

			ast_log(LOG_NOTICE, "mwi: mailbox does not contain messages\n");
                        reqprep(&req, p, "NOTIFY", 1);
			add_header(&req, "Event", "message-summary");
                        add_header(&req, "Content-Type", "text/plain");
			add_line(&req, "Message-Waiting: no\n");
			send_request(p, &req);
		}

	}
	return 0;

}

static int sip_send_mwi_to_peer(struct sip_peer *peer)
{
	struct sip_pvt *p;
	p = sip_alloc(NULL, NULL);
	if (!p) {
		ast_log(LOG_WARNING, "Unable to build sip pvt data for MWI\n");
		return -1;
	}
	if (create_addr(p, peer->name)) {
		sip_destroy(p);
		return -1;
	}
	/* Recalculate our side, and recalculate Call ID */
	memcpy(&p->ourip, myaddrfor(&p->sa.sin_addr), sizeof(p->ourip));
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=%08x", inet_ntoa(p->ourip), ourport, p->branch);
	build_callid(p->callid, sizeof(p->callid), p->ourip);
	/* Send MWI */
	sip_send_mwi(p);
	/* Destroy channel */
	sip_destroy(p);
	return 0;
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
	format &= capability;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", format);
		return NULL;
	}
	p = sip_alloc(NULL, NULL);
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
		host = tmp;
		ext = NULL;
	}

	/* Assign a default capability */
	p->capability = capability;

	if (create_addr(p, host)) {
		sip_destroy(p);
		return NULL;
	}
	/* Recalculate our side, and recalculate Call ID */
	memcpy(&p->ourip, myaddrfor(&p->sa.sin_addr), sizeof(p->ourip));
	snprintf(p->via, sizeof(p->via), "SIP/2.0/UDP %s:%d;branch=%08x", inet_ntoa(p->ourip), ourport, p->branch);
	build_callid(p->callid, sizeof(p->callid), p->ourip);
	if (ext)
		strncpy(p->username, ext, sizeof(p->username) - 1);
#if 0
	printf("Setting up to call extension '%s' at '%s'\n", ext ? ext : "<none>", host);
#endif
	tmpc = sip_new(p, AST_STATE_DOWN);
	if (!tmpc)
		sip_destroy(p);
	restart_monitor();
	return tmpc;
}

static struct sip_user *build_user(char *name, struct ast_variable *v)
{
	struct sip_user *user;
	int format;
	user = (struct sip_user *)malloc(sizeof(struct sip_user));
	if (user) {
		memset(user, 0, sizeof(struct sip_user));
		strncpy(user->name, name, sizeof(user->name)-1);
		user->canreinvite = 1;
		/* JK02: set default context */
		strcpy(user->context, context);
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
			} else if (!strcasecmp(v->name, "canreinvite")) {
				user->canreinvite = ast_true(v->value);
			} else if (!strcasecmp(v->name, "callerid")) {
				strncpy(user->callerid, v->value, sizeof(user->callerid)-1);
				user->hascallerid=1;
			} else if (!strcasecmp(v->name, "accountcode")) {
				strncpy(user->accountcode, v->value, sizeof(user->accountcode)-1);
			} else if (!strcasecmp(v->name, "mailbox")) {
                                strncpy(user->mailbox, v->value, sizeof(user->mailbox)-1);
			} else if (!strcasecmp(v->name, "amaflags")) {
				format = ast_cdr_amaflags2int(v->value);
				if (format < 0) {
					ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
				} else {
					user->amaflags = format;
				}
			} else if (!strcasecmp(v->name, "insecure")) {
				user->insecure = ast_true(v->value);
			}
			v = v->next;
		}
	}
	if (!strlen(user->methods)) {
		if (strlen(user->secret)) 
			strncpy(user->methods, "md5,plaintext", sizeof(user->methods) - 1);
	}
	return user;
}

static struct sip_peer *build_peer(char *name, struct ast_variable *v)
{
	struct sip_peer *peer;
	struct sip_peer *prev;
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
		peer = malloc(sizeof(struct sip_peer));
		memset(peer, 0, sizeof(struct sip_peer));
		peer->expire = -1;
		peer->pokeexpire = -1;
	}
	if (peer) {
		if (!found) {
			strncpy(peer->name, name, sizeof(peer->name)-1);
			strncpy(peer->context, context, sizeof(peer->context)-1);
			peer->addr.sin_port = htons(DEFAULT_SIP_PORT);
			peer->expirey = expirey;
		}
		peer->capability = capability;
		while(v) {
			if (!strcasecmp(v->name, "secret")) 
				strncpy(peer->secret, v->value, sizeof(peer->secret)-1);
			else if (!strcasecmp(v->name, "auth")) 
				strncpy(peer->methods, v->value, sizeof(peer->methods)-1);
			else if (!strcasecmp(v->name, "canreinvite")) 
				peer->canreinvite = ast_true(v->value);
			else if (!strcasecmp(v->name, "context"))
				strncpy(peer->context, v->value, sizeof(peer->context)-1);
			else if (!strcasecmp(v->name, "host")) {
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
			} else if (!strcasecmp(v->name, "port")) {
				if (peer->dynamic)
					peer->defaddr.sin_port = htons(atoi(v->value));
				else
					peer->addr.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "username")) {
				strncpy(peer->username, v->value, sizeof(peer->username)-1);
			} else if (!strcasecmp(v->name, "mailbox")) {
                                strncpy(peer->mailbox, v->value, sizeof(peer->mailbox)-1);
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
				peer->insecure = ast_true(v->value);
			} else if (!strcasecmp(v->name, "qualify")) {
				if (!strcasecmp(v->value, "no")) {
					peer->maxms = 0;
				} else if (!strcasecmp(v->value, "yes")) {
					peer->maxms = DEFAULT_MAXMS;
				} else if (sscanf(v->value, "%d", &peer->maxms) != 1) {
					ast_log(LOG_WARNING, "Qualification of peer '%s' should be 'yes', 'no', or a number of milliseconds at line %d of iax.conf\n", peer->name, v->lineno);
					peer->maxms = 0;
				}
			}

			v=v->next;
		}
		if (!strlen(peer->methods))
			strcpy(peer->methods, "md5,plaintext");
		peer->delme = 0;
	}
	return peer;
}

static int reload_config()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct sip_peer *peer;
	struct sip_user *user;
	char *cat;
    char *utype;
	struct hostent *hp;
	int format;
	int oldport = ntohs(bindaddr.sin_port);
	
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
	memset(&bindaddr, 0, sizeof(bindaddr));
	/* Initialize some reasonable defaults */
	strncpy(context, "default", sizeof(context) - 1);
	strcpy(language, "");
	v = ast_variable_browse(cfg, "general");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
		} else if (!strcasecmp(v->name, "bindaddr")) {
			if (!(hp = gethostbyname(v->value))) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
			} else {
				memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
			}
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
		} else
			ast_log(LOG_NOTICE, "Ignoring unknown SIP general keyword '%s'\n", v->name);
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
					ast_log(LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, "sip.conf");
				}
			} else
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
		}
		cat = ast_category_browse(cfg, cat);
	}
	
	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(&__ourip, &bindaddr, sizeof(__ourip));
	} else {
		hp = gethostbyname(ourhost);
		if (!hp) {
			ast_log(LOG_WARNING, "Unable to get our IP address, SIP disabled\n");
			return 0;
		}
		memcpy(&__ourip, hp->h_addr, sizeof(__ourip));
	}
	if (!ntohs(bindaddr.sin_port))
		bindaddr.sin_port = ntohs(DEFAULT_SIP_PORT);
	bindaddr.sin_family = AF_INET;
	pthread_mutex_lock(&netlock);
	if ((sipsock > -1) && (ntohs(bindaddr.sin_port) != oldport)) {
		close(sipsock);
		sipsock = -1;
	}
	if (sipsock < 0) {
		sipsock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sipsock < 0) {
			ast_log(LOG_WARNING, "Unable to create SIP socket: %s\n", strerror(errno));
		} else {
			if (bind(sipsock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
				ast_log(LOG_WARNING, "Failed to bind to %s:%d: %s\n",
						inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port),
							strerror(errno));
				close(sipsock);
				sipsock = -1;
			} else if (option_verbose > 1) {
				ast_verbose(VERBOSE_PREFIX_2 "SIP Listening on %s:%d\n", 
					inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port));
				if (option_verbose > 1)
					ast_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);

				if (setsockopt(sipsock, SOL_IP, IP_TOS, &tos, sizeof(tos))) 
					ast_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
	
			}
		}
	}
	pthread_mutex_unlock(&netlock);

	ast_destroy(cfg);
	return 0;
}

int load_module()
{
	int res;
	struct sip_peer *peer;
	struct sip_registry *reg;
	res = reload_config();
	if (!res) {
		/* Make sure we can register our sip channel type */
		if (ast_channel_register(type, tdesc, capability, sip_request)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
			return -1;
		}
		ast_cli_register(&cli_show_users);
		ast_cli_register(&cli_show_channels);
		ast_cli_register(&cli_show_peers);
		ast_cli_register(&cli_show_registry);
		ast_cli_register(&cli_debug);
		ast_cli_register(&cli_no_debug);
		sched = sched_context_create();
		if (!sched) {
			ast_log(LOG_WARNING, "Unable to create schedule context\n");
		}
		io = io_context_create();
		if (!io) {
			ast_log(LOG_WARNING, "Unable to create I/O context\n");
		}
	
		ast_pthread_mutex_lock(&peerl.lock);
		for (peer = peerl.peers; peer; peer = peer->next)
			sip_poke_peer(peer);

		for (reg = registrations; reg; reg = reg->next) 
			sip_do_register(reg);

		ast_pthread_mutex_unlock(&peerl.lock);
		/* And start the monitor for the first time */
		restart_monitor();
	}
	return res;
}

void delete_users(void)
{
	struct sip_user *user, *userlast;
	struct sip_peer *peer;
	struct sip_registry *reg, *regl;

	/* Delete all users */
	ast_pthread_mutex_lock(&userl.lock);
	for (user=userl.users;user;) {
		ast_free_ha(user->ha);
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

void prune_peers(void)
{
	/* Prune peers who still are supposed to be deleted */
	struct sip_peer *peer, *peerlast, *peernext;
	ast_pthread_mutex_lock(&peerl.lock);
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
	ast_pthread_mutex_unlock(&peerl.lock);
}

int reload(void)
{
	struct sip_registry *reg;
	struct sip_peer *peer;
	delete_users();
	reload_config();

	prune_peers();
	/* And start the monitor for the first time */
	restart_monitor();
	for (reg = registrations; reg; reg = reg->next) 
		sip_do_register(reg);
	ast_pthread_mutex_lock(&peerl.lock);
	for (peer = peerl.peers; peer; peer = peer->next)
		sip_poke_peer(peer);
	ast_pthread_mutex_unlock(&peerl.lock);
	return 0;
}

int unload_module()
{
	struct sip_pvt *p, *pl;
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	if (!ast_pthread_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		iflist = NULL;
		ast_pthread_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_pthread_mutex_lock(&monlock)) {
		if (monitor_thread) {
			pthread_cancel(monitor_thread);
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = -2;
		ast_pthread_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	if (!ast_pthread_mutex_lock(&iflock)) {
		/* Destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			pl = p;
			p = p->next;
			/* Free associated memory */
			free(pl);
		}
		iflist = NULL;
		ast_pthread_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
		
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

char *description()
{
	return desc;
}

