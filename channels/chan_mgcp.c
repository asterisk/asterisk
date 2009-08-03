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

/*! \file
 *
 * \brief Implementation of Media Gateway Control Protocol
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \par See also
 * \arg \ref Config_mgcp
 *
 * \ingroup channel_drivers
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/signal.h>
#include <signal.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/say.h"
#include "asterisk/cdr.h"
#include "asterisk/astdb.h"
#include "asterisk/features.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/utils.h"
#include "asterisk/netsock.h"
#include "asterisk/causes.h"
#include "asterisk/dsp.h"
#include "asterisk/devicestate.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/event.h"

/*
 * Define to work around buggy dlink MGCP phone firmware which
 * appears not to know that "rt" is part of the "G" package.
 */
/* #define DLINK_BUGGY_FIRMWARE	*/

#define MGCPDUMPER
#define DEFAULT_EXPIRY	120
#define MAX_EXPIRY	3600
#define DIRECTMEDIA	1

#ifndef INADDR_NONE
#define INADDR_NONE (in_addr_t)(-1)
#endif

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = ""
};
static struct ast_jb_conf global_jbconf;

static const char tdesc[] = "Media Gateway Control Protocol (MGCP)";
static const char config[] = "mgcp.conf";

#define MGCP_DTMF_RFC2833	(1 << 0)
#define MGCP_DTMF_INBAND	(1 << 1)
#define MGCP_DTMF_HYBRID	(1 << 2)

#define DEFAULT_MGCP_GW_PORT	2427 /*!< From RFC 2705 */
#define DEFAULT_MGCP_CA_PORT	2727 /*!< From RFC 2705 */
#define MGCP_MAX_PACKET		1500 /*!< Also from RFC 2543, should sub headers tho */
#define DEFAULT_RETRANS		1000 /*!< How frequently to retransmit */
#define MAX_RETRANS		5    /*!< Try only 5 times for retransmissions */

/*! MGCP rtp stream modes { */
#define MGCP_CX_SENDONLY	0
#define MGCP_CX_RECVONLY	1
#define MGCP_CX_SENDRECV	2
#define MGCP_CX_CONF		3
#define MGCP_CX_CONFERENCE	3
#define MGCP_CX_MUTE		4
#define MGCP_CX_INACTIVE	4
/*! } */

static const char * const mgcp_cxmodes[] = {
	"sendonly",
	"recvonly",
	"sendrecv",
	"confrnce",
	"inactive"
};

enum {
	MGCP_CMD_EPCF,
	MGCP_CMD_CRCX,
	MGCP_CMD_MDCX,
	MGCP_CMD_DLCX,
	MGCP_CMD_RQNT,
	MGCP_CMD_NTFY,
	MGCP_CMD_AUEP,
	MGCP_CMD_AUCX,
	MGCP_CMD_RSIP
};

static char context[AST_MAX_EXTENSION] = "default";

static char language[MAX_LANGUAGE] = "";
static char musicclass[MAX_MUSICCLASS] = "";
static char parkinglot[AST_MAX_CONTEXT];
static char cid_num[AST_MAX_EXTENSION] = "";
static char cid_name[AST_MAX_EXTENSION] = "";

static int dtmfmode = 0;
static int nat = 0;

static ast_group_t cur_callergroup = 0;
static ast_group_t cur_pickupgroup = 0;

static struct {
	unsigned int tos;
	unsigned int tos_audio;
	unsigned int cos;
	unsigned int cos_audio;
} qos = { 0, 0, 0, 0 };

static int immediate = 0;

static int callwaiting = 0;

static int callreturn = 0;

static int slowsequence = 0;

static int threewaycalling = 0;

/*! This is for flashhook transfers */
static int transfer = 0;

static int cancallforward = 0;

static int singlepath = 0;

static int directmedia = DIRECTMEDIA;

static char accountcode[AST_MAX_ACCOUNT_CODE] = "";

static char mailbox[AST_MAX_EXTENSION];

static int amaflags = 0;

static int adsi = 0;

static unsigned int oseq;

/*! Wait up to 16 seconds for first digit (FXO logic) */
static int firstdigittimeout = 16000;

/*! How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

/*! How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

/*! Protect the monitoring thread, so only one process can kill or start it, and not
    when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(netlock);

AST_MUTEX_DEFINE_STATIC(monlock);

/*! This is the thread for the monitor which checks for input on the channels
    which are not currently in use. */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

static int capability = AST_FORMAT_ULAW;
static int nonCodecCapability = AST_RTP_DTMF;

static char ourhost[MAXHOSTNAMELEN];
static struct in_addr __ourip;
static int ourport;

static int mgcpdebug = 0;

static struct sched_context *sched;
static struct io_context *io;
/*! The private structures of the  mgcp channels are linked for
  ! selecting outgoing channels */
   
#define MGCP_MAX_HEADERS	64
#define MGCP_MAX_LINES		64

struct mgcp_request {
	int len;
	char *verb;
	char *identifier;
	char *endpoint;
	char *version;
	int headers;			/*!< MGCP Headers */
	char *header[MGCP_MAX_HEADERS];
	int lines;			/*!< SDP Content */
	char *line[MGCP_MAX_LINES];
	char data[MGCP_MAX_PACKET];
	int cmd;                        /*!< int version of verb = command */
	unsigned int trid;              /*!< int version of identifier = transaction id */
	struct mgcp_request *next;      /*!< next in the queue */
};

/*! \brief mgcp_message: MGCP message for queuing up */
struct mgcp_message {
	struct mgcp_endpoint *owner_ep;
	struct mgcp_subchannel *owner_sub;
	int retrans;
	unsigned long expire;
	unsigned int seqno;
	int len;
	struct mgcp_message *next;
	char buf[0];
};

#define RESPONSE_TIMEOUT 30	/*!< in seconds */

struct mgcp_response {
	time_t whensent;
	int len;
	int seqno;
	struct mgcp_response *next;
	char buf[0];
};

#define MAX_SUBS 2

#define SUB_REAL 0
#define SUB_ALT  1

struct mgcp_subchannel {
	/*! subchannel magic string. 
	   Needed to prove that any subchannel pointer passed by asterisk 
	   really points to a valid subchannel memory area.
	   Ugly.. But serves the purpose for the time being.
	 */
#define MGCP_SUBCHANNEL_MAGIC "!978!"
	char magic[6]; 
	ast_mutex_t lock;
	int id;
	struct ast_channel *owner;
	struct mgcp_endpoint *parent;
	struct ast_rtp_instance *rtp;
	struct sockaddr_in tmpdest;
	char txident[80]; /*! \todo FIXME txident is replaced by rqnt_ident in endpoint. 
			This should be obsoleted */
	char cxident[80];
	char callid[80];
	int cxmode;
	struct mgcp_request *cx_queue; /*!< pending CX commands */
	ast_mutex_t cx_queue_lock;     /*!< CX queue lock */
	int nat;
	int iseq;                      /*!< Not used? RTP? */
	int outgoing;
	int alreadygone;
	struct mgcp_subchannel *next;  /*!< for out circular linked list */
};

#define MGCP_ONHOOK  1
#define MGCP_OFFHOOK 2

#define TYPE_TRUNK 1
#define TYPE_LINE  2

struct mgcp_endpoint {
	ast_mutex_t lock;
	char name[80];
	struct mgcp_subchannel *sub;		/*!< Pointer to our current connection, channel and stuff */
	char accountcode[AST_MAX_ACCOUNT_CODE];
	char exten[AST_MAX_EXTENSION];		/*!< Extention where to start */
	char context[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char cid_num[AST_MAX_EXTENSION];	/*!< Caller*ID number */
	char cid_name[AST_MAX_EXTENSION];	/*!< Caller*ID name */
	char lastcallerid[AST_MAX_EXTENSION];	/*!< Last Caller*ID */
	char dtmf_buf[AST_MAX_EXTENSION];	/*!< place to collect digits be */
	char call_forward[AST_MAX_EXTENSION];	/*!< Last Caller*ID */
	char musicclass[MAX_MUSICCLASS];
	char curtone[80];			/*!< Current tone */
	char mailbox[AST_MAX_EXTENSION];
	char parkinglot[AST_MAX_CONTEXT];   /*!< Parkinglot */
	struct ast_event_sub *mwi_event_sub;
	ast_group_t callgroup;
	ast_group_t pickupgroup;
	int callwaiting;
	int hascallwaiting;
	int transfer;
	int threewaycalling;
	int singlepath;
	int cancallforward;
	int directmedia;
	int callreturn;
	int dnd; /* How does this affect callwait? Do we just deny a mgcp_request if we're dnd? */
	int hascallerid;
	int hidecallerid;
	int dtmfmode;
	int amaflags;
	int type;
	int slowsequence;			/*!< MS: Sequence the endpoint as a whole */
	int group;
	int iseq; /*!< Not used? */
	int lastout; /*!< tracking this on the subchannels.  Is it needed here? */
	int needdestroy; /*!< Not used? */
	int capability;
	int nonCodecCapability;
	int onhooktime;
	int msgstate; /*!< voicemail message state */
	int immediate;
	int hookstate;
	int adsi;
	char rqnt_ident[80];             /*!< request identifier */
	struct mgcp_request *rqnt_queue; /*!< pending RQNT commands */
	ast_mutex_t rqnt_queue_lock;
	struct mgcp_request *cmd_queue;  /*!< pending commands other than RQNT */
	ast_mutex_t cmd_queue_lock;
	int delme;                       /*!< needed for reload */
	int needaudit;                   /*!< needed for reload */
	struct ast_dsp *dsp; /*!< XXX Should there be a dsp/subchannel? XXX */
	/* owner is tracked on the subchannels, and the *sub indicates whos in charge */
	/* struct ast_channel *owner; */
	/* struct ast_rtp *rtp; */
	/* struct sockaddr_in tmpdest; */
	/* message go the the endpoint and not the channel so they stay here */
	struct mgcp_endpoint *next;
	struct mgcp_gateway *parent;
};

static struct mgcp_gateway {
	/* A gateway containing one or more endpoints */
	char name[80];
	int isnamedottedip; /*!< is the name FQDN or dotted ip */
	struct sockaddr_in addr;
	struct sockaddr_in defaddr;
	struct in_addr ourip;
	int dynamic;
	int expire;		/*!< XXX Should we ever expire dynamic registrations? XXX */
	struct mgcp_endpoint *endpoints;
	struct ast_ha *ha;
/* obsolete
	time_t lastouttime;
	int lastout;
	int messagepending;
*/
/* Wildcard endpoint name */
	char wcardep[30];
	struct mgcp_message *msgs; /*!< gw msg queue */
	ast_mutex_t msgs_lock;     /*!< queue lock */  
	int retransid;             /*!< retrans timer id */
	int delme;                 /*!< needed for reload */
	struct mgcp_response *responses;
	struct mgcp_gateway *next;
} *gateways;

AST_MUTEX_DEFINE_STATIC(mgcp_reload_lock);
static int mgcp_reloading = 0;

/*! \brief gatelock: mutex for gateway/endpoint lists */
AST_MUTEX_DEFINE_STATIC(gatelock);

static int mgcpsock  = -1;

static struct sockaddr_in bindaddr;

static struct ast_frame  *mgcp_read(struct ast_channel *ast);
static int transmit_response(struct mgcp_subchannel *sub, char *msg, struct mgcp_request *req, char *msgrest);
static int transmit_notify_request(struct mgcp_subchannel *sub, char *tone);
static int transmit_modify_request(struct mgcp_subchannel *sub);
static int transmit_notify_request_with_callerid(struct mgcp_subchannel *sub, char *tone, char *callernum, char *callername);
static int transmit_modify_with_sdp(struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp, int codecs);
static int transmit_connection_del(struct mgcp_subchannel *sub);
static int transmit_audit_endpoint(struct mgcp_endpoint *p);
static void start_rtp(struct mgcp_subchannel *sub);
static void handle_response(struct mgcp_endpoint *p, struct mgcp_subchannel *sub,  
                            int result, unsigned int ident, struct mgcp_request *resp);
static void dump_cmd_queues(struct mgcp_endpoint *p, struct mgcp_subchannel *sub);
static char *mgcp_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static int reload_config(int reload);

static struct ast_channel *mgcp_request(const char *type, int format, const struct ast_channel *requestor, void *data, int *cause);
static int mgcp_call(struct ast_channel *ast, char *dest, int timeout);
static int mgcp_hangup(struct ast_channel *ast);
static int mgcp_answer(struct ast_channel *ast);
static struct ast_frame *mgcp_read(struct ast_channel *ast);
static int mgcp_write(struct ast_channel *ast, struct ast_frame *frame);
static int mgcp_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen);
static int mgcp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int mgcp_senddigit_begin(struct ast_channel *ast, char digit);
static int mgcp_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int mgcp_devicestate(void *data);
static void add_header_offhook(struct mgcp_subchannel *sub, struct mgcp_request *resp);

static const struct ast_channel_tech mgcp_tech = {
	.type = "MGCP",
	.description = tdesc,
	.capabilities = AST_FORMAT_ULAW,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER,
	.requester = mgcp_request,
	.devicestate = mgcp_devicestate,
	.call = mgcp_call,
	.hangup = mgcp_hangup,
	.answer = mgcp_answer,
	.read = mgcp_read,
	.write = mgcp_write,
	.indicate = mgcp_indicate,
	.fixup = mgcp_fixup,
	.send_digit_begin = mgcp_senddigit_begin,
	.send_digit_end = mgcp_senddigit_end,
	.bridge = ast_rtp_instance_bridge,
};

static void mwi_event_cb(const struct ast_event *event, void *userdata)
{
	/* This module does not handle MWI in an event-based manner.  However, it
	 * subscribes to MWI for each mailbox that is configured so that the core
	 * knows that we care about it.  Then, chan_mgcp will get the MWI from the
	 * event cache instead of checking the mailbox directly. */
}

static int has_voicemail(struct mgcp_endpoint *p)
{
	int new_msgs;
	struct ast_event *event;
	char *mbox, *cntx;

	cntx = mbox = ast_strdupa(p->mailbox);
	strsep(&cntx, "@");
	if (ast_strlen_zero(cntx))
		cntx = "default";

	event = ast_event_get_cached(AST_EVENT_MWI,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mbox,
		AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, cntx,
		AST_EVENT_IE_END);

	if (event) {
		new_msgs = ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS);
		ast_event_destroy(event);
	} else
		new_msgs = ast_app_has_voicemail(p->mailbox, NULL);

	return new_msgs;
}

static int unalloc_sub(struct mgcp_subchannel *sub)
{
	struct mgcp_endpoint *p = sub->parent;
	if (p->sub == sub) {
		ast_log(LOG_WARNING, "Trying to unalloc the real channel %s@%s?!?\n", p->name, p->parent->name);
		return -1;
	}
	ast_debug(1, "Released sub %d of channel %s@%s\n", sub->id, p->name, p->parent->name);

	sub->owner = NULL;
	if (!ast_strlen_zero(sub->cxident)) {
		transmit_connection_del(sub);
	}
	sub->cxident[0] = '\0';
	sub->callid[0] = '\0';
	sub->cxmode = MGCP_CX_INACTIVE;
	sub->outgoing = 0;
	sub->alreadygone = 0;
	memset(&sub->tmpdest, 0, sizeof(sub->tmpdest));
	if (sub->rtp) {
		ast_rtp_instance_destroy(sub->rtp);
		sub->rtp = NULL;
	}
	dump_cmd_queues(NULL, sub); /* SC */
	return 0;
}

/* modified for new transport mechanism */
static int __mgcp_xmit(struct mgcp_gateway *gw, char *data, int len)
{
	int res;
	if (gw->addr.sin_addr.s_addr)
		res=sendto(mgcpsock, data, len, 0, (struct sockaddr *)&gw->addr, sizeof(struct sockaddr_in));
	else 
		res=sendto(mgcpsock, data, len, 0, (struct sockaddr *)&gw->defaddr, sizeof(struct sockaddr_in));
	if (res != len) {
		ast_log(LOG_WARNING, "mgcp_xmit returned %d: %s\n", res, strerror(errno));
	}
	return res;
}

static int resend_response(struct mgcp_subchannel *sub, struct mgcp_response *resp)
{
	struct mgcp_endpoint *p = sub->parent;
	int res;
	if (mgcpdebug) {
		ast_verbose("Retransmitting:\n%s\n to %s:%d\n", resp->buf, ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
	}
	res = __mgcp_xmit(p->parent, resp->buf, resp->len);
	if (res > 0)
		res = 0;
	return res;
}

static int send_response(struct mgcp_subchannel *sub, struct mgcp_request *req)
{
	struct mgcp_endpoint *p = sub->parent;
	int res;
	if (mgcpdebug) {
		ast_verbose("Transmitting:\n%s\n to %s:%d\n", req->data, ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
	}
	res = __mgcp_xmit(p->parent, req->data, req->len);
	if (res > 0)
		res = 0;
	return res;
}

/* modified for new transport framework */
static void dump_queue(struct mgcp_gateway *gw, struct mgcp_endpoint *p)
{
	struct mgcp_message *cur, *q = NULL, *w, *prev;

	ast_mutex_lock(&gw->msgs_lock);
	prev = NULL, cur = gw->msgs;
	while (cur) {
		if (!p || cur->owner_ep == p) {
			if (prev)
				prev->next = cur->next;
			else
				gw->msgs = cur->next;

			ast_log(LOG_NOTICE, "Removing message from %s transaction %u\n", 
				gw->name, cur->seqno);

			w = cur;
			cur = cur->next;
			if (q) {
				w->next = q;
			} else {
				w->next = NULL;
			}
			q = w;
		} else {
			prev = cur, cur=cur->next;
		}
	}
	ast_mutex_unlock(&gw->msgs_lock);

	while (q) {
		cur = q;
		q = q->next;
		ast_free(cur);
	}
}

static void mgcp_queue_frame(struct mgcp_subchannel *sub, struct ast_frame *f)
{
	for(;;) {
		if (sub->owner) {
			if (!ast_channel_trylock(sub->owner)) {
				ast_queue_frame(sub->owner, f);
				ast_channel_unlock(sub->owner);
				break;
			} else {
				DEADLOCK_AVOIDANCE(&sub->lock);
			}
		} else
			break;
	}
}

static void mgcp_queue_hangup(struct mgcp_subchannel *sub)
{
	for(;;) {
		if (sub->owner) {
			if (!ast_channel_trylock(sub->owner)) {
				ast_queue_hangup(sub->owner);
				ast_channel_unlock(sub->owner);
				break;
			} else {
				DEADLOCK_AVOIDANCE(&sub->lock);
			}
		} else
			break;
	}
}

static void mgcp_queue_control(struct mgcp_subchannel *sub, int control)
{
	struct ast_frame f = { AST_FRAME_CONTROL, };
	f.subclass = control;
	return mgcp_queue_frame(sub, &f);
}

static int retrans_pkt(const void *data)
{
	struct mgcp_gateway *gw = (struct mgcp_gateway *)data;
	struct mgcp_message *cur, *exq = NULL, *w, *prev;
	int res = 0;

	/* find out expired msgs */
	ast_mutex_lock(&gw->msgs_lock);

	prev = NULL, cur = gw->msgs;
	while (cur) {
		if (cur->retrans < MAX_RETRANS) {
			cur->retrans++;
			if (mgcpdebug) {
				ast_verbose("Retransmitting #%d transaction %u on [%s]\n",
					cur->retrans, cur->seqno, gw->name);
			}
			__mgcp_xmit(gw, cur->buf, cur->len);

			prev = cur;
			cur = cur->next;
		} else {
			if (prev)
				prev->next = cur->next;
			else
				gw->msgs = cur->next;

			ast_log(LOG_WARNING, "Maximum retries exceeded for transaction %u on [%s]\n",
				cur->seqno, gw->name);

			w = cur;
			cur = cur->next;

			if (exq) {
				w->next = exq;
			} else {
				w->next = NULL;
			}
			exq = w;
		}
	}

	if (!gw->msgs) {
		gw->retransid = -1;
		res = 0;
	} else {
		res = 1;
	}
	ast_mutex_unlock(&gw->msgs_lock);

	while (exq) {
		cur = exq;
		/* time-out transaction */
		handle_response(cur->owner_ep, cur->owner_sub, 406, cur->seqno, NULL); 
		exq = exq->next;
		ast_free(cur);
	}

	return res;
}

/* modified for the new transaction mechanism */
static int mgcp_postrequest(struct mgcp_endpoint *p, struct mgcp_subchannel *sub, 
                            char *data, int len, unsigned int seqno)
{
	struct mgcp_message *msg;
	struct mgcp_message *cur;
	struct mgcp_gateway *gw;
 	struct timeval now;

	msg = ast_malloc(sizeof(*msg) + len);
	if (!msg) {
		return -1;
	}
	gw = ((p && p->parent) ? p->parent : NULL);
	if (!gw) {
		ast_free(msg);
		return -1;
	}
/* SC
	time(&t);
	if (gw->messagepending && (gw->lastouttime + 20 < t)) {
		ast_log(LOG_NOTICE, "Timeout waiting for response to message:%d,  lastouttime: %ld, now: %ld.  Dumping pending queue\n",
			gw->msgs ? gw->msgs->seqno : -1, (long) gw->lastouttime, (long) t);
		dump_queue(sub->parent);
	}
*/
	msg->owner_sub = sub;
	msg->owner_ep = p;
	msg->seqno = seqno;
	msg->next = NULL;
	msg->len = len;
	msg->retrans = 0;
	memcpy(msg->buf, data, msg->len);

	ast_mutex_lock(&gw->msgs_lock);
	cur = gw->msgs;
	if (cur) {
		while(cur->next)
			cur = cur->next;
		cur->next = msg;
	} else {
		gw->msgs = msg;
	}

	now = ast_tvnow();
	msg->expire = now.tv_sec * 1000 + now.tv_usec / 1000 + DEFAULT_RETRANS;

	if (gw->retransid == -1)
		gw->retransid = ast_sched_add(sched, DEFAULT_RETRANS, retrans_pkt, (void *)gw);
	ast_mutex_unlock(&gw->msgs_lock);
/* SC
	if (!gw->messagepending) {
		gw->messagepending = 1;
		gw->lastout = seqno;
		gw->lastouttime = t;
*/
	__mgcp_xmit(gw, msg->buf, msg->len);
		/* XXX Should schedule retransmission XXX */
/* SC
	} else
		ast_debug(1, "Deferring transmission of transaction %d\n", seqno);
*/
	return 0;
}

/* modified for new transport */
static int send_request(struct mgcp_endpoint *p, struct mgcp_subchannel *sub, 
                        struct mgcp_request *req, unsigned int seqno)
{
	int res = 0;
	struct mgcp_request **queue, *q, *r, *t;
	ast_mutex_t *l;

	ast_debug(1, "Slow sequence is %d\n", p->slowsequence);
	if (p->slowsequence) {
		queue = &p->cmd_queue;
		l = &p->cmd_queue_lock;
		ast_mutex_lock(l);
	} else {
		switch (req->cmd) {
		case MGCP_CMD_DLCX:
			queue = &sub->cx_queue;
			l = &sub->cx_queue_lock;
			ast_mutex_lock(l);
			q = sub->cx_queue;
			/* delete pending cx cmds */
			while (q) {
				r = q->next;
				ast_free(q);
				q = r;
			}
			*queue = NULL;
			break;

		case MGCP_CMD_CRCX:
		case MGCP_CMD_MDCX:
			queue = &sub->cx_queue;
			l = &sub->cx_queue_lock;
			ast_mutex_lock(l);
			break;

		case MGCP_CMD_RQNT:
			queue = &p->rqnt_queue;
			l = &p->rqnt_queue_lock;
			ast_mutex_lock(l);
			break;

		default:
			queue = &p->cmd_queue;
			l = &p->cmd_queue_lock;
			ast_mutex_lock(l);
			break;
		}
	}

	r = ast_malloc(sizeof(*r));
	if (!r) {
		ast_log(LOG_WARNING, "Cannot post MGCP request: insufficient memory\n");
		ast_mutex_unlock(l);
		return -1;
	}
	memcpy(r, req, sizeof(*r));

	if (!(*queue)) {
		if (mgcpdebug) {
			ast_verbose("Posting Request:\n%s to %s:%d\n", req->data, 
				ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
		}

		res = mgcp_postrequest(p, sub, req->data, req->len, seqno);
	} else {
		if (mgcpdebug) {
			ast_verbose("Queueing Request:\n%s to %s:%d\n", req->data, 
				ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
		}
	}

	/* XXX find tail. We could also keep tail in the data struct for faster access */
	for (t = *queue; t && t->next; t = t->next);

	r->next = NULL;
	if (t)
		t->next = r;
	else
		*queue = r;

	ast_mutex_unlock(l);

	return res;
}

static int mgcp_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res;
	struct mgcp_endpoint *p;
	struct mgcp_subchannel *sub;
	char tone[50] = "";
	const char *distinctive_ring = NULL;
	struct varshead *headp;
	struct ast_var_t *current;

	if (mgcpdebug) {
		ast_verb(3, "MGCP mgcp_call(%s)\n", ast->name);
	}
	sub = ast->tech_pvt;
	p = sub->parent;
	headp = &ast->varshead;
	AST_LIST_TRAVERSE(headp,current,entries) {
		/* Check whether there is an ALERT_INFO variable */
		if (strcasecmp(ast_var_name(current),"ALERT_INFO") == 0) {
			distinctive_ring = ast_var_value(current);
		}
	}

	ast_mutex_lock(&sub->lock);
	switch (p->hookstate) {
	case MGCP_OFFHOOK:
		if (!ast_strlen_zero(distinctive_ring)) {
			snprintf(tone, sizeof(tone), "L/wt%s", distinctive_ring);
			if (mgcpdebug) {
				ast_verb(3, "MGCP distinctive callwait %s\n", tone);
			}
		} else {
			ast_copy_string(tone, "L/wt", sizeof(tone));
			if (mgcpdebug) {
				ast_verb(3, "MGCP normal callwait %s\n", tone);
			}
		}
		break;
	case MGCP_ONHOOK:
	default:
		if (!ast_strlen_zero(distinctive_ring)) {
			snprintf(tone, sizeof(tone), "L/r%s", distinctive_ring);
			if (mgcpdebug) {
				ast_verb(3, "MGCP distinctive ring %s\n", tone);
			}
		} else {
			ast_copy_string(tone, "L/rg", sizeof(tone));
			if (mgcpdebug) {
				ast_verb(3, "MGCP default ring\n");
			}
		}
		break;
	}

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "mgcp_call called on %s, neither down nor reserved\n", ast->name);
		ast_mutex_unlock(&sub->lock);
		return -1;
	}

	res = 0;
	sub->outgoing = 1;
	sub->cxmode = MGCP_CX_RECVONLY;
	if (p->type == TYPE_LINE) {
		if (!sub->rtp) {
			start_rtp(sub);
		} else {
			transmit_modify_request(sub);
		}

		if (sub->next->owner && !ast_strlen_zero(sub->next->cxident) && !ast_strlen_zero(sub->next->callid)) {
			/* try to prevent a callwait from disturbing the other connection */
			sub->next->cxmode = MGCP_CX_RECVONLY;
			transmit_modify_request(sub->next);
		}

		transmit_notify_request_with_callerid(sub, tone, ast->connected.id.number, ast->connected.id.name);
		ast_setstate(ast, AST_STATE_RINGING);

		if (sub->next->owner && !ast_strlen_zero(sub->next->cxident) && !ast_strlen_zero(sub->next->callid)) {
			/* Put the connection back in sendrecv */
			sub->next->cxmode = MGCP_CX_SENDRECV;
			transmit_modify_request(sub->next);
		}
	} else {
		ast_log(LOG_NOTICE, "Don't know how to dial on trunks yet\n");
		res = -1;
	}
	ast_mutex_unlock(&sub->lock);
	ast_queue_control(ast, AST_CONTROL_RINGING);
	return res;
}

static int mgcp_hangup(struct ast_channel *ast)
{
	struct mgcp_subchannel *sub = ast->tech_pvt;
	struct mgcp_endpoint *p = sub->parent;

	ast_debug(1, "mgcp_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_debug(1, "Asked to hangup channel not connected\n");
		return 0;
	}
	if (strcmp(sub->magic, MGCP_SUBCHANNEL_MAGIC)) {
		ast_debug(1, "Invalid magic. MGCP subchannel freed up already.\n");
		return 0;
	}
	ast_mutex_lock(&sub->lock);
	if (mgcpdebug) {
		ast_verb(3, "MGCP mgcp_hangup(%s) on %s@%s\n", ast->name, p->name, p->parent->name);
	}

	if ((p->dtmfmode & MGCP_DTMF_INBAND) && p->dsp) {
		/* check whether other channel is active. */
		if (!sub->next->owner) {
			if (p->dtmfmode & MGCP_DTMF_HYBRID)
				p->dtmfmode &= ~MGCP_DTMF_INBAND;
			if (mgcpdebug) {
				ast_verb(2, "MGCP free dsp on %s@%s\n", p->name, p->parent->name);
			}
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}
	}

	sub->owner = NULL;
	if (!ast_strlen_zero(sub->cxident)) {
		transmit_connection_del(sub);
	}
	sub->cxident[0] = '\0';
	if ((sub == p->sub) && sub->next->owner) {
		if (p->hookstate == MGCP_OFFHOOK) {
			if (sub->next->owner && ast_bridged_channel(sub->next->owner)) {
				transmit_notify_request_with_callerid(p->sub, "L/wt", ast_bridged_channel(sub->next->owner)->cid.cid_num, ast_bridged_channel(sub->next->owner)->cid.cid_name);
			}
		} else {
			/* set our other connection as the primary and swith over to it */
			p->sub = sub->next;
			p->sub->cxmode = MGCP_CX_RECVONLY;
			transmit_modify_request(p->sub);
			if (sub->next->owner && ast_bridged_channel(sub->next->owner)) {
				transmit_notify_request_with_callerid(p->sub, "L/rg", ast_bridged_channel(sub->next->owner)->cid.cid_num, ast_bridged_channel(sub->next->owner)->cid.cid_name);
			}
		}

	} else if ((sub == p->sub->next) && p->hookstate == MGCP_OFFHOOK) {
		transmit_notify_request(sub, "L/v");
	} else if (p->hookstate == MGCP_OFFHOOK) {
		transmit_notify_request(sub, "L/ro");
	} else {
		transmit_notify_request(sub, "");
	}

	ast->tech_pvt = NULL;
	sub->alreadygone = 0;
	sub->outgoing = 0;
	sub->cxmode = MGCP_CX_INACTIVE;
	sub->callid[0] = '\0';
	if (p) {
		memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
	}
	/* Reset temporary destination */
	memset(&sub->tmpdest, 0, sizeof(sub->tmpdest));
	if (sub->rtp) {
		ast_rtp_instance_destroy(sub->rtp);
		sub->rtp = NULL;
	}

	ast_module_unref(ast_module_info->self);

	if ((p->hookstate == MGCP_ONHOOK) && (!sub->next->rtp)) {
		p->hidecallerid = 0;
		if (p->hascallwaiting && !p->callwaiting) {
			ast_verb(3, "Enabling call waiting on %s\n", ast->name);
			p->callwaiting = -1;
		}
		if (has_voicemail(p)) {
			if (mgcpdebug) {
				ast_verb(3, "MGCP mgcp_hangup(%s) on %s@%s set vmwi(+)\n",
					ast->name, p->name, p->parent->name);
			}
			transmit_notify_request(sub, "L/vmwi(+)");
		} else {
			if (mgcpdebug) {
				ast_verb(3, "MGCP mgcp_hangup(%s) on %s@%s set vmwi(-)\n",
					ast->name, p->name, p->parent->name);
			}
			transmit_notify_request(sub, "L/vmwi(-)");
		}
	}
	ast_mutex_unlock(&sub->lock);
	return 0;
}

static char *handle_mgcp_show_endpoints(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mgcp_gateway  *mg;
	struct mgcp_endpoint *me;
	int hasendpoints = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mgcp show endpoints";
		e->usage =
			"Usage: mgcp show endpoints\n"
			"       Lists all endpoints known to the MGCP (Media Gateway Control Protocol) subsystem.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) 
		return CLI_SHOWUSAGE;
	ast_mutex_lock(&gatelock);
	mg = gateways;
	while(mg) {
		me = mg->endpoints;
		ast_cli(a->fd, "Gateway '%s' at %s (%s)\n", mg->name, mg->addr.sin_addr.s_addr ? ast_inet_ntoa(mg->addr.sin_addr) : ast_inet_ntoa(mg->defaddr.sin_addr), mg->dynamic ? "Dynamic" : "Static");
		while(me) {
			/* Don't show wilcard endpoint */
			if (strcmp(me->name, mg->wcardep) != 0)
				ast_cli(a->fd, "   -- '%s@%s in '%s' is %s\n", me->name, mg->name, me->context, me->sub->owner ? "active" : "idle");
			hasendpoints = 1;
			me = me->next;
		}
		if (!hasendpoints) {
			ast_cli(a->fd, "   << No Endpoints Defined >>     ");
		}
		mg = mg->next;
	}
	ast_mutex_unlock(&gatelock);
	return CLI_SUCCESS;
}

static char *handle_mgcp_audit_endpoint(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mgcp_gateway  *mg;
	struct mgcp_endpoint *me;
	int found = 0;
	char *ename,*gname, *c;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mgcp audit endpoint";
		e->usage =
			"Usage: mgcp audit endpoint <endpointid>\n"
			"       Lists the capabilities of an endpoint in the MGCP (Media Gateway Control Protocol) subsystem.\n"
			"       mgcp debug MUST be on to see the results of this command.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!mgcpdebug) {
		return CLI_SHOWUSAGE;
	}
	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	/* split the name into parts by null */
	ename = ast_strdupa(a->argv[3]);
	gname = ename;
	while (*gname) {
		if (*gname == '@') {
			*gname = 0;
			gname++;
			break;
		}
		gname++;
	}
	if (gname[0] == '[')
		gname++;
	if ((c = strrchr(gname, ']')))
		*c = '\0';
	ast_mutex_lock(&gatelock);
	mg = gateways;
	while(mg) {
		if (!strcasecmp(mg->name, gname)) {
			me = mg->endpoints;
			while(me) {
				if (!strcasecmp(me->name, ename)) {
					found = 1;
					transmit_audit_endpoint(me);
					break;
				}
				me = me->next;
			}
			if (found) {
				break;
			}
		}
		mg = mg->next;
	}
	if (!found) {
		ast_cli(a->fd, "   << Could not find endpoint >>     ");
	}
	ast_mutex_unlock(&gatelock);
	return CLI_SUCCESS;
}

static char *handle_mgcp_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "mgcp set debug {on|off}";
		e->usage =
			"Usage: mgcp set debug {on|off}\n"
			"       Enables/Disables dumping of MGCP packets for debugging purposes\n";	
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strncasecmp(a->argv[e->args - 1], "on", 2)) {
		mgcpdebug = 1;
		ast_cli(a->fd, "MGCP Debugging Enabled\n");
	} else if (!strncasecmp(a->argv[3], "off", 3)) {
		mgcpdebug = 0;
		ast_cli(a->fd, "MGCP Debugging Disabled\n");
	} else {
		return CLI_SHOWUSAGE;
	}
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_mgcp[] = {
	AST_CLI_DEFINE(handle_mgcp_audit_endpoint, "Audit specified MGCP endpoint"),
	AST_CLI_DEFINE(handle_mgcp_show_endpoints, "List defined MGCP endpoints"),
	AST_CLI_DEFINE(handle_mgcp_set_debug, "Enable/Disable MGCP debugging"),
	AST_CLI_DEFINE(mgcp_reload, "Reload MGCP configuration"),
};

static int mgcp_answer(struct ast_channel *ast)
{
	int res = 0;
	struct mgcp_subchannel *sub = ast->tech_pvt;
	struct mgcp_endpoint *p = sub->parent;

	ast_mutex_lock(&sub->lock);
	sub->cxmode = MGCP_CX_SENDRECV;
	if (!sub->rtp) {
		start_rtp(sub);
	} else {
		transmit_modify_request(sub);
	}
	ast_verb(3, "MGCP mgcp_answer(%s) on %s@%s-%d\n",
			ast->name, p->name, p->parent->name, sub->id);
	if (ast->_state != AST_STATE_UP) {
		ast_setstate(ast, AST_STATE_UP);
		ast_debug(1, "mgcp_answer(%s)\n", ast->name);
		transmit_notify_request(sub, "");
		transmit_modify_request(sub);
	}
	ast_mutex_unlock(&sub->lock);
	return res;
}

static struct ast_frame *mgcp_rtp_read(struct mgcp_subchannel *sub)
{
	/* Retrieve audio/etc from channel.  Assumes sub->lock is already held. */
	struct ast_frame *f;

	f = ast_rtp_instance_read(sub->rtp, 0);
	/* Don't send RFC2833 if we're not supposed to */
	if (f && (f->frametype == AST_FRAME_DTMF) && !(sub->parent->dtmfmode & MGCP_DTMF_RFC2833))
		return &ast_null_frame;
	if (sub->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass != sub->owner->nativeformats) {
				ast_debug(1, "Oooh, format changed to %d\n", f->subclass);
				sub->owner->nativeformats = f->subclass;
				ast_set_read_format(sub->owner, sub->owner->readformat);
				ast_set_write_format(sub->owner, sub->owner->writeformat);
			}
			/* Courtesy fearnor aka alex@pilosoft.com */
			if ((sub->parent->dtmfmode & MGCP_DTMF_INBAND) && (sub->parent->dsp)) {
#if 0
				ast_log(LOG_NOTICE, "MGCP ast_dsp_process\n");
#endif
				f = ast_dsp_process(sub->owner, sub->parent->dsp, f);
			}
		}
	}
	return f;
}


static struct ast_frame *mgcp_read(struct ast_channel *ast)
{
	struct ast_frame *f;
	struct mgcp_subchannel *sub = ast->tech_pvt;
	ast_mutex_lock(&sub->lock);
	f = mgcp_rtp_read(sub);
	ast_mutex_unlock(&sub->lock);
	return f;
}

static int mgcp_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct mgcp_subchannel *sub = ast->tech_pvt;
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
	if (sub) {
		ast_mutex_lock(&sub->lock);
		if ((sub->parent->sub == sub) || !sub->parent->singlepath) {
			if (sub->rtp) {
				res =  ast_rtp_instance_write(sub->rtp, frame);
			}
		}
		ast_mutex_unlock(&sub->lock);
	}
	return res;
}

static int mgcp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct mgcp_subchannel *sub = newchan->tech_pvt;

	ast_mutex_lock(&sub->lock);
	ast_log(LOG_NOTICE, "mgcp_fixup(%s, %s)\n", oldchan->name, newchan->name);
	if (sub->owner != oldchan) {
		ast_mutex_unlock(&sub->lock);
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, sub->owner);
		return -1;
	}
	sub->owner = newchan;
	ast_mutex_unlock(&sub->lock);
	return 0;
}

static int mgcp_senddigit_begin(struct ast_channel *ast, char digit)
{
	struct mgcp_subchannel *sub = ast->tech_pvt;
	struct mgcp_endpoint *p = sub->parent;
	int res = 0;

	ast_mutex_lock(&sub->lock);
	if (p->dtmfmode & MGCP_DTMF_INBAND || p->dtmfmode & MGCP_DTMF_HYBRID) {
		ast_log(LOG_DEBUG, "Sending DTMF using inband/hybrid\n");
		res = -1; /* Let asterisk play inband indications */
	} else if (p->dtmfmode & MGCP_DTMF_RFC2833) {
		ast_log(LOG_DEBUG, "Sending DTMF using RFC2833");
		ast_rtp_instance_dtmf_begin(sub->rtp, digit);
	} else {
		ast_log(LOG_ERROR, "Don't know about DTMF_MODE %d\n", p->dtmfmode);
	}
	ast_mutex_unlock(&sub->lock);

	return res;
}

static int mgcp_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct mgcp_subchannel *sub = ast->tech_pvt;
	struct mgcp_endpoint *p = sub->parent;
	int res = 0;
	char tmp[4];

	ast_mutex_lock(&sub->lock);
	if (p->dtmfmode & MGCP_DTMF_INBAND || p->dtmfmode & MGCP_DTMF_HYBRID) {
		ast_log(LOG_DEBUG, "Stopping DTMF using inband/hybrid\n");
		res = -1; /* Tell Asterisk to stop inband indications */
	} else if (p->dtmfmode & MGCP_DTMF_RFC2833) {
		ast_log(LOG_DEBUG, "Stopping DTMF using RFC2833\n");
		tmp[0] = 'D';
		tmp[1] = '/';
		tmp[2] = digit;
		tmp[3] = '\0';
		transmit_notify_request(sub, tmp);
                ast_rtp_instance_dtmf_end(sub->rtp, digit);
	} else {
		ast_log(LOG_ERROR, "Don't know about DTMF_MODE %d\n", p->dtmfmode);
	}
	ast_mutex_unlock(&sub->lock);

	return res;
}

/*!
 *  \brief  mgcp_devicestate: channel callback for device status monitoring
 *  \param  data tech/resource name of MGCP device to query
 *
 * Callback for device state management in channel subsystem
 * to obtain device status (up/down) of a specific MGCP endpoint
 *
 *  \return device status result (from devicestate.h) AST_DEVICE_INVALID (not available) or AST_DEVICE_UNKNOWN (available but unknown state)
 */
static int mgcp_devicestate(void *data)
{
	struct mgcp_gateway  *g;
	struct mgcp_endpoint *e = NULL;
	char *tmp, *endpt, *gw;
	int ret = AST_DEVICE_INVALID;

	endpt = ast_strdupa(data);
	if ((tmp = strchr(endpt, '@'))) {
		*tmp++ = '\0';
		gw = tmp;
	} else
		goto error;

	ast_mutex_lock(&gatelock);
	g = gateways;
	while (g) {
		if (strcasecmp(g->name, gw) == 0) {
			e = g->endpoints;
			break;
		}
		g = g->next;
	}

	if (!e)
		goto error;

	while (e) {
		if (strcasecmp(e->name, endpt) == 0)
			break;
		e = e->next;
	}

	if (!e)
		goto error;

	/*
	 * As long as the gateway/endpoint is valid, we'll
	 * assume that the device is available and its state
	 * can be tracked.
	 */
	ret = AST_DEVICE_UNKNOWN;

error:
	ast_mutex_unlock(&gatelock);
	return ret;
}

static char *control2str(int ind) {
	switch (ind) {
	case AST_CONTROL_HANGUP:
		return "Other end has hungup";
	case AST_CONTROL_RING:
		return "Local ring";
	case AST_CONTROL_RINGING:
		return "Remote end is ringing";
	case AST_CONTROL_ANSWER:
		return "Remote end has answered";
	case AST_CONTROL_BUSY:
		return "Remote end is busy";
	case AST_CONTROL_TAKEOFFHOOK:
		return "Make it go off hook";
	case AST_CONTROL_OFFHOOK:
		return "Line is off hook";
	case AST_CONTROL_CONGESTION:
		return "Congestion (circuits busy)";
	case AST_CONTROL_FLASH:
		return "Flash hook";
	case AST_CONTROL_WINK:
		return "Wink";
	case AST_CONTROL_OPTION:
		return "Set a low-level option";
	case AST_CONTROL_RADIO_KEY:
		return "Key Radio";
	case AST_CONTROL_RADIO_UNKEY:
		return "Un-Key Radio";
	}
	return "UNKNOWN";
}

static int mgcp_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen)
{
	struct mgcp_subchannel *sub = ast->tech_pvt;
	int res = 0;

	if (mgcpdebug) {
		ast_verb(3, "MGCP asked to indicate %d '%s' condition on channel %s\n",
			ind, control2str(ind), ast->name);
	}
	ast_mutex_lock(&sub->lock);
	switch(ind) {
	case AST_CONTROL_RINGING:
#ifdef DLINK_BUGGY_FIRMWARE	
		transmit_notify_request(sub, "rt");
#else
		transmit_notify_request(sub, "G/rt");
#endif		
		break;
	case AST_CONTROL_BUSY:
		transmit_notify_request(sub, "L/bz");
		break;
	case AST_CONTROL_CONGESTION:
		transmit_notify_request(sub, "G/cg");
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_SRCUPDATE:
		ast_rtp_instance_new_source(sub->rtp);
		break;
	case -1:
		transmit_notify_request(sub, "");
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", ind);
		res = -1;
	}
	ast_mutex_unlock(&sub->lock);
	return res;
}

static struct ast_channel *mgcp_new(struct mgcp_subchannel *sub, int state, const char *linkedid)
{
	struct ast_channel *tmp;
	struct mgcp_endpoint *i = sub->parent;
	int fmt;

	tmp = ast_channel_alloc(1, state, i->cid_num, i->cid_name, linkedid, i->accountcode, i->exten, i->context, i->amaflags, "MGCP/%s@%s-%d", i->name, i->parent->name, sub->id);
	if (tmp) {
		tmp->tech = &mgcp_tech;
		tmp->nativeformats = i->capability;
		if (!tmp->nativeformats)
			tmp->nativeformats = capability;
		fmt = ast_best_codec(tmp->nativeformats);
		if (sub->rtp)
			ast_channel_set_fd(tmp, 0, ast_rtp_instance_fd(sub->rtp, 0));
		if (i->dtmfmode & (MGCP_DTMF_INBAND | MGCP_DTMF_HYBRID)) {
			i->dsp = ast_dsp_new();
			ast_dsp_set_features(i->dsp, DSP_FEATURE_DIGIT_DETECT);
			/* this is to prevent clipping of dtmf tones during dsp processing */
			ast_dsp_set_digitmode(i->dsp, DSP_DIGITMODE_NOQUELCH);
		} else {
			i->dsp = NULL;
		}
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->writeformat = fmt;
		tmp->rawwriteformat = fmt;
		tmp->readformat = fmt;
		tmp->rawreadformat = fmt;
		tmp->tech_pvt = sub;
		if (!ast_strlen_zero(i->language))
			ast_string_field_set(tmp, language, i->language);
		if (!ast_strlen_zero(i->accountcode))
			ast_string_field_set(tmp, accountcode, i->accountcode);
		if (i->amaflags)
			tmp->amaflags = i->amaflags;
		sub->owner = tmp;
		ast_module_ref(ast_module_info->self);
		tmp->callgroup = i->callgroup;
		tmp->pickupgroup = i->pickupgroup;
		ast_string_field_set(tmp, call_forward, i->call_forward);
		ast_copy_string(tmp->context, i->context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));

		/* Don't use ast_set_callerid() here because it will
		 * generate a needless NewCallerID event */
		tmp->cid.cid_ani = ast_strdup(i->cid_num);
		
		if (!i->adsi)
			tmp->adsicpe = AST_ADSI_UNAVAILABLE;
		tmp->priority = 1;
		if (sub->rtp)
			ast_jb_configure(tmp, &global_jbconf);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
		ast_verb(3, "MGCP mgcp_new(%s) created in state: %s\n",
				tmp->name, ast_state2str(state));
	} else {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	}
	return tmp;
}

static char* get_sdp_by_line(char* line, char *name, int nameLen)
{
	if (strncasecmp(line, name, nameLen) == 0 && line[nameLen] == '=') {
		char* r = line + nameLen + 1;
		while (*r && (*r < 33)) ++r;
		return r;
	}
	return "";
}

static char *get_sdp(struct mgcp_request *req, char *name)
{
	int x;
	int len = strlen(name);
	char *r;

	for (x=0; x<req->lines; x++) {
		r = get_sdp_by_line(req->line[x], name, len);
		if (r[0] != '\0') return r;
	}
	return "";
}

static void sdpLineNum_iterator_init(int* iterator)
{
	*iterator = 0;
}

static char* get_sdp_iterate(int* iterator, struct mgcp_request *req, char *name)
{
	int len = strlen(name);
	char *r;
	while (*iterator < req->lines) {
		r = get_sdp_by_line(req->line[(*iterator)++], name, len);
		if (r[0] != '\0') return r;
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

/*! \brief get_csv: (SC:) get comma separated value */
static char *get_csv(char *c, int *len, char **next) 
{
	char *s;

	*next = NULL, *len = 0;
	if (!c) return NULL;

	while (*c && (*c < 33 || *c == ','))
		c++;

	s = c;
	while (*c && (*c >= 33 && *c != ','))
		c++, (*len)++;
	*next = c;

	if (*len == 0)
		s = NULL, *next = NULL;

	return s;
}

static struct mgcp_subchannel *find_subchannel_and_lock(char *name, int msgid, struct sockaddr_in *sin)
{
	struct mgcp_endpoint *p = NULL;
	struct mgcp_subchannel *sub = NULL;
	struct mgcp_gateway *g;
	char tmp[256] = "";
	char *at = NULL, *c;
	int found = 0;
	if (name) {
		ast_copy_string(tmp, name, sizeof(tmp));
		at = strchr(tmp, '@');
		if (!at) {
			ast_log(LOG_NOTICE, "Endpoint '%s' has no at sign!\n", name);
			return NULL;
		}
		*at++ = '\0';
	}
	ast_mutex_lock(&gatelock);
	if (at && (at[0] == '[')) {
		at++;
		c = strrchr(at, ']');
		if (c)
			*c = '\0';
	}
	g = gateways;
	while(g) {
		if ((!name || !strcasecmp(g->name, at)) && 
		    (sin || g->addr.sin_addr.s_addr || g->defaddr.sin_addr.s_addr)) {
			/* Found the gateway.  If it's dynamic, save it's address -- now for the endpoint */
			if (sin && g->dynamic && name) {
				if ((g->addr.sin_addr.s_addr != sin->sin_addr.s_addr) ||
					(g->addr.sin_port != sin->sin_port)) {
					memcpy(&g->addr, sin, sizeof(g->addr));
					if (ast_ouraddrfor(&g->addr.sin_addr, &g->ourip))
						memcpy(&g->ourip, &__ourip, sizeof(g->ourip));
					ast_verb(3, "Registered MGCP gateway '%s' at %s port %d\n", g->name, ast_inet_ntoa(g->addr.sin_addr), ntohs(g->addr.sin_port));
				}
			}
			/* not dynamic, check if the name matches */
			else if (name) {
				if (strcasecmp(g->name, at)) {
					g = g->next;
					continue;
				}
			}
			/* not dynamic, no name, check if the addr matches */
			else if (!name && sin) {
 				if ((g->addr.sin_addr.s_addr != sin->sin_addr.s_addr) ||
				    (g->addr.sin_port != sin->sin_port)) {
					g = g->next;
					continue;
				}
			} else {
				g = g->next;
				continue;
			}
			/* SC */
			p = g->endpoints;
			while(p) {
				ast_debug(1, "Searching on %s@%s for subchannel\n",
					p->name, g->name);
				if (msgid) {
#if 0 /* new transport mech */
					sub = p->sub;
					do {
						ast_debug(1, "Searching on %s@%s-%d for subchannel with lastout: %d\n",
							p->name, g->name, sub->id, msgid);
						if (sub->lastout == msgid) {
							ast_debug(1, "Found subchannel sub%d to handle request %d sub->lastout: %d\n",
								sub->id, msgid, sub->lastout);
							found = 1;
							break;
						}
						sub = sub->next;
					} while (sub != p->sub);
					if (found) {
						break;
					}
#endif
					/* SC */
					sub = p->sub;
					found = 1;
					/* SC */
					break;
				} else if (name && !strcasecmp(p->name, tmp)) {
					ast_debug(1, "Coundn't determine subchannel, assuming current master %s@%s-%d\n", 
						p->name, g->name, p->sub->id);
					sub = p->sub;
					found = 1;
					break;
				}
				p = p->next;
			}
			if (sub && found) {
				ast_mutex_lock(&sub->lock);
				break;
			}
		}
		g = g->next;
	}
	ast_mutex_unlock(&gatelock);
	if (!sub) {
		if (name) {
			if (g)
				ast_log(LOG_NOTICE, "Endpoint '%s' not found on gateway '%s'\n", tmp, at);
			else
				ast_log(LOG_NOTICE, "Gateway '%s' (and thus its endpoint '%s') does not exist\n", at, tmp);
		} 
	}
	return sub;
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
			if (ast_strlen_zero(req->header[f])) {
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
	if (!ast_strlen_zero(req->line[f])) 
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
		
	if (mgcpdebug) {
		ast_verbose("Verb: '%s', Identifier: '%s', Endpoint: '%s', Version: '%s'\n",
			req->verb, req->identifier, req->endpoint, req->version);
		ast_verbose("%d headers, %d lines\n", req->headers, req->lines);
	}
	if (*c) 
		ast_log(LOG_WARNING, "Odd content, extra stuff left over ('%s')\n", c);
}

static int process_sdp(struct mgcp_subchannel *sub, struct mgcp_request *req)
{
	char *m;
	char *c;
	char *a;
	char host[258];
	int len;
	int portno;
	int peercapability, peerNonCodecCapability;
	struct sockaddr_in sin;
	char *codecs;
	struct ast_hostent ahp; struct hostent *hp;
	int codec, codec_count=0;
	int iterator;
	struct mgcp_endpoint *p = sub->parent;

	/* Get codec and RTP info from SDP */
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
	if (sscanf(m, "audio %d RTP/AVP %n", &portno, &len) != 1) {
		ast_log(LOG_WARNING, "Unable to determine port number for RTP in '%s'\n", m); 
		return -1;
	}
	sin.sin_family = AF_INET;
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	sin.sin_port = htons(portno);
	ast_rtp_instance_set_remote_address(sub->rtp, &sin);
#if 0
	printf("Peer RTP is at port %s:%d\n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
#endif	
	/* Scan through the RTP payload types specified in a "m=" line: */
	ast_rtp_codecs_payloads_clear(ast_rtp_instance_get_codecs(sub->rtp), sub->rtp);
	codecs = ast_strdupa(m + len);
	while (!ast_strlen_zero(codecs)) {
		if (sscanf(codecs, "%d%n", &codec, &len) != 1) {
			if (codec_count)
				break;
			ast_log(LOG_WARNING, "Error in codec string '%s' at '%s'\n", m, codecs);
			return -1;
		}
		ast_rtp_codecs_payloads_set_m_type(ast_rtp_instance_get_codecs(sub->rtp), sub->rtp, codec);
		codec_count++;
		codecs += len;
	}

	/* Next, scan through each "a=rtpmap:" line, noting each */
	/* specified RTP payload type (with corresponding MIME subtype): */
	sdpLineNum_iterator_init(&iterator);
	while ((a = get_sdp_iterate(&iterator, req, "a"))[0] != '\0') {
		char* mimeSubtype = ast_strdupa(a); /* ensures we have enough space */
		if (sscanf(a, "rtpmap: %u %[^/]/", &codec, mimeSubtype) != 2)
			continue;
		/* Note: should really look at the 'freq' and '#chans' params too */
		ast_rtp_codecs_payloads_set_rtpmap_type(ast_rtp_instance_get_codecs(sub->rtp), sub->rtp, codec, "audio", mimeSubtype, 0);
	}

	/* Now gather all of the codecs that were asked for: */
	ast_rtp_codecs_payload_formats(ast_rtp_instance_get_codecs(sub->rtp), &peercapability, &peerNonCodecCapability);
	p->capability = capability & peercapability;
	if (mgcpdebug) {
		ast_verbose("Capabilities: us - %d, them - %d, combined - %d\n",
			capability, peercapability, p->capability);
		ast_verbose("Non-codec capabilities: us - %d, them - %d, combined - %d\n",
			nonCodecCapability, peerNonCodecCapability, p->nonCodecCapability);
	}
	if (!p->capability) {
		ast_log(LOG_WARNING, "No compatible codecs!\n");
		return -1;
	}
	return 0;
}

static int add_header(struct mgcp_request *req, const char *var, const char *value)
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
		ast_copy_string(req->data + req->len, "\r\n", sizeof(req->data) - req->len);
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
	/* check if we need brackets around the gw name */
	if (p->parent->isnamedottedip)
		snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s %d %s@[%s] MGCP 1.0\r\n", verb, oseq, p->name, p->parent->name);
	else
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
	if (oseq > 999999999)
		oseq = 1;
	init_req(p, req, verb);
	return 0;
}

static int transmit_response(struct mgcp_subchannel *sub, char *msg, struct mgcp_request *req, char *msgrest)
{
	struct mgcp_request resp;
	struct mgcp_endpoint *p = sub->parent;
	struct mgcp_response *mgr;

	respprep(&resp, p, msg, req, msgrest);
	mgr = ast_calloc(1, sizeof(*mgr) + resp.len + 1);
	if (mgr) {
		/* Store MGCP response in case we have to retransmit */
		sscanf(req->identifier, "%d", &mgr->seqno);
		time(&mgr->whensent);
		mgr->len = resp.len;
		memcpy(mgr->buf, resp.data, resp.len);
		mgr->buf[resp.len] = '\0';
		mgr->next = p->parent->responses;
		p->parent->responses = mgr;
	}
	return send_response(sub, &resp);
}


static int add_sdp(struct mgcp_request *resp, struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp)
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
	char m[256] = "";
	char a[1024] = "";
	int x;
	struct sockaddr_in dest = { 0, };
	struct mgcp_endpoint *p = sub->parent;
	/* XXX We break with the "recommendation" and send our IP, in order that our
	       peer doesn't have to ast_gethostbyname() us XXX */
	len = 0;
	if (!sub->rtp) {
		ast_log(LOG_WARNING, "No way to add SDP without an RTP structure\n");
		return -1;
	}
	ast_rtp_instance_get_local_address(sub->rtp, &sin);
	if (rtp) {
		ast_rtp_instance_get_remote_address(sub->rtp, &dest);
	} else {
		if (sub->tmpdest.sin_addr.s_addr) {
			dest.sin_addr = sub->tmpdest.sin_addr;
			dest.sin_port = sub->tmpdest.sin_port;
			/* Reset temporary destination */
			memset(&sub->tmpdest, 0, sizeof(sub->tmpdest));
		} else {
			dest.sin_addr = p->parent->ourip;
			dest.sin_port = sin.sin_port;
		}
	}
	if (mgcpdebug) {
		ast_verbose("We're at %s port %d\n", ast_inet_ntoa(p->parent->ourip), ntohs(sin.sin_port));
	}
	ast_copy_string(v, "v=0\r\n", sizeof(v));
	snprintf(o, sizeof(o), "o=root %d %d IN IP4 %s\r\n", (int)getpid(), (int)getpid(), ast_inet_ntoa(dest.sin_addr));
	ast_copy_string(s, "s=session\r\n", sizeof(s));
	snprintf(c, sizeof(c), "c=IN IP4 %s\r\n", ast_inet_ntoa(dest.sin_addr));
	ast_copy_string(t, "t=0 0\r\n", sizeof(t));
	snprintf(m, sizeof(m), "m=audio %d RTP/AVP", ntohs(dest.sin_port));
	for (x = 1; x <= AST_FORMAT_AUDIO_MASK; x <<= 1) {
		if (p->capability & x) {
			if (mgcpdebug) {
				ast_verbose("Answering with capability %d\n", x);
			}
			codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(sub->rtp), 1, x);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				strncat(m, costr, sizeof(m) - strlen(m) - 1);
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype2(1, x, 0));
				strncat(a, costr, sizeof(a) - strlen(a) - 1);
			}
		}
	}
	for (x = 1; x <= AST_RTP_MAX; x <<= 1) {
		if (p->nonCodecCapability & x) {
			if (mgcpdebug) {
				ast_verbose("Answering with non-codec capability %d\n", x);
			}
			codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(sub->rtp), 0, x);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				strncat(m, costr, sizeof(m) - strlen(m) - 1);
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype2(0, x, 0));
				strncat(a, costr, sizeof(a) - strlen(a) - 1);
				if (x == AST_RTP_DTMF) {
					/* Indicate we support DTMF...  Not sure about 16,
					   but MSN supports it so dang it, we will too... */
					snprintf(costr, sizeof costr, "a=fmtp:%d 0-16\r\n", codec);
					strncat(a, costr, sizeof(a) - strlen(a) - 1);
				}
			}
		}
	}
	strncat(m, "\r\n", sizeof(m) - strlen(m) - 1);
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

static int transmit_modify_with_sdp(struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp, int codecs)
{
	struct mgcp_request resp;
	char local[256];
	char tmp[80];
	int x;
	struct mgcp_endpoint *p = sub->parent;

	if (ast_strlen_zero(sub->cxident) && rtp) {
		/* We don't have a CXident yet, store the destination and
		   wait a bit */
		ast_rtp_instance_get_remote_address(rtp, &sub->tmpdest);
		return 0;
	}
	ast_copy_string(local, "p:20", sizeof(local));
	for (x = 1; x <= AST_FORMAT_AUDIO_MASK; x <<= 1) {
		if (p->capability & x) {
			snprintf(tmp, sizeof(tmp), ", a:%s", ast_rtp_lookup_mime_subtype2(1, x, 0));
			strncat(local, tmp, sizeof(local) - strlen(local) - 1);
		}
	}
	reqprep(&resp, p, "MDCX");
	add_header(&resp, "C", sub->callid);
	add_header(&resp, "L", local);
	add_header(&resp, "M", mgcp_cxmodes[sub->cxmode]);
	/* X header should not be sent. kept for compatibility */
	add_header(&resp, "X", sub->txident);
	add_header(&resp, "I", sub->cxident);
	/*add_header(&resp, "S", "");*/
	add_sdp(&resp, sub, rtp);
	/* fill in new fields */
	resp.cmd = MGCP_CMD_MDCX;
	resp.trid = oseq;
	return send_request(p, sub, &resp, oseq); /* SC */
}

static int transmit_connect_with_sdp(struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp)
{
	struct mgcp_request resp;
	char local[256];
	char tmp[80];
	int x;
	struct mgcp_endpoint *p = sub->parent;

	ast_copy_string(local, "p:20", sizeof(local));
	for (x = 1; x <= AST_FORMAT_AUDIO_MASK; x <<= 1) {
		if (p->capability & x) {
			snprintf(tmp, sizeof(tmp), ", a:%s", ast_rtp_lookup_mime_subtype2(1, x, 0));
			strncat(local, tmp, sizeof(local) - strlen(local) - 1);
		}
	}
	if (mgcpdebug) {
		ast_verb(3, "Creating connection for %s@%s-%d in cxmode: %s callid: %s\n",
			p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode], sub->callid);
	}
	reqprep(&resp, p, "CRCX");
	add_header(&resp, "C", sub->callid);
	add_header(&resp, "L", local);
	add_header(&resp, "M", mgcp_cxmodes[sub->cxmode]);
	/* X header should not be sent. kept for compatibility */
	add_header(&resp, "X", sub->txident);
	/*add_header(&resp, "S", "");*/
	add_sdp(&resp, sub, rtp);
	/* fill in new fields */
	resp.cmd = MGCP_CMD_CRCX;
	resp.trid = oseq;
	return send_request(p, sub, &resp, oseq);  /* SC */
}

static int transmit_notify_request(struct mgcp_subchannel *sub, char *tone)
{
	struct mgcp_request resp;
	struct mgcp_endpoint *p = sub->parent;

	if (mgcpdebug) {
		ast_verb(3, "MGCP Asked to indicate tone: %s on  %s@%s-%d in cxmode: %s\n",
			tone, p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode]);
	}
	ast_copy_string(p->curtone, tone, sizeof(p->curtone));
	reqprep(&resp, p, "RQNT");
	add_header(&resp, "X", p->rqnt_ident); /* SC */
	switch (p->hookstate) {
	case MGCP_ONHOOK:
		add_header(&resp, "R", "L/hd(N)");
		break;
	case MGCP_OFFHOOK:
		add_header_offhook(sub, &resp);
		break;
	}
	if (!ast_strlen_zero(tone)) {
		add_header(&resp, "S", tone);
	}
	/* fill in new fields */
	resp.cmd = MGCP_CMD_RQNT;
	resp.trid = oseq;
	return send_request(p, NULL, &resp, oseq); /* SC */
}

static int transmit_notify_request_with_callerid(struct mgcp_subchannel *sub, char *tone, char *callernum, char *callername)
{
	struct mgcp_request resp;
	char tone2[256];
	char *l, *n;
	struct timeval t = ast_tvnow();
	struct ast_tm tm;
	struct mgcp_endpoint *p = sub->parent;
	
	ast_localtime(&t, &tm, NULL);
	n = callername;
	l = callernum;
	if (!n)
		n = "";
	if (!l)
		l = "";

	/* Keep track of last callerid for blacklist and callreturn */
	ast_copy_string(p->lastcallerid, l, sizeof(p->lastcallerid));

	snprintf(tone2, sizeof(tone2), "%s,L/ci(%02d/%02d/%02d/%02d,%s,%s)", tone, 
		tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, l, n);
	ast_copy_string(p->curtone, tone, sizeof(p->curtone));
	reqprep(&resp, p, "RQNT");
	add_header(&resp, "X", p->rqnt_ident); /* SC */
	switch (p->hookstate) {
	case MGCP_ONHOOK:
		add_header(&resp, "R", "L/hd(N)");
		break;
	case MGCP_OFFHOOK:
		add_header_offhook(sub, &resp);
		break;
	}
	if (!ast_strlen_zero(tone2)) {
		add_header(&resp, "S", tone2);
	}
	if (mgcpdebug) {
		ast_verb(3, "MGCP Asked to indicate tone: %s on  %s@%s-%d in cxmode: %s\n",
			tone2, p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode]);
	}
	/* fill in new fields */
	resp.cmd = MGCP_CMD_RQNT;
	resp.trid = oseq;
	return send_request(p, NULL, &resp, oseq);  /* SC */
}

static int transmit_modify_request(struct mgcp_subchannel *sub)
{
	struct mgcp_request resp;
	struct mgcp_endpoint *p = sub->parent;

	if (ast_strlen_zero(sub->cxident)) {
		/* We don't have a CXident yet, store the destination and
		   wait a bit */
		return 0;
	}
	if (mgcpdebug) {
		ast_verb(3, "Modified %s@%s-%d with new mode: %s on callid: %s\n",
			p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode], sub->callid);
	}
	reqprep(&resp, p, "MDCX");
	add_header(&resp, "C", sub->callid);
	add_header(&resp, "M", mgcp_cxmodes[sub->cxmode]);
	/* X header should not be sent. kept for compatibility */
	add_header(&resp, "X", sub->txident);
	add_header(&resp, "I", sub->cxident);
	switch (sub->parent->hookstate) {
	case MGCP_ONHOOK:
		add_header(&resp, "R", "L/hd(N)");
		break;
	case MGCP_OFFHOOK:
		add_header_offhook(sub, &resp);
		break;
	}
	/* fill in new fields */
	resp.cmd = MGCP_CMD_MDCX;
	resp.trid = oseq;
	return send_request(p, sub, &resp, oseq); /* SC */
}


static void add_header_offhook(struct mgcp_subchannel *sub, struct mgcp_request *resp)
{
	struct mgcp_endpoint *p = sub->parent;

	if (p && p->sub && p->sub->owner && p->sub->owner->_state >= AST_STATE_RINGING && (p->dtmfmode & (MGCP_DTMF_INBAND | MGCP_DTMF_HYBRID)))
		add_header(resp, "R", "L/hu(N),L/hf(N)");
	else
		add_header(resp, "R", "L/hu(N),L/hf(N),D/[0-9#*](N)");
}

static int transmit_audit_endpoint(struct mgcp_endpoint *p)
{
	struct mgcp_request resp;
	reqprep(&resp, p, "AUEP");
	/* removed unknown param VS */
	/*add_header(&resp, "F", "A,R,D,S,X,N,I,T,O,ES,E,MD,M");*/
	add_header(&resp, "F", "A");
	/* fill in new fields */
	resp.cmd = MGCP_CMD_AUEP;
	resp.trid = oseq;
	return send_request(p, NULL, &resp, oseq);  /* SC */
}

static int transmit_connection_del(struct mgcp_subchannel *sub)
{
	struct mgcp_endpoint *p = sub->parent;
	struct mgcp_request resp;

	if (mgcpdebug) {
		ast_verb(3, "Delete connection %s %s@%s-%d with new mode: %s on callid: %s\n",
			sub->cxident, p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode], sub->callid);
	}
	reqprep(&resp, p, "DLCX");
	/* check if call id is avail */
	if (sub->callid[0])
		add_header(&resp, "C", sub->callid);
	/* X header should not be sent. kept for compatibility */
	add_header(&resp, "X", sub->txident);
	/* check if cxident is avail */
	if (sub->cxident[0])
		add_header(&resp, "I", sub->cxident);
	/* fill in new fields */
	resp.cmd = MGCP_CMD_DLCX;
	resp.trid = oseq;
	return send_request(p, sub, &resp, oseq);  /* SC */
}

static int transmit_connection_del_w_params(struct mgcp_endpoint *p, char *callid, char *cxident)
{
	struct mgcp_request resp;

	if (mgcpdebug) {
		ast_verb(3, "Delete connection %s %s@%s on callid: %s\n",
			cxident ? cxident : "", p->name, p->parent->name, callid ? callid : "");
	}
	reqprep(&resp, p, "DLCX");
	/* check if call id is avail */
	if (callid && *callid)
		add_header(&resp, "C", callid);
	/* check if cxident is avail */
	if (cxident && *cxident)
		add_header(&resp, "I", cxident);
	/* fill in new fields */
	resp.cmd = MGCP_CMD_DLCX;
	resp.trid = oseq;
	return send_request(p, p->sub, &resp, oseq);
}

/*! \brief  dump_cmd_queues: (SC:) cleanup pending commands */
static void dump_cmd_queues(struct mgcp_endpoint *p, struct mgcp_subchannel *sub) 
{
	struct mgcp_request *t, *q;

	if (p) {
		ast_mutex_lock(&p->rqnt_queue_lock);
		for (q = p->rqnt_queue; q; t = q->next, ast_free(q), q=t);
		p->rqnt_queue = NULL;
		ast_mutex_unlock(&p->rqnt_queue_lock);

		ast_mutex_lock(&p->cmd_queue_lock);
		for (q = p->cmd_queue; q; t = q->next, ast_free(q), q=t);
		p->cmd_queue = NULL;
		ast_mutex_unlock(&p->cmd_queue_lock);

		ast_mutex_lock(&p->sub->cx_queue_lock);
		for (q = p->sub->cx_queue; q; t = q->next, ast_free(q), q=t);
		p->sub->cx_queue = NULL;
		ast_mutex_unlock(&p->sub->cx_queue_lock);

		ast_mutex_lock(&p->sub->next->cx_queue_lock);
		for (q = p->sub->next->cx_queue; q; t = q->next, ast_free(q), q=t);
		p->sub->next->cx_queue = NULL;
		ast_mutex_unlock(&p->sub->next->cx_queue_lock);
	} else if (sub) {
		ast_mutex_lock(&sub->cx_queue_lock);
		for (q = sub->cx_queue; q; t = q->next, ast_free(q), q=t);
		sub->cx_queue = NULL;
		ast_mutex_unlock(&sub->cx_queue_lock);
	}
}


/*! \brief  find_command: (SC:) remove command transaction from queue */
static struct mgcp_request *find_command(struct mgcp_endpoint *p, struct mgcp_subchannel *sub,
                                         struct mgcp_request **queue, ast_mutex_t *l, int ident)
{
	struct mgcp_request *prev, *req;

	ast_mutex_lock(l);
	for (prev = NULL, req = *queue; req; prev = req, req = req->next) {
		if (req->trid == ident) {
			/* remove from queue */
			if (!prev)
				*queue = req->next;
			else
				prev->next = req->next;

			/* send next pending command */
			if (*queue) {
				if (mgcpdebug) {
					ast_verbose("Posting Queued Request:\n%s to %s:%d\n", (*queue)->data, 
						ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
				}

				mgcp_postrequest(p, sub, (*queue)->data, (*queue)->len, (*queue)->trid);
			}
			break;
		}
	}
	ast_mutex_unlock(l);
	return req;
}

/* modified for new transport mechanism */
static void handle_response(struct mgcp_endpoint *p, struct mgcp_subchannel *sub,  
                            int result, unsigned int ident, struct mgcp_request *resp)
{
	char *c;
	struct mgcp_request *req;
	struct mgcp_gateway *gw = p->parent;

	if (result < 200) {
		/* provisional response */
		return;
	}

	if (p->slowsequence) 
		req = find_command(p, sub, &p->cmd_queue, &p->cmd_queue_lock, ident);
	else if (sub)
		req = find_command(p, sub, &sub->cx_queue, &sub->cx_queue_lock, ident);
	else if (!(req = find_command(p, sub, &p->rqnt_queue, &p->rqnt_queue_lock, ident)))
		req = find_command(p, sub, &p->cmd_queue, &p->cmd_queue_lock, ident);

	if (!req) {
		ast_verb(3, "No command found on [%s] for transaction %d. Ignoring...\n",
				gw->name, ident);
		return;
	}

	if (p && (result >= 400) && (result <= 599)) {
		switch (result) {
		case 401:
			p->hookstate = MGCP_OFFHOOK;
			break;
		case 402:
			p->hookstate = MGCP_ONHOOK;
			break;
		case 406:
			ast_log(LOG_NOTICE, "Transaction %d timed out\n", ident);
			break;
		case 407:
			ast_log(LOG_NOTICE, "Transaction %d aborted\n", ident);
			break;
		}
		if (sub) {
			if (sub->owner) {
				ast_log(LOG_NOTICE, "Terminating on result %d from %s@%s-%d\n", 
					result, p->name, p->parent->name, sub ? sub->id:-1);
				mgcp_queue_hangup(sub);
			}
		} else {
			if (p->sub->next->owner) {
				ast_log(LOG_NOTICE, "Terminating on result %d from %s@%s-%d\n", 
					result, p->name, p->parent->name, sub ? sub->id:-1);
				mgcp_queue_hangup(p->sub);
			}

			if (p->sub->owner) {
				ast_log(LOG_NOTICE, "Terminating on result %d from %s@%s-%d\n", 
					result, p->name, p->parent->name, sub ? sub->id:-1);
				mgcp_queue_hangup(p->sub);
			}

			dump_cmd_queues(p, NULL);
		}
	}

	if (resp) {
		if (req->cmd == MGCP_CMD_CRCX) {
			if ((c = get_header(resp, "I"))) {
				if (!ast_strlen_zero(c) && sub) {
					/* if we are hanging up do not process this conn. */
					if (sub->owner) {
						if (!ast_strlen_zero(sub->cxident)) {
							if (strcasecmp(c, sub->cxident)) {
								ast_log(LOG_WARNING, "Subchannel already has a cxident. sub->cxident: %s requested %s\n", sub->cxident, c);
							}
						}
						ast_copy_string(sub->cxident, c, sizeof(sub->cxident));
						if (sub->tmpdest.sin_addr.s_addr) {
							transmit_modify_with_sdp(sub, NULL, 0);
						}
					} else {
						/* XXX delete this one
						   callid and conn id may already be lost. 
						   so the following del conn may have a side effect of 
						   cleaning up the next subchannel */
						transmit_connection_del(sub);
					}
				}
			}
		}

		if (req->cmd == MGCP_CMD_AUEP) {
			/* check stale connection ids */
			if ((c = get_header(resp, "I"))) {
				char *v, *n;
				int len;
				while ((v = get_csv(c, &len, &n))) {
					if (len) {
						if (strncasecmp(v, p->sub->cxident, len) &&
						    strncasecmp(v, p->sub->next->cxident, len)) {
							/* connection id not found. delete it */
							char cxident[80] = "";

							if (len > (sizeof(cxident) - 1))
								len = sizeof(cxident) - 1;
							ast_copy_string(cxident, v, len);
							ast_verb(3, "Non existing connection id %s on %s@%s \n",
									    cxident, p->name, gw->name);
							transmit_connection_del_w_params(p, NULL, cxident);
						}
					}
					c = n;
				}
			}

			/* Try to determine the hookstate returned from an audit endpoint command */
			if ((c = get_header(resp, "ES"))) {
				if (!ast_strlen_zero(c)) {
					if (strstr(c, "hu")) {
						if (p->hookstate != MGCP_ONHOOK) {
							/* XXX cleanup if we think we are offhook XXX */
							if ((p->sub->owner || p->sub->next->owner ) && 
							    p->hookstate == MGCP_OFFHOOK)
								mgcp_queue_hangup(sub);
							p->hookstate = MGCP_ONHOOK;

							/* update the requested events according to the new hookstate */
							transmit_notify_request(p->sub, "");

							ast_verb(3, "Setting hookstate of %s@%s to ONHOOK\n", p->name, gw->name);
							}
					} else if (strstr(c, "hd")) {
						if (p->hookstate != MGCP_OFFHOOK) {
							p->hookstate = MGCP_OFFHOOK;

							/* update the requested events according to the new hookstate */
							transmit_notify_request(p->sub, "");

							ast_verb(3, "Setting hookstate of %s@%s to OFFHOOK\n", p->name, gw->name);
							}
						}
					}
				}
			}

		if (resp && resp->lines) {
			/* do not process sdp if we are hanging up. this may be a late response */
			if (sub && sub->owner) {
				if (!sub->rtp)
					start_rtp(sub);
				if (sub->rtp)
					process_sdp(sub, resp);
			}
		}
	}

	ast_free(req);
}

static void start_rtp(struct mgcp_subchannel *sub)
{
	ast_mutex_lock(&sub->lock);
	/* check again to be on the safe side */
	if (sub->rtp) {
		ast_rtp_instance_destroy(sub->rtp);
		sub->rtp = NULL;
	}
	/* Allocate the RTP now */
	sub->rtp = ast_rtp_instance_new(NULL, sched, &bindaddr, NULL);
	if (sub->rtp && sub->owner)
		ast_channel_set_fd(sub->owner, 0, ast_rtp_instance_fd(sub->rtp, 0));
	if (sub->rtp) {
		ast_rtp_instance_set_qos(sub->rtp, qos.tos_audio, qos.cos_audio, "MGCP RTP");
		ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_NAT, sub->nat);
	}
	/* Make a call*ID */
        snprintf(sub->callid, sizeof(sub->callid), "%08lx%s", ast_random(), sub->txident);
	/* Transmit the connection create */
	transmit_connect_with_sdp(sub, NULL);
	ast_mutex_unlock(&sub->lock);
}

static void *mgcp_ss(void *data)
{
	struct ast_channel *chan = data;
	struct mgcp_subchannel *sub = chan->tech_pvt;
	struct mgcp_endpoint *p = sub->parent;
	/* char exten[AST_MAX_EXTENSION] = ""; */
	int len = 0;
	int timeout = firstdigittimeout;
	int res= 0;
	int getforward = 0;
	int loop_pause = 100;

	len = strlen(p->dtmf_buf);

	while(len < AST_MAX_EXTENSION-1) {
		res = 1;  /* Assume that we will get a digit */
		while (strlen(p->dtmf_buf) == len){
			ast_safe_sleep(chan, loop_pause);
			timeout -= loop_pause;
			if (timeout <= 0){
				res = 0;
				break;
			}
			res = 1;
		}

		timeout = 0;
		len = strlen(p->dtmf_buf);

		if (!ast_ignore_pattern(chan->context, p->dtmf_buf)) {
			/*res = tone_zone_play_tone(p->subs[index].zfd, -1);*/
			ast_indicate(chan, -1);
		} else {
			/* XXX Redundant?  We should already be playing dialtone */
			/*tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALTONE);*/
			transmit_notify_request(sub, "L/dl");
		}
		if (ast_exists_extension(chan, chan->context, p->dtmf_buf, 1, p->cid_num)) {
			if (!res || !ast_matchmore_extension(chan, chan->context, p->dtmf_buf, 1, p->cid_num)) {
				if (getforward) {
					/* Record this as the forwarding extension */
					ast_copy_string(p->call_forward, p->dtmf_buf, sizeof(p->call_forward)); 
					ast_verb(3, "Setting call forward to '%s' on channel %s\n",
							p->call_forward, chan->name);
					/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
					transmit_notify_request(sub, "L/sl");
					if (res)
						break;
					usleep(500000);
					/*res = tone_zone_play_tone(p->subs[index].zfd, -1);*/
					ast_indicate(chan, -1);
					sleep(1);
					memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
					/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALTONE);*/
					transmit_notify_request(sub, "L/dl");
					len = 0;
					getforward = 0;
				} else {
					/*res = tone_zone_play_tone(p->subs[index].zfd, -1);*/
					ast_indicate(chan, -1);
					ast_copy_string(chan->exten, p->dtmf_buf, sizeof(chan->exten));
					memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
					ast_set_callerid(chan,
						p->hidecallerid ? "" : p->cid_num,
						p->hidecallerid ? "" : p->cid_name,
						chan->cid.cid_ani ? NULL : p->cid_num);
					ast_setstate(chan, AST_STATE_RING);
					/*dahdi_enable_ec(p);*/
					if (p->dtmfmode & MGCP_DTMF_HYBRID) {
						p->dtmfmode |= MGCP_DTMF_INBAND;
						ast_indicate(chan, -1);
					}
					res = ast_pbx_run(chan);
					if (res) {
						ast_log(LOG_WARNING, "PBX exited non-zero\n");
						/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_CONGESTION);*/
						/*transmit_notify_request(p, "nbz", 1);*/
						transmit_notify_request(sub, "G/cg");
					}
					return NULL;
				}
			} else {
				/* It's a match, but they just typed a digit, and there is an ambiguous match,
				   so just set the timeout to matchdigittimeout and wait some more */
				timeout = matchdigittimeout;
			}
		} else if (res == 0) {
			ast_debug(1, "not enough digits (and no ambiguous match)...\n");
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_CONGESTION);*/
			transmit_notify_request(sub, "G/cg");
			/*dahdi_wait_event(p->subs[index].zfd);*/
			ast_hangup(chan);
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			return NULL;
		} else if (p->hascallwaiting && p->callwaiting && !strcmp(p->dtmf_buf, "*70")) {
			ast_verb(3, "Disabling call waiting on %s\n", chan->name);
			/* Disable call waiting if enabled */
			p->callwaiting = 0;
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			len = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			timeout = firstdigittimeout;
		} else if (!strcmp(p->dtmf_buf,ast_pickup_ext())) {
			/* Scan all channels and see if any there
			 * ringing channqels with that have call groups
			 * that equal this channels pickup group  
			 */
			if (ast_pickup_call(chan)) {
				ast_log(LOG_WARNING, "No call pickup possible...\n");
				/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_CONGESTION);*/
				transmit_notify_request(sub, "G/cg");
			}
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			ast_hangup(chan);
			return NULL;
		} else if (!p->hidecallerid && !strcmp(p->dtmf_buf, "*67")) {
			ast_verb(3, "Disabling Caller*ID on %s\n", chan->name);
			/* Disable Caller*ID if enabled */
			p->hidecallerid = 1;
			ast_set_callerid(chan, "", "", NULL);
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			len = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			timeout = firstdigittimeout;
		} else if (p->callreturn && !strcmp(p->dtmf_buf, "*69")) {
			res = 0;
			if (!ast_strlen_zero(p->lastcallerid)) {
				res = ast_say_digit_str(chan, p->lastcallerid, "", chan->language);
			}
			if (!res)
				/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
				transmit_notify_request(sub, "L/sl");
			break;
		} else if (!strcmp(p->dtmf_buf, "*78")) {
			/* Do not disturb */
			ast_verb(3, "Enabled DND on channel %s\n", chan->name);
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			p->dnd = 1;
			getforward = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			len = 0;
		} else if (!strcmp(p->dtmf_buf, "*79")) {
			/* Do not disturb */
			ast_verb(3, "Disabled DND on channel %s\n", chan->name);
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			p->dnd = 0;
			getforward = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			len = 0;
		} else if (p->cancallforward && !strcmp(p->dtmf_buf, "*72")) {
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			getforward = 1;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			len = 0;
		} else if (p->cancallforward && !strcmp(p->dtmf_buf, "*73")) {
			ast_verb(3, "Cancelling call forwarding on channel %s\n", chan->name);
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			memset(p->call_forward, 0, sizeof(p->call_forward));
			getforward = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			len = 0;
		} else if (!strcmp(p->dtmf_buf, ast_parking_ext()) && 
			sub->next->owner && ast_bridged_channel(sub->next->owner)) {
			/* This is a three way call, the main call being a real channel, 
			   and we're parking the first call. */
			ast_masq_park_call(ast_bridged_channel(sub->next->owner), chan, 0, NULL);
			ast_verb(3, "Parking call to '%s'\n", chan->name);
			break;
		} else if (!ast_strlen_zero(p->lastcallerid) && !strcmp(p->dtmf_buf, "*60")) {
			ast_verb(3, "Blacklisting number %s\n", p->lastcallerid);
			res = ast_db_put("blacklist", p->lastcallerid, "1");
			if (!res) {
				/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
				transmit_notify_request(sub, "L/sl");
				memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
				len = 0;
			}
		} else if (p->hidecallerid && !strcmp(p->dtmf_buf, "*82")) {
			ast_verb(3, "Enabling Caller*ID on %s\n", chan->name);
			/* Enable Caller*ID if enabled */
			p->hidecallerid = 0;
			ast_set_callerid(chan, p->cid_num, p->cid_name, NULL);
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			len = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			timeout = firstdigittimeout;
		} else if (!ast_canmatch_extension(chan, chan->context, p->dtmf_buf, 1, chan->cid.cid_num) &&
				((p->dtmf_buf[0] != '*') || (strlen(p->dtmf_buf) > 2))) {
			ast_debug(1, "Can't match %s from '%s' in context %s\n", p->dtmf_buf, chan->cid.cid_num ? chan->cid.cid_num : "<Unknown Caller>", chan->context);
			break;
		}
		if (!timeout)
			timeout = gendigittimeout;
		if (len && !ast_ignore_pattern(chan->context, p->dtmf_buf))
			/*tone_zone_play_tone(p->subs[index].zfd, -1);*/
			ast_indicate(chan, -1);
	}
#if 0
	for (;;) {
		res = ast_waitfordigit(chan, to);
		if (!res) {
			ast_debug(1, "Timeout...\n");
			break;
		}
		if (res < 0) {
			ast_debug(1, "Got hangup...\n");
			ast_hangup(chan);
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
		ast_copy_string(chan->exten, exten, sizeof(chan->exten)1);
		if (!p->rtp) {
			start_rtp(p);
		}
		ast_setstate(chan, AST_STATE_RING);
		chan->rings = 1;
		if (ast_pbx_run(chan)) {
			ast_log(LOG_WARNING, "Unable to launch PBX on %s\n", chan->name);
		} else {
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			return NULL;
		}
	}
#endif
	ast_hangup(chan);
	memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
	return NULL;
}

static int attempt_transfer(struct mgcp_endpoint *p)
{
	/* *************************
	 * I hope this works.
	 * Copied out of chan_zap
	 * Cross your fingers
	 * *************************/

	/* In order to transfer, we need at least one of the channels to
	   actually be in a call bridge.  We can't conference two applications
	   together (but then, why would we want to?) */
	if (ast_bridged_channel(p->sub->owner)) {
		/* The three-way person we're about to transfer to could still be in MOH, so
		   stop if now if appropriate */
		if (ast_bridged_channel(p->sub->next->owner))
			ast_queue_control(p->sub->next->owner, AST_CONTROL_UNHOLD);
		if (p->sub->owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->sub->next->owner), AST_CONTROL_RINGING);
		}
		if (ast_channel_masquerade(p->sub->next->owner, ast_bridged_channel(p->sub->owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
				ast_bridged_channel(p->sub->owner)->name, p->sub->next->owner->name);
			return -1;
		}
		/* Orphan the channel */
		unalloc_sub(p->sub->next);
	} else if (ast_bridged_channel(p->sub->next->owner)) {
		if (p->sub->owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->sub->next->owner), AST_CONTROL_RINGING);
		}
		ast_queue_control(p->sub->next->owner, AST_CONTROL_UNHOLD);
		if (ast_channel_masquerade(p->sub->owner, ast_bridged_channel(p->sub->next->owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
				ast_bridged_channel(p->sub->next->owner)->name, p->sub->owner->name);
			return -1;
		}
		/*swap_subs(p, SUB_THREEWAY, SUB_REAL);*/
		ast_verb(3, "Swapping %d for %d on %s@%s\n", p->sub->id, p->sub->next->id, p->name, p->parent->name);
		p->sub = p->sub->next;
		unalloc_sub(p->sub->next);
		/* Tell the caller not to hangup */
		return 1;
	} else {
		ast_debug(1, "Neither %s nor %s are in a bridge, nothing to transfer\n",
			p->sub->owner->name, p->sub->next->owner->name);
		p->sub->next->owner->_softhangup |= AST_SOFTHANGUP_DEV;
		if (p->sub->next->owner) {
			p->sub->next->alreadygone = 1;
			mgcp_queue_hangup(p->sub->next);
		}
	}
	return 0;
}

static void handle_hd_hf(struct mgcp_subchannel *sub, char *ev) 
{
	struct mgcp_endpoint *p = sub->parent;
	struct ast_channel *c;
	pthread_t t;

	/* Off hook / answer */
	if (sub->outgoing) {
		/* Answered */
		if (sub->owner) {
			if (ast_bridged_channel(sub->owner))
				ast_queue_control(sub->owner, AST_CONTROL_UNHOLD);
			sub->cxmode = MGCP_CX_SENDRECV;
			if (!sub->rtp) {
				start_rtp(sub);
			} else {
				transmit_modify_request(sub);
			}
			/*transmit_notify_request(sub, "aw");*/
			transmit_notify_request(sub, "");
			mgcp_queue_control(sub, AST_CONTROL_ANSWER);
		}
	} else {
		/* Start switch */
		/*sub->cxmode = MGCP_CX_SENDRECV;*/
		if (!sub->owner) {
			if (!sub->rtp) {
				start_rtp(sub);
			} else {
				transmit_modify_request(sub);
			}
			if (p->immediate) {
				/* The channel is immediately up. Start right away */
#ifdef DLINK_BUGGY_FIRMWARE	
				transmit_notify_request(sub, "rt");
#else
				transmit_notify_request(sub, "G/rt");
#endif		
				c = mgcp_new(sub, AST_STATE_RING, NULL);
				if (!c) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %s@%s\n", p->name, p->parent->name);
					transmit_notify_request(sub, "G/cg");
					ast_hangup(c);
				}
			} else {
				if (has_voicemail(p)) {
					transmit_notify_request(sub, "L/sl");
				} else {
					transmit_notify_request(sub, "L/dl");
				}
				c = mgcp_new(sub, AST_STATE_DOWN, NULL);
				if (c) {
					if (ast_pthread_create_detached(&t, NULL, mgcp_ss, c)) {
						ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
						ast_hangup(c);
					}
				} else {
					ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", p->name, p->parent->name);
				}
			}
		} else {
			if (p->hookstate == MGCP_OFFHOOK) {
				ast_log(LOG_WARNING, "Off hook, but already have owner on %s@%s\n", p->name, p->parent->name);
			} else {
				ast_log(LOG_WARNING, "On hook, but already have owner on %s@%s\n", p->name, p->parent->name);
				ast_log(LOG_WARNING, "If we're onhook why are we here trying to handle a hd or hf?\n");
			}
			if (ast_bridged_channel(sub->owner))
				ast_queue_control(sub->owner, AST_CONTROL_UNHOLD);
			sub->cxmode = MGCP_CX_SENDRECV;
			if (!sub->rtp) {
				start_rtp(sub);
			} else {
				transmit_modify_request(sub);
			}
			/*transmit_notify_request(sub, "aw");*/
			transmit_notify_request(sub, "");
			/*ast_queue_control(sub->owner, AST_CONTROL_ANSWER);*/
		}
	}
}

static int handle_request(struct mgcp_subchannel *sub, struct mgcp_request *req, struct sockaddr_in *sin)
{
	char *ev, *s;
	struct ast_frame f = { 0, };
	struct mgcp_endpoint *p = sub->parent;
	struct mgcp_gateway *g = NULL;
	int res;

	if (mgcpdebug) {
		ast_verbose("Handling request '%s' on %s@%s\n", req->verb, p->name, p->parent->name);
	}
	/* Clear out potential response */
	if (!strcasecmp(req->verb, "RSIP")) {
		/* Test if this RSIP request is just a keepalive */
		if(!strcasecmp( get_header(req, "RM"), "X-keepalive")) {
			ast_verb(3, "Received keepalive request from %s@%s\n", p->name, p->parent->name);
			transmit_response(sub, "200", req, "OK");
		} else {
			dump_queue(p->parent, p);
			dump_cmd_queues(p, NULL);
			
			if ((strcmp(p->name, p->parent->wcardep) != 0)) {
				ast_verb(3, "Resetting interface %s@%s\n", p->name, p->parent->name);
			}
			/* For RSIP on wildcard we reset all endpoints */
			if (!strcmp(p->name, p->parent->wcardep)) {
				/* Reset all endpoints */
				struct mgcp_endpoint *tmp_ep;
				
				g = p->parent;
				tmp_ep = g->endpoints;
				while (tmp_ep) {
					/*if ((strcmp(tmp_ep->name, "*") != 0) && (strcmp(tmp_ep->name, "aaln/" "*") != 0)) {*/
					if (strcmp(tmp_ep->name, g->wcardep) != 0) {
						struct mgcp_subchannel *tmp_sub, *first_sub;
						ast_verb(3, "Resetting interface %s@%s\n", tmp_ep->name, p->parent->name);
						
						first_sub = tmp_ep->sub;
						tmp_sub = tmp_ep->sub;
						while (tmp_sub) {
							mgcp_queue_hangup(tmp_sub);
							tmp_sub = tmp_sub->next;
							if (tmp_sub == first_sub)
								break;
						}
					}
					tmp_ep = tmp_ep->next;
				}
			} else if (sub->owner) {
				mgcp_queue_hangup(sub);
			}
			transmit_response(sub, "200", req, "OK");
			/* We dont send NTFY or AUEP to wildcard ep */
			if (strcmp(p->name, p->parent->wcardep) != 0) {
				transmit_notify_request(sub, "");
				/* Audit endpoint. 
				 Idea is to prevent lost lines due to race conditions 
				*/
				transmit_audit_endpoint(p);
			}
		}
	} else if (!strcasecmp(req->verb, "NTFY")) {
		/* Acknowledge and be sure we keep looking for the same things */
		transmit_response(sub, "200", req, "OK");
		/* Notified of an event */
		ev = get_header(req, "O");
		s = strchr(ev, '/');
		if (s) ev = s + 1;
		ast_debug(1, "Endpoint '%s@%s-%d' observed '%s'\n", p->name, p->parent->name, sub->id, ev);
		/* Keep looking for events unless this was a hangup */
		if (strcasecmp(ev, "hu") && strcasecmp(ev, "hd") && strcasecmp(ev, "ping")) {
			transmit_notify_request(sub, p->curtone);
		}
		if (!strcasecmp(ev, "hd")) {
			p->hookstate = MGCP_OFFHOOK;
			sub->cxmode = MGCP_CX_SENDRECV;
			handle_hd_hf(sub, ev);
		} else if (!strcasecmp(ev, "hf")) {
			/* We can assume we are offhook if we received a hookflash */
			/* First let's just do call wait and ignore threeway */
			/* We're currently in charge */
			if (p->hookstate != MGCP_OFFHOOK) {
				/* Cisco c7940 sends hf even if the phone is onhook */
				/* Thanks to point on IRC for pointing this out */
				return -1;
			}
			/* do not let * conference two down channels */  
			if (sub->owner && sub->owner->_state == AST_STATE_DOWN && !sub->next->owner)
				return -1;

			if (p->callwaiting || p->transfer || p->threewaycalling) {
				ast_verb(3, "Swapping %d for %d on %s@%s\n", p->sub->id, p->sub->next->id, p->name, p->parent->name);
				p->sub = p->sub->next;

				/* transfer control to our next subchannel */
				if (!sub->next->owner) {
					/* plave the first call on hold and start up a new call */
					sub->cxmode = MGCP_CX_MUTE;
					ast_verb(3, "MGCP Muting %d on %s@%s\n", sub->id, p->name, p->parent->name);
					transmit_modify_request(sub);
					if (sub->owner && ast_bridged_channel(sub->owner))
						ast_queue_control(sub->owner, AST_CONTROL_HOLD);
					sub->next->cxmode = MGCP_CX_RECVONLY;
					handle_hd_hf(sub->next, ev);
				} else if (sub->owner && sub->next->owner) {
					/* We've got two active calls lets decide whether or not to conference or just flip flop */
					if ((!sub->outgoing) && (!sub->next->outgoing)) {
						/* We made both calls lets conferenct */
						ast_verb(3, "MGCP Conferencing %d and %d on %s@%s\n",
								sub->id, sub->next->id, p->name, p->parent->name);
						sub->cxmode = MGCP_CX_CONF;
						sub->next->cxmode = MGCP_CX_CONF;
						if (ast_bridged_channel(sub->next->owner))
							ast_queue_control(sub->next->owner, AST_CONTROL_UNHOLD);
						transmit_modify_request(sub);
						transmit_modify_request(sub->next);
					} else {
						/* Let's flipflop between calls */
						/* XXX Need to check for state up ??? */
						/* XXX Need a way to indicate the current call, or maybe the call that's waiting */
						ast_verb(3, "We didn't make one of the calls FLIPFLOP %d and %d on %s@%s\n",
								sub->id, sub->next->id, p->name, p->parent->name);
						sub->cxmode = MGCP_CX_MUTE;
						ast_verb(3, "MGCP Muting %d on %s@%s\n", sub->id, p->name, p->parent->name);
						transmit_modify_request(sub);
						if (ast_bridged_channel(sub->owner))
							ast_queue_control(sub->owner, AST_CONTROL_HOLD);
                        
						if (ast_bridged_channel(sub->next->owner)) 
							ast_queue_control(sub->next->owner, AST_CONTROL_HOLD);
                        
						handle_hd_hf(sub->next, ev);
					}
				} else {
					/* We've most likely lost one of our calls find an active call and bring it up */
					if (sub->owner) {
						p->sub = sub;
					} else if (sub->next->owner) {
						p->sub = sub->next;
					} else {
						/* We seem to have lost both our calls */
						/* XXX - What do we do now? */
						return -1;
					}
					if (ast_bridged_channel(p->sub->owner))
						ast_queue_control(p->sub->owner, AST_CONTROL_UNHOLD);
					p->sub->cxmode = MGCP_CX_SENDRECV;
					transmit_modify_request(p->sub);
				}
			} else {
				ast_log(LOG_WARNING, "Callwaiting, call transfer or threeway calling not enabled on endpoint %s@%s\n", 
					p->name, p->parent->name);
			}
		} else if (!strcasecmp(ev, "hu")) {
			p->hookstate = MGCP_ONHOOK;
			sub->cxmode = MGCP_CX_RECVONLY;
			ast_debug(1, "MGCP %s@%s Went on hook\n", p->name, p->parent->name);
			/* Do we need to send MDCX before a DLCX ?
			if (sub->rtp) {
				transmit_modify_request(sub);
			}
			*/
			if (p->transfer && (sub->owner && sub->next->owner) && ((!sub->outgoing) || (!sub->next->outgoing))) {
				/* We're allowed to transfer, we have two avtive calls and */
				/* we made at least one of the calls.  Let's try and transfer */
				ast_mutex_lock(&p->sub->next->lock);
				res = attempt_transfer(p);
				if (res < 0) {
					if (p->sub->next->owner) {
						sub->next->alreadygone = 1;
						mgcp_queue_hangup(sub->next);
					}
				} else if (res) {
					ast_log(LOG_WARNING, "Transfer attempt failed\n");
					ast_mutex_unlock(&p->sub->next->lock);
					return -1;
				}
				ast_mutex_unlock(&p->sub->next->lock);
			} else {
				/* Hangup the current call */
				/* If there is another active call, mgcp_hangup will ring the phone with the other call */
				if (sub->owner) {
					sub->alreadygone = 1;
					mgcp_queue_hangup(sub);
				} else {
					ast_verb(3, "MGCP handle_request(%s@%s-%d) ast_channel already destroyed, resending DLCX.\n",
							p->name, p->parent->name, sub->id);
					/* Instruct the other side to remove the connection since it apparently *
					 * still thinks the channel is active. *
					 * For Cisco IAD2421 /BAK/ */
					transmit_connection_del(sub);
				}
			}
			if ((p->hookstate == MGCP_ONHOOK) && (!sub->rtp) && (!sub->next->rtp)) {
				p->hidecallerid = 0;
				if (p->hascallwaiting && !p->callwaiting) {
					ast_verb(3, "Enabling call waiting on MGCP/%s@%s-%d\n", p->name, p->parent->name, sub->id);
					p->callwaiting = -1;
				}
				if (has_voicemail(p)) {
					ast_verb(3, "MGCP handle_request(%s@%s) set vmwi(+)\n", p->name, p->parent->name);
					transmit_notify_request(sub, "L/vmwi(+)");
				} else {
					ast_verb(3, "MGCP handle_request(%s@%s) set vmwi(-)\n", p->name, p->parent->name);
					transmit_notify_request(sub, "L/vmwi(-)");
				}
			}
		} else if ((strlen(ev) == 1) && 
				(((ev[0] >= '0') && (ev[0] <= '9')) ||
				 ((ev[0] >= 'A') && (ev[0] <= 'D')) ||
				  (ev[0] == '*') || (ev[0] == '#'))) {
			if (sub && sub->owner && (sub->owner->_state >=  AST_STATE_UP)) {
				f.frametype = AST_FRAME_DTMF;
				f.subclass = ev[0];
				f.src = "mgcp";
				/* XXX MUST queue this frame to all subs in threeway call if threeway call is active */
				mgcp_queue_frame(sub, &f);
				ast_mutex_lock(&sub->next->lock);
				if (sub->next->owner)
					mgcp_queue_frame(sub->next, &f);
				ast_mutex_unlock(&sub->next->lock);
				if (strstr(p->curtone, "wt") && (ev[0] == 'A')) {
					memset(p->curtone, 0, sizeof(p->curtone));
				}
			} else {
				p->dtmf_buf[strlen(p->dtmf_buf)] = ev[0];
				p->dtmf_buf[strlen(p->dtmf_buf)] = '\0';
			}
		} else if (!strcasecmp(ev, "T")) {
			/* Digit timeout -- unimportant */
		} else if (!strcasecmp(ev, "ping")) {
			/* ping -- unimportant */
		} else {
			ast_log(LOG_NOTICE, "Received unknown event '%s' from %s@%s\n", ev, p->name, p->parent->name);
		}
	} else {
		ast_log(LOG_WARNING, "Unknown verb '%s' received from %s\n", req->verb, ast_inet_ntoa(sin->sin_addr));
		transmit_response(sub, "510", req, "Unknown verb");
	}
	return 0;
}

static int find_and_retrans(struct mgcp_subchannel *sub, struct mgcp_request *req)
{
	int seqno=0;
	time_t now;
	struct mgcp_response *prev = NULL, *cur, *next, *answer=NULL;
	time(&now);
	if (sscanf(req->identifier, "%d", &seqno) != 1) 
		seqno = 0;
	cur = sub->parent->parent->responses;
	while(cur) {
		next = cur->next;
		if (now - cur->whensent > RESPONSE_TIMEOUT) {
			/* Delete this entry */
			if (prev)
				prev->next = next;
			else
				sub->parent->parent->responses = next;
			ast_free(cur);
		} else {
			if (seqno == cur->seqno)
				answer = cur;
			prev = cur;
		}
		cur = next;
	}
	if (answer) {
		resend_response(sub, answer);
		return 1;
	}
	return 0;
}

static int mgcpsock_read(int *id, int fd, short events, void *ignore)
{
	struct mgcp_request req;
	struct sockaddr_in sin;
	struct mgcp_subchannel *sub;
	int res;
	socklen_t len;
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
	if (mgcpdebug) {
		ast_verbose("MGCP read: \n%s\nfrom %s:%d\n", req.data, ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	}
	parse(&req);
	if (req.headers < 1) {
		/* Must have at least one header */
		return 1;
	}
	if (ast_strlen_zero(req.identifier)) {
		ast_log(LOG_NOTICE, "Message from %s missing identifier\n", ast_inet_ntoa(sin.sin_addr));
		return 1;
	}

	if (sscanf(req.verb, "%d", &result) && sscanf(req.identifier, "%d", &ident)) {
		/* Try to find who this message is for, if it's important */
		sub = find_subchannel_and_lock(NULL, ident, &sin);
		if (sub) {
			struct mgcp_gateway *gw = sub->parent->parent;
			struct mgcp_message *cur, *prev;

			ast_mutex_unlock(&sub->lock);
			ast_mutex_lock(&gw->msgs_lock);
			for (prev = NULL, cur = gw->msgs; cur; prev = cur, cur = cur->next) {
				if (cur->seqno == ident) {
					ast_debug(1, "Got response back on transaction %d\n", ident);
					if (prev)
						prev->next = cur->next;
					else
						gw->msgs = cur->next;
					break;
				}
			}

			/* stop retrans timer if the queue is empty */
			if (!gw->msgs) {
				AST_SCHED_DEL(sched, gw->retransid);
			}

			ast_mutex_unlock(&gw->msgs_lock);
			if (cur) {
				handle_response(cur->owner_ep, cur->owner_sub, result, ident, &req);
				ast_free(cur);
				return 1;
			}

			ast_log(LOG_NOTICE, "Got response back on [%s] for transaction %d we aren't sending?\n", 
				gw->name, ident);
		}
	} else {
		if (ast_strlen_zero(req.endpoint) || 
		    	ast_strlen_zero(req.version) || 
			ast_strlen_zero(req.verb)) {
			ast_log(LOG_NOTICE, "Message must have a verb, an idenitifier, version, and endpoint\n");
			return 1;
		}
		/* Process request, with iflock held */
		sub = find_subchannel_and_lock(req.endpoint, 0, &sin);
		if (sub) {
			/* look first to find a matching response in the queue */
			if (!find_and_retrans(sub, &req))
				/* pass the request off to the currently mastering subchannel */
				handle_request(sub, &req, &sin);
			ast_mutex_unlock(&sub->lock);
		}
	}
	return 1;
}

static int *mgcpsock_read_id = NULL;

static void *do_monitor(void *data)
{
	int res;
	int reloading;
	/*struct mgcp_gateway *g;*/
	/*struct mgcp_endpoint *e;*/
	/*time_t thispass = 0, lastpass = 0;*/

	/* Add an I/O event to our UDP socket */
	if (mgcpsock > -1) 
		mgcpsock_read_id = ast_io_add(io, mgcpsock, mgcpsock_read, AST_IO_IN, NULL);
	
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
	for(;;) {
		/* Check for a reload request */
		ast_mutex_lock(&mgcp_reload_lock);
		reloading = mgcp_reloading;
		mgcp_reloading = 0;
		ast_mutex_unlock(&mgcp_reload_lock);
		if (reloading) {
			ast_verb(1, "Reloading MGCP\n");
			reload_config(1);
			/* Add an I/O event to our UDP socket */
			if (mgcpsock > -1 && !mgcpsock_read_id) {
				mgcpsock_read_id = ast_io_add(io, mgcpsock, mgcpsock_read, AST_IO_IN, NULL);
			}
		}

		/* Check for interfaces needing to be killed */
		/* Don't let anybody kill us right away.  Nobody should lock the interface list
		   and wait for the monitor list, but the other way around is okay. */
		ast_mutex_lock(&monlock);
		/* Lock the network interface */
		ast_mutex_lock(&netlock);

#if 0
		/* XXX THIS IS COMPLETELY HOSED */
		/* The gateway goes into a state of panic */
		/* If the vmwi indicator is sent while it is reseting interfaces */
		lastpass = thispass;
		thispass = time(NULL);
		g = gateways;
		while(g) {
			if (thispass != lastpass) {
				e = g->endpoints;
				while(e) {
					if (e->type == TYPE_LINE) {
						res = has_voicemail(e);
						if ((e->msgstate != res) && (e->hookstate == MGCP_ONHOOK) && (!e->rtp)){
							if (res) {
								transmit_notify_request(e, "L/vmwi(+)");
							} else {
								transmit_notify_request(e, "L/vmwi(-)");
							}
							e->msgstate = res;
							e->onhooktime = thispass;
						}
					}
					e = e->next;
				}
			}
			g = g->next;
		}
#endif
		/* Okay, now that we know what to do, release the network lock */
		ast_mutex_unlock(&netlock);
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		ast_mutex_unlock(&monlock);
		pthread_testcancel();
		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		/* copied from chan_sip.c */
		if ((res < 0) || (res > 1000))
			res = 1000;
		res = ast_io_wait(io, res);
		ast_mutex_lock(&monlock);
		if (res >= 0) 
			ast_sched_runq(sched);
		ast_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
}

static int restart_monitor(void)
{
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
		/* Start a new monitor */
		if (ast_pthread_create_background(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static struct ast_channel *mgcp_request(const char *type, int format, const struct ast_channel *requestor, void *data, int *cause)
{
	int oldformat;
	struct mgcp_subchannel *sub;
	struct ast_channel *tmpc = NULL;
	char tmp[256];
	char *dest = data;

	oldformat = format;
	format &= capability;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", format);
		return NULL;
	}
	ast_copy_string(tmp, dest, sizeof(tmp));
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_NOTICE, "MGCP Channels require an endpoint\n");
		return NULL;
	}
	sub = find_subchannel_and_lock(tmp, 0, NULL);
	if (!sub) {
		ast_log(LOG_WARNING, "Unable to find MGCP endpoint '%s'\n", tmp);
		*cause = AST_CAUSE_UNREGISTERED;
		return NULL;
	}
	
	ast_verb(3, "MGCP mgcp_request(%s)\n", tmp);
	ast_verb(3, "MGCP cw: %d, dnd: %d, so: %d, sno: %d\n",
			sub->parent->callwaiting, sub->parent->dnd, sub->owner ? 1 : 0, sub->next->owner ? 1: 0);
	/* Must be busy */
	if (((sub->parent->callwaiting) && ((sub->owner) && (sub->next->owner))) ||
		((!sub->parent->callwaiting) && (sub->owner)) ||
		 (sub->parent->dnd && (ast_strlen_zero(sub->parent->call_forward)))) {
		if (sub->parent->hookstate == MGCP_ONHOOK) {
			if (has_voicemail(sub->parent)) {
				transmit_notify_request(sub,"L/vmwi(+)");
			} else {
				transmit_notify_request(sub,"L/vmwi(-)");
			}
		}
		*cause = AST_CAUSE_BUSY;
		ast_mutex_unlock(&sub->lock);
		return NULL;
	}
	tmpc = mgcp_new(sub->owner ? sub->next : sub, AST_STATE_DOWN, requestor ? requestor->linkedid : NULL);
	ast_mutex_unlock(&sub->lock);
	if (!tmpc)
		ast_log(LOG_WARNING, "Unable to make channel for '%s'\n", tmp);
	restart_monitor();
	return tmpc;
}

/* modified for reload support */
/*! \brief  build_gateway: parse mgcp.conf and create gateway/endpoint structures */
static struct mgcp_gateway *build_gateway(char *cat, struct ast_variable *v)
{
	struct mgcp_gateway *gw;
	struct mgcp_endpoint *e;
	struct mgcp_subchannel *sub;
	/*char txident[80];*/
	int i=0, y=0;
	int gw_reload = 0;
	int ep_reload = 0;
	directmedia = DIRECTMEDIA;

	/* locate existing gateway */
	gw = gateways;
	while (gw) {
		if (!strcasecmp(cat, gw->name)) {
			/* gateway already exists */
			gw->delme = 0;
			gw_reload = 1;
			break;
		}
		gw = gw->next;
	}

	if (!gw)
		gw = ast_calloc(1, sizeof(*gw));

	if (gw) {
		if (!gw_reload) {
			gw->expire = -1;
			gw->retransid = -1; /* SC */
			ast_mutex_init(&gw->msgs_lock);
			ast_copy_string(gw->name, cat, sizeof(gw->name));
			/* check if the name is numeric ip */
			if ((strchr(gw->name, '.')) && inet_addr(gw->name) != INADDR_NONE)
				gw->isnamedottedip = 1;
		}
		while(v) {
			if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					/* They'll register with us */
					gw->dynamic = 1;
					memset(&gw->addr.sin_addr, 0, 4);
					if (gw->addr.sin_port) {
						/* If we've already got a port, make it the default rather than absolute */
						gw->defaddr.sin_port = gw->addr.sin_port;
						gw->addr.sin_port = 0;
					}
				} else {
					/* Non-dynamic.  Make sure we become that way if we're not */
					AST_SCHED_DEL(sched, gw->expire);
					gw->dynamic = 0;
					if (ast_get_ip(&gw->addr, v->value)) {
						if (!gw_reload) {
							ast_mutex_destroy(&gw->msgs_lock);
							ast_free(gw);
						}
						return NULL;
					}
				}
			} else if (!strcasecmp(v->name, "defaultip")) {
				if (ast_get_ip(&gw->defaddr, v->value)) {
					if (!gw_reload) {
						ast_mutex_destroy(&gw->msgs_lock);
						ast_free(gw);
					}
					return NULL;
				}
			} else if (!strcasecmp(v->name, "permit") ||
				!strcasecmp(v->name, "deny")) {
				gw->ha = ast_append_ha(v->name, v->value, gw->ha, NULL);
			} else if (!strcasecmp(v->name, "port")) {
				gw->addr.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "context")) {
				ast_copy_string(context, v->value, sizeof(context));
			} else if (!strcasecmp(v->name, "dtmfmode")) {
				if (!strcasecmp(v->value, "inband"))
					dtmfmode = MGCP_DTMF_INBAND;
				else if (!strcasecmp(v->value, "rfc2833")) 
					dtmfmode = MGCP_DTMF_RFC2833;
				else if (!strcasecmp(v->value, "hybrid"))
					dtmfmode = MGCP_DTMF_HYBRID;
				else if (!strcasecmp(v->value, "none")) 
					dtmfmode = 0;
				else
					ast_log(LOG_WARNING, "'%s' is not a valid DTMF mode at line %d\n", v->value, v->lineno);
			} else if (!strcasecmp(v->name, "nat")) {
				nat = ast_true(v->value);
			} else if (!strcasecmp(v->name, "callerid")) {
				if (!strcasecmp(v->value, "asreceived")) {
					cid_num[0] = '\0';
					cid_name[0] = '\0';
				} else {
					ast_callerid_split(v->value, cid_name, sizeof(cid_name), cid_num, sizeof(cid_num));
				}
			} else if (!strcasecmp(v->name, "language")) {
				ast_copy_string(language, v->value, sizeof(language));
			} else if (!strcasecmp(v->name, "accountcode")) {
				ast_copy_string(accountcode, v->value, sizeof(accountcode));
			} else if (!strcasecmp(v->name, "amaflags")) {
				y = ast_cdr_amaflags2int(v->value);
				if (y < 0) {
					ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
				} else {
					amaflags = y;
				}
			} else if (!strcasecmp(v->name, "musiconhold")) {
				ast_copy_string(musicclass, v->value, sizeof(musicclass));
			} else if (!strcasecmp(v->name, "parkinglot")) {
				ast_copy_string(parkinglot, v->value, sizeof(parkinglot));
			} else if (!strcasecmp(v->name, "callgroup")) {
				cur_callergroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "pickupgroup")) {
				cur_pickupgroup = ast_get_group(v->value);
			} else if (!strcasecmp(v->name, "immediate")) {
				immediate = ast_true(v->value);
			} else if (!strcasecmp(v->name, "cancallforward")) {
				cancallforward = ast_true(v->value);
			} else if (!strcasecmp(v->name, "singlepath")) {
				singlepath = ast_true(v->value);
			} else if (!strcasecmp(v->name, "directmedia") || !strcasecmp(v->name, "canreinvite")) {
				directmedia = ast_true(v->value);
			} else if (!strcasecmp(v->name, "mailbox")) {
				ast_copy_string(mailbox, v->value, sizeof(mailbox));
			} else if (!strcasecmp(v->name, "hasvoicemail")) {
				if (ast_true(v->value) && ast_strlen_zero(mailbox)) {
					ast_copy_string(mailbox, gw->name, sizeof(mailbox));
				}
			} else if (!strcasecmp(v->name, "adsi")) {
				adsi = ast_true(v->value);
			} else if (!strcasecmp(v->name, "callreturn")) {
				callreturn = ast_true(v->value);
			} else if (!strcasecmp(v->name, "callwaiting")) {
				callwaiting = ast_true(v->value);
			} else if (!strcasecmp(v->name, "slowsequence")) {
				slowsequence = ast_true(v->value);
			} else if (!strcasecmp(v->name, "transfer")) {
				transfer = ast_true(v->value);
			} else if (!strcasecmp(v->name, "threewaycalling")) {
				threewaycalling = ast_true(v->value);
			} else if (!strcasecmp(v->name, "wcardep")) {
				/* locate existing endpoint */
				e = gw->endpoints;
				while (e) {
					if (!strcasecmp(v->value, e->name)) {
						/* endpoint already exists */
						e->delme = 0;
						ep_reload = 1;
						break;
					}
					e = e->next;
				}

				if (!e) {
					/* Allocate wildcard endpoint */
					e = ast_calloc(1, sizeof(*e));
					ep_reload = 0;
				}

				if (e) {
					if (!ep_reload) {
						memset(e, 0, sizeof(struct mgcp_endpoint));
						ast_mutex_init(&e->lock);
						ast_mutex_init(&e->rqnt_queue_lock);
						ast_mutex_init(&e->cmd_queue_lock);
						ast_copy_string(e->name, v->value, sizeof(e->name));
						e->needaudit = 1;
					}
					ast_copy_string(gw->wcardep, v->value, sizeof(gw->wcardep));
					/* XXX Should we really check for uniqueness?? XXX */
					ast_copy_string(e->accountcode, accountcode, sizeof(e->accountcode));
					ast_copy_string(e->context, context, sizeof(e->context));
					ast_copy_string(e->cid_num, cid_num, sizeof(e->cid_num));
					ast_copy_string(e->cid_name, cid_name, sizeof(e->cid_name));
					ast_copy_string(e->language, language, sizeof(e->language));
					ast_copy_string(e->musicclass, musicclass, sizeof(e->musicclass));
					ast_copy_string(e->mailbox, mailbox, sizeof(e->mailbox));
					ast_copy_string(e->parkinglot, parkinglot, sizeof(e->parkinglot));
					if (!ast_strlen_zero(e->mailbox)) {
						char *mbox, *cntx;
						cntx = mbox = ast_strdupa(e->mailbox);
						strsep(&cntx, "@");
						if (ast_strlen_zero(cntx))
							cntx = "default";
						e->mwi_event_sub = ast_event_subscribe(AST_EVENT_MWI, mwi_event_cb, "MGCP MWI subscription", NULL,
							AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mbox,
							AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, cntx,
							AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_EXISTS,
							AST_EVENT_IE_END);
					}
					snprintf(e->rqnt_ident, sizeof(e->rqnt_ident), "%08lx", ast_random());
					e->msgstate = -1;
					e->amaflags = amaflags;
					e->capability = capability;
					e->parent = gw;
					e->dtmfmode = dtmfmode;
					if (!ep_reload && e->sub && e->sub->rtp)
						e->dtmfmode |= MGCP_DTMF_INBAND;
					e->adsi = adsi;
					e->type = TYPE_LINE;
					e->immediate = immediate;
					e->callgroup=cur_callergroup;
					e->pickupgroup=cur_pickupgroup;
					e->callreturn = callreturn;
					e->cancallforward = cancallforward;
					e->singlepath = singlepath;
					e->directmedia = directmedia;
					e->callwaiting = callwaiting;
					e->hascallwaiting = callwaiting;
					e->slowsequence = slowsequence;
					e->transfer = transfer;
					e->threewaycalling = threewaycalling;
					e->onhooktime = time(NULL);
					/* ASSUME we're onhook */
					e->hookstate = MGCP_ONHOOK;
					if (!ep_reload) {
						/*snprintf(txident, sizeof(txident), "%08lx", ast_random());*/
						for (i = 0; i < MAX_SUBS; i++) {
							sub = ast_calloc(1, sizeof(*sub));
							if (sub) {
								ast_verb(3, "Allocating subchannel '%d' on %s@%s\n", i, e->name, gw->name);
								ast_mutex_init(&sub->lock);
								ast_mutex_init(&sub->cx_queue_lock);
								sub->parent = e;
								sub->id = i;
								snprintf(sub->txident, sizeof(sub->txident), "%08lx", ast_random());
								/*stnrcpy(sub->txident, txident, sizeof(sub->txident) - 1);*/
								sub->cxmode = MGCP_CX_INACTIVE;
								sub->nat = nat;
								sub->next = e->sub;
								e->sub = sub;
							} else {
								/* XXX Should find a way to clean up our memory */
								ast_log(LOG_WARNING, "Out of memory allocating subchannel\n");
								return NULL;
							}
	 					}
						/* Make out subs a circular linked list so we can always sping through the whole bunch */
						sub = e->sub;
						/* find the end of the list */
						while(sub->next){
							sub = sub->next;
	 					}
						/* set the last sub->next to the first sub */
						sub->next = e->sub;

						e->next = gw->endpoints;
						gw->endpoints = e;
					}
				}
			} else if (!strcasecmp(v->name, "trunk") ||
			           !strcasecmp(v->name, "line")) {

				/* locate existing endpoint */
				e = gw->endpoints;
				while (e) {
					if (!strcasecmp(v->value, e->name)) {
						/* endpoint already exists */
						e->delme = 0;
						ep_reload = 1;
						break;
					}
					e = e->next;
				}

				if (!e) {
					e = ast_calloc(1, sizeof(*e));
					ep_reload = 0;
				}

				if (e) {
					if (!ep_reload) {
						ast_mutex_init(&e->lock);
						ast_mutex_init(&e->rqnt_queue_lock);
						ast_mutex_init(&e->cmd_queue_lock);
						ast_copy_string(e->name, v->value, sizeof(e->name));
						e->needaudit = 1;
					}
					/* XXX Should we really check for uniqueness?? XXX */
					ast_copy_string(e->accountcode, accountcode, sizeof(e->accountcode));
					ast_copy_string(e->context, context, sizeof(e->context));
					ast_copy_string(e->cid_num, cid_num, sizeof(e->cid_num));
					ast_copy_string(e->cid_name, cid_name, sizeof(e->cid_name));
					ast_copy_string(e->language, language, sizeof(e->language));
					ast_copy_string(e->musicclass, musicclass, sizeof(e->musicclass));
					ast_copy_string(e->mailbox, mailbox, sizeof(e->mailbox));
					ast_copy_string(e->parkinglot, parkinglot, sizeof(e->parkinglot));
					if (!ast_strlen_zero(mailbox)) {
						ast_verb(3, "Setting mailbox '%s' on %s@%s\n", mailbox, gw->name, e->name);
					}
					if (!ep_reload) {
						/* XXX potential issue due to reload */
						e->msgstate = -1;
						e->parent = gw;
					}
					e->amaflags = amaflags;
					e->capability = capability;
					e->dtmfmode = dtmfmode;
					e->adsi = adsi;
					if (!strcasecmp(v->name, "trunk"))
						e->type = TYPE_TRUNK;
					else
						e->type = TYPE_LINE;

					e->immediate = immediate;
					e->callgroup=cur_callergroup;
					e->pickupgroup=cur_pickupgroup;
					e->callreturn = callreturn;
					e->cancallforward = cancallforward;
					e->directmedia = directmedia;
					e->singlepath = singlepath;
					e->callwaiting = callwaiting;
					e->hascallwaiting = callwaiting;
					e->slowsequence = slowsequence;
					e->transfer = transfer;
					e->threewaycalling = threewaycalling;
					if (!ep_reload) {
						e->onhooktime = time(NULL);
						/* ASSUME we're onhook */
						e->hookstate = MGCP_ONHOOK;
						snprintf(e->rqnt_ident, sizeof(e->rqnt_ident), "%08lx", ast_random());
					}

					for (i = 0, sub = NULL; i < MAX_SUBS; i++) {
						if (!ep_reload) {
							sub = ast_calloc(1, sizeof(*sub));
						} else {
							if (!sub)
								sub = e->sub;
							else
								sub = sub->next;
						}

						if (sub) {
							if (!ep_reload) {
								ast_verb(3, "Allocating subchannel '%d' on %s@%s\n", i, e->name, gw->name);
								ast_mutex_init(&sub->lock);
								ast_mutex_init(&sub->cx_queue_lock);
								ast_copy_string(sub->magic, MGCP_SUBCHANNEL_MAGIC, sizeof(sub->magic));
								sub->parent = e;
								sub->id = i;
								snprintf(sub->txident, sizeof(sub->txident), "%08lx", ast_random());
								sub->cxmode = MGCP_CX_INACTIVE;
								sub->next = e->sub;
								e->sub = sub;
							}
							sub->nat = nat;
						} else {
							/* XXX Should find a way to clean up our memory */
							ast_log(LOG_WARNING, "Out of memory allocating subchannel\n");
							return NULL;
						}
					}
					if (!ep_reload) {
						/* Make out subs a circular linked list so we can always sping through the whole bunch */
						sub = e->sub;
						/* find the end of the list */
						while (sub->next) {
							sub = sub->next;
						}
						/* set the last sub->next to the first sub */
						sub->next = e->sub;

						e->next = gw->endpoints;
						gw->endpoints = e;
					}
				}
			} else
				ast_log(LOG_WARNING, "Don't know keyword '%s' at line %d\n", v->name, v->lineno);
			v = v->next;
		}
	}
	if (!ntohl(gw->addr.sin_addr.s_addr) && !gw->dynamic) {
		ast_log(LOG_WARNING, "Gateway '%s' lacks IP address and isn't dynamic\n", gw->name);
		if (!gw_reload) {
			ast_mutex_destroy(&gw->msgs_lock);
			ast_free(gw);
		}
		return NULL;
	}
	gw->defaddr.sin_family = AF_INET;
	gw->addr.sin_family = AF_INET;
	if (gw->defaddr.sin_addr.s_addr && !ntohs(gw->defaddr.sin_port)) 
		gw->defaddr.sin_port = htons(DEFAULT_MGCP_GW_PORT);
	if (gw->addr.sin_addr.s_addr && !ntohs(gw->addr.sin_port))
		gw->addr.sin_port = htons(DEFAULT_MGCP_GW_PORT);
	if (gw->addr.sin_addr.s_addr)
		if (ast_ouraddrfor(&gw->addr.sin_addr, &gw->ourip))
			memcpy(&gw->ourip, &__ourip, sizeof(gw->ourip));

	return (gw_reload ? NULL : gw);
}

static enum ast_rtp_glue_result mgcp_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct mgcp_subchannel *sub = NULL;

	if (!(sub = chan->tech_pvt) || !(sub->rtp))
		return AST_RTP_GLUE_RESULT_FORBID;

	*instance = sub->rtp ? ao2_ref(sub->rtp, +1), sub->rtp : NULL;

	if (sub->parent->directmedia)
		return AST_RTP_GLUE_RESULT_REMOTE;
	else
		return AST_RTP_GLUE_RESULT_LOCAL;
}

static int mgcp_set_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance *rtp, struct ast_rtp_instance *vrtp, struct ast_rtp_instance *trtp, int codecs, int nat_active)
{
	/* XXX Is there such thing as video support with MGCP? XXX */
	struct mgcp_subchannel *sub;
	sub = chan->tech_pvt;
	if (sub && !sub->alreadygone) {
		transmit_modify_with_sdp(sub, rtp, codecs);
		return 0;
	}
	return -1;
}

static struct ast_rtp_glue mgcp_rtp_glue = {
	.type = "MGCP",
	.get_rtp_info = mgcp_get_rtp_peer,
	.update_peer = mgcp_set_rtp_peer,
};

static void destroy_endpoint(struct mgcp_endpoint *e)
{
	struct mgcp_subchannel *sub = e->sub->next, *s;
	int i;

	for (i = 0; i < MAX_SUBS; i++) {
		ast_mutex_lock(&sub->lock);
		if (!ast_strlen_zero(sub->cxident)) {
			transmit_connection_del(sub);
		}
		if (sub->rtp) {
			ast_rtp_instance_destroy(sub->rtp);
			sub->rtp = NULL;
		}
		memset(sub->magic, 0, sizeof(sub->magic));
		mgcp_queue_hangup(sub);
		dump_cmd_queues(NULL, sub);
		ast_mutex_unlock(&sub->lock);
		sub = sub->next;
	}

	if (e->dsp) {
		ast_dsp_free(e->dsp);
	}

	dump_queue(e->parent, e);
	dump_cmd_queues(e, NULL);

	sub = e->sub;
	for (i = 0; (i < MAX_SUBS) && sub; i++) {
		s = sub;
		sub = sub->next;
		ast_mutex_destroy(&s->lock);
		ast_mutex_destroy(&s->cx_queue_lock);
		ast_free(s);
	}

	if (e->mwi_event_sub)
		ast_event_unsubscribe(e->mwi_event_sub);

	ast_mutex_destroy(&e->lock);
	ast_mutex_destroy(&e->rqnt_queue_lock);
	ast_mutex_destroy(&e->cmd_queue_lock);
	ast_free(e);
}

static void destroy_gateway(struct mgcp_gateway *g)
{
	if (g->ha)
		ast_free_ha(g->ha);

	dump_queue(g, NULL);

	ast_free(g);
}

static void prune_gateways(void)
{
	struct mgcp_gateway *g, *z, *r;
	struct mgcp_endpoint *e, *p, *t;

	ast_mutex_lock(&gatelock);

	/* prune gateways */
	for (z = NULL, g = gateways; g;) {
		/* prune endpoints */
		for (p = NULL, e = g->endpoints; e; ) {
			if (e->delme || g->delme) {
				t = e;
				e = e->next;
				if (!p)
					g->endpoints = e;
				else
					p->next = e;
				destroy_endpoint(t);
			} else {
				p = e;
				e = e->next;
			}
		}

		if (g->delme) {
			r = g;
			g = g->next;
			if (!z)
				gateways = g;
			else
				z->next = g;

			destroy_gateway(r);
		} else {
			z = g;
			g = g->next;
		}
	}

	ast_mutex_unlock(&gatelock);
}

static int reload_config(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct mgcp_gateway *g;
	struct mgcp_endpoint *e;
	char *cat;
	struct ast_hostent ahp;
	struct hostent *hp;
	int format;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	
	if (gethostname(ourhost, sizeof(ourhost)-1)) {
		ast_log(LOG_WARNING, "Unable to get hostname, MGCP disabled\n");
		return 0;
	}
	cfg = ast_config_load(config, config_flags);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, MGCP disabled\n", config);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", config);
		return 0;
	}

	memset(&bindaddr, 0, sizeof(bindaddr));
	dtmfmode = 0;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	v = ast_variable_browse(cfg, "general");
	while (v) {
		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value)) {
			v = v->next;
			continue;
		}

		/* Create the interface list */
		if (!strcasecmp(v->name, "bindaddr")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) {
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
		} else if (!strcasecmp(v->name, "tos")) {
			if (ast_str2tos(v->value, &qos.tos))
			    ast_log(LOG_WARNING, "Invalid tos value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "tos_audio")) {
			if (ast_str2tos(v->value, &qos.tos_audio))
			    ast_log(LOG_WARNING, "Invalid tos_audio value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "cos")) {				
			if (ast_str2cos(v->value, &qos.cos))
			    ast_log(LOG_WARNING, "Invalid cos value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "cos_audio")) {				
			if (ast_str2cos(v->value, &qos.cos_audio))
			    ast_log(LOG_WARNING, "Invalid cos_audio value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "port")) {
			if (sscanf(v->value, "%d", &ourport) == 1) {
				bindaddr.sin_port = htons(ourport);
			} else {
				ast_log(LOG_WARNING, "Invalid port number '%s' at line %d of %s\n", v->value, v->lineno, config);
			}
		}
		v = v->next;
	}

	/* mark existing entries for deletion */
	ast_mutex_lock(&gatelock);
	g = gateways;
	while (g) {
		g->delme = 1;
		e = g->endpoints;
		while (e) {
			e->delme = 1;
			e = e->next;
		}
		g = g->next;
	}
	ast_mutex_unlock(&gatelock);
	
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			ast_mutex_lock(&gatelock);
			g = build_gateway(cat, ast_variable_browse(cfg, cat));
			if (g) {
				ast_verb(3, "Added gateway '%s'\n", g->name);
				g->next = gateways;
				gateways = g;
			}
			ast_mutex_unlock(&gatelock);

			/* FS: process queue and IO */
			if (monitor_thread == pthread_self()) {
				if (sched) ast_sched_runq(sched);
				if (io) ast_io_wait(io, 10);
			}
		}
		cat = ast_category_browse(cfg, cat);
	}

    	/* prune deleted entries etc. */
    	prune_gateways();

	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(&__ourip, &bindaddr.sin_addr, sizeof(__ourip));
	} else {
		hp = ast_gethostbyname(ourhost, &ahp);
		if (!hp) {
			ast_log(LOG_WARNING, "Unable to get our IP address, MGCP disabled\n");
			ast_config_destroy(cfg);
			return 0;
		}
		memcpy(&__ourip, hp->h_addr, sizeof(__ourip));
	}
	if (!ntohs(bindaddr.sin_port))
		bindaddr.sin_port = ntohs(DEFAULT_MGCP_CA_PORT);
	bindaddr.sin_family = AF_INET;
	ast_mutex_lock(&netlock);
	if (mgcpsock > -1)
		close(mgcpsock);

	if (mgcpsock_read_id != NULL)
		ast_io_remove(io, mgcpsock_read_id);
	mgcpsock_read_id = NULL;

	mgcpsock = socket(AF_INET, SOCK_DGRAM, 0);
	if (mgcpsock < 0) {
		ast_log(LOG_WARNING, "Unable to create MGCP socket: %s\n", strerror(errno));
	} else {
		if (bind(mgcpsock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
			ast_log(LOG_WARNING, "Failed to bind to %s:%d: %s\n",
				ast_inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port),
					strerror(errno));
			close(mgcpsock);
			mgcpsock = -1;
		} else {
			ast_verb(2, "MGCP Listening on %s:%d\n",
					ast_inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port));
			ast_netsock_set_qos(mgcpsock, qos.tos, qos.cos, "MGCP");
		}
	}
	ast_mutex_unlock(&netlock);
	ast_config_destroy(cfg);

	/* send audit only to the new endpoints */
	g = gateways;
	while (g) {
		e = g->endpoints;
		while (e && e->needaudit) {
			e->needaudit = 0;
			transmit_audit_endpoint(e);
			ast_verb(3, "MGCP Auditing endpoint %s@%s for hookstate\n", e->name, g->name);
			e = e->next;
		}
		g = g->next;
	}

	return 0;
}

/*! \brief  load_module: PBX load module - initialization ---*/
static int load_module(void)
{
	if (!(sched = sched_context_create())) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (!(io = io_context_create())) {
		ast_log(LOG_WARNING, "Unable to create I/O context\n");
		sched_context_destroy(sched);
		return AST_MODULE_LOAD_FAILURE;
	}

	if (reload_config(0))
		return AST_MODULE_LOAD_DECLINE;

	/* Make sure we can register our mgcp channel type */
	if (ast_channel_register(&mgcp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'MGCP'\n");
		io_context_destroy(io);
		sched_context_destroy(sched);
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_rtp_glue_register(&mgcp_rtp_glue);
	ast_cli_register_multiple(cli_mgcp, sizeof(cli_mgcp) / sizeof(struct ast_cli_entry));
	
	/* And start the monitor for the first time */
	restart_monitor();

	return AST_MODULE_LOAD_SUCCESS;
}

static char *mgcp_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	static int deprecated = 0;

	if (e) {
		switch (cmd) {
		case CLI_INIT:
			e->command = "mgcp reload";
			e->usage =
				"Usage: mgcp reload\n"
				"       'mgcp reload' is deprecated.  Please use 'reload chan_mgcp.so' instead.\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
		}
	}

	if (!deprecated && a && a->argc > 0) {
		ast_log(LOG_WARNING, "'mgcp reload' is deprecated.  Please use 'reload chan_mgcp.so' instead.\n");
		deprecated = 1;
	}

	ast_mutex_lock(&mgcp_reload_lock);
	if (mgcp_reloading) {
		ast_verbose("Previous mgcp reload not yet done\n");
	} else
		mgcp_reloading = 1;
	ast_mutex_unlock(&mgcp_reload_lock);
	restart_monitor();
	return CLI_SUCCESS;
}

static int reload(void)
{
	mgcp_reload(NULL, 0, NULL);
	return 0;
}

static int unload_module(void)
{
	struct mgcp_endpoint *e;
	struct mgcp_gateway *g;

	/* Check to see if we're reloading */
	if (ast_mutex_trylock(&mgcp_reload_lock)) {
		ast_log(LOG_WARNING, "MGCP is currently reloading.  Unable to remove module.\n");
		return -1;
	} else {
		mgcp_reloading = 1;
		ast_mutex_unlock(&mgcp_reload_lock);
	}

	/* First, take us out of the channel loop */
	ast_channel_unregister(&mgcp_tech);

	/* Shut down the monitoring thread */
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
		/* We always want to leave this in a consistent state */
		ast_channel_register(&mgcp_tech);
		mgcp_reloading = 0;
		mgcp_reload(NULL, 0, NULL);
		return -1;
	}

	if (!ast_mutex_lock(&gatelock)) {
		for (g = gateways; g; g = g->next) {
			g->delme = 1;
			for (e = g->endpoints; e; e = e->next)
				e->delme = 1;
		}

		prune_gateways();
		ast_mutex_unlock(&gatelock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the gateways list.\n");
		/* We always want to leave this in a consistent state */
		ast_channel_register(&mgcp_tech);
		/* Allow the monitor to restart */
		monitor_thread = AST_PTHREADT_NULL;
		mgcp_reloading = 0;
		mgcp_reload(NULL, 0, NULL);
		return -1;
	}

	close(mgcpsock);
	ast_rtp_glue_unregister(&mgcp_rtp_glue);
	ast_cli_unregister_multiple(cli_mgcp, sizeof(cli_mgcp) / sizeof(struct ast_cli_entry));
	sched_context_destroy(sched);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Media Gateway Control Protocol (MGCP)",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
