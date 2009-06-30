/*
 * Copyright (C) 2004-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be 
 * used and copied only in accordance with the terms of this license. 
 * The text of the license may generally be found in the root 
 * directory of this installation in the COPYING file.  It 
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must 
 * maintain this copyright notice.
 *
 *****************************************************************************/


#include "chan_ooh323.h"

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

/* Defaults */
#define DEFAULT_CONTEXT "default"
#define DEFAULT_H323ID "Asterisk PBX"
#define DEFAULT_LOGFILE "/var/log/asterisk/h323_log"
#define DEFAULT_H323ACCNT "ast_h323"

/* Flags */
#define H323_SILENCESUPPRESSION (1<<0)
#define H323_GKROUTED           (1<<1)
#define H323_TUNNELING          (1<<2)
#define H323_FASTSTART          (1<<3)
#define H323_OUTGOING           (1<<4)
#define H323_ALREADYGONE        (1<<5)
#define H323_NEEDDESTROY        (1<<6)
#define H323_DISABLEGK          (1<<7)

/* Channel description */
static const char type[] = "OOH323";
static const char tdesc[] = "Objective Systems H323 Channel Driver";
static const char config[] = "chan_ooh323.conf";
static const char config_old[] = "ooh323.conf";


/* Channel Definition */
static struct ast_channel *ooh323_request(const char *type, int format, 
                                        void *data, int *cause);
static int ooh323_digit_begin(struct ast_channel *ast, char digit);
static int ooh323_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int ooh323_call(struct ast_channel *ast, char *dest, int timeout);
static int ooh323_hangup(struct ast_channel *ast);
static int ooh323_answer(struct ast_channel *ast);
static struct ast_frame *ooh323_read(struct ast_channel *ast);
static int ooh323_write(struct ast_channel *ast, struct ast_frame *f);
static int ooh323_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int ooh323_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static enum ast_rtp_get_result ooh323_get_rtp_peer(struct ast_channel *chan, struct ast_rtp **rtp);
static enum ast_rtp_get_result ooh323_get_vrtp_peer(struct ast_channel *chan, struct ast_rtp **rtp);
static int ooh323_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, 
                             struct ast_rtp *vrtp, struct ast_rtp *trtp, int codecs, int nat_active);

static void print_codec_to_cli(int fd, struct ast_codec_pref *pref);

#if 0
static void ast_ooh323c_exit();
#endif

static const struct ast_channel_tech ooh323_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = -1,
	.requester = ooh323_request,
	.send_digit_begin = ooh323_digit_begin,
	.send_digit_end = ooh323_digit_end,
	.call = ooh323_call,
	.hangup = ooh323_hangup,
	.answer = ooh323_answer,
	.read = ooh323_read,
	.write = ooh323_write,
	.exception = ooh323_read,
	.indicate = ooh323_indicate,
	.fixup = ooh323_fixup,
	.send_html = 0,
	.bridge = ast_rtp_bridge,
};

static struct ast_rtp_protocol ooh323_rtp = {
	.type = type,
	.get_rtp_info = ooh323_get_rtp_peer,
	.get_vrtp_info = ooh323_get_vrtp_peer,
	.set_rtp_peer = ooh323_set_rtp_peer
};

/* H.323 channel private structure */
static struct ooh323_pvt {
	ast_mutex_t lock;		/* Channel private lock */
	struct ast_rtp *rtp;
	struct ast_rtp *vrtp; /* Placeholder for now */
	struct ast_channel *owner;	/* Master Channel */
	time_t lastrtptx;
	time_t lastrtprx;
	unsigned int flags;
	unsigned int call_reference;
	char *callToken;
	char *username;
	char *host;
	char *callerid_name;
	char *callerid_num;
	char caller_h323id[AST_MAX_EXTENSION];
	char caller_dialedDigits[AST_MAX_EXTENSION];
	char caller_email[AST_MAX_EXTENSION];
	char caller_url[256];
	char callee_h323id[AST_MAX_EXTENSION];
	char callee_dialedDigits[AST_MAX_EXTENSION];
	char callee_email[AST_MAX_EXTENSION];
	char callee_url[AST_MAX_EXTENSION];
 
	int port;
	int readformat;   /* negotiated read format */
	int writeformat;  /* negotiated write format */
	int capability;
	struct ast_codec_pref prefs;
	int dtmfmode;
	char exten[AST_MAX_EXTENSION];	/* Requested extension */
	char context[AST_MAX_EXTENSION];	/* Context where to start */
	char accountcode[256];	/* Account code */
	int nat;
	int amaflags;
	struct ast_dsp *vad;
	struct ooh323_pvt *next;	/* Next entity */
} *iflist = NULL;

/* Protect the channel/interface list (ooh323_pvt) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Profile of H.323 user registered with PBX*/
struct ooh323_user{
	ast_mutex_t lock;
	char        name[256];
	char        context[AST_MAX_EXTENSION];
	int         incominglimit;
	unsigned    inUse;
	char        accountcode[20];
	int         amaflags;
	int         capability;
	struct ast_codec_pref prefs;
	int         dtmfmode;
	int         rtptimeout;
	int         mUseIP;        /* Use IP address or H323-ID to search user */
	char        mIP[20];
	struct ooh323_user *next;
};

/* Profile of valid asterisk peers */
struct ooh323_peer{
	ast_mutex_t lock;
	char        name[256];
	unsigned    outgoinglimit;
	unsigned    outUse;
	int         capability;
	struct ast_codec_pref prefs;
	char        accountcode[20];
	int         amaflags;
	int         dtmfmode;
	int         mFriend;    /* indicates defined as friend */
	char        ip[20];
	int         port;
	char        *h323id;    /* H323-ID alias, which asterisk will register with gk to reach this peer*/
	char        *email;     /* Email alias, which asterisk will register with gk to reach this peer*/
	char        *url;       /* url alias, which asterisk will register with gk to reach this peer*/
	char        *e164;      /* e164 alias, which asterisk will register with gk to reach this peer*/
	int         rtptimeout;
	struct ooh323_peer *next;
};


/* List of H.323 users known to PBX */
static struct ast_user_list {
	struct ooh323_user *users;
	ast_mutex_t lock;
} userl;

static struct ast_peer_list {
	struct ooh323_peer *peers;
	ast_mutex_t lock;
} peerl;

/* Mutex to protect H.323 reload process */
static int h323_reloading = 0;
AST_MUTEX_DEFINE_STATIC(h323_reload_lock);

/* Mutex to protect usage counter */
static int usecnt = 0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

AST_MUTEX_DEFINE_STATIC(ooh323c_cmd_lock);

/* stack callbacks */
int onAlerting(ooCallData *call);
int onNewCallCreated(ooCallData *call);
int onCallEstablished(ooCallData *call);
int onCallCleared(ooCallData *call);

static char gLogFile[256] = DEFAULT_LOGFILE;
static int  gPort = 1720;
static char gIP[20];
static char gCallerID[AST_MAX_EXTENSION] = DEFAULT_H323ID;
static struct ooAliases *gAliasList;
static int  gCapability = AST_FORMAT_ULAW;
static struct ast_codec_pref gPrefs;
static int  gDTMFMode = H323_DTMF_RFC2833;
static char gGatekeeper[100];
static enum RasGatekeeperMode gRasGkMode = RasNoGatekeeper;

static int  gIsGateway = 0;
static int  gFastStart = 1;
static int  gTunneling = 1;
static int  gMediaWaitForConnect = 0;
static int  gTOS = 0;
static int  gRTPTimeout = 60;
static char gAccountcode[80] = DEFAULT_H323ACCNT;
static int  gAMAFLAGS;
static char gContext[AST_MAX_EXTENSION] = DEFAULT_CONTEXT;
static int  gIncomingLimit = 4;
static int  gOutgoingLimit = 4;
OOBOOL gH323Debug = FALSE;

static struct ooh323_config
{
   int  mTCPPortStart;
   int  mTCPPortEnd;
} ooconfig;

/** Asterisk RTP stuff*/
static struct sched_context *sched;
static struct io_context *io;

/* Protect the monitoring thread, so only one process can kill or start it, 
   and not when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);


/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;


static struct ast_channel *ooh323_new(struct ooh323_pvt *i, int state,
                                             const char *host) 
{
	struct ast_channel *ch = NULL;
	int fmt;
	if (gH323Debug)
		ast_verbose("---   ooh323_new - %s\n", host);


	/* Don't hold a h323 pvt lock while we allocate a channel */
	ast_mutex_unlock(&i->lock);
	ch = ast_channel_alloc(1, state, i->callerid_num, i->callerid_name, i->accountcode, i->exten, i->context, i->amaflags, "OOH323/%s-%08x", host, (unsigned int)(unsigned long) i);
	ast_mutex_lock(&i->lock);

	if (ch) {
		ast_channel_lock(ch);
		ch->tech = &ooh323_tech;

		ch->nativeformats = i->capability;

		fmt = ast_best_codec(ch->nativeformats);

		ch->fds[0] = ast_rtp_fd(i->rtp);
		ch->fds[1] = ast_rtcp_fd(i->rtp);

		if (state == AST_STATE_RING)
			ch->rings = 1;

		ch->adsicpe = AST_ADSI_UNAVAILABLE;
		ch->writeformat = fmt;
		ch->rawwriteformat = fmt;
		ch->readformat = fmt;
		ch->rawreadformat = fmt;
		ch->tech_pvt = i;
		i->owner = ch;

		/* Allocate dsp for in-band DTMF support */
		if (i->dtmfmode & H323_DTMF_INBAND) {
			i->vad = ast_dsp_new();
			ast_dsp_set_features(i->vad, DSP_FEATURE_DIGIT_DETECT);
		}

		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);

		/* Notify the module monitors that use count for resource has changed*/
		ast_update_use_count();

		ast_copy_string(ch->context, i->context, sizeof(ch->context));
		ast_copy_string(ch->exten, i->exten, sizeof(ch->exten));

		ch->priority = 1;
		if (i->callerid_name) {
			ch->cid.cid_name = strdup(i->callerid_name);
		}
		if (i->callerid_num) {

			ch->cid.cid_num = strdup(i->callerid_num);
		}

		if (!ast_test_flag(i, H323_OUTGOING)) {
		
			if (!ast_strlen_zero(i->caller_h323id)) {
				pbx_builtin_setvar_helper(ch, "_CALLER_H323ID", i->caller_h323id);

			}
			if (!ast_strlen_zero(i->caller_dialedDigits)) {
				pbx_builtin_setvar_helper(ch, "_CALLER_H323DIALEDDIGITS", 
				i->caller_dialedDigits);
			}
			if (!ast_strlen_zero(i->caller_email)) {
				pbx_builtin_setvar_helper(ch, "_CALLER_H323EMAIL", 
				i->caller_email);
			}
			if (!ast_strlen_zero(i->caller_url)) {
				pbx_builtin_setvar_helper(ch, "_CALLER_H323URL", i->caller_url);
			}
		}

		if (!ast_strlen_zero(i->accountcode))
			ast_string_field_set(ch, accountcode, i->accountcode);
		
		if (i->amaflags)
			ch->amaflags = i->amaflags;

		ast_setstate(ch, state);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(ch)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ch->name);
				ast_channel_unlock(ch);
				ast_hangup(ch);
				ch = NULL;
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");


	if (ch)
		ast_channel_unlock(ch);

	if (gH323Debug)
		ast_verbose("+++   h323_new\n");

	return ch;
}



static struct ooh323_pvt *ooh323_alloc(int callref, char *callToken) 
{
	struct ooh323_pvt *pvt = NULL;
	struct in_addr ipAddr;
	if (gH323Debug)
		ast_verbose("---   ooh323_alloc\n");

	if (!(pvt = ast_calloc(1, sizeof(*pvt)))) {
		ast_log(LOG_ERROR, "Couldn't allocate private ooh323 structure\n");
		return NULL;
	}

	ast_mutex_init(&pvt->lock);
	ast_mutex_lock(&pvt->lock);

	if (!inet_aton(gIP, &ipAddr)) {
		ast_log(LOG_ERROR, "Invalid OOH323 driver ip address\n");
		ast_mutex_unlock(&pvt->lock);
		ast_mutex_destroy(&pvt->lock);
		free(pvt);
		return NULL;
	}

	if (!(pvt->rtp = ast_rtp_new_with_bindaddr(sched, io, 1, 0, ipAddr))) {
		ast_log(LOG_WARNING, "Unable to create RTP session: %s\n", 
				  strerror(errno));
		ast_mutex_unlock(&pvt->lock);
		ast_mutex_destroy(&pvt->lock);
		free(pvt);
		return NULL;
	}
 
	ast_rtp_setqos(pvt->rtp, gTOS, 0, "ooh323");

	pvt->call_reference = callref;
	if (callToken)
		pvt->callToken = strdup(callToken);

	/* whether to use gk for this call */
	if (gRasGkMode == RasNoGatekeeper)
		OO_SETFLAG(pvt->flags, H323_DISABLEGK);

	pvt->dtmfmode = gDTMFMode;
	ast_copy_string(pvt->context, gContext, sizeof(pvt->context));
	ast_copy_string(pvt->accountcode, gAccountcode, sizeof(pvt->accountcode));
	pvt->amaflags = gAMAFLAGS;
	pvt->capability = gCapability;
	memcpy(&pvt->prefs, &gPrefs, sizeof(pvt->prefs));

	ast_mutex_unlock(&pvt->lock); 
	/* Add to interface list */
	ast_mutex_lock(&iflock);
	pvt->next = iflist;
	iflist = pvt;
	ast_mutex_unlock(&iflock);

	if (gH323Debug)
		ast_verbose("+++   ooh323_alloc\n");

	return pvt;
}


/*
	Possible data values - peername, exten/peername, exten@ip
 */
static struct ast_channel *ooh323_request(const char *type, int format, 
													 void *data, int *cause)
{
	struct ast_channel *chan = NULL;
	struct ooh323_pvt *p = NULL;
	struct ooh323_peer *peer = NULL;
	char *dest = NULL; 
	char *ext = NULL;
	char tmp[256];
	char formats[512];
	int oldformat;
	int port = 0;

	if (gH323Debug)
		ast_verbose("---   ooh323_request - data %s format %s\n", (char*)data,  
										ast_getformatname_multiple(formats,512,format));

	oldformat = format;
	format &= AST_FORMAT_AUDIO_MASK;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format "
								  "'%d'\n", format);
		return NULL;
	}

	p = ooh323_alloc(0,0); /* Initial callRef is zero */

	if (!p) {
		ast_log(LOG_WARNING, "Unable to build pvt data for '%s'\n", (char*)data);
		return NULL;
	}
	ast_mutex_lock(&p->lock);

	/* This is an outgoing call, since ooh323_request is called */
	ast_set_flag(p, H323_OUTGOING);

	ast_copy_string(tmp, data, sizeof(data));

	dest = strchr(tmp, '/');

	if (dest) {  
		*dest = '\0';
		dest++;
		ext = tmp;
	} else if ((dest = strchr(tmp, '@'))) {
		*dest = '\0';
		dest++;
		ext = tmp;
	} else {
		dest = tmp;
		ext = NULL;
	}

#if 0
	if ((sport = strchr(dest, ':'))) {
		*sport = '\0';
		sport++;
		port = atoi(sport);
	}
#endif

	if (dest) {
		peer = find_peer(dest, port);
	} else{
		ast_log(LOG_ERROR, "Destination format is not supported\n");
		return NULL;
	}

	if (peer) {
		p->username = strdup(peer->name);
		p->host = strdup(peer->ip);
		p->port = peer->port;
		/* Disable gk as we are going to call a known peer*/
		OO_SETFLAG(p->flags, H323_DISABLEGK);

		if (ext)
			ast_copy_string(p->exten, ext, sizeof(p->exten));

		if (peer->capability & format) {
			p->capability = peer->capability & format;
		} else {
		  p->capability = peer->capability;
		}
		memcpy(&p->prefs, &peer->prefs, sizeof(struct ast_codec_pref));
		p->dtmfmode = peer->dtmfmode;
		ast_copy_string(p->accountcode, peer->accountcode, sizeof(p->accountcode));
		p->amaflags = peer->amaflags;
	} else {
		p->dtmfmode = gDTMFMode;
		p->capability = gCapability;

		memcpy(&p->prefs, &gPrefs, sizeof(struct ast_codec_pref));
		p->username = strdup(dest);

		p->host = strdup(dest);
		if (port > 0) {
			p->port = port;
		}
		if (ext) {
			ast_copy_string(p->exten, ext, sizeof(p->exten));
		}
	}


	chan = ooh323_new(p, AST_STATE_DOWN, p->username);
	
	ast_mutex_unlock(&p->lock);

	if (!chan) {
		ast_mutex_lock(&iflock);
		ooh323_destroy(p);
		ast_mutex_unlock(&iflock);
	}

	restart_monitor();
	if (gH323Debug)
		ast_verbose("+++   ooh323_request\n");

	return chan;

}


static struct ooh323_pvt* find_call(ooCallData *call)
{
	struct ooh323_pvt *p;

	if (gH323Debug)
		ast_verbose("---   find_call\n");

	ast_mutex_lock(&iflock);

	for (p = iflist; p; p = p->next) {
		if (p->callToken && !strcmp(p->callToken, call->callToken)) {
			break;
		}
	}
	ast_mutex_unlock(&iflock);

	if (gH323Debug)
		ast_verbose("+++   find_call\n");

	return p;
}

struct ooh323_user *find_user(const char * name, const char* ip)
{
	struct ooh323_user *user;

	if (gH323Debug)
		ast_verbose("---   find_user\n");

	ast_mutex_lock(&userl.lock);
	for (user = userl.users; user; user = user->next) {
		if (ip && user->mUseIP && !strcmp(user->mIP, ip)) {
			break;
		}
		if (name && !strcmp(user->name, name)) {
			break;
		}
	}
	ast_mutex_unlock(&userl.lock);

	if (gH323Debug)
		ast_verbose("+++   find_user\n");

	return user;
}

struct ooh323_peer *find_friend(const char *name, int port)
{
	struct ooh323_peer *peer;  

	if (gH323Debug)
		ast_verbose("---   find_friend \"%s\"\n", name);


	ast_mutex_lock(&peerl.lock);
	for (peer = peerl.peers; peer; peer = peer->next) {
		if (gH323Debug) {
			ast_verbose("		comparing with \"%s\"\n", peer->ip);
		}
		if (!strcmp(peer->ip, name)) {
			if (port <= 0 || (port > 0 && peer->port == port)) {
				break;
			}
		}
	}
	ast_mutex_unlock(&peerl.lock);

	if (gH323Debug) {
		if (peer) {
			ast_verbose("		found matching friend\n");
		}
		ast_verbose("+++   find_friend \"%s\"\n", name);
	}

	return peer;		
}


struct ooh323_peer *find_peer(const char * name, int port)
{
	struct ooh323_peer *peer;

	if (gH323Debug)
		ast_verbose("---   find_peer \"%s\"\n", name);

	ast_mutex_lock(&peerl.lock);
	for (peer = peerl.peers; peer; peer = peer->next) {
		if (gH323Debug) {
			ast_verbose("		comparing with \"%s\"\n", peer->ip);
		}
		if (!strcasecmp(peer->name, name))
			break;
		if (peer->h323id && !strcasecmp(peer->h323id, name))
			break;
		if (peer->e164 && !strcasecmp(peer->e164, name))
			break;
		/*
		if (!strcmp(peer->ip, name)) {
			if (port > 0 && peer->port == port) { break; }
			else if (port <= 0) { break; }
		}
		*/
	}
	ast_mutex_unlock(&peerl.lock);

	if (gH323Debug) {
		if (peer) {
			ast_verbose("		found matching peer\n");
		}
		ast_verbose("+++   find_peer \"%s\"\n", name);
	}

	return peer;		
}

static int ooh323_digit_begin(struct ast_channel *chan, char digit)
{
	char dtmf[2];
	struct ooh323_pvt *p = (struct ooh323_pvt *) chan->tech_pvt;
	
	if (gH323Debug)
		ast_verbose("---   ooh323_digit_begin\n");

	if (!p) {
		ast_log(LOG_ERROR, "No private structure for call\n");
		return -1;
	}
	ast_mutex_lock(&p->lock);
	if (p->rtp && (p->dtmfmode & H323_DTMF_RFC2833)) {
		ast_rtp_senddigit_begin(p->rtp, digit);
	} else if (((p->dtmfmode & H323_DTMF_Q931) ||
						 (p->dtmfmode & H323_DTMF_H245ALPHANUMERIC) ||
						 (p->dtmfmode & H323_DTMF_H245SIGNAL))) {
		dtmf[0] = digit;
		dtmf[1] = '\0';
		ast_mutex_lock(&ooh323c_cmd_lock);
		ooSendDTMFDigit(p->callToken, dtmf);
		ast_mutex_unlock(&ooh323c_cmd_lock);
	}
	ast_mutex_unlock(&p->lock);
	if (gH323Debug)
		ast_verbose("+++   ooh323_digit_begin\n");

	return 0;
}

static int ooh323_digit_end(struct ast_channel *chan, char digit, unsigned int duration)
{
	struct ooh323_pvt *p = (struct ooh323_pvt *) chan->tech_pvt;

	if (gH323Debug)
		ast_verbose("---   ooh323_digit_end\n");

	if (!p) {
		ast_log(LOG_ERROR, "No private structure for call\n");
		return -1;
	}
	ast_mutex_lock(&p->lock);
	if (p->rtp && (p->dtmfmode & H323_DTMF_RFC2833)) 
		ast_rtp_senddigit_end(p->rtp, digit);

	ast_mutex_unlock(&p->lock);
	if (gH323Debug)
		ast_verbose("+++   ooh323_digit_end\n");

	return 0;
}


static int ooh323_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct ooh323_pvt *p = ast->tech_pvt;
	char destination[256];
	int res = 0;
	const char *val = NULL;
	ooCallOptions opts = {
		.fastStart = TRUE,
		.tunneling = TRUE,
		.disableGk = TRUE,
		.callMode = OO_CALLMODE_AUDIOCALL
	};
	if (gH323Debug)
		ast_verbose("---   ooh323_call- %s\n", dest);

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "ooh323_call called on %s, neither down nor "
									"reserved\n", ast->name);
		return -1;
	}
	ast_mutex_lock(&p->lock);
	ast_set_flag(p, H323_OUTGOING);
	if (ast->cid.cid_num) {
		if (p->callerid_num) {
			free(p->callerid_num);
		}
		p->callerid_num = strdup(ast->cid.cid_num);
	}

	if (ast->cid.cid_name) {
		if (p->callerid_name) {
			free(p->callerid_name);
		}
		p->callerid_name = strdup(ast->cid.cid_name);
	}
	else{
		ast->cid.cid_name = strdup(gCallerID);
		if (p->callerid_name) {
			free(p->callerid_name);
		}
		p->callerid_name = strdup(ast->cid.cid_name);
	}

	/* Retrieve vars */


	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323ID"))) {
		ast_copy_string(p->caller_h323id, val, sizeof(p->caller_h323id));
	}
	
	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323DIALEDDIGITS"))) {
		ast_copy_string(p->caller_dialedDigits, val, sizeof(p->caller_dialedDigits));
		if (!p->callerid_num) {
			p->callerid_num = strdup(val);
		}
	}

	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323EMAIL"))) {
		ast_copy_string(p->caller_email, val, sizeof(p->caller_email));
	}

	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323URL"))) {
		ast_copy_string(p->caller_url, val, sizeof(p->caller_url));
	}


	if (!(p->callToken = (char*)malloc(AST_MAX_EXTENSION))) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Failed to allocate memory for callToken\n");
		return -1; /* TODO: need to clean/hangup?? */
	}		

	if (p->host && p->port != 0)
		snprintf(destination, sizeof(destination), "%s:%d", p->host, p->port);
	else if (p->host)
		snprintf(destination, sizeof(destination), "%s", p->host);
	else
		ast_copy_string(destination, dest, sizeof(destination));

	ast_mutex_lock(&ooh323c_cmd_lock);
	if (OO_TESTFLAG(p->flags, H323_DISABLEGK))
		res = ooMakeCall(destination, p->callToken, AST_MAX_EXTENSION, &opts);
	else
		res = ooMakeCall(destination, p->callToken, AST_MAX_EXTENSION, NULL);
	ast_mutex_unlock(&ooh323c_cmd_lock);

	ast_mutex_unlock(&p->lock);
	if (res != OO_OK) {
		ast_log(LOG_ERROR, "Failed to make call\n");
		return -1; /* TODO: cleanup */
	}
	if (gH323Debug)
		ast_verbose("+++   ooh323_call\n");

  return 0;
}

static int ooh323_hangup(struct ast_channel *ast)
{
	struct ooh323_pvt *p = ast->tech_pvt;

	if (gH323Debug)
		ast_verbose("---   ooh323_hangup\n");

	if (p) {
		ast_mutex_lock(&p->lock);

		if (gH323Debug)
			ast_verbose("	 hanging %s\n", p->username);
		ast->tech_pvt = NULL; 
		if (!ast_test_flag(p, H323_ALREADYGONE)) {
			ast_mutex_lock(&ooh323c_cmd_lock);
			ooHangCall(p->callToken, 
				 ooh323_convert_hangupcause_asteriskToH323(p->owner->hangupcause));
			ast_mutex_unlock(&ooh323c_cmd_lock);
			ast_set_flag(p, H323_ALREADYGONE);
			/* ast_mutex_unlock(&p->lock); */
		} else {
			ast_set_flag(p, H323_NEEDDESTROY);
		}
		/* detach channel here */
		if (p->owner) {
			p->owner->tech_pvt = NULL;
			p->owner = NULL;
		}

		ast_mutex_unlock(&p->lock);
		ast_mutex_lock(&usecnt_lock);
		usecnt--;
		ast_mutex_unlock(&usecnt_lock);

		/* Notify the module monitors that use count for resource has changed */
		ast_update_use_count();
	  
	} else {
		ast_log(LOG_ERROR, "No call to hangup\n" );
		return -1;
	}
	
	if (gH323Debug)
		ast_verbose("+++   ooh323_hangup\n");

  return 0;
}

static int ooh323_answer(struct ast_channel *ast)
{
	struct ooh323_pvt *p = ast->tech_pvt;

	if (gH323Debug)
		ast_verbose("--- ooh323_answer\n");

	ast_mutex_lock(&p->lock);
	if (ast->_state != AST_STATE_UP) {
		ast_channel_lock(ast);
		ast_setstate(ast, AST_STATE_UP);
		ast_debug(1, "ooh323_answer(%s)\n", ast->name);
		ast_channel_unlock(ast);
		ast_mutex_lock(&ooh323c_cmd_lock);
		ooAnswerCall(p->callToken);
		ast_mutex_unlock(&ooh323c_cmd_lock);
	}
	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++ ooh323_answer\n");

  return 0;
}

static struct ast_frame *ooh323_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	struct ooh323_pvt *p = ast->tech_pvt;

	ast_mutex_lock(&p->lock);
	if (p->rtp)
		fr = ooh323_rtp_read(ast, p);
	else
		fr = &null_frame;
	/* time(&p->lastrtprx); */
	ast_mutex_unlock(&p->lock);
	return fr;
}

static int ooh323_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct ooh323_pvt *p = ast->tech_pvt;
	int res = 0;

	if (f->frametype == AST_FRAME_VOICE) {
		if (!(f->subclass & ast->nativeformats)) {
			ast_log(LOG_WARNING, "Asked to transmit frame type %d, while native "
									  "formats is %d (read/write = %d/%d)\n",
									f->subclass, ast->nativeformats, ast->readformat,
										ast->writeformat);
			return 0;
		}
		if (p) {
			ast_mutex_lock(&p->lock);
			if (p->rtp)
				res = ast_rtp_write(p->rtp, f);
			ast_mutex_unlock(&p->lock);
		}
	} else if (f->frametype == AST_FRAME_IMAGE) {
		return 0;
	} else {
		ast_log(LOG_WARNING, "Can't send %d type frames with SIP write\n", 
									 f->frametype);
		return 0;
	}

	return res;
}

static int ooh323_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{

	struct ooh323_pvt *p = (struct ooh323_pvt *) ast->tech_pvt;
	char *callToken = (char *)NULL;

	ast_mutex_lock(&p->lock);
	callToken = (p->callToken ? strdup(p->callToken) : NULL);
	ast_mutex_unlock(&p->lock);

	if (!callToken) {
		if (gH323Debug)
			ast_verbose("	ooh323_indicate - No callToken\n");
		return -1;
	}

	if (gH323Debug)
		ast_verbose("----- ooh323_indicate %d on call %s\n", condition, callToken);
	 

	switch (condition) {
	case AST_CONTROL_CONGESTION:
		if (!ast_test_flag(p, H323_ALREADYGONE)) {
			ast_mutex_lock(&ooh323c_cmd_lock);
			ooHangCall(callToken, OO_REASON_LOCAL_CONGESTED);
			ast_mutex_unlock(&ooh323c_cmd_lock);
			ast_set_flag(p, H323_ALREADYGONE);
		}
		break;
	case AST_CONTROL_BUSY:
		if (!ast_test_flag(p, H323_ALREADYGONE)) {
			ast_mutex_lock(&ooh323c_cmd_lock);
			ooHangCall(callToken, OO_REASON_LOCAL_BUSY);
			ast_mutex_unlock(&ooh323c_cmd_lock);
			ast_set_flag(p, H323_ALREADYGONE);
		}
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);		
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_PROCEEDING:
	case AST_CONTROL_RINGING:
	case AST_CONTROL_PROGRESS:
	case -1:
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d on %s\n",
									condition, callToken);
	}

	if (gH323Debug)
		ast_verbose("++++  ooh323_indicate %d on %s\n", condition, callToken);


	return -1;
}

static int ooh323_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct ooh323_pvt *p = newchan->tech_pvt;

	if (gH323Debug)
		ast_verbose("--- ooh323c ooh323_fixup\n");

	ast_mutex_lock(&p->lock);
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "Old channel wasn't %p but was %p\n", oldchan, p->owner);
		ast_mutex_unlock(&p->lock);
		return -1;
	}

	if (p->owner == oldchan) {
		p->owner = newchan;
	} else {
		p->owner = oldchan;
	}

	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++ ooh323c ooh323_fixup \n");

	return 0;
}


void ooh323_set_write_format(ooCallData *call, int fmt)
{
#if 0
	struct ooh323_pvt *p = NULL;
	char formats[512];
#ifdef print_debug
	printf("---   ooh323_update_writeformat %s\n", 
										ast_getformatname_multiple(formats,512, fmt));
#endif
	
	p = find_call(call);
	if (!p) {
		ast_log(LOG_ERROR, "No matching call found for %s\n", call->callToken);
		return;
	}

	ast_mutex_lock(&p->lock);

	p->writeformat = fmt;
	ast_mutex_unlock(&p->lock);

	if (p->owner) {
	  printf("Writeformat before update %s\n", 
				  ast_getformatname_multiple(formats,512, p->owner->writeformat));
	  ast_set_write_format(p->owner, fmt);
	}
	else
	  ast_log(LOG_ERROR, "No owner found\n");


#ifdef print_debug
  printf("+++   ooh323_update_writeformat\n");
#endif
#endif
}


void ooh323_set_read_format(ooCallData *call, int fmt)
{
#if 0
	struct ooh323_pvt *p = NULL;
	char formats[512];
#ifdef print_debug
	printf("---   ooh323_update_readformat %s\n", 
											ast_getformatname_multiple(formats,512, fmt));
#endif

	p = find_call(call);
	if (!p) {
		ast_log(LOG_ERROR, "No matching call found for %s\n", call->callToken);
		return;
	}

	ast_mutex_lock(&p->lock);
	p->readformat = fmt;
	ast_mutex_unlock(&p->lock);
	ast_set_read_format(p->owner, fmt);	
	
#ifdef print_debug
  printf("+++   ooh323_update_readformat\n");
#endif
#endif
}

int onAlerting(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;
	struct ast_channel *c = NULL;

	if (gH323Debug)
		ast_verbose("--- onAlerting %s\n", call->callToken);

	if (!(p = find_call(call))) {
		ast_log(LOG_ERROR, "No matching call found\n");
		return -1;
	}  
	ast_mutex_lock(&p->lock);
	if (!ast_test_flag(p, H323_OUTGOING)) {
		if (!(c = ooh323_new(p, AST_STATE_RING, p->username))) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Could not create ast_channel\n");
			return -1;
		}
		ast_mutex_unlock(&p->lock);
	} else {
		if (!p->owner) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return 0;
		}
		c = p->owner;
		ast_mutex_unlock(&p->lock);
		ast_channel_lock(c);
		ast_setstate(c, AST_STATE_RINGING);
		ast_channel_unlock(c);
		ast_queue_control(c, AST_CONTROL_RINGING);
	}

	if (gH323Debug)
		ast_verbose("+++ onAlerting %s\n", call->callToken);

	return OO_OK;
}

/**
  * Callback for sending digits from H.323 up to asterisk
  *
  */
int ooh323_onReceivedDigit(OOH323CallData *call, const char *digit)
{
	struct ooh323_pvt *p = NULL;
	struct ast_frame f;
	int res;

	ast_debug(1, "Received Digit: %c\n", digit[0]);
	p = find_call(call);
	if (!p) {
		ast_log(LOG_ERROR, "Failed to find a matching call.\n");
		return -1;
	}
	if (!p->owner) {
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return -1;
	}
	ast_mutex_lock(&p->lock);
	memset(&f, 0, sizeof(f));
	f.frametype = AST_FRAME_DTMF;
	f.subclass = digit[0];
	f.datalen = 0;
	f.samples = 800;
	f.offset = 0;
	f.data.ptr = NULL;
	f.mallocd = 0;
	f.src = "SEND_DIGIT";
	ast_mutex_unlock(&p->lock);
	res = ast_queue_frame(p->owner, &f);
	return res;
}

int ooh323_onReceivedSetup(ooCallData *call, Q931Message *pmsg)
{
	struct ooh323_pvt *p = NULL;
	struct ooh323_user *user = NULL;
	ooAliases *alias = NULL;
	char *at = NULL;
	char number [OO_MAX_NUMBER_LENGTH];

	if (gH323Debug)
		ast_verbose("---   ooh323_onReceivedSetup %s\n", call->callToken);


	if (!(p = ooh323_alloc(call->callReference, call->callToken))) {
		ast_log(LOG_ERROR, "Failed to create a new call.\n");
		return -1;
	}
	ast_mutex_lock(&p->lock);
	ast_clear_flag(p, H323_OUTGOING);
  

	if (call->remoteDisplayName) {
		p->callerid_name = strdup(call->remoteDisplayName);
	}

	if (ooCallGetCallingPartyNumber(call, number, OO_MAX_NUMBER_LENGTH) == OO_OK) {
		p->callerid_num = strdup(number);
	}

	if (call->remoteAliases) {
		for (alias = call->remoteAliases; alias; alias = alias->next) {
			if (alias->type == T_H225AliasAddress_h323_ID) {
				if (!p->callerid_name) {
					p->callerid_name = strdup(alias->value);
				}
				ast_copy_string(p->caller_h323id, alias->value, sizeof(p->caller_h323id));
			} else if (alias->type == T_H225AliasAddress_dialedDigits) {
				if (!p->callerid_num) {
					p->callerid_num = strdup(alias->value);
				}
				ast_copy_string(p->caller_dialedDigits, alias->value, 
															sizeof(p->caller_dialedDigits));
			} else if (alias->type == T_H225AliasAddress_email_ID) {
				ast_copy_string(p->caller_email, alias->value, sizeof(p->caller_email));
			} else if (alias->type == T_H225AliasAddress_url_ID) {
				ast_copy_string(p->caller_url, alias->value, sizeof(p->caller_url));
			}
		}
	}

	number[0] = '\0';
	if (ooCallGetCalledPartyNumber(call, number, OO_MAX_NUMBER_LENGTH) == OO_OK) {
		ast_copy_string(p->exten, number, sizeof(p->exten));
	} else {
		update_our_aliases(call, p);
		if (!ast_strlen_zero(p->callee_dialedDigits)) {
			ast_copy_string(p->exten, p->callee_dialedDigits, sizeof(p->exten));
		} else if (!ast_strlen_zero(p->callee_h323id)) {
			ast_copy_string(p->exten, p->callee_h323id, sizeof(p->exten));
		} else if (!ast_strlen_zero(p->callee_email)) {
			ast_copy_string(p->exten, p->callee_email, sizeof(p->exten));
			if ((at = strchr(p->exten, '@'))) {
				*at = '\0';
			}
		}
	}

	/* if no extension found, set to default 's' */
	if (ast_strlen_zero(p->exten)) {
		ast_copy_string(p->exten, "s", sizeof(p->exten));
	}

	if (!p->callerid_name) {
		p->callerid_name = strdup(call->remoteIP);
	}
	
	if (p->callerid_name) {
		if ((user = find_user(p->callerid_name, call->remoteIP))) {
			ast_mutex_lock(&user->lock);
			p->username = strdup(user->name);
			ast_copy_string(p->context, user->context, sizeof(p->context));
			ast_copy_string(p->accountcode, user->accountcode, sizeof(p->accountcode));
			p->amaflags = user->amaflags;
			p->capability = user->capability;
			memcpy(&p->prefs, &user->prefs, sizeof(struct ast_codec_pref));
			p->dtmfmode = user->dtmfmode;
			/* Since, call is coming from a pbx user, no need to use gk */
			OO_SETFLAG(p->flags, H323_DISABLEGK);
			OO_SETFLAG(call->flags, OO_M_DISABLEGK);
			ast_mutex_unlock(&user->lock);
		}
	}


	ooh323c_set_capability_for_call(call, &p->prefs, p->capability, p->dtmfmode);
	configure_local_rtp(p, call);

	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++   ooh323_onReceivedSetup - Determined context %s, "
						"extension %s\n", p->context, p->exten);

	return OO_OK;
}



int onNewCallCreated(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;
	int i = 0;

	if (gH323Debug)
		ast_verbose("---   onNewCallCreated %s\n", call->callToken);

	if (!strcmp(call->callType, "outgoing")) {
		p = find_call(call);
		if (!p) {
			ast_log(LOG_ERROR, "No matching call found for outgoing call\n");
			return -1;
		}
		ast_mutex_lock(&p->lock);
		if (p->callerid_name) {
			ooCallSetCallerId(call, p->callerid_name);
		}
		if (p->callerid_num) {
			i = 0;
			while (*(p->callerid_num + i) != '\0') {
				if (!isdigit(*(p->callerid_num + i))) {
					break;
				}
				i++;
			}
			if (*(p->callerid_num + i) == '\0') {
				ooCallSetCallingPartyNumber(call, p->callerid_num);
			} else {
				if (!p->callerid_name) {
					ooCallSetCallerId(call, p->callerid_num);
				}
			}
		}
		
		if (!ast_strlen_zero(p->caller_h323id))
			ooCallAddAliasH323ID(call, p->caller_h323id);

		if (!ast_strlen_zero(p->caller_dialedDigits)) {
			if (gH323Debug) {
				ast_verbose("Setting dialed digits %s\n", p->caller_dialedDigits);
			}
			ooCallAddAliasDialedDigits(call, p->caller_dialedDigits);
		} else if (p->callerid_num) {
			if (ooIsDailedDigit(p->callerid_num)) {
				if (gH323Debug) {
					ast_verbose("setting callid number %s\n", p->callerid_num);
				}
				ooCallAddAliasDialedDigits(call, p->callerid_num);
			} else if (ast_strlen_zero(p->caller_h323id)) {
				ooCallAddAliasH323ID(call, p->callerid_num);
			}
		}
  

		if (!ast_strlen_zero(p->exten))  {
			if (ooIsDailedDigit(p->exten)) {
				ooCallSetCalledPartyNumber(call, p->exten);
				ooCallAddRemoteAliasDialedDigits(call, p->exten);
			} else {
			  ooCallAddRemoteAliasH323ID(call, p->exten);
			}
		}

		if (gH323Debug) {
			char prefsBuf[256];
			ast_codec_pref_string(&p->prefs, prefsBuf, sizeof(prefsBuf));
			ast_verbose(" Outgoing call %s(%s) - Codec prefs - %s\n", 
						  p->username?p->username:"NULL", call->callToken, prefsBuf);
		}

		ooh323c_set_capability_for_call(call, &p->prefs, p->capability, p->dtmfmode);

		configure_local_rtp(p, call);
		ast_mutex_unlock(&p->lock);
	}
	if (gH323Debug)
		ast_verbose("+++   onNewCallCreated %s\n", call->callToken);

	return OO_OK;
}

int onCallEstablished(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;

	if (gH323Debug)
		ast_verbose("---   onCallEstablished %s\n", call->callToken);

	if (!(p = find_call(call))) {
		ast_log(LOG_ERROR, "Failed to find a matching call.\n");
		return -1;
	}
	ast_mutex_lock(&p->lock);
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return -1;
	}
	
	while (ast_channel_trylock(p->owner)) {
		ast_debug(1,"Failed to grab lock, trying again\n");
		ast_mutex_unlock(&p->lock);
		usleep(1);
		ast_mutex_lock(&p->lock);
	}	 
	if (p->owner->_state != AST_STATE_UP) {
		ast_setstate(p->owner, AST_STATE_UP);
	}
	ast_channel_unlock(p->owner);
	if (ast_test_flag(p, H323_OUTGOING)) {
		struct ast_channel* c = p->owner;
		ast_mutex_unlock(&p->lock);
		ast_queue_control(c, AST_CONTROL_ANSWER);
	} else {
		ast_mutex_unlock(&p->lock);
	}

	if (gH323Debug)
		ast_verbose("+++   onCallEstablished %s\n", call->callToken);

	return OO_OK;
}

int onCallCleared(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;
	int ownerLock = 0;

	if (gH323Debug)
		ast_verbose("---   onCallCleared %s \n", call->callToken);

	p = find_call(call); 
	if (!p) {
		return 0;
	}
	ast_mutex_lock(&p->lock);
  
	while (p->owner) {
		if (ast_channel_trylock(p->owner)) {
			ooTrace(OOTRCLVLINFO, "Failed to grab lock, trying again\n");
			ast_debug(1,"Failed to grab lock, trying again\n");
			ast_mutex_unlock(&p->lock);
			usleep(1);
			ast_mutex_lock(&p->lock);
		} else {
			ownerLock = 1;
			break;
		}
	}

	if (ownerLock) {
		if (!ast_test_flag(p, H323_ALREADYGONE)) { 

			/* NOTE: Channel is not detached yet */
			ast_set_flag(p, H323_ALREADYGONE);
			p->owner->hangupcause = 
				ooh323_convert_hangupcause_h323ToAsterisk(call->callEndReason);
			p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
			ast_channel_unlock(p->owner);
			ast_queue_hangup(p->owner);
			ast_mutex_unlock(&p->lock);
			return OO_OK;
		}
		ast_channel_unlock(p->owner);
	}
	ast_set_flag(p, H323_NEEDDESTROY);
	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++   onCallCleared\n");

	return OO_OK;
}

#if 0
static void ooh323_delete_user(struct ooh323_user *user)
{
	struct ooh323_user *prev = NULL, *cur = NULL;

	if (gH323Debug)
		ast_verbose("---   ooh323_delete_user\n");

	if (user) {	
		cur = userl.users;
		ast_mutex_lock(&userl.lock);
		while (cur) {
			if (cur == user) break;
			prev = cur;
			cur = cur->next;
		}

		if (cur) {
			if (prev)
				prev->next = cur->next;
			else
				userl.users = cur->next;
		}
		ast_mutex_unlock(&userl.lock);

		free(user);
	}  

	if (gH323Debug)
		ast_verbose("+++   ooh323_delete_user\n");

}
#endif

void ooh323_delete_peer(struct ooh323_peer *peer)
{
	struct ooh323_peer *prev = NULL, *cur = NULL;

	if (gH323Debug)
		ast_verbose("---   ooh323_delete_peer\n");

	if (peer) {	
		ast_mutex_lock(&peerl.lock);
		for (cur = peerl.peers; cur; prev = cur, cur = cur->next) {
			if (cur == peer) {
				break;
			}
		}

		if (cur) {
			if (prev) {
				prev->next = cur->next;
			} else {
				peerl.peers = cur->next;
			}
		}
		ast_mutex_unlock(&peerl.lock);

		if (peer->h323id)
			free(peer->h323id);
		if (peer->email)
			free(peer->email);
		if (peer->url)
			free(peer->url);
		if (peer->e164)
			free(peer->e164);

		free(peer);
	}  

	if (gH323Debug)
		ast_verbose("+++   ooh323_delete_peer\n");
}



static struct ooh323_user *build_user(const char *name, struct ast_variable *v)
{
	struct ooh323_user *user = NULL;

	if (gH323Debug)
		ast_verbose("---   build_user\n");

	user = ast_calloc(1, sizeof(*user));
	if (user) {
		ast_mutex_init(&user->lock);
		ast_copy_string(user->name, name, sizeof(user->name));
		user->capability = gCapability;
		memcpy(&user->prefs, &gPrefs, sizeof(user->prefs));
		user->rtptimeout = gRTPTimeout;
		user->dtmfmode = gDTMFMode;
		/* set default context */
		ast_copy_string(user->context, gContext, sizeof(user->context));
		ast_copy_string(user->accountcode, gAccountcode, sizeof(user->accountcode));
		user->amaflags = gAMAFLAGS;

		while (v) {
			if (!strcasecmp(v->name, "context")) {
				ast_copy_string(user->context, v->value, sizeof(user->context));
			} else if (!strcasecmp(v->name, "incominglimit")) {
				user->incominglimit = atoi(v->value);
				if (user->incominglimit < 0)
					user->incominglimit = 0;
			} else if (!strcasecmp(v->name, "accountcode")) {
				ast_copy_string(user->accountcode, v->value, sizeof(user->accountcode));
			} else if (!strcasecmp(v->name, "rtptimeout")) {
				user->rtptimeout = atoi(v->value);
				if (user->rtptimeout < 0)
					user->rtptimeout = gRTPTimeout;
			} else if (!strcasecmp(v->name, "disallow")) {
				ast_parse_allow_disallow(&user->prefs, &user->capability, 
												 v->value, 0);
			} else if (!strcasecmp(v->name, "allow")) {
				const char* tcodecs = v->value;
				if (!strcasecmp(v->value, "all")) {
					tcodecs = "ulaw,alaw,g729,g723,gsm";
				}
				ast_parse_allow_disallow(&user->prefs, &user->capability, 
												 tcodecs, 1);
			} else if (!strcasecmp(v->name, "amaflags")) {
				user->amaflags = ast_cdr_amaflags2int(v->value);
			} else if (!strcasecmp(v->name, "ip")) {
				ast_copy_string(user->mIP, v->value, sizeof(user->mIP));
				user->mUseIP = 1;
			} else if (!strcasecmp(v->name, "dtmfmode")) {
				if (!strcasecmp(v->value, "rfc2833"))
					user->dtmfmode = H323_DTMF_RFC2833;
				else if (!strcasecmp(v->value, "q931keypad"))
					user->dtmfmode = H323_DTMF_Q931;
				else if (!strcasecmp(v->value, "h245alphanumeric"))
					user->dtmfmode = H323_DTMF_H245ALPHANUMERIC;
				else if (!strcasecmp(v->value, "h245signal"))
					user->dtmfmode = H323_DTMF_H245SIGNAL;
			}
			v = v->next;
		}
	}

	if (gH323Debug)
		ast_verbose("+++   build_user\n");

	return user;
}

static struct ooh323_peer *build_peer(const char *name, struct ast_variable *v, int friend_type)
{
	struct ooh323_peer *peer = NULL;

	if (gH323Debug)
		ast_verbose("---   build_peer\n");

	peer = ast_calloc(1, sizeof(*peer));
	if (peer) {
		memset(peer, 0, sizeof(struct ooh323_peer));
		ast_mutex_init(&peer->lock);
		ast_copy_string(peer->name, name, sizeof(peer->name));
		peer->capability = gCapability;
		memcpy(&peer->prefs, &gPrefs, sizeof(struct ast_codec_pref));
		peer->rtptimeout = gRTPTimeout;
		ast_copy_string(peer->accountcode, gAccountcode, sizeof(peer->accountcode));
		peer->amaflags = gAMAFLAGS;
		peer->dtmfmode = gDTMFMode;
		if (0 == friend_type) {
			peer->mFriend = 1;
		}

		while (v) {
			if (!strcasecmp(v->name, "h323id")) {
				if (!(peer->h323id = ast_strdup(v->value))) {
					ast_log(LOG_ERROR, "Could not allocate memory for h323id of "
											 "peer %s\n", name);
					ooh323_delete_peer(peer);
					return NULL;
				}
			} else if (!strcasecmp(v->name, "e164")) {
				if (!(peer->e164 = ast_strdup(v->value))) {
					ast_log(LOG_ERROR, "Could not allocate memory for e164 of "
											 "peer %s\n", name);
					ooh323_delete_peer(peer);
					return NULL;
				}
			} else  if (!strcasecmp(v->name, "email")) {
				if (!(peer->email = ast_strdup(v->value))) {
					ast_log(LOG_ERROR, "Could not allocate memory for email of "
											 "peer %s\n", name);
					ooh323_delete_peer(peer);
					return NULL;
				}
			} else if (!strcasecmp(v->name, "url")) {
				if (!(peer->url = ast_strdup(v->value))) {
					ast_log(LOG_ERROR, "Could not allocate memory for h323id of "
											 "peer %s\n", name);
					ooh323_delete_peer(peer);
					return NULL;
				}
			} else if (!strcasecmp(v->name, "port")) {
				peer->port = atoi(v->value);
			} else if (!strcasecmp(v->name, "ip")) {
				ast_copy_string(peer->ip, v->value, sizeof(peer->ip));
			} else if (!strcasecmp(v->name, "outgoinglimit")) {
				if ((peer->outgoinglimit = atoi(v->value)) < 0) {
					peer->outgoinglimit = 0;
				}
			} else if (!strcasecmp(v->name, "accountcode")) {
				ast_copy_string(peer->accountcode, v->value, sizeof(peer->accountcode));
			} else if (!strcasecmp(v->name, "rtptimeout")) {
				if ((peer->rtptimeout = atoi(v->value)) < 0) {
					peer->rtptimeout = gRTPTimeout;
				}
			} else if (!strcasecmp(v->name, "disallow")) {
				ast_parse_allow_disallow(&peer->prefs, &peer->capability, 
												 v->value, 0); 
			} else if (!strcasecmp(v->name, "allow")) {
				const char* tcodecs = v->value;
				if (!strcasecmp(v->value, "all")) {
					tcodecs = "ulaw,alaw,g729,g723,gsm";
				}
				ast_parse_allow_disallow(&peer->prefs, &peer->capability, 
												 tcodecs, 1);				 
			} else if (!strcasecmp(v->name,  "amaflags")) {
				peer->amaflags = ast_cdr_amaflags2int(v->value);
			} else if (!strcasecmp(v->name, "dtmfmode")) {
				if (!strcasecmp(v->value, "rfc2833"))
					peer->dtmfmode = H323_DTMF_RFC2833;
				else if (!strcasecmp(v->value, "q931keypad"))
					peer->dtmfmode = H323_DTMF_Q931;
				else if (!strcasecmp(v->value, "h245alphanumeric"))
					peer->dtmfmode = H323_DTMF_H245ALPHANUMERIC;
				else if (!strcasecmp(v->value, "h245signal"))
					peer->dtmfmode = H323_DTMF_H245SIGNAL;
			}
			v = v->next;
		}
	}

	if (gH323Debug)
		ast_verbose("+++   build_peer\n");

	return peer;
}

static int ooh323_do_reload(void)
{
	if (gH323Debug) {
		ast_verbose("---   ooh323_do_reload\n");
	}

	reload_config(1);

	if (gH323Debug) {
		ast_verbose("+++   ooh323_do_reload\n");
	}

	return 0;
}

#if 0
/*--- h323_reload: Force reload of module from cli ---*/
static int ooh323_reload(int fd, int argc, char *argv[])
{

	if (gH323Debug)
		ast_verbose("---   ooh323_reload\n");

	ast_mutex_lock(&h323_reload_lock);
	if (h323_reloading) {
		ast_verbose("Previous OOH323 reload not yet done\n");
	} 
	else {
		h323_reloading = 1;
	}
	ast_mutex_unlock(&h323_reload_lock);
	restart_monitor();

	if (gH323Debug)
		ast_verbose("+++   ooh323_reload\n");

	return 0;
}
#endif

#if 0
static int reload(void *mod)
{
	return ooh323_reload(0, 0, NULL);
}
#endif

int reload_config(int reload)
{
	int format;
	struct ooAliases  *pNewAlias = NULL;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ooh323_user *user = NULL;
	struct ooh323_peer *peer = NULL;
	char *cat;
	const char *utype;

	if (gH323Debug)
		ast_verbose("---   reload_config\n");

	cfg = ast_config_load(config, config_flags);
	if (!cfg) {
		cfg = ast_config_load(config_old, config_flags);
	}

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, OOH323 disabled\n", config);
		return 1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return RESULT_SUCCESS;

	if (reload) {
		delete_users();
		delete_peers();
	}

	/* Inintialize everything to default */
	strcpy(gLogFile, DEFAULT_LOGFILE);
	gPort = 1720;
	gIP[0] = '\0';
	strcpy(gCallerID, DEFAULT_H323ID);
	gCapability = AST_FORMAT_ULAW;
	memset(&gPrefs, 0, sizeof(struct ast_codec_pref));
	gDTMFMode = H323_DTMF_RFC2833;
	gRasGkMode = RasNoGatekeeper;
	gGatekeeper[0] = '\0';
	gRTPTimeout = 60;
	strcpy(gAccountcode, DEFAULT_H323ACCNT);
	gFastStart = 1;
	gTunneling = 1;
	gTOS = 0;
	strcpy(gContext, DEFAULT_CONTEXT);
	gAliasList = NULL;
	gMediaWaitForConnect = 0;
	ooconfig.mTCPPortStart = 12030;
	ooconfig.mTCPPortEnd = 12230;

	v = ast_variable_browse(cfg, "general");
	while (v) {
	
		if (!strcasecmp(v->name, "port")) {
			gPort = (int)strtol(v->value, NULL, 10);
		} else if (!strcasecmp(v->name, "bindaddr")) {
			ast_copy_string(gIP, v->value, sizeof(gIP));
		} else if (!strcasecmp(v->name, "h225portrange")) {
			char* endlimit = 0;
			char temp[256];
			ast_copy_string(temp, v->value, sizeof(temp));
			endlimit = strchr(temp, ',');
			if (endlimit) {
				*endlimit = '\0';
				endlimit++;
				ooconfig.mTCPPortStart = atoi(temp);
				ooconfig.mTCPPortEnd = atoi(endlimit);

				if (ooH323EpSetTCPPortRange(ooconfig.mTCPPortStart, 
													ooconfig.mTCPPortEnd) == OO_FAILED) {
					ast_log(LOG_ERROR, "h225portrange: Failed to set range\n");
				}
			} else {
				ast_log(LOG_ERROR, "h225portrange: Invalid format, separate port range with \",\"\n");
			}
		} else if (!strcasecmp(v->name, "gateway")) {
			gIsGateway = ast_true(v->value);
		} else if (!strcasecmp(v->name, "faststart")) {
			gFastStart = ast_true(v->value);
			if (gFastStart)
				ooH323EpEnableFastStart();
			else
				ooH323EpDisableFastStart();
		} else if (!strcasecmp(v->name, "mediawaitforconnect")) {
			gMediaWaitForConnect = ast_true(v->value);
			if (gMediaWaitForConnect)
				ooH323EpEnableMediaWaitForConnect();
			else 
				ooH323EpDisableMediaWaitForConnect();
		} else if (!strcasecmp(v->name, "h245tunneling")) {
			gTunneling = ast_true(v->value);
			if (gTunneling)
				ooH323EpEnableH245Tunneling();
			else
				ooH323EpDisableH245Tunneling();
		} else if (!strcasecmp(v->name, "h323id")) {
			pNewAlias = malloc(sizeof(*pNewAlias));
			if (!pNewAlias) {
				ast_log(LOG_ERROR, "Failed to allocate memory for h323id alias\n");
				return 1;
			}
			pNewAlias->type =  T_H225AliasAddress_h323_ID;
			pNewAlias->value = strdup(v->value);
			pNewAlias->next = gAliasList;
			gAliasList = pNewAlias;
			pNewAlias = NULL;
		} else if (!strcasecmp(v->name, "e164")) {
			pNewAlias = malloc(sizeof(*pNewAlias));
			if (!pNewAlias) {
				ast_log(LOG_ERROR, "Failed to allocate memory for e164 alias\n");
				return 1;
			}
			pNewAlias->type =  T_H225AliasAddress_dialedDigits;
			pNewAlias->value = strdup(v->value);
			pNewAlias->next = gAliasList;
			gAliasList = pNewAlias;
			pNewAlias = NULL;
		} else if (!strcasecmp(v->name, "email")) {
			pNewAlias = malloc(sizeof(*pNewAlias));
			if (!pNewAlias) {
				ast_log(LOG_ERROR, "Failed to allocate memory for email alias\n");
				return 1;
			}
			pNewAlias->type =  T_H225AliasAddress_email_ID;
			pNewAlias->value = strdup(v->value);
			pNewAlias->next = gAliasList;
			gAliasList = pNewAlias;
			pNewAlias = NULL;
		} else if (!strcasecmp(v->name, "callerid")) {
			ast_copy_string(gCallerID, v->value, sizeof(gCallerID));
		} else if (!strcasecmp(v->name, "incominglimit")) {
			gIncomingLimit = atoi(v->value);
		} else if (!strcasecmp(v->name, "outgoinglimit")) {
			gOutgoingLimit = atoi(v->value);
		} else if (!strcasecmp(v->name, "gatekeeper")) {
			if (!strcasecmp(v->value, "DISABLE")) {
				gRasGkMode = RasNoGatekeeper;
			} else if (!strcasecmp(v->value, "DISCOVER")) {
				gRasGkMode = RasDiscoverGatekeeper;
			} else {
				gRasGkMode = RasUseSpecificGatekeeper;
				ast_copy_string(gGatekeeper, v->value, sizeof(gGatekeeper));
			}
		} else if (!strcasecmp(v->name, "logfile")) {
			ast_copy_string(gLogFile, v->value, sizeof(gLogFile));
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(gContext, v->value, sizeof(gContext));
			ast_verb(3, "  == Setting default context to %s\n", gContext);
		} else if (!strcasecmp(v->name, "rtptimeout")) {
			gRTPTimeout = atoi(v->value);
			if (gRTPTimeout <= 0)
				gRTPTimeout = 60;
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%i", &format) == 1)
				gTOS = format & 0xff;
			else if (!strcasecmp(v->value, "lowdelay"))
				gTOS = IPTOS_LOWDELAY;
			else if (!strcasecmp(v->value, "throughput"))
				gTOS = IPTOS_THROUGHPUT;
			else if (!strcasecmp(v->value, "reliability"))
				gTOS = IPTOS_RELIABILITY;
			else if (!strcasecmp(v->value, "mincost"))
				gTOS = IPTOS_MINCOST;
			else if (!strcasecmp(v->value, "none"))
				gTOS = 0;
			else
				ast_log(LOG_WARNING, "Invalid tos value at line %d, should be "
											"'lowdelay', 'throughput', 'reliability', "
											"'mincost', or 'none'\n", v->lineno);
		} else if (!strcasecmp(v->name, "amaflags")) {
			gAMAFLAGS = ast_cdr_amaflags2int(v->value);
		} else if (!strcasecmp(v->name, "accountcode")) {
			ast_copy_string(gAccountcode, v->value, sizeof(gAccountcode)-1);
		} else if (!strcasecmp(v->name, "disallow")) {
			ast_parse_allow_disallow(&gPrefs, &gCapability, v->value, 0);
		} else if (!strcasecmp(v->name, "allow")) {
			const char* tcodecs = v->value;
			if (!strcasecmp(v->value, "all")) {
				tcodecs = "ulaw,alaw,g729,g723,gsm";
			}
			ast_parse_allow_disallow(&gPrefs, &gCapability, tcodecs, 1);
		} else if (!strcasecmp(v->name, "dtmfmode")) {
			if (!strcasecmp(v->value, "inband"))
				gDTMFMode = H323_DTMF_INBAND;
			else if (!strcasecmp(v->value, "rfc2833"))
				gDTMFMode = H323_DTMF_RFC2833;
			else if (!strcasecmp(v->value, "q931keypad"))
				gDTMFMode = H323_DTMF_Q931;
			else if (!strcasecmp(v->value, "h245alphanumeric"))
				gDTMFMode = H323_DTMF_H245ALPHANUMERIC;
			else if (!strcasecmp(v->value, "h245signal"))
				gDTMFMode = H323_DTMF_H245SIGNAL;
			else {
				ast_log(LOG_WARNING, "Unknown dtmf mode '%s', using rfc2833\n", v->value);
				gDTMFMode = H323_DTMF_RFC2833;
			}
		}  
		v = v->next;
	}
	
	for (cat = ast_category_browse(cfg, NULL); cat; cat = ast_category_browse(cfg, cat)) {
		if (strcasecmp(cat, "general")) {
			int friend_type = 0;
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (utype) {
				friend_type = strcasecmp(utype, "friend");
				if (!strcmp(utype, "user") || 0 == friend_type) {
					user = build_user(cat, ast_variable_browse(cfg, cat));
					if (user) {
						ast_mutex_lock(&userl.lock);
						user->next = userl.users;
						userl.users = user;
						ast_mutex_unlock(&userl.lock);
					} else {
						ast_log(LOG_WARNING, "Failed to build user %s\n", cat);
					}
				}
				if (!strcasecmp(utype, "peer") || 0 == friend_type) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat), friend_type);
					if (peer) {
						ast_mutex_lock(&peerl.lock);
						peer->next = peerl.peers;
						peerl.peers = peer;
						ast_mutex_unlock(&peerl.lock);
					} else {
						ast_log(LOG_WARNING, "Failed to build peer %s\n", cat);
					}
				}
			}
		}
	}
	ast_config_destroy(cfg);


	/* Determine ip address if neccessary */
	if (ast_strlen_zero(gIP)) {
		ooGetLocalIPAddress(gIP);
		if (!strcmp(gIP, "127.0.0.1")) {
			ast_log(LOG_NOTICE, "Failed to determine local ip address. Please "
									 "specify it in ooh323.conf. OOH323 Disabled\n");
			return 1;
		}
	}

	if (gH323Debug)
		ast_verbose("+++   reload_config\n");

	return 0;
}

static char *handle_cli_ooh323_show_peer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char ip_port[30];
	struct ooh323_peer *prev = NULL, *peer = NULL;
	
	switch (cmd) {
	case CLI_INIT:
		e->command = "ooh323 show peer";
		e->usage =
			"Usage: ooh323 show peer <name>\n"
			"		 List details of specific OOH323 peer.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&peerl.lock);
	peer = peerl.peers;
	while (peer) {
		ast_mutex_lock(&peer->lock);
		if (!strcmp(peer->name, a->argv[3]))
			break;
		else {
			prev = peer;
			peer = peer->next;
			ast_mutex_unlock(&prev->lock);
		}
	}

	if (peer) {
		snprintf(ip_port, sizeof(ip_port), "%s:%d", peer->ip, peer->port);
		ast_cli(a->fd, "%-15.15s%s\n", "Name: ", peer->name);
		ast_cli(a->fd, "%-15.15s%s", "Format Prefs: ", "(");
		print_codec_to_cli(a->fd, &peer->prefs);
		ast_cli(a->fd, ")\n");
		ast_cli(a->fd, "%-15.15s", "DTMF Mode: ");
		if (peer->dtmfmode & H323_DTMF_RFC2833)
			ast_cli(a->fd, "%s\n", "rfc2833");
		else if (peer->dtmfmode & H323_DTMF_Q931)
			ast_cli(a->fd, "%s\n", "q931keypad");
		else if (peer->dtmfmode & H323_DTMF_H245ALPHANUMERIC)
			ast_cli(a->fd, "%s\n", "h245alphanumeric");
		else if (peer->dtmfmode & H323_DTMF_H245SIGNAL)
			ast_cli(a->fd, "%s\n", "h245signal");
		else
			ast_cli(a->fd, "%s\n", "unknown");
		ast_cli(a->fd, "%-15.15s%s\n", "AccountCode: ", peer->accountcode);
		ast_cli(a->fd, "%-15.15s%s\n", "AMA flags: ",
		ast_cdr_flags2str(peer->amaflags));
		ast_cli(a->fd, "%-15.15s%s\n", "Ip:Port: ", ip_port);
		ast_cli(a->fd, "%-15.15s%d\n", "OutgoingLimit: ", peer->outgoinglimit);
		ast_cli(a->fd, "%-15.15s%d\n", "rtptimeout: ", peer->rtptimeout);
		ast_mutex_unlock(&peer->lock);
	} else {
		ast_cli(a->fd, "Peer %s not found\n", a->argv[3]);
		ast_cli(a->fd, "\n");
	}
	ast_mutex_unlock(&peerl.lock);

	return CLI_SUCCESS;
}

static char *handle_cli_ooh323_show_peers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char ip_port[30];
	char formats[512];
	struct ooh323_peer *prev = NULL, *peer = NULL;

#define FORMAT  "%-15.15s  %-15.15s  %-23.23s  %-s\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "ooh323 show peers";
		e->usage =
			"Usage: ooh323 show peers\n"
			"		 Lists all known OOH323 peers.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, FORMAT, "Name", "Accountcode", "ip:port", "Formats");

	ast_mutex_lock(&peerl.lock);
	peer = peerl.peers;
	while (peer) {
		ast_mutex_lock(&peer->lock);
		snprintf(ip_port, sizeof(ip_port), "%s:%d", peer->ip, peer->port);
		ast_cli(a->fd, FORMAT, peer->name,
					peer->accountcode,
					ip_port,
					ast_getformatname_multiple(formats, sizeof(formats), peer->capability));
		prev = peer;
		peer = peer->next;
		ast_mutex_unlock(&prev->lock);
	}
	ast_mutex_unlock(&peerl.lock);

#undef FORMAT

	return CLI_SUCCESS;
}

/*! \brief Print codec list from preference to CLI/manager */
static void print_codec_to_cli(int fd, struct ast_codec_pref *pref)
{
	int x, codec;

	for (x = 0; x < 32; x++) {
		codec = ast_codec_pref_index(pref, x);
		if (!codec)
			break;
		ast_cli(fd, "%s", ast_getformatname(codec));
		ast_cli(fd, ":%d", pref->framing[x]);
		if (x < 31 && ast_codec_pref_index(pref, x + 1))
			ast_cli(fd, ",");
	}
	if (!x)
		ast_cli(fd, "none");
}

static char *handle_cli_ooh323_show_user(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ooh323_user *prev = NULL, *user = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ooh323 show user";
		e->usage =
			"Usage: ooh323 show user <name>\n"
			"		 List details of specific OOH323 user.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&userl.lock);
	user = userl.users;
	while (user) {
		ast_mutex_lock(&user->lock);
		if (!strcmp(user->name, a->argv[3]))
			break;
		else {
			prev = user;
			user = user->next;
			ast_mutex_unlock(&prev->lock);
		}
	}

	if (user) {
		ast_cli(a->fd, "%-15.15s%s\n", "Name: ", user->name);
		ast_cli(a->fd, "%-15.15s%s", "Format Prefs: ", "(");
		print_codec_to_cli(a->fd, &user->prefs);
		ast_cli(a->fd, ")\n");
		ast_cli(a->fd, "%-15.15s", "DTMF Mode: ");
		if (user->dtmfmode & H323_DTMF_RFC2833)
			ast_cli(a->fd, "%s\n", "rfc2833");
		else if (user->dtmfmode & H323_DTMF_Q931)
			ast_cli(a->fd, "%s\n", "q931keypad");
		else if (user->dtmfmode & H323_DTMF_H245ALPHANUMERIC)
			ast_cli(a->fd, "%s\n", "h245alphanumeric");
		else if (user->dtmfmode & H323_DTMF_H245SIGNAL)
			ast_cli(a->fd, "%s\n", "h245signal");
		else
			ast_cli(a->fd, "%s\n", "unknown");
		ast_cli(a->fd, "%-15.15s%s\n", "AccountCode: ", user->accountcode);
		ast_cli(a->fd, "%-15.15s%s\n", "AMA flags: ", ast_cdr_flags2str(user->amaflags));
		ast_cli(a->fd, "%-15.15s%s\n", "Context: ", user->context);
		ast_cli(a->fd, "%-15.15s%d\n", "IncomingLimit: ", user->incominglimit);
		ast_cli(a->fd, "%-15.15s%d\n", "rtptimeout: ", user->rtptimeout);
		ast_mutex_unlock(&user->lock);
	} else {
		ast_cli(a->fd, "User %s not found\n", a->argv[3]);
		ast_cli(a->fd, "\n");
	}
	ast_mutex_unlock(&userl.lock);

	return CLI_SUCCESS;
}

static char *handle_cli_ooh323_show_users(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char formats[512];
	struct ooh323_user *prev = NULL, *user = NULL;

#define FORMAT1  "%-15.15s  %-15.15s  %-15.15s  %-s\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "ooh323 show users";
		e->usage =
			"Usage: ooh323 show users \n"
			"		 Lists all known OOH323 users.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, FORMAT1, "Username", "Accountcode", "Context", "Formats");

	ast_mutex_lock(&userl.lock);
	user = userl.users;
	while (user) {
		ast_mutex_lock(&user->lock);
		ast_cli(a->fd, FORMAT1, user->name,
					user->accountcode, user->context,
					ast_getformatname_multiple(formats, 512, user->capability));
		prev = user;
		user = user->next;
		ast_mutex_unlock(&prev->lock);
	}
	ast_mutex_unlock(&userl.lock);

#undef FORMAT1

	return CLI_SUCCESS;
}

static char *handle_cli_ooh323_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "ooh323 set debug [off]";
		e->usage =
			"Usage: ooh323 set debug [off]\n"
			"		 Enables/Disables debugging of OOH323 channel driver\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 3 || a->argc > 4)
		return CLI_SHOWUSAGE;
	if (a->argc == 4 && strcasecmp(a->argv[3], "off"))
		return CLI_SHOWUSAGE;

	gH323Debug = (a->argc == 4) ? FALSE : TRUE;
	ast_cli(a->fd, "OOH323 Debugging %s\n", gH323Debug ? "Enabled" : "Disabled");

	return CLI_SUCCESS;
}

#if 0
static int ooh323_show_channels(int fd, int argc, char *argv[])
{
	return RESULT_SUCCESS;
}
#endif

static char *handle_cli_ooh323_show_config(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char value[512];
	ooAliases *pAlias = NULL, *pAliasNext = NULL;;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ooh323 show config";
		e->usage =
			"Usage: ooh323 show config\n"
			"		 Shows global configuration of H.323 channel driver\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	snprintf(value, sizeof(value), "%s:%d", gIP, gPort);
	ast_cli(a->fd, "\nObjective Open H.323 Channel Driver's Config:\n");
	ast_cli(a->fd, "%-20s%s\n", "IP:Port: ", value);
	ast_cli(a->fd, "%-20s%s\n", "FastStart", gFastStart?"yes":"no");
	ast_cli(a->fd, "%-20s%s\n", "Tunneling", gTunneling?"yes":"no");
	ast_cli(a->fd, "%-20s%s\n", "CallerId", gCallerID);
	ast_cli(a->fd, "%-20s%s\n", "MediaWaitForConnect", gMediaWaitForConnect ? "yes" : "no");

#if 0
	{
		extern OOH323EndPoint gH323ep;

		ast_cli(a->fd, "%-20s%s\n", "FASTSTART",
			(OO_TESTFLAG(gH323ep.flags, OO_M_FASTSTART) != 0) ? "yes" : "no");
		ast_cli(a->fd, "%-20s%s\n", "TUNNELING",
			(OO_TESTFLAG(gH323ep.flags, OO_M_TUNNELING) != 0) ? "yes" : "no");
		ast_cli(a->fd, "%-20s%s\n", "MEDIAWAITFORCONN",
			(OO_TESTFLAG(gH323ep.flags, OO_M_MEDIAWAITFORCONN) != 0) ? "yes" : "no");
	}
#endif

	if (gRasGkMode == RasNoGatekeeper)
		snprintf(value, sizeof(value), "%s", "No Gatekeeper");
	else if (gRasGkMode == RasDiscoverGatekeeper)
		snprintf(value, sizeof(value), "%s", "Discover");
	else
		snprintf(value, sizeof(value), "%s", gGatekeeper);

	ast_cli(a->fd, "%-20s%s\n", "Gatekeeper:", value);
	ast_cli(a->fd, "%-20s%s\n", "H.323 LogFile:", gLogFile);
	ast_cli(a->fd, "%-20s%s\n", "Context:", gContext);
	ast_cli(a->fd, "%-20s%s\n", "Capability:", ast_getformatname_multiple(value, sizeof(value), gCapability));
	ast_cli(a->fd, "%-20s", "DTMF Mode: ");
	if (gDTMFMode & H323_DTMF_RFC2833)
		ast_cli(a->fd, "%s\n", "rfc2833");
	else if (gDTMFMode & H323_DTMF_Q931)
		ast_cli(a->fd, "%s\n", "q931keypad");
	else if (gDTMFMode & H323_DTMF_H245ALPHANUMERIC)
		ast_cli(a->fd, "%s\n", "h245alphanumeric");
	else if (gDTMFMode & H323_DTMF_H245SIGNAL)
		ast_cli(a->fd, "%s\n", "h245signal");
	else
		ast_cli(a->fd, "%s\n", "unknown");
	ast_cli(a->fd, "%-20s%s\n", "AccountCode: ", gAccountcode);
	ast_cli(a->fd, "%-20s%s\n", "AMA flags: ", ast_cdr_flags2str(gAMAFLAGS));

	pAlias = gAliasList;
	if (pAlias)
		ast_cli(a->fd, "%-20s\n", "Aliases: ");
	while (pAlias) {
		pAliasNext = pAlias->next;
		if (pAliasNext) {
			ast_cli(a->fd, "\t%-30s\t%-30s\n", pAlias->value, pAliasNext->value);
			pAlias = pAliasNext->next;
		} else {
			ast_cli(a->fd, "\t%-30s\n", pAlias->value);
			pAlias = pAlias->next;
		}
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_ooh323[] = {
	AST_CLI_DEFINE(handle_cli_ooh323_set_debug,	"Enable/Disable OOH323 debugging"),
	AST_CLI_DEFINE(handle_cli_ooh323_show_config, "Show details on global configuration of H.323 channel driver"),
	AST_CLI_DEFINE(handle_cli_ooh323_show_peer,	"Show details on specific OOH323 peer"),
	AST_CLI_DEFINE(handle_cli_ooh323_show_peers,  "Show defined OOH323 peers"),
	AST_CLI_DEFINE(handle_cli_ooh323_show_user,	"Show details on specific OOH323 user"),
	AST_CLI_DEFINE(handle_cli_ooh323_show_users,  "Show defined OOH323 users"),
};

static int load_module(void)
{
	int res;
	struct ooAliases * pNewAlias = NULL;
	struct ooh323_peer *peer = NULL;
	OOH225MsgCallbacks h225Callbacks = {0, 0, 0, 0};

	OOH323CALLBACKS h323Callbacks = {
		.onNewCallCreated = onNewCallCreated,
		.onAlerting = onAlerting,
		.onIncomingCall = NULL,
		.onOutgoingCall = NULL,
		.onCallEstablished = onCallEstablished,
		.onCallCleared = onCallCleared,
		.openLogicalChannels = NULL,
		.onReceivedDTMF = &ooh323_onReceivedDigit
	};

	ast_log(LOG_NOTICE, 
		"---------------------------------------------------------------------------------\n"
		"---  ******* IMPORTANT NOTE ***********\n"
		"---\n"
		"---  This module is currently unsupported.  Use it at your own risk.\n"
		"---\n"
		"---------------------------------------------------------------------------------\n");

	h225Callbacks.onReceivedSetup = &ooh323_onReceivedSetup;

	userl.users = NULL;
	ast_mutex_init(&userl.lock);
	peerl.peers = NULL;
	ast_mutex_init(&peerl.lock);
 
#if 0		
	ast_register_atexit(&ast_ooh323c_exit);
#endif

	if (!(sched = sched_context_create())) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
	}
	if (!(io = io_context_create())) {
		ast_log(LOG_WARNING, "Unable to create I/O context\n");
	}


	if (!(res = reload_config(0))) {
		/* Make sure we can register our OOH323 channel type */
		if (ast_channel_register(&ooh323_tech)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
			return 0;
		}
		ast_rtp_proto_register(&ooh323_rtp);
		ast_cli_register_multiple(cli_ooh323, sizeof(cli_ooh323) / sizeof(struct ast_cli_entry));

		 /* fire up the H.323 Endpoint */		 
		if (OO_OK != ooH323EpInitialize(OO_CALLMODE_AUDIOCALL, gLogFile)) {
			ast_log(LOG_ERROR, "Failed to initialize OOH323 endpoint-OOH323 Disabled\n");
			return 1;
		}

		if (gIsGateway)
			ooH323EpSetAsGateway();

		ooH323EpDisableAutoAnswer();
		ooH323EpSetH225MsgCallbacks(h225Callbacks);
		ooH323EpSetTraceLevel(OOTRCLVLDBGC);
		ooH323EpSetLocalAddress(gIP, gPort);
		ooH323EpSetCallerID(gCallerID);
 
		/* Set aliases if any */
		for (pNewAlias = gAliasList; pNewAlias; pNewAlias = pNewAlias->next) {
			switch (pNewAlias->type) {
			case T_H225AliasAddress_h323_ID:
				ooH323EpAddAliasH323ID(pNewAlias->value);
				break;
			case T_H225AliasAddress_dialedDigits:	
				ooH323EpAddAliasDialedDigits(pNewAlias->value);
				break;
			case T_H225AliasAddress_email_ID:	
				ooH323EpAddAliasEmailID(pNewAlias->value);
				break;
			}
		}

		ast_mutex_lock(&peerl.lock);
		peer = peerl.peers;
		while (peer) {
			if (peer->h323id)
				ooH323EpAddAliasH323ID(peer->h323id);
			if (peer->email)
				ooH323EpAddAliasEmailID(peer->email);
			if (peer->e164)
				ooH323EpAddAliasDialedDigits(peer->e164);
			if (peer->url)
				ooH323EpAddAliasURLID(peer->url);
			peer = peer->next;
		}
		ast_mutex_unlock(&peerl.lock);
	

		if (gMediaWaitForConnect)
			ooH323EpEnableMediaWaitForConnect();
		else 
			ooH323EpDisableMediaWaitForConnect();

		/* Fast start and tunneling options */
		if (gFastStart)
			ooH323EpEnableFastStart();
		else
			ooH323EpDisableFastStart();

		if (!gTunneling)
			ooH323EpDisableH245Tunneling();

		/* Gatekeeper */
		if (gRasGkMode == RasUseSpecificGatekeeper)
			ooGkClientInit(gRasGkMode, gGatekeeper, 0);
		else if (gRasGkMode == RasDiscoverGatekeeper)
			ooGkClientInit(gRasGkMode, 0, 0);

		/* Register callbacks */
		ooH323EpSetH323Callbacks(h323Callbacks);

		/* Add endpoint capabilities */
		if (ooh323c_set_capability(&gPrefs, gCapability, gDTMFMode) < 0) {
			ast_log(LOG_ERROR, "Capabilities failure for OOH323. OOH323 Disabled.\n");
			return 1;
		}
  

		/* Create H.323 listener */
		if (ooCreateH323Listener() != OO_OK) {
			ast_log(LOG_ERROR, "OOH323 Listener Creation failure. OOH323 DISABLED\n");
		
			ooH323EpDestroy();
			return 1;
		}

		if (ooh323c_start_stack_thread() < 0) {
			ast_log(LOG_ERROR, "Failed to start OOH323 stack thread. OOH323 DISABLED\n");
			ooH323EpDestroy();
			return 1;
		}
		/* And start the monitor for the first time */
		restart_monitor();
	}

	return 0;
}


static void *do_monitor(void *data)
{
	int res;
	int reloading;
	struct ooh323_pvt *h323 = NULL;
	time_t t;

	for (;;) {
		struct ooh323_pvt *h323_next;
		/* Check for a reload request */
		ast_mutex_lock(&h323_reload_lock);
		reloading = h323_reloading;
		h323_reloading = 0;
		ast_mutex_unlock(&h323_reload_lock);
		if (reloading) {
			ast_verb(1, "Reloading H.323\n");
			ooh323_do_reload();
		}
		/* Check for interfaces needing to be killed */
		ast_mutex_lock(&iflock);
		time(&t);
		h323 = iflist;
		while (h323) {
			h323_next = h323->next;

			/* TODO: Need to add rtptimeout keepalive support */
			if (ast_test_flag(h323, H323_NEEDDESTROY)) {
				ooh323_destroy (h323);
			}
			h323 = h323_next;
		}
		ast_mutex_unlock(&iflock);
		pthread_testcancel();
		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		if ((res < 0) || (res > 1000)) {
			res = 1000;
		}
		res = ast_io_wait(io, res);
		pthread_testcancel();
		ast_mutex_lock(&monlock);
		if (res >= 0) {
			ast_sched_runq(sched);
		}
		ast_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
}

int restart_monitor(void)
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



int ooh323_destroy(struct ooh323_pvt *p)
{
	/* NOTE: Assumes iflock already acquired */
	struct ooh323_pvt *prev = NULL, *cur = NULL;


	if (gH323Debug) {
		ast_verbose("---   ooh323_destroy \n");

		if (p)
			ast_verbose(" Destroying %s\n", p->username);
	}

	cur = iflist;
	while (cur) {
		if (cur == p) { break; }
		prev = cur;
		cur = cur->next;
	}

	if (cur) {
		ast_mutex_lock(&cur->lock);
		if (prev)
			prev->next = cur->next;
		else
			iflist = cur->next;

		if (cur->callToken) {
			free(cur->callToken);
			cur->callToken = 0;
		}

		if (cur->username) {
			free(cur->username);
			cur->username = 0;
		}

		if (cur->host) {
			free(cur->host);
			cur->host = 0;
		}

		if (cur->callerid_name) {
			free(cur->callerid_name);
			cur->callerid_name = 0;
		}
		
		if (cur->callerid_num) {
			free(cur->callerid_num);
			cur->callerid_num = 0;
		}


		if (cur->rtp) {
			ast_rtp_destroy(cur->rtp);
			cur->rtp = 0;
		}
	
		/* Unlink us from the owner if we have one */
		if (cur->owner) {
			ast_channel_lock(cur->owner);
			ast_debug(1, "Detaching from %s\n", cur->owner->name);
			cur->owner->tech_pvt = NULL;
			ast_channel_unlock(cur->owner);
			cur->owner = NULL;
		}
  
		if (cur->vad) {
			ast_dsp_free(cur->vad);
			cur->vad = NULL;
		}
		ast_mutex_unlock(&cur->lock);
		ast_mutex_destroy(&cur->lock);

		free(cur);
	}

	if (gH323Debug)
		ast_verbose("+++   ooh323_destroy\n");

	return 0;
}

int delete_peers()
{
	struct ooh323_peer *cur = NULL, *prev = NULL;
	ast_mutex_lock(&peerl.lock);
	cur = peerl.peers;
	while (cur) {
		prev = cur;
		cur = cur->next;

		ast_mutex_destroy(&prev->lock);
		if (prev->h323id)
			free(prev->h323id);
		if (prev->email)
			free(prev->email);
		if (prev->url)
			free(prev->url);
		if (prev->e164)
			free(prev->e164);
		free(prev);

		if (cur == peerl.peers) {
			break;
		}
	}
	peerl.peers = NULL;
	ast_mutex_unlock(&peerl.lock);
	return 0;
}

int delete_users()
{
	struct ooh323_user *cur = NULL, *prev = NULL;
	ast_mutex_lock(&userl.lock);
	cur = userl.users;
	while (cur) {
		prev = cur;
		cur = cur->next;
		ast_mutex_destroy(&prev->lock);
		free(prev);
		if (cur == userl.users) {
			break;
		}
	}
	userl.users = NULL;
	ast_mutex_unlock(&userl.lock);
	return 0;
}
  
static int unload_module(void)
{
	struct ooh323_pvt *p;
	struct ooAliases *cur = NULL, *prev = NULL;

	if (gH323Debug) {
		ast_verbose("--- ooh323  unload_module \n");
	}
	/* First, take us out of the channel loop */
	ast_cli_unregister_multiple(cli_ooh323, sizeof(cli_ooh323) / sizeof(struct ast_cli_entry));
	ast_rtp_proto_unregister(&ooh323_rtp);
	ast_channel_unregister(&ooh323_tech);

#if 0
	ast_unregister_atexit(&ast_ooh323c_exit);
#endif

	if (gH323Debug) {
		ast_verbose("  unload_module - hanging up all interfaces\n");
	}
	if (!ast_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while (p) {
			if (p->owner) {
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			}
			p = p->next;
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}


	if (gH323Debug) {
		ast_verbose("  unload_module - stopping monitor thread\n");
	}  
	if (monitor_thread != AST_PTHREADT_NULL) {
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
	}


	if (gH323Debug) {
		ast_verbose("   unload_module - stopping stack thread\n");
	}
	ooh323c_stop_stack_thread();


	if (gH323Debug) {
		ast_verbose("   unload_module - freeing up memory used by interfaces\n");
	}
	if (!ast_mutex_lock(&iflock)) {
		struct ooh323_pvt *pl;

		/* Destroy all the interfaces and free their memory */
		p = iflist;
		while (p) {
			pl = p;
			p = p->next;
			/* Free associated memory */
			ooh323_destroy(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}
 

	if (gH323Debug) {
		ast_verbose("  unload_module - deleting users\n");
	}
	delete_users();


	if (gH323Debug) {
		ast_verbose("  unload_module - deleting peers\n");
	}
	delete_peers();


	if (gH323Debug) {
		ast_verbose("  unload_module - Freeing up alias list\n");
	}
	cur = gAliasList;
	while (cur) {
	  prev = cur;
	  cur = cur->next;
	  free(prev->value);
	  free(prev);
	}
	gAliasList = NULL;


	if (gH323Debug) {
		ast_verbose("	unload_module- destroying OOH323 endpoint \n");
	}
	ooH323EpDestroy();

	if (gH323Debug) {
		ast_verbose("+++ ooh323  unload_module \n");	
	}

	return 0;
}



static enum ast_rtp_get_result ooh323_get_rtp_peer(struct ast_channel *chan, struct ast_rtp **rtp)
{
	struct ooh323_pvt *p = NULL;
	enum ast_rtp_get_result res = AST_RTP_TRY_PARTIAL;

	if (!(p = (struct ooh323_pvt *) chan->tech_pvt))
	return AST_RTP_GET_FAILED;

	*rtp = p->rtp;

	if (!(p->rtp)) {
		return AST_RTP_GET_FAILED;
	}
	res = AST_RTP_TRY_NATIVE;

	return res;
}

static enum ast_rtp_get_result ooh323_get_vrtp_peer(struct ast_channel *chan, struct ast_rtp **rtp)
{
	struct ooh323_pvt *p = NULL;
	enum ast_rtp_get_result res = AST_RTP_TRY_PARTIAL;

	if (!(p = (struct ooh323_pvt *) chan->tech_pvt))
		return AST_RTP_GET_FAILED;

	*rtp = p->vrtp;

	if (!(p->rtp)) {
		return AST_RTP_GET_FAILED;
	}
	res = AST_RTP_TRY_NATIVE;

	return res;
}


int ooh323_update_capPrefsOrderForCall
	(ooCallData *call, struct ast_codec_pref *prefs)
{
	int i = 0;
	int codec = ast_codec_pref_index(prefs, i);

	ooResetCapPrefs(call);
	while (codec) {
		ooAppendCapToCapPrefs(call, ooh323_convertAsteriskCapToH323Cap(codec));
		codec = ast_codec_pref_index(prefs, ++i);
	}

	return 0;
}


int ooh323_convertAsteriskCapToH323Cap(int cap)
{
	char formats[512];
	switch (cap) {
	case AST_FORMAT_ULAW:
		return OO_G711ULAW64K;
	case AST_FORMAT_ALAW:
		return OO_G711ALAW64K;
	case AST_FORMAT_GSM:
		return OO_GSMFULLRATE;
	case AST_FORMAT_G729A:
		return OO_G729A;
	case AST_FORMAT_G723_1:
		return OO_G7231;
	case AST_FORMAT_H263:
		return OO_H263VIDEO;
	default:
		ast_log(LOG_NOTICE, "Don't know how to deal with mode %s\n", 
							ast_getformatname_multiple(formats, sizeof(formats), cap));
		return -1;
	}
}

static int ooh323_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp,
	 struct ast_rtp *vrtp, struct ast_rtp *trtp, int codecs, int nat_active)
{
	/* XXX Deal with Video */
	struct ooh323_pvt *p;
	struct sockaddr_in them;
	struct sockaddr_in us;
	int mode;

	if (gH323Debug)
		ast_verbose("---   ooh323_set_peer - %s\n", chan->name);

	if (!rtp) {
		return 0;
	}

	mode = ooh323_convertAsteriskCapToH323Cap(chan->writeformat); 
	p = (struct ooh323_pvt *) chan->tech_pvt;
	if (!p) {
		ast_log(LOG_ERROR, "No Private Structure, this is bad\n");
		return -1;
	}
	ast_rtp_get_peer(rtp, &them);
	ast_rtp_get_us(rtp, &us);
	return 0;
}




int configure_local_rtp(struct ooh323_pvt *p, ooCallData *call)
{
	struct sockaddr_in us;
	ooMediaInfo mediaInfo;
	int x, format = 0;	  

	if (gH323Debug)
		ast_verbose("---   configure_local_rtp\n");

	if (p->rtp) {
		ast_rtp_codec_setpref(p->rtp, &p->prefs);
	}

	/* figure out our local RTP port and tell the H.323 stack about it*/
	ast_rtp_get_us(p->rtp, &us);

	ast_copy_string(mediaInfo.lMediaIP, ast_inet_ntoa(us.sin_addr), sizeof(mediaInfo.lMediaIP));
	mediaInfo.lMediaPort = ntohs(us.sin_port);
	mediaInfo.lMediaCntrlPort = mediaInfo.lMediaPort +1;
	for (x = 0; 0 != (format = ast_codec_pref_index(&p->prefs, x)); x++) {
		strcpy(mediaInfo.dir, "transmit");
		mediaInfo.cap = ooh323_convertAsteriskCapToH323Cap(format);
		ooAddMediaInfo(call, mediaInfo);
		strcpy(mediaInfo.dir, "receive");
		ooAddMediaInfo(call, mediaInfo);
		if (mediaInfo.cap == OO_G729A) {
			strcpy(mediaInfo.dir, "transmit");
			mediaInfo.cap = OO_G729;
			ooAddMediaInfo(call, mediaInfo);
			strcpy(mediaInfo.dir, "receive");
			ooAddMediaInfo(call, mediaInfo);
		}
	}

	if (gH323Debug)
		ast_verbose("+++   configure_local_rtp\n");

	return 1;
}

void setup_rtp_connection(ooCallData *call, const char *remoteIp, 
								  int remotePort)
{
	struct ooh323_pvt *p = NULL;
	struct sockaddr_in them;

	if (gH323Debug)
		ast_verbose("---   setup_rtp_connection\n");

	/* Find the call or allocate a private structure if call not found */
	p = find_call(call); 

	if (!p) {
		ast_log(LOG_ERROR, "Something is wrong: rtp\n");
		return;
	}

	them.sin_family = AF_INET;
	them.sin_addr.s_addr = inet_addr(remoteIp); /* only works for IPv4 */
	them.sin_port = htons(remotePort);
	ast_rtp_set_peer(p->rtp, &them);

	if (gH323Debug) {
		ast_verbose("+++   setup_rtp_connection\n");
	}

	return;
}

void close_rtp_connection(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;

	if (gH323Debug) {
		ast_verbose("---   close_rtp_connection\n");
	}

	p = find_call(call);
	if (!p) {
		ast_log(LOG_ERROR, "Couldn't find matching call to close rtp connection\n");
		return;
	}
	ast_mutex_lock(&p->lock);
	if (p->rtp) {
		ast_rtp_stop(p->rtp);
	}
	ast_mutex_unlock(&p->lock);

	if (gH323Debug) {
		ast_verbose("+++   close_rtp_connection\n");
	}

	return;
}


int update_our_aliases(ooCallData *call, struct ooh323_pvt *p)
{
	int updated = -1;
	ooAliases *psAlias = NULL;
	
	if (!call->ourAliases)
		return updated;
	for (psAlias = call->ourAliases; psAlias; psAlias = psAlias->next) {
		if (psAlias->type == T_H225AliasAddress_h323_ID) {
			ast_copy_string(p->callee_h323id, psAlias->value, sizeof(p->callee_h323id));
			updated = 1;
		}
		if (psAlias->type == T_H225AliasAddress_dialedDigits) {
			ast_copy_string(p->callee_dialedDigits, psAlias->value, sizeof(p->callee_dialedDigits));
			updated = 1;
		}
		if (psAlias->type == T_H225AliasAddress_url_ID) {
			ast_copy_string(p->callee_url, psAlias->value, sizeof(p->callee_url));
			updated = 1;
		}
		if (psAlias->type == T_H225AliasAddress_email_ID) {
			ast_copy_string(p->callee_email, psAlias->value, sizeof(p->callee_email));
			updated = 1;
		}
	}
	return updated;
}

struct ast_frame *ooh323_rtp_read(struct ast_channel *ast, struct ooh323_pvt *p)
{
	/* Retrieve audio/etc from channel.  Assumes p->lock is already held. */
	struct ast_frame *f;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	switch (ast->fdno) {
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
	if (f && (f->frametype == AST_FRAME_DTMF) && !(p->dtmfmode & H323_DTMF_RFC2833)) {
		return &null_frame;
	}
	if (p->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass != p->owner->nativeformats) {
				ast_debug(1, "Oooh, format changed to %d\n", f->subclass);
				p->owner->nativeformats = f->subclass;
				ast_set_read_format(p->owner, p->owner->readformat);
				ast_set_write_format(p->owner, p->owner->writeformat);
			}
			if ((p->dtmfmode & H323_DTMF_INBAND) && p->vad) {
				f = ast_dsp_process(p->owner, p->vad, f);
				if (f && (f->frametype == AST_FRAME_DTMF)) {
					ast_debug(1, "* Detected inband DTMF '%c'\n", f->subclass);
				}
			}
		}
	}
	return f;
}


int ooh323_convert_hangupcause_asteriskToH323(int cause)
{
	switch (cause) {
	case AST_CAUSE_CALL_REJECTED:
		return OO_REASON_REMOTE_REJECTED;
	case AST_CAUSE_UNALLOCATED:
		return OO_REASON_NOUSER;
	case AST_CAUSE_BUSY:
		return OO_REASON_REMOTE_BUSY;
	case AST_CAUSE_BEARERCAPABILITY_NOTAVAIL:
		return OO_REASON_NOCOMMON_CAPABILITIES;
	case AST_CAUSE_CONGESTION:
		return OO_REASON_REMOTE_BUSY;
	case AST_CAUSE_NO_ANSWER:
		return OO_REASON_REMOTE_NOANSWER;
	case AST_CAUSE_NORMAL:
		return OO_REASON_REMOTE_CLEARED;
	case AST_CAUSE_FAILURE:
	default:
		return OO_REASON_UNKNOWN;
	}

	return 0;
}

int ooh323_convert_hangupcause_h323ToAsterisk(int cause)
{
	switch (cause) {
	case OO_REASON_REMOTE_REJECTED:
		return AST_CAUSE_CALL_REJECTED;
	case OO_REASON_NOUSER: 
		return AST_CAUSE_UNALLOCATED;
	case OO_REASON_REMOTE_BUSY:
	case OO_REASON_LOCAL_BUSY:
		return AST_CAUSE_BUSY;
	case OO_REASON_NOCOMMON_CAPABILITIES:	/* No codecs approved */
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	case OO_REASON_REMOTE_CONGESTED:
	case OO_REASON_LOCAL_CONGESTED:
		return AST_CAUSE_CONGESTION;
	case OO_REASON_REMOTE_NOANSWER:
		return AST_CAUSE_NO_ANSWER;
	case OO_REASON_UNKNOWN: 
	case OO_REASON_INVALIDMESSAGE:
	case OO_REASON_TRANSPORTFAILURE:
		return AST_CAUSE_FAILURE;
	case OO_REASON_REMOTE_CLEARED:
		return AST_CAUSE_NORMAL;
	default:
		return AST_CAUSE_NORMAL;
	}
	/* Never reached */
	return 0;
}

#if 0
void ast_ooh323c_exit()
{
	ooGkClientDestroy();
}
#endif

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Objective Systems H323 Channel");
