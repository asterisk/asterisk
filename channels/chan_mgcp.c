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
 * \ingroup channel_drivers
 */

/*! \li \ref chan_mgcp.c uses the configuration file \ref mgcp.conf
 * \addtogroup configuration_file
 */

/*! \page mgcp.conf mgcp.conf
 * \verbinclude mgcp.conf.sample
 */

/*** MODULEINFO
        <use type="module">res_pktccops</use>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <netdb.h>
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
#include "asterisk/pickup.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/say.h"
#include "asterisk/astdb.h"
#include "asterisk/features.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/utils.h"
#include "asterisk/netsock2.h"
#include "asterisk/causes.h"
#include "asterisk/dsp.h"
#include "asterisk/devicestate.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/chanvars.h"
#include "asterisk/pktccops.h"
#include "asterisk/stasis.h"
#include "asterisk/bridge.h"
#include "asterisk/features_config.h"
#include "asterisk/parking.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/format_cache.h"

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

/*! Global jitterbuffer configuration - by default, jb is disabled
 *  \note Values shown here match the defaults shown in mgcp.conf.sample */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
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
static int ncs = 0;
static int pktcgatealloc = 0;
static int hangupongateremove = 0;

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

static char mailbox[AST_MAX_MAILBOX_UNIQUEID];

static int amaflags = 0;

static int adsi = 0;

static unsigned int oseq_global = 0;
AST_MUTEX_DEFINE_STATIC(oseq_lock);

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
 *  which are not currently in use.
 */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

static struct ast_format_cap *global_capability;
static int nonCodecCapability = AST_RTP_DTMF;

static char ourhost[MAXHOSTNAMELEN];
static struct in_addr __ourip;
static int ourport;

static int mgcpdebug = 0;

static struct ast_sched_context *sched;
static struct io_context *io;
/*! The private structures of the mgcp channels are linked for
 * selecting outgoing channels
 */

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
	int sdpsent;
	struct cops_gate *gate;
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
	struct stasis_subscription *mwi_event_sub;
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
	int ncs;
	int pktcgatealloc;
	int hangupongateremove;
	int type;
	int slowsequence;			/*!< MS: Sequence the endpoint as a whole */
	int group;
	int iseq; /*!< Not used? */
	int lastout; /*!< tracking this on the subchannels.  Is it needed here? */
	int needdestroy; /*!< Not used? */
	struct ast_format_cap *cap;
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
	/* message go the endpoint and not the channel so they stay here */
	struct ast_variable *chanvars;		/*!< Variables to set for channel created by user */
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
	int realtime;
	struct mgcp_response *responses;
	struct mgcp_gateway *next;
} *gateways = NULL;

AST_MUTEX_DEFINE_STATIC(mgcp_reload_lock);
static int mgcp_reloading = 0;

/*! \brief gatelock: mutex for gateway/endpoint lists */
AST_MUTEX_DEFINE_STATIC(gatelock);

static int mgcpsock  = -1;

static struct sockaddr_in bindaddr;

static void mgcp_set_owner(struct mgcp_subchannel *sub, struct ast_channel *chan);
static struct ast_frame  *mgcp_read(struct ast_channel *ast);
static int transmit_response(struct mgcp_subchannel *sub, char *msg, struct mgcp_request *req, char *msgrest);
static int transmit_notify_request(struct mgcp_subchannel *sub, char *tone);
static int transmit_modify_request(struct mgcp_subchannel *sub);
static int transmit_connect(struct mgcp_subchannel *sub);
static int transmit_notify_request_with_callerid(struct mgcp_subchannel *sub, char *tone, char *callernum, char *callername);
static int transmit_modify_with_sdp(struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp, const struct ast_format_cap *codecs);
static int transmit_connection_del(struct mgcp_subchannel *sub);
static int transmit_audit_endpoint(struct mgcp_endpoint *p);
static void start_rtp(struct mgcp_subchannel *sub);
static void handle_response(struct mgcp_endpoint *p, struct mgcp_subchannel *sub,
                            int result, unsigned int ident, struct mgcp_request *resp);
static void dump_cmd_queues(struct mgcp_endpoint *p, struct mgcp_subchannel *sub);
static char *mgcp_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static int reload_config(int reload);

static struct ast_channel *mgcp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *dest, int *cause);
static int mgcp_call(struct ast_channel *ast, const char *dest, int timeout);
static int mgcp_hangup(struct ast_channel *ast);
static int mgcp_answer(struct ast_channel *ast);
static struct ast_frame *mgcp_read(struct ast_channel *ast);
static int mgcp_write(struct ast_channel *ast, struct ast_frame *frame);
static int mgcp_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen);
static int mgcp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int mgcp_senddigit_begin(struct ast_channel *ast, char digit);
static int mgcp_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int mgcp_devicestate(const char *data);
static void add_header_offhook(struct mgcp_subchannel *sub, struct mgcp_request *resp, char *tone);
static int transmit_connect_with_sdp(struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp);
static struct mgcp_gateway *build_gateway(char *cat, struct ast_variable *v);
static int mgcp_alloc_pktcgate(struct mgcp_subchannel *sub);
static int acf_channel_read(struct ast_channel *chan, const char *funcname, char *preparse, char *buf, size_t buflen);
static struct ast_variable *add_var(const char *buf, struct ast_variable *list);
static struct ast_variable *copy_vars(struct ast_variable *src);

static struct ast_channel_tech mgcp_tech = {
	.type = "MGCP",
	.description = tdesc,
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
	.func_channel_read = acf_channel_read,
};

static int has_voicemail(struct mgcp_endpoint *p)
{
	int new_msgs;
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	msg = stasis_cache_get(ast_mwi_state_cache(), ast_mwi_state_type(), p->mailbox);
	if (msg) {
		struct ast_mwi_state *mwi_state = stasis_message_data(msg);
		new_msgs = mwi_state->new_msgs;
	} else {
		new_msgs = ast_app_has_voicemail(p->mailbox, NULL);
	}

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

	mgcp_set_owner(sub, NULL);
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
	dump_cmd_queues(NULL, sub);
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
	ast_debug(1, "Retransmitting:\n%s\n to %s:%d\n", resp->buf, ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
	res = __mgcp_xmit(p->parent, resp->buf, resp->len);
	if (res > 0)
		res = 0;
	return res;
}

static int send_response(struct mgcp_subchannel *sub, struct mgcp_request *req)
{
	struct mgcp_endpoint *p = sub->parent;
	int res;
	ast_debug(1, "Transmitting:\n%s\n to %s:%d\n", req->data, ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
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
	for (prev = NULL, cur = gw->msgs; cur; prev = cur, cur = cur->next) {
		if (!p || cur->owner_ep == p) {
			if (prev) {
				prev->next = cur->next;
			} else {
				gw->msgs = cur->next;
			}

			ast_log(LOG_NOTICE, "Removing message from %s transaction %u\n",
				gw->name, cur->seqno);

			w = cur;
			if (q) {
				w->next = q;
			} else {
				w->next = NULL;
			}
			q = w;
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
	for (;;) {
		if (sub->owner) {
			if (!ast_channel_trylock(sub->owner)) {
				ast_queue_frame(sub->owner, f);
				ast_channel_unlock(sub->owner);
				break;
			} else {
				DEADLOCK_AVOIDANCE(&sub->lock);
			}
		} else {
			break;
		}
	}
}

static void mgcp_queue_hangup(struct mgcp_subchannel *sub)
{
	for (;;) {
		if (sub->owner) {
			if (!ast_channel_trylock(sub->owner)) {
				ast_queue_hangup(sub->owner);
				ast_channel_unlock(sub->owner);
				break;
			} else {
				DEADLOCK_AVOIDANCE(&sub->lock);
			}
		} else {
			break;
		}
	}
}

static void mgcp_queue_control(struct mgcp_subchannel *sub, int control)
{
	struct ast_frame f = { AST_FRAME_CONTROL, { control } };
	return mgcp_queue_frame(sub, &f);
}

static int retrans_pkt(const void *data)
{
	struct mgcp_gateway *gw = (struct mgcp_gateway *)data;
	struct mgcp_message *cur, *exq = NULL, *w, *prev;
	int res = 0;

	/* find out expired msgs */
	ast_mutex_lock(&gw->msgs_lock);

	for (prev = NULL, cur = gw->msgs; cur; prev = cur, cur = cur->next) {
		if (cur->retrans < MAX_RETRANS) {
			cur->retrans++;
			ast_debug(1, "Retransmitting #%d transaction %u on [%s]\n",
				cur->retrans, cur->seqno, gw->name);
			__mgcp_xmit(gw, cur->buf, cur->len);
		} else {
			if (prev)
				prev->next = cur->next;
			else
				gw->msgs = cur->next;

			ast_log(LOG_WARNING, "Maximum retries exceeded for transaction %u on [%s]\n",
				cur->seqno, gw->name);

			w = cur;

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

	if (!(msg = ast_malloc(sizeof(*msg) + len))) {
		return -1;
	}
	if (!(gw = ((p && p->parent) ? p->parent : NULL))) {
		ast_free(msg);
		return -1;
	}

	msg->owner_sub = sub;
	msg->owner_ep = p;
	msg->seqno = seqno;
	msg->next = NULL;
	msg->len = len;
	msg->retrans = 0;
	memcpy(msg->buf, data, msg->len);

	ast_mutex_lock(&gw->msgs_lock);
	for (cur = gw->msgs; cur && cur->next; cur = cur->next);
	if (cur) {
		cur->next = msg;
	} else {
		gw->msgs = msg;
	}

	now = ast_tvnow();
	msg->expire = now.tv_sec * 1000 + now.tv_usec / 1000 + DEFAULT_RETRANS;

	if (gw->retransid == -1)
		gw->retransid = ast_sched_add(sched, DEFAULT_RETRANS, retrans_pkt, (void *)gw);
	ast_mutex_unlock(&gw->msgs_lock);
	__mgcp_xmit(gw, msg->buf, msg->len);
	/* XXX Should schedule retransmission XXX */
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
			/* buggy sb5120 */
			if (!sub->parent->ncs) {
				while (q) {
					r = q->next;
					ast_free(q);
					q = r;
				}
				*queue = NULL;
			}
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

	if (!(r = ast_malloc(sizeof(*r)))) {
		ast_log(LOG_WARNING, "Cannot post MGCP request: insufficient memory\n");
		ast_mutex_unlock(l);
		return -1;
	}
	memcpy(r, req, sizeof(*r));

	if (!(*queue)) {
		ast_debug(1, "Posting Request:\n%s to %s:%d\n", req->data,
			ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));

		res = mgcp_postrequest(p, sub, req->data, req->len, seqno);
	} else {
		ast_debug(1, "Queueing Request:\n%s to %s:%d\n", req->data,
			ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));
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

static int mgcp_call(struct ast_channel *ast, const char *dest, int timeout)
{
	int res;
	struct mgcp_endpoint *p;
	struct mgcp_subchannel *sub;
	char tone[50] = "";
	const char *distinctive_ring = pbx_builtin_getvar_helper(ast, "ALERT_INFO");

	ast_debug(3, "MGCP mgcp_call(%s)\n", ast_channel_name(ast));
	sub = ast_channel_tech_pvt(ast);
	p = sub->parent;

	ast_mutex_lock(&sub->lock);
	switch (p->hookstate) {
	case MGCP_OFFHOOK:
		if (!ast_strlen_zero(distinctive_ring)) {
			snprintf(tone, sizeof(tone), "L/wt%s", distinctive_ring);
			ast_debug(3, "MGCP distinctive callwait %s\n", tone);
		} else {
			ast_copy_string(tone, (p->ncs ? "L/wt1" : "L/wt"), sizeof(tone));
			ast_debug(3, "MGCP normal callwait %s\n", tone);
		}
		break;
	case MGCP_ONHOOK:
	default:
		if (!ast_strlen_zero(distinctive_ring)) {
			snprintf(tone, sizeof(tone), "L/r%s", distinctive_ring);
			ast_debug(3, "MGCP distinctive ring %s\n", tone);
		} else {
			ast_copy_string(tone, "L/rg", sizeof(tone));
			ast_debug(3, "MGCP default ring\n");
		}
		break;
	}

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "mgcp_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		ast_mutex_unlock(&sub->lock);
		return -1;
	}

	res = 0;
	sub->outgoing = 1;
	sub->cxmode = MGCP_CX_RECVONLY;
	ast_setstate(ast, AST_STATE_RINGING);
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

		transmit_notify_request_with_callerid(sub, tone,
			S_COR(ast_channel_connected(ast)->id.number.valid, ast_channel_connected(ast)->id.number.str, ""),
			S_COR(ast_channel_connected(ast)->id.name.valid, ast_channel_connected(ast)->id.name.str, ""));
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
	return res;
}

static int mgcp_hangup(struct ast_channel *ast)
{
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(ast);
	struct mgcp_endpoint *p = sub->parent;

	ast_debug(1, "mgcp_hangup(%s)\n", ast_channel_name(ast));
	if (!ast_channel_tech_pvt(ast)) {
		ast_debug(1, "Asked to hangup channel not connected\n");
		return 0;
	}
	if (strcmp(sub->magic, MGCP_SUBCHANNEL_MAGIC)) {
		ast_debug(1, "Invalid magic. MGCP subchannel freed up already.\n");
		return 0;
	}
	ast_mutex_lock(&sub->lock);
	ast_debug(3, "MGCP mgcp_hangup(%s) on %s@%s\n", ast_channel_name(ast), p->name, p->parent->name);

	if ((p->dtmfmode & MGCP_DTMF_INBAND) && p->dsp) {
		/* check whether other channel is active. */
		if (!sub->next->owner) {
			if (p->dtmfmode & MGCP_DTMF_HYBRID) {
				p->dtmfmode &= ~MGCP_DTMF_INBAND;
			}
			ast_debug(2, "MGCP free dsp on %s@%s\n", p->name, p->parent->name);
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}
	}

	mgcp_set_owner(sub, NULL);

	/* for deleting gate */
	if (p->pktcgatealloc && sub->gate) {
		sub->gate->gate_open = NULL;
		sub->gate->gate_remove = NULL;
		sub->gate->got_dq_gi = NULL;
		sub->gate->tech_pvt = NULL;
		if (sub->gate->state == GATE_ALLOC_PROGRESS || sub->gate->state == GATE_ALLOCATED) {
			ast_pktccops_gate_alloc(GATE_DEL, sub->gate, 0, 0, 0, 0, 0, 0, NULL, NULL);
		} else {
			sub->gate->deltimer = time(NULL) + 5;
		}
		sub->gate = NULL;
	}

	if (!ast_strlen_zero(sub->cxident)) {
		transmit_connection_del(sub);
	}
	sub->cxident[0] = '\0';
	if ((sub == p->sub) && sub->next->owner) {
		RAII_VAR(struct ast_channel *, bridged, ast_channel_bridge_peer(sub->next->owner), ast_channel_cleanup);

		if (p->hookstate == MGCP_OFFHOOK) {
			if (sub->next->owner && bridged) {
				/* ncs fix! */
				transmit_notify_request_with_callerid(p->sub, (p->ncs ? "L/wt1" : "L/wt"),
					S_COR(ast_channel_caller(bridged)->id.number.valid, ast_channel_caller(bridged)->id.number.str, ""),
					S_COR(ast_channel_caller(bridged)->id.name.valid, ast_channel_caller(bridged)->id.name.str, ""));
			}
		} else {
			/* set our other connection as the primary and swith over to it */
			p->sub = sub->next;
			p->sub->cxmode = MGCP_CX_RECVONLY;
			transmit_modify_request(p->sub);
			if (sub->next->owner && bridged) {
				transmit_notify_request_with_callerid(p->sub, "L/rg",
					S_COR(ast_channel_caller(bridged)->id.number.valid, ast_channel_caller(bridged)->id.number.str, ""),
					S_COR(ast_channel_caller(bridged)->id.name.valid, ast_channel_caller(bridged)->id.name.str, ""));
			}
		}

	} else if ((sub == p->sub->next) && p->hookstate == MGCP_OFFHOOK) {
		transmit_notify_request(sub, p->ncs ? "" : "L/v");
	} else if (p->hookstate == MGCP_OFFHOOK) {
		transmit_notify_request(sub, "L/ro");
	} else {
		transmit_notify_request(sub, "");
	}

	ast_channel_tech_pvt_set(ast, NULL);
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
			ast_verb(3, "Enabling call waiting on %s\n", ast_channel_name(ast));
			p->callwaiting = -1;
		}
		if (has_voicemail(p)) {
			ast_debug(3, "MGCP mgcp_hangup(%s) on %s@%s set vmwi(+)\n",
				ast_channel_name(ast), p->name, p->parent->name);
			transmit_notify_request(sub, "L/vmwi(+)");
		} else {
			ast_debug(3, "MGCP mgcp_hangup(%s) on %s@%s set vmwi(-)\n",
				ast_channel_name(ast), p->name, p->parent->name);
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
	struct ast_variable * v = NULL;

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

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}
	ast_mutex_lock(&gatelock);
	for (mg = gateways; mg; mg = mg->next) {
		ast_cli(a->fd, "Gateway '%s' at %s (%s%s)\n", mg->name, mg->addr.sin_addr.s_addr ? ast_inet_ntoa(mg->addr.sin_addr) : ast_inet_ntoa(mg->defaddr.sin_addr), mg->realtime ? "Realtime, " : "", mg->dynamic ? "Dynamic" : "Static");
		for (me = mg->endpoints; me; me = me->next) {
			ast_cli(a->fd, "   -- '%s@%s in '%s' is %s\n", me->name, mg->name, me->context, me->sub->owner ? "active" : "idle");
			if (me->chanvars) {
				ast_cli(a->fd, "  Variables:\n");
				for (v = me->chanvars ; v ; v = v->next) {
					ast_cli(a->fd, "    %s = '%s'\n", v->name, v->value);
				}
			}
			hasendpoints = 1;
		}
		if (!hasendpoints) {
			ast_cli(a->fd, "   << No Endpoints Defined >>     ");
		}
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
	for (gname = ename; *gname; gname++) {
		if (*gname == '@') {
			*gname = 0;
			gname++;
			break;
		}
	}
	if (gname[0] == '[') {
		gname++;
	}
	if ((c = strrchr(gname, ']'))) {
		*c = '\0';
	}
	ast_mutex_lock(&gatelock);
	for (mg = gateways; mg; mg = mg->next) {
		if (!strcasecmp(mg->name, gname)) {
			for (me = mg->endpoints; me; me = me->next) {
				if (!strcasecmp(me->name, ename)) {
					found = 1;
					transmit_audit_endpoint(me);
					break;
				}
			}
			if (found) {
				break;
			}
		}
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
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(ast);
	struct mgcp_endpoint *p = sub->parent;

	ast_mutex_lock(&sub->lock);
	sub->cxmode = MGCP_CX_SENDRECV;
	if (!sub->rtp) {
		start_rtp(sub);
	} else {
		transmit_modify_request(sub);
	}
	ast_verb(3, "MGCP mgcp_answer(%s) on %s@%s-%d\n",
			ast_channel_name(ast), p->name, p->parent->name, sub->id);
	if (ast_channel_state(ast) != AST_STATE_UP) {
		ast_setstate(ast, AST_STATE_UP);
		ast_debug(1, "mgcp_answer(%s)\n", ast_channel_name(ast));
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
			if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(sub->owner), f->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
				struct ast_format_cap *caps;

				ast_debug(1, "Oooh, format changed to %s\n", ast_format_get_name(f->subclass.format));

				caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
				if (caps) {
					ast_format_cap_append(caps, f->subclass.format, 0);
					ast_channel_nativeformats_set(sub->owner, caps);
					ao2_ref(caps, -1);
				} else {
					return &ast_null_frame;
				}

				ast_set_read_format(sub->owner, ast_channel_readformat(sub->owner));
				ast_set_write_format(sub->owner, ast_channel_writeformat(sub->owner));
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

static void mgcp_set_owner(struct mgcp_subchannel *sub, struct ast_channel *chan)
{
	sub->owner = chan;
	if (sub->rtp) {
		ast_rtp_instance_set_channel_id(sub->rtp, sub->owner ? ast_channel_uniqueid(chan) : "");
	}
}

static struct ast_frame *mgcp_read(struct ast_channel *ast)
{
	struct ast_frame *f;
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(ast);
	ast_mutex_lock(&sub->lock);
	f = mgcp_rtp_read(sub);
	ast_mutex_unlock(&sub->lock);
	return f;
}

static int mgcp_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(ast);
	int res = 0;

	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE)
			return 0;
		else {
			ast_log(LOG_WARNING, "Can't send %u type frames with MGCP write\n", frame->frametype);
			return 0;
		}
	} else {
		if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), frame->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
			struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

			ast_log(LOG_WARNING, "Asked to transmit frame type %s, while native formats is %s (read/write = %s/%s)\n",
				ast_format_get_name(frame->subclass.format),
				ast_format_cap_get_names(ast_channel_nativeformats(ast), &cap_buf),
				ast_format_get_name(ast_channel_readformat(ast)),
				ast_format_get_name(ast_channel_writeformat(ast)));
			/* return -1; */
		}
	}
	if (sub) {
		ast_mutex_lock(&sub->lock);
		if (!sub->sdpsent && sub->gate) {
			if (sub->gate->state == GATE_ALLOCATED) {
				ast_debug(1, "GATE ALLOCATED, sending sdp\n");
				transmit_modify_with_sdp(sub, NULL, 0);
			}
		}
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
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(newchan);

	ast_mutex_lock(&sub->lock);
	ast_log(LOG_NOTICE, "mgcp_fixup(%s, %s)\n", ast_channel_name(oldchan), ast_channel_name(newchan));
	if (sub->owner != oldchan) {
		ast_mutex_unlock(&sub->lock);
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, sub->owner);
		return -1;
	}
	mgcp_set_owner(sub, newchan);
	ast_mutex_unlock(&sub->lock);
	return 0;
}

static int mgcp_senddigit_begin(struct ast_channel *ast, char digit)
{
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(ast);
	struct mgcp_endpoint *p = sub->parent;
	int res = 0;

	ast_mutex_lock(&sub->lock);
	if (p->dtmfmode & MGCP_DTMF_INBAND || p->dtmfmode & MGCP_DTMF_HYBRID) {
		ast_debug(1, "Sending DTMF using inband/hybrid\n");
		res = -1; /* Let asterisk play inband indications */
	} else if (p->dtmfmode & MGCP_DTMF_RFC2833) {
		ast_debug(1, "Sending DTMF using RFC2833\n");
		ast_rtp_instance_dtmf_begin(sub->rtp, digit);
	} else {
		ast_log(LOG_ERROR, "Don't know about DTMF_MODE %d\n", p->dtmfmode);
	}
	ast_mutex_unlock(&sub->lock);

	return res;
}

static int mgcp_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(ast);
	struct mgcp_endpoint *p = sub->parent;
	int res = 0;
	char tmp[4];

	ast_mutex_lock(&sub->lock);
	if (p->dtmfmode & MGCP_DTMF_INBAND || p->dtmfmode & MGCP_DTMF_HYBRID) {
		ast_debug(1, "Stopping DTMF using inband/hybrid\n");
		res = -1; /* Tell Asterisk to stop inband indications */
	} else if (p->dtmfmode & MGCP_DTMF_RFC2833) {
		ast_debug(1, "Stopping DTMF using RFC2833\n");
		if (sub->parent->ncs) {
			tmp[0] = digit;
			tmp[1] = '\0';
		} else {
			tmp[0] = 'D';
			tmp[1] = '/';
			tmp[2] = digit;
			tmp[3] = '\0';
		}
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
static int mgcp_devicestate(const char *data)
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
	for (g = gateways; g; g = g->next) {
		if (strcasecmp(g->name, gw) == 0) {
			e = g->endpoints;
			break;
		}
	}

	if (!e)
		goto error;

	for (; e; e = e->next) {
		if (strcasecmp(e->name, endpt) == 0) {
			break;
		}
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
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(ast);
	int res = 0;

	ast_debug(3, "MGCP asked to indicate %d '%s' condition on channel %s\n",
		ind, control2str(ind), ast_channel_name(ast));
	ast_mutex_lock(&sub->lock);
	switch(ind) {
	case AST_CONTROL_RINGING:
#ifdef DLINK_BUGGY_FIRMWARE
		transmit_notify_request(sub, "rt");
#else
		if (!sub->sdpsent) { /* will hide the inband progress!!! */
			transmit_notify_request(sub, sub->parent->ncs ? "L/rt" : "G/rt");
		}
#endif
		break;
	case AST_CONTROL_BUSY:
		transmit_notify_request(sub, "L/bz");
		break;
	case AST_CONTROL_INCOMPLETE:
		/* We do not currently support resetting of the Interdigit Timer, so treat
		 * Incomplete control frames as a congestion response
		 */
	case AST_CONTROL_CONGESTION:
		transmit_notify_request(sub, sub->parent->ncs ? "L/cg" : "G/cg");
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_SRCUPDATE:
		ast_rtp_instance_update_source(sub->rtp);
		break;
	case AST_CONTROL_SRCCHANGE:
		ast_rtp_instance_change_source(sub->rtp);
		break;
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
		transmit_modify_request(sub);
	case -1:
		transmit_notify_request(sub, "");
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", ind);
		/* fallthrough */
	case AST_CONTROL_PVT_CAUSE_CODE:
		res = -1;
	}
	ast_mutex_unlock(&sub->lock);
	return res;
}

static struct ast_channel *mgcp_new(struct mgcp_subchannel *sub, int state, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_format_cap *caps = NULL;
	struct ast_channel *tmp;
	struct ast_variable *v = NULL;
	struct mgcp_endpoint *i = sub->parent;
	struct ast_format *tmpfmt;

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_log(LOG_ERROR, "Format capabilities could not be created\n");
		return NULL;
	}
	tmp = ast_channel_alloc(1, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, assignedids, requestor, i->amaflags, "MGCP/%s@%s-%d", i->name, i->parent->name, sub->id);
	if (!tmp) {
		ast_log(LOG_WARNING, "Channel could not be created\n");
		ao2_ref(caps, -1);
		return NULL;
	}

	ast_channel_stage_snapshot(tmp);
	ast_channel_tech_set(tmp, &mgcp_tech);
	if (ast_format_cap_count(i->cap)) {
		ast_format_cap_append_from_cap(caps, i->cap, AST_MEDIA_TYPE_UNKNOWN);
	} else {
		ast_format_cap_append_from_cap(caps, global_capability, AST_MEDIA_TYPE_UNKNOWN);
	}
	ast_channel_nativeformats_set(tmp, caps);
	ao2_ref(caps, -1);
	if (sub->rtp) {
		ast_channel_set_fd(tmp, 0, ast_rtp_instance_fd(sub->rtp, 0));
	}
	if (i->dtmfmode & (MGCP_DTMF_INBAND | MGCP_DTMF_HYBRID)) {
		i->dsp = ast_dsp_new();
		ast_dsp_set_features(i->dsp, DSP_FEATURE_DIGIT_DETECT);
		/* this is to prevent clipping of dtmf tones during dsp processing */
		ast_dsp_set_digitmode(i->dsp, DSP_DIGITMODE_NOQUELCH);
	} else {
		i->dsp = NULL;
	}
	if (state == AST_STATE_RING) {
		ast_channel_rings_set(tmp, 1);
	}

	tmpfmt = ast_format_cap_get_format(ast_channel_nativeformats(tmp), 0);
	ast_channel_set_writeformat(tmp, tmpfmt);
	ast_channel_set_rawwriteformat(tmp, tmpfmt);
	ast_channel_set_readformat(tmp, tmpfmt);
	ast_channel_set_rawreadformat(tmp, tmpfmt);
	ao2_ref(tmpfmt, -1);
	ast_channel_tech_pvt_set(tmp, sub);
	if (!ast_strlen_zero(i->language))
		ast_channel_language_set(tmp, i->language);
	if (!ast_strlen_zero(i->accountcode))
		ast_channel_accountcode_set(tmp, i->accountcode);
	if (i->amaflags)
		ast_channel_amaflags_set(tmp, i->amaflags);
	mgcp_set_owner(sub, tmp);
	ast_module_ref(ast_module_info->self);
	ast_channel_callgroup_set(tmp, i->callgroup);
	ast_channel_pickupgroup_set(tmp, i->pickupgroup);
	ast_channel_call_forward_set(tmp, i->call_forward);
	ast_channel_context_set(tmp, i->context);
	ast_channel_exten_set(tmp, i->exten);
	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
	if (!ast_strlen_zero(i->cid_num)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
	}

	if (!i->adsi) {
		ast_channel_adsicpe_set(tmp, AST_ADSI_UNAVAILABLE);
	}
	ast_channel_priority_set(tmp, 1);

	/* Set channel variables for this call from configuration */
	for (v = i->chanvars ; v ; v = v->next) {
		char valuebuf[1024];
		pbx_builtin_setvar_helper(tmp, v->name, ast_get_encoded_str(v->value, valuebuf, sizeof(valuebuf)));
	}

	if (sub->rtp) {
		ast_jb_configure(tmp, &global_jbconf);
	}

	ast_channel_stage_snapshot_done(tmp);
	ast_channel_unlock(tmp);

	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
			ast_hangup(tmp);
			tmp = NULL;
		}
	}
	ast_verb(3, "MGCP mgcp_new(%s) created in state: %s\n",
			ast_channel_name(tmp), ast_state2str(state));

	return tmp;
}

static char *get_sdp_by_line(char* line, char *name, int nameLen)
{
	if (strncasecmp(line, name, nameLen) == 0 && line[nameLen] == '=') {
		char *r = line + nameLen + 1;
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

	for (x = 0; x < req->lines; x++) {
		r = get_sdp_by_line(req->line[x], name, len);
		if (r[0] != '\0') return r;
	}
	return "";
}

static void sdpLineNum_iterator_init(int *iterator)
{
	*iterator = 0;
}

static char *get_sdp_iterate(int* iterator, struct mgcp_request *req, char *name)
{
	int len = strlen(name);
	char *r;
	while (*iterator < req->lines) {
		r = get_sdp_by_line(req->line[(*iterator)++], name, len);
		if (r[0] != '\0') return r;
	}
	return "";
}

static char *__get_header(struct mgcp_request *req, char *name, int *start, char *def)
{
	int x;
	int len = strlen(name);
	char *r;
	for (x = *start; x < req->headers; x++) {
		if (!strncasecmp(req->header[x], name, len) &&
		    (req->header[x][len] == ':')) {
			r = req->header[x] + len + 1;
			while (*r && (*r < 33)) {
				r++;
			}
			*start = x + 1;
			return r;
		}
	}
	/* Don't return NULL, so get_header is always a valid pointer */
	return def;
}

static char *get_header(struct mgcp_request *req, char *name)
{
	int start = 0;
	return __get_header(req, name, &start, "");
}

/*! \brief get_csv: (SC:) get comma separated value */
static char *get_csv(char *c, int *len, char **next)
{
	char *s;

	*next = NULL, *len = 0;
	if (!c) return NULL;

	while (*c && (*c < 33 || *c == ',')) {
		c++;
	}

	s = c;
	while (*c && (*c >= 33 && *c != ',')) {
		c++, (*len)++;
	}
	*next = c;

	if (*len == 0) {
		s = NULL, *next = NULL;
	}

	return s;
}

static struct mgcp_gateway *find_realtime_gw(char *name, char *at, struct sockaddr_in *sin)
{
	struct mgcp_gateway *g = NULL;
	struct ast_variable *mgcpgwconfig = NULL;
	struct ast_variable *gwv, *epname = NULL;
	struct mgcp_endpoint *e;
	char lines[256];
	int i, j;

	ast_debug(1, "*** find Realtime MGCPGW\n");

	if (!(i = ast_check_realtime("mgcpgw")) || !(j = ast_check_realtime("mgcpep"))) {
		return NULL;
	}

	if (ast_strlen_zero(at)) {
		ast_debug(1, "null gw name\n");
		return NULL;
	}

	if (!(mgcpgwconfig = ast_load_realtime("mgcpgw", "name", at, NULL))) {
		return NULL;
	}

	/*!
	 * \note This is a fairly odd way of instantiating lines.  Instead of each
	 * line created by virtue of being in the database (and loaded via
	 * ast_load_realtime_multientry), this code forces a specific order with a
	 * "lines" entry in the "mgcpgw" record.  This has benefits, because as with
	 * chan_dahdi, values are inherited across definitions.  The downside is
	 * that it's not as clear what the values will be simply by looking at a
	 * single row in the database, and it's probable that the sanest configuration
	 * should have the first column in the "mgcpep" table be "clearvars", with a
	 * static value of "all", if any variables are set at all.  It may be worth
	 * making this assumption explicit in the code in the future, and then just
	 * using ast_load_realtime_multientry for the "mgcpep" records.
	 */
	lines[0] = '\0';
	for (gwv = mgcpgwconfig; gwv; gwv = gwv->next) {
		if (!strcasecmp(gwv->name, "lines")) {
			ast_copy_string(lines, gwv->value, sizeof(lines));
			break;
		}
	}
	/* Position gwv at the end of the list */
	for (gwv = gwv && gwv->next ? gwv : mgcpgwconfig; gwv->next; gwv = gwv->next);

	if (!ast_strlen_zero(lines)) {
		AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(line)[100];
		);
		AST_STANDARD_APP_ARGS(args, lines);
		for (i = 0; i < args.argc; i++) {
			gwv->next = ast_load_realtime("mgcpep", "name", at, "line", args.line[i], NULL);

			/* Remove "line" AND position gwv at the end of the list. */
			for (epname = NULL; gwv->next; gwv = gwv->next) {
				if (!strcasecmp(gwv->next->name, "line")) {
					/* Remove it from the list */
					epname = gwv->next;
					gwv->next = gwv->next->next;
				}
			}
			/* Since "line" instantiates the configuration, we have to move it to the end. */
			if (epname) {
				gwv->next = epname;
				epname->next = NULL;
				gwv = gwv->next;
			}
		}
	}
	for (gwv = mgcpgwconfig; gwv; gwv = gwv->next) {
		ast_debug(1, "MGCP Realtime var: %s => %s\n", gwv->name, gwv->value);
	}

	if (mgcpgwconfig) {
		g = build_gateway(at, mgcpgwconfig);
		ast_variables_destroy(mgcpgwconfig);
	}
	if (g) {
		g->next = gateways;
		g->realtime = 1;
		gateways = g;
		for (e = g->endpoints; e; e = e->next) {
			transmit_audit_endpoint(e);
			e->needaudit = 0;
		}
	}
	return g;
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
		if (c) {
			*c = '\0';
		}
	}
	for (g = gateways ? gateways : find_realtime_gw(name, at, sin); g; g = g->next ? g->next : find_realtime_gw(name, at, sin)) {
		if ((!name || !strcasecmp(g->name, at)) &&
		    (sin || g->addr.sin_addr.s_addr || g->defaddr.sin_addr.s_addr)) {
			/* Found the gateway.  If it's dynamic, save it's address -- now for the endpoint */
			if (sin && g->dynamic && name) {
				if ((g->addr.sin_addr.s_addr != sin->sin_addr.s_addr) ||
					(g->addr.sin_port != sin->sin_port)) {
					memcpy(&g->addr, sin, sizeof(g->addr));
					{
						struct ast_sockaddr tmp1, tmp2;
						struct sockaddr_in tmp3 = {0,};

						tmp3.sin_addr = g->ourip;
						ast_sockaddr_from_sin(&tmp1, &g->addr);
						ast_sockaddr_from_sin(&tmp2, &tmp3);
						if (ast_ouraddrfor(&tmp1, &tmp2)) {
							memcpy(&g->ourip, &__ourip, sizeof(g->ourip));
						}
						ast_sockaddr_to_sin(&tmp2, &tmp3);
						g->ourip = tmp3.sin_addr;
					}
					ast_verb(3, "Registered MGCP gateway '%s' at %s port %d\n", g->name, ast_inet_ntoa(g->addr.sin_addr), ntohs(g->addr.sin_port));
				}
			/* not dynamic, check if the name matches */
			} else if (name) {
				if (strcasecmp(g->name, at)) {
					continue;
				}
			/* not dynamic, no name, check if the addr matches */
			} else if (!name && sin) {
				if ((g->addr.sin_addr.s_addr != sin->sin_addr.s_addr) ||
				    (g->addr.sin_port != sin->sin_port)) {
					continue;
				}
			} else {
				continue;
			}
			for (p = g->endpoints; p; p = p->next) {
				ast_debug(1, "Searching on %s@%s for subchannel\n", p->name, g->name);
				if (msgid) {
					sub = p->sub;
					found = 1;
					break;
				} else if (name && !strcasecmp(p->name, tmp)) {
					ast_debug(1, "Coundn't determine subchannel, assuming current master %s@%s-%d\n",
						p->name, g->name, p->sub->id);
					sub = p->sub;
					found = 1;
					break;
				}
			}
			if (sub && found) {
				ast_mutex_lock(&sub->lock);
				break;
			}
		}
	}
	ast_mutex_unlock(&gatelock);
	if (!sub) {
		if (name) {
			if (g) {
				ast_log(LOG_NOTICE, "Endpoint '%s' not found on gateway '%s'\n", tmp, at);
			} else {
				ast_log(LOG_NOTICE, "Gateway '%s' (and thus its endpoint '%s') does not exist\n", at, tmp);
			}
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
	for (; *c; c++) {
		if (*c == '\n') {
			/* We've got a new header */
			*c = 0;
			ast_debug(3, "Header: %s (%d)\n", req->header[f], (int) strlen(req->header[f]));
			if (ast_strlen_zero(req->header[f])) {
				/* Line by itself means we're now in content */
				c++;
				break;
			}
			if (f >= MGCP_MAX_HEADERS - 1) {
				ast_log(LOG_WARNING, "Too many MGCP headers...\n");
			} else {
				f++;
			}
			req->header[f] = c + 1;
		} else if (*c == '\r') {
			/* Ignore but eliminate \r's */
			*c = 0;
		}
	}
	/* Check for last header */
	if (!ast_strlen_zero(req->header[f])) {
		f++;
	}
	req->headers = f;
	/* Now we process any mime content */
	f = 0;
	req->line[f] = c;
	for (; *c; c++) {
		if (*c == '\n') {
			/* We've got a new line */
			*c = 0;
			ast_debug(3, "Line: %s (%d)\n", req->line[f], (int) strlen(req->line[f]));
			if (f >= MGCP_MAX_LINES - 1) {
				ast_log(LOG_WARNING, "Too many SDP lines...\n");
			} else {
				f++;
			}
			req->line[f] = c + 1;
		} else if (*c == '\r') {
			/* Ignore and eliminate \r's */
			*c = 0;
		}
	}
	/* Check for last line */
	if (!ast_strlen_zero(req->line[f])) {
		f++;
	}
	req->lines = f;
	/* Parse up the initial header */
	c = req->header[0];
	while (*c && *c < 33) c++;
	/* First the verb */
	req->verb = c;
	while (*c && (*c > 32)) c++;
	if (*c) {
		*c = '\0';
		c++;
		while (*c && (*c < 33)) c++;
		req->identifier = c;
		while (*c && (*c > 32)) c++;
		if (*c) {
			*c = '\0';
			c++;
			while (*c && (*c < 33)) c++;
			req->endpoint = c;
			while (*c && (*c > 32)) c++;
			if (*c) {
				*c = '\0';
				c++;
				while (*c && (*c < 33)) c++;
				req->version = c;
				while (*c && (*c > 32)) c++;
				while (*c && (*c < 33)) c++;
				while (*c && (*c > 32)) c++;
				*c = '\0';
			}
		}
	}

	ast_debug(1, "Verb: '%s', Identifier: '%s', Endpoint: '%s', Version: '%s'\n",
			req->verb, req->identifier, req->endpoint, req->version);
	ast_debug(1, "%d headers, %d lines\n", req->headers, req->lines);
	if (*c) {
		ast_log(LOG_WARNING, "Odd content, extra stuff left over ('%s')\n", c);
	}
}

static int process_sdp(struct mgcp_subchannel *sub, struct mgcp_request *req)
{
	char *m;
	char *c;
	char *a;
	char host[258];
	int len = 0;
	int portno;
	struct ast_format_cap *peercap;
	int peerNonCodecCapability;
	struct sockaddr_in sin;
	struct ast_sockaddr sin_tmp;
	char *codecs;
	struct ast_hostent ahp; struct hostent *hp;
	int codec, codec_count=0;
	int iterator;
	struct mgcp_endpoint *p = sub->parent;
	struct ast_str *global_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
	struct ast_str *peer_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
	struct ast_str *pvt_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

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
	if (sscanf(m, "audio %30d RTP/AVP %n", &portno, &len) != 1 || !len) {
		ast_log(LOG_WARNING, "Malformed media stream descriptor: %s\n", m);
		return -1;
	}
	sin.sin_family = AF_INET;
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	sin.sin_port = htons(portno);
	ast_sockaddr_from_sin(&sin_tmp, &sin);
	ast_rtp_instance_set_remote_address(sub->rtp, &sin_tmp);
	ast_debug(3, "Peer RTP is at port %s:%d\n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	/* Scan through the RTP payload types specified in a "m=" line: */
	codecs = ast_strdupa(m + len);
	while (!ast_strlen_zero(codecs)) {
		if (sscanf(codecs, "%30d%n", &codec, &len) != 1) {
			if (codec_count) {
				break;
			}
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
		if (sscanf(a, "rtpmap: %30d %127[^/]/", &codec, mimeSubtype) != 2)
			continue;
		/* Note: should really look at the 'freq' and '#chans' params too */
		ast_rtp_codecs_payloads_set_rtpmap_type(ast_rtp_instance_get_codecs(sub->rtp), sub->rtp, codec, "audio", mimeSubtype, 0);
	}

	/* Now gather all of the codecs that were asked for: */
	if (!(peercap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return -1;
	}
	ast_rtp_codecs_payload_formats(ast_rtp_instance_get_codecs(sub->rtp), peercap, &peerNonCodecCapability);
	ast_format_cap_get_compatible(global_capability, peercap, p->cap);
	ast_debug(1, "Capabilities: us - %s, them - %s, combined - %s\n",
		ast_format_cap_get_names(global_capability, &global_buf),
		ast_format_cap_get_names(peercap, &peer_buf),
		ast_format_cap_get_names(p->cap, &pvt_buf));
	ao2_ref(peercap, -1);

	ast_debug(1, "Non-codec capabilities: us - %d, them - %d, combined - %d\n",
		nonCodecCapability, peerNonCodecCapability, p->nonCodecCapability);
	if (!ast_format_cap_count(p->cap)) {
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
	if (req->headers < MGCP_MAX_HEADERS) {
		req->headers++;
	} else {
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
	if (req->lines < MGCP_MAX_LINES) {
		req->lines++;
	} else {
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
	if (req->headers < MGCP_MAX_HEADERS) {
		req->headers++;
	} else {
		ast_log(LOG_WARNING, "Out of header space\n");
	}
	return 0;
}

static int init_req(struct mgcp_endpoint *p, struct mgcp_request *req, char *verb, unsigned int oseq)
{
	/* Initialize a response */
	if (req->headers || req->len) {
		ast_log(LOG_WARNING, "Request already initialized?!?\n");
		return -1;
	}
	req->header[req->headers] = req->data + req->len;
	/* check if we need brackets around the gw name */
	if (p->parent->isnamedottedip) {
		snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s %u %s@[%s] MGCP 1.0%s\r\n", verb, oseq, p->name, p->parent->name, p->ncs ? " NCS 1.0" : "");
	} else {
+		snprintf(req->header[req->headers], sizeof(req->data) - req->len, "%s %u %s@%s MGCP 1.0%s\r\n", verb, oseq, p->name, p->parent->name, p->ncs ? " NCS 1.0" : "");
	}
	req->len += strlen(req->header[req->headers]);
	if (req->headers < MGCP_MAX_HEADERS) {
		req->headers++;
	} else {
		ast_log(LOG_WARNING, "Out of header space\n");
	}
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
	unsigned int oseq;
	memset(req, 0, sizeof(struct mgcp_request));
	ast_mutex_lock(&oseq_lock);
	oseq_global++;
	if (oseq_global > 999999999) {
		oseq_global = 1;
	}
	oseq = oseq_global;
	ast_mutex_unlock(&oseq_lock);
	init_req(p, req, verb, oseq);
	return oseq;
}

static int transmit_response(struct mgcp_subchannel *sub, char *msg, struct mgcp_request *req, char *msgrest)
{
	struct mgcp_request resp;
	struct mgcp_endpoint *p = sub->parent;
	struct mgcp_response *mgr;

	if (!sub) {
		return -1;
	}

	respprep(&resp, p, msg, req, msgrest);
	if (!(mgr = ast_calloc(1, sizeof(*mgr) + resp.len + 1))) {
		return send_response(sub, &resp);
	}
	/* Store MGCP response in case we have to retransmit */
	sscanf(req->identifier, "%30d", &mgr->seqno);
	time(&mgr->whensent);
	mgr->len = resp.len;
	memcpy(mgr->buf, resp.data, resp.len);
	mgr->buf[resp.len] = '\0';
	mgr->next = p->parent->responses;
	p->parent->responses = mgr;

	return send_response(sub, &resp);
}


static int add_sdp(struct mgcp_request *resp, struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp)
{
	int len;
	int codec;
	char costr[80];
	struct sockaddr_in sin;
	struct ast_sockaddr sin_tmp;
	char v[256];
	char s[256];
	char o[256];
	char c[256];
	char t[256];
	char m[256] = "";
	char a[1024] = "";
	int x;
	struct sockaddr_in dest = { 0, };
	struct ast_sockaddr dest_tmp;
	struct mgcp_endpoint *p = sub->parent;
	/* XXX We break with the "recommendation" and send our IP, in order that our
	       peer doesn't have to ast_gethostbyname() us XXX */
	len = 0;
	if (!sub->rtp) {
		ast_log(LOG_WARNING, "No way to add SDP without an RTP structure\n");
		return -1;
	}
	ast_rtp_instance_get_local_address(sub->rtp, &sin_tmp);
	ast_sockaddr_to_sin(&sin_tmp, &sin);
	if (rtp) {
		ast_rtp_instance_get_remote_address(sub->rtp, &dest_tmp);
		ast_sockaddr_to_sin(&dest_tmp, &dest);
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
	ast_debug(1, "We're at %s port %d\n", ast_inet_ntoa(p->parent->ourip), ntohs(sin.sin_port));
	ast_copy_string(v, "v=0\r\n", sizeof(v));
	snprintf(o, sizeof(o), "o=root %d %d IN IP4 %s\r\n", (int)getpid(), (int)getpid(), ast_inet_ntoa(dest.sin_addr));
	ast_copy_string(s, "s=session\r\n", sizeof(s));
	snprintf(c, sizeof(c), "c=IN IP4 %s\r\n", ast_inet_ntoa(dest.sin_addr));
	ast_copy_string(t, "t=0 0\r\n", sizeof(t));
	snprintf(m, sizeof(m), "m=audio %d RTP/AVP", ntohs(dest.sin_port));

	for (x = 0; x < ast_format_cap_count(p->cap); x++) {
		struct ast_format *format = ast_format_cap_get_format(p->cap, x);

		if (ast_format_get_type(format) != AST_MEDIA_TYPE_AUDIO) {
			ao2_ref(format, -1);
			continue;
		}

		ast_debug(1, "Answering with capability %s\n", ast_format_get_name(format));
		codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(sub->rtp), 1, format, 0);
		if (codec > -1) {
			snprintf(costr, sizeof(costr), " %d", codec);
			strncat(m, costr, sizeof(m) - strlen(m) - 1);
			snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype2(1, format, 0, 0));
			strncat(a, costr, sizeof(a) - strlen(a) - 1);
		}

		ao2_ref(format, -1);
	}

	for (x = 1LL; x <= AST_RTP_MAX; x <<= 1) {
		if (p->nonCodecCapability & x) {
			ast_debug(1, "Answering with non-codec capability %d\n", (int) x);
			codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(sub->rtp), 0, NULL, x);
			if (codec > -1) {
				snprintf(costr, sizeof(costr), " %d", codec);
				strncat(m, costr, sizeof(m) - strlen(m) - 1);
				snprintf(costr, sizeof(costr), "a=rtpmap:%d %s/8000\r\n", codec, ast_rtp_lookup_mime_subtype2(0, NULL, x, 0));
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

static int transmit_modify_with_sdp(struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp, const struct ast_format_cap *codecs)
{
	struct mgcp_request resp;
	char local[256];
	char tmp[80];
	struct mgcp_endpoint *p = sub->parent;
	int i;
	struct ast_sockaddr sub_tmpdest_tmp;
	unsigned int oseq;

	if (ast_strlen_zero(sub->cxident) && rtp) {
		/* We don't have a CXident yet, store the destination and
		   wait a bit */
		ast_rtp_instance_get_remote_address(rtp, &sub_tmpdest_tmp);
		ast_sockaddr_to_sin(&sub_tmpdest_tmp, &sub->tmpdest);
		return 0;
	}
	ast_copy_string(local, "e:on, s:off, p:20", sizeof(local));

	for (i = 0; i < ast_format_cap_count(p->cap); i++) {
		struct ast_format *format = ast_format_cap_get_format(p->cap, i);

		if (ast_format_get_type(format) != AST_MEDIA_TYPE_AUDIO) {
			ao2_ref(format, -1);
			continue;
		}

		snprintf(tmp, sizeof(tmp), ", a:%s", ast_rtp_lookup_mime_subtype2(1, format, 0, 0));
		strncat(local, tmp, sizeof(local) - strlen(local) - 1);

		ao2_ref(format, -1);
	}

	if (sub->gate) {
		if (sub->gate->state == GATE_ALLOCATED || sub->gate->state == GATE_OPEN) {
			snprintf(tmp, sizeof(tmp), ", dq-gi:%x", sub->gate->gateid);
			strncat(local, tmp, sizeof(local) - strlen(local) - 1);
			sub->sdpsent = 1;
		} else {
			/* oops wait */
			ast_debug(1, "Waiting for opened gate...\n");
			sub->sdpsent = 0;
			return 0;
		}
	}


	oseq = reqprep(&resp, p, "MDCX");
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
	return send_request(p, sub, &resp, oseq);
}

static int transmit_connect_with_sdp(struct mgcp_subchannel *sub, struct ast_rtp_instance *rtp)
{
	struct mgcp_request resp;
	char local[256];
	char tmp[80];
	int i;
	struct mgcp_endpoint *p = sub->parent;
	unsigned int oseq;

	ast_debug(3, "Creating connection for %s@%s-%d in cxmode: %s callid: %s\n",
		 p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode], sub->callid);

	ast_copy_string(local, "e:on, s:off, p:20", sizeof(local));

	for (i = 0; i < ast_format_cap_count(p->cap); i++) {
		struct ast_format *format = ast_format_cap_get_format(p->cap, i);

		if (ast_format_get_type(format) != AST_MEDIA_TYPE_AUDIO) {
			ao2_ref(format, -1);
			continue;
		}

		snprintf(tmp, sizeof(tmp), ", a:%s", ast_rtp_lookup_mime_subtype2(1, format, 0, 0));
		strncat(local, tmp, sizeof(local) - strlen(local) - 1);

		ao2_ref(format, -1);
	}

	if (sub->gate) {
		if(sub->gate->state == GATE_ALLOCATED) {
			snprintf(tmp, sizeof(tmp), ", dq-gi:%x", sub->gate->gateid);
			strncat(local, tmp, sizeof(local) - strlen(local) - 1);
		}
	}
	sub->sdpsent = 1;
	oseq = reqprep(&resp, p, "CRCX");
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
	return send_request(p, sub, &resp, oseq);
}

static int mgcp_pktcgate_remove(struct cops_gate *gate)
{
	struct mgcp_subchannel *sub = gate->tech_pvt;

	if (!sub) {
		return 1;
	}

	ast_mutex_lock(&sub->lock);
	ast_debug(1, "Pktc: gate 0x%x deleted\n", gate->gateid);
	if (sub->gate->state != GATE_CLOSED && sub->parent->hangupongateremove) {
		sub->gate = NULL;
		if (sub->owner) {
			ast_softhangup(sub->owner, AST_CAUSE_REQUESTED_CHAN_UNAVAIL);
			ast_channel_unlock(sub->owner);
		}
	} else {
		sub->gate = NULL;
	}
	ast_mutex_unlock(&sub->lock);
	return 1;
}

static int mgcp_pktcgate_open(struct cops_gate *gate)
{
	struct mgcp_subchannel *sub = gate->tech_pvt;
	if (!sub) {
		return 1;
	}
	ast_mutex_lock(&sub->lock);
	ast_debug(1, "Pktc: gate 0x%x open\n", gate->gateid);
	if (!sub->sdpsent) transmit_modify_with_sdp(sub, NULL, 0);
	ast_mutex_unlock(&sub->lock);
	return 1;
}

static int mgcp_alloc_pktcgate(struct mgcp_subchannel *sub)
{
	struct mgcp_endpoint *p = sub->parent;
	sub->gate = ast_pktccops_gate_alloc(GATE_SET, NULL, ntohl(p->parent->addr.sin_addr.s_addr),
					8, 128000, 232, 0, 0, NULL, &mgcp_pktcgate_remove);

	if (!sub->gate) {
		return 0;
	}
	sub->gate->tech_pvt = sub;
	sub->gate->gate_open = &mgcp_pktcgate_open;
	return 1;
}

static int transmit_connect(struct mgcp_subchannel *sub)
{
	struct mgcp_request resp;
	int x;
	char local[256];
	char tmp[80];
	struct ast_format *tmpfmt;
	struct mgcp_endpoint *p = sub->parent;
	unsigned int oseq;

	ast_copy_string(local, "p:20, s:off, e:on", sizeof(local));

	for (x = 0; x < ast_format_cap_count(p->cap); x++) {
		tmpfmt = ast_format_cap_get_format(p->cap, x);

		snprintf(tmp, sizeof(tmp), ", a:%s", ast_rtp_lookup_mime_subtype2(1, tmpfmt, 0, 0));
		strncat(local, tmp, sizeof(local) - strlen(local) - 1);

		ao2_ref(tmpfmt, -1);
	}

	ast_debug(3, "Creating connection for %s@%s-%d in cxmode: %s callid: %s\n",
		    p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode], sub->callid);
	sub->sdpsent = 0;
	oseq = reqprep(&resp, p, "CRCX");
	add_header(&resp, "C", sub->callid);
	add_header(&resp, "L", local);
	add_header(&resp, "M", "inactive");
	/* X header should not be sent. kept for compatibility */
	add_header(&resp, "X", sub->txident);
	/*add_header(&resp, "S", "");*/
	/* fill in new fields */
	resp.cmd = MGCP_CMD_CRCX;
	resp.trid = oseq;
	return send_request(p, sub, &resp, oseq);
}

static int transmit_notify_request(struct mgcp_subchannel *sub, char *tone)
{
	struct mgcp_request resp;
	struct mgcp_endpoint *p = sub->parent;
	unsigned int oseq;

	ast_debug(3, "MGCP Asked to indicate tone: %s on  %s@%s-%d in cxmode: %s\n",
		tone, p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode]);
	ast_copy_string(p->curtone, tone, sizeof(p->curtone));
	oseq = reqprep(&resp, p, "RQNT");
	add_header(&resp, "X", p->rqnt_ident);
	switch (p->hookstate) {
	case MGCP_ONHOOK:
		add_header(&resp, "R", "L/hd(N)");
		break;
	case MGCP_OFFHOOK:
		add_header_offhook(sub, &resp, tone);
		break;
	}
	if (!ast_strlen_zero(tone)) {
		add_header(&resp, "S", tone);
	}
	/* fill in new fields */
	resp.cmd = MGCP_CMD_RQNT;
	resp.trid = oseq;
	return send_request(p, NULL, &resp, oseq);
}

static int transmit_notify_request_with_callerid(struct mgcp_subchannel *sub, char *tone, char *callernum, char *callername)
{
	struct mgcp_request resp;
	char tone2[256];
	char *l, *n;
	struct timeval t = ast_tvnow();
	struct ast_tm tm;
	struct mgcp_endpoint *p = sub->parent;
	unsigned int oseq;

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
	oseq = reqprep(&resp, p, "RQNT");
	add_header(&resp, "X", p->rqnt_ident);
	switch (p->hookstate) {
	case MGCP_ONHOOK:
		add_header(&resp, "R", "L/hd(N)");
		break;
	case MGCP_OFFHOOK:
		add_header_offhook(sub, &resp, tone);
		break;
	}
	if (!ast_strlen_zero(tone2)) {
		add_header(&resp, "S", tone2);
	}
	ast_debug(3, "MGCP Asked to indicate tone: %s on  %s@%s-%d in cxmode: %s\n",
		tone2, p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode]);
	/* fill in new fields */
	resp.cmd = MGCP_CMD_RQNT;
	resp.trid = oseq;
	return send_request(p, NULL, &resp, oseq);
}

static int transmit_modify_request(struct mgcp_subchannel *sub)
{
	struct mgcp_request resp;
	struct mgcp_endpoint *p = sub->parent;
	int i;
	int fc = 1;
	char local[256];
	char tmp[80];
	unsigned int oseq;

	if (ast_strlen_zero(sub->cxident)) {
		/* We don't have a CXident yet, store the destination and
		   wait a bit */
		return 0;
	}
	ast_debug(3, "Modified %s@%s-%d with new mode: %s on callid: %s\n",
		p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode], sub->callid);

	ast_copy_string(local, "", sizeof(local));
	for (i = 0; i < ast_format_cap_count(p->cap); i++) {
		struct ast_format *format = ast_format_cap_get_format(p->cap, i);

		if (p->ncs && !fc) {
			ast_format_cap_remove_by_type(p->cap, AST_MEDIA_TYPE_UNKNOWN);
			ast_format_cap_append(p->cap, format, 0); /* sb5120e bug */
			ao2_ref(format, -1);
			break;
		} else {
			fc = 0;
			snprintf(tmp, sizeof(tmp), ", a:%s", ast_rtp_lookup_mime_subtype2(1, format, 0, 0));
		}
		strncat(local, tmp, sizeof(local) - strlen(local) - 1);

		ao2_ref(format, -1);
	}

	if (!sub->sdpsent) {
		if (sub->gate) {
			if (sub->gate->state == GATE_ALLOCATED || sub->gate->state == GATE_OPEN) {
				snprintf(tmp, sizeof(tmp), ", dq-gi:%x", sub->gate->gateid);
				strncat(local, tmp, sizeof(local) - strlen(local) - 1);
			} else {
					/* we still don't have gateid wait */
				return 0;
			}
		}
	}

	oseq = reqprep(&resp, p, "MDCX");
	add_header(&resp, "C", sub->callid);
	if (!sub->sdpsent) {
		add_header(&resp, "L", local);
	}
	add_header(&resp, "M", mgcp_cxmodes[sub->cxmode]);
	/* X header should not be sent. kept for compatibility */
	add_header(&resp, "X", sub->txident);
	add_header(&resp, "I", sub->cxident);
	switch (sub->parent->hookstate) {
	case MGCP_ONHOOK:
		add_header(&resp, "R", "L/hd(N)");
		break;
	case MGCP_OFFHOOK:
		add_header_offhook(sub, &resp, "");
		break;
	}
	if (!sub->sdpsent) {
		add_sdp(&resp, sub, NULL);
		sub->sdpsent = 1;
	}
	/* fill in new fields */
	resp.cmd = MGCP_CMD_MDCX;
	resp.trid = oseq;
	return send_request(p, sub, &resp, oseq);
}


static void add_header_offhook(struct mgcp_subchannel *sub, struct mgcp_request *resp, char *tone)
{
	struct mgcp_endpoint *p = sub->parent;
	char tone_indicate_end = 0;

	/* We also should check the tone to indicate, because it have no sense
	   to request notify D/[0-9#*] (dtmf keys) if we are sending congestion
	   tone for example G/cg */
	if (p && (!strcasecmp(tone, (p->ncs ? "L/ro" : "G/cg")))) {
		tone_indicate_end = 1;
	}

	if (p && p->sub && p->sub->owner &&
			ast_channel_state(p->sub->owner) >= AST_STATE_RINGING &&
			(p->dtmfmode & (MGCP_DTMF_INBAND | MGCP_DTMF_HYBRID))) {
	    add_header(resp, "R", "L/hu(N),L/hf(N)");

	} else if (!tone_indicate_end){
	    add_header(resp, "R", (p->ncs ? "L/hu(N),L/hf(N),L/[0-9#*](N)" : "L/hu(N),L/hf(N),D/[0-9#*](N)"));
	} else {
		ast_debug(1, "We don't want more digits if we will end the call\n");
		add_header(resp, "R", "L/hu(N),L/hf(N)");
	}
}




static int transmit_audit_endpoint(struct mgcp_endpoint *p)
{
	struct mgcp_request resp;
	unsigned int oseq;
	oseq = reqprep(&resp, p, "AUEP");
	/* removed unknown param VS */
	/*add_header(&resp, "F", "A,R,D,S,X,N,I,T,O,ES,E,MD,M");*/
	add_header(&resp, "F", "A");
	/* fill in new fields */
	resp.cmd = MGCP_CMD_AUEP;
	resp.trid = oseq;
	return send_request(p, NULL, &resp, oseq);
}

static int transmit_connection_del(struct mgcp_subchannel *sub)
{
	struct mgcp_endpoint *p = sub->parent;
	struct mgcp_request resp;
	unsigned int oseq;

	ast_debug(3, "Delete connection %s %s@%s-%d with new mode: %s on callid: %s\n",
		sub->cxident, p->name, p->parent->name, sub->id, mgcp_cxmodes[sub->cxmode], sub->callid);
	oseq = reqprep(&resp, p, "DLCX");
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
	return send_request(p, sub, &resp, oseq);
}

static int transmit_connection_del_w_params(struct mgcp_endpoint *p, char *callid, char *cxident)
{
	struct mgcp_request resp;
	unsigned int oseq;

	ast_debug(3, "Delete connection %s %s@%s on callid: %s\n",
		cxident ? cxident : "", p->name, p->parent->name, callid ? callid : "");
	oseq = reqprep(&resp, p, "DLCX");
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
				ast_debug(1, "Posting Queued Request:\n%s to %s:%d\n", (*queue)->data,
					ast_inet_ntoa(p->parent->addr.sin_addr), ntohs(p->parent->addr.sin_port));

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
		ast_verb(3, "No command found on [%s] for transaction %u. Ignoring...\n",
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
			ast_log(LOG_NOTICE, "Transaction %u timed out\n", ident);
			break;
		case 407:
			ast_log(LOG_NOTICE, "Transaction %u aborted\n", ident);
			break;
		}
		if (sub) {
			if (!sub->cxident[0] && (req->cmd == MGCP_CMD_CRCX)) {
			    ast_log(LOG_NOTICE, "DLCX for all connections on %s due to error %d\n", gw->name, result);
			    transmit_connection_del(sub);
			}
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
		/* responseAck: */
		if (result == 200 && (req->cmd == MGCP_CMD_CRCX || req->cmd == MGCP_CMD_MDCX)) {
				if (sub) {
					transmit_response(sub, "000", resp, "OK");
					if (sub->owner && ast_channel_state(sub->owner) == AST_STATE_RINGING) {
						ast_queue_control(sub->owner, AST_CONTROL_RINGING);
					}
				}
		}
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
	struct ast_sockaddr bindaddr_tmp;

	ast_mutex_lock(&sub->lock);
	/* check again to be on the safe side */
	if (sub->rtp) {
		ast_rtp_instance_destroy(sub->rtp);
		sub->rtp = NULL;
	}
	/* Allocate the RTP now */
	ast_sockaddr_from_sin(&bindaddr_tmp, &bindaddr);
	sub->rtp = ast_rtp_instance_new("asterisk", sched, &bindaddr_tmp, NULL);
	if (sub->rtp && sub->owner)
		ast_channel_set_fd(sub->owner, 0, ast_rtp_instance_fd(sub->rtp, 0));
	if (sub->rtp) {
		ast_rtp_instance_set_qos(sub->rtp, qos.tos_audio, qos.cos_audio, "MGCP RTP");
		ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_NAT, sub->nat);
	}
	/* Make a call*ID */
	snprintf(sub->callid, sizeof(sub->callid), "%08lx%s", (unsigned long)ast_random(), sub->txident);
	/* Transmit the connection create */
	if(!sub->parent->pktcgatealloc) {
		transmit_connect_with_sdp(sub, NULL);
	} else {
		transmit_connect(sub);
		sub->gate = NULL;
		if(!mgcp_alloc_pktcgate(sub))
			mgcp_queue_hangup(sub);
	}
	ast_mutex_unlock(&sub->lock);
}

static void *mgcp_ss(void *data)
{
	struct ast_channel *chan = data;
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(chan);
	struct mgcp_endpoint *p = sub->parent;
	/* char exten[AST_MAX_EXTENSION] = ""; */
	int len = 0;
	int timeout = firstdigittimeout;
	int res= 0;
	int getforward = 0;
	int loop_pause = 100;
	RAII_VAR(struct ast_features_pickup_config *, pickup_cfg, NULL, ao2_cleanup);
	const char *pickupexten;

	len = strlen(p->dtmf_buf);

	ast_channel_lock(chan);
	pickup_cfg = ast_get_chan_features_pickup_config(chan);
	if (!pickup_cfg) {
		ast_log(LOG_ERROR, "Unable to retrieve pickup configuration options. Unable to detect call pickup extension\n");
		pickupexten = "";
	} else {
		pickupexten = ast_strdupa(pickup_cfg->pickupexten);
	}
	ast_channel_unlock(chan);

	while (len < AST_MAX_EXTENSION - 1) {
		ast_debug(1, "Dtmf buffer '%s' for '%s@%s'\n", p->dtmf_buf, p->name, p->parent->name);
		res = 1;  /* Assume that we will get a digit */
		while (strlen(p->dtmf_buf) == len) {
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

		if (!ast_ignore_pattern(ast_channel_context(chan), p->dtmf_buf)) {
			/*res = tone_zone_play_tone(p->subs[index].zfd, -1);*/
			ast_indicate(chan, -1);
		} else {
			/* XXX Redundant?  We should already be playing dialtone */
			/*tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALTONE);*/
			transmit_notify_request(sub, "L/dl");
		}
		if (ast_exists_extension(chan, ast_channel_context(chan), p->dtmf_buf, 1, p->cid_num)) {
			if (!res || !ast_matchmore_extension(chan, ast_channel_context(chan), p->dtmf_buf, 1, p->cid_num)) {
				if (getforward) {
					/* Record this as the forwarding extension */
					ast_copy_string(p->call_forward, p->dtmf_buf, sizeof(p->call_forward));
					ast_verb(3, "Setting call forward to '%s' on channel %s\n",
							p->call_forward, ast_channel_name(chan));
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
					ast_channel_lock(chan);
					ast_channel_exten_set(chan, p->dtmf_buf);
					ast_channel_dialed(chan)->number.str = ast_strdup(p->dtmf_buf);
					memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
					ast_set_callerid(chan,
						p->hidecallerid ? "" : p->cid_num,
						p->hidecallerid ? "" : p->cid_name,
						ast_channel_caller(chan)->ani.number.valid ? NULL : p->cid_num);
					ast_setstate(chan, AST_STATE_RING);
					ast_channel_unlock(chan);
					if (p->dtmfmode & MGCP_DTMF_HYBRID) {
						p->dtmfmode |= MGCP_DTMF_INBAND;
						ast_indicate(chan, -1);
					}
					res = ast_pbx_run(chan);
					if (res) {
						ast_log(LOG_WARNING, "PBX exited non-zero\n");
						/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_CONGESTION);*/
						/*transmit_notify_request(p, "nbz", 1);*/
						transmit_notify_request(sub, p->ncs ? "L/cg" : "G/cg");
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
			transmit_notify_request(sub, p->ncs ? "L/cg" : "G/cg");
			/*dahdi_wait_event(p->subs[index].zfd);*/
			ast_hangup(chan);
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			return NULL;
		} else if (p->hascallwaiting && p->callwaiting && !strcmp(p->dtmf_buf, "*70")) {
			ast_verb(3, "Disabling call waiting on %s\n", ast_channel_name(chan));
			/* Disable call waiting if enabled */
			p->callwaiting = 0;
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			len = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			timeout = firstdigittimeout;
		} else if (!strcmp(p->dtmf_buf, pickupexten)) {
			/* Scan all channels and see if any there
			 * ringing channqels with that have call groups
			 * that equal this channels pickup group
			 */
			if (ast_pickup_call(chan)) {
				ast_log(LOG_WARNING, "No call pickup possible...\n");
				/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_CONGESTION);*/
				transmit_notify_request(sub, p->ncs ? "L/cg" : "G/cg");
			}
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			ast_hangup(chan);
			return NULL;
		} else if (!p->hidecallerid && !strcmp(p->dtmf_buf, "*67")) {
			ast_verb(3, "Disabling Caller*ID on %s\n", ast_channel_name(chan));
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
				res = ast_say_digit_str(chan, p->lastcallerid, "", ast_channel_language(chan));
			}
			if (!res)
				/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
				transmit_notify_request(sub, "L/sl");
			break;
		} else if (!strcmp(p->dtmf_buf, "*78")) {
			/* Do not disturb */
			ast_verb(3, "Enabled DND on channel %s\n", ast_channel_name(chan));
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			p->dnd = 1;
			getforward = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			len = 0;
		} else if (!strcmp(p->dtmf_buf, "*79")) {
			/* Do not disturb */
			ast_verb(3, "Disabled DND on channel %s\n", ast_channel_name(chan));
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
			ast_verb(3, "Cancelling call forwarding on channel %s\n", ast_channel_name(chan));
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			memset(p->call_forward, 0, sizeof(p->call_forward));
			getforward = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			len = 0;
		} else if (ast_parking_provider_registered() && ast_parking_is_exten_park(ast_channel_context(chan), p->dtmf_buf) &&
			sub->next->owner) {
			RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);
			/* This is a three way call, the main call being a real channel,
				and we're parking the first call. */
			ast_channel_lock(chan);
			bridge_channel = ast_channel_get_bridge_channel(chan);
			ast_channel_unlock(chan);
			if (bridge_channel && !ast_parking_blind_transfer_park(bridge_channel, ast_channel_context(chan), p->dtmf_buf, NULL, NULL)) {
				ast_verb(3, "Parking call to '%s'\n", ast_channel_name(chan));
			}
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
			ast_verb(3, "Enabling Caller*ID on %s\n", ast_channel_name(chan));
			/* Enable Caller*ID if enabled */
			p->hidecallerid = 0;
			ast_set_callerid(chan, p->cid_num, p->cid_name, NULL);
			/*res = tone_zone_play_tone(p->subs[index].zfd, DAHDI_TONE_DIALRECALL);*/
			transmit_notify_request(sub, "L/sl");
			len = 0;
			memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			timeout = firstdigittimeout;
		} else if (!ast_canmatch_extension(chan, ast_channel_context(chan), p->dtmf_buf, 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))
			&& ((p->dtmf_buf[0] != '*') || (strlen(p->dtmf_buf) > 2))) {
			ast_debug(1, "Can't match %s from '%s' in context %s\n", p->dtmf_buf,
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "<Unknown Caller>"),
				ast_channel_context(chan));
			break;
		}
		if (!timeout)
			timeout = gendigittimeout;
		if (len && !ast_ignore_pattern(ast_channel_context(chan), p->dtmf_buf))
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

/*! \brief Complete an attended transfer
 *
 * \param p The endpoint performing the attended transfer
 * \param sub The sub-channel completing the attended transfer
 *
 * \note p->sub is the currently active sub-channel (the channel the phone is using)
 * \note p->sub->next is the sub-channel not in use, potentially on hold
 *
 * \retval 0 when channel should be hung up
 * \retval 1 when channel should not be hung up
 */
static int attempt_transfer(struct mgcp_endpoint *p, struct mgcp_subchannel *sub)
{
	enum ast_transfer_result res;

	/* Ensure that the other channel goes off hold and that it is indicating properly */
	ast_queue_unhold(sub->next->owner);
	if (ast_channel_state(sub->owner) == AST_STATE_RINGING) {
		ast_queue_control(sub->next->owner, AST_CONTROL_RINGING);
	}

	ast_mutex_unlock(&p->sub->next->lock);
	ast_mutex_unlock(&p->sub->lock);
	res = ast_bridge_transfer_attended(sub->owner, sub->next->owner);

	/* Subs are only freed when the endpoint itself is destroyed, so they will continue to exist
	 * after ast_bridge_transfer_attended returns making this safe without reference counting
	 */
	ast_mutex_lock(&p->sub->lock);
	ast_mutex_lock(&p->sub->next->lock);

	if (res != AST_BRIDGE_TRANSFER_SUCCESS) {
		/* If transferring fails hang up the other channel if present and us */
		if (sub->next->owner) {
			ast_channel_softhangup_internal_flag_add(sub->next->owner, AST_SOFTHANGUP_DEV);
			mgcp_queue_hangup(sub->next);
		}
		sub->next->alreadygone = 1;
		return 0;
	}

	unalloc_sub(sub->next);

	/* If the active sub is NOT the one completing the transfer change it to be, and hang up the other sub */
	if (p->sub != sub) {
		p->sub = sub;
		return 1;
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
			ast_queue_unhold(sub->owner);
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
				transmit_notify_request(sub, p->ncs ? "L/rt" : "G/rt");
#endif
				c = mgcp_new(sub, AST_STATE_RING, NULL, NULL);
				if (!c) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %s@%s\n", p->name, p->parent->name);
					transmit_notify_request(sub, p->ncs ? "L/cg" : "G/cg");
					ast_hangup(c);
				}
			} else {
				if (has_voicemail(p)) {
					transmit_notify_request(sub, "L/sl");
				} else {
					transmit_notify_request(sub, "L/dl");
				}
				c = mgcp_new(sub, AST_STATE_DOWN, NULL, NULL);
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
			ast_queue_unhold(sub->owner);
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

	ast_debug(1, "Handling request '%s' on %s@%s\n", req->verb, p->name, p->parent->name);
	/* Clear out potential response */
	if (!strcasecmp(req->verb, "RSIP")) {
		/* Test if this RSIP request is just a keepalive */
		if (!strcasecmp( get_header(req, "RM"), "X-keepalive")) {
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
				for (tmp_ep = g->endpoints; tmp_ep; tmp_ep = tmp_ep->next) {
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
				}
			} else if (sub->owner) {
				mgcp_queue_hangup(sub);
			}
			transmit_response(sub, "200", req, "OK");
			/* We don't send NTFY or AUEP to wildcard ep */
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

			if (p) {
			  /* When the endpoint have a Off hook transition we allways
			     starts without any previous dtmfs */
			  memset(p->dtmf_buf, 0, sizeof(p->dtmf_buf));
			}

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
			if (sub->owner && ast_channel_state(sub->owner) == AST_STATE_DOWN && !sub->next->owner)
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
					if (sub->owner) {
						ast_queue_hold(sub->owner, NULL);
					}
					sub->next->cxmode = MGCP_CX_RECVONLY;
					handle_hd_hf(sub->next, ev);
				} else if (sub->owner && sub->next->owner) {
					/* We've got two active calls lets decide whether or not to conference or just flip flop */
					if ((!sub->outgoing) && (!sub->next->outgoing)) {
						/* We made both calls lets conference */
						ast_verb(3, "MGCP Conferencing %d and %d on %s@%s\n",
								sub->id, sub->next->id, p->name, p->parent->name);
						sub->cxmode = MGCP_CX_CONF;
						sub->next->cxmode = MGCP_CX_CONF;
						ast_queue_unhold(sub->next->owner);
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

						ast_queue_hold(sub->owner, NULL);
						ast_queue_hold(sub->next->owner, NULL);

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
					ast_queue_unhold(p->sub->owner);
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
				res = attempt_transfer(p, sub);
				if (res) {
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
			if (sub && sub->owner && (ast_channel_state(sub->owner) >=  AST_STATE_UP)) {
				f.frametype = AST_FRAME_DTMF;
				f.subclass.integer = ev[0];
				f.src = "mgcp";
				/* XXX MUST queue this frame to all subs in threeway call if threeway call is active */
				mgcp_queue_frame(sub, &f);
				ast_mutex_lock(&sub->next->lock);
				if (sub->next->owner)
					mgcp_queue_frame(sub->next, &f);
				ast_mutex_unlock(&sub->next->lock);
				if (strstr(p->curtone, (p->ncs ? "wt1" : "wt")) && (ev[0] == 'A')) {
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
	struct mgcp_response *prev = NULL, *cur, *next, *answer = NULL;
	time(&now);
	if (sscanf(req->identifier, "%30d", &seqno) != 1) {
		seqno = 0;
	}
	for (cur = sub->parent->parent->responses, next = cur ? cur->next : NULL; cur; cur = next, next = cur ? cur->next : NULL) {
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
	ast_debug(1, "MGCP read: \n%s\nfrom %s:%d\n", req.data, ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	parse(&req);
	if (req.headers < 1) {
		/* Must have at least one header */
		return 1;
	}
	if (ast_strlen_zero(req.identifier)) {
		ast_log(LOG_NOTICE, "Message from %s missing identifier\n", ast_inet_ntoa(sin.sin_addr));
		return 1;
	}

	if (sscanf(req.verb, "%30d", &result) && sscanf(req.identifier, "%30d", &ident)) {
		if (result < 200) {
			ast_debug(1, "Ignoring provisional response on transaction %d\n", ident);
			return 1;
		}
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

static int mgcp_prune_realtime_gateway(struct mgcp_gateway *g)
{
	struct mgcp_endpoint *enext, *e;
	struct mgcp_subchannel *s, *sub;
	int i, prune = 1;

	if (g->ha || !g->realtime || ast_mutex_trylock(&g->msgs_lock) || g->msgs) {
		ast_mutex_unlock(&g->msgs_lock);
		return 0;
	}

	for (e = g->endpoints; e; e = e->next) {
		ast_mutex_lock(&e->lock);
		if (e->dsp || ast_mutex_trylock(&e->rqnt_queue_lock) || ast_mutex_trylock(&e->cmd_queue_lock)) {
			prune = 0;
		} else if (e->rqnt_queue || e->cmd_queue) {
			prune = 0;
		}
		s = e->sub;
		for (i = 0; (i < MAX_SUBS) && s; i++) {
			ast_mutex_lock(&s->lock);
			if (!ast_strlen_zero(s->cxident) || s->rtp || ast_mutex_trylock(&s->cx_queue_lock) || s->gate) {
				prune = 0;
			} else if (s->cx_queue) {
				prune = 0;
			}
			s = s->next;
		}
	}

	for (e = g->endpoints, sub = e->sub, enext = e->next; e; e = enext, enext = e->next) {
		for (i = 0; (i < MAX_SUBS) && sub; i++) {
			s = sub;
			sub = sub->next;
			ast_mutex_unlock(&s->lock);
			ast_mutex_unlock(&s->cx_queue_lock);
			if (prune) {
				ast_mutex_destroy(&s->lock);
				ast_mutex_destroy(&s->cx_queue_lock);
				ast_free(s);
			}
		}
		ast_mutex_unlock(&e->lock);
		ast_mutex_unlock(&e->rqnt_queue_lock);
		ast_mutex_unlock(&e->cmd_queue_lock);
		if (prune) {
			ast_mutex_destroy(&e->lock);
			ast_mutex_destroy(&e->rqnt_queue_lock);
			ast_mutex_destroy(&e->cmd_queue_lock);
			ast_free(e);
		}
	}
	if (prune) {
		ast_debug(1, "***** MGCP REALTIME PRUNE GW: %s\n", g->name);
	}
	return prune;
}

static void *do_monitor(void *data)
{
	int res;
	int reloading;
	struct mgcp_gateway *g, *gprev;
	/*struct mgcp_gateway *g;*/
	/*struct mgcp_endpoint *e;*/
	/*time_t thispass = 0, lastpass = 0;*/
	time_t lastrun = 0;

	/* Add an I/O event to our UDP socket */
	if (mgcpsock > -1) {
		mgcpsock_read_id = ast_io_add(io, mgcpsock, mgcpsock_read, AST_IO_IN, NULL);
	}
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
	for (;;) {
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
		/* pruning unused realtime gateways, running in every 60 seconds*/
		if(time(NULL) > (lastrun + 60)) {
			ast_mutex_lock(&gatelock);
			g = gateways;
			gprev = NULL;
			while(g) {
				if(g->realtime) {
					if(mgcp_prune_realtime_gateway(g)) {
						if(gprev) {
							gprev->next = g->next;
						} else {
							gateways = g->next;
						}
						ast_mutex_unlock(&g->msgs_lock);
						ast_mutex_destroy(&g->msgs_lock);
						ast_free(g);
					} else {
						ast_mutex_unlock(&g->msgs_lock);
						gprev = g;
					}
				} else {
					gprev = g;
				}
				g = g->next;
			}
			ast_mutex_unlock(&gatelock);
			lastrun = time(NULL);
		}
		/* Okay, now that we know what to do, release the network lock */
		ast_mutex_unlock(&netlock);
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		ast_mutex_unlock(&monlock);
		pthread_testcancel();
		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		/* copied from chan_sip.c */
		if ((res < 0) || (res > 1000)) {
			res = 1000;
		}
		res = ast_io_wait(io, res);
		ast_mutex_lock(&monlock);
		if (res >= 0) {
			ast_sched_runq(sched);
		}
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

static struct ast_channel *mgcp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *dest, int *cause)
{
	struct mgcp_subchannel *sub;
	struct ast_channel *tmpc = NULL;
	char tmp[256];

	if (!(ast_format_cap_iscompatible(cap, global_capability))) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%s'\n",
			ast_format_cap_get_names(cap, &cap_buf));
		/*return NULL;*/
	}
	ast_copy_string(tmp, dest, sizeof(tmp));
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_NOTICE, "MGCP Channels require an endpoint\n");
		return NULL;
	}
	if (!(sub = find_subchannel_and_lock(tmp, 0, NULL))) {
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
	tmpc = mgcp_new(sub->owner ? sub->next : sub, AST_STATE_DOWN, assignedids, requestor);
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
	struct ast_variable *chanvars = NULL;

	/*char txident[80];*/
	int i=0, y=0;
	int gw_reload = 0;
	int ep_reload = 0;
	directmedia = DIRECTMEDIA;

	/* locate existing gateway */
	for (gw = gateways; gw; gw = gw->next) {
		if (!strcasecmp(cat, gw->name)) {
			/* gateway already exists */
			gw->delme = 0;
			gw_reload = 1;
			break;
		}
	}

	if (!gw && !(gw = ast_calloc(1, sizeof(*gw)))) {
		return NULL;
	}

	if (!gw_reload) {
		gw->expire = -1;
		gw->realtime = 0;
		gw->retransid = -1;
		ast_mutex_init(&gw->msgs_lock);
		ast_copy_string(gw->name, cat, sizeof(gw->name));
		/* check if the name is numeric ip */
		if ((strchr(gw->name, '.')) && inet_addr(gw->name) != INADDR_NONE)
			gw->isnamedottedip = 1;
	}
	for (; v; v = v->next) {
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
				{
					struct ast_sockaddr tmp;

					ast_sockaddr_from_sin(&tmp, &gw->addr);
					if (ast_get_ip(&tmp, v->value)) {
						if (!gw_reload) {
							ast_mutex_destroy(&gw->msgs_lock);
							ast_free(gw);
						}
						return NULL;
					}
					ast_sockaddr_to_sin(&tmp, &gw->addr);
				}
			}
		} else if (!strcasecmp(v->name, "defaultip")) {
			struct ast_sockaddr tmp;

			ast_sockaddr_from_sin(&tmp, &gw->defaddr);
			if (ast_get_ip(&tmp, v->value)) {
				if (!gw_reload) {
					ast_mutex_destroy(&gw->msgs_lock);
					ast_free(gw);
				}
				return NULL;
			}
			ast_sockaddr_to_sin(&tmp, &gw->defaddr);
		} else if (!strcasecmp(v->name, "permit") ||
			!strcasecmp(v->name, "deny")) {
			int acl_error = 0;
			gw->ha = ast_append_ha(v->name, v->value, gw->ha, &acl_error);
			if (acl_error) {
				ast_log(LOG_ERROR, "Invalid ACL '%s' specified for MGCP gateway '%s' on line %d. Not creating.\n",
						v->value, cat, v->lineno);
				if (!gw_reload) {
					ast_mutex_destroy(&gw->msgs_lock);
					ast_free(gw);
				} else {
					gw->delme = 1;
				}
				return NULL;
			}
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
		} else if (!strcasecmp(v->name, "ncs")) {
			ncs = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hangupongateremove")) {
			hangupongateremove = ast_true(v->value);
		} else if (!strcasecmp(v->name, "pktcgatealloc")) {
			pktcgatealloc = ast_true(v->value);
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
			y = ast_channel_string2amaflag(v->value);
			if (y < 0) {
				ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
			} else {
				amaflags = y;
			}
		} else if (!strcasecmp(v->name, "setvar")) {
			chanvars = add_var(v->value, chanvars);
		} else if (!strcasecmp(v->name, "clearvars")) {
			if (chanvars) {
				ast_variables_destroy(chanvars);
				chanvars = NULL;
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
				/*
				 * hasvoicemail is a users.conf legacy voicemail enable method.
				 * hasvoicemail is only going to work for app_voicemail mailboxes.
				 */
				if (strchr(gw->name, '@')) {
					ast_copy_string(mailbox, gw->name, sizeof(mailbox));
				} else {
					snprintf(mailbox, sizeof(mailbox), "%s@default", gw->name);
				}
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
			for (e = gw->endpoints; e; e = e->next) {
				if (!strcasecmp(v->value, e->name)) {
					/* endpoint already exists */
					e->delme = 0;
					ep_reload = 1;
					break;
				}
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
					e->cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
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
					struct stasis_topic *mailbox_specific_topic;

					mailbox_specific_topic = ast_mwi_topic(e->mailbox);
					if (mailbox_specific_topic) {
						/* This module does not handle MWI in an event-based manner.  However, it
						 * subscribes to MWI for each mailbox that is configured so that the core
						 * knows that we care about it.  Then, chan_mgcp will get the MWI from the
						 * event cache instead of checking the mailbox directly. */
						e->mwi_event_sub = stasis_subscribe_pool(mailbox_specific_topic, stasis_subscription_cb_noop, NULL);
						stasis_subscription_accept_message_type(e->mwi_event_sub, ast_mwi_state_type());
						stasis_subscription_set_filter(e->mwi_event_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
					}
				}
				snprintf(e->rqnt_ident, sizeof(e->rqnt_ident), "%08lx", (unsigned long)ast_random());
				e->msgstate = -1;
				e->amaflags = amaflags;
				ast_format_cap_append_from_cap(e->cap, global_capability, AST_MEDIA_TYPE_UNKNOWN);
				e->parent = gw;
				e->ncs = ncs;
				e->dtmfmode = dtmfmode;
				if (!ep_reload && e->sub && e->sub->rtp) {
					e->dtmfmode |= MGCP_DTMF_INBAND;
				}
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
				e->chanvars = copy_vars(chanvars);
				if (!ep_reload) {
					/*snprintf(txident, sizeof(txident), "%08lx", (unsigned long)ast_random());*/
					for (i = 0; i < MAX_SUBS; i++) {
						sub = ast_calloc(1, sizeof(*sub));
						if (sub) {
							ast_verb(3, "Allocating subchannel '%d' on %s@%s\n", i, e->name, gw->name);
							ast_mutex_init(&sub->lock);
							ast_mutex_init(&sub->cx_queue_lock);
							sub->parent = e;
							sub->id = i;
							snprintf(sub->txident, sizeof(sub->txident), "%08lx", (unsigned long)ast_random());
							/*stnrcpy(sub->txident, txident, sizeof(sub->txident) - 1);*/
							sub->cxmode = MGCP_CX_INACTIVE;
							sub->nat = nat;
							sub->gate = NULL;
							sub->sdpsent = 0;
							sub->next = e->sub;
							e->sub = sub;
						} else {
							/* XXX Should find a way to clean up our memory */
							ast_log(LOG_WARNING, "Out of memory allocating subchannel\n");
							return NULL;
						}
					}
					/* Make out subs a circular linked list so we can always sping through the whole bunch */
					/* find the end of the list */
					for (sub = e->sub; sub && sub->next; sub = sub->next);
					/* set the last sub->next to the first sub */
					sub->next = e->sub;

					e->next = gw->endpoints;
					gw->endpoints = e;
				}
			}
		} else if (!strcasecmp(v->name, "trunk") ||
		           !strcasecmp(v->name, "line")) {

			/* locate existing endpoint */
			for (e = gw->endpoints; e; e = e->next) {
				if (!strcasecmp(v->value, e->name)) {
					/* endpoint already exists */
					e->delme = 0;
					ep_reload = 1;
					break;
				}
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
					e->cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
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
				ast_format_cap_append_from_cap(e->cap, global_capability, AST_MEDIA_TYPE_UNKNOWN);
				e->dtmfmode = dtmfmode;
				e->ncs = ncs;
				e->pktcgatealloc = pktcgatealloc;
				e->hangupongateremove = hangupongateremove;
				e->adsi = adsi;
				e->type = (!strcasecmp(v->name, "trunk")) ? TYPE_TRUNK : TYPE_LINE;
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

				/* If we already have a valid chanvars, it's not a new endpoint (it's a reload),
				   so first, free previous mem
				 */
				if (e->chanvars) {
					ast_variables_destroy(e->chanvars);
					e->chanvars = NULL;
				}
				e->chanvars = copy_vars(chanvars);

				if (!ep_reload) {
					e->onhooktime = time(NULL);
					/* ASSUME we're onhook */
					e->hookstate = MGCP_ONHOOK;
					snprintf(e->rqnt_ident, sizeof(e->rqnt_ident), "%08lx", (unsigned long)ast_random());
				}

				for (i = 0, sub = NULL; i < MAX_SUBS; i++) {
					if (!ep_reload) {
						sub = ast_calloc(1, sizeof(*sub));
					} else {
						if (!sub) {
							sub = e->sub;
						} else {
							sub = sub->next;
						}
					}

					if (sub) {
						if (!ep_reload) {
							ast_verb(3, "Allocating subchannel '%d' on %s@%s\n", i, e->name, gw->name);
							ast_mutex_init(&sub->lock);
							ast_mutex_init(&sub->cx_queue_lock);
							ast_copy_string(sub->magic, MGCP_SUBCHANNEL_MAGIC, sizeof(sub->magic));
							sub->parent = e;
							sub->id = i;
							snprintf(sub->txident, sizeof(sub->txident), "%08lx", (unsigned long)ast_random());
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
					/* find the end of the list */
					for (sub = e->sub; sub && sub->next; sub = sub->next);
					/* set the last sub->next to the first sub */
					sub->next = e->sub;

					e->next = gw->endpoints;
					gw->endpoints = e;
				}
			}
		} else if (!strcasecmp(v->name, "name") || !strcasecmp(v->name, "lines")) {
			/* just eliminate realtime warnings */
		} else {
			ast_log(LOG_WARNING, "Don't know keyword '%s' at line %d\n", v->name, v->lineno);
		}
	}
	if (!ntohl(gw->addr.sin_addr.s_addr) && !gw->dynamic) {
		ast_log(LOG_WARNING, "Gateway '%s' lacks IP address and isn't dynamic\n", gw->name);
		if (!gw_reload) {
			ast_mutex_destroy(&gw->msgs_lock);
			ast_free(gw);
		}

		/* Return NULL */
		gw_reload = 1;
	} else {
		gw->defaddr.sin_family = AF_INET;
		gw->addr.sin_family = AF_INET;
		if (gw->defaddr.sin_addr.s_addr && !ntohs(gw->defaddr.sin_port)) {
			gw->defaddr.sin_port = htons(DEFAULT_MGCP_GW_PORT);
		}
		if (gw->addr.sin_addr.s_addr && !ntohs(gw->addr.sin_port)) {
			gw->addr.sin_port = htons(DEFAULT_MGCP_GW_PORT);
		}
		{
			struct ast_sockaddr tmp1, tmp2;
			struct sockaddr_in tmp3 = {0,};

			tmp3.sin_addr = gw->ourip;
			ast_sockaddr_from_sin(&tmp1, &gw->addr);
			ast_sockaddr_from_sin(&tmp2, &tmp3);
			if (gw->addr.sin_addr.s_addr && ast_ouraddrfor(&tmp1, &tmp2)) {
				memcpy(&gw->ourip, &__ourip, sizeof(gw->ourip));
			} else {
				ast_sockaddr_to_sin(&tmp2, &tmp3);
				gw->ourip = tmp3.sin_addr;
			}
		}
	}

	if (chanvars) {
		ast_variables_destroy(chanvars);
		chanvars = NULL;
	}
	return (gw_reload ? NULL : gw);
}

static enum ast_rtp_glue_result mgcp_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct mgcp_subchannel *sub = NULL;

	if (!(sub = ast_channel_tech_pvt(chan)) || !(sub->rtp))
		return AST_RTP_GLUE_RESULT_FORBID;

	ao2_ref(sub->rtp, +1);
	*instance = sub->rtp;

	if (sub->parent->directmedia)
		return AST_RTP_GLUE_RESULT_REMOTE;
	else
		return AST_RTP_GLUE_RESULT_LOCAL;
}

static int mgcp_set_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance *rtp, struct ast_rtp_instance *vrtp, struct ast_rtp_instance *trtp, const struct ast_format_cap *cap, int nat_active)
{
	/* XXX Is there such thing as video support with MGCP? XXX */
	struct mgcp_subchannel *sub;
	sub = ast_channel_tech_pvt(chan);
	if (sub && !sub->alreadygone) {
		transmit_modify_with_sdp(sub, rtp, cap);
		return 0;
	}
	return -1;
}

static void mgcp_get_codec(struct ast_channel *chan, struct ast_format_cap *result)
{
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(chan);
	struct mgcp_endpoint *p = sub->parent;

	ast_format_cap_append_from_cap(result, p->cap, AST_MEDIA_TYPE_UNKNOWN);
}

static struct ast_rtp_glue mgcp_rtp_glue = {
	.type = "MGCP",
	.get_rtp_info = mgcp_get_rtp_peer,
	.update_peer = mgcp_set_rtp_peer,
	.get_codec = mgcp_get_codec,
};


static int acf_channel_read(struct ast_channel *chan, const char *funcname, char *args, char *buf, size_t buflen)
{
	struct mgcp_subchannel *sub = ast_channel_tech_pvt(chan);
	int res = 0;

	/* Sanity check */
	if (!chan || ast_channel_tech(chan) != &mgcp_tech) {
		ast_log(LOG_ERROR, "This function requires a valid MGCP channel\n");
		return -1;
	}

	if (!strcasecmp(args, "ncs")) {
		snprintf(buf, buflen, "%s", sub->parent->ncs ?  "yes":"no");
	} else {
		res = -1;
	}
	return res;
}


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
		if(sub->gate) {
			sub->gate->tech_pvt = NULL;
			sub->gate->got_dq_gi = NULL;
			sub->gate->gate_remove = NULL;
			sub->gate->gate_open = NULL;
		}
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

	if (e->mwi_event_sub) {
		e->mwi_event_sub = stasis_unsubscribe(e->mwi_event_sub);
	}

	if (e->chanvars) {
		ast_variables_destroy(e->chanvars);
		e->chanvars = NULL;
	}

	ast_mutex_destroy(&e->lock);
	ast_mutex_destroy(&e->rqnt_queue_lock);
	ast_mutex_destroy(&e->cmd_queue_lock);
	ao2_ref(e->cap, -1);
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
			if (!g->realtime && (e->delme || g->delme)) {
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

static struct ast_variable *add_var(const char *buf, struct ast_variable *list)
{
	struct ast_variable *tmpvar = NULL;
	char *varname = ast_strdupa(buf), *varval = NULL;

	if ((varval = strchr(varname, '='))) {
		*varval++ = '\0';
		if ((tmpvar = ast_variable_new(varname, varval, ""))) {
			tmpvar->next = list;
			list = tmpvar;
		}
	}
	return list;
}

/*! \brief
 * duplicate a list of channel variables, \return the copy.
 */
static struct ast_variable *copy_vars(struct ast_variable *src)
{
	struct ast_variable *res = NULL, *tmp, *v = NULL;

	for (v = src ; v ; v = v->next) {
		if ((tmp = ast_variable_new(v->name, v->value, v->file))) {
			tmp->next = res;
			res = tmp;
		}
	}
	return res;
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

	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value)) {
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
			ast_format_cap_update_by_allow_disallow(global_capability, v->value, 1);
		} else if (!strcasecmp(v->name, "disallow")) {
			ast_format_cap_update_by_allow_disallow(global_capability, v->value, 0);
		} else if (!strcasecmp(v->name, "tos")) {
			if (ast_str2tos(v->value, &qos.tos)) {
			    ast_log(LOG_WARNING, "Invalid tos value at line %d, refer to QoS documentation\n", v->lineno);
			}
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
			if (sscanf(v->value, "%5d", &ourport) == 1) {
				bindaddr.sin_port = htons(ourport);
			} else {
				ast_log(LOG_WARNING, "Invalid port number '%s' at line %d of %s\n", v->value, v->lineno, config);
			}
		} else if (!strcasecmp(v->name, "firstdigittimeout")) {
			firstdigittimeout = atoi(v->value);
		} else if (!strcasecmp(v->name, "gendigittimeout")) {
			gendigittimeout = atoi(v->value);
		} else if (!strcasecmp(v->name, "matchdigittimeout")) {
			matchdigittimeout = atoi(v->value);
		}
	}

	/* mark existing entries for deletion */
	ast_mutex_lock(&gatelock);
	for (g = gateways; g; g = g->next) {
		g->delme = 1;
		for (e = g->endpoints; e; e = e->next) {
			e->delme = 1;
		}
	}
	ast_mutex_unlock(&gatelock);

	for (cat = ast_category_browse(cfg, NULL); cat; cat = ast_category_browse(cfg, cat)) {
		if (strcasecmp(cat, "general")) {
			ast_mutex_lock(&gatelock);
			if ((g = build_gateway(cat, ast_variable_browse(cfg, cat)))) {
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
		bindaddr.sin_port = htons(DEFAULT_MGCP_CA_PORT);
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
			ast_set_qos(mgcpsock, qos.tos, qos.cos, "MGCP");
		}
	}
	ast_mutex_unlock(&netlock);
	ast_config_destroy(cfg);

	/* send audit only to the new endpoints */
	for (g = gateways; g; g = g->next) {
		for (e = g->endpoints; e && e->needaudit; e = e->next) {
			e->needaudit = 0;
			transmit_audit_endpoint(e);
			ast_verb(3, "MGCP Auditing endpoint %s@%s for hookstate\n", e->name, g->name);
		}
	}

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (!(global_capability = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(mgcp_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		ao2_ref(global_capability, -1);
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(global_capability, ast_format_ulaw, 0);
	ast_format_cap_append(mgcp_tech.capabilities, ast_format_ulaw, 0);
	ast_format_cap_append(mgcp_tech.capabilities, ast_format_alaw, 0);
	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
		ao2_ref(global_capability, -1);
		ao2_ref(mgcp_tech.capabilities, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(io = io_context_create())) {
		ast_log(LOG_WARNING, "Unable to create I/O context\n");
		ast_sched_context_destroy(sched);
		ao2_ref(global_capability, -1);
		ao2_ref(mgcp_tech.capabilities, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (reload_config(0)) {
		ao2_ref(global_capability, -1);
		ao2_ref(mgcp_tech.capabilities, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Make sure we can register our mgcp channel type */
	if (ast_channel_register(&mgcp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'MGCP'\n");
		io_context_destroy(io);
		ast_sched_context_destroy(sched);
		ao2_ref(global_capability, -1);
		ao2_ref(mgcp_tech.capabilities, -1);
		return AST_MODULE_LOAD_DECLINE;
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
	} else {
		mgcp_reloading = 1;
	}
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
			for (e = g->endpoints; e; e = e->next) {
				e->delme = 1;
			}
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

	if (mgcpsock > -1) {
		close(mgcpsock);
	}
	ast_rtp_glue_unregister(&mgcp_rtp_glue);
	ast_cli_unregister_multiple(cli_mgcp, sizeof(cli_mgcp) / sizeof(struct ast_cli_entry));
	ast_sched_context_destroy(sched);

	ao2_ref(global_capability, -1);
	global_capability = NULL;
	ao2_ref(mgcp_tech.capabilities, -1);
	mgcp_tech.capabilities = NULL;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Media Gateway Control Protocol (MGCP)",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	.optional_modules = "res_pktccops",
);
