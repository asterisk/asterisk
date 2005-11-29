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

#define SIPDUMPER
#define DEFAULT_EXPIREY 120
#define MAX_EXPIREY     3600

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
static int capability = AST_FORMAT_ULAW;

static char ourhost[256];
static struct in_addr __ourip;
static int ourport;

/* Expire slowly */
static int expirey = 900;

static struct sched_context *sched;
static struct io_context *io;
/* The private structures of the  sip channels are linked for
   selecting outgoing channels */
   
#define SIP_MAX_HEADERS		64
#define SIP_MAX_LINES 		64

struct sip_request {
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
	unsigned int cseq;							/* Current seqno */
	int lastinvite;						/* Last Cseq of invite */
	int alreadygone;					/* Whether or not we've already been destroyed by or peer */
	int needdestroy;					/* if we need to be destroyed */
	int capability;						/* Special capability */
	int outgoing;						/* Outgoing or incoming call? */
	int insecure;						/* Don't check source port/ip */
	int expirey;						/* How long we take to expire */
	int branch;							/* One random number */
	int tag;							/* Another random number */
	struct sockaddr_in sa;				/* Our peer */
	struct in_addr ourip;				/* Our IP */
	struct ast_channel *owner;			/* Who owns us */
	char exten[AST_MAX_EXTENSION];		/* Extention where to start */
	char context[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char theirtag[256];				/* Their tag */
	char username[81];
	char callerid[256];					/* Caller*ID */
	char accountcode[256];				/* Account code */
	int amaflags;						/* AMA Flags */
	struct sip_request initreq;			/* Initial request */
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
	int hascallerid;
	int amaflags;
	int insecure;
	struct ast_ha *ha;
	struct sip_user *next;
};

struct sip_peer {
	char name[80];
	char secret[80];
	char methods[80];
	char username[80];
	int dynamic;
	int expire;
	int expirey;
	int capability;
	int insecure;
	struct sockaddr_in addr;
	struct in_addr mask;
	
	struct sockaddr_in defaddr;
	struct ast_ha *ha;
	int delme;
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


static int sipsock  = -1;

static struct sockaddr_in bindaddr;

static struct ast_frame  *sip_read(struct ast_channel *ast);
static int transmit_response(struct sip_pvt *p, char *msg, struct sip_request *req);
static int transmit_response_with_sdp(struct sip_pvt *p, char *msg, struct sip_request *req);
static int transmit_request(struct sip_pvt *p, char *msg, int inc);
static int transmit_invite_with_sdp(struct sip_pvt *p, char *msg);

static int __sip_xmit(struct sip_pvt *p, char *data, int len)
{
	int res;
    res=sendto(sipsock, data, len, 0, (struct sockaddr *)&p->sa, sizeof(struct sockaddr_in));
	if (res != len) {
		ast_log(LOG_WARNING, "sip_xmit returned %d\n", res);
	}
	return res;
}

static int send_response(struct sip_pvt *p, struct sip_request *req)
{
	int res;
	printf("Transmitting:\n%s\n to %s:%d\n", req->data, inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	res = __sip_xmit(p, req->data, req->len);
	if (res > 0)
		res = 0;
	return res;
}

static int send_request(struct sip_pvt *p, struct sip_request *req)
{
	int res;
	printf("XXX Need to handle Retransmitting XXX:\n%s\n", req->data);
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

static int sip_digit(struct ast_channel *ast, char digit)
{
	printf("SIP digit! (%c)\n", digit);
	return 0;
}

static int create_addr(struct sockaddr_in *sin, int *capability, char *peer, char *username, int *insecure)
{
	struct hostent *hp;
	struct sip_peer *p;
	int found=0;
	sin->sin_family = AF_INET;
	ast_pthread_mutex_lock(&peerl.lock);
	p = peerl.peers;
	while(p) {
		if (!strcasecmp(p->name, peer)) {
			found++;
			if (capability)
				*capability = p->capability;
			if (username)
				strncpy(username, p->username, 80);
			if (insecure)
				*insecure = p->insecure;
			if (p->addr.sin_addr.s_addr || p->defaddr.sin_addr.s_addr) {
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
			sin->sin_port = htons(DEFAULT_SIP_PORT);
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

static int sip_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res;
	struct sip_pvt *p;
	
	p = ast->pvt->pvt;
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "sip_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	res = 0;
	p->outgoing = 1;
	transmit_invite_with_sdp(p, p->username);
	return res;
}

static void __sip_destroy(struct sip_pvt *p)
{
	struct sip_pvt *cur, *prev = NULL;
	if (p->rtp) {
		ast_rtp_destroy(p->rtp);
	}
	/* Unlink us from the owner if we have one */
	if (p->owner) {
		ast_pthread_mutex_lock(&p->owner->lock);
		ast_log(LOG_DEBUG, "Detaching from %s\n", p->owner->name);
		p->owner->pvt->pvt = NULL;
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
	} else
		free(p);
}
static void sip_destroy(struct sip_pvt *p)
{
	ast_pthread_mutex_lock(&iflock);
	__sip_destroy(p);
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
	strncpy(ifreq.ifr_ifrn.ifrn_name,iface,sizeof(ifreq.ifr_ifrn.ifrn_name));

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
		char *fields[10];

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

			printf("Interface is %s\n",iface);
			temp = lookup_iface(iface);
			printf("IP Address is %s\n",inet_ntoa(*temp));
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
	if (!p->owner || p->owner->_state != AST_STATE_UP)
		needcancel = 1;
	/* Disconnect */
	p = ast->pvt->pvt;
	p->owner = NULL;
	ast->pvt->pvt = NULL;

	p->needdestroy = 1;
	p->outgoing = 0;
	/* Start the process if it's not already started */
	if (!p->alreadygone && strlen(p->initreq.data)) {
		if (needcancel) {
			p->outgoing = 1;
			transmit_request(p, "CANCEL", 0);
		} else {
			/* Send a hangup */
			transmit_request(p, "BYE", 1);
		}
	}
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
		return -1;
	}
	p->owner = newchan;
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
}

static int sip_indicate(struct ast_channel *ast, int condition)
{
	struct sip_pvt *p = ast->pvt->pvt;
	switch(condition) {
	case AST_CONTROL_RINGING:
		if (ast->_state == AST_STATE_RING) {
			transmit_response(p, "180 Ringing", &p->initreq);
		} else {
			ast_log(LOG_WARNING, "XXX Need to send in-band ringtone XXX\n");
		}
		break;
	case AST_CONTROL_BUSY:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "600 Busy everywhere", &p->initreq);
		} else {
			ast_log(LOG_WARNING, "XXX Need to send in-band busy tone XXX\n");
		}
		break;
	case AST_CONTROL_CONGESTION:
		if (ast->_state != AST_STATE_UP) {
			transmit_response(p, "486 Busy here", &p->initreq);
		} else {
			ast_log(LOG_WARNING, "XXX Need to send in-band congestion tone XXX\n");
		}
		break;
	default:
		printf("Don't know how to indicate condition %d\n", condition);
	}
	return 0;
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
		tmp->pvt->send_digit = sip_digit;
		tmp->pvt->call = sip_call;
		tmp->pvt->hangup = sip_hangup;
		tmp->pvt->answer = sip_answer;
		tmp->pvt->read = sip_read;
		tmp->pvt->write = sip_write;
		tmp->pvt->indicate = sip_indicate;
		tmp->pvt->fixup = sip_fixup;
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		i->owner = tmp;
		ast_pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, i->exten, sizeof(tmp->exten)-1);
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
	p->rtp = ast_rtp_new(sched, io);
	if (!p->rtp) {
		ast_log(LOG_WARNING, "Unable to create RTP session: %s\n", strerror(errno));
		free(p);
		return NULL;
	}
	ast_pthread_mutex_init(&p->lock);
	ast_rtp_set_data(p->rtp, p);
	ast_rtp_set_callback(p->rtp, rtpready);
	if (sin) {
		memcpy(&p->sa, sin, sizeof(p->sa));
		memcpy(&p->ourip, myaddrfor(&p->sa.sin_addr), sizeof(p->ourip));
	} else
		memcpy(&p->ourip, &__ourip, sizeof(p->ourip));
	if (!callid)
		build_callid(p->callid, sizeof(p->callid), p->ourip);
	else
		strncpy(p->callid, callid, sizeof(p->callid) - 1);
	/* Add to list */
	ast_pthread_mutex_lock(&iflock);
	p->next = iflist;
	iflist = p;
	ast_pthread_mutex_unlock(&iflock);
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
	printf("%d headers, %d lines\n", req->headers, req->lines);
	if (*c) 
		ast_log(LOG_WARNING, "Odd content, extra stuff left over ('%s')\n", c);
}

static int process_sdp(struct sip_pvt *p, struct sip_request *req)
{
	char *m;
	char *c;
	char host[258];
	int len;
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
	if (sscanf(m, "audio %d RTP/AVP %n", &portno, &len) != 1) {
		ast_log(LOG_WARNING, "Unable to determine port number for RTP in '%s'\n", m); 
		return -1;
	}
	sin.sin_family = AF_INET;
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	sin.sin_port = htons(portno);
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
	printf("Capabilities: us - %d, them - %d, combined - %d\n",
		capability, peercapability, p->capability);
	if (!p->capability) {
		ast_log(LOG_WARNING, "No compatible codecs!\n");
		return -1;
	}
	return 0;
	
}

static int add_header(struct sip_request *req, char *var, char *value)
{
	if (req->lines) {
		ast_log(LOG_WARNING, "Can't add more headers when lines have been added\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	req->len += snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s: %s\r\n", var, value);
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
	if (req->lines) {
		ast_log(LOG_WARNING, "Can't add more headers when lines have been added\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	req->len += snprintf(req->header[req->headers], sizeof(req->data) - req->len, "\r\n");
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
	if (!req->lines) {
		/* Add extra empty return */
		req->len += snprintf(req->data + req->len, sizeof(req->data) - req->len, "\r\n");
	}
	req->line[req->lines] = req->data + req->len;
	req->len += snprintf(req->line[req->lines], sizeof(req->data) - req->len, "%s", line);
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
	req->len += snprintf(req->header[req->headers], sizeof(req->data) - req->len, "SIP/2.0 %s\r\n", resp);
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
	req->len += snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s %s SIP/2.0\r\n", resp, recip);
	if (req->headers < SIP_MAX_HEADERS)
		req->headers++;
	else
		ast_log(LOG_WARNING, "Out of header space\n");
	return 0;
}

static int respprep(struct sip_request *resp, struct sip_pvt *p, char *msg, struct sip_request *req)
{
	char newto[256], *ot;
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
		char contact2[256], *c, contact[256];
		snprintf(tmp, sizeof(tmp), "%d", p->expirey);
		strncpy(contact2, get_header(req, "Contact"), sizeof(contact2)-1);
		c = ditch_braces(contact2);
		snprintf(contact, sizeof(contact), "<%s>", c);
		add_header(resp, "Expires", tmp);
		add_header(resp, "Contact", contact);
	} else {
		char contact2[256], *c, contact[256];
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
	char stripped[80];
	char tmp[80];
	char newto[256];
	char *c, *n;
	char *ot, *of;

	memset(req, 0, sizeof(struct sip_request));
	if (inc)
		p->cseq++;

	
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

	snprintf(tmp, sizeof(tmp), "%d %s", p->cseq, msg);

	copy_all_header(req, orig, "Via");

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

static int add_sdp(struct sip_request *resp, struct sip_pvt *p)
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
	/* XXX We break with the "recommendation" and send our IP, in order that our
	       peer doesn't have to gethostbyname() us XXX */
	len = 0;
	ast_rtp_get_us(p->rtp, &sin);
	printf("We're at %s port %d\n", inet_ntoa(p->ourip), ntohs(sin.sin_port));	
	snprintf(v, sizeof(v), "v=0\r\n");
	snprintf(o, sizeof(o), "o=root %d %d IN IP4 %s\r\n", getpid(), getpid(), inet_ntoa(p->ourip));
	snprintf(s, sizeof(s), "s=session\r\n");
	snprintf(c, sizeof(c), "c=IN IP4 %s\r\n", inet_ntoa(p->ourip));
	snprintf(t, sizeof(t), "t=0 0\r\n");
	snprintf(m, sizeof(m), "m=audio %d RTP/AVP", ntohs(sin.sin_port));
	for (x=1;x<= AST_FORMAT_MAX_AUDIO; x <<= 1) {
		if (p->capability & x) {
			printf("Answering with capability %d\n", x);
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
	add_sdp(&resp, p);
	return send_response(p, &resp);
}

static int transmit_invite_with_sdp(struct sip_pvt *p, char *username)
{
	struct sip_request req;
	char from[256];
	char to[256];
	char tmp[80];
	char via[256];
	char cid[256];
	char *l, *n=NULL;
	if (p->owner && p->owner->callerid) {
		strcpy(cid, p->owner->callerid);
		ast_callerid_parse(cid, &n, &l);
		if (!n)
			n = l;
	}
	if (!n)
		n = "";
	p->branch = rand();	
	p->tag = rand();
	snprintf(from, sizeof(from), "\"%s\" <sip:sip@%s>;tag=%08x", n, inet_ntoa(p->ourip), p->tag);
	if (strlen(username)) {
		if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
			snprintf(to, sizeof(to), "<sip:%s@%s:%d>",username, inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
		} else {
			snprintf(to, sizeof(to), "<sip:%s@%s>",username, inet_ntoa(p->sa.sin_addr));
		}
	} else if (ntohs(p->sa.sin_port) != DEFAULT_SIP_PORT) {
		snprintf(to, sizeof(to), "<sip:%s:%d>", inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	} else {
		snprintf(to, sizeof(to), "<sip:%s>", inet_ntoa(p->sa.sin_addr));
	}
	snprintf(via, sizeof(via), "SIP/2.0/UDP %s:%d;branch=%08x", inet_ntoa(p->ourip), ourport, p->branch);

	memset(&req, 0, sizeof(req));
	init_req(&req, "INVITE", to);
	/* Start with 101 instead of 1 */
	p->cseq = 100;
	snprintf(tmp, sizeof(tmp), "%d %s", ++p->cseq, "INVITE");

	add_header(&req, "Via", via);
	add_header(&req, "From", from);
	add_header(&req, "To", to);
	add_header(&req, "Call-ID", p->callid);
	add_header(&req, "CSeq", tmp);
	add_header(&req, "User-Agent", "Asterisk PBX");
	add_sdp(&req, p);
	/* Use this as the basis */
	copy_request(&p->initreq, &req);
	parse(&p->initreq);
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

static int parse_contact(struct sip_pvt *pvt, struct sip_peer *p, struct sip_request *req)
{
	char contact[80]; 
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
	if (p->expire > -1)
		ast_sched_del(sched, p->expire);
	if ((expirey < 1) || (expirey > MAX_EXPIREY))
		expirey = DEFAULT_EXPIREY;
	p->expire = ast_sched_add(sched, expirey * 1000, expire_register, p);
	pvt->expirey = expirey;
	if (memcmp(&p->addr, &oldsin, sizeof(oldsin))) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Registered SIP '%s' at %s port %d expires %d\n", p->username, inet_ntoa(p->addr.sin_addr), ntohs(p->addr.sin_port), expirey);
	}
	return 0;
}

static int register_verify(struct sip_pvt *p, struct sockaddr_in *sin, struct sip_request *req)
{
	int res = -1;
	struct sip_peer *peer;
	char tmp[256];
	char *name, *c;
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
			if (parse_contact(p, peer, req)) 
				ast_log(LOG_WARNING, "Failed to parse contact info\n");
			else
				res = 0;
			
		}	
		peer = peer->next;
	}
	ast_pthread_mutex_unlock(&peerl.lock);
	return res;
}

static int get_destination(struct sip_pvt *p)
{
	char tmp[256], *c, *a;
	strncpy(tmp, get_header(&p->initreq, "To"), sizeof(tmp));
	c = ditch_braces(tmp);
	if (strncmp(c, "sip:", 4)) {
		ast_log(LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return -1;
	}
	c += 4;
	if ((a = strchr(c, '@')) || (a = strchr(c, ';'))) {
		*a = '\0';
	}
	printf("Looking for %s in %s\n", c, p->context);
	if (ast_exists_extension(NULL, p->context, c, 1, NULL)) {
		strncpy(p->exten, c, sizeof(p->exten));
		return 0;
	}

	if (ast_canmatch_extension(NULL, p->context, c, 1, NULL)) {
		return 1;
	}
	
	return -1;
}

static int check_via(struct sip_pvt *p, struct sip_request *req)
{
	char via[256];
	char *c, *pt;
	struct hostent *hp;

	strncpy(via, get_header(req, "Via"), sizeof(via));
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
		printf("Sending to %s : %d\n", inet_ntoa(p->sa.sin_addr), ntohs(p->sa.sin_port));
	}
	return 0;
}

static int check_user(struct sip_pvt *p, struct sip_request *req)
{
	struct sip_user *user;
	char *of, from[256], *c;
	of = get_header(req, "From");
	strncpy(from, of, sizeof(from) - 1);
	of = ditch_braces(from);
	if (strncmp(of, "sip:", 4))
		return 0;
	else
		of += 4;
	strncpy(p->callerid, of, sizeof(p->callerid) - 1);
	/* Get just the username part */
	if ((c = strchr(of, '@')))
		*c = '\0';
	if ((c = strchr(of, ':')))
		*c = '\0';
	if (!strlen(of))
			return 0;
	printf("From: %s\n", of);
	ast_pthread_mutex_lock(&userl.lock);
	user = userl.users;
	while(user) {
		if (!strcasecmp(user->name, of)) {
			strncpy(p->context, user->context, sizeof(p->context) - 1);
			if (strlen(user->callerid) && strlen(p->callerid)) 
				strncpy(p->callerid, user->callerid, sizeof(p->callerid) - 1);
			strncpy(p->accountcode, user->accountcode, sizeof(p->accountcode)  -1);
			p->amaflags = user->amaflags;
			printf("Context is %s\n", p->context);
		}
		user = user->next;
	}
	ast_pthread_mutex_unlock(&userl.lock);
	return 0;
}

static void handle_response(struct sip_pvt *p, int resp, char *rest, struct sip_request *req)
{
	char *to;
	struct ast_rtp *rtp;
	ast_pthread_mutex_lock(&p->lock);
	if (p->outgoing) {
		/* Get their tag if we haven't already */
		if (!strlen(p->theirtag)) {
			to = get_header(req, "To");
			to = strstr(to, "tag=");
			if (to) {
				to += 4;
				strncpy(p->theirtag, to, sizeof(p->theirtag));
				to = strchr(p->theirtag, ';');
				if (to)
					*to = '\0';
			}
		}
		
		switch(resp) {
		case 100:
			/* Not important */
			break;
		case 180:
			if (p->owner) {
				ast_queue_control(p->owner, AST_CONTROL_RINGING, 1);
				if (p->owner->_state != AST_STATE_UP)
					ast_setstate(p->owner, AST_STATE_RINGING);
			}
			break;
		case 200:
			process_sdp(p, req);
			if (p->owner) {
				if (p->owner->_state != AST_STATE_UP) {
					ast_setstate(p->owner, AST_STATE_UP);
					ast_queue_control(p->owner, AST_CONTROL_ANSWER, 1);
				}
				transmit_request(p, "ACK", 0);
			}
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
				/* Send hangup */	
				if (p->owner)
					ast_queue_hangup(p->owner, 1);
				transmit_request(p, "ACK", 0);
				sip_destroy(p);
				p = NULL;
			} else
				ast_log(LOG_NOTICE, "Dunno anything about a %d %s response from %s\n", resp, rest, p->owner ? p->owner->name : inet_ntoa(p->sa.sin_addr));
		}
	}
	if (p)
		ast_pthread_mutex_unlock(&p->lock);
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
	if (p->cseq && (p->cseq < seqno)) {
		ast_log(LOG_DEBUG, "Ignoring out of order packet %d (expecting %d)\n", seqno, p->cseq);
		return -1;
	} else if (p->cseq && (p->cseq != seqno)) {
		/* ignore means "don't do anything with it" but still have to 
		   respond appropriately  */
		ignore=1;
	}
	
	/* Get the command */
	cseq += len;
	while(*cmd && (*cmd < 33))
		cmd++;
	if (!*cmd)
		return -1;
	e = cmd;
	while(*e && (*e > 32))
		e++;
	/* Get the command */
	if (*e) {
		*e = '\0';
		e++;
	}
	if (strcmp(cmd, "SIP/2.0"))
		/* Next should follow monotonically increasing */
		p->cseq = seqno + 1;

	if (!strcasecmp(cmd, "INVITE")) {
		/* Process the SDP portion */
		if (!ignore) {
			/* Use this as the basis */
			printf("Using latest request as basis request\n");
			copy_request(&p->initreq, req);
			check_user(p, req);
			check_via(p, req);
			if (process_sdp(p, req))
				return -1;
		} else
			printf("Ignoring this request\n");
		if (!p->lastinvite) {
			/* Initialize the context if it hasn't been already */
			if (!strlen(p->context))
				strncpy(p->context, context, sizeof(p->context));
			if ((res = get_destination(p))) {
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
					strncpy(p->exten, "s", sizeof(p->exten));
				/* Initialize tag */	
				p->tag = rand();
				/* First invitation */
				c = sip_new(p, AST_STATE_RING);
			}
			
		} else 
			c = p->owner;
		if (!ignore && p)
			p->lastinvite = seqno;
		if (c) {
			switch(c->_state) {
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
	} else if (!strcasecmp(cmd, "CANCEL") || !strcasecmp(cmd, "BYE")) {
		copy_request(&p->initreq, req);
		/* Hangup this channel */
		p->alreadygone = 1;
		if (p->rtp) {
			/* Immediately stop RTP */
			ast_rtp_destroy(p->rtp);
			p->rtp = NULL;
		}
		if (p->owner)
			ast_queue_hangup(p->owner, 1);
		transmit_response(p, "200 OK", req);
	} else if (!strcasecmp(cmd, "REGISTER")) {
		/* Use this as the basis */
		printf("Using latest request as basis request\n");
		copy_request(&p->initreq, req);
		check_via(p, req);
		transmit_response(p, "100 Trying", req);
		if (register_verify(p, sin, req)) {
			ast_log(LOG_NOTICE, "Registration from '%s' failed for '%s'\n", get_header(req, "To"), inet_ntoa(sin->sin_addr));
			transmit_response(p, "401 Unauthorized", &p->initreq);
		} else {
			transmit_response(p, "200 OK", req);
		}
		sip_destroy(p);
	} else if (!strcasecmp(cmd, "ACK")) {
		/* Uhm, I haven't figured out the point of the ACK yet.  Are we
		   supposed to retransmit responses until we get an ack? 
		   Make sure this is on a valid call */
		if (!p->lastinvite)
			sip_destroy(p);
	} else if (!strcasecmp(cmd, "SIP/2.0")) {
		while(*e && (*e < 33)) e++;
		if (sscanf(e, "%i %n", &respid, &len) != 1) {
			ast_log(LOG_WARNING, "Invalid response: '%s'\n", e);
		} else {
			handle_response(p, respid, e + len, req);
		}
	} else {
		transmit_response(p, "405 Method Not Allowed", req);
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
	printf("Sip read: \n%s\n", req.data);
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
	sched = sched_context_create();
	if (!sched) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
		return NULL;
	}
	io = io_context_create();
	if (!io) {
		ast_log(LOG_WARNING, "Unable to create I/O context\n");
		return NULL;
	}
	
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
				__sip_destroy(sip);
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

static struct ast_channel *sip_request(char *type, int format, void *data)
{
	int oldformat;
	struct sip_pvt *p;
	struct ast_channel *tmpc = NULL;
	char *ext, *host;
	char tmp[256];
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
	if (create_addr(&p->sa, &p->capability, host, p->username, &p->insecure)) {
		sip_destroy(p);
		return NULL;
	}
	/* Recalculate our side, and recalculate Call ID */
	memcpy(&p->ourip, myaddrfor(&p->sa.sin_addr), sizeof(p->ourip));
	build_callid(p->callid, sizeof(p->callid), p->ourip);
	if (ext)
		strncpy(p->username, ext, sizeof(p->username) - 1);
	printf("Setting up to call extension '%s' at '%s'\n", ext ? ext : "<none>", host);
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
	}
	if (peer) {
		if (!found) {
			strncpy(peer->name, name, sizeof(peer->name)-1);
			peer->addr.sin_port = htons(DEFAULT_SIP_PORT);
			peer->expirey = expirey;
		}
		peer->capability = capability;
		while(v) {
			if (!strcasecmp(v->name, "secret")) 
				strncpy(peer->secret, v->value, sizeof(peer->secret)-1);
			else if (!strcasecmp(v->name, "auth")) 
				strncpy(peer->methods, v->value, sizeof(peer->methods)-1);
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
			}

			v=v->next;
		}
		if (!strlen(peer->methods))
			strcpy(peer->methods, "md5,plaintext");
		peer->delme = 0;
	}
	return peer;
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct sip_peer *peer;
	struct sip_user *user;
	char *cat;
    char *utype;
	struct hostent *hp;
	
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
		} else if (!strcasecmp(v->name, "port")) {
			if (sscanf(v->value, "%i", &ourport) == 1) {
				bindaddr.sin_port = htons(ourport);
			} else {
				ast_log(LOG_WARNING, "Invalid port number '%s' at line %d of %s\n", v->value, v->lineno, config);
			}
		}
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
	if (sipsock > -1)
		close(sipsock);
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
		} else if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "SIP Listening on %s:%d\n", 
				inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port));
	}
	pthread_mutex_unlock(&netlock);

	/* Make sure we can register our sip channel type */
	if (ast_channel_register(type, tdesc, capability, sip_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_destroy(cfg);
		return -1;
	}
	ast_destroy(cfg);
	/* And start the monitor for the first time */
	restart_monitor();
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

