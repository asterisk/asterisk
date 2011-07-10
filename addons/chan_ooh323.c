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

/* Reworked version I, Nov-2009, by Alexandr Anikin, may@telecom-service.ru */


/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "chan_ooh323.h"
#include <math.h>

/*** DOCUMENTATION
	<function name="OOH323" language="en_US">
		<synopsis>
			Allow Setting / Reading OOH323 Settings
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<enumlist>
					<enum name="faxdetect">
						<para>Fax Detect [R/W]</para>
						<para>Returns 0 or 1</para>
						<para>Write yes or no</para>
					</enum>
				</enumlist>
				<enumlist>
					<enum name="t38support">
						<para>t38support [R/W]</para>
						<para>Returns 0 or 1</para>
						<para>Write yes or no</para>
					</enum>
				</enumlist>
				<enumlist>
					<enum name="h323id">
						<para>Returns h323id [R]</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Read and set channel parameters in the dialplan.
			<replaceable>name</replaceable> is one of the above only those with a [W] can be writen to.
			</para>
		</description>
	</function>
***/

#define FORMAT_STRING_SIZE	512

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
#define H323_NEEDSTART		(1<<8)

#define MAXT30	240
#define T38TOAUDIOTIMEOUT 30
#define T38_DISABLED 0
#define T38_ENABLED 1
#define T38_FAXGW 1

#define FAXDETECT_CNG	1
#define FAXDETECT_T38	2

/* Channel description */
static const char type[] = "OOH323";
static const char tdesc[] = "Objective Systems H323 Channel Driver";
static const char config[] = "ooh323.conf";

struct ast_module *myself;

static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = ""
};
static struct ast_jb_conf global_jbconf;

/* Channel Definition */
static struct ast_channel *ooh323_request(const char *type, struct ast_format_cap *cap,
			const struct ast_channel *requestor,  void *data, int *cause);
static int ooh323_digit_begin(struct ast_channel *ast, char digit);
static int ooh323_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int ooh323_call(struct ast_channel *ast, char *dest, int timeout);
static int ooh323_hangup(struct ast_channel *ast);
static int ooh323_answer(struct ast_channel *ast);
static struct ast_frame *ooh323_read(struct ast_channel *ast);
static int ooh323_write(struct ast_channel *ast, struct ast_frame *f);
static int ooh323_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int ooh323_queryoption(struct ast_channel *ast, int option, void *data, int *datalen);
static int ooh323_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static enum ast_rtp_glue_result ooh323_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **rtp);
static enum ast_rtp_glue_result ooh323_get_vrtp_peer(struct ast_channel *chan, struct ast_rtp_instance **rtp);
static int ooh323_set_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance *rtp, 
          struct ast_rtp_instance *vrtp, struct ast_rtp_instance *trtp, const struct ast_format_cap *codecs, int nat_active);

static struct ast_udptl *ooh323_get_udptl_peer(struct ast_channel *chan);
static int ooh323_set_udptl_peer(struct ast_channel *chan, struct ast_udptl *udptl);

static void print_codec_to_cli(int fd, struct ast_codec_pref *pref);

struct ooh323_peer *find_friend(const char *name, int port);


static struct ast_channel_tech ooh323_tech = {
	.type = type,
	.description = tdesc,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER,
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
	.queryoption = ooh323_queryoption,
	.bridge = ast_rtp_instance_bridge,		/* XXX chan unlocked ? */
	.early_bridge = ast_rtp_instance_early_bridge,

};

static struct ast_rtp_glue ooh323_rtp = {
	.type = type,
	.get_rtp_info = ooh323_get_rtp_peer,
	.get_vrtp_info = ooh323_get_vrtp_peer,
	.update_peer = ooh323_set_rtp_peer,
};

static struct ast_udptl_protocol ooh323_udptl = {
	type: "H323",
	get_udptl_info: ooh323_get_udptl_peer,
	set_udptl_peer: ooh323_set_udptl_peer,
};



struct ooh323_user;

/* H.323 channel private structure */
static struct ooh323_pvt {
	ast_mutex_t lock;		/* Channel private lock */
	struct ast_rtp_instance *rtp;
	struct ast_rtp_instance *vrtp; /* Placeholder for now */

	int t38support;			/* T.38 mode - disable, transparent, faxgw */
	int faxdetect;
	int faxdetected;
	int rtptimeout;
	struct ast_udptl *udptl;
	int faxmode;
	int t38_tx_enable;
	int t38_init;
	struct ast_sockaddr udptlredirip;
	time_t lastTxT38;
	int chmodepend;

	struct ast_channel *owner;	/* Master Channel */
   	union {
    		char  *user;	/* cooperating user/peer */
    		char  *peer;
   	} neighbor;
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
	struct ast_format readformat;   /* negotiated read format */
	struct ast_format writeformat;  /* negotiated write format */
	struct ast_format_cap *cap;
	struct ast_codec_pref prefs;
	int dtmfmode;
	int dtmfcodec;
	char exten[AST_MAX_EXTENSION];	/* Requested extension */
	char context[AST_MAX_EXTENSION];	/* Context where to start */
	char accountcode[256];	/* Account code */
	int nat;
	int amaflags;
	int progsent;			/* progress is sent */
	int alertsent;			/* alerting is sent */
	int g729onlyA;			/* G.729 only A */
	struct ast_dsp *vad;
	struct OOH323Regex *rtpmask;	/* rtp ip regexp */
	char rtpmaskstr[120];
	int rtdrcount, rtdrinterval;	/* roundtripdelayreq */
	int faststart, h245tunneling;	/* faststart & h245 tunneling */
	struct ooh323_pvt *next;	/* Next entity */
} *iflist = NULL;

/* Protect the channel/interface list (ooh323_pvt) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Profile of H.323 user registered with PBX*/
struct ooh323_user{
	ast_mutex_t lock;
	char		name[256];
	char		context[AST_MAX_EXTENSION];
	int		incominglimit;
	unsigned	inUse;
	char		accountcode[20];
	int		amaflags;
	struct ast_format_cap *cap;
	struct ast_codec_pref prefs;
	int		dtmfmode;
	int		dtmfcodec;
	int		faxdetect;
	int		t38support;
	int		rtptimeout;
	int		mUseIP;        /* Use IP address or H323-ID to search user */
	char		mIP[4*8+7+2];  /* Max for IPv6 - 2 brackets, 8 4hex, 7 - : */
	struct OOH323Regex *rtpmask;
	char		rtpmaskstr[120];
	int		rtdrcount, rtdrinterval;
	int		faststart, h245tunneling;
	int		g729onlyA;
	struct ooh323_user *next;
};

/* Profile of valid asterisk peers */
struct ooh323_peer{
	ast_mutex_t lock;
	char        name[256];
	unsigned    outgoinglimit;
	unsigned    outUse;
	struct ast_format_cap *cap;
	struct ast_codec_pref prefs;
	char        accountcode[20];
	int         amaflags;
	int         dtmfmode;
	int	    dtmfcodec;
	int	    faxdetect;
	int	    t38support;
	int         mFriend;    /* indicates defined as friend */
	char        ip[4*8+7+2]; /* Max for IPv6 - 2 brackets, 8 4hex, 7 - : */
	int         port;
	char        *h323id;    /* H323-ID alias, which asterisk will register with gk to reach this peer*/
	char        *email;     /* Email alias, which asterisk will register with gk to reach this peer*/
	char        *url;       /* url alias, which asterisk will register with gk to reach this peer*/
	char        *e164;      /* e164 alias, which asterisk will register with gk to reach this peer*/
	int         rtptimeout;
	struct OOH323Regex	    *rtpmask;
	char	    rtpmaskstr[120];
	int	    rtdrcount,rtdrinterval;
	int	    faststart, h245tunneling;
	int	    g729onlyA;
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

static long callnumber = 0;
AST_MUTEX_DEFINE_STATIC(ooh323c_cn_lock);

/* stack callbacks */
int onAlerting(ooCallData *call);
int onProgress(ooCallData *call);
int onNewCallCreated(ooCallData *call);
int onOutgoingCall(ooCallData *call);
int onCallEstablished(ooCallData *call);
int onCallCleared(ooCallData *call);
void onModeChanged(ooCallData *call, int t38mode);

static char gLogFile[256] = DEFAULT_LOGFILE;
static int  gPort = 1720;
static char gIP[2+8*4+7];	/* Max for IPv6 addr */
struct ast_sockaddr bindaddr;
int v6mode = 0;
static char gCallerID[AST_MAX_EXTENSION] = "";
static struct ooAliases *gAliasList;
static struct ast_format_cap *gCap;
static struct ast_codec_pref gPrefs;
static int  gDTMFMode = H323_DTMF_RFC2833;
static int  gDTMFCodec = 101;
static int  gFAXdetect = FAXDETECT_CNG;
static int  gT38Support = T38_FAXGW;
static char gGatekeeper[100];
static enum RasGatekeeperMode gRasGkMode = RasNoGatekeeper;

static int  gIsGateway = 0;
static int  gFastStart = 1;
static int  gTunneling = 1;
static int  gBeMaster = 0;
static int  gMediaWaitForConnect = 0;
static int  gTOS = 0;
static int  gRTPTimeout = 60;
static int  g729onlyA = 0;
static char gAccountcode[80] = DEFAULT_H323ACCNT;
static int  gAMAFLAGS;
static char gContext[AST_MAX_EXTENSION] = DEFAULT_CONTEXT;
static int  gIncomingLimit = 1024;
static int  gOutgoingLimit = 1024;
OOBOOL gH323Debug = FALSE;
static int gTRCLVL = OOTRCLVLERR;
static int gRTDRCount = 0, gRTDRInterval = 0;

static int t35countrycode = 0;
static int t35extensions = 0;
static int manufacturer = 0;
static char vendor[AST_MAX_EXTENSION] =  "";
static char version[AST_MAX_EXTENSION] = "";

static struct ooh323_config
{
   int  mTCPPortStart;
   int  mTCPPortEnd;
} ooconfig;

/** Asterisk RTP stuff*/
static struct ast_sched_context *sched;
static struct io_context *io;

/* Protect the monitoring thread, so only one process can kill or start it, 
   and not when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);


/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;


static struct ast_channel *ooh323_new(struct ooh323_pvt *i, int state,
                                             const char *host, struct ast_format_cap *cap, const char *linkedid)
{
	struct ast_channel *ch = NULL;
	struct ast_format tmpfmt;
	int features = 0;

	if (gH323Debug)
		ast_verbose("---   ooh323_new - %s\n", host);

	ast_format_clear(&tmpfmt);
	/* Don't hold a h323 pvt lock while we allocate a channel */
	ast_mutex_unlock(&i->lock);
   	ch = ast_channel_alloc(1, state, i->callerid_num, i->callerid_name, 
				i->accountcode, i->exten, i->context, linkedid, i->amaflags,
				"OOH323/%s-%ld", host, callnumber);
   	ast_mutex_lock(&ooh323c_cn_lock);
   	callnumber++;
   	ast_mutex_unlock(&ooh323c_cn_lock);
   
	ast_mutex_lock(&i->lock);

	if (ch) {
		ast_channel_lock(ch);
		ch->tech = &ooh323_tech;

		if (cap)
			ast_best_codec(cap, &tmpfmt);
		if (!tmpfmt.id)
			ast_codec_pref_index(&i->prefs, 0, &tmpfmt);

		ast_format_cap_add(ch->nativeformats, &tmpfmt);
		ast_format_copy(&ch->rawwriteformat, &tmpfmt);
		ast_format_copy(&ch->rawreadformat, &tmpfmt);

		ast_jb_configure(ch, &global_jbconf);

		if (state == AST_STATE_RING)
			ch->rings = 1;

		ch->adsicpe = AST_ADSI_UNAVAILABLE;
		ast_set_write_format(ch, &tmpfmt);
		ast_set_read_format(ch, &tmpfmt);
		ch->tech_pvt = i;
		i->owner = ch;
		ast_module_ref(myself);

		/* Allocate dsp for in-band DTMF support */
		if ((i->dtmfmode & H323_DTMF_INBAND) || (i->faxdetect & FAXDETECT_CNG)) {
			i->vad = ast_dsp_new();
		}

		/* inband DTMF*/
		if (i->dtmfmode & H323_DTMF_INBAND) {
			features |= DSP_FEATURE_DIGIT_DETECT;
			if (i->dtmfmode & H323_DTMF_INBANDRELAX) {
				ast_dsp_set_digitmode(i->vad, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
			}
		}

		/* fax detection*/
		if (i->faxdetect & FAXDETECT_CNG) {
			features |= DSP_FEATURE_FAX_DETECT;
			ast_dsp_set_faxmode(i->vad,
					DSP_FAXMODE_DETECT_CNG | DSP_FAXMODE_DETECT_CED);
		}

		if (features) {
			ast_dsp_set_features(i->vad, features);
		}

		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);

		/* Notify the module monitors that use count for resource has changed*/
		ast_update_use_count();

		ast_copy_string(ch->context, i->context, sizeof(ch->context));
		ast_copy_string(ch->exten, i->exten, sizeof(ch->exten));

		ch->priority = 1;

      		if(!ast_test_flag(i, H323_OUTGOING)) {
		
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

		manager_event(EVENT_FLAG_SYSTEM, "ChannelUpdate", "Channel: %s\r\nChanneltype: %s\r\n"
				"CallRef: %d\r\n", ch->name, "OOH323", i->call_reference);
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");


   	if(ch)   ast_channel_unlock(ch);

	if (gH323Debug)
		ast_verbose("+++   h323_new\n");

	return ch;
}



static struct ooh323_pvt *ooh323_alloc(int callref, char *callToken) 
{
	struct ooh323_pvt *pvt = NULL;

	if (gH323Debug)
		ast_verbose("---   ooh323_alloc\n");

	if (!(pvt = ast_calloc(1, sizeof(*pvt)))) {
		ast_log(LOG_ERROR, "Couldn't allocate private ooh323 structure\n");
		return NULL;
	}
	if (!(pvt->cap = ast_format_cap_alloc_nolock())) {
		ast_free(pvt);
		ast_log(LOG_ERROR, "Couldn't allocate private ooh323 structure\n");
		return NULL;
	}

	ast_mutex_init(&pvt->lock);
	ast_mutex_lock(&pvt->lock);

	pvt->faxmode = 0;
	pvt->chmodepend = 0;
	pvt->faxdetected = 0;
	pvt->faxdetect = gFAXdetect;
	pvt->t38support = gT38Support;
	pvt->rtptimeout = gRTPTimeout;
	pvt->rtdrinterval = gRTDRInterval;
	pvt->rtdrcount = gRTDRCount;
	pvt->g729onlyA = g729onlyA;

	pvt->call_reference = callref;
	if (callToken)
		pvt->callToken = strdup(callToken);

	/* whether to use gk for this call */
	if (gRasGkMode == RasNoGatekeeper)
		OO_SETFLAG(pvt->flags, H323_DISABLEGK);

	pvt->dtmfmode = gDTMFMode;
	pvt->dtmfcodec = gDTMFCodec;
	ast_copy_string(pvt->context, gContext, sizeof(pvt->context));
	ast_copy_string(pvt->accountcode, gAccountcode, sizeof(pvt->accountcode));

	pvt->amaflags = gAMAFLAGS;
	ast_format_cap_copy(pvt->cap, gCap);
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
static struct ast_channel *ooh323_request(const char *type, struct ast_format_cap *cap,
		const struct ast_channel *requestor, void *data, int *cause)

{
	struct ast_channel *chan = NULL;
	struct ooh323_pvt *p = NULL;
	struct ooh323_peer *peer = NULL;
	char *dest = NULL; 
	char *ext = NULL;
	char tmp[256];
	char formats[FORMAT_STRING_SIZE];
	int port = 0;

	if (gH323Debug)
		ast_verbose("---   ooh323_request - data %s format %s\n", (char*)data,  
										ast_getformatname_multiple(formats,FORMAT_STRING_SIZE,cap));

	if (!(ast_format_cap_has_type(cap, AST_FORMAT_TYPE_AUDIO))) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%s'\n", ast_getformatname_multiple(formats,FORMAT_STRING_SIZE,cap));
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


   	ast_copy_string(tmp, data, sizeof(tmp));

	dest = strchr(tmp, '/');

	if (dest) {  
		*dest = '\0';
		dest++;
		ext = dest;
		dest = tmp;
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
		ast_mutex_lock(&iflock);
		ast_mutex_unlock(&p->lock);
		ooh323_destroy(p);
		ast_mutex_unlock(&iflock);
		ast_log(LOG_ERROR, "Destination format is not supported\n");
		return NULL;
	}

	if (peer) {
		p->username = strdup(peer->name);
		p->host = strdup(peer->ip);
		p->port = peer->port;
		/* Disable gk as we are going to call a known peer*/
		/* OO_SETFLAG(p->flags, H323_DISABLEGK); */

		if (ext)
			ast_copy_string(p->exten, ext, sizeof(p->exten));

		ast_format_cap_copy(p->cap, peer->cap);
		memcpy(&p->prefs, &peer->prefs, sizeof(struct ast_codec_pref));
		p->g729onlyA = peer->g729onlyA;
		p->dtmfmode |= peer->dtmfmode;
		p->dtmfcodec  = peer->dtmfcodec;
		p->faxdetect = peer->faxdetect;
		p->t38support = peer->t38support;
		p->rtptimeout = peer->rtptimeout;
		p->faststart = peer->faststart;
		p->h245tunneling = peer->h245tunneling;
		if (peer->rtpmask && peer->rtpmaskstr[0]) {
			p->rtpmask = peer->rtpmask;
			ast_copy_string(p->rtpmaskstr, peer->rtpmaskstr, sizeof(p->rtpmaskstr));
		}

		if (peer->rtdrinterval) {
			p->rtdrinterval = peer->rtdrinterval;
			p->rtdrcount = peer->rtdrcount;
		}

		ast_copy_string(p->accountcode, peer->accountcode, sizeof(p->accountcode));
		p->amaflags = peer->amaflags;
	} else {
		if (gRasGkMode ==  RasNoGatekeeper) {
			/* no gk and no peer */
			ast_log(LOG_ERROR, "Call to undefined peer %s", dest);
			ast_mutex_lock(&iflock);
			ast_mutex_unlock(&p->lock);
			ooh323_destroy(p);
			ast_mutex_unlock(&iflock);
			return NULL;
		}
		p->g729onlyA = g729onlyA;
		p->dtmfmode = gDTMFMode;
		p->dtmfcodec = gDTMFCodec;
		p->faxdetect = gFAXdetect;
		p->t38support = gT38Support;
		p->rtptimeout = gRTPTimeout;
		ast_format_cap_copy(p->cap, gCap);
		p->rtdrinterval = gRTDRInterval;
		p->rtdrcount = gRTDRCount;
		p->faststart = gFastStart;
		p->h245tunneling = gTunneling;

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


	chan = ooh323_new(p, AST_STATE_DOWN, p->username, cap,
				 requestor ? requestor->linkedid : NULL);
	
	ast_mutex_unlock(&p->lock);

	if (!chan) {
		ast_mutex_lock(&iflock);
		ooh323_destroy(p);
		ast_mutex_unlock(&iflock);
   	} else {
      		ast_mutex_lock(&p->lock);
      		p->callToken = (char*)ast_calloc(1, AST_MAX_EXTENSION);
      		if(!p->callToken) {
       			ast_mutex_unlock(&p->lock);
       			ast_mutex_lock(&iflock);
       			ooh323_destroy(p);
       			ast_mutex_unlock(&iflock);
       			ast_log(LOG_ERROR, "Failed to allocate memory for callToken\n");
       			return NULL;
      		}

      		ast_mutex_unlock(&p->lock);
      		ast_mutex_lock(&ooh323c_cmd_lock);
      		ooMakeCall(data, p->callToken, AST_MAX_EXTENSION, NULL);
      		ast_mutex_unlock(&ooh323c_cmd_lock);
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
      ast_verbose("---   find_user: %s, %s\n",name,ip);

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

	if (p->rtp && ((p->dtmfmode & H323_DTMF_RFC2833) || (p->dtmfmode & H323_DTMF_CISCO))) {
		ast_rtp_instance_dtmf_begin(p->rtp, digit);
	} else if (((p->dtmfmode & H323_DTMF_Q931) ||
						 (p->dtmfmode & H323_DTMF_H245ALPHANUMERIC) ||
						 (p->dtmfmode & H323_DTMF_H245SIGNAL))) {
		dtmf[0] = digit;
		dtmf[1] = '\0';
		ooSendDTMFDigit(p->callToken, dtmf);
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
	if (p->rtp && ((p->dtmfmode & H323_DTMF_RFC2833) || (p->dtmfmode & H323_DTMF_CISCO)) ) 
		ast_rtp_instance_dtmf_end(p->rtp, digit);

	ast_mutex_unlock(&p->lock);
	if (gH323Debug)
		ast_verbose("+++   ooh323_digit_end\n");

	return 0;
}


static int ooh323_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct ooh323_pvt *p = ast->tech_pvt;
	char destination[256];
   	int res=0, i;
	const char *val = NULL;
	ooCallOptions opts = {
		.fastStart = TRUE,
		.tunneling = TRUE,
		.disableGk = TRUE,
      		.callMode = OO_CALLMODE_AUDIOCALL,
      		.transfercap = 0
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
	if (ast->connected.id.number.valid && ast->connected.id.number.str) {
		free(p->callerid_num);
		p->callerid_num = strdup(ast->connected.id.number.str);
	}

	if (ast->connected.id.name.valid && ast->connected.id.name.str) {
		free(p->callerid_name);
		p->callerid_name = strdup(ast->connected.id.name.str);
	} else if (ast->connected.id.number.valid && ast->connected.id.number.str) {
		free(p->callerid_name);
		p->callerid_name = strdup(ast->connected.id.number.str);
	} else {
		ast->connected.id.name.valid = 1;
		free(ast->connected.id.name.str);
		ast->connected.id.name.str = strdup(gCallerID);
		free(p->callerid_name);
		p->callerid_name = strdup(ast->connected.id.name.str);
	}

	/* Retrieve vars */


	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323ID"))) {
		ast_copy_string(p->caller_h323id, val, sizeof(p->caller_h323id));
	}
	
	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323DIALEDDIGITS"))) {
		ast_copy_string(p->caller_dialedDigits, val, sizeof(p->caller_dialedDigits));
      		if(!p->callerid_num)
			p->callerid_num = strdup(val);
	}

	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323EMAIL"))) {
		ast_copy_string(p->caller_email, val, sizeof(p->caller_email));
	}

	if ((val = pbx_builtin_getvar_helper(ast, "CALLER_H323URL"))) {
		ast_copy_string(p->caller_url, val, sizeof(p->caller_url));
	}

	if (p->host && p->port != 0)
		snprintf(destination, sizeof(destination), "%s:%d", p->host, p->port);
	else if (p->host)
		snprintf(destination, sizeof(destination), "%s", p->host);
	else
		ast_copy_string(destination, dest, sizeof(destination));

	destination[sizeof(destination)-1]='\0';

	opts.transfercap = ast->transfercapability;
	opts.fastStart = p->faststart;
	opts.tunneling = p->h245tunneling;

	for (i=0;i<480 && !isRunning(p->callToken);i++) usleep(12000);

	if(OO_TESTFLAG(p->flags, H323_DISABLEGK)) {
		res = ooRunCall(destination, p->callToken, AST_MAX_EXTENSION, &opts);
	} else {
		res = ooRunCall(destination, p->callToken, AST_MAX_EXTENSION, NULL);
 	}

	ast_mutex_unlock(&p->lock);
	if (res != OO_OK) {
		ast_log(LOG_ERROR, "Failed to make call\n");
      		return -1; /* ToDO: cleanup */
	}
	if (gH323Debug)
		ast_verbose("+++   ooh323_call\n");

  return 0;
}

static int ooh323_hangup(struct ast_channel *ast)
{
	struct ooh323_pvt *p = ast->tech_pvt;
   	int q931cause = AST_CAUSE_NORMAL_CLEARING;

	if (gH323Debug)
		ast_verbose("---   ooh323_hangup\n");

	if (p) {
		ast_mutex_lock(&p->lock);

        if (ast->hangupcause) {
                q931cause = ast->hangupcause;
        } else {
                const char *cause = pbx_builtin_getvar_helper(ast, "DIALSTATUS");
                if (cause) {
                        if (!strcmp(cause, "CONGESTION")) {
                                q931cause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
                        } else if (!strcmp(cause, "BUSY")) {
                                q931cause = AST_CAUSE_USER_BUSY;
                        } else if (!strcmp(cause, "CHANISUNVAIL")) {
                                q931cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
                        } else if (!strcmp(cause, "NOANSWER")) {
                                q931cause = AST_CAUSE_NO_ANSWER;
                        } else if (!strcmp(cause, "CANCEL")) {
                                q931cause = AST_CAUSE_CALL_REJECTED;
                        }
                }
        }



		if (gH323Debug)
			ast_verbose("    hanging %s with cause: %d\n", p->username, q931cause);
		ast->tech_pvt = NULL; 
		if (!ast_test_flag(p, H323_ALREADYGONE)) {
         		ooHangCall(p->callToken, 
				ooh323_convert_hangupcause_asteriskToH323(q931cause), q931cause);
			ast_set_flag(p, H323_ALREADYGONE);
			/* ast_mutex_unlock(&p->lock); */
      		} else 
			ast_set_flag(p, H323_NEEDDESTROY);
		/* detach channel here */
		if (p->owner) {
			p->owner->tech_pvt = NULL;
			p->owner = NULL;
			ast_module_unref(myself);
		}

		ast_mutex_unlock(&p->lock);
		ast_mutex_lock(&usecnt_lock);
		usecnt--;
		ast_mutex_unlock(&usecnt_lock);

		/* Notify the module monitors that use count for resource has changed */
		ast_update_use_count();
	  
	} else {
		ast_debug(1, "No call to hangup\n" );
	}
	
	if (gH323Debug)
		ast_verbose("+++   ooh323_hangup\n");

  return 0;
}

static int ooh323_answer(struct ast_channel *ast)
{
	struct ooh323_pvt *p = ast->tech_pvt;
	char *callToken = (char *)NULL;

	if (gH323Debug)
		ast_verbose("--- ooh323_answer\n");

	if (p) {

		ast_mutex_lock(&p->lock);
		callToken = (p->callToken ? strdup(p->callToken) : NULL);
		if (ast->_state != AST_STATE_UP) {
			ast_channel_lock(ast);
			if (!p->alertsent) {
	    			if (gH323Debug) {
					ast_debug(1, "Sending forced ringback for %s, res = %d\n", 
						callToken, ooManualRingback(callToken));
				} else {
	    				ooManualRingback(callToken);
				}
				p->alertsent = 1;
			}
			ast_setstate(ast, AST_STATE_UP);
      			if (option_debug)
				ast_debug(1, "ooh323_answer(%s)\n", ast->name);
			ast_channel_unlock(ast);
			ooAnswerCall(p->callToken);
		}
		ast_mutex_unlock(&p->lock);
	}

	if (gH323Debug)
		ast_verbose("+++ ooh323_answer\n");

  return 0;
}

static struct ast_frame *ooh323_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	struct ooh323_pvt *p = ast->tech_pvt;

	if (!p) return &null_frame;

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
	char buf[256];

	if (p) {
		ast_mutex_lock(&p->lock);

		if (f->frametype == AST_FRAME_MODEM) {
			ast_debug(1, "Send UDPTL %d/%d len %d for %s\n",
				f->frametype, f->subclass.integer, f->datalen, ast->name);
			if (p->udptl)
				res = ast_udptl_write(p->udptl, f);
			ast_mutex_unlock(&p->lock);
			return res;
		}

	
		if (f->frametype == AST_FRAME_VOICE) {
/* sending progress for first */
			if (!ast_test_flag(p, H323_OUTGOING) && !p->progsent &&
			 		p->callToken) {
				ooManualProgress(p->callToken);
				p->progsent = 1;
			}


			if (!(ast_format_cap_iscompatible(ast->nativeformats, &f->subclass.format))) {
				if (!(ast_format_cap_is_empty(ast->nativeformats))) {
					ast_log(LOG_WARNING,
							"Asked to transmit frame type %s, while native formats is %s (read/write = %s/%s)\n",
							ast_getformatname(&f->subclass.format),
							ast_getformatname_multiple(buf, sizeof(buf), ast->nativeformats),
							ast_getformatname(&ast->readformat),
							ast_getformatname(&ast->writeformat));

					ast_set_write_format(ast, &f->subclass.format);
				} else {
					/* ast_set_write_format(ast, f->subclass);
					ast->nativeformats = f->subclass; */
				}
			ast_mutex_unlock(&p->lock);
			return 0;
			}

		if (p->rtp)
			res = ast_rtp_instance_write(p->rtp, f);

		ast_mutex_unlock(&p->lock);

		} else if (f->frametype == AST_FRAME_IMAGE) {
			ast_mutex_unlock(&p->lock);
			return 0;
		} else {
			ast_log(LOG_WARNING, "Can't send %d type frames with OOH323 write\n", 
									 f->frametype);
			ast_mutex_unlock(&p->lock);
			return 0;
		}

	}

	return res;
}

static int ooh323_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{

	struct ooh323_pvt *p = (struct ooh323_pvt *) ast->tech_pvt;
	char *callToken = (char *)NULL;
	int res = -1;

	if (!p) return -1;

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
	 
   	ast_mutex_lock(&p->lock);
	switch (condition) {
	case AST_CONTROL_CONGESTION:
		if (!ast_test_flag(p, H323_ALREADYGONE)) {
            		ooHangCall(callToken, OO_REASON_LOCAL_CONGESTED, 
						AST_CAUSE_SWITCH_CONGESTION);
			ast_set_flag(p, H323_ALREADYGONE);
		}
		break;
	case AST_CONTROL_BUSY:
		if (!ast_test_flag(p, H323_ALREADYGONE)) {
            		ooHangCall(callToken, OO_REASON_LOCAL_BUSY, AST_CAUSE_USER_BUSY);
			ast_set_flag(p, H323_ALREADYGONE);
		}
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);		
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_PROGRESS:
		if (ast->_state != AST_STATE_UP) {
	    		if (!p->progsent) {
	     			if (gH323Debug) {
					ast_debug(1, "Sending manual progress for %s, res = %d\n", callToken,
             				ooManualProgress(callToken));	
				} else {
	     				ooManualProgress(callToken);
				}
	     			p->progsent = 1;
	    		}
		}
	    break;
      case AST_CONTROL_RINGING:
		if (ast->_state == AST_STATE_RING || ast->_state == AST_STATE_RINGING) {
			if (!p->alertsent) {
				if (gH323Debug) {
					ast_debug(1, "Sending manual ringback for %s, res = %d\n",
						callToken,
						ooManualRingback(callToken));
				} else {
					ooManualRingback(callToken);
				}
				p->alertsent = 1;
			}
			p->alertsent = 1;
		}
	 break;
	case AST_CONTROL_SRCUPDATE:
		if (p->rtp) {
			ast_rtp_instance_update_source(p->rtp);
		}
		break;
	case AST_CONTROL_SRCCHANGE:
		if (p->rtp) {
			ast_rtp_instance_change_source(p->rtp);
		}
		break;
	case AST_CONTROL_CONNECTED_LINE:
		if (!ast->connected.id.name.valid
			|| ast_strlen_zero(ast->connected.id.name.str)) {
			break;
		}
		if (gH323Debug) {
			ast_debug(1, "Sending connected line info for %s (%s)\n",
				callToken, ast->connected.id.name.str);
		}
		ooSetANI(callToken, ast->connected.id.name.str);
		break;

      case AST_CONTROL_T38_PARAMETERS:
		if (p->t38support != T38_ENABLED) {
			struct ast_control_t38_parameters parameters = { .request_response = 0 };
			parameters.request_response = AST_T38_REFUSED;
			ast_queue_control_data(ast, AST_CONTROL_T38_PARAMETERS,
						 &parameters, sizeof(parameters));
			break;
		}
		if (datalen != sizeof(struct ast_control_t38_parameters)) {
			ast_log(LOG_ERROR, "Invalid datalen for AST_CONTROL_T38. "
					   "Expected %d, got %d\n",
				(int)sizeof(enum ast_control_t38), (int)datalen);
		} else {
			const struct ast_control_t38_parameters *parameters = data;
			struct ast_control_t38_parameters our_parameters;
			enum ast_control_t38 message = parameters->request_response;
			switch (message) {

			case AST_T38_NEGOTIATED:
				if (p->faxmode) {
					res = 0;
					break;
				}
			case AST_T38_REQUEST_NEGOTIATE:

				if (p->faxmode) {
					/* T.38 already negotiated */
					our_parameters.request_response = AST_T38_NEGOTIATED;
					our_parameters.max_ifp = ast_udptl_get_far_max_ifp(p->udptl);
					our_parameters.rate = AST_T38_RATE_14400;
					ast_queue_control_data(p->owner, AST_CONTROL_T38_PARAMETERS, &our_parameters, sizeof(our_parameters));
				} else if (!p->chmodepend) {
					p->chmodepend = 1;
					ooRequestChangeMode(p->callToken, 1);
					res = 0;
				}
				break;

			case AST_T38_REQUEST_TERMINATE:

				if (!p->faxmode) {
					/* T.38 already terminated */
					our_parameters.request_response = AST_T38_TERMINATED;
					ast_queue_control_data(p->owner, AST_CONTROL_T38_PARAMETERS, &our_parameters, sizeof(our_parameters));
				} else if (!p->chmodepend) {
					p->chmodepend = 1;
					ooRequestChangeMode(p->callToken, 0);
					res = 0;
				}
				break;

			case AST_T38_REQUEST_PARMS:
				our_parameters.request_response = AST_T38_REQUEST_PARMS;
				our_parameters.max_ifp = ast_udptl_get_far_max_ifp(p->udptl);
				our_parameters.rate = AST_T38_RATE_14400;
				ast_queue_control_data(p->owner, AST_CONTROL_T38_PARAMETERS, &our_parameters, sizeof(our_parameters));
				res = AST_T38_REQUEST_PARMS;
				break;

			default:
				;

			}

		}
		break;
      case AST_CONTROL_PROCEEDING:
	case -1:
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d on %s\n",
									condition, callToken);
	}

   	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("++++  ooh323_indicate %d on %s\n", condition, callToken);

   	free(callToken);
	return res;
}

static int ooh323_queryoption(struct ast_channel *ast, int option, void *data, int *datalen)
{

	struct ooh323_pvt *p = (struct ooh323_pvt *) ast->tech_pvt;
	int res = -1;
	enum ast_t38_state state = T38_STATE_UNAVAILABLE;
	char* cp;

	if (!p) return -1;

	ast_mutex_lock(&p->lock);

	if (gH323Debug)
		ast_verbose("----- ooh323_queryoption %d on channel %s\n", option, ast->name);
	 
	switch (option) {

		case AST_OPTION_T38_STATE:

			if (*datalen != sizeof(enum ast_t38_state)) {
				ast_log(LOG_ERROR, "Invalid datalen for AST_OPTION_T38_STATE option."
				" Expected %d, got %d\n", (int)sizeof(enum ast_t38_state), *datalen);
				break;
			}

			if (p->t38support != T38_DISABLED) {
				if (p->faxmode) {
					state = (p->chmodepend) ? T38_STATE_NEGOTIATING : T38_STATE_NEGOTIATED;
				} else {
					state = T38_STATE_UNKNOWN;
				}
			}

			*((enum ast_t38_state *) data) = state;
			res = 0;
			break;


		case AST_OPTION_DIGIT_DETECT:

			cp = (char *) data;
			*cp = p->vad ? 1 : 0;
			ast_debug(1, "Reporting digit detection %sabled on %s\n",
							 *cp ? "en" : "dis", ast->name);

			res = 0;
			break;

		default:	;

	}

	if (gH323Debug)
		ast_verbose("+++++ ooh323_queryoption %d on channel %s\n", option, ast->name);
	 
   	ast_mutex_unlock(&p->lock);

	return res;
}



static int ooh323_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct ooh323_pvt *p = newchan->tech_pvt;

	if (!p) return -1;

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


void ooh323_set_write_format(ooCallData *call, struct ast_format *fmt, int txframes)
{
	struct ooh323_pvt *p = NULL;
	char formats[FORMAT_STRING_SIZE];

	if (gH323Debug)
		ast_verbose("---   ooh323_update_writeformat %s/%d\n", 
				ast_getformatname(fmt), txframes);
	
	p = find_call(call);
	if (!p) {
		ast_log(LOG_ERROR, "No matching call found for %s\n", call->callToken);
		return;
	}

	ast_mutex_lock(&p->lock);

	ast_format_copy(&(p->writeformat), fmt);

	if (p->owner) {
		while (p->owner && ast_channel_trylock(p->owner)) {
			ast_debug(1,"Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		}
		if (!p->owner) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return;
		}
		if (gH323Debug)
	  		ast_verbose("Writeformat before update %s/%s\n", 
			  ast_getformatname(&p->owner->writeformat),
			  ast_getformatname_multiple(formats, sizeof(formats), p->owner->nativeformats));
		if (txframes)
			ast_codec_pref_setsize(&p->prefs, fmt, txframes);
		ast_rtp_codecs_packetization_set(ast_rtp_instance_get_codecs(p->rtp), p->rtp, &p->prefs);
		if (p->dtmfmode & H323_DTMF_RFC2833 && p->dtmfcodec) {
			ast_rtp_codecs_payloads_set_rtpmap_type(ast_rtp_instance_get_codecs(p->rtp),
				 p->rtp, p->dtmfcodec, "audio", "telephone-event", 0);
		}
		if (p->dtmfmode & H323_DTMF_CISCO && p->dtmfcodec) {
			ast_rtp_codecs_payloads_set_rtpmap_type(ast_rtp_instance_get_codecs(p->rtp),
				 p->rtp, p->dtmfcodec, "audio", "cisco-telephone-event", 0);
		}

		ast_format_cap_set(p->owner->nativeformats, fmt);
	  	ast_set_write_format(p->owner, &p->owner->writeformat);
	  	ast_set_read_format(p->owner, &p->owner->readformat);
		ast_channel_unlock(p->owner);
   	} else
		ast_log(LOG_ERROR, "No owner found\n");


	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++   ooh323_update_writeformat\n");
}

void ooh323_set_read_format(ooCallData *call, struct ast_format *fmt)
{
	struct ooh323_pvt *p = NULL;

	if (gH323Debug)
		ast_verbose("---   ooh323_update_readformat %s\n", 
				ast_getformatname(fmt));
	
	p = find_call(call);
	if (!p) {
		ast_log(LOG_ERROR, "No matching call found for %s\n", call->callToken);
		return;
	}

	ast_mutex_lock(&p->lock);

	ast_format_copy(&(p->readformat), fmt);

	if (p->owner) {
		while (p->owner && ast_channel_trylock(p->owner)) {
			ast_debug(1,"Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		}
		if (!p->owner) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return;
		}

		if (gH323Debug)
	  		ast_verbose("Readformat before update %s\n", 
				  ast_getformatname(&p->owner->readformat));
		ast_format_cap_set(p->owner->nativeformats, fmt);
	  	ast_set_read_format(p->owner, &p->owner->readformat);
		ast_channel_unlock(p->owner);
   	} else
		ast_log(LOG_ERROR, "No owner found\n");

	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++   ooh323_update_readformat\n");
}


int onAlerting(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;
	struct ast_channel *c = NULL;

	if (gH323Debug)
		ast_verbose("--- onAlerting %s\n", call->callToken);

   	p = find_call(call);

   	if(!p) {
		ast_log(LOG_ERROR, "No matching call found\n");
		return -1;
	}  
	ast_mutex_lock(&p->lock);
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return 0;
	}
	while (p->owner && ast_channel_trylock(p->owner)) {
		ast_debug(1, "Failed to grab lock, trying again\n");
		DEADLOCK_AVOIDANCE(&p->lock);
	}
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return 0;
	}
	c = p->owner;

	if (call->remoteDisplayName) {
		struct ast_party_connected_line connected;
		struct ast_set_party_connected_line update_connected;

		memset(&update_connected, 0, sizeof(update_connected));
		update_connected.id.name = 1;
		ast_party_connected_line_init(&connected);
		connected.id.name.valid = 1;
		connected.id.name.str = (char *) call->remoteDisplayName;
		connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
		ast_channel_queue_connected_line_update(c, &connected, &update_connected);
	}
	if (c->_state != AST_STATE_UP)
		ast_setstate(c, AST_STATE_RINGING);

	ast_queue_control(c, AST_CONTROL_RINGING);
      	ast_channel_unlock(c);
      	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++ onAlerting %s\n", call->callToken);

	return OO_OK;
}

int onProgress(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;
	struct ast_channel *c = NULL;

	if (gH323Debug)
		ast_verbose("--- onProgress %s\n", call->callToken);

   	p = find_call(call);

   	if(!p) {
		ast_log(LOG_ERROR, "No matching call found\n");
		return -1;
	}  
	ast_mutex_lock(&p->lock);
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return 0;
	}
	while (p->owner && ast_channel_trylock(p->owner)) {
		ast_debug(1, "Failed to grab lock, trying again\n");
		DEADLOCK_AVOIDANCE(&p->lock);
	}
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return 0;
	}
	c = p->owner;

	if (call->remoteDisplayName) {
		struct ast_party_connected_line connected;
		struct ast_set_party_connected_line update_connected;

		memset(&update_connected, 0, sizeof(update_connected));
		update_connected.id.name = 1;
		ast_party_connected_line_init(&connected);
		connected.id.name.valid = 1;
		connected.id.name.str = (char *) call->remoteDisplayName;
		connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
		ast_channel_queue_connected_line_update(c, &connected, &update_connected);
	}
	if (c->_state != AST_STATE_UP)
		ast_setstate(c, AST_STATE_RINGING);

	ast_queue_control(c, AST_CONTROL_PROGRESS);
      	ast_channel_unlock(c);
      	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++ onProgress %s\n", call->callToken);

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
	f.subclass.integer = digit[0];
	f.datalen = 0;
	f.samples = 800;
	f.offset = 0;
	f.data.ptr = NULL;
	f.mallocd = 0;
	f.src = "SEND_DIGIT";

	while (p->owner && ast_channel_trylock(p->owner)) {
		ast_debug(1, "Failed to grab lock, trying again\n");
		DEADLOCK_AVOIDANCE(&p->lock);
	}
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return 0;
	}
	res = ast_queue_frame(p->owner, &f);
   	ast_channel_unlock(p->owner);
   	ast_mutex_unlock(&p->lock);
	return res;
}

int ooh323_onReceivedSetup(ooCallData *call, Q931Message *pmsg)
{
	struct ooh323_pvt *p = NULL;
	struct ooh323_user *user = NULL;
   	struct ast_channel *c = NULL;
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
				}
         else if(alias->type == T_H225AliasAddress_dialedDigits)
         {
            if(!p->callerid_num)
               p->callerid_num = strdup(alias->value);
				ast_copy_string(p->caller_dialedDigits, alias->value, 
															sizeof(p->caller_dialedDigits));
         }
         else if(alias->type == T_H225AliasAddress_email_ID)
         {
				ast_copy_string(p->caller_email, alias->value, sizeof(p->caller_email));
         }
         else if(alias->type == T_H225AliasAddress_url_ID)
         {
				ast_copy_string(p->caller_url, alias->value, sizeof(p->caller_url));
			}
		}
	}

	number[0] = '\0';
   	if(ooCallGetCalledPartyNumber(call, number, OO_MAX_NUMBER_LENGTH)== OO_OK) {
      		strncpy(p->exten, number, sizeof(p->exten)-1);
   	} else {
		update_our_aliases(call, p);
		if (!ast_strlen_zero(p->callee_dialedDigits)) {
         		ast_copy_string(p->exten, p->callee_dialedDigits, sizeof(p->exten));
      		} else if(!ast_strlen_zero(p->callee_h323id)) {
			ast_copy_string(p->exten, p->callee_h323id, sizeof(p->exten));
      		} else if(!ast_strlen_zero(p->callee_email)) {
			ast_copy_string(p->exten, p->callee_email, sizeof(p->exten));
			if ((at = strchr(p->exten, '@'))) {
				*at = '\0';
			}
		}
	}

	/* if no extension found, set to default 's' */
	if (ast_strlen_zero(p->exten)) {
      		p->exten[0]='s';
      		p->exten[1]='\0';
	}

      	user = find_user(p->callerid_name, call->remoteIP);
      	if(user && (user->incominglimit == 0 || user->inUse < user->incominglimit)) {
		ast_mutex_lock(&user->lock);
		p->username = strdup(user->name);
 		p->neighbor.user = user->mUseIP ? ast_strdup(user->mIP) :
						  ast_strdup(user->name);
		ast_copy_string(p->context, user->context, sizeof(p->context));
		ast_copy_string(p->accountcode, user->accountcode, sizeof(p->accountcode));
		p->amaflags = user->amaflags;
		ast_format_cap_copy(p->cap, user->cap);
		p->g729onlyA = user->g729onlyA;
		memcpy(&p->prefs, &user->prefs, sizeof(struct ast_codec_pref));
		p->dtmfmode |= user->dtmfmode;
		p->dtmfcodec = user->dtmfcodec;
		p->faxdetect = user->faxdetect;
		p->t38support = user->t38support;
		p->rtptimeout = user->rtptimeout;
		p->h245tunneling = user->h245tunneling;
		p->faststart = user->faststart;

		if (p->faststart)
         		OO_SETFLAG(call->flags, OO_M_FASTSTART);
		else
			OO_CLRFLAG(call->flags, OO_M_FASTSTART);
		/* if we disable h245tun for this user then we clear flag */
		/* in any other case we don't must touch this */
		/* ie if we receive setup without h245tun but enabled
		   				we can't enable it per call */
		if (!p->h245tunneling)
			OO_CLRFLAG(call->flags, OO_M_TUNNELING);

		if (user->rtpmask && user->rtpmaskstr[0]) {
			p->rtpmask = user->rtpmask;
			ast_copy_string(p->rtpmaskstr, user->rtpmaskstr, 
							 sizeof(p->rtpmaskstr));
		}
		if (user->rtdrcount > 0 && user->rtdrinterval > 0) {
			p->rtdrcount = user->rtdrcount;
			p->rtdrinterval = user->rtdrinterval;
		}
	 	if (user->incominglimit) user->inUse++;
		ast_mutex_unlock(&user->lock);
	} else {
	 if (!OO_TESTFLAG(p->flags,H323_DISABLEGK)) {
		p->username = strdup(call->remoteIP);
	} else {
	  ast_mutex_unlock(&p->lock);
	  ast_log(LOG_ERROR, "Unacceptable ip %s\n", call->remoteIP);
	  if (!user) {
	   ooHangCall(call->callToken, ooh323_convert_hangupcause_asteriskToH323(AST_CAUSE_CALL_REJECTED), AST_CAUSE_CALL_REJECTED);
	   call->callEndReason = OO_REASON_REMOTE_REJECTED;
	  }
	  else {
	   ooHangCall(call->callToken, ooh323_convert_hangupcause_asteriskToH323(AST_CAUSE_NORMAL_CIRCUIT_CONGESTION), AST_CAUSE_NORMAL_CIRCUIT_CONGESTION);
	   call->callEndReason = OO_REASON_REMOTE_REJECTED;
	  }
	  ast_set_flag(p, H323_NEEDDESTROY);
	  return -1;
	 }
	}

	ooh323c_set_capability_for_call(call, &p->prefs, p->cap, p->dtmfmode, p->dtmfcodec,
					 p->t38support, p->g729onlyA);
/* Incoming call */
  	c = ooh323_new(p, AST_STATE_RING, p->username, 0, NULL);
  	if(!c) {
   	ast_mutex_unlock(&p->lock);
   	ast_log(LOG_ERROR, "Could not create ast_channel\n");
         return -1;
  	}
	if (!configure_local_rtp(p, call)) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Couldn't create rtp structure\n");
		return -1;
	}

	ast_mutex_unlock(&p->lock);

	if (gH323Debug)
		ast_verbose("+++   ooh323_onReceivedSetup - Determined context %s, "
						"extension %s\n", p->context, p->exten);

	return OO_OK;
}



int onOutgoingCall(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;
	int i = 0;

	if (gH323Debug)
		ast_verbose("---   onOutgoingCall %lx: %s\n", (long unsigned int) call, call->callToken);

	if (!strcmp(call->callType, "outgoing")) {
		p = find_call(call);
		if (!p) {
      			ast_log(LOG_ERROR, "Failed to find a matching call.\n");
			return -1;
		}
		ast_mutex_lock(&p->lock);

		if (!ast_strlen_zero(p->callerid_name)) {
			ooCallSetCallerId(call, p->callerid_name);
		}
		if (!ast_strlen_zero(p->callerid_num)) {
			i = 0;
			while (*(p->callerid_num + i) != '\0') {
            			if(!isdigit(*(p->callerid_num+i))) { break; }
				i++;
			}
         		if(*(p->callerid_num+i) == '\0')
				ooCallSetCallingPartyNumber(call, p->callerid_num);
         		else {
            			if(!p->callerid_name)
					ooCallSetCallerId(call, p->callerid_num);
			}
		}
		
		if (!ast_strlen_zero(p->caller_h323id))
			ooCallAddAliasH323ID(call, p->caller_h323id);

		if (!ast_strlen_zero(p->caller_dialedDigits)) {
			if (gH323Debug) {
				ast_verbose("Setting dialed digits %s\n", p->caller_dialedDigits);
			}
			ooCallAddAliasDialedDigits(call, p->caller_dialedDigits);
		} else if (!ast_strlen_zero(p->callerid_num)) {
			if (ooIsDailedDigit(p->callerid_num)) {
				if (gH323Debug) {
					ast_verbose("setting callid number %s\n", p->callerid_num);
				}
				ooCallAddAliasDialedDigits(call, p->callerid_num);
			} else if (ast_strlen_zero(p->caller_h323id)) {
				ooCallAddAliasH323ID(call, p->callerid_num);
			}
		}
		if (p->rtpmask && p->rtpmaskstr[0]) {
			call->rtpMask = p->rtpmask;
			ast_mutex_lock(&call->rtpMask->lock);
			call->rtpMask->inuse++;
			ast_mutex_unlock(&call->rtpMask->lock);
			ast_copy_string(call->rtpMaskStr, p->rtpmaskstr, sizeof(call->rtpMaskStr));
		}

		if (!configure_local_rtp(p, call)) {
			ast_mutex_unlock(&p->lock);
			return OO_FAILED;
		}

		ast_mutex_unlock(&p->lock);
	}

	if (gH323Debug)
		ast_verbose("+++   onOutgoingCall %s\n", call->callToken);
	return OO_OK;
}


int onNewCallCreated(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;
	int i = 0;

	if (gH323Debug)
		ast_verbose("---   onNewCallCreated %lx: %s\n", (long unsigned int) call, call->callToken);

   	ast_mutex_lock(&call->Lock);
   	if (ooh323c_start_call_thread(call)) {
    		ast_log(LOG_ERROR,"Failed to create call thread.\n");
    		ast_mutex_unlock(&call->Lock);
    		return -1;
   	}

	if (!strcmp(call->callType, "outgoing")) {
		p = find_call(call);
		if (!p) {
      			ast_log(LOG_ERROR, "Failed to find a matching call.\n");
			ast_mutex_unlock(&call->Lock);
			return -1;
		}
		ast_mutex_lock(&p->lock);

		if (!ast_strlen_zero(p->callerid_name)) {
			ooCallSetCallerId(call, p->callerid_name);
		}
		if (!ast_strlen_zero(p->callerid_num)) {
			i = 0;
			while (*(p->callerid_num + i) != '\0') {
            			if(!isdigit(*(p->callerid_num+i))) { break; }
				i++;
			}
         		if(*(p->callerid_num+i) == '\0')
				ooCallSetCallingPartyNumber(call, p->callerid_num);
         		else {
            			if(ast_strlen_zero(p->callerid_name))
					ooCallSetCallerId(call, p->callerid_num);
			}
		}
		
		if (!ast_strlen_zero(p->caller_h323id))
			ooCallAddAliasH323ID(call, p->caller_h323id);

		if (!ast_strlen_zero(p->caller_dialedDigits)) {
			if (gH323Debug) {
				ast_verbose("Setting dialed digits %s\n", p->caller_dialedDigits);
			}
			ooCallAddAliasDialedDigits(call, p->caller_dialedDigits);
		} else if (!ast_strlen_zero(p->callerid_num)) {
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

      		ooh323c_set_capability_for_call(call, &p->prefs, p->cap,
                                     p->dtmfmode, p->dtmfcodec, p->t38support, p->g729onlyA);

		/* configure_local_rtp(p, call); */
		ast_mutex_unlock(&p->lock);
	}

   	ast_mutex_unlock(&call->Lock);
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

   	if(ast_test_flag(p, H323_OUTGOING)) {
		ast_mutex_lock(&p->lock);
		if (!p->owner) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return -1;
		}
	
		while (p->owner && ast_channel_trylock(p->owner)) {
			ast_debug(1, "Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		}
		if (p->owner) {
			struct ast_channel* c = p->owner;

			if (call->remoteDisplayName) {
				struct ast_party_connected_line connected;
				struct ast_set_party_connected_line update_connected;

				memset(&update_connected, 0, sizeof(update_connected));
				update_connected.id.name = 1;
				ast_party_connected_line_init(&connected);
				connected.id.name.valid = 1;
				connected.id.name.str = (char *) call->remoteDisplayName;
				connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
				ast_channel_queue_connected_line_update(c, &connected, &update_connected);
			}

			ast_queue_control(c, AST_CONTROL_ANSWER);
   			ast_channel_unlock(p->owner);
			manager_event(EVENT_FLAG_SYSTEM,"ChannelUpdate","Channel: %s\r\nChanneltype: %s\r\n"
				"CallRef: %d\r\n", c->name, "OOH323", p->call_reference);
		}
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


   if ((p = find_call(call))) {
	ast_mutex_lock(&p->lock);
  
	while (p->owner) {
		if (ast_channel_trylock(p->owner)) {
			ooTrace(OOTRCLVLINFO, "Failed to grab lock, trying again\n");
         		ast_debug(1, "Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		} else {
         		ownerLock = 1; break;
		}
	}

	if (ownerLock) {
		if (!ast_test_flag(p, H323_ALREADYGONE)) { 

			ast_set_flag(p, H323_ALREADYGONE);
			p->owner->hangupcause = call->q931cause;
			p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
			ast_queue_hangup_with_cause(p->owner,call->q931cause);
		}
   	}

   	if(p->owner) {
    		p->owner->tech_pvt = NULL;
		ast_channel_unlock(p->owner);
    		p->owner = NULL;
		ast_module_unref(myself);
	}

	ast_set_flag(p, H323_NEEDDESTROY);

   	ooh323c_stop_call_thread(call);

	ast_mutex_unlock(&p->lock);
   	ast_mutex_lock(&usecnt_lock);
   	usecnt--;
   	ast_mutex_unlock(&usecnt_lock);

    }

	if (gH323Debug)
		ast_verbose("+++   onCallCleared\n");

	return OO_OK;
}

/* static void ooh323_delete_user(struct ooh323_user *user)
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

} */

void ooh323_delete_peer(struct ooh323_peer *peer)
{
	struct ooh323_peer *prev = NULL, *cur = NULL;

	if (gH323Debug)
		ast_verbose("---   ooh323_delete_peer\n");

	if (peer) {	
      cur = peerl.peers;
		ast_mutex_lock(&peerl.lock);
      while(cur) {
         if(cur==peer) break;
         prev = cur;
         cur = cur->next;
		}

		if (cur) {
         if(prev)
				prev->next = cur->next;
         else
				peerl.peers = cur->next;
			}
		ast_mutex_unlock(&peerl.lock);

      if(peer->h323id)   free(peer->h323id);
      if(peer->email)    free(peer->email);
      if(peer->url)      free(peer->url);
      if(peer->e164)     free(peer->e164);

		peer->cap = ast_format_cap_destroy(peer->cap);
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

   	user = ast_calloc(1,sizeof(struct ooh323_user));
	if (user) {
		memset(user, 0, sizeof(struct ooh323_user));
		if (!(user->cap = ast_format_cap_alloc())) {
			ast_free(user);
			return NULL;
		}
		ast_mutex_init(&user->lock);
		ast_copy_string(user->name, name, sizeof(user->name));
		ast_format_cap_copy(user->cap, gCap);
		memcpy(&user->prefs, &gPrefs, sizeof(user->prefs));
		user->rtptimeout = gRTPTimeout;
		user->dtmfmode = gDTMFMode;
		user->dtmfcodec = gDTMFCodec;
		user->faxdetect = gFAXdetect;
		user->t38support = gT38Support;
		user->faststart = gFastStart;
		user->h245tunneling = gTunneling;
		user->g729onlyA = g729onlyA;
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
            			strncpy(user->accountcode, v->value, 
						sizeof(user->accountcode)-1);
			} else if (!strcasecmp(v->name, "roundtrip")) {
				sscanf(v->value, "%d,%d", &user->rtdrcount, &user->rtdrinterval);
			} else if (!strcasecmp(v->name, "faststart")) {
				user->faststart = ast_true(v->value);
			} else if (!strcasecmp(v->name, "h245tunneling")) {
				user->h245tunneling = ast_true(v->value);
			} else if (!strcasecmp(v->name, "g729onlyA")) {
				user->g729onlyA = ast_true(v->value);
			} else if (!strcasecmp(v->name, "rtptimeout")) {
				user->rtptimeout = atoi(v->value);
				if (user->rtptimeout < 0)
					user->rtptimeout = gRTPTimeout;
			} else if (!strcasecmp(v->name, "rtpmask")) {
				if ((user->rtpmask = ast_calloc(1, sizeof(struct OOH323Regex))) &&
					(regcomp(&user->rtpmask->regex, v->value, REG_EXTENDED) 
											== 0)) {
					ast_mutex_init(&user->rtpmask->lock);
					user->rtpmask->inuse = 1;
					ast_copy_string(user->rtpmaskstr, v->value, 
								sizeof(user->rtpmaskstr));
				} else user->rtpmask = NULL;
			} else if (!strcasecmp(v->name, "disallow")) {
				ast_parse_allow_disallow(&user->prefs, 
					user->cap,  v->value, 0);
			} else if (!strcasecmp(v->name, "allow")) {
				const char* tcodecs = v->value;
				if (!strcasecmp(v->value, "all")) {
					tcodecs = "ulaw,alaw,g729,g723,gsm";
				}
				ast_parse_allow_disallow(&user->prefs,
					 user->cap,  tcodecs, 1);
			} else if (!strcasecmp(v->name, "amaflags")) {
				user->amaflags = ast_cdr_amaflags2int(v->value);
         		} else if (!strcasecmp(v->name, "ip") || !strcasecmp(v->name, "host")) {
				struct ast_sockaddr p;
				if (!ast_parse_arg(v->value, PARSE_ADDR, &p)) {
					ast_copy_string(user->mIP, ast_sockaddr_stringify_addr(&p), sizeof(user->mIP)-1);
				} else {	
            				ast_copy_string(user->mIP, v->value, sizeof(user->mIP)-1);
				}
            			user->mUseIP = 1;
	 		} else if (!strcasecmp(v->name, "dtmfmode")) {
				if (!strcasecmp(v->value, "rfc2833"))
					user->dtmfmode = H323_DTMF_RFC2833;
				if (!strcasecmp(v->value, "cisco"))
					user->dtmfmode = H323_DTMF_CISCO;
				else if (!strcasecmp(v->value, "q931keypad"))
					user->dtmfmode = H323_DTMF_Q931;
				else if (!strcasecmp(v->value, "h245alphanumeric"))
					user->dtmfmode = H323_DTMF_H245ALPHANUMERIC;
				else if (!strcasecmp(v->value, "h245signal"))
					user->dtmfmode = H323_DTMF_H245SIGNAL;
				else if (!strcasecmp(v->value, "inband"))
					user->dtmfmode = H323_DTMF_INBAND;
			} else if (!strcasecmp(v->name, "relaxdtmf")) {
				user->dtmfmode |= ast_true(v->value) ? H323_DTMF_INBANDRELAX : 0;
			} else if (!strcasecmp(v->name, "dtmfcodec") && atoi(v->value)) {
				user->dtmfcodec = atoi(v->value);
			} else if (!strcasecmp(v->name, "faxdetect")) {
				if (ast_true(v->value)) {
					user->faxdetect = FAXDETECT_CNG | FAXDETECT_T38;
				} else if (ast_false(v->value)) {
					user->faxdetect = 0;
				} else {
					char *buf = ast_strdupa(v->value);
					char *word, *next = buf;
					user->faxdetect = 0;
					while ((word = strsep(&next, ","))) {
						if (!strcasecmp(word, "cng")) {
							user->faxdetect |= FAXDETECT_CNG;
						} else if (!strcasecmp(word, "t38")) {
							user->faxdetect |= FAXDETECT_T38;
						} else {
							ast_log(LOG_WARNING, "Unknown faxdetect mode '%s' on line %d.\n", word, v->lineno);
						}
					}

				}
			} else if (!strcasecmp(v->name, "t38support")) {
				if (!strcasecmp(v->value, "disabled"))
					user->t38support = T38_DISABLED;
				if (!strcasecmp(v->value, "no"))
					user->t38support = T38_DISABLED;
				else if (!strcasecmp(v->value, "faxgw"))
					user->t38support = T38_FAXGW;
				else if (!strcasecmp(v->value, "yes"))
					user->t38support = T38_ENABLED;
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
		if (!(peer->cap = ast_format_cap_alloc())) {
			ast_free(peer);
			return NULL;
		}
		ast_mutex_init(&peer->lock);
		ast_copy_string(peer->name, name, sizeof(peer->name));
		ast_format_cap_copy(peer->cap, gCap);
      		memcpy(&peer->prefs, &gPrefs, sizeof(peer->prefs));
		peer->rtptimeout = gRTPTimeout;
		ast_copy_string(peer->accountcode, gAccountcode, sizeof(peer->accountcode));
		peer->amaflags = gAMAFLAGS;
		peer->dtmfmode = gDTMFMode;
		peer->dtmfcodec = gDTMFCodec;
		peer->faxdetect = gFAXdetect;
		peer->t38support = gT38Support;
		peer->faststart = gFastStart;
		peer->h245tunneling = gTunneling;
		peer->g729onlyA = g729onlyA;
		peer->port = 1720;
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
         		} else if (!strcasecmp(v->name, "host") || !strcasecmp(v->name, "ip")) {
				struct ast_sockaddr p;
				if (!ast_parse_arg(v->value, PARSE_ADDR, &p)) {
					ast_copy_string(peer->ip, ast_sockaddr_stringify_host(&p), sizeof(peer->ip));
				} else {	
            				ast_copy_string(peer->ip, v->value, sizeof(peer->ip));
				}
			
			} else if (!strcasecmp(v->name, "outgoinglimit")) {
            			peer->outgoinglimit = atoi(v->value);
            			if (peer->outgoinglimit < 0)
					peer->outgoinglimit = 0;
			} else if (!strcasecmp(v->name, "accountcode")) {
				ast_copy_string(peer->accountcode, v->value, sizeof(peer->accountcode));
			} else if (!strcasecmp(v->name, "faststart")) {
				peer->faststart = ast_true(v->value);
			} else if (!strcasecmp(v->name, "h245tunneling")) {
				peer->h245tunneling = ast_true(v->value);
			} else if (!strcasecmp(v->name, "g729onlyA")) {
				peer->g729onlyA = ast_true(v->value);
			} else if (!strcasecmp(v->name, "rtptimeout")) {
            			peer->rtptimeout = atoi(v->value);
            			if(peer->rtptimeout < 0)
					peer->rtptimeout = gRTPTimeout;
			} else if (!strcasecmp(v->name, "rtpmask")) {
				if ((peer->rtpmask = ast_calloc(1, sizeof(struct OOH323Regex))) &&
					(regcomp(&peer->rtpmask->regex, v->value, REG_EXTENDED) 
											== 0)) {
					ast_mutex_init(&peer->rtpmask->lock);
					peer->rtpmask->inuse = 1;
					ast_copy_string(peer->rtpmaskstr, v->value, 
								sizeof(peer->rtpmaskstr));
				} else peer->rtpmask = NULL;
			} else if (!strcasecmp(v->name, "disallow")) {
				ast_parse_allow_disallow(&peer->prefs, peer->cap, 
												 v->value, 0); 
			} else if (!strcasecmp(v->name, "allow")) {
				const char* tcodecs = v->value;
				if (!strcasecmp(v->value, "all")) {
					tcodecs = "ulaw,alaw,g729,g723,gsm";
				}
				ast_parse_allow_disallow(&peer->prefs, peer->cap, 
												 tcodecs, 1);				 
			} else if (!strcasecmp(v->name,  "amaflags")) {
				peer->amaflags = ast_cdr_amaflags2int(v->value);
			} else if (!strcasecmp(v->name, "roundtrip")) {
				sscanf(v->value, "%d,%d", &peer->rtdrcount, &peer->rtdrinterval);
			} else if (!strcasecmp(v->name, "dtmfmode")) {
				if (!strcasecmp(v->value, "rfc2833"))
					peer->dtmfmode = H323_DTMF_RFC2833;
				if (!strcasecmp(v->value, "cisco"))
					peer->dtmfmode = H323_DTMF_CISCO;
				else if (!strcasecmp(v->value, "q931keypad"))
					peer->dtmfmode = H323_DTMF_Q931;
				else if (!strcasecmp(v->value, "h245alphanumeric"))
					peer->dtmfmode = H323_DTMF_H245ALPHANUMERIC;
				else if (!strcasecmp(v->value, "h245signal"))
					peer->dtmfmode = H323_DTMF_H245SIGNAL;
				else if (!strcasecmp(v->value, "inband"))
					peer->dtmfmode = H323_DTMF_INBAND;
			} else if (!strcasecmp(v->name, "relaxdtmf")) {
				peer->dtmfmode |= ast_true(v->value) ? H323_DTMF_INBANDRELAX : 0;
			} else if (!strcasecmp(v->name, "dtmfcodec") && atoi(v->value)) {
				peer->dtmfcodec = atoi(v->value);
			} else if (!strcasecmp(v->name, "faxdetect")) {
				if (ast_true(v->value)) {
					peer->faxdetect = FAXDETECT_CNG | FAXDETECT_T38;
				} else if (ast_false(v->value)) {
					peer->faxdetect = 0;
				} else {
					char *buf = ast_strdupa(v->value);
					char *word, *next = buf;
					peer->faxdetect = 0;
					while ((word = strsep(&next, ","))) {
						if (!strcasecmp(word, "cng")) {
							peer->faxdetect |= FAXDETECT_CNG;
						} else if (!strcasecmp(word, "t38")) {
							peer->faxdetect |= FAXDETECT_T38;
						} else {
							ast_log(LOG_WARNING, "Unknown faxdetect mode '%s' on line %d.\n", word, v->lineno);
						}
					}

				}
			} else if (!strcasecmp(v->name, "t38support")) {
				if (!strcasecmp(v->value, "disabled"))
					peer->t38support = T38_DISABLED;
				if (!strcasecmp(v->value, "no"))
					peer->t38support = T38_DISABLED;
				else if (!strcasecmp(v->value, "faxgw"))
					peer->t38support = T38_FAXGW;
				else if (!strcasecmp(v->value, "yes"))
					peer->t38support = T38_ENABLED;
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

/*--- h323_reload: Force reload of module from cli ---*/

char *handle_cli_ooh323_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{

       switch (cmd) {
       case CLI_INIT:
               e->command = "ooh323 reload";
               e->usage =
                       "Usage: ooh323 reload\n"
                       "                Reload OOH323 config.\n";
               return NULL;
       case CLI_GENERATE:
               return NULL;
       }

       if (a->argc != 2)
               return CLI_SHOWUSAGE;

	if (gH323Debug)
		ast_verbose("---   ooh323_reload\n");

	ast_mutex_lock(&h323_reload_lock);
	if (h323_reloading) {
		ast_verbose("Previous OOH323 reload not yet done\n");
   } else {
		h323_reloading = 1;
	}
	ast_mutex_unlock(&h323_reload_lock);
	restart_monitor();

	if (gH323Debug)
		ast_verbose("+++   ooh323_reload\n");

	return 0;
}

int reload_config(int reload)
{
	int format;
	struct ooAliases  *pNewAlias = NULL, *cur, *prev;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ooh323_user *user = NULL;
	struct ooh323_peer *peer = NULL;
	char *cat;
	const char *utype;
	struct ast_format tmpfmt;

	if (gH323Debug)
		ast_verbose("---   reload_config\n");

	cfg = ast_config_load((char*)config, config_flags);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, OOH323 disabled\n", config);
		return 1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return RESULT_SUCCESS;

	if (reload) {
		delete_users();
		delete_peers();
		if (gH323Debug) {
			ast_verbose("  reload_config - Freeing up alias list\n");
		}
		cur = gAliasList;
		while (cur) {
			prev = cur;
	  		cur = cur->next;
	  		free(prev->value);
	  		free(prev);
		}
		gAliasList = NULL;
	}

	/* Inintialize everything to default */
	strcpy(gLogFile, DEFAULT_LOGFILE);
	gPort = 1720;
	gIP[0] = '\0';
	strcpy(gCallerID, DEFAULT_H323ID);
	ast_format_cap_set(gCap, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
	memset(&gPrefs, 0, sizeof(struct ast_codec_pref));
	gDTMFMode = H323_DTMF_RFC2833;
	gDTMFCodec = 101;
	gFAXdetect = FAXDETECT_CNG;
	gT38Support = T38_FAXGW;
	gTRCLVL = OOTRCLVLERR;
	gRasGkMode = RasNoGatekeeper;
	gGatekeeper[0] = '\0';
	gRTPTimeout = 60;
	gRTDRInterval = 0;
	gRTDRCount = 0;
	strcpy(gAccountcode, DEFAULT_H323ACCNT);
	gFastStart = 1;
	gTunneling = 1;
	gTOS = 0;
	strcpy(gContext, DEFAULT_CONTEXT);
	gAliasList = NULL;
	gMediaWaitForConnect = 0;
	ooconfig.mTCPPortStart = 12030;
	ooconfig.mTCPPortEnd = 12230;
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	v = ast_variable_browse(cfg, "general");
	while (v) {

		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value)) {
			v = v->next;
			continue;
		}
	
		if (!strcasecmp(v->name, "port")) {
			gPort = (int)strtol(v->value, NULL, 10);
		} else if (!strcasecmp(v->name, "bindaddr")) {
			ast_copy_string(gIP, v->value, sizeof(gIP));
			if (ast_parse_arg(v->value, PARSE_ADDR, &bindaddr)) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
				return 1;
			}
			if (ast_sockaddr_is_ipv6(&bindaddr)) {
				v6mode = 1;
			}
		} else if (!strcasecmp(v->name, "h225portrange")) {
			char* endlimit = 0;
         		char temp[512];
			ast_copy_string(temp, v->value, sizeof(temp));
			endlimit = strchr(temp, ',');
			if (endlimit) {
				*endlimit = '\0';
				endlimit++;
				ooconfig.mTCPPortStart = atoi(temp);
				ooconfig.mTCPPortEnd = atoi(endlimit);

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
		} else if (!strcasecmp(v->name, "g729onlyA")) {
			g729onlyA = ast_true(v->value);
		} else if (!strcasecmp(v->name, "roundtrip")) {
			sscanf(v->value, "%d,%d", &gRTDRCount, &gRTDRInterval);
      		} else if (!strcasecmp(v->name, "trybemaster")) {
			gBeMaster = ast_true(v->value);
			if (gBeMaster)
				ooH323EpTryBeMaster(1);
			else 
				ooH323EpTryBeMaster(0);
		} else if (!strcasecmp(v->name, "h323id")) {
         		pNewAlias = ast_calloc(1, sizeof(struct ooAliases));
			if (!pNewAlias) {
				ast_log(LOG_ERROR, "Failed to allocate memory for h323id alias\n");
				return 1;
			}
	 		if (gAliasList == NULL) { /* first h323id - set as callerid if callerid is not set */
	  			ast_copy_string(gCallerID, v->value, sizeof(gCallerID));
	 		}
			pNewAlias->type =  T_H225AliasAddress_h323_ID;
			pNewAlias->value = strdup(v->value);
			pNewAlias->next = gAliasList;
			gAliasList = pNewAlias;
			pNewAlias = NULL;
		} else if (!strcasecmp(v->name, "e164")) {
         		pNewAlias = ast_calloc(1, sizeof(struct ooAliases));
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
         		pNewAlias = ast_calloc(1, sizeof(struct ooAliases));
			if (!pNewAlias) {
				ast_log(LOG_ERROR, "Failed to allocate memory for email alias\n");
				return 1;
			}
			pNewAlias->type =  T_H225AliasAddress_email_ID;
			pNewAlias->value = strdup(v->value);
			pNewAlias->next = gAliasList;
			gAliasList = pNewAlias;
			pNewAlias = NULL;
      } else if (!strcasecmp(v->name, "t35country")) {
         t35countrycode = atoi(v->value);
      } else if (!strcasecmp(v->name, "t35extensions")) {
         t35extensions = atoi(v->value);
      } else if (!strcasecmp(v->name, "manufacturer")) {
         manufacturer = atoi(v->value);
      } else if (!strcasecmp(v->name, "vendorid")) {
         ast_copy_string(vendor, v->value, sizeof(vendor));
      } else if (!strcasecmp(v->name, "versionid")) {
         ast_copy_string(version, v->value, sizeof(version));
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
            			strncpy(gGatekeeper, v->value, sizeof(gGatekeeper)-1);
			}
		} else if (!strcasecmp(v->name, "logfile")) {
         strncpy(gLogFile, v->value, sizeof(gLogFile)-1);
		} else if (!strcasecmp(v->name, "context")) {
         strncpy(gContext, v->value, sizeof(gContext)-1);
         ast_verbose(VERBOSE_PREFIX_3 "  == Setting default context to %s\n", 
                                                      gContext);
		} else if (!strcasecmp(v->name, "rtptimeout")) {
			gRTPTimeout = atoi(v->value);
			if (gRTPTimeout <= 0)
				gRTPTimeout = 60;
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%30i", &format) == 1)
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
         ast_copy_string(gAccountcode, v->value, sizeof(gAccountcode));
		} else if (!strcasecmp(v->name, "disallow")) {
			ast_parse_allow_disallow(&gPrefs, gCap, v->value, 0);
		} else if (!strcasecmp(v->name, "allow")) {
			const char* tcodecs = v->value;
			if (!strcasecmp(v->value, "all")) {
				tcodecs = "ulaw,alaw,g729,g723,gsm";
			}
			ast_parse_allow_disallow(&gPrefs, gCap, tcodecs, 1);
		} else if (!strcasecmp(v->name, "dtmfmode")) {
			if (!strcasecmp(v->value, "inband"))
				gDTMFMode = H323_DTMF_INBAND;
			else if (!strcasecmp(v->value, "rfc2833"))
				gDTMFMode = H323_DTMF_RFC2833;
			else if (!strcasecmp(v->value, "cisco"))
				gDTMFMode = H323_DTMF_CISCO;
			else if (!strcasecmp(v->value, "q931keypad"))
				gDTMFMode = H323_DTMF_Q931;
			else if (!strcasecmp(v->value, "h245alphanumeric"))
				gDTMFMode = H323_DTMF_H245ALPHANUMERIC;
			else if (!strcasecmp(v->value, "h245signal"))
				gDTMFMode = H323_DTMF_H245SIGNAL;
			else {
            ast_log(LOG_WARNING, "Unknown dtmf mode '%s', using rfc2833\n", 
                                                                    v->value);
				gDTMFMode = H323_DTMF_RFC2833;
			}
		} else if (!strcasecmp(v->name, "relaxdtmf")) {
			gDTMFMode |= ast_true(v->value) ? H323_DTMF_INBANDRELAX : 0;
		} else if (!strcasecmp(v->name, "dtmfcodec") && atoi(v->value)) {
			gDTMFCodec = atoi(v->value);
		} else if (!strcasecmp(v->name, "faxdetect")) {
			if (ast_true(v->value)) {
				gFAXdetect = FAXDETECT_CNG | FAXDETECT_T38;
			} else if (ast_false(v->value)) {
				gFAXdetect = 0;
			} else {
				char *buf = ast_strdupa(v->value);
				char *word, *next = buf;
				gFAXdetect = 0;
				while ((word = strsep(&next, ","))) {
					if (!strcasecmp(word, "cng")) {
						gFAXdetect |= FAXDETECT_CNG;
					} else if (!strcasecmp(word, "t38")) {
						gFAXdetect |= FAXDETECT_T38;
					} else {
						ast_log(LOG_WARNING, "Unknown faxdetect mode '%s' on line %d.\n", word, v->lineno);
					}
				}

			}
		} else if (!strcasecmp(v->name, "t38support")) {
			if (!strcasecmp(v->value, "disabled"))
				gT38Support = T38_DISABLED;
			if (!strcasecmp(v->value, "no"))
				gT38Support = T38_DISABLED;
			else if (!strcasecmp(v->value, "faxgw"))
				gT38Support = T38_FAXGW;
			else if (!strcasecmp(v->value, "yes"))
				gT38Support = T38_ENABLED;
		} else if (!strcasecmp(v->name, "tracelevel")) {
			gTRCLVL = atoi(v->value);
			ooH323EpSetTraceLevel(gTRCLVL);
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
		if (!strcmp(gIP, "127.0.0.1") || !strcmp(gIP, "::1")) {
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
		if (!strcmp(peer->name, a->argv[3])) {
			break;
		} else {
			prev = peer;
			peer = peer->next;
			ast_mutex_unlock(&prev->lock);
		}
	}

	if (peer) {
		sprintf(ip_port, "%s:%d", peer->ip, peer->port);
		ast_cli(a->fd, "%-15.15s%s\n", "Name: ", peer->name);
		ast_cli(a->fd, "%s:%s,%s\n", "FastStart/H.245 Tunneling", peer->faststart?"yes":"no",
					peer->h245tunneling?"yes":"no");
		ast_cli(a->fd, "%-15.15s%s", "Format Prefs: ", "(");
		print_codec_to_cli(a->fd, &peer->prefs);
		ast_cli(a->fd, ")\n");
		ast_cli(a->fd, "%-15.15s", "DTMF Mode: ");
		if (peer->dtmfmode & H323_DTMF_CISCO) {
			ast_cli(a->fd, "%s\n", "cisco");
			ast_cli(a->fd, "%-15.15s%d\n", "DTMF Codec: ", peer->dtmfcodec);
		} else if (peer->dtmfmode & H323_DTMF_RFC2833) {
			ast_cli(a->fd, "%s\n", "rfc2833");
			ast_cli(a->fd, "%-15.15s%d\n", "DTMF Codec: ", peer->dtmfcodec);
		} else if (peer->dtmfmode & H323_DTMF_Q931) {
			ast_cli(a->fd, "%s\n", "q931keypad");
		} else if (peer->dtmfmode & H323_DTMF_H245ALPHANUMERIC) {
			ast_cli(a->fd, "%s\n", "h245alphanumeric");
		} else if (peer->dtmfmode & H323_DTMF_H245SIGNAL) {
			ast_cli(a->fd, "%s\n", "h245signal");
		} else if (peer->dtmfmode & H323_DTMF_INBAND && peer->dtmfmode & H323_DTMF_INBANDRELAX) {
			ast_cli(a->fd, "%s\n", "inband-relaxed");
		} else if (peer->dtmfmode & H323_DTMF_INBAND) {
			ast_cli(a->fd, "%s\n", "inband");
		} else {
			ast_cli(a->fd, "%s\n", "unknown");
		}
		ast_cli(a->fd,"%-15s", "T.38 Mode: ");
		if (peer->t38support == T38_DISABLED) {
			ast_cli(a->fd, "%s\n", "disabled");
		} else if (peer->t38support == T38_FAXGW) {
			ast_cli(a->fd, "%s\n", "faxgw/chan_sip compatible");
		}
		if (peer->faxdetect == (FAXDETECT_CNG | FAXDETECT_T38)) {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "Yes");
		} else if (peer->faxdetect & FAXDETECT_CNG) {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "Cng");
		} else if (peer->faxdetect & FAXDETECT_T38) {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "T.38");
		} else {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "No");
		}

		ast_cli(a->fd, "%-15.15s%s\n", "AccountCode: ", peer->accountcode);
		ast_cli(a->fd, "%-15.15s%s\n", "AMA flags: ", ast_cdr_flags2str(peer->amaflags));
		ast_cli(a->fd, "%-15.15s%s\n", "IP:Port: ", ip_port);
		ast_cli(a->fd, "%-15.15s%d\n", "OutgoingLimit: ", peer->outgoinglimit);
		ast_cli(a->fd, "%-15.15s%d\n", "rtptimeout: ", peer->rtptimeout);
		if (peer->rtpmaskstr[0]) {
			ast_cli(a->fd, "%-15.15s%s\n", "rtpmask: ", peer->rtpmaskstr);
		}
		if (peer->rtdrcount && peer->rtdrinterval) {
			ast_cli(a->fd, "%-15.15s%d,%d\n", "RoundTrip: ", peer->rtdrcount, peer->rtdrinterval);
		}
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
	struct ooh323_peer *prev = NULL, *peer = NULL;
   char formats[FORMAT_STRING_SIZE];
   char ip_port[30];
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
                 ast_getformatname_multiple(formats,FORMAT_STRING_SIZE,peer->cap));
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
	int x;
	struct ast_format tmpfmt;
	for (x = 0; x < 32; x++) {
		ast_codec_pref_index(pref, x, &tmpfmt);
		if (!tmpfmt.id)
			break;
		ast_cli(fd, "%s", ast_getformatname(&tmpfmt));
		ast_cli(fd, ":%d", pref->framing[x]);
		if (x < 31 && ast_codec_pref_index(pref, x + 1, &tmpfmt))
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
		if (!strcmp(user->name, a->argv[3])) {
			break;
		} else {
			prev = user;
			user = user->next;
			ast_mutex_unlock(&prev->lock);
		}
	}

	if (user) {
		ast_cli(a->fd, "%-15.15s%s\n", "Name: ", user->name);
		ast_cli(a->fd, "%s:%s,%s\n", "FastStart/H.245 Tunneling", user->faststart?"yes":"no",
					user->h245tunneling?"yes":"no");
		ast_cli(a->fd, "%-15.15s%s", "Format Prefs: ", "(");
		print_codec_to_cli(a->fd, &user->prefs);
		ast_cli(a->fd, ")\n");
		ast_cli(a->fd, "%-15.15s", "DTMF Mode: ");
		if (user->dtmfmode & H323_DTMF_CISCO) {
			ast_cli(a->fd, "%s\n", "cisco");
			ast_cli(a->fd, "%-15.15s%d\n", "DTMF Codec: ", user->dtmfcodec);
		} else if (user->dtmfmode & H323_DTMF_RFC2833) {
			ast_cli(a->fd, "%s\n", "rfc2833");
			ast_cli(a->fd, "%-15.15s%d\n", "DTMF Codec: ", user->dtmfcodec);
		} else if (user->dtmfmode & H323_DTMF_Q931) {
			ast_cli(a->fd, "%s\n", "q931keypad");
		} else if (user->dtmfmode & H323_DTMF_H245ALPHANUMERIC) {
			ast_cli(a->fd, "%s\n", "h245alphanumeric");
		} else if (user->dtmfmode & H323_DTMF_H245SIGNAL) {
			ast_cli(a->fd, "%s\n", "h245signal");
		} else if (user->dtmfmode & H323_DTMF_INBAND && user->dtmfmode & H323_DTMF_INBANDRELAX) {
			ast_cli(a->fd, "%s\n", "inband-relaxed");
		} else if (user->dtmfmode & H323_DTMF_INBAND) {
			ast_cli(a->fd, "%s\n", "inband");
		} else {
			ast_cli(a->fd, "%s\n", "unknown");
		}
		ast_cli(a->fd,"%-15s", "T.38 Mode: ");
		if (user->t38support == T38_DISABLED) {
			ast_cli(a->fd, "%s\n", "disabled");
		} else if (user->t38support == T38_FAXGW) {
			ast_cli(a->fd, "%s\n", "faxgw/chan_sip compatible");
		}
		if (user->faxdetect == (FAXDETECT_CNG | FAXDETECT_T38)) {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "Yes");
		} else if (user->faxdetect & FAXDETECT_CNG) {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "Cng");
		} else if (user->faxdetect & FAXDETECT_T38) {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "T.38");
		} else {
			ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "No");
		}

		ast_cli(a->fd, "%-15.15s%s\n", "AccountCode: ", user->accountcode);
		ast_cli(a->fd, "%-15.15s%s\n", "AMA flags: ", ast_cdr_flags2str(user->amaflags));
		ast_cli(a->fd, "%-15.15s%s\n", "Context: ", user->context);
		ast_cli(a->fd, "%-15.15s%d\n", "IncomingLimit: ", user->incominglimit);
		ast_cli(a->fd, "%-15.15s%d\n", "InUse: ", user->inUse);
		ast_cli(a->fd, "%-15.15s%d\n", "rtptimeout: ", user->rtptimeout);
		if (user->rtpmaskstr[0]) {
			ast_cli(a->fd, "%-15.15s%s\n", "rtpmask: ", user->rtpmaskstr);
		}
		ast_mutex_unlock(&user->lock);
		if (user->rtdrcount && user->rtdrinterval) {
			ast_cli(a->fd, "%-15.15s%d,%d\n", "RoundTrip: ", user->rtdrcount, user->rtdrinterval);
		}
	} else {
		ast_cli(a->fd, "User %s not found\n", a->argv[3]);
		ast_cli(a->fd, "\n");
	}
	ast_mutex_unlock(&userl.lock);

	return CLI_SUCCESS;
}

static char *handle_cli_ooh323_show_users(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ooh323_user *prev = NULL, *user = NULL;
   char formats[FORMAT_STRING_SIZE];
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
   while(user)
   {
		ast_mutex_lock(&user->lock);
     		ast_cli(a->fd, FORMAT1, user->name, 
					user->accountcode, user->context,
					ast_getformatname_multiple(formats, FORMAT_STRING_SIZE, user->cap));
		prev = user;
		user = user->next;
		ast_mutex_unlock(&prev->lock);

	}
	ast_mutex_unlock(&userl.lock);
#undef FORMAT1
   return RESULT_SUCCESS;

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
	char value[FORMAT_STRING_SIZE];
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

	ast_cli(a->fd, "\nObjective Open H.323 Channel Driver's Config:\n");
	snprintf(value, sizeof(value), "%s:%d", gIP, gPort);
	ast_cli(a->fd, "%-20s%s\n", "IP:Port: ", value);
	ast_cli(a->fd, "%-20s%d-%d\n", "H.225 port range: ", ooconfig.mTCPPortStart, ooconfig.mTCPPortEnd);
	ast_cli(a->fd, "%-20s%s\n", "FastStart", gFastStart?"yes":"no");
	ast_cli(a->fd, "%-20s%s\n", "Tunneling", gTunneling?"yes":"no");
	ast_cli(a->fd, "%-20s%s\n", "CallerId", gCallerID);
	ast_cli(a->fd, "%-20s%s\n", "MediaWaitForConnect", gMediaWaitForConnect?"yes":"no");

#if (0)
		extern OOH323EndPoint gH323ep;
	ast_cli(a->fd, "%-20s%s\n", "FASTSTART",
		(OO_TESTFLAG(gH323ep.flags, OO_M_FASTSTART) != 0) ? "yes" : "no");
	ast_cli(a->fd, "%-20s%s\n", "TUNNELING",
		(OO_TESTFLAG(gH323ep.flags, OO_M_TUNNELING) != 0) ? "yes" : "no");
	ast_cli(a->fd, "%-20s%s\n", "MEDIAWAITFORCONN",
		(OO_TESTFLAG(gH323ep.flags, OO_M_MEDIAWAITFORCONN) != 0) ? "yes" : "no");
#endif

	if (gRasGkMode == RasNoGatekeeper) {
		snprintf(value, sizeof(value), "%s", "No Gatekeeper");
	} else if (gRasGkMode == RasDiscoverGatekeeper) {
		snprintf(value, sizeof(value), "%s", "Discover");
	} else {
		snprintf(value, sizeof(value), "%s", gGatekeeper);
	}
	ast_cli(a->fd,  "%-20s%s\n", "Gatekeeper:", value);
	ast_cli(a->fd,  "%-20s%s\n", "H.323 LogFile:", gLogFile);
	ast_cli(a->fd,  "%-20s%s\n", "Context:", gContext);
	ast_cli(a->fd,  "%-20s%s\n", "Capability:",
		ast_getformatname_multiple(value,FORMAT_STRING_SIZE,gCap));
	ast_cli(a->fd, "%-20s", "DTMF Mode: ");
	if (gDTMFMode & H323_DTMF_CISCO) {
		ast_cli(a->fd, "%s\n", "cisco");
		ast_cli(a->fd, "%-20.15s%d\n", "DTMF Codec: ", gDTMFCodec);
	} else if (gDTMFMode & H323_DTMF_RFC2833) {
		ast_cli(a->fd, "%s\n", "rfc2833");
		ast_cli(a->fd, "%-20.15s%d\n", "DTMF Codec: ", gDTMFCodec);
	} else if (gDTMFMode & H323_DTMF_Q931) {
		ast_cli(a->fd, "%s\n", "q931keypad");
	} else if (gDTMFMode & H323_DTMF_H245ALPHANUMERIC) {
		ast_cli(a->fd, "%s\n", "h245alphanumeric");
	} else if (gDTMFMode & H323_DTMF_H245SIGNAL) {
		ast_cli(a->fd, "%s\n", "h245signal");
	} else if (gDTMFMode & H323_DTMF_INBAND && gDTMFMode & H323_DTMF_INBANDRELAX) {
		ast_cli(a->fd, "%s\n", "inband-relaxed");
	} else if (gDTMFMode & H323_DTMF_INBAND) {
		ast_cli(a->fd, "%s\n", "inband");
	} else {
		ast_cli(a->fd, "%s\n", "unknown");
	}

	ast_cli(a->fd,"%-20s", "T.38 Mode: ");
	if (gT38Support == T38_DISABLED) {
		ast_cli(a->fd, "%s\n", "disabled");
	} else if (gT38Support == T38_FAXGW) {
		ast_cli(a->fd, "%s\n", "faxgw/chan_sip compatible");
	}
	if (gFAXdetect == (FAXDETECT_CNG | FAXDETECT_T38)) {
		ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "Yes");
	} else if (gFAXdetect & FAXDETECT_CNG) {
		ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "Cng");
	} else if (gFAXdetect & FAXDETECT_T38) {
		ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "T.38");
	} else {
		ast_cli(a->fd,"%-20s%s\n", "FAX Detect:", "No");
	}

	if (gRTDRCount && gRTDRInterval) {
		ast_cli(a->fd, "%-20.15s%d,%d\n", "RoundTrip: ", gRTDRCount, gRTDRInterval);
	}

	ast_cli(a->fd, "%-20s%ld\n", "Call counter: ", callnumber);
	ast_cli(a->fd, "%-20s%s\n", "AccountCode: ", gAccountcode);
	ast_cli(a->fd, "%-20s%s\n", "AMA flags: ", ast_cdr_flags2str(gAMAFLAGS));

	pAlias = gAliasList;
	if(pAlias) {
		ast_cli(a->fd, "%-20s\n", "Aliases: ");
	}
	while (pAlias) {
		pAliasNext = pAlias->next;
		if (pAliasNext) {
			ast_cli(a->fd,"\t%-30s\t%-30s\n",pAlias->value, pAliasNext->value);
			pAlias = pAliasNext->next;
		} else {
			ast_cli(a->fd,"\t%-30s\n",pAlias->value);
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
        AST_CLI_DEFINE(handle_cli_ooh323_reload, "reload ooh323 config")
};

/*! \brief OOH323 Dialplan function - reads ooh323 settings */
static int function_ooh323_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ooh323_pvt *p = chan->tech_pvt;

	ast_channel_lock(chan);
	if (!p) {
		ast_channel_unlock(chan);
		return -1;
	}

	if (strcmp(chan->tech->type, "OOH323")) {
		ast_log(LOG_ERROR, "This function is only supported on OOH323 channels, Channel is %s\n", chan->tech->type);
		ast_channel_unlock(chan);
		return -1;
	}

	ast_mutex_lock(&p->lock);
	if (!strcasecmp(data, "faxdetect")) {
		ast_copy_string(buf, p->faxdetect ? "1" : "0", len);
	} else if (!strcasecmp(data, "t38support")) {
		ast_copy_string(buf, p->t38support ? "1" : "0", len);
	} else if (!strcasecmp(data, "caller_h323id")) {
		ast_copy_string(buf, p->caller_h323id, len);
	} else if (!strcasecmp(data, "caller_dialeddigits")) {
		ast_copy_string(buf, p->caller_dialedDigits, len);
	} else if (!strcasecmp(data, "caller_email")) {
		ast_copy_string(buf, p->caller_email, len);
	} else if (!strcasecmp(data, "h323id_url")) {
		ast_copy_string(buf, p->caller_url, len);
	} else if (!strcasecmp(data, "callee_h323id")) {
		ast_copy_string(buf, p->callee_h323id, len);
	} else if (!strcasecmp(data, "callee_dialeddigits")) {
		ast_copy_string(buf, p->callee_dialedDigits, len);
	} else if (!strcasecmp(data, "callee_email")) {
		ast_copy_string(buf, p->callee_email, len);
	} else if (!strcasecmp(data, "callee_url")) {
		ast_copy_string(buf, p->callee_url, len);
	}
	ast_mutex_unlock(&p->lock);

	ast_channel_unlock(chan);
	return 0;
}

/*! \brief OOH323 Dialplan function - writes ooh323 settings */
static int function_ooh323_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ooh323_pvt *p = chan->tech_pvt;
	int res = -1;

	ast_channel_lock(chan);
	if (!p) {
		ast_channel_unlock(chan);
		return -1;
	}

	if (strcmp(chan->tech->type, "OOH323")) {
		ast_log(LOG_ERROR, "This function is only supported on OOH323 channels, Channel is %s\n", chan->tech->type);
		ast_channel_unlock(chan);
		return -1;
	}

	ast_mutex_lock(&p->lock);
	if (!strcasecmp(data, "faxdetect")) {
		if (ast_true(value)) {
			p->faxdetect = 1;
			res = 0;
		} else if (ast_false(value)) {
			p->faxdetect = 0;
			res = 0;
		} else {
			char *buf = ast_strdupa(value);
			char *word, *next = buf;
			p->faxdetect = 0;
			res = 0;
			while ((word = strsep(&next, ","))) {
				if (!strcasecmp(word, "cng")) {
					p->faxdetect |= FAXDETECT_CNG;
				} else if (!strcasecmp(word, "t38")) {
					p->faxdetect |= FAXDETECT_T38;
				} else {
					ast_log(LOG_WARNING, "Unknown faxdetect mode '%s'.\n", word);
					res = -1;
				}
			}

		}
	} else if (!strcasecmp(data, "t38support")) {
		if (ast_true(value)) {
			p->t38support = 1;
			res = 0;
		} else {
			p->t38support = 0;
			res = 0;
		}
	}
	ast_mutex_unlock(&p->lock);
	ast_channel_unlock(chan);

	return res;
}

/*! \brief Structure to declare a dialplan function: OOH323 */
static struct ast_custom_function ooh323_function = {
        .name = "OOH323",
        .read = function_ooh323_read,
        .write = function_ooh323_write,
};

static int load_module(void)
{
	int res;
	struct ooAliases * pNewAlias = NULL;
	struct ooh323_peer *peer = NULL;
	struct ast_format tmpfmt;
	OOH225MsgCallbacks h225Callbacks = {0, 0, 0, 0};

	OOH323CALLBACKS h323Callbacks = {
		.onNewCallCreated = onNewCallCreated,
		.onAlerting = onAlerting,
		.onProgress = onProgress,
		.onIncomingCall = NULL,
		.onOutgoingCall = onOutgoingCall,
		.onCallEstablished = onCallEstablished,
		.onCallCleared = onCallCleared,
		.openLogicalChannels = NULL,
		.onReceivedDTMF = ooh323_onReceivedDigit,
		.onModeChanged = onModeChanged
	};
	if (!(gCap = ast_format_cap_alloc())) {
		return 1; 
	}
	if (!(ooh323_tech.capabilities = ast_format_cap_alloc())) {
		return 1;
	}
	ast_format_cap_add(gCap, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
	ast_format_cap_add_all(ooh323_tech.capabilities);

	myself = ast_module_info->self;

	h225Callbacks.onReceivedSetup = &ooh323_onReceivedSetup;

	userl.users = NULL;
	ast_mutex_init(&userl.lock);
	peerl.peers = NULL;
	ast_mutex_init(&peerl.lock);
 
#if 0		
	ast_register_atexit(&ast_ooh323c_exit);
#endif

	if (!(sched = ast_sched_context_create())) {
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
		ast_rtp_glue_register(&ooh323_rtp);
		ast_udptl_proto_register(&ooh323_udptl);
		ast_cli_register_multiple(cli_ooh323, sizeof(cli_ooh323) / sizeof(struct ast_cli_entry));

		 /* fire up the H.323 Endpoint */		 
		if (OO_OK != ooH323EpInitialize(OO_CALLMODE_AUDIOCALL, gLogFile)) {
         ast_log(LOG_ERROR, "Failed to initialize OOH323 endpoint-"
                            "OOH323 Disabled\n");
			return 1;
		}

		if (gIsGateway)
			ooH323EpSetAsGateway();

      		ooH323EpSetVersionInfo(t35countrycode, t35extensions, manufacturer,
									 vendor, version);
		ooH323EpDisableAutoAnswer();
		ooH323EpSetH225MsgCallbacks(h225Callbacks);
      		ooH323EpSetTraceLevel(gTRCLVL);
		ooH323EpSetLocalAddress(gIP, gPort);
		if (v6mode) {
			ast_debug(1, "OOH323 channel is in IP6 mode\n");
		}
		ooH323EpSetCallerID(gCallerID);
 
      if(ooH323EpSetTCPPortRange(ooconfig.mTCPPortStart, 
                                 ooconfig.mTCPPortEnd) == OO_FAILED) {
         ast_log(LOG_ERROR, "h225portrange: Failed to set range\n");
      }

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
         default:
            ;
			}
		}

		ast_mutex_lock(&peerl.lock);
		peer = peerl.peers;
		while (peer) {
         if(peer->h323id) ooH323EpAddAliasH323ID(peer->h323id);
         if(peer->email)  ooH323EpAddAliasEmailID(peer->email);
         if(peer->e164)   ooH323EpAddAliasDialedDigits(peer->e164);
         if(peer->url)    ooH323EpAddAliasURLID(peer->url);
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

		if (gBeMaster)
			ooH323EpTryBeMaster(1);

      		ooH323EpEnableManualRingback();

		/* Gatekeeper */
		if (gRasGkMode == RasUseSpecificGatekeeper)
			ooGkClientInit(gRasGkMode, gGatekeeper, 0);
		else if (gRasGkMode == RasDiscoverGatekeeper)
			ooGkClientInit(gRasGkMode, 0, 0);

		/* Register callbacks */
		ooH323EpSetH323Callbacks(h323Callbacks);

		/* Add endpoint capabilities */
		if (ooh323c_set_capability(&gPrefs, gCap, gDTMFMode, gDTMFCodec) < 0) {
			ast_log(LOG_ERROR, "Capabilities failure for OOH323. OOH323 Disabled.\n");
			return 1;
		}
  
		/* Create H.323 listener */
		if (ooCreateH323Listener() != OO_OK) {
         ast_log(LOG_ERROR, "OOH323 Listener Creation failure. "
                            "OOH323 DISABLED\n");
		
			ooH323EpDestroy();
			return 1;
		}

		if (ooh323c_start_stack_thread() < 0) {
         ast_log(LOG_ERROR, "Failed to start OOH323 stack thread. "
                            "OOH323 DISABLED\n");
			ooH323EpDestroy();
			return 1;
		}
		/* And start the monitor for the first time */
		restart_monitor();
	}

	/* Register dialplan functions */
	ast_custom_function_register(&ooh323_function);

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
         } /* else if (ast_test_flag(h323, H323_NEEDSTART) && h323->owner) {
	  ast_channel_lock(h323->owner);
          if (ast_pbx_start(h323->owner)) {
            ast_log(LOG_WARNING, "Unable to start PBX on %s\n", h323->owner->name);
            ast_channel_unlock(h323->owner);
            ast_hangup(h323->owner);
          }
          ast_channel_unlock(h323->owner);
	  ast_clear_flag(h323, H323_NEEDSTART);
	 } */
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
	struct ooh323_user *user = NULL;

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
	 		if (gH323Debug) 
				ast_verbose(" Destroying %s\n", cur->callToken);
			ast_free(cur->callToken);
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
			ast_rtp_instance_destroy(cur->rtp);
			cur->rtp = NULL;
		}

		if (cur->udptl) {
			ast_udptl_destroy(cur->udptl);
			cur->udptl = NULL;
		}
	
		/* Unlink us from the owner if we have one */
		if (cur->owner) {
         		while(ast_channel_trylock(cur->owner)) {
            			ast_debug(1, "Failed to grab lock, trying again\n");
				DEADLOCK_AVOIDANCE(&cur->lock);
         		}           
			ast_debug(1, "Detaching from %s\n", cur->owner->name);
			cur->owner->tech_pvt = NULL;
			ast_channel_unlock(cur->owner);
			cur->owner = NULL;
			ast_module_unref(myself);
		}
  
		if (cur->vad) {
			ast_dsp_free(cur->vad);
			cur->vad = NULL;
		}

/* decrement user/peer count */

      if(!ast_test_flag(cur, H323_OUTGOING)) {
	 if (cur->neighbor.user) {
	  user = find_user(p->callerid_name, cur->neighbor.user);
	  if(user && user->inUse > 0) {
	  	ast_mutex_lock(&user->lock);
	  	user->inUse--;
	  	ast_mutex_unlock(&user->lock);
	  }
	  free(cur->neighbor.user);
	 }
      } else {
/* outgoing limit decrement here !!! */
      }

		ast_mutex_unlock(&cur->lock);
		ast_mutex_destroy(&cur->lock);
		cur->cap = ast_format_cap_destroy(cur->cap);
		ast_free(cur);
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
      if(prev->h323id)   free(prev->h323id);
      if(prev->email)    free(prev->email);
      if(prev->url)      free(prev->url);
      if(prev->e164)     free(prev->e164);
      if(prev->rtpmask) {
		ast_mutex_lock(&prev->rtpmask->lock);
		prev->rtpmask->inuse--;
		ast_mutex_unlock(&prev->rtpmask->lock);
	 	if (prev->rtpmask->inuse == 0) {
	  		regfree(&prev->rtpmask->regex);
			ast_mutex_destroy(&prev->rtpmask->lock);
	  		free(prev->rtpmask);
      		}
      }
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

      		if(prev->rtpmask) {
			ast_mutex_lock(&prev->rtpmask->lock);
			prev->rtpmask->inuse--;
			ast_mutex_unlock(&prev->rtpmask->lock);
	 		if (prev->rtpmask->inuse == 0) {
	  			regfree(&prev->rtpmask->regex);
				ast_mutex_destroy(&prev->rtpmask->lock);
	  			free(prev->rtpmask);
      			}
      		}
		prev->cap = ast_format_cap_destroy(prev->cap);
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
	ast_rtp_glue_unregister(&ooh323_rtp);
	ast_udptl_proto_unregister(&ooh323_udptl);
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

	/* Unregister dial plan functions */
	ast_custom_function_unregister(&ooh323_function);

	if (gH323Debug) {
		ast_verbose("+++ ooh323  unload_module \n");
	}

	gCap = ast_format_cap_destroy(gCap);
	ooh323_tech.capabilities = ast_format_cap_destroy(ooh323_tech.capabilities);
	return 0;
}



static enum ast_rtp_glue_result ooh323_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **rtp)
{
	struct ooh323_pvt *p = NULL;
	enum ast_rtp_glue_result res = AST_RTP_GLUE_RESULT_LOCAL;

	if (!(p = (struct ooh323_pvt *) chan->tech_pvt))
		return AST_RTP_GLUE_RESULT_FORBID;

	if (!(p->rtp)) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	*rtp = p->rtp ? ao2_ref(p->rtp, +1), p->rtp : NULL;

	res = AST_RTP_GLUE_RESULT_LOCAL;

	if (ast_test_flag(&global_jbconf, AST_JB_FORCED)) {
		res = AST_RTP_GLUE_RESULT_FORBID;
	}

	return res;
}

static enum ast_rtp_glue_result ooh323_get_vrtp_peer(struct ast_channel *chan, struct ast_rtp_instance **rtp)
{
	struct ooh323_pvt *p = NULL;
	enum ast_rtp_glue_result res = AST_RTP_GLUE_RESULT_LOCAL;

	if (!(p = (struct ooh323_pvt *) chan->tech_pvt))
		return AST_RTP_GLUE_RESULT_FORBID;

	if (!(p->rtp)) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	*rtp = p->vrtp ? ao2_ref(p->vrtp, +1), p->vrtp : NULL;
	res = AST_RTP_GLUE_RESULT_LOCAL;

	return res;
}


int ooh323_update_capPrefsOrderForCall
	(ooCallData *call, struct ast_codec_pref *prefs)
{
	int i = 0;
	struct ast_format tmpfmt;

	ast_codec_pref_index(prefs, i, &tmpfmt);

	ooResetCapPrefs(call);
	while (tmpfmt.id) {
		ooAppendCapToCapPrefs(call, ooh323_convertAsteriskCapToH323Cap(&tmpfmt));
		ast_codec_pref_index(prefs, ++i, &tmpfmt);
	}

	return 0;
}


int ooh323_convertAsteriskCapToH323Cap(struct ast_format *format)
{
	switch (format->id) {
	case AST_FORMAT_ULAW:
		return OO_G711ULAW64K;
	case AST_FORMAT_ALAW:
		return OO_G711ALAW64K;
	case AST_FORMAT_GSM:
		return OO_GSMFULLRATE;

#ifdef AST_FORMAT_AMRNB
	case AST_FORMAT_AMRNB:
		return OO_AMRNB;
#endif
#ifdef AST_FORMAT_SPEEX
	case AST_FORMAT_SPEEX:
		return OO_SPEEX;
#endif

	case AST_FORMAT_G729A:
		return OO_G729A;
	case AST_FORMAT_G726:
		return OO_G726;
	case AST_FORMAT_G726_AAL2:
		return OO_G726AAL2;
	case AST_FORMAT_G723_1:
		return OO_G7231;
	case AST_FORMAT_H263:
		return OO_H263VIDEO;
	default:
		ast_log(LOG_NOTICE, "Don't know how to deal with mode %s\n", ast_getformatname(format));
		return -1;
	}
}

static int ooh323_set_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance *rtp,
	 struct ast_rtp_instance *vrtp, struct ast_rtp_instance *trtp, const struct ast_format_cap *cap, int nat_active)
{
	/* XXX Deal with Video */
	struct ooh323_pvt *p;
	struct ast_sockaddr tmp;
	int mode;

	if (gH323Debug)
		ast_verbose("---   ooh323_set_peer - %s\n", chan->name);

	if (!rtp) {
		return 0;
	}

	mode = ooh323_convertAsteriskCapToH323Cap(&chan->writeformat); 
	p = (struct ooh323_pvt *) chan->tech_pvt;
	if (!p) {
		ast_log(LOG_ERROR, "No Private Structure, this is bad\n");
		return -1;
	}
	ast_rtp_instance_get_remote_address(rtp, &tmp);
	ast_rtp_instance_get_local_address(rtp, &tmp);
	return 0;

/* 	May 20101003 */
/*	What we should to do here? */


}




int configure_local_rtp(struct ooh323_pvt *p, ooCallData *call)
{
	char lhost[INET6_ADDRSTRLEN], *lport=NULL;
	struct ast_sockaddr tmp;
	ooMediaInfo mediaInfo;
	int x;
	struct ast_format tmpfmt;

	ast_format_clear(&tmpfmt);

	if (gH323Debug)
		ast_verbose("---   configure_local_rtp\n");


	if (ast_parse_arg(call->localIP, PARSE_ADDR, &tmp)) {
		ast_sockaddr_copy(&tmp, &bindaddr);
	}
	if (!(p->rtp = ast_rtp_instance_new("asterisk", sched, &tmp, NULL))) {
		ast_log(LOG_WARNING, "Unable to create RTP session: %s\n",
			strerror(errno));
		return 0;
	}

	ast_rtp_instance_set_qos(p->rtp, gTOS, 0, "ooh323-rtp");

	if (!(p->udptl = ast_udptl_new_with_bindaddr(sched, io, 0, &tmp))) {
		ast_log(LOG_WARNING, "Unable to create UDPTL session: %s\n",
			strerror(errno));
		return 0;
	}
	ast_udptl_set_far_max_datagram(p->udptl, 144);

	if (p->owner) {
		while (p->owner && ast_channel_trylock(p->owner)) {
			ast_debug(1,"Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		}
		if (!p->owner) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return 0;
		}
	} else {
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return 0;
	}

	ast_channel_set_fd(p->owner, 0, ast_rtp_instance_fd(p->rtp, 0));
	ast_channel_set_fd(p->owner, 1, ast_rtp_instance_fd(p->rtp, 1));
	ast_channel_set_fd(p->owner, 5, ast_udptl_fd(p->udptl));

	ast_channel_unlock(p->owner);

	if (p->rtp) {
		ast_rtp_codecs_packetization_set(ast_rtp_instance_get_codecs(p->rtp), p->rtp, &p->prefs);
		if (p->dtmfmode & H323_DTMF_RFC2833 && p->dtmfcodec) {
			ast_rtp_instance_set_prop(p->rtp, AST_RTP_PROPERTY_DTMF, 1);
			ast_rtp_codecs_payloads_set_rtpmap_type(ast_rtp_instance_get_codecs(p->rtp),
				 p->rtp, p->dtmfcodec, "audio", "telephone-event", 0);
		}
		if (p->dtmfmode & H323_DTMF_CISCO && p->dtmfcodec) {
			ast_rtp_instance_set_prop(p->rtp, AST_RTP_PROPERTY_DTMF, 1);
			ast_rtp_codecs_payloads_set_rtpmap_type(ast_rtp_instance_get_codecs(p->rtp),
				 p->rtp, p->dtmfcodec, "audio", "cisco-telephone-event", 0);
		}
		/* figure out our local RTP port and tell the H.323 stack about it*/
		ast_rtp_instance_get_local_address(p->rtp, &tmp);
		strncpy(lhost, ast_sockaddr_stringify_addr(&tmp), sizeof(lhost));
		lport = ast_sockaddr_stringify_port(&tmp);

		if (p->rtptimeout) {
			ast_rtp_instance_set_timeout(p->rtp, p->rtptimeout);
		}
		ast_rtp_instance_set_prop(p->rtp, AST_RTP_PROPERTY_RTCP, 1);
		
	}

	if (p->rtdrcount) {
		if (gH323Debug)
			ast_verbose("Setup RTDR info: %d, %d\n", p->rtdrinterval, p->rtdrcount);
		call->rtdrInterval = p->rtdrinterval;
		call->rtdrCount = p->rtdrcount;
	}


	ast_copy_string(mediaInfo.lMediaIP, lhost, sizeof(mediaInfo.lMediaIP));
	mediaInfo.lMediaPort = atoi(lport);
	mediaInfo.lMediaCntrlPort = mediaInfo.lMediaPort +1;
	for (x = 0; ast_codec_pref_index(&p->prefs, x, &tmpfmt); x++) {
		strcpy(mediaInfo.dir, "transmit");
		mediaInfo.cap = ooh323_convertAsteriskCapToH323Cap(&tmpfmt);
		ooAddMediaInfo(call, mediaInfo);
		strcpy(mediaInfo.dir, "receive");
		ooAddMediaInfo(call, mediaInfo);
		if (mediaInfo.cap == OO_G729A) {
			strcpy(mediaInfo.dir, "transmit");
			mediaInfo.cap = OO_G729;
			ooAddMediaInfo(call, mediaInfo);
			strcpy(mediaInfo.dir, "receive");
			ooAddMediaInfo(call, mediaInfo);

			strcpy(mediaInfo.dir, "transmit");
			mediaInfo.cap = OO_G729B;
			ooAddMediaInfo(call, mediaInfo);
			strcpy(mediaInfo.dir, "receive");
			ooAddMediaInfo(call, mediaInfo);
		}
	}

	if (p->udptl) {
		ast_udptl_get_us(p->udptl, &tmp);
		strncpy(lhost, ast_sockaddr_stringify_addr(&tmp), sizeof(lhost));
		lport = ast_sockaddr_stringify_port(&tmp);
		ast_copy_string(mediaInfo.lMediaIP, lhost, sizeof(mediaInfo.lMediaIP));
		mediaInfo.lMediaPort = atoi(lport);
		mediaInfo.lMediaCntrlPort = mediaInfo.lMediaPort +1;
		mediaInfo.cap = OO_T38;
		strcpy(mediaInfo.dir, "transmit");
		ooAddMediaInfo(call, mediaInfo);
		strcpy(mediaInfo.dir, "receive");
		ooAddMediaInfo(call, mediaInfo);
	}

	if (gH323Debug)
		ast_verbose("+++   configure_local_rtp\n");

	return 1;
}

void setup_rtp_connection(ooCallData *call, const char *remoteIp, 
								  int remotePort)
{
	struct ooh323_pvt *p = NULL;
	struct ast_sockaddr tmp;

	if (gH323Debug)
		ast_verbose("---   setup_rtp_connection %s:%d\n", remoteIp, remotePort);

	/* Find the call or allocate a private structure if call not found */
	p = find_call(call); 

	if (!p) {
		ast_log(LOG_ERROR, "Something is wrong: rtp\n");
		return;
	}

	ast_parse_arg(remoteIp, PARSE_ADDR, &tmp);
	ast_sockaddr_set_port(&tmp, remotePort);
	ast_rtp_instance_set_remote_address(p->rtp, &tmp);

	if (p->writeformat.id == AST_FORMAT_G726_AAL2) 
                ast_rtp_codecs_payloads_set_rtpmap_type(ast_rtp_instance_get_codecs(p->rtp), p->rtp, 2,
							"audio", "G726-32", AST_RTP_OPT_G726_NONSTANDARD);

	if(gH323Debug)
		ast_verbose("+++   setup_rtp_connection\n");

	return;
}

void close_rtp_connection(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;

   if(gH323Debug)
		ast_verbose("---   close_rtp_connection\n");

	p = find_call(call);
	if (!p) {
      ast_log(LOG_ERROR, "Couldn't find matching call to close rtp "
                         "connection\n");
		return;
	}
	ast_mutex_lock(&p->lock);
	if (p->rtp) {
		ast_rtp_instance_stop(p->rtp);
	}
	ast_mutex_unlock(&p->lock);

   if(gH323Debug)
		ast_verbose("+++   close_rtp_connection\n");

	return;
}

/*
 udptl handling functions
 */

static struct ast_udptl *ooh323_get_udptl_peer(struct ast_channel *chan)
{
	struct ooh323_pvt *p;
	struct ast_udptl *udptl = NULL;

	p = chan->tech_pvt;
	if (!p)
		return NULL;

	ast_mutex_lock(&p->lock);
	if (p->udptl)
		udptl = p->udptl;
	ast_mutex_unlock(&p->lock);
	return udptl;
}

static int ooh323_set_udptl_peer(struct ast_channel *chan, struct ast_udptl *udptl)
{
	struct ooh323_pvt *p;

	p = chan->tech_pvt;
	if (!p)
		return -1;
	ast_mutex_lock(&p->lock);

	if (udptl) {
		ast_udptl_get_peer(udptl, &p->udptlredirip);
	} else
		memset(&p->udptlredirip, 0, sizeof(p->udptlredirip));

	ast_mutex_unlock(&p->lock);
	/* free(callToken); */
	return 0;
}

void setup_udptl_connection(ooCallData *call, const char *remoteIp, 
								  int remotePort)
{
	struct ooh323_pvt *p = NULL;
	struct ast_sockaddr them;

	if (gH323Debug)
		ast_verbose("---   setup_udptl_connection\n");

	/* Find the call or allocate a private structure if call not found */
	p = find_call(call); 

	if (!p) {
		ast_log(LOG_ERROR, "Something is wrong: rtp\n");
		return;
	}

	ast_mutex_lock(&p->lock);
	if (p->owner) {
		while (p->owner && ast_channel_trylock(p->owner)) {
			ast_debug(1, "Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		}
		if (!p->owner) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return;
		}
	} else {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return;
	}

	ast_parse_arg(remoteIp, PARSE_ADDR, &them);
	ast_sockaddr_set_port(&them, remotePort);

	ast_udptl_set_peer(p->udptl, &them);
	ast_udptl_set_tag(p->udptl, "%s", p->owner->name);
	p->t38_tx_enable = 1;
	p->lastTxT38 = time(NULL);
	if (p->t38support == T38_ENABLED) {
		struct ast_control_t38_parameters parameters = { .request_response = 0 };
		parameters.request_response = AST_T38_NEGOTIATED;
		parameters.max_ifp = ast_udptl_get_far_max_ifp(p->udptl);
		parameters.rate = AST_T38_RATE_14400;
		ast_queue_control_data(p->owner, AST_CONTROL_T38_PARAMETERS, &parameters, sizeof(parameters));
	}
	if (gH323Debug)
		ast_debug(1, "Receiving UDPTL  %s:%s\n", ast_sockaddr_stringify_host(&them),
							ast_sockaddr_stringify_port(&them));

	ast_channel_unlock(p->owner);
	ast_mutex_unlock(&p->lock);

	if(gH323Debug)
		ast_verbose("+++   setup_udptl_connection\n");

	return;
}

void close_udptl_connection(ooCallData *call)
{
	struct ooh323_pvt *p = NULL;

   	if(gH323Debug)
		ast_verbose("---   close_udptl_connection\n");

	p = find_call(call);
	if (!p) {
      		ast_log(LOG_ERROR, "Couldn't find matching call to close udptl "
                         "connection\n");
		return;
	}
	ast_mutex_lock(&p->lock);
	if (p->owner) {
		while (p->owner && ast_channel_trylock(p->owner)) {
			ast_debug(1, "Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		}
		if (!p->owner) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return;
		}
	} else {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return;
	}

	p->t38_tx_enable = 0;
	if (p->t38support == T38_ENABLED) {
		struct ast_control_t38_parameters parameters = { .request_response = 0 };
		parameters.request_response = AST_T38_TERMINATED;
		ast_queue_control_data(p->owner, AST_CONTROL_T38_PARAMETERS, &parameters, sizeof(parameters));
	}

	ast_channel_unlock(p->owner);
	ast_mutex_unlock(&p->lock);

   	if(gH323Debug)
		ast_verbose("+++   close_udptl_connection\n");

	return;
}

/* end of udptl handling */

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
         ast_copy_string(p->callee_dialedDigits, psAlias->value, 
                                        sizeof(p->callee_dialedDigits));
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
	struct ast_frame *dfr = NULL;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	switch (ast->fdno) {
	case 0:
		f = ast_rtp_instance_read(p->rtp, 0);	/* RTP Audio */
		break;
	case 1:
		f = ast_rtp_instance_read(p->rtp, 1);	/* RTCP Control Channel */
		break;
	case 2:
		f = ast_rtp_instance_read(p->vrtp, 0);	/* RTP Video */
		break;
	case 3:
		f = ast_rtp_instance_read(p->vrtp, 1);	/* RTCP Control Channel for video */
		break;
	case 5:
		f = ast_udptl_read(p->udptl);		/* UDPTL t.38 data */
		if (gH323Debug) {
			 ast_debug(1, "Got UDPTL %d/%d len %d for %s\n",
				f->frametype, f->subclass.integer, f->datalen, ast->name);
		}
		break;

	default:
		f = &null_frame;
	}

	if (p->owner && !p->faxmode && (f->frametype == AST_FRAME_VOICE)) {
		/* We already hold the channel lock */
		if (!(ast_format_cap_iscompatible(p->owner->nativeformats, &f->subclass.format))) {
			ast_debug(1, "Oooh, voice format changed to %s\n", ast_getformatname(&f->subclass.format));
			ast_format_cap_set(p->owner->nativeformats, &f->subclass.format);
			ast_set_read_format(p->owner, &p->owner->readformat);
			ast_set_write_format(p->owner, &p->owner->writeformat);
		}
		if (((p->dtmfmode & H323_DTMF_INBAND) || (p->faxdetect & FAXDETECT_CNG)) && p->vad &&
		    (f->subclass.format.id == AST_FORMAT_SLINEAR || f->subclass.format.id == AST_FORMAT_ALAW ||
		     f->subclass.format.id == AST_FORMAT_ULAW)) {
			dfr = ast_frdup(f);
			dfr = ast_dsp_process(p->owner, p->vad, dfr);
		}
	} else {
		return f;
	}

	/* process INBAND DTMF*/
	if (dfr && (dfr->frametype == AST_FRAME_DTMF) && ((dfr->subclass.integer == 'f') || (dfr->subclass.integer == 'e'))) {
		ast_debug(1, "* Detected FAX Tone %s\n", (dfr->subclass.integer == 'e') ? "CED" : "CNG");
		/* Switch to T.38 ON CED*/
		if (!p->faxmode && !p->chmodepend && (dfr->subclass.integer == 'e') && (p->t38support != T38_DISABLED)) {
			if (gH323Debug)
				ast_verbose("request to change %s to t.38 because fax ced\n", p->callToken);
			p->chmodepend = 1;
			p->faxdetected = 1;
			ooRequestChangeMode(p->callToken, 1);
		} else if ((dfr->subclass.integer == 'f') && !p->faxdetected) {
			const char *target_context = S_OR(p->owner->macrocontext, p->owner->context);
			if ((strcmp(p->owner->exten, "fax")) &&
			    (ast_exists_extension(p->owner, target_context, "fax", 1,
		            S_COR(p->owner->caller.id.number.valid, p->owner->caller.id.number.str, NULL)))) {
				ast_verb(2, "Redirecting '%s' to fax extension due to CNG detection\n", p->owner->name);
				pbx_builtin_setvar_helper(p->owner, "FAXEXTEN", p->owner->exten);
				if (ast_async_goto(p->owner, target_context, "fax", 1)) {
					ast_log(LOG_NOTICE, "Failed to async goto '%s' into fax of '%s'\n", p->owner->name,target_context);
				}
				p->faxdetected = 1;
				if (dfr) {
					ast_frfree(dfr);
				}
				return &ast_null_frame;
			}
		}
	} else if (dfr && dfr->frametype == AST_FRAME_DTMF) {
		ast_debug(1, "* Detected inband DTMF '%c'\n", f->subclass.integer);
		ast_frfree(f);
		return dfr;
	}

	if (dfr) {
		ast_frfree(dfr);
	}
	return f;
}

void onModeChanged(ooCallData *call, int t38mode) {
        struct ooh323_pvt *p;

	p = find_call(call);
	if (!p) {
		ast_log(LOG_ERROR, "No matching call found for %s\n", call->callToken);
		return;
	}

	ast_mutex_lock(&p->lock);

	if (gH323Debug)
       		ast_debug(1, "change mode to %d for %s\n", t38mode, call->callToken);

	if (t38mode == p->faxmode) {
		if (gH323Debug)
			ast_debug(1, "mode for %s is already %d\n", call->callToken,
					t38mode);
		p->chmodepend = 0;
		ast_mutex_unlock(&p->lock);
		return;
	}

	if (p->owner) {
		while (p->owner && ast_channel_trylock(p->owner)) {
			ast_debug(1,"Failed to grab lock, trying again\n");
			DEADLOCK_AVOIDANCE(&p->lock);
		}
		if (!p->owner) {
			p->chmodepend = 0;
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Channel has no owner\n");
			return;
		}
	} else {
		p->chmodepend = 0;
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return;
	}

	if (t38mode) {


		if (p->t38support == T38_ENABLED) {
			struct ast_control_t38_parameters parameters = { .request_response = 0 };

			if ((p->faxdetect & FAXDETECT_T38) && !p->faxdetected) {
                       		const char *target_context;
				ast_debug(1, "* Detected T.38 Request\n");
				target_context = S_OR(p->owner->macrocontext, p->owner->context);
                        	if ((strcmp(p->owner->exten, "fax")) &&
                            		(ast_exists_extension(p->owner, target_context, "fax", 1,
                            		S_COR(p->owner->caller.id.number.valid, p->owner->caller.id.number.str, NULL)))) {
                                	ast_verb(2, "Redirecting '%s' to fax extension due to CNG detection\n", p->owner->name);
                                	pbx_builtin_setvar_helper(p->owner, "FAXEXTEN", p->owner->exten);
                                	if (ast_async_goto(p->owner, target_context, "fax", 1)) {
                                        	ast_log(LOG_NOTICE, "Failed to async goto '%s' into fax of '%s'\n", p->owner->name,target_context);
					}
                                }
                                p->faxdetected = 1;
			}

/* AST_T38_CONTROL mode */

			parameters.request_response = AST_T38_REQUEST_NEGOTIATE;
			if (call->T38FarMaxDatagram) {
				ast_udptl_set_far_max_datagram(p->udptl, call->T38FarMaxDatagram);
			} else {
				ast_udptl_set_far_max_datagram(p->udptl, 144);
			}
			if (call->T38Version) {
				parameters.version = call->T38Version;
			}
			parameters.max_ifp = ast_udptl_get_far_max_ifp(p->udptl);
			parameters.rate = AST_T38_RATE_14400;
			ast_queue_control_data(p->owner, AST_CONTROL_T38_PARAMETERS, 
							&parameters, sizeof(parameters));
			p->faxmode = 1;


		}
	} else {
		if (p->t38support == T38_ENABLED) {
			struct ast_control_t38_parameters parameters = { .request_response = 0 };
			parameters.request_response = AST_T38_REQUEST_TERMINATE;
			parameters.max_ifp = ast_udptl_get_far_max_ifp(p->udptl);
			parameters.rate = AST_T38_RATE_14400;
			ast_queue_control_data(p->owner, AST_CONTROL_T38_PARAMETERS, 
							&parameters, sizeof(parameters));
		}
		p->faxmode = 0;
		p->faxdetected = 0;
		p->t38_init = 0;
	}

	p->chmodepend = 0;
	ast_channel_unlock(p->owner);
	ast_mutex_unlock(&p->lock);
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
