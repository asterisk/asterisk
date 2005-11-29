/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Media Gateway Control Protocol
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
#include <asterisk/cli.h>
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

#define MGCPDUMPER
#define DEFAULT_EXPIREY 120
#define MAX_EXPIREY     3600

static char *desc = "Media Gateway Control Protocol (MGCP)";
static char *type = "MGCP";
static char *tdesc = "Media Gateway Control Protocol (MGCP)";
static char *config = "mgcp.conf";

#define DEFAULT_MGCP_PORT	2427/* From RFC 2705 */
#define MGCP_MAX_PACKET	1500		/* Also from RFC 2543, should sub headers tho */

static int usecnt =0;
static pthread_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;
static int oseq;

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

static struct sched_context *sched;
static struct io_context *io;
/* The private structures of the  mgcp channels are linked for
   selecting outgoing channels */
   
#define MGCP_MAX_HEADERS		64
#define MGCP_MAX_LINES 		64

struct mgcp_request {
	int len;
	char *verb;
	char *identifier;
	char *endpoint;
	char *version;
	int headers;					/* MGCP Headers */
	char *header[MGCP_MAX_HEADERS];
	int lines;						/* SDP Content */
	char *line[MGCP_MAX_LINES];
	char data[MGCP_MAX_PACKET];
};

static struct mgcp_pkt {
	int retrans;
	struct mgcp_endpoint *owner;
	int packetlen;
	char data[MGCP_MAX_PACKET];
	struct mgcp_pkt *next;
} *packets = NULL;	

#define TYPE_TRUNK		1
#define TYPE_LINE		2

struct mgcp_endpoint {
	pthread_mutex_t lock;
	char name[80];
	char accountcode[80];
	char exten[AST_MAX_EXTENSION];		/* Extention where to start */
	char context[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char callerid[256];					/* Caller*ID */
	char curtone[80];					/* Current tone */
	char txident[80];
	char cxident[80];
	char callid[80];
	int hascallerid;
	int amaflags;
	int type;
	int group;
	int iseq;
	int lastout;
	int alreadygone;
	int needdestroy;
	int capability;
	int outgoing;
	struct ast_channel *owner;
	struct ast_rtp *rtp;
	struct mgcp_endpoint *next;
	struct mgcp_gateway *parent;
};

struct mgcp_gateway {
	/* A gateway containing one or more endpoints */
	char name[80];
	struct sockaddr_in addr;
	struct in_addr ourip;
	struct mgcp_endpoint *endpoints;
	struct ast_ha *ha;
	struct mgcp_gateway *next;
} *gateways;

static pthread_mutex_t gatelock  = AST_MUTEX_INITIALIZER;

static int mgcpsock  = -1;

static struct sockaddr_in bindaddr;

static struct ast_frame  *mgcp_read(struct ast_channel *ast);
static int transmit_response(struct mgcp_endpoint *p, char *msg, struct mgcp_request *req, char *msgrest);
static int transmit_notify_request(struct mgcp_endpoint *p, char *tone, int offhook);
static int transmit_connection_del(struct mgcp_endpoint *p);
static int transmit_notify_request_with_callerid(struct mgcp_endpoint *p, char *tone, int offhook, char *callerid);

static int __mgcp_xmit(struct mgcp_endpoint *p, char *data, int len)
{
	int res;
    res=sendto(mgcpsock, data, len, 0, (struct sockaddr *)&p->parent->addr, sizeof(struct sockaddr_in));
	if (res != len) {
		ast_log(LOG_WARNING, "mgcp_xmit returned %d: %s\n", res, strerror(errno));
	}
	return res;
}

static int send_response(struct mgcp_endpoint *p, struct mgcp_request *req)
{
	int res;
	printf("Transmitting:\n%s\n to %s:%d\n", req->data, inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
	res = __mgcp_xmit(p, req->data, req->len);
	if (res > 0)
		res = 0;
	return res;
}

static int send_request(struct mgcp_endpoint *p, struct mgcp_request *req)
{
	int res;
	printf("XXX Need to handle Retransmitting XXX:\n%s to %s:%d\n", req->data, inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
	res = __mgcp_xmit(p, req->data, req->len);
	return res;
}

static int mgcp_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res;
	struct mgcp_endpoint *p;
	
	p = ast->pvt->pvt;
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "mgcp_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	res = 0;
	p->outgoing = 1;
	if (p->type == TYPE_LINE) {
		transmit_notify_request_with_callerid(p, "rg", 0, ast->callerid);
		ast_setstate(ast, AST_STATE_RINGING);
		ast_queue_control(ast, AST_CONTROL_RINGING, 0);
	} else {
		ast_log(LOG_NOTICE, "Don't know how to dial on trunks yet\n");
		res = -1;
	}
	return res;
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


static int mgcp_hangup(struct ast_channel *ast)
{
	struct mgcp_endpoint *p = ast->pvt->pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "mgcp_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_DEBUG, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_pthread_mutex_lock(&p->lock);
	p->owner = NULL;
	if (strlen(p->cxident))
		transmit_connection_del(p);
	if (!p->alreadygone && (!p->outgoing || (ast->_state == AST_STATE_UP)))
		transmit_notify_request(p, "ro", 1);
	else
		transmit_notify_request(p, "", 0);
	ast->pvt->pvt = NULL;
	p->alreadygone = 0;
	p->outgoing = 0;
	strcpy(p->cxident, "");
	strcpy(p->callid, "");
	if (p->rtp) {
		ast_rtp_destroy(p->rtp);
		p->rtp = NULL;
	}
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
}

static int mgcp_show_endpoints(int fd, int argc, char *argv[])
{
	struct mgcp_gateway  *g;
	struct mgcp_endpoint *e;
	int hasendpoints = 0;
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&gatelock);
	g = gateways;
	while(g) {
		e = g->endpoints;
		ast_cli(fd, "Gateway '%s' at %s\n", g->name, inet_ntoa(g->addr.sin_addr));
		while(e) {
			ast_cli(fd, "   -- '%s@%s in '%s' is %s\n", e->name, g->name, e->context, e->owner ? "active" : "idle");
			hasendpoints = 1;
			e = e->next;
		}
		if (!hasendpoints) {
			ast_cli(fd, "   << No Endpoints Defined >>     ");
		}
		g = g->next;
	}
	ast_pthread_mutex_unlock(&gatelock);
	return RESULT_SUCCESS;
}

static char show_endpoints_usage[] = 
"Usage: mgcp show endpoints\n"
"       Lists all endpoints known to the MGCP (Media Gateawy Control Protocol) subsystem.\n";

static struct ast_cli_entry  cli_show_endpoints = 
	{ { "mgcp", "show", "endpoints", NULL }, mgcp_show_endpoints, "Show defined MGCP endpoints", show_endpoints_usage };

static int mgcp_answer(struct ast_channel *ast)
{
	int res = 0;
	struct mgcp_endpoint *p = ast->pvt->pvt;
	if (ast->_state != AST_STATE_UP) {
		ast_setstate(ast, AST_STATE_UP);
		if (option_debug)
			ast_log(LOG_DEBUG, "mgcp_answer(%s)\n", ast->name);
		transmit_notify_request(p, "", 1);
	}
	return res;
}

static struct ast_frame  *mgcp_read(struct ast_channel *ast)
{
	static struct ast_frame f = { AST_FRAME_NULL, };
	ast_log(LOG_DEBUG, "I should never get called but am on %s!\n", ast->name);
	return &f;
}

static int mgcp_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct mgcp_endpoint *p = ast->pvt->pvt;
	int res = 0;
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE)
			return 0;
		else {
			ast_log(LOG_WARNING, "Can't send %d type frames with MGCP write\n", frame->frametype);
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

static int mgcp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct mgcp_endpoint *p = newchan->pvt->pvt;
	ast_pthread_mutex_lock(&p->lock);
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
		return -1;
	}
	p->owner = newchan;
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
}

static int mgcp_senddigit(struct ast_channel *ast, char digit)
{
	struct mgcp_endpoint *p = ast->pvt->pvt;
	char tmp[2];
	tmp[0] = digit;
	tmp[1] = '\0';
	transmit_notify_request(p, tmp, 1);
	return -1;
}


static int mgcp_indicate(struct ast_channel *ast, int ind)
{
	struct mgcp_endpoint *p = ast->pvt->pvt;
	switch(ind) {
	case AST_CONTROL_RINGING:
		transmit_notify_request(p, "rt", 1);
		break;
	case AST_CONTROL_BUSY:
		transmit_notify_request(p, "bz", 1);
		break;
	case AST_CONTROL_CONGESTION:
		transmit_notify_request(p, "nbz", 1);
		break;
	case -1:
		transmit_notify_request(p, "", 1);
		break;		
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", ind);
		return -1;
	}
	return 0;
}

static struct ast_channel *mgcp_new(struct mgcp_endpoint *i, int state)
{
	struct ast_channel *tmp;
	int fmt;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		tmp->nativeformats = i->capability;
		if (!tmp->nativeformats)
			tmp->nativeformats = capability;
		fmt = ast_best_codec(tmp->nativeformats);
		snprintf(tmp->name, sizeof(tmp->name), "MGCP/%s@%s", i->name, i->parent->name);
		tmp->type = type;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->writeformat = fmt;
		tmp->pvt->rawwriteformat = fmt;
		tmp->readformat = fmt;
		tmp->pvt->rawreadformat = fmt;
		tmp->pvt->pvt = i;
		tmp->pvt->call = mgcp_call;
		tmp->pvt->hangup = mgcp_hangup;
		tmp->pvt->answer = mgcp_answer;
		tmp->pvt->read = mgcp_read;
		tmp->pvt->write = mgcp_write;
		tmp->pvt->indicate = mgcp_indicate;
		tmp->pvt->fixup = mgcp_fixup;
		tmp->pvt->send_digit = mgcp_senddigit;
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

static char *get_sdp(struct mgcp_request *req, char *name)
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

static char *__get_header(struct mgcp_request *req, char *name, int *start)
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
	/* Don't return NULL, so get_header is always a valid pointer */
	return "";
}

static char *get_header(struct mgcp_request *req, char *name)
{
	int start = 0;
	return __get_header(req, name, &start);
}

static int rtpready(struct ast_rtp *rtp, struct ast_frame *f, void *data)
{
	/* Just deliver the audio directly */
	struct mgcp_endpoint *p = data;
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

static struct mgcp_endpoint *find_endpoint(char *name, int msgid, struct sockaddr_in *sin)
{
	struct mgcp_endpoint *p = NULL;
	struct mgcp_gateway *g;
	char tmp[256] = "";
	char *at = NULL;
	if (name) {
		strncpy(tmp, name, sizeof(tmp) - 1);
		at = strchr(tmp, '@');
		if (!at) {
			ast_log(LOG_NOTICE, "Endpoint '%s' has no at sign!\n", name);
			return NULL;
		}
		*at = '\0';
		at++;
	}
	ast_pthread_mutex_lock(&gatelock);
	g = gateways;
	while(g) {
		if (!name || !strcasecmp(g->name, at)) {
			/* Found the gateway -- now for the endpoint */
			p = g->endpoints;
			while(p) {
				if ((name && !strcasecmp(p->name, tmp)) ||
				    (msgid && (p->lastout == msgid)))
					break;
				p = p->next;
			}
			if (name || p)
				break;
		}
		g = g->next;
	}
	ast_pthread_mutex_unlock(&gatelock);
	if (!p) {
		if (name) {
			if (g)
				ast_log(LOG_NOTICE, "Endpoint '%s' not found on gateway '%s'\n", tmp,at);
			else
				ast_log(LOG_NOTICE, "Gateway '%s' (and thus its endpoint '%s') does not exist\n", at, tmp);
		} 
	}
	return p;
}

static void parse(struct mgcp_request *req)
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
			if (f >= MGCP_MAX_HEADERS - 1) {
				ast_log(LOG_WARNING, "Too many MGCP headers...\n");
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
			if (f >= MGCP_MAX_LINES - 1) {
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
	/* Parse up the initial header */
	c = req->header[0];
	while(*c && *c < 33) c++;
	/* First the verb */
	req->verb = c;
	while(*c && (*c > 32)) c++;
	if (*c) {
		*c = '\0';
		c++;
		while(*c && (*c < 33)) c++;
		req->identifier = c;
		while(*c && (*c > 32)) c++;
		if (*c) {
			*c = '\0';
			c++;
			while(*c && (*c < 33)) c++;
			req->endpoint = c;
			while(*c && (*c > 32)) c++;
			if (*c) {
				*c = '\0';
				c++;
				while(*c && (*c < 33)) c++;
				req->version = c;
				while(*c && (*c > 32)) c++;
				while(*c && (*c < 33)) c++;
				while(*c && (*c > 32)) c++;
				*c = '\0';
			}
		}
	}
		
	printf("Verb: '%s', Identifier: '%s', Endpoint: '%s', Version: '%s'\n",
		req->verb, req->identifier, req->endpoint, req->version);
	printf("%d headers, %d lines\n", req->headers, req->lines);
	if (*c) 
		ast_log(LOG_WARNING, "Odd content, extra stuff left over ('%s')\n", c);
}

static int process_sdp(struct mgcp_endpoint *p, struct mgcp_request *req)
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
	printf("Capabilities: us - %d, them - %d, combined - %d\n",
		capability, peercapability, p->capability);
	if (!p->capability) {
		ast_log(LOG_WARNING, "No compatible codecs!\n");
		return -1;
	}
	return 0;
	
}

static int add_header(struct mgcp_request *req, char *var, char *value)
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
	snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s: %s\r\n", var, value);
	req->len += strlen(req->header[req->headers]);
	if (req->headers < MGCP_MAX_HEADERS)
		req->headers++;
	else {
		ast_log(LOG_WARNING, "Out of header space\n");
		return -1;
	}
	return 0;	
}

static int add_line(struct mgcp_request *req, char *line)
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
	if (req->lines < MGCP_MAX_LINES)
		req->lines++;
	else {
		ast_log(LOG_WARNING, "Out of line space\n");
		return -1;
	}
	return 0;	
}

static int init_resp(struct mgcp_request *req, char *resp, struct mgcp_request *orig, char *resprest)
{
	/* Initialize a response */
	if (req->headers || req->len) {
		ast_log(LOG_WARNING, "Request already initialized?!?\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s %s %s\r\n", resp, orig->identifier, resprest);
	req->len += strlen(req->header[req->headers]);
	if (req->headers < MGCP_MAX_HEADERS)
		req->headers++;
	else
		ast_log(LOG_WARNING, "Out of header space\n");
	return 0;
}

static int init_req(struct mgcp_endpoint *p, struct mgcp_request *req, char *verb)
{
	/* Initialize a response */
	if (req->headers || req->len) {
		ast_log(LOG_WARNING, "Request already initialized?!?\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s %d %s@%s MGCP 1.0\r\n", verb, oseq, p->name, p->parent->name);
	req->len += strlen(req->header[req->headers]);
	if (req->headers < MGCP_MAX_HEADERS)
		req->headers++;
	else
		ast_log(LOG_WARNING, "Out of header space\n");
	return 0;
}


static int respprep(struct mgcp_request *resp, struct mgcp_endpoint *p, char *msg, struct mgcp_request *req, char *msgrest)
{
	memset(resp, 0, sizeof(*resp));
	init_resp(resp, msg, req, msgrest);
	return 0;
}

static int reqprep(struct mgcp_request *req, struct mgcp_endpoint *p, char *verb)
{
	memset(req, 0, sizeof(struct mgcp_request));
	oseq++;
	init_req(p, req, verb);
	return 0;
}

static int transmit_response(struct mgcp_endpoint *p, char *msg, struct mgcp_request *req, char *msgrest)
{
	struct mgcp_request resp;
	respprep(&resp, p, msg, req, msgrest);
	return send_response(p, &resp);
}


static int add_sdp(struct mgcp_request *resp, struct mgcp_endpoint *p, struct ast_rtp *rtp)
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
		dest.sin_addr = p->parent->ourip;
		dest.sin_port = sin.sin_port;
	}
	printf("We're at %s port %d\n", inet_ntoa(p->parent->ourip), ntohs(sin.sin_port));	
	snprintf(v, sizeof(v), "v=0\r\n");
	snprintf(o, sizeof(o), "o=root %d %d IN IP4 %s\r\n", getpid(), getpid(), inet_ntoa(dest.sin_addr));
	snprintf(s, sizeof(s), "s=session\r\n");
	snprintf(c, sizeof(c), "c=IN IP4 %s\r\n", inet_ntoa(dest.sin_addr));
	snprintf(t, sizeof(t), "t=0 0\r\n");
	snprintf(m, sizeof(m), "m=audio %d RTP/AVP", ntohs(dest.sin_port));
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
	strcat(m, "\r\n");
	len = strlen(v) + strlen(s) + strlen(o) + strlen(c) + strlen(t) + strlen(m) + strlen(a);
	snprintf(costr, sizeof(costr), "%d", len);
	add_line(resp, v);
	add_line(resp, o);
	add_line(resp, s);
	add_line(resp, c);
	add_line(resp, t);
	add_line(resp, m);
	add_line(resp, a);
	return 0;
}

static int transmit_modify_with_sdp(struct mgcp_endpoint *p, struct ast_rtp *rtp)
{
	struct mgcp_request resp;
	char local[256];
	char tmp[80];
	int x;
	snprintf(local, sizeof(local), "p:20");
	for (x=1;x<= AST_FORMAT_MAX_AUDIO; x <<= 1) {
		if (p->capability & x) {
			snprintf(tmp, sizeof(tmp), ", a:%s", ast2rtpn(x));
			strcat(local, tmp);
		}
	}
	reqprep(&resp, p, "MDCX");
	add_header(&resp, "C", p->callid);
	add_header(&resp, "L", local);
	add_header(&resp, "M", "sendrecv");
	add_header(&resp, "X", p->txident);
	add_header(&resp, "S", "");
	add_sdp(&resp, p, rtp);
	p->lastout = oseq;
	return send_request(p, &resp);
}

static int transmit_connect_with_sdp(struct mgcp_endpoint *p, struct ast_rtp *rtp)
{
	struct mgcp_request resp;
	char local[256];
	char tmp[80];
	int x;
	snprintf(local, sizeof(local), "p:20");
	for (x=1;x<= AST_FORMAT_MAX_AUDIO; x <<= 1) {
		if (p->capability & x) {
			snprintf(tmp, sizeof(tmp), ", a:%s", ast2rtpn(x));
			strcat(local, tmp);
		}
	}
	reqprep(&resp, p, "CRCX");
	add_header(&resp, "C", p->callid);
	add_header(&resp, "L", local);
	add_header(&resp, "M", "sendrecv");
	add_header(&resp, "X", p->txident);
	add_header(&resp, "S", "");
	add_sdp(&resp, p, rtp);
	p->lastout = oseq;
	return send_request(p, &resp);
}

static int transmit_notify_request(struct mgcp_endpoint *p, char *tone, int offhook)
{
	struct mgcp_request resp;
	strncpy(p->curtone, tone, sizeof(p->curtone) - 1);
	reqprep(&resp, p, "RQNT");
	add_header(&resp, "X", p->txident);
	if (offhook)
		add_header(&resp, "R", "hu(N), hf(N), D/[0-9#*](N)");
	else
		add_header(&resp, "R", "hd(N)");
	add_header(&resp, "S", tone);
	return send_request(p, &resp);
}

static int transmit_notify_request_with_callerid(struct mgcp_endpoint *p, char *tone, int offhook, char *callerid)
{
	struct mgcp_request resp;
	char cid[256];
	char tone2[256];
	char *l, *n;
	time_t t;
	struct tm *tm;
	
	time(&t);
	tm = localtime(&t);
	if (callerid)
		strncpy(cid, callerid, sizeof(cid) - 1);
	else
		strcpy(cid, "");
	ast_callerid_parse(cid, &n, &l);
	if (l) {
		ast_shrink_phone_number(l);
		if (!ast_isphonenumber(l)) {
			n = l;
			l = "";
		}
	} 
	if (!n)
		n = "O";
	if (!l)
		l = "";
	snprintf(tone2, sizeof(tone2), "%s, ci(%02d/%02d/%02d/%02d,%s,%s)", tone, 
			tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, l, n);
	strncpy(p->curtone, tone, sizeof(p->curtone) - 1);
	reqprep(&resp, p, "RQNT");
	add_header(&resp, "X", p->txident);
	if (offhook)
		add_header(&resp, "R", "hu(N), hf(N), D/[0-9#*](N)");
	else
		add_header(&resp, "R", "hd(N)");
	add_header(&resp, "S", tone2);
	return send_request(p, &resp);
}
static int transmit_connection_del(struct mgcp_endpoint *p)
{
	struct mgcp_request resp;
	reqprep(&resp, p, "DLCX");
	add_header(&resp, "C", p->callid);
	add_header(&resp, "I", p->cxident);
	return send_request(p, &resp);
}

static void handle_response(struct mgcp_endpoint *p, int result, int ident)
{
	if ((result >= 400) && (result <= 499)) {
		ast_log(LOG_NOTICE, "Terminating on result %d from %s@%s\n", result, p->name, p->parent->name);
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_DEV);
	}
}

static void start_rtp(struct mgcp_endpoint *p)
{
		ast_pthread_mutex_lock(&p->lock);
		/* Allocate the RTP now */
		p->rtp = ast_rtp_new(sched, io);
		ast_rtp_set_callback(p->rtp, rtpready);
		ast_rtp_set_data(p->rtp, p);
		/* Make a call*ID */
		snprintf(p->callid, sizeof(p->callid), "%08x%s", rand(), p->txident);
		/* Transmit the connection create */
		transmit_connect_with_sdp(p, NULL);
		ast_pthread_mutex_unlock(&p->lock);
}

static void *mgcp_ss(void *data)
{
	struct ast_channel *chan = data;
	struct mgcp_endpoint *p = chan->pvt->pvt;
	char exten[AST_MAX_EXTENSION] = "";
	int pos = 0;
	int to = 16000;
	int res;
	for (;;) {
		res = ast_waitfordigit(chan, to);
		if (!res) {
			ast_log(LOG_DEBUG, "Timeout...\n");
			break;
		}
		if (res < 0) {
			ast_log(LOG_DEBUG, "Got hangup...\n");
			break;
		}
		exten[pos++] = res;
		if (!ast_ignore_pattern(chan->context, exten))
			ast_indicate(chan, -1);
		if (ast_matchmore_extension(chan, chan->context, exten, 1, chan->callerid)) {
			if (ast_exists_extension(chan, chan->context, exten, 1, chan->callerid)) 
				to = 3000;
			else
				to = 8000;
		} else
			break;
	}
	if (ast_exists_extension(chan, chan->context, exten, 1, chan->callerid)) {
		strncpy(chan->exten, exten, sizeof(chan->exten) - 1);
		start_rtp(p);
		ast_setstate(chan, AST_STATE_RING);
		chan->rings = 1;
		if (ast_pbx_run(chan)) {
			ast_log(LOG_WARNING, "Unable to launch PBX on %s\n", chan->name);
		} else
			return NULL;
	}
	ast_hangup(chan);
	return NULL;
}

static int handle_request(struct mgcp_endpoint *p, struct mgcp_request *req, struct sockaddr_in *sin)
{
	char *ev, *s;
	struct ast_channel *c;
	pthread_t t;
	struct ast_frame f = { 0, };
	printf("Handling request '%s' on %s@%s\n", req->verb, p->name, p->parent->name);
	/* Clear out potential response */
	if (!strcasecmp(req->verb, "RSIP")) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Resetting interface %s@%s\n", p->name, p->parent->name);
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_DEV);
		transmit_response(p, "200", req, "OK");
	} else if (!strcasecmp(req->verb, "NTFY")) {
		/* Acknowledge and be sure we keep looking for the same things */
		transmit_response(p, "200", req, "OK");
		/* Notified of an event */
		ev = get_header(req, "O");
		s = strchr(ev, '/');
		if (s) ev = s + 1;
		ast_log(LOG_DEBUG, "Endpoint '%s@%s' observed '%s'\n", p->name, p->parent->name, ev);
		/* Keep looking for events unless this was a hangup */
		if (strcasecmp(ev, "hu") && strcasecmp(ev, "hd"))
			transmit_notify_request(p, p->curtone, 1);
		if (!strcasecmp(ev, "hd")) {
			/* Off hook / answer */
			if (p->outgoing) {
				/* Answered */
				if (p->owner) {
					start_rtp(p);
					ast_queue_control(p->owner, AST_CONTROL_ANSWER, 1);
				}
			} else {
				/* Start switch */
				if (!p->owner) {
					transmit_notify_request(p, "dl", 1);
					c = mgcp_new(p, AST_STATE_DOWN);
					if (c) {
						if (pthread_create(&t, NULL, mgcp_ss, c)) {
							ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
							ast_hangup(c);
						}
					} else
						ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", p->name, p->parent->name);
				} else {
					ast_log(LOG_WARNING, "Off hook, but alreaedy have owner on %s@%s\n", p->name, p->parent->name);
				}
			}
		} else if (!strcasecmp(ev, "hu")) {
			ast_log(LOG_DEBUG, "Went on hook\n");
			if (p->owner) {
				p->alreadygone = 1;
				ast_queue_hangup(p->owner, 1);
			}
		} else if ((strlen(ev) == 1) && 
					(((ev[0] >= '0') && (ev[0] <= '9')) ||
					 ((ev[0] >= 'A') && (ev[0] <= 'D')) ||
					 (ev[0] == '*') || (ev[0] == '#'))) {
			f.frametype = AST_FRAME_DTMF;
			f.subclass = ev[0];
			f.src = "mgcp";
			if (p->owner)
				ast_queue_frame(p->owner, &f, 1);
		} else if (!strcasecmp(ev, "T")) {
			/* Digit timeout -- unimportant */
		} else {
			ast_log(LOG_NOTICE, "Received unknown event '%s' from %s@%s\n", ev, p->name, p->parent->name);
		}
	} else {
		ast_log(LOG_WARNING, "Unknown verb '%s' received from %s\n", req->verb, inet_ntoa(sin->sin_addr));
		transmit_response(p, "510", req, "Unknown verb");
	}
	return 0;
}

static int mgcpsock_read(int *id, int fd, short events, void *ignore)
{
	struct mgcp_request req;
	struct sockaddr_in sin;
	struct mgcp_endpoint *p;
	char *c;
	int res;
	int len;
	int result;
	int ident;
	len = sizeof(sin);
	memset(&req, 0, sizeof(req));
	res = recvfrom(mgcpsock, req.data, sizeof(req.data) - 1, 0, (struct sockaddr *)&sin, &len);
	if (res < 0) {
		if (errno != ECONNREFUSED)
			ast_log(LOG_WARNING, "Recv error: %s\n", strerror(errno));
		return 1;
	}
	req.data[res] = '\0';
	req.len = res;
	printf("MGCP read: \n%s\n", req.data);
	parse(&req);
	if (req.headers < 1) {
		/* Must have at least one header */
		return 1;
	}
	if (!req.identifier || !strlen(req.identifier)) {
		ast_log(LOG_NOTICE, "Message from %s missing identifier\n", inet_ntoa(sin.sin_addr));
		return 1;
	}

	if (sscanf(req.verb, "%d", &result) &&
		sscanf(req.identifier, "%d", &ident)) {
		/* Try to find who this message is for, if it's important */
		p = find_endpoint(NULL, ident, &sin);
		if (p) {
			handle_response(p, result, ident);
			if ((c = get_header(&req, "I")))
				strncpy(p->cxident, c, sizeof(p->cxident) - 1);
			if (req.lines)
				process_sdp(p, &req);
		}
	} else {
		if (!req.endpoint || !strlen(req.endpoint) || 
		    !req.version || !strlen(req.version) || 
			!req.verb || !strlen(req.verb)) {
			ast_log(LOG_NOTICE, "Message must have a verb, an idenitifier, version, and endpoint\n");
			return 1;
		}
		/* Process request, with iflock held */
		p = find_endpoint(req.endpoint, 0, &sin);
		if (p) {
			handle_request(p, &req, &sin);
		}
	}
	return 1;
}

static void *do_monitor(void *data)
{
	int res;
	struct mgcp_pkt *p;
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
	if (mgcpsock > -1) 
		ast_io_add(io, mgcpsock, mgcpsock_read, AST_IO_IN, NULL);
	
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
	for(;;) {
		/* Check for interfaces needing to be killed */
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

static struct ast_channel *mgcp_request(char *type, int format, void *data)
{
	int oldformat;
	struct mgcp_endpoint *p;
	struct ast_channel *tmpc = NULL;
	char tmp[256];
	char *dest = data;

	oldformat = format;
	format &= capability;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", format);
		return NULL;
	}
	strncpy(tmp, dest, sizeof(tmp) - 1);
	if (!strlen(tmp)) {
		ast_log(LOG_NOTICE, "MGCP Channels require an endpoint\n");
		return NULL;
	}
	p = find_endpoint(tmp, 0, NULL);
	if (!p) {
		ast_log(LOG_WARNING, "Unable to find MGCP endpoint '%s'\n", tmp);
		return NULL;
	}
	/* Must be busy */
	if (p->owner)
		return NULL;
	tmpc = mgcp_new(p, AST_STATE_DOWN);
	if (!tmpc)
		ast_log(LOG_WARNING, "Unable to make channel for '%s'\n", tmp);
	restart_monitor();
	return tmpc;
}

struct mgcp_gateway *build_gateway(char *cat, struct ast_variable *v)
{
	struct mgcp_gateway *gw;
	struct mgcp_endpoint *e;
	char context[AST_MAX_EXTENSION] = "default";
	char language[80] = "";
	char callerid[AST_MAX_EXTENSION] = "";

	gw = malloc(sizeof(struct mgcp_gateway));
	if (gw) {
		memset(gw, 0, sizeof(struct mgcp_gateway));
		strncpy(gw->name, cat, sizeof(gw->name) - 1);
		while(v) {
			if (!strcasecmp(v->name, "host")) {
				if (ast_get_ip(&gw->addr, v->value)) {
					free(gw);
					return NULL;
				}
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny")) {
				gw->ha = ast_append_ha(v->name, v->value, gw->ha);
			} else if (!strcasecmp(v->name, "port")) {
				gw->addr.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "context")) {
				strncpy(context, v->value, sizeof(context) - 1);
			} else if (!strcasecmp(v->name, "callerid")) {
				if (!strcasecmp(v->value, "asreceived"))
					strcpy(callerid, "");
				else
					strncpy(callerid, v->value, sizeof(callerid) - 1);
			} else if (!strcasecmp(v->name, "language")) {
				strncpy(language, v->value, sizeof(language)-1);
			} else if (!strcasecmp(v->name, "trunk") ||
			           !strcasecmp(v->name, "line")) {
				e = malloc(sizeof(struct mgcp_endpoint));
				if (e) {
					memset(e, 0, sizeof(struct mgcp_endpoint));
					/* XXX Should we really check for uniqueness?? XXX */
					snprintf(e->txident, sizeof(e->txident), "%08x", rand());
					strncpy(e->context, context, sizeof(e->context) - 1);
					strncpy(e->callerid, callerid, sizeof(e->callerid) - 1);
					strncpy(e->language, language, sizeof(e->language) - 1);
					e->capability = capability;
					e->parent = gw;
					strncpy(e->name, v->value, sizeof(e->name) - 1);
					if (!strcasecmp(v->name, "trunk"))
						e->type = TYPE_TRUNK;
					else
						e->type = TYPE_LINE;
					e->next = gw->endpoints;
					gw->endpoints = e;
				}
			} else
				ast_log(LOG_WARNING, "Don't know keyword '%s' at line %d\n", v->name, v->lineno);
			v = v->next;
		}
		
	}
	if (!ntohl(gw->addr.sin_addr.s_addr)) {
		ast_log(LOG_WARNING, "Gateway '%s' lacks IP address\n", gw->name);
		free(gw);
		gw = NULL;
	} else if (!ntohs(gw->addr.sin_port)) {
		gw->addr.sin_port = htons(DEFAULT_MGCP_PORT);
		memcpy(&gw->ourip, myaddrfor(&gw->addr.sin_addr), sizeof(gw->ourip));
	}
	return gw;
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct mgcp_gateway *g;
	char *cat;
	struct hostent *hp;
	int format;
	
	if (gethostname(ourhost, sizeof(ourhost))) {
		ast_log(LOG_WARNING, "Unable to get hostname, MGCP disabled\n");
		return 0;
	}
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, MGCP disabled\n", config);
		return 0;
	}
	memset(&bindaddr, 0, sizeof(bindaddr));
	v = ast_variable_browse(cfg, "general");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "bindaddr")) {
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
			g = build_gateway(cat, ast_variable_browse(cfg, cat));
			if (g) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Added gateway '%s'\n", g->name);
				ast_pthread_mutex_lock(&gatelock);
				g->next = gateways;
				gateways = g;
				ast_pthread_mutex_unlock(&gatelock);
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	
	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(&__ourip, &bindaddr, sizeof(__ourip));
	} else {
		hp = gethostbyname(ourhost);
		if (!hp) {
			ast_log(LOG_WARNING, "Unable to get our IP address, MGCP disabled\n");
			return 0;
		}
		memcpy(&__ourip, hp->h_addr, sizeof(__ourip));
	}
	if (!ntohs(bindaddr.sin_port))
		bindaddr.sin_port = ntohs(DEFAULT_MGCP_PORT);
	bindaddr.sin_family = AF_INET;
	pthread_mutex_lock(&netlock);
	if (mgcpsock > -1)
		close(mgcpsock);
	mgcpsock = socket(AF_INET, SOCK_DGRAM, 0);
	if (mgcpsock < 0) {
		ast_log(LOG_WARNING, "Unable to create MGCP socket: %s\n", strerror(errno));
	} else {
		if (bind(mgcpsock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
			ast_log(LOG_WARNING, "Failed to bind to %s:%d: %s\n",
					inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port),
						strerror(errno));
			close(mgcpsock);
			mgcpsock = -1;
		} else if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "MGCP Listening on %s:%d\n", 
				inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port));
	}
	pthread_mutex_unlock(&netlock);
	ast_destroy(cfg);

	/* Make sure we can register our mgcp channel type */
	if (ast_channel_register(type, tdesc, capability, mgcp_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_destroy(cfg);
		return -1;
	}
	ast_cli_register(&cli_show_endpoints);
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

int unload_module()
{
	struct mgcp_endpoint *p, *pl;
#if 0
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	if (!ast_pthread_mutex_lock(&gatelock)) {
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
#endif		
	return -1;
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

