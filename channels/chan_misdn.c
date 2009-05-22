/*
 * Asterisk -- An open source telephony toolkit.
 * 
 * Copyright (C) 2004 - 2006, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
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
 *
 */

/*!
 * \file
 *
 * \brief the chan_misdn channel driver for Asterisk
 *
 * \author Christian Richter <crich@beronet.com>
 *
 * \extref MISDN http://www.misdn.org/
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>isdnnet</depend>
	<depend>misdn</depend>
	<depend>suppserv</depend>
 ***/
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/file.h>
#include <semaphore.h>

#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/io.h"
#include "asterisk/frame.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"
#include "asterisk/musiconhold.h"
#include "asterisk/dsp.h"
#include "asterisk/translate.h"
#include "asterisk/config.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/indications.h"
#include "asterisk/app.h"
#include "asterisk/features.h"
#include "asterisk/term.h"
#include "asterisk/sched.h"
#include "asterisk/stringfields.h"
#include "asterisk/causes.h"

#include "chan_misdn_config.h"
#include "isdn_lib.h"

char global_tracefile[BUFFERSIZE + 1];

static int g_config_initialized = 0;

struct misdn_jb{
	int size;
	int upper_threshold;
	char *samples, *ok;
	int wp,rp;
	int state_empty;
	int state_full;
	int state_buffer;
	int bytes_wrote;
	ast_mutex_t mutexjb;
};



/*! \brief allocates the jb-structure and initialize the elements */
struct misdn_jb *misdn_jb_init(int size, int upper_threshold);

/*! \brief frees the data and destroys the given jitterbuffer struct */
void misdn_jb_destroy(struct misdn_jb *jb);

/*! \brief fills the jitterbuffer with len data returns < 0 if there was an
error (buffer overrun). */
int misdn_jb_fill(struct misdn_jb *jb, const char *data, int len);

/*! \brief gets len bytes out of the jitterbuffer if available, else only the
available data is returned and the return value indicates the number
of data. */
int misdn_jb_empty(struct misdn_jb *jb, char *data, int len);


/* BEGIN: chan_misdn.h */

ast_mutex_t release_lock;

enum misdn_chan_state {
	MISDN_NOTHING=0,	/*!< at beginning */
	MISDN_WAITING4DIGS, /*!<  when waiting for infos */
	MISDN_EXTCANTMATCH, /*!<  when asterisk couldn't match our ext */
	MISDN_INCOMING_SETUP, /*!<  for incoming setups*/
	MISDN_DIALING, /*!<  when pbx_start */
	MISDN_PROGRESS, /*!<  we got a progress */
	MISDN_PROCEEDING, /*!<  we got a progress */
	MISDN_CALLING, /*!<  when misdn_call is called */
	MISDN_CALLING_ACKNOWLEDGE, /*!<  when we get SETUP_ACK */
	MISDN_ALERTING, /*!<  when Alerting */
	MISDN_BUSY, /*!<  when BUSY */
	MISDN_CONNECTED, /*!<  when connected */
	MISDN_PRECONNECTED, /*!<  when connected */
	MISDN_DISCONNECTED, /*!<  when connected */
	MISDN_RELEASED, /*!<  when connected */
	MISDN_BRIDGED, /*!<  when bridged */
	MISDN_CLEANING, /*!< when hangup from * but we were connected before */
	MISDN_HUNGUP_FROM_MISDN, /*!< when DISCONNECT/RELEASE/REL_COMP  came from misdn */
	MISDN_HUNGUP_FROM_AST, /*!< when DISCONNECT/RELEASE/REL_COMP came out of misdn_hangup */
	MISDN_HOLDED, /*!< if this chan is holded */
	MISDN_HOLD_DISCONNECT, /*!< if this chan is holded */
  
};

#define ORG_AST 1
#define ORG_MISDN 2

struct hold_info {
	/*!
	 * \brief Logical port the channel call record is HOLDED on 
	 * because the B channel is no longer associated. 
	 */
	int port;

	/*!
	 * \brief Original B channel number the HOLDED call was using. 
	 * \note Used only for debug display messages.
	 */
	int channel;
};

/*!
 * \brief Channel call record structure
 */
struct chan_list {
	/*! 
	 * \brief The "allowed_bearers" string read in from /etc/asterisk/misdn.conf
	 */
	char allowed_bearers[BUFFERSIZE + 1];
	
	/*! 
	 * \brief State of the channel
	 */
	enum misdn_chan_state state;

	/*! 
	 * \brief TRUE if a hangup needs to be queued 
	 * \note This is a debug flag only used to catch calls to hangup_chan() that are already hungup.
	 */
	int need_queue_hangup;

	/*!
	 * \brief TRUE if a channel can be hung up by calling asterisk directly when done.
	 */
	int need_hangup;

	/*!
	 * \brief TRUE if we could send an AST_CONTROL_BUSY if needed.
	 */
	int need_busy;
	
	/*!
	 * \brief Who originally created this channel. ORG_AST or ORG_MISDN
	 */
	int originator;

	/*! 
	 * \brief TRUE of we are not to respond immediately to a SETUP message.  Check the dialplan first.
	 * \note The "noautorespond_on_setup" boolean read in from /etc/asterisk/misdn.conf
	 */
	int noautorespond_on_setup;
	
	int norxtone;	/* Boolean assigned values but the value is not used. */

	/*!
	 * \brief TRUE if we are not to generate tones (Playtones)
	 */
	int notxtone; 

	/*!
	 * \brief TRUE if echo canceller is enabled.  Value is toggled.
	 */
	int toggle_ec;
	
	/*!
	 * \brief TRUE if you want to send Tone Indications to an incoming
	 * ISDN channel on a TE Port.
	 * \note The "incoming_early_audio" boolean read in from /etc/asterisk/misdn.conf
	 */
	int incoming_early_audio;

	/*!
	 * \brief TRUE if DTMF digits are to be passed inband only.
	 * \note It is settable by the misdn_set_opt() application.
	 */
	int ignore_dtmf;

	/*!
	 * \brief Pipe file descriptor handles array. 
	 * Read from pipe[0], write to pipe[1] 
	 */
	int pipe[2];

	/*!
	 * \brief Read buffer for inbound audio from pipe[0]
	 */
	char ast_rd_buf[4096];

	/*!
	 * \brief Inbound audio frame returned by misdn_read().
	 */
	struct ast_frame frame;

	/*!
	 * \brief Fax detection option. (0:no 1:yes 2:yes+nojump)
	 * \note The "faxdetect" option string read in from /etc/asterisk/misdn.conf
	 * \note It is settable by the misdn_set_opt() application.
	 */
	int faxdetect;

	/*!
	 * \brief Number of seconds to detect a Fax machine when detection enabled.
	 * \note 0 disables the timeout.
	 * \note The "faxdetect_timeout" value read in from /etc/asterisk/misdn.conf
	 */
	int faxdetect_timeout;

	/*!
	 * \brief Starting time of fax detection with timeout when nonzero.
	 */
	struct timeval faxdetect_tv;

	/*!
	 * \brief TRUE if a fax has been detected.
	 */
	int faxhandled;

	/*!
	 * \brief TRUE if we will use the Asterisk DSP to detect DTMF/Fax
	 * \note The "astdtmf" boolean read in from /etc/asterisk/misdn.conf
	 */
	int ast_dsp;

	/*!
	 * \brief Jitterbuffer length
	 * \note The "jitterbuffer" value read in from /etc/asterisk/misdn.conf
	 */
	int jb_len;

	/*!
	 * \brief Jitterbuffer upper threshold
	 * \note The "jitterbuffer_upper_threshold" value read in from /etc/asterisk/misdn.conf
	 */
	int jb_upper_threshold;

	/*!
	 * \brief Allocated jitterbuffer controller
	 * \note misdn_jb_init() creates the jitterbuffer.
	 * \note Must use misdn_jb_destroy() to clean up. 
	 */
	struct misdn_jb *jb;
	
	/*!
	 * \brief Allocated DSP controller
	 * \note ast_dsp_new() creates the DSP controller.
	 * \note Must use ast_dsp_free() to clean up. 
	 */
	struct ast_dsp *dsp;

	/*!
	 * \brief Allocated audio frame sample translator
	 * \note ast_translator_build_path() creates the translator path.
	 * \note Must use ast_translator_free_path() to clean up. 
	 */
	struct ast_trans_pvt *trans;
  
	/*!
	 * \brief Associated Asterisk channel structure.
	 */
	struct ast_channel * ast;

	//int dummy;	/* Not used */
  
	/*!
	 * \brief Associated B channel structure.
	 */
	struct misdn_bchannel *bc;

	/*!
	 * \brief HOLDED channel information
	 */
	struct hold_info hold_info;

	/*! 
	 * \brief From associated B channel: Layer 3 process ID 
	 * \note Used to find the HOLDED channel call record when retrieving a call. 
	 */
	unsigned int l3id;

	/*! 
	 * \brief From associated B channel: B Channel mISDN driver layer ID from mISDN_get_layerid()
	 * \note Used only for debug display messages.
	 */
	int addr;

	/*!
	 * \brief Incoming call dialplan context identifier.
	 * \note The "context" string read in from /etc/asterisk/misdn.conf
	 */
	char context[AST_MAX_CONTEXT];

	/*!
	 * \brief The configured music-on-hold class to use for this call.
	 * \note The "musicclass" string read in from /etc/asterisk/misdn.conf
	 */
	char mohinterpret[MAX_MUSICCLASS];

	//int zero_read_cnt;	/* Not used */

	/*!
	 * \brief Number of outgoing audio frames dropped since last debug gripe message.
	 */
	int dropped_frame_cnt;

	/*!
	 * \brief TRUE if we must do the ringback tones.
	 * \note The "far_alerting" boolean read in from /etc/asterisk/misdn.conf
	 */
	int far_alerting;

	/*!
	 * \brief TRUE if NT should disconnect an overlap dialing call when a timeout occurs.
	 * \note The "nttimeout" boolean read in from /etc/asterisk/misdn.conf
	 */
	int nttimeout;

	/*!
	 * \brief Other channel call record PID 
	 * \note Value imported from Asterisk environment variable MISDN_PID 
	 */
	int other_pid;

	/*!
	 * \brief Bridged other channel call record
	 * \note Pointer set when other_pid imported from Asterisk environment 
	 * variable MISDN_PID by either side.
	 */
	struct chan_list *other_ch;

	/*!
	 * \brief Tone zone sound used for dialtone generation.
	 * \note Used as a boolean.  Non-NULL to prod generation if enabled. 
	 */
	const struct tone_zone_sound *ts;
	
	/*!
	 * \brief Enables overlap dialing for the set amount of seconds.  (0 = Disabled)
	 * \note The "overlapdial" value read in from /etc/asterisk/misdn.conf
	 */
	int overlap_dial;

	/*!
	 * \brief Overlap dialing timeout Task ID.  -1 if not running.
	 */
	int overlap_dial_task;

	/*!
	 * \brief overlap_tv access lock.
	 */
	ast_mutex_t overlap_tv_lock;

	/*!
	 * \brief Overlap timer start time.  Timer restarted for every digit received.
	 */
	struct timeval overlap_tv;
  
	//struct chan_list *peer;	/* Not used */

	/*!
	 * \brief Next channel call record in the list.
	 */
	struct chan_list *next;
	//struct chan_list *prev;		/* Not used */
	//struct chan_list *first;	/* Not used */
};



void export_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);
void import_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);

struct robin_list {
	char *group;
	int port;
	int channel;
	struct robin_list *next;
	struct robin_list *prev;
};
static struct robin_list *robin = NULL;



static struct ast_frame *process_ast_dsp(struct chan_list *tmp, struct ast_frame *frame);



static inline void free_robin_list_r (struct robin_list *r)
{
	if (r) {
		if (r->next)
			free_robin_list_r(r->next);
		if (r->group)
			free(r->group);
		free(r);
	}
}

static void free_robin_list ( void )
{
	free_robin_list_r(robin);
	robin = NULL;
}

static struct robin_list* get_robin_position (char *group) 
{
	struct robin_list *new;
	struct robin_list *iter = robin;
	for (; iter; iter = iter->next) {
		if (!strcasecmp(iter->group, group))
			return iter;
	}
	new = (struct robin_list *) calloc(1, sizeof(struct robin_list));
	new->group = strndup(group, strlen(group));
	new->port = 0;
	new->channel = 0;
	if (robin) {
		new->next = robin;
		robin->prev = new;
	}
	robin = new;
	return robin;
}


/*! \brief the main schedule context for stuff like l1 watcher, overlap dial, ... */
static struct sched_context *misdn_tasks = NULL;
static pthread_t misdn_tasks_thread;

static int *misdn_ports;

static void chan_misdn_log(int level, int port, char *tmpl, ...)
	__attribute__((format(printf, 3, 4)));

static struct ast_channel *misdn_new(struct chan_list *cl, int state,  char *exten, char *callerid, int format, int port, int c);
static void send_digit_to_chan(struct chan_list *cl, char digit );

static void hangup_chan(struct chan_list *ch);
static int pbx_start_chan(struct chan_list *ch);

#define MISDN_ASTERISK_TECH_PVT(ast) ast->tech_pvt
#define MISDN_ASTERISK_PVT(ast) 1

#include "asterisk/strings.h"

/* #define MISDN_DEBUG 1 */

static const char misdn_type[] = "mISDN";

static int tracing = 0 ;

/*! \brief Only alaw and mulaw is allowed for now */
static int prefformat =  AST_FORMAT_ALAW ; /*  AST_FORMAT_SLINEAR ;  AST_FORMAT_ULAW | */

static int *misdn_debug;
static int *misdn_debug_only;
static int max_ports;

static int *misdn_in_calls;
static int *misdn_out_calls;


struct chan_list dummy_cl;

/*!
 * \brief Global channel call record list head.
 */
struct chan_list *cl_te=NULL;
ast_mutex_t cl_te_lock;

static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data);

static void send_cause2ast(struct ast_channel *ast, struct misdn_bchannel*bc, struct chan_list *ch);

static void cl_queue_chan(struct chan_list **list, struct chan_list *chan);
static void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan);
static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc);
static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid);



static int dialtone_indicate(struct chan_list *cl);
static int hanguptone_indicate(struct chan_list *cl);
static int stop_indicate(struct chan_list *cl);

static int start_bc_tones(struct chan_list *cl);
static int stop_bc_tones(struct chan_list *cl);
static void release_chan(struct misdn_bchannel *bc);

static int misdn_check_l2l1(struct ast_channel *chan, void *data);
static int misdn_set_opt_exec(struct ast_channel *chan, void *data);
static int misdn_facility_exec(struct ast_channel *chan, void *data);

int chan_misdn_jb_empty(struct misdn_bchannel *bc, char *buf, int len);


void debug_numplan(int port, int numplan, char *type);


int add_out_calls(int port);
int add_in_calls(int port);


#ifdef MISDN_1_2
static int update_pipeline_config(struct misdn_bchannel *bc);
#else
static int update_ec_config(struct misdn_bchannel *bc);
#endif



/*************** Helpers *****************/

static struct chan_list * get_chan_by_ast(struct ast_channel *ast)
{
	struct chan_list *tmp;
  
	for (tmp=cl_te; tmp; tmp = tmp->next) {
		if ( tmp->ast == ast ) return tmp;
	}
  
	return NULL;
}

static struct chan_list * get_chan_by_ast_name(char *name)
{
	struct chan_list *tmp;
  
	for (tmp=cl_te; tmp; tmp = tmp->next) {
		if ( tmp->ast  && strcmp(tmp->ast->name,name) == 0) return tmp;
	}
  
	return NULL;
}



struct allowed_bearers {
	char *name;			/*!< Bearer capability name string used in /etc/misdn.conf allowed_bearers */
	char *display;		/*!< Bearer capability displayable name */
	int cap;			/*!< SETUP message bearer capability field code value */
	int deprecated;		/*!< TRUE if this entry is deprecated. (Misspelled or bad name to use) */
};

/* *INDENT-OFF* */
static const struct allowed_bearers allowed_bearers_array[]= {
	/* Name,                      Displayable Name       Bearer Capability,                    Deprecated */
	{ "speech",                  "Speech",               INFO_CAPABILITY_SPEECH,               0 },
	{ "3_1khz",                  "3.1KHz Audio",         INFO_CAPABILITY_AUDIO_3_1K,           0 },
	{ "digital_unrestricted",    "Unrestricted Digital", INFO_CAPABILITY_DIGITAL_UNRESTRICTED, 0 },
	{ "digital_restricted",      "Restricted Digital",   INFO_CAPABILITY_DIGITAL_RESTRICTED,   0 },
	{ "digital_restriced",       "Restricted Digital",   INFO_CAPABILITY_DIGITAL_RESTRICTED,   1 }, /* Allow misspelling for backwards compatibility */
	{ "video",                   "Video",                INFO_CAPABILITY_VIDEO,                0 }
};
/* *INDENT-ON* */

static const char *bearer2str(int cap)
{
	unsigned index;

	for (index = 0; index < ARRAY_LEN(allowed_bearers_array); ++index) {
		if (allowed_bearers_array[index].cap == cap) {
			return allowed_bearers_array[index].display;
		}
	}	/* end for */

	return "Unknown Bearer";
}


static void print_facility(struct FacParm *fac, struct misdn_bchannel *bc)
{
	switch (fac->Function) {
	case Fac_CD:
		chan_misdn_log(1,bc->port," --> calldeflect to: %s, presentable: %s\n", fac->u.CDeflection.DeflectedToNumber,
			fac->u.CDeflection.PresentationAllowed ? "yes" : "no");
		break;
	case Fac_AOCDCurrency:
		if (fac->u.AOCDcur.chargeNotAvailable)
			chan_misdn_log(1,bc->port," --> AOCD currency: charge not available\n");
		else if (fac->u.AOCDcur.freeOfCharge)
			chan_misdn_log(1,bc->port," --> AOCD currency: free of charge\n");
		else if (fac->u.AOCDchu.billingId >= 0)
			chan_misdn_log(1,bc->port," --> AOCD currency: currency:%s amount:%d multiplier:%d typeOfChargingInfo:%s billingId:%d\n",
				fac->u.AOCDcur.currency, fac->u.AOCDcur.currencyAmount, fac->u.AOCDcur.multiplier,
				(fac->u.AOCDcur.typeOfChargingInfo == 0) ? "subTotal" : "total", fac->u.AOCDcur.billingId);
		else
			chan_misdn_log(1,bc->port," --> AOCD currency: currency:%s amount:%d multiplier:%d typeOfChargingInfo:%s\n",
				fac->u.AOCDcur.currency, fac->u.AOCDcur.currencyAmount, fac->u.AOCDcur.multiplier,
				(fac->u.AOCDcur.typeOfChargingInfo == 0) ? "subTotal" : "total");
		break;
	case Fac_AOCDChargingUnit:
		if (fac->u.AOCDchu.chargeNotAvailable)
			chan_misdn_log(1,bc->port," --> AOCD charging unit: charge not available\n");
		else if (fac->u.AOCDchu.freeOfCharge)
			chan_misdn_log(1,bc->port," --> AOCD charging unit: free of charge\n");
		else if (fac->u.AOCDchu.billingId >= 0)
			chan_misdn_log(1,bc->port," --> AOCD charging unit: recordedUnits:%d typeOfChargingInfo:%s billingId:%d\n",
				fac->u.AOCDchu.recordedUnits, (fac->u.AOCDchu.typeOfChargingInfo == 0) ? "subTotal" : "total", fac->u.AOCDchu.billingId);
		else
			chan_misdn_log(1,bc->port," --> AOCD charging unit: recordedUnits:%d typeOfChargingInfo:%s\n",
				fac->u.AOCDchu.recordedUnits, (fac->u.AOCDchu.typeOfChargingInfo == 0) ? "subTotal" : "total");
		break;
	default:
		chan_misdn_log(1,bc->port," --> unknown facility\n");
		break;
	}
}

static void print_bearer(struct misdn_bchannel *bc) 
{
	
	chan_misdn_log(2, bc->port, " --> Bearer: %s\n",bearer2str(bc->capability));
	
	switch(bc->law) {
	case INFO_CODEC_ALAW:
		chan_misdn_log(2, bc->port, " --> Codec: Alaw\n");
		break;
	case INFO_CODEC_ULAW:
		chan_misdn_log(2, bc->port, " --> Codec: Ulaw\n");
		break;
	}
}

static void export_aoc_vars(int originator, struct ast_channel *ast, struct misdn_bchannel *bc)
{
	char buf[128];

	if (!ast)
		return;

	if (originator == ORG_AST) {
		ast = ast_bridged_channel(ast);
		if (!ast)
			return;
	}

	switch (bc->AOCDtype) {
	case Fac_AOCDCurrency:
		pbx_builtin_setvar_helper(ast, "AOCD_Type", "currency");
		if (bc->AOCD.currency.chargeNotAvailable)
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "no");
		else {
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "yes");
			if (bc->AOCD.currency.freeOfCharge)
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "yes");
			else {
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "no");
				if (snprintf(buf, sizeof(buf), "%d %s", bc->AOCD.currency.currencyAmount * bc->AOCD.currency.multiplier, bc->AOCD.currency.currency) < sizeof(buf)) {
					pbx_builtin_setvar_helper(ast, "AOCD_Amount", buf);
					if (bc->AOCD.currency.billingId >= 0 && snprintf(buf, sizeof(buf), "%d", bc->AOCD.currency.billingId) < sizeof(buf))
						pbx_builtin_setvar_helper(ast, "AOCD_BillingId", buf);
				}
			}
		}
		break;
	case Fac_AOCDChargingUnit:
		pbx_builtin_setvar_helper(ast, "AOCD_Type", "charging_unit");
		if (bc->AOCD.chargingUnit.chargeNotAvailable)
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "no");
		else {
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "yes");
			if (bc->AOCD.chargingUnit.freeOfCharge)
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "yes");
			else {
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "no");
				if (snprintf(buf, sizeof(buf), "%d", bc->AOCD.chargingUnit.recordedUnits) < sizeof(buf)) {
					pbx_builtin_setvar_helper(ast, "AOCD_RecordedUnits", buf);
					if (bc->AOCD.chargingUnit.billingId >= 0 && snprintf(buf, sizeof(buf), "%d", bc->AOCD.chargingUnit.billingId) < sizeof(buf))
						pbx_builtin_setvar_helper(ast, "AOCD_BillingId", buf);
				}
			}
		}
		break;
	default:
		break;
	}
}

/*************** Helpers END *************/

static void sighandler(int sig)
{}

static void* misdn_tasks_thread_func (void *data)
{
	int wait;
	struct sigaction sa;

	sa.sa_handler = sighandler;
	sa.sa_flags = SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGUSR1);
	sigaction(SIGUSR1, &sa, NULL);
	
	sem_post((sem_t *)data);

	while (1) {
		wait = ast_sched_wait(misdn_tasks);
		if (wait < 0)
			wait = 8000;
		if (poll(NULL, 0, wait) < 0)
			chan_misdn_log(4, 0, "Waking up misdn_tasks thread\n");
		ast_sched_runq(misdn_tasks);
	}
	return NULL;
}

static void misdn_tasks_init (void)
{
	sem_t blocker;
	int i = 5;

	if (sem_init(&blocker, 0, 0)) {
		perror("chan_misdn: Failed to initialize semaphore!");
		exit(1);
	}

	chan_misdn_log(4, 0, "Starting misdn_tasks thread\n");
	
	misdn_tasks = sched_context_create();
	pthread_create(&misdn_tasks_thread, NULL, misdn_tasks_thread_func, &blocker);

	while (sem_wait(&blocker) && --i);
	sem_destroy(&blocker);
}

static void misdn_tasks_destroy (void)
{
	if (misdn_tasks) {
		chan_misdn_log(4, 0, "Killing misdn_tasks thread\n");
		if ( pthread_cancel(misdn_tasks_thread) == 0 ) {
			cb_log(4, 0, "Joining misdn_tasks thread\n");
			pthread_join(misdn_tasks_thread, NULL);
		}
		sched_context_destroy(misdn_tasks);
	}
}

static inline void misdn_tasks_wakeup (void)
{
	pthread_kill(misdn_tasks_thread, SIGUSR1);
}

static inline int _misdn_tasks_add_variable (int timeout, ast_sched_cb callback, const void *data, int variable)
{
	int task_id;

	if (!misdn_tasks) {
		misdn_tasks_init();
	}
	task_id = ast_sched_add_variable(misdn_tasks, timeout, callback, data, variable);
	misdn_tasks_wakeup();

	return task_id;
}

static int misdn_tasks_add (int timeout, ast_sched_cb callback, const void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 0);
}

static int misdn_tasks_add_variable (int timeout, ast_sched_cb callback, const void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 1);
}

static void misdn_tasks_remove (int task_id)
{
	AST_SCHED_DEL(misdn_tasks, task_id);
}

static int misdn_l1_task (const void *data)
{
	misdn_lib_isdn_l1watcher(*(int *)data);
	chan_misdn_log(5, *(int *)data, "L1watcher timeout\n");
	return 1;
}

static int misdn_overlap_dial_task (const void *data)
{
	struct timeval tv_end, tv_now;
	int diff;
	struct chan_list *ch = (struct chan_list *)data;

	chan_misdn_log(4, ch->bc->port, "overlap dial task, chan_state: %d\n", ch->state);

	if (ch->state != MISDN_WAITING4DIGS) {
		ch->overlap_dial_task = -1;
		return 0;
	}
	
	ast_mutex_lock(&ch->overlap_tv_lock);
	tv_end = ch->overlap_tv;
	ast_mutex_unlock(&ch->overlap_tv_lock);
	
	tv_end.tv_sec += ch->overlap_dial;
	tv_now = ast_tvnow();

	diff = ast_tvdiff_ms(tv_end, tv_now);

	if (diff <= 100) {
		char *dad=ch->bc->dad, sexten[]="s";
		/* if we are 100ms near the timeout, we are satisfied.. */
		stop_indicate(ch);
		
		if (ast_strlen_zero(ch->bc->dad)) {
			dad=sexten;
			strcpy(ch->ast->exten, sexten);
		}

		if (ast_exists_extension(ch->ast, ch->context, dad, 1, ch->bc->oad)) {
			ch->state=MISDN_DIALING;
			if (pbx_start_chan(ch) < 0) {
				chan_misdn_log(-1, ch->bc->port, "ast_pbx_start returned < 0 in misdn_overlap_dial_task\n");
				goto misdn_overlap_dial_task_disconnect;
			}
		} else {
misdn_overlap_dial_task_disconnect:
			hanguptone_indicate(ch);
			ch->bc->out_cause = AST_CAUSE_UNALLOCATED;
			ch->state=MISDN_CLEANING;
			misdn_lib_send_event(ch->bc, EVENT_DISCONNECT);
		}
		ch->overlap_dial_task = -1;
		return 0;
	} else
		return diff;
}

static void send_digit_to_chan(struct chan_list *cl, char digit )
{
	static const char* dtmf_tones[] = {
		"!941+1336/100,!0/100",	/* 0 */
		"!697+1209/100,!0/100",	/* 1 */
		"!697+1336/100,!0/100",	/* 2 */
		"!697+1477/100,!0/100",	/* 3 */
		"!770+1209/100,!0/100",	/* 4 */
		"!770+1336/100,!0/100",	/* 5 */
		"!770+1477/100,!0/100",	/* 6 */
		"!852+1209/100,!0/100",	/* 7 */
		"!852+1336/100,!0/100",	/* 8 */
		"!852+1477/100,!0/100",	/* 9 */
		"!697+1633/100,!0/100",	/* A */
		"!770+1633/100,!0/100",	/* B */
		"!852+1633/100,!0/100",	/* C */
		"!941+1633/100,!0/100",	/* D */
		"!941+1209/100,!0/100",	/* * */
		"!941+1477/100,!0/100" };	/* # */
	struct ast_channel *chan=cl->ast; 
  
	if (digit >= '0' && digit <='9')
		ast_playtones_start(chan,0,dtmf_tones[digit-'0'], 0);
	else if (digit >= 'A' && digit <= 'D')
		ast_playtones_start(chan,0,dtmf_tones[digit-'A'+10], 0);
	else if (digit == '*')
		ast_playtones_start(chan,0,dtmf_tones[14], 0);
	else if (digit == '#')
		ast_playtones_start(chan,0,dtmf_tones[15], 0);
	else {
		/* not handled */
		ast_log(LOG_DEBUG, "Unable to handle DTMF tone '%c' for '%s'\n", digit, chan->name);
	}
}

/*** CLI HANDLING ***/
static int misdn_set_debug(int fd, int argc, char *argv[])
{
	int level;

	if (argc != 4 && argc != 5 && argc != 6 && argc != 7)
		return RESULT_SHOWUSAGE; 

	level = atoi(argv[3]);

	switch (argc) {
	case 4:
	case 5:
		{
			int i;
			int only = 0;
			if (argc == 5) {
				if (strncasecmp(argv[4], "only", strlen(argv[4])))
					return RESULT_SHOWUSAGE;
				else
					only = 1;
			}
	
			for (i = 0; i <= max_ports; i++) {
				misdn_debug[i] = level;
				misdn_debug_only[i] = only;
			}
			ast_cli(fd, "changing debug level for all ports to %d%s\n",misdn_debug[0], only?" (only)":"");
		}
		break;
	case 6:
	case 7:
		{
			int port;
			if (strncasecmp(argv[4], "port", strlen(argv[4])))
				return RESULT_SHOWUSAGE;
			port = atoi(argv[5]);
			if (port <= 0 || port > max_ports) {
				switch (max_ports) {
				case 0:
					ast_cli(fd, "port number not valid! no ports available so you won't get lucky with any number here...\n");
					break;
				case 1:
					ast_cli(fd, "port number not valid! only port 1 is available.\n");
					break;
				default:
					ast_cli(fd, "port number not valid! only ports 1 to %d are available.\n", max_ports);
				}
				return 0;
			}
			if (argc == 7) {
				if (strncasecmp(argv[6], "only", strlen(argv[6])))
					return RESULT_SHOWUSAGE;
				else
					misdn_debug_only[port] = 1;
			} else
				misdn_debug_only[port] = 0;
			misdn_debug[port] = level;
			ast_cli(fd, "changing debug level to %d%s for port %d\n", misdn_debug[port], misdn_debug_only[port]?" (only)":"", port);
		}
	}
	return 0;
}

static int misdn_set_crypt_debug(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE; 

	return 0;
}

static int misdn_port_block(int fd, int argc, char *argv[])
{
	int port;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_block(port);

	return 0;
}

static int misdn_port_unblock(int fd, int argc, char *argv[])
{
	int port;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_unblock(port);

	return 0;
}


static int misdn_restart_port (int fd, int argc, char *argv[])
{
	int port;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_restart(port);

	return 0;
}

static int misdn_restart_pid (int fd, int argc, char *argv[])
{
	int pid;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	pid = atoi(argv[3]);

	misdn_lib_pid_restart(pid);

	return 0;
}

static int misdn_port_up (int fd, int argc, char *argv[])
{
	int port;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	port = atoi(argv[3]);
	
	misdn_lib_get_port_up(port);
  
	return 0;
}

static int misdn_port_down (int fd, int argc, char *argv[])
{
	int port;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	port = atoi(argv[3]);
	
	misdn_lib_get_port_down(port);
  
	return 0;
}

static inline void show_config_description (int fd, enum misdn_cfg_elements elem)
{
	char section[BUFFERSIZE];
	char name[BUFFERSIZE];
	char desc[BUFFERSIZE];
	char def[BUFFERSIZE];
	char tmp[BUFFERSIZE];

	misdn_cfg_get_name(elem, tmp, sizeof(tmp));
	term_color(name, tmp, COLOR_BRWHITE, 0, sizeof(tmp));
	misdn_cfg_get_desc(elem, desc, sizeof(desc), def, sizeof(def));

	if (elem < MISDN_CFG_LAST)
		term_color(section, "PORTS SECTION", COLOR_YELLOW, 0, sizeof(section));
	else
		term_color(section, "GENERAL SECTION", COLOR_YELLOW, 0, sizeof(section));

	if (*def)
		ast_cli(fd, "[%s] %s   (Default: %s)\n\t%s\n", section, name, def, desc);
	else
		ast_cli(fd, "[%s] %s\n\t%s\n", section, name, desc);
}

static int misdn_show_config (int fd, int argc, char *argv[])
{
	char buffer[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	int linebreak;
	int onlyport = -1;
	int ok = 0;

	if (argc >= 4) {
		if (!strcmp(argv[3], "description")) {
			if (argc == 5) {
				enum misdn_cfg_elements elem = misdn_cfg_get_elem(argv[4]);
				if (elem == MISDN_CFG_FIRST)
					ast_cli(fd, "Unknown element: %s\n", argv[4]);
				else
					show_config_description(fd, elem);
				return 0;
			}
			return RESULT_SHOWUSAGE;
		}
		if (!strcmp(argv[3], "descriptions")) {
			if ((argc == 4) || ((argc == 5) && !strcmp(argv[4], "general"))) {
				for (elem = MISDN_GEN_FIRST + 1; elem < MISDN_GEN_LAST; ++elem) {
					show_config_description(fd, elem);
					ast_cli(fd, "\n");
				}
				ok = 1;
			}
			if ((argc == 4) || ((argc == 5) && !strcmp(argv[4], "ports"))) {
				for (elem = MISDN_CFG_FIRST + 1; elem < MISDN_CFG_LAST - 1 /* the ptp hack, remove the -1 when ptp is gone */; ++elem) {
					show_config_description(fd, elem);
					ast_cli(fd, "\n");
				}
				ok = 1;
			}
			return ok ? 0 : RESULT_SHOWUSAGE;
		}
		if (!sscanf(argv[3], "%d", &onlyport) || onlyport < 0) {
			ast_cli(fd, "Unknown option: %s\n", argv[3]);
			return RESULT_SHOWUSAGE;
		}
	}
	
	if (argc == 3 || onlyport == 0) {
		ast_cli(fd, "Misdn General-Config:\n");
		for (elem = MISDN_GEN_FIRST + 1, linebreak = 1; elem < MISDN_GEN_LAST; elem++, linebreak++) {
			misdn_cfg_get_config_string(0, elem, buffer, BUFFERSIZE);
			ast_cli(fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
		}
		ast_cli(fd, "\n");
	}

	if (onlyport < 0) {
		int port = misdn_cfg_get_next_port(0);
		for (; port > 0; port = misdn_cfg_get_next_port(port)) {
			ast_cli(fd, "\n[PORT %d]\n", port);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string(port, elem, buffer, BUFFERSIZE);
				ast_cli(fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}	
			ast_cli(fd, "\n");
		}
	}
	
	if (onlyport > 0) {
		if (misdn_cfg_is_port_valid(onlyport)) {
			ast_cli(fd, "[PORT %d]\n", onlyport);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string(onlyport, elem, buffer, BUFFERSIZE);
				ast_cli(fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}	
			ast_cli(fd, "\n");
		} else {
			ast_cli(fd, "Port %d is not active!\n", onlyport);
		}
	}

	return 0;
}

struct state_struct {
	enum misdn_chan_state state;
	char txt[255];
};

static struct state_struct state_array[] = {
	{MISDN_NOTHING,"NOTHING"}, /* at beginning */
	{MISDN_WAITING4DIGS,"WAITING4DIGS"}, /*  when waiting for infos */
	{MISDN_EXTCANTMATCH,"EXTCANTMATCH"}, /*  when asterisk couldn't match our ext */
	{MISDN_INCOMING_SETUP,"INCOMING SETUP"}, /*  when pbx_start */
	{MISDN_DIALING,"DIALING"}, /*  when pbx_start */
	{MISDN_PROGRESS,"PROGRESS"}, /*  when pbx_start */
	{MISDN_PROCEEDING,"PROCEEDING"}, /*  when pbx_start */
	{MISDN_CALLING,"CALLING"}, /*  when misdn_call is called */
	{MISDN_CALLING_ACKNOWLEDGE,"CALLING_ACKNOWLEDGE"}, /*  when misdn_call is called */
	{MISDN_ALERTING,"ALERTING"}, /*  when Alerting */
	{MISDN_BUSY,"BUSY"}, /*  when BUSY */
	{MISDN_CONNECTED,"CONNECTED"}, /*  when connected */
	{MISDN_PRECONNECTED,"PRECONNECTED"}, /*  when connected */
	{MISDN_DISCONNECTED,"DISCONNECTED"}, /*  when connected */
	{MISDN_RELEASED,"RELEASED"}, /*  when connected */
	{MISDN_BRIDGED,"BRIDGED"}, /*  when bridged */
	{MISDN_CLEANING,"CLEANING"}, /* when hangup from * but we were connected before */
	{MISDN_HUNGUP_FROM_MISDN,"HUNGUP_FROM_MISDN"}, /* when DISCONNECT/RELEASE/REL_COMP  came from misdn */
	{MISDN_HOLDED,"HOLDED"}, /* when DISCONNECT/RELEASE/REL_COMP  came from misdn */
	{MISDN_HOLD_DISCONNECT,"HOLD_DISCONNECT"}, /* when DISCONNECT/RELEASE/REL_COMP  came from misdn */
	{MISDN_HUNGUP_FROM_AST,"HUNGUP_FROM_AST"} /* when DISCONNECT/RELEASE/REL_COMP came out of misdn_hangup */
};

static const char *misdn_get_ch_state(struct chan_list *p) 
{
	int i;
	static char state[8];
	
	if( !p) return NULL;
  
	for (i = 0; i < sizeof(state_array) / sizeof(struct state_struct); i++) {
		if (state_array[i].state == p->state)
			return state_array[i].txt; 
	}

 	snprintf(state, sizeof(state), "%d", p->state) ;

	return state;
}



static void reload_config(void)
{
	int i, cfg_debug;

	if (!g_config_initialized) {
		ast_log(LOG_WARNING, "chan_misdn is not initialized properly, still reloading ?\n");
		return ;
	}
	
	free_robin_list();
	misdn_cfg_reload();
	misdn_cfg_update_ptp();
	misdn_cfg_get(0, MISDN_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);
	misdn_cfg_get(0, MISDN_GEN_DEBUG, &cfg_debug, sizeof(int));

	for (i = 0;  i <= max_ports; i++) {
		misdn_debug[i] = cfg_debug;
		misdn_debug_only[i] = 0;
	}
}

static int misdn_reload (int fd, int argc, char *argv[])
{
	ast_cli(fd, "Reloading mISDN configuration\n");
	reload_config();
	return 0;
}

static void print_bc_info (int fd, struct chan_list *help, struct misdn_bchannel *bc)
{
	struct ast_channel *ast = help->ast;
	ast_cli(fd,
		"* Pid:%d Prt:%d Ch:%d Mode:%s Org:%s dad:%s oad:%s rad:%s ctx:%s state:%s\n",

		bc->pid, bc->port, bc->channel,
		bc->nt ? "NT" : "TE",
		help->originator == ORG_AST ? "*" : "I",
		ast ? ast->exten : NULL,
		ast ? ast->cid.cid_num : NULL,
		bc->rad,
		ast ? ast->context : NULL,
		misdn_get_ch_state(help)
		);
	if (misdn_debug[bc->port] > 0)
		ast_cli(fd,
			"  --> astname: %s\n"
			"  --> ch_l3id: %x\n"
			"  --> ch_addr: %x\n"
			"  --> bc_addr: %x\n"
			"  --> bc_l3id: %x\n"
			"  --> display: %s\n"
			"  --> activated: %d\n"
			"  --> state: %s\n"
			"  --> capability: %s\n"
#ifdef MISDN_1_2
			"  --> pipeline: %s\n"
#else
			"  --> echo_cancel: %d\n"
#endif
			"  --> notone : rx %d tx:%d\n"
			"  --> bc_hold: %d\n",
			help->ast->name,
			help->l3id,
			help->addr,
			bc->addr,
			bc ? bc->l3_id : -1,
			bc->display,
			
			bc->active,
			bc_state2str(bc->bc_state),
			bearer2str(bc->capability),
#ifdef MISDN_1_2
			bc->pipeline,
#else
 			bc->ec_enable,
#endif

			help->norxtone, help->notxtone,
			bc->holded
			);

}

static int misdn_show_cls (int fd, int argc, char *argv[])
{
	struct chan_list *help;

	help = cl_te;
  
	ast_cli(fd, "Channel List: %p\n", cl_te);

	for (; help; help = help->next) {
		struct misdn_bchannel *bc = help->bc;   
		struct ast_channel *ast = help->ast;
		if (!ast) {
			if (!bc) {
				ast_cli(fd, "chan_list obj. with l3id:%x has no bc and no ast Leg\n", help->l3id);
				continue;
			}
			ast_cli(fd, "bc with pid:%d has no Ast Leg\n", bc->pid);
			continue;
		}

		if (misdn_debug[0] > 2)
			ast_cli(fd, "Bc:%p Ast:%p\n", bc, ast);
		if (bc) {
			print_bc_info(fd, help, bc);
		} else {
			if (help->state == MISDN_HOLDED) {
				ast_cli(fd, "ITS A HOLDED BC:\n");
				ast_cli(fd, " --> l3_id: %x\n"
						" --> dad:%s oad:%s\n"
						" --> hold_port: %d\n"
						" --> hold_channel: %d\n",
						help->l3id,
						ast->exten,
						ast->cid.cid_num,
						help->hold_info.port,
						help->hold_info.channel
						);
			} else {
				ast_cli(fd, "* Channel in unknown STATE !!! Exten:%s, Callerid:%s\n", ast->exten, ast->cid.cid_num);
			}
		}
	}

 	misdn_dump_chanlist();

	return 0;
}

static int misdn_show_cl (int fd, int argc, char *argv[])
{
	struct chan_list *help;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	help = cl_te;

	for (; help; help = help->next) {
		struct misdn_bchannel *bc = help->bc;   
		struct ast_channel *ast = help->ast;
    
		if (bc && ast) {
			if (!strcasecmp(ast->name,argv[3])) {
				print_bc_info(fd, help, bc);
				break; 
			}
		} 
	}

	return 0;
}

ast_mutex_t lock;
int MAXTICS = 8;

static int misdn_set_tics (int fd, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;

	MAXTICS = atoi(argv[3]);

	return 0;
}

static int misdn_show_stacks (int fd, int argc, char *argv[])
{
	int port;

	ast_cli(fd, "BEGIN STACK_LIST:\n");
	for (port = misdn_cfg_get_next_port(0); port > 0;
	     port = misdn_cfg_get_next_port(port)) {
		char buf[128];
		get_show_stack_details(port, buf);
		ast_cli(fd, "  %s  Debug:%d%s\n", buf, misdn_debug[port], misdn_debug_only[port] ? "(only)" : "");
	}

	return 0;
}

static int misdn_show_ports_stats (int fd, int argc, char *argv[])
{
	int port;

	ast_cli(fd, "Port\tin_calls\tout_calls\n");
	for (port = misdn_cfg_get_next_port(0); port > 0;
	     port = misdn_cfg_get_next_port(port)) {
		ast_cli(fd, "%d\t%d\t\t%d\n", port, misdn_in_calls[port], misdn_out_calls[port]);
	}
	ast_cli(fd, "\n");

	return 0;
}

static int misdn_show_port (int fd, int argc, char *argv[])
{
	int port;
	char buf[128];

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	port = atoi(argv[3]);
  
	ast_cli(fd, "BEGIN STACK_LIST:\n");
	get_show_stack_details(port, buf);
	ast_cli(fd, "  %s  Debug:%d%s\n", buf, misdn_debug[port], misdn_debug_only[port] ? "(only)" : "");

	return 0;
}

static int misdn_send_cd (int fd, int argc, char *argv[])
{
	char *channame; 
	char *nr;
	struct chan_list *tmp;

	if (argc != 5)
		return RESULT_SHOWUSAGE;
 
	
	{
		channame = argv[3];
		nr = argv[4];

		ast_cli(fd, "Sending Calldeflection (%s) to %s\n", nr, channame);
		tmp = get_chan_by_ast_name(channame);
		if (!tmp) {
			ast_cli(fd, "Sending CD with nr %s to %s failed: Channel does not exist.\n",nr, channame);
			return 0; 
		}

		if (strlen(nr) >= 15) {
			ast_cli(fd, "Sending CD with nr %s to %s failed: Number too long (up to 15 digits are allowed).\n",nr, channame);
			return 0; 
		}
		tmp->bc->fac_out.Function = Fac_CD;
		ast_copy_string((char *)tmp->bc->fac_out.u.CDeflection.DeflectedToNumber, nr, sizeof(tmp->bc->fac_out.u.CDeflection.DeflectedToNumber));
		misdn_lib_send_event(tmp->bc, EVENT_FACILITY);
	}

	return 0;
}

static int misdn_send_restart(int fd, int argc, char *argv[])
{
	int port;
	int channel;

	if (argc < 4 || argc > 5)
		return RESULT_SHOWUSAGE;

	port = atoi(argv[3]);

	if (argc == 5) {
		channel = atoi(argv[4]);
		misdn_lib_send_restart(port, channel);
	} else {
 		misdn_lib_send_restart(port, -1);
	}

	return 0;
}

static int misdn_send_digit (int fd, int argc, char *argv[])
{
	char *channame; 
	char *msg; 
	struct chan_list *tmp;
	int i, msglen;

	if (argc != 5)
		return RESULT_SHOWUSAGE;

	channame = argv[3];
	msg = argv[4];
	msglen = strlen(msg);

	ast_cli(fd, "Sending %s to %s\n", msg, channame);

	tmp = get_chan_by_ast_name(channame);
	if (!tmp) {
		ast_cli(fd, "Sending %s to %s failed Channel does not exist\n", msg, channame);
		return 0; 
	}
#if 1
	for (i = 0; i < msglen; i++) {
		ast_cli(fd, "Sending: %c\n", msg[i]);
		send_digit_to_chan(tmp, msg[i]);
		/* res = ast_safe_sleep(tmp->ast, 250); */
		usleep(250000);
		/* res = ast_waitfor(tmp->ast,100); */
	}
#else
	ast_dtmf_stream(tmp->ast, NULL, msg, 250);
#endif

	return 0;
}

static int misdn_toggle_echocancel (int fd, int argc, char *argv[])
{
	char *channame;
	struct chan_list *tmp;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	channame = argv[3];
  
	ast_cli(fd, "Toggling EchoCancel on %s\n", channame);
  
	tmp = get_chan_by_ast_name(channame);
	if (!tmp) {
		ast_cli(fd, "Toggling EchoCancel %s failed Channel does not exist\n", channame);
		return 0;
	}

	tmp->toggle_ec = tmp->toggle_ec ? 0 : 1;

	if (tmp->toggle_ec) {
#ifdef MISDN_1_2
		update_pipeline_config(tmp->bc);
#else
		update_ec_config(tmp->bc);
#endif
		manager_ec_enable(tmp->bc);
	} else {
		manager_ec_disable(tmp->bc);
	}

	return 0;
}

static int misdn_send_display (int fd, int argc, char *argv[])
{
	char *channame;
	char *msg;
	struct chan_list *tmp;

	if (argc != 5)
		return RESULT_SHOWUSAGE;

	channame = argv[3];
	msg = argv[4];

	ast_cli(fd, "Sending %s to %s\n", msg, channame);
	tmp = get_chan_by_ast_name(channame);
    
	if (tmp && tmp->bc) {
		ast_copy_string(tmp->bc->display, msg, sizeof(tmp->bc->display));
		misdn_lib_send_event(tmp->bc, EVENT_INFORMATION);
	} else {
		ast_cli(fd, "No such channel %s\n", channame);
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static char *complete_ch_helper(const char *line, const char *word, int pos, int state, int rpos)
{
	struct ast_channel *c;
	int which=0;
	char *ret;
	if (pos != rpos)
		return NULL;
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strncasecmp(word, c->name, strlen(word))) {
			if (++which > state)
				break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (c) {
		ret = strdup(c->name);
		ast_mutex_unlock(&c->lock);
	} else
		ret = NULL;
	return ret;
}

static char *complete_ch(const char *line, const char *word, int pos, int state)
{
	return complete_ch_helper(line, word, pos, state, 3);
}

static char *complete_debug_port (const char *line, const char *word, int pos, int state)
{
	if (state)
		return NULL;

	switch (pos) {
	case 4:
		if (*word == 'p')
			return strdup("port");
		else if (*word == 'o')
			return strdup("only");
		break;
	case 6:
		if (*word == 'o')
			return strdup("only");
		break;
	}
	return NULL;
}

static char *complete_show_config (const char *line, const char *word, int pos, int state)
{
	char buffer[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	int wordlen = strlen(word);
	int which = 0;
	int port = 0;

	switch (pos) {
	case 3:
		if ((!strncmp(word, "description", wordlen)) && (++which > state))
			return strdup("description");
		if ((!strncmp(word, "descriptions", wordlen)) && (++which > state))
			return strdup("descriptions");
		if ((!strncmp(word, "0", wordlen)) && (++which > state))
			return strdup("0");
		while ((port = misdn_cfg_get_next_port(port)) != -1) {
			snprintf(buffer, sizeof(buffer), "%d", port);
			if ((!strncmp(word, buffer, wordlen)) && (++which > state)) {
				return strdup(buffer);
			}
		}
		break;
	case 4:
		if (strstr(line, "description ")) {
			for (elem = MISDN_CFG_FIRST + 1; elem < MISDN_GEN_LAST; ++elem) {
				if ((elem == MISDN_CFG_LAST) || (elem == MISDN_GEN_FIRST))
					continue;
				misdn_cfg_get_name(elem, buffer, BUFFERSIZE);
				if (!wordlen || !strncmp(word, buffer, wordlen)) {
					if (++which > state)
						return strdup(buffer);
				}
			}
		} else if (strstr(line, "descriptions ")) {
			if ((!wordlen || !strncmp(word, "general", wordlen)) && (++which > state))
				return strdup("general");
			if ((!wordlen || !strncmp(word, "ports", wordlen)) && (++which > state))
				return strdup("ports");
		}
		break;
	}
	return NULL;
}

static struct ast_cli_entry chan_misdn_clis[] = {
	{ {"misdn","send","calldeflect", NULL}, misdn_send_cd, "Sends CallDeflection to mISDN Channel",
		"Usage: misdn send calldeflect <channel> \"<nr>\" \n", complete_ch },
	{ {"misdn","send","digit", NULL}, misdn_send_digit,	"Sends DTMF Digit to mISDN Channel",
		"Usage: misdn send digit <channel> \"<msg>\" \n"
		"       Send <digit> to <channel> as DTMF Tone\n"
		"       when channel is a mISDN channel\n", complete_ch },
	{ {"misdn","toggle","echocancel", NULL}, misdn_toggle_echocancel, "Toggles EchoCancel on mISDN Channel",
		"Usage: misdn toggle echocancel <channel>\n", complete_ch },
	{ {"misdn","send","display", NULL}, misdn_send_display, "Sends Text to mISDN Channel", 
		"Usage: misdn send display <channel> \"<msg>\" \n"
		"       Send <msg> to <channel> as Display Message\n"
		"       when channel is a mISDN channel\n", complete_ch },
	{ {"misdn","show","config", NULL}, misdn_show_config, "Shows internal mISDN config, read from cfg-file",
		"Usage: misdn show config [<port> | description <config element> | descriptions [general|ports]]\n"
		"       Use 0 for <port> to only print the general config.\n", complete_show_config },
	{ {"misdn","reload", NULL}, misdn_reload, "Reloads internal mISDN config, read from cfg-file",
		"Usage: misdn reload\n" },
	{ {"misdn","set","tics", NULL}, misdn_set_tics, "", 
		"\n" },
	{ {"misdn","show","channels", NULL}, misdn_show_cls, "Shows internal mISDN chan_list",
		"Usage: misdn show channels\n" },
	{ {"misdn","show","channel", NULL}, misdn_show_cl, "Shows internal mISDN chan_list",
		"Usage: misdn show channels\n", complete_ch },
	{ {"misdn","port","block", NULL}, misdn_port_block, "Blocks the given port",
		"Usage: misdn port block\n" },
	{ {"misdn","port","unblock", NULL}, misdn_port_unblock, "Unblocks the given port",
		"Usage: misdn port unblock\n" },
	{ {"misdn","restart","port", NULL}, misdn_restart_port, "Restarts the given port",
		"Usage: misdn restart port\n" },
	{ {"misdn","restart","pid", NULL}, misdn_restart_pid, "Restarts the given pid",
		"Usage: misdn restart pid\n" },
	{ {"misdn","send","restart", NULL},  misdn_send_restart, 
	  "Sends a restart for every bchannel on the given port", 
	  "Usage: misdn send restart <port>\n"},
	{ {"misdn","port","up", NULL}, misdn_port_up, "Tries to establish L1 on the given port",
		"Usage: misdn port up <port>\n" },
	{ {"misdn","port","down", NULL}, misdn_port_down, "Tries to deactivate the L1 on the given port",
		"Usage: misdn port down <port>\n" },
	{ {"misdn","show","stacks", NULL}, misdn_show_stacks, "Shows internal mISDN stack_list",
		"Usage: misdn show stacks\n" },
	{ {"misdn","show","ports","stats", NULL}, misdn_show_ports_stats, "Shows chan_misdns call statistics per port",
		"Usage: misdn show port stats\n" },
	{ {"misdn","show","port", NULL}, misdn_show_port, "Shows detailed information for given port",
		"Usage: misdn show port <port>\n" },
	{ {"misdn","set","debug", NULL}, misdn_set_debug, "Sets Debuglevel of chan_misdn",
		"Usage: misdn set debug <level> [only] | [port <port> [only]]\n", complete_debug_port },
	{ {"misdn","set","crypt","debug", NULL}, misdn_set_crypt_debug, "Sets CryptDebuglevel of chan_misdn, at the moment, level={1,2}",
		"Usage: misdn set crypt debug <level>\n" }
};

/*! \brief Updates caller ID information from config */
static int update_config(struct chan_list *ch, int orig) 
{
	struct ast_channel *ast;
	struct misdn_bchannel *bc;
	int port, hdlc = 0;
	int pres, screen;

	if (!ch) {
		ast_log(LOG_WARNING, "Cannot configure without chanlist\n");
		return -1;
	}

	ast = ch->ast;
	bc = ch->bc;
	if (! ast || ! bc) {
		ast_log(LOG_WARNING, "Cannot configure without ast || bc\n");
		return -1;
	}

	port = bc->port;

	chan_misdn_log(7, port, "update_config: Getting Config\n");

	misdn_cfg_get(port, MISDN_CFG_HDLC, &hdlc, sizeof(int));
	
	if (hdlc) {
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		case INFO_CAPABILITY_DIGITAL_RESTRICTED:
			chan_misdn_log(1, bc->port, " --> CONF HDLC\n");
			bc->hdlc = 1;
			break;
		}
	}


	misdn_cfg_get(port, MISDN_CFG_PRES, &pres, sizeof(pres));
	misdn_cfg_get(port, MISDN_CFG_SCREEN, &screen, sizeof(screen));
	chan_misdn_log(2, port, " --> pres: %d screen: %d\n", pres, screen);
		
	if (pres < 0 || screen < 0) {
		chan_misdn_log(2, port, " --> pres: %x\n", ast->cid.cid_pres);
			
		switch (ast->cid.cid_pres & 0x60) {
		case AST_PRES_RESTRICTED:
			bc->pres = 1;
			chan_misdn_log(2, port, " --> PRES: Restricted (1)\n");
			break;
		case AST_PRES_UNAVAILABLE:
			bc->pres = 2;
			chan_misdn_log(2, port, " --> PRES: Unavailable (2)\n");
			break;
		default:
			bc->pres = 0;
			chan_misdn_log(2, port, " --> PRES: Allowed (0)\n");
			break;
		}

		switch (ast->cid.cid_pres & 0x3) {
		default:
		case AST_PRES_USER_NUMBER_UNSCREENED:
			bc->screen = 0;
			chan_misdn_log(2, port, " --> SCREEN: Unscreened (0)\n");
			break;
		case AST_PRES_USER_NUMBER_PASSED_SCREEN:
			bc->screen = 1;
			chan_misdn_log(2, port, " --> SCREEN: Passed Screen (1)\n");
			break;
		case AST_PRES_USER_NUMBER_FAILED_SCREEN:
			bc->screen = 2;
			chan_misdn_log(2, port, " --> SCREEN: Failed Screen (2)\n");
			break;
		case AST_PRES_NETWORK_NUMBER:
			bc->screen = 3;
			chan_misdn_log(2, port, " --> SCREEN: Network Nr. (3)\n");
			break;
		}
	} else {
		bc->screen = screen;
		bc->pres = pres;
	}

	return 0;
}


static void config_jitterbuffer(struct chan_list *ch)
{
	struct misdn_bchannel *bc = ch->bc;
	int len = ch->jb_len, threshold = ch->jb_upper_threshold;
	
	chan_misdn_log(5, bc->port, "config_jb: Called\n");
	
	if (! len) {
		chan_misdn_log(1, bc->port, "config_jb: Deactivating Jitterbuffer\n");
		bc->nojitter=1;
	} else {
		if (len <= 100 || len > 8000) {
			chan_misdn_log(0, bc->port, "config_jb: Jitterbuffer out of Bounds, setting to 1000\n");
			len = 1000;
		}

		if ( threshold > len ) {
			chan_misdn_log(0, bc->port, "config_jb: Jitterbuffer Threshold > Jitterbuffer setting to Jitterbuffer -1\n");
		}

		if ( ch->jb) {
			cb_log(0, bc->port, "config_jb: We've got a Jitterbuffer Already on this port.\n");
			misdn_jb_destroy(ch->jb);
			ch->jb = NULL;
		}

		ch->jb=misdn_jb_init(len, threshold);

		if (!ch->jb ) 
			bc->nojitter = 1;
	}
}


void debug_numplan(int port, int numplan, char *type)
{
	switch (numplan) {
	case NUMPLAN_INTERNATIONAL:
		chan_misdn_log(2, port, " --> %s: International\n", type);
		break;
	case NUMPLAN_NATIONAL:
		chan_misdn_log(2, port, " --> %s: National\n", type);
		break;
	case NUMPLAN_SUBSCRIBER:
		chan_misdn_log(2, port, " --> %s: Subscriber\n", type);
		break;
	case NUMPLAN_UNKNOWN:
		chan_misdn_log(2, port, " --> %s: Unknown\n", type);
		break;
		/* Maybe we should cut off the prefix if present ? */
	default:
		chan_misdn_log(0, port, " --> !!!! Wrong dialplan setting, please see the misdn.conf sample file\n ");
		break;
	}
}


#ifdef MISDN_1_2
static int update_pipeline_config(struct misdn_bchannel *bc)
{
	int ec;

	misdn_cfg_get(bc->port, MISDN_CFG_PIPELINE, bc->pipeline, sizeof(bc->pipeline));

	if (*bc->pipeline)
		return 0;

	misdn_cfg_get(bc->port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));
	if (ec == 1)
		ast_copy_string(bc->pipeline, "mg2ec", sizeof(bc->pipeline));
	else if (ec > 1)
		snprintf(bc->pipeline, sizeof(bc->pipeline), "mg2ec(deftaps=%d)", ec);

	return 0;
}
#else
static int update_ec_config(struct misdn_bchannel *bc)
{
	int ec;
	int port = bc->port;

	misdn_cfg_get(port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));

	if (ec == 1) {
		bc->ec_enable = 1;
	} else if (ec > 1) {
		bc->ec_enable = 1;
		bc->ec_deftaps = ec;
	}

	return 0;
}
#endif


static int read_config(struct chan_list *ch, int orig)
{
	struct ast_channel *ast;
	struct misdn_bchannel *bc;
	int port;
	int hdlc = 0;
	char lang[BUFFERSIZE + 1];
	char faxdetect[BUFFERSIZE + 1];
	char buf[256];
	char buf2[256];
	ast_group_t pg;
	ast_group_t cg;

	if (!ch) {
		ast_log(LOG_WARNING, "Cannot configure without chanlist\n");
		return -1;
	}

	ast = ch->ast;
	bc = ch->bc;
	if (! ast || ! bc) {
		ast_log(LOG_WARNING, "Cannot configure without ast || bc\n");
		return -1;
	}
	
	port = bc->port;
	chan_misdn_log(1, port, "read_config: Getting Config\n");

	misdn_cfg_get(port, MISDN_CFG_LANGUAGE, lang, BUFFERSIZE);
	ast_string_field_set(ast, language, lang);

	misdn_cfg_get(port, MISDN_CFG_MUSICCLASS, ch->mohinterpret, sizeof(ch->mohinterpret));

	misdn_cfg_get(port, MISDN_CFG_TXGAIN, &bc->txgain, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_RXGAIN, &bc->rxgain, sizeof(int));

	misdn_cfg_get(port, MISDN_CFG_INCOMING_EARLY_AUDIO, &ch->incoming_early_audio, sizeof(int));

	misdn_cfg_get(port, MISDN_CFG_SENDDTMF, &bc->send_dtmf, sizeof(int));
	
	misdn_cfg_get(port, MISDN_CFG_ASTDTMF, &ch->ast_dsp, sizeof(int));

	if (ch->ast_dsp) {
		ch->ignore_dtmf = 1;
	}

	misdn_cfg_get(port, MISDN_CFG_NEED_MORE_INFOS, &bc->need_more_infos, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_NTTIMEOUT, &ch->nttimeout, sizeof(int));

	misdn_cfg_get(port, MISDN_CFG_NOAUTORESPOND_ON_SETUP, &ch->noautorespond_on_setup, sizeof(int));

	misdn_cfg_get(port, MISDN_CFG_FAR_ALERTING, &ch->far_alerting, sizeof(int));

	misdn_cfg_get(port, MISDN_CFG_ALLOWED_BEARERS, &ch->allowed_bearers, BUFFERSIZE);

  	misdn_cfg_get(port, MISDN_CFG_FAXDETECT, faxdetect, BUFFERSIZE);

	misdn_cfg_get(port, MISDN_CFG_HDLC, &hdlc, sizeof(int));

	if (hdlc) {
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		case INFO_CAPABILITY_DIGITAL_RESTRICTED:
			chan_misdn_log(1, bc->port, " --> CONF HDLC\n");
			bc->hdlc = 1;
			break;
		}
		
	}
	/*Initialize new Jitterbuffer*/
	misdn_cfg_get(port, MISDN_CFG_JITTERBUFFER, &ch->jb_len, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_JITTERBUFFER_UPPER_THRESHOLD, &ch->jb_upper_threshold, sizeof(int));

	config_jitterbuffer(ch);

	misdn_cfg_get(bc->port, MISDN_CFG_CONTEXT, ch->context, sizeof(ch->context));

	ast_copy_string(ast->context, ch->context, sizeof(ast->context));

#ifdef MISDN_1_2
	update_pipeline_config(bc);
#else
	update_ec_config(bc);
#endif

	{
		int eb3;
		
		misdn_cfg_get( bc->port, MISDN_CFG_EARLY_BCONNECT, &eb3, sizeof(int));
		bc->early_bconnect=eb3;
	}

	misdn_cfg_get(port, MISDN_CFG_PICKUPGROUP, &pg, sizeof(pg));
	misdn_cfg_get(port, MISDN_CFG_CALLGROUP, &cg, sizeof(cg));

	chan_misdn_log(5, port, " --> * CallGrp:%s PickupGrp:%s\n", ast_print_group(buf, sizeof(buf), cg), ast_print_group(buf2, sizeof(buf2), pg));
	ast->pickupgroup = pg;
	ast->callgroup = cg;
	
	if (orig == ORG_AST) {
		char callerid[BUFFERSIZE + 1];

		/* ORIGINATOR Asterisk (outgoing call) */

		misdn_cfg_get(port, MISDN_CFG_TE_CHOOSE_CHANNEL, &(bc->te_choose_channel), sizeof(int));

 		if (strstr(faxdetect, "outgoing") || strstr(faxdetect, "both")) {
 			if (strstr(faxdetect, "nojump"))
 				ch->faxdetect = 2;
 			else
 				ch->faxdetect = 1;
 		}

		misdn_cfg_get(port, MISDN_CFG_CALLERID, callerid, BUFFERSIZE);
		if ( ! ast_strlen_zero(callerid) ) {
			chan_misdn_log(1, port, " --> * Setting Cid to %s\n", callerid);
			ast_copy_string(bc->oad, callerid, sizeof(bc->oad));
		}

		misdn_cfg_get(port, MISDN_CFG_DIALPLAN, &bc->dnumplan, sizeof(int));
		misdn_cfg_get(port, MISDN_CFG_LOCALDIALPLAN, &bc->onumplan, sizeof(int));
		misdn_cfg_get(port, MISDN_CFG_CPNDIALPLAN, &bc->cpnnumplan, sizeof(int));
		debug_numplan(port, bc->dnumplan, "TON");
		debug_numplan(port, bc->onumplan, "LTON");
		debug_numplan(port, bc->cpnnumplan, "CTON");

		ch->overlap_dial = 0;
	} else {
		/* ORIGINATOR MISDN (incoming call) */
		char prefix[BUFFERSIZE + 1] = "";

 		if (strstr(faxdetect, "incoming") || strstr(faxdetect, "both")) {
 			if (strstr(faxdetect, "nojump"))
 				ch->faxdetect = 2;
 			else
 				ch->faxdetect = 1;
 		}

		misdn_cfg_get(port, MISDN_CFG_CPNDIALPLAN, &bc->cpnnumplan, sizeof(int));
		debug_numplan(port, bc->cpnnumplan, "CTON");

		switch (bc->onumplan) {
		case NUMPLAN_INTERNATIONAL:
			misdn_cfg_get(bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
			break;

		case NUMPLAN_NATIONAL:
			misdn_cfg_get(bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
			break;
		default:
			break;
		}

		ast_copy_string(buf, bc->oad, sizeof(buf));
		snprintf(bc->oad, sizeof(bc->oad), "%s%s", prefix, buf);

		if (!ast_strlen_zero(bc->dad)) {
			ast_copy_string(bc->orig_dad, bc->dad, sizeof(bc->orig_dad));
		}

		if ( ast_strlen_zero(bc->dad) && !ast_strlen_zero(bc->keypad)) {
			ast_copy_string(bc->dad, bc->keypad, sizeof(bc->dad));
		}

		prefix[0] = 0;

		switch (bc->dnumplan) {
		case NUMPLAN_INTERNATIONAL:
			misdn_cfg_get(bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
			break;
		case NUMPLAN_NATIONAL:
			misdn_cfg_get(bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
			break;
		default:
			break;
		}

		ast_copy_string(buf, bc->dad, sizeof(buf));
		snprintf(bc->dad, sizeof(bc->dad), "%s%s", prefix, buf);

		if (strcmp(bc->dad, ast->exten)) {
			ast_copy_string(ast->exten, bc->dad, sizeof(ast->exten));
		}

		ast_set_callerid(ast, bc->oad, NULL, bc->oad);

		if ( !ast_strlen_zero(bc->rad) ) {
			if (ast->cid.cid_rdnis)
				free(ast->cid.cid_rdnis);
			ast->cid.cid_rdnis = strdup(bc->rad);
		}
	
		misdn_cfg_get(bc->port, MISDN_CFG_OVERLAP_DIAL, &ch->overlap_dial, sizeof(ch->overlap_dial));
		ast_mutex_init(&ch->overlap_tv_lock);
	} /* ORIG MISDN END */

	ch->overlap_dial_task = -1;
	
	if (ch->faxdetect  || ch->ast_dsp) {
		misdn_cfg_get(port, MISDN_CFG_FAXDETECT_TIMEOUT, &ch->faxdetect_timeout, sizeof(ch->faxdetect_timeout));
		if (!ch->dsp)
			ch->dsp = ast_dsp_new();
		if (ch->dsp) {
			if (ch->faxdetect) 
				ast_dsp_set_features(ch->dsp, DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_DETECT);
			else 
				ast_dsp_set_features(ch->dsp, DSP_FEATURE_DTMF_DETECT );
		}
		if (!ch->trans)
			ch->trans = ast_translator_build_path(AST_FORMAT_SLINEAR, AST_FORMAT_ALAW);
	}

	/* AOCD initialization */
	bc->AOCDtype = Fac_None;

	return 0;
}


/*****************************/
/*** AST Indications Start ***/
/*****************************/

static int misdn_call(struct ast_channel *ast, char *dest, int timeout)
{
	int port = 0;
	int r;
	int exceed;
	int bridging;
	struct chan_list *ch;
	struct misdn_bchannel *newbc;
	char *opts, *ext;
	char *dest_cp;

	if (!ast) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on ast_channel *ast where ast == NULL\n");
		return -1;
	}

	if (((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) || !dest  ) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}

	ch = MISDN_ASTERISK_TECH_PVT(ast);
	if (!ch) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	newbc = ch->bc;
	if (!newbc) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	/*
	 * dest is ---v
	 * Dial(mISDN/g:group_name[/extension[/options]])
	 * Dial(mISDN/port[:preselected_channel][/extension[/options]])
	 *
	 * The dial extension could be empty if you are using MISDN_KEYPAD
	 * to control ISDN provider features.
	 */
	dest_cp = ast_strdupa(dest);
	strsep(&dest_cp, "/");/* Discard port/group token */
	ext = strsep(&dest_cp, "/");
	if (!ext) {
		ext = "";
	}
	opts = dest_cp;
	
	port = newbc->port;

	if ((exceed = add_out_calls(port))) {
		char tmp[16];
		snprintf(tmp, sizeof(tmp), "%d", exceed);
		pbx_builtin_setvar_helper(ast, "MAX_OVERFLOW", tmp);
		return -1;
	}
	
	chan_misdn_log(1, port, "* CALL: %s\n", dest);
	
	chan_misdn_log(2, port, " --> * dad:%s tech:%s ctx:%s\n", ast->exten, ast->name, ast->context);
	
	chan_misdn_log(3, port, " --> * adding2newbc ext %s\n", ast->exten);
	if (ast->exten) {
		ast_copy_string(ast->exten, ext, sizeof(ast->exten));
		ast_copy_string(newbc->dad, ext, sizeof(newbc->dad));
	}

	ast_copy_string(newbc->rad, S_OR(ast->cid.cid_rdnis, ""), sizeof(newbc->rad));

	chan_misdn_log(3, port, " --> * adding2newbc callerid %s\n", ast->cid.cid_num);
	if (ast_strlen_zero(newbc->oad) && !ast_strlen_zero(ast->cid.cid_num)) {
		ast_copy_string(newbc->oad, ast->cid.cid_num, sizeof(newbc->oad));
	}

	newbc->capability = ast->transfercapability;
	pbx_builtin_setvar_helper(ast, "TRANSFERCAPABILITY", ast_transfercapability2str(newbc->capability));
	if ( ast->transfercapability == INFO_CAPABILITY_DIGITAL_UNRESTRICTED) {
		chan_misdn_log(2, port, " --> * Call with flag Digital\n");
	}

	/* update screening and presentation */ 
	update_config(ch, ORG_AST);
		
	/* fill in some ies from channel vary*/
	import_ch(ast, newbc, ch);

	/* Finally The Options Override Everything */
	if (opts)
		misdn_set_opt_exec(ast, opts);
	else
		chan_misdn_log(2, port, "NO OPTS GIVEN\n");

	/*check for bridging*/
	misdn_cfg_get(0, MISDN_GEN_BRIDGING, &bridging, sizeof(bridging));
	if (bridging && ch->other_ch) {
#ifdef MISDN_1_2
		chan_misdn_log(1, port, "Disabling EC (aka Pipeline) on both Sides\n");
		*ch->bc->pipeline = 0;
		*ch->other_ch->bc->pipeline = 0;
#else
		chan_misdn_log(1, port, "Disabling EC on both Sides\n");
		ch->bc->ec_enable = 0;
		ch->other_ch->bc->ec_enable = 0;
#endif
	}

	r = misdn_lib_send_event( newbc, EVENT_SETUP );

	/** we should have l3id after sending setup **/
	ch->l3id = newbc->l3_id;

	if ( r == -ENOCHAN  ) {
		chan_misdn_log(0, port, " --> * Theres no Channel at the moment .. !\n");
		chan_misdn_log(1, port, " --> * SEND: State Down pid:%d\n", newbc ? newbc->pid : -1);
		ast->hangupcause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}
	
	chan_misdn_log(2, port, " --> * SEND: State Dialing pid:%d\n", newbc ? newbc->pid : 1);

	ast_setstate(ast, AST_STATE_DIALING);
	ast->hangupcause = AST_CAUSE_NORMAL_CLEARING;
	
	if (newbc->nt)
		stop_bc_tones(ch);

	ch->state = MISDN_CALLING;
	
	return 0; 
}


static int misdn_answer(struct ast_channel *ast)
{
	struct chan_list *p;
	const char *tmp;

	if (!ast || !(p = MISDN_ASTERISK_TECH_PVT(ast))) return -1;
	
	chan_misdn_log(1, p ? (p->bc ? p->bc->port : 0) : 0, "* ANSWER:\n");
	
	if (!p) {
		ast_log(LOG_WARNING, " --> Channel not connected ??\n");
		ast_queue_hangup(ast);
	}

	if (!p->bc) {
		chan_misdn_log(1, 0, " --> Got Answer, but there is no bc obj ??\n");

		ast_queue_hangup(ast);
	}

	tmp = pbx_builtin_getvar_helper(p->ast, "CRYPT_KEY");
	if (!ast_strlen_zero(tmp)) {
		chan_misdn_log(1, p->bc->port, " --> Connection will be BF crypted\n");
		ast_copy_string(p->bc->crypt_key, tmp, sizeof(p->bc->crypt_key));
	} else {
		chan_misdn_log(3, p->bc->port, " --> Connection is without BF encryption\n");
	}

	tmp = pbx_builtin_getvar_helper(ast, "MISDN_DIGITAL_TRANS");
	if (!ast_strlen_zero(tmp) && ast_true(tmp)) {
		chan_misdn_log(1, p->bc->port, " --> Connection is transparent digital\n");
		p->bc->nodsp = 1;
		p->bc->hdlc = 0;
		p->bc->nojitter = 1;
	}

	p->state = MISDN_CONNECTED;
	stop_indicate(p);

	if ( ast_strlen_zero(p->bc->cad) ) {
		chan_misdn_log(2,p->bc->port," --> empty cad using dad\n");
		ast_copy_string(p->bc->cad, p->bc->dad, sizeof(p->bc->cad));
	}

	misdn_lib_send_event( p->bc, EVENT_CONNECT);
	start_bc_tones(p);

	return 0;
}

static int misdn_digit_begin(struct ast_channel *chan, char digit)
{
	/* XXX Modify this callback to support Asterisk controlling the length of DTMF */
	return 0;
}

static int misdn_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct chan_list *p;
	struct misdn_bchannel *bc;

	if (!ast || !(p = MISDN_ASTERISK_TECH_PVT(ast))) return -1;

	bc = p->bc;
	chan_misdn_log(1, bc ? bc->port : 0, "* IND : Digit %c\n", digit);
	
	if (!bc) {
		ast_log(LOG_WARNING, " --> !! Got Digit Event without having bchannel Object\n");
		return -1;
	}
	
	switch (p->state ) {
	case MISDN_CALLING:
		{
			int l;		
			char buf[8];
			buf[0]=digit;
			buf[1]=0;
			
			l = sizeof(bc->infos_pending);
			strncat(bc->infos_pending, buf, l - strlen(bc->infos_pending) - 1);
		}
		break;
	case MISDN_CALLING_ACKNOWLEDGE:
		{
			bc->info_dad[0]=digit;
			bc->info_dad[1]=0;
			
			{
				int l = sizeof(bc->dad);
				strncat(bc->dad, bc->info_dad, l - strlen(bc->dad) - 1);
			}
			{
				int l = sizeof(p->ast->exten);
				strncpy(p->ast->exten, bc->dad, l);
				p->ast->exten[l-1] = 0;
			}
			
			misdn_lib_send_event( bc, EVENT_INFORMATION);
		}
		break;
	default:	
			/* Do not send Digits in CONNECTED State, when
			 * the other side is too mISDN. */
			if (p->other_ch ) 
				return 0;

			if ( bc->send_dtmf ) 
				send_digit_to_chan(p,digit);
		break;
	}

	return 0;
}


static int misdn_fixup(struct ast_channel *oldast, struct ast_channel *ast)
{
	struct chan_list *p;

	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast) )) return -1;

	chan_misdn_log(1, p->bc ? p->bc->port : 0, "* IND: Got Fixup State:%s L3id:%x\n", misdn_get_ch_state(p), p->l3id);

	p->ast = ast;

	return 0;
}



static int misdn_indication(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct chan_list *p;

	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast))) {
		ast_log(LOG_WARNING, "Returned -1 in misdn_indication\n");
		return -1;
	}
	
	if (!p->bc ) {
		chan_misdn_log(1, 0, "* IND : Indication from %s\n", ast->exten);
		ast_log(LOG_WARNING, "Private Pointer but no bc ?\n");
		return -1;
	}
	
	chan_misdn_log(5, p->bc->port, "* IND : Indication [%d] from %s\n", cond, ast->exten);
	
	switch (cond) {
	case AST_CONTROL_BUSY:
		chan_misdn_log(1, p->bc->port, "* IND :\tbusy pid:%d\n", p->bc ? p->bc->pid : -1);
		ast_setstate(ast, AST_STATE_BUSY);

		p->bc->out_cause = AST_CAUSE_USER_BUSY;
		if (p->state != MISDN_CONNECTED) {
			start_bc_tones(p);
			misdn_lib_send_event( p->bc, EVENT_DISCONNECT);
		} else {
			chan_misdn_log(-1, p->bc->port, " --> !! Got Busy in Connected State !?! ast:%s\n", ast->name);
		}
		return -1;
	case AST_CONTROL_RING:
		chan_misdn_log(1, p->bc->port, "* IND :\tring pid:%d\n", p->bc ? p->bc->pid : -1);
		return -1;
	case AST_CONTROL_RINGING:
		chan_misdn_log(1, p->bc->port, "* IND :\tringing pid:%d\n", p->bc ? p->bc->pid : -1);
		switch (p->state) {
		case MISDN_ALERTING:
			chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d but I was Ringing before, so ignoring it\n", p->bc ? p->bc->pid : -1);
			break;
		case MISDN_CONNECTED:
			chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d but Connected, so just send TONE_ALERTING without state changes \n", p->bc ? p->bc->pid : -1);
			return -1;
		default:
			p->state = MISDN_ALERTING;
			chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d\n", p->bc ? p->bc->pid : -1);
			misdn_lib_send_event( p->bc, EVENT_ALERTING);

			if (p->other_ch && p->other_ch->bc) {
				if (misdn_inband_avail(p->other_ch->bc)) {
					chan_misdn_log(2, p->bc->port, " --> other End is mISDN and has inband info available\n");
					break;
				}

				if (!p->other_ch->bc->nt) {
					chan_misdn_log(2, p->bc->port, " --> other End is mISDN TE so it has inband info for sure (?)\n");
					break;
				}
			}

			chan_misdn_log(3, p->bc->port, " --> * SEND: State Ring pid:%d\n", p->bc ? p->bc->pid : -1);
			ast_setstate(ast, AST_STATE_RING);

			if (!p->bc->nt && (p->originator == ORG_MISDN) && !p->incoming_early_audio)
				chan_misdn_log(2, p->bc->port, " --> incoming_early_audio off\n");
			else 
				return -1;
		}
		break;
	case AST_CONTROL_ANSWER:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tanswer pid:%d\n", p->bc ? p->bc->pid : -1);
		start_bc_tones(p);
		break;
	case AST_CONTROL_TAKEOFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\ttakeoffhook pid:%d\n", p->bc ? p->bc->pid : -1);
		return -1;
	case AST_CONTROL_OFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\toffhook pid:%d\n", p->bc ? p->bc->pid : -1);
		return -1;
	case AST_CONTROL_FLASH:
		chan_misdn_log(1, p->bc->port, " --> *\tflash pid:%d\n", p->bc ? p->bc->pid : -1);
		break;
	case AST_CONTROL_PROGRESS:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tprogress pid:%d\n", p->bc ? p->bc->pid : -1);
		misdn_lib_send_event( p->bc, EVENT_PROGRESS);
		break;
	case AST_CONTROL_PROCEEDING:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tproceeding pid:%d\n", p->bc ? p->bc->pid : -1);
		misdn_lib_send_event( p->bc, EVENT_PROCEEDING);
		break;
	case AST_CONTROL_CONGESTION:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tcongestion pid:%d\n", p->bc ? p->bc->pid : -1);

		p->bc->out_cause = AST_CAUSE_SWITCH_CONGESTION;
		start_bc_tones(p);
		misdn_lib_send_event( p->bc, EVENT_DISCONNECT);

		if (p->bc->nt) {
			hanguptone_indicate(p);
		}
		break;
	case -1 :
		chan_misdn_log(1, p->bc->port, " --> * IND :\t-1! (stop indication) pid:%d\n", p->bc ? p->bc->pid : -1);

		stop_indicate(p);

		if (p->state == MISDN_CONNECTED) 
			start_bc_tones(p);
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, p->mohinterpret); 
		chan_misdn_log(1, p->bc->port, " --> *\tHOLD pid:%d\n", p->bc ? p->bc->pid : -1);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		chan_misdn_log(1, p->bc->port, " --> *\tUNHOLD pid:%d\n", p->bc ? p->bc->pid : -1);
		break;
	default:
		chan_misdn_log(1, p->bc->port, " --> * Unknown Indication:%d pid:%d\n", cond, p->bc ? p->bc->pid : -1);
	}
  
	return 0;
}

static int misdn_hangup(struct ast_channel *ast)
{
	struct chan_list *p;
	struct misdn_bchannel *bc = NULL;
	const char *varcause = NULL;

	ast_log(LOG_DEBUG, "misdn_hangup(%s)\n", ast->name);

	if (!ast || ! (p=MISDN_ASTERISK_TECH_PVT(ast) ) ) return -1;

	if (!p) {
		chan_misdn_log(3, 0, "misdn_hangup called, without chan_list obj.\n");
		return 0 ;
	}

	bc = p->bc;

	if (bc) {
		const char *tmp=pbx_builtin_getvar_helper(ast,"MISDN_USERUSER");
		if (tmp) {
			ast_log(LOG_NOTICE, "MISDN_USERUSER: %s\n", tmp);
			strcpy(bc->uu, tmp);
			bc->uulen=strlen(bc->uu);
		}
	}

	MISDN_ASTERISK_TECH_PVT(ast) = NULL;
	p->ast = NULL;

	if (ast->_state == AST_STATE_RESERVED || 
		p->state == MISDN_NOTHING || 
		p->state == MISDN_HOLDED || 
		p->state == MISDN_HOLD_DISCONNECT ) {

		CLEAN_CH:
		/* between request and call */
		ast_log(LOG_DEBUG, "State Reserved (or nothing) => chanIsAvail\n");
		MISDN_ASTERISK_TECH_PVT(ast) = NULL;
	
		ast_mutex_lock(&release_lock);
		cl_dequeue_chan(&cl_te, p);
		close(p->pipe[0]);
		close(p->pipe[1]);
		free(p);
		ast_mutex_unlock(&release_lock);
		
		if (bc)
			misdn_lib_release(bc);
		
		return 0;
	}

	if (!bc) {
		ast_log(LOG_WARNING, "Hangup with private but no bc ? state:%s l3id:%x\n", misdn_get_ch_state(p), p->l3id);
		goto CLEAN_CH;
	}


	p->need_hangup = 0;
	p->need_queue_hangup = 0;
	p->need_busy = 0;


	if (!p->bc->nt) 
		stop_bc_tones(p);

	bc->out_cause = ast->hangupcause ? ast->hangupcause : AST_CAUSE_NORMAL_CLEARING;

	if ((varcause = pbx_builtin_getvar_helper(ast, "HANGUPCAUSE")) ||
		(varcause = pbx_builtin_getvar_helper(ast, "PRI_CAUSE"))) {
		int tmpcause = atoi(varcause);
		bc->out_cause = tmpcause ? tmpcause : AST_CAUSE_NORMAL_CLEARING;
	}

	chan_misdn_log(1, bc->port, "* IND : HANGUP\tpid:%d ctx:%s dad:%s oad:%s State:%s\n", p->bc ? p->bc->pid : -1, ast->context, ast->exten, ast->cid.cid_num, misdn_get_ch_state(p));
	chan_misdn_log(3, bc->port, " --> l3id:%x\n", p->l3id);
	chan_misdn_log(3, bc->port, " --> cause:%d\n", bc->cause);
	chan_misdn_log(2, bc->port, " --> out_cause:%d\n", bc->out_cause);
	chan_misdn_log(2, bc->port, " --> state:%s\n", misdn_get_ch_state(p));

	switch (p->state) {
	case MISDN_INCOMING_SETUP:
		/* This is the only place in misdn_hangup, where we 
		 * can call release_chan, else it might create lot's of trouble
		 * */
		ast_log(LOG_NOTICE, "release channel, in INCOMING_SETUP state.. no other events happened\n");
		release_chan(bc);

		p->state = MISDN_CLEANING;
		if (bc->need_release_complete)
			misdn_lib_send_event( bc, EVENT_RELEASE_COMPLETE);
		break;
	case MISDN_HOLDED:
	case MISDN_DIALING:
		start_bc_tones(p);
		hanguptone_indicate(p);

		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
		break;
	case MISDN_CALLING_ACKNOWLEDGE:
		start_bc_tones(p);
		hanguptone_indicate(p);

		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
		break;

	case MISDN_CALLING:
	case MISDN_ALERTING:
	case MISDN_PROGRESS:
	case MISDN_PROCEEDING:
		if (p->originator != ORG_AST) 
			hanguptone_indicate(p);

		/*p->state=MISDN_CLEANING;*/
		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
		break;
	case MISDN_CONNECTED:
	case MISDN_PRECONNECTED:
		/*  Alerting or Disconnect */
		if (p->bc->nt) {
			start_bc_tones(p);
			hanguptone_indicate(p);
			p->bc->progress_indicator = INFO_PI_INBAND_AVAILABLE;
		}
		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);

		/*p->state=MISDN_CLEANING;*/
		break;
	case MISDN_DISCONNECTED:
		if (bc->need_release)
			misdn_lib_send_event( bc, EVENT_RELEASE);
		p->state = MISDN_CLEANING; /* MISDN_HUNGUP_FROM_AST; */
		break;

	case MISDN_RELEASED:
	case MISDN_CLEANING:
		p->state = MISDN_CLEANING;
		break;

	case MISDN_BUSY:
		break;

	case MISDN_HOLD_DISCONNECT:
		/* need to send release here */
		chan_misdn_log(1, bc->port, " --> cause %d\n", bc->cause);
		chan_misdn_log(1, bc->port, " --> out_cause %d\n", bc->out_cause);

		bc->out_cause = -1;
		if (bc->need_release)
			misdn_lib_send_event(bc, EVENT_RELEASE);
		p->state = MISDN_CLEANING;
		break;
	default:
		if (bc->nt) {
			bc->out_cause = -1;
			if (bc->need_release)
				misdn_lib_send_event(bc, EVENT_RELEASE);
			p->state = MISDN_CLEANING; 
		} else {
			if (bc->need_disconnect)
				misdn_lib_send_event(bc, EVENT_DISCONNECT);
		}
	}

	p->state = MISDN_CLEANING;

	chan_misdn_log(3, bc->port, " --> Channel: %s hanguped new state:%s\n", ast->name, misdn_get_ch_state(p));

	return 0;
}


static struct ast_frame *process_ast_dsp(struct chan_list *tmp, struct ast_frame *frame)
{
	struct ast_frame *f,*f2;
 
 	if (tmp->trans) {
 		f2 = ast_translate(tmp->trans, frame, 0);
 		f = ast_dsp_process(tmp->ast, tmp->dsp, f2);
 	} else {
		chan_misdn_log(0, tmp->bc->port, "No T-Path found\n");
		return NULL;
	}

 	if (!f || (f->frametype != AST_FRAME_DTMF))
 		return frame;
 
 	ast_log(LOG_DEBUG, "Detected inband DTMF digit: %c\n", f->subclass);
 
 	if (tmp->faxdetect && (f->subclass == 'f')) {
 		/* Fax tone -- Handle and return NULL */
 		if (!tmp->faxhandled) {
  			struct ast_channel *ast = tmp->ast;
 			tmp->faxhandled++;
 			chan_misdn_log(0, tmp->bc->port, "Fax detected, preparing %s for fax transfer.\n", ast->name);
 			tmp->bc->rxgain = 0;
 			isdn_lib_update_rxgain(tmp->bc);
 			tmp->bc->txgain = 0;
 			isdn_lib_update_txgain(tmp->bc);
#ifdef MISDN_1_2
			*tmp->bc->pipeline = 0;
#else
 			tmp->bc->ec_enable = 0;
#endif
 			isdn_lib_update_ec(tmp->bc);
 			isdn_lib_stop_dtmf(tmp->bc);
 			switch (tmp->faxdetect) {
 			case 1:
  				if (strcmp(ast->exten, "fax")) {
 					char *context;
 					char context_tmp[BUFFERSIZE];
 					misdn_cfg_get(tmp->bc->port, MISDN_CFG_FAXDETECT_CONTEXT, &context_tmp, sizeof(context_tmp));
 					context = ast_strlen_zero(context_tmp) ? (ast_strlen_zero(ast->macrocontext) ? ast->context : ast->macrocontext) : context_tmp;
 					if (ast_exists_extension(ast, context, "fax", 1, ast->cid.cid_num)) {
  						if (option_verbose > 2)
 							ast_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension (context:%s)\n", ast->name, context);
  						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
  						pbx_builtin_setvar_helper(ast,"FAXEXTEN",ast->exten);
 						if (ast_async_goto(ast, context, "fax", 1))
 							ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, context);
  					} else
 						ast_log(LOG_NOTICE, "Fax detected, but no fax extension ctx:%s exten:%s\n", context, ast->exten);
 				} else {
  					ast_log(LOG_DEBUG, "Already in a fax extension, not redirecting\n");
				}
 				break;
 			case 2:
 				ast_verbose(VERBOSE_PREFIX_3 "Not redirecting %s to fax extension, nojump is set.\n", ast->name);
 				break;
 			}
 		} else {
 			ast_log(LOG_DEBUG, "Fax already handled\n");
		}
  	}
 	
 	if (tmp->ast_dsp && (f->subclass != 'f')) {
 		chan_misdn_log(2, tmp->bc->port, " --> * SEND: DTMF (AST_DSP) :%c\n", f->subclass);
 	}

	return f;
}


static struct ast_frame *misdn_read(struct ast_channel *ast)
{
	struct chan_list *tmp;
	fd_set rrfs;
	struct timeval tv;
	int len, t;

	if (!ast) {
		chan_misdn_log(1, 0, "misdn_read called without ast\n");
		return NULL;
	}
 	if (!(tmp = MISDN_ASTERISK_TECH_PVT(ast))) {
		chan_misdn_log(1, 0, "misdn_read called without ast->pvt\n");
		return NULL;
	}

	if (!tmp->bc && !(tmp->state == MISDN_HOLDED)) {
		chan_misdn_log(1, 0, "misdn_read called without bc\n");
		return NULL;
	}

	tv.tv_sec=0;
	tv.tv_usec=20000;

	FD_ZERO(&rrfs);
	FD_SET(tmp->pipe[0],&rrfs);

	t=select(FD_SETSIZE,&rrfs,NULL, NULL,&tv);

	if (!t) {
		chan_misdn_log(3, tmp->bc->port, "read Select Timed out\n");
		len=160;
	}

	if (t<0) {
		chan_misdn_log(-1, tmp->bc->port, "Select Error (err=%s)\n",strerror(errno));
		return NULL;
	}

	if (FD_ISSET(tmp->pipe[0],&rrfs)) {
		len=read(tmp->pipe[0],tmp->ast_rd_buf,sizeof(tmp->ast_rd_buf));

		if (len<=0) {
			/* we hangup here, since our pipe is closed */
			chan_misdn_log(2,tmp->bc->port,"misdn_read: Pipe closed, hanging up\n");
			return NULL;
		}

	} else {
		return NULL;
	}

	tmp->frame.frametype = AST_FRAME_VOICE;
	tmp->frame.subclass = AST_FORMAT_ALAW;
	tmp->frame.datalen = len;
	tmp->frame.samples = len;
	tmp->frame.mallocd = 0;
	tmp->frame.offset = 0;
	tmp->frame.delivery = ast_tv(0,0);
	tmp->frame.src = NULL;
	tmp->frame.data = tmp->ast_rd_buf;

	if (tmp->faxdetect && !tmp->faxhandled) {
		if (tmp->faxdetect_timeout) {
			if (ast_tvzero(tmp->faxdetect_tv)) {
				tmp->faxdetect_tv = ast_tvnow();
				chan_misdn_log(2, tmp->bc->port, "faxdetect: starting detection with timeout: %ds ...\n", tmp->faxdetect_timeout);
				return process_ast_dsp(tmp, &tmp->frame);
			} else {
				struct timeval tv_now = ast_tvnow();
				int diff = ast_tvdiff_ms(tv_now, tmp->faxdetect_tv);
				if (diff <= (tmp->faxdetect_timeout * 1000)) {
					chan_misdn_log(5, tmp->bc->port, "faxdetect: detecting ...\n");
					return process_ast_dsp(tmp, &tmp->frame);
				} else {
					chan_misdn_log(2, tmp->bc->port, "faxdetect: stopping detection (time ran out) ...\n");
					tmp->faxdetect = 0;
					return &tmp->frame;
				}
			}
		} else {
			chan_misdn_log(5, tmp->bc->port, "faxdetect: detecting ... (no timeout)\n");
			return process_ast_dsp(tmp, &tmp->frame);
		}
	} else {
		if (tmp->ast_dsp)
			return process_ast_dsp(tmp, &tmp->frame);
		else
			return &tmp->frame;
	}
}


static int misdn_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct chan_list *ch;
	int i  = 0;
	
	if (!ast || ! (ch = MISDN_ASTERISK_TECH_PVT(ast)) ) return -1;

	if (ch->state == MISDN_HOLDED) {
		chan_misdn_log(7, 0, "misdn_write: Returning because holded\n");
		return 0;
	}
	
	if (!ch->bc ) {
		ast_log(LOG_WARNING, "private but no bc\n");
		return -1;
	}
	
	if (ch->notxtone) {
		chan_misdn_log(7, ch->bc->port, "misdn_write: Returning because notxtone\n");
		return 0;
	}


	if (!frame->subclass) {
		chan_misdn_log(4, ch->bc->port, "misdn_write: * prods us\n");
		return 0;
	}
	
	if (!(frame->subclass & prefformat)) {
		
		chan_misdn_log(-1, ch->bc->port, "Got Unsupported Frame with Format:%d\n", frame->subclass);
		return 0;
	}
	

	if (!frame->samples ) {
		chan_misdn_log(4, ch->bc->port, "misdn_write: zero write\n");
		
		if (!strcmp(frame->src,"ast_prod")) {
			chan_misdn_log(1, ch->bc->port, "misdn_write: state (%s) prodded.\n", misdn_get_ch_state(ch));

			if (ch->ts) {
				chan_misdn_log(4, ch->bc->port, "Starting Playtones\n");
				misdn_lib_tone_generator_start(ch->bc);
			}
			return 0;
		}

		return -1;
	}

	if ( ! ch->bc->addr ) {
		chan_misdn_log(8, ch->bc->port, "misdn_write: no addr for bc dropping:%d\n", frame->samples);
		return 0;
	}
	
#ifdef MISDN_DEBUG
	{
		int i, max = 5 > frame->samples ? frame->samples : 5;

		printf("write2mISDN %p %d bytes: ", p, frame->samples);

		for (i = 0; i < max; i++)
			printf("%2.2x ", ((char*) frame->data)[i]);
		printf ("\n");
	}
#endif

	switch (ch->bc->bc_state) {
	case BCHAN_ACTIVATED:
	case BCHAN_BRIDGED:
		break;
	default:
		if (!ch->dropped_frame_cnt)
			chan_misdn_log(5, ch->bc->port, "BC not active (nor bridged) dropping: %d frames addr:%x exten:%s cid:%s ch->state:%s bc_state:%d l3id:%x\n", frame->samples, ch->bc->addr, ast->exten, ast->cid.cid_num, misdn_get_ch_state( ch), ch->bc->bc_state, ch->bc->l3_id);
		
		ch->dropped_frame_cnt++;
		if (ch->dropped_frame_cnt > 100) {
			ch->dropped_frame_cnt = 0;
			chan_misdn_log(5, ch->bc->port, "BC not active (nor bridged) dropping: %d frames addr:%x  dropped > 100 frames!\n", frame->samples, ch->bc->addr);
		}

		return 0;
	}

	chan_misdn_log(9, ch->bc->port, "Sending :%d bytes to MISDN\n", frame->samples);
	if ( !ch->bc->nojitter && misdn_cap_is_speech(ch->bc->capability) ) {
		/* Buffered Transmit (triggered by read from isdn side)*/
		if (misdn_jb_fill(ch->jb, frame->data, frame->samples) < 0) {
			if (ch->bc->active)
				cb_log(0, ch->bc->port, "Misdn Jitterbuffer Overflow.\n");
		}
		
	} else {
		/*transmit without jitterbuffer*/
		i = misdn_lib_tx2misdn_frm(ch->bc, frame->data, frame->samples);
	}

	return 0;
}




static enum ast_bridge_result  misdn_bridge (struct ast_channel *c0,
				      struct ast_channel *c1, int flags,
				      struct ast_frame **fo,
				      struct ast_channel **rc,
				      int timeoutms)

{
	struct chan_list *ch1, *ch2;
	struct ast_channel *carr[2], *who;
	int to = -1;
	struct ast_frame *f;
	int p1_b, p2_b;
	int bridging;
  
	ch1 = get_chan_by_ast(c0);
	ch2 = get_chan_by_ast(c1);

	carr[0] = c0;
	carr[1] = c1;
  
	if (!(ch1 && ch2))
		return -1;

	misdn_cfg_get(ch1->bc->port, MISDN_CFG_BRIDGING, &p1_b, sizeof(int));
	misdn_cfg_get(ch2->bc->port, MISDN_CFG_BRIDGING, &p2_b, sizeof(int));

	if (! p1_b || ! p2_b) {
		ast_log(LOG_NOTICE, "Falling back to Asterisk bridging\n");
		return AST_BRIDGE_FAILED;
	}

	misdn_cfg_get(0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
	if (bridging) {
		/* trying to make a mISDN_dsp conference */
		chan_misdn_log(1, ch1->bc->port, "I SEND: Making conference with Number:%d\n", ch1->bc->pid + 1);
		misdn_lib_bridge(ch1->bc, ch2->bc);
	}

	if (option_verbose > 2) 
		ast_verbose(VERBOSE_PREFIX_3 "Native bridging %s and %s\n", c0->name, c1->name);

	chan_misdn_log(1, ch1->bc->port, "* Making Native Bridge between %s and %s\n", ch1->bc->oad, ch2->bc->oad);
 
	if (! (flags & AST_BRIDGE_DTMF_CHANNEL_0) )
		ch1->ignore_dtmf = 1;

	if (! (flags & AST_BRIDGE_DTMF_CHANNEL_1) )
		ch2->ignore_dtmf = 1;

	for (;/*ever*/;) {
		to = -1;
		who = ast_waitfor_n(carr, 2, &to);

		if (!who) {
			ast_log(LOG_NOTICE, "misdn_bridge: empty read, breaking out\n");
			break;
		}
		f = ast_read(who);

		if (!f || f->frametype == AST_FRAME_CONTROL) {
			/* got hangup .. */

			if (!f) 
				chan_misdn_log(4, ch1->bc->port, "Read Null Frame\n");
			else
				chan_misdn_log(4, ch1->bc->port, "Read Frame Control class:%d\n", f->subclass);

			*fo = f;
			*rc = who;
			break;
		}
		
		if ( f->frametype == AST_FRAME_DTMF ) {
			chan_misdn_log(1, 0, "Read DTMF %d from %s\n", f->subclass, who->exten);

			*fo = f;
			*rc = who;
			break;
		}
	
#if 0
		if (f->frametype == AST_FRAME_VOICE) {
			chan_misdn_log(1, ch1->bc->port, "I SEND: Splitting conference with Number:%d\n", ch1->bc->pid +1);
	
			continue;
		}
#endif

		if (who == c0) {
			ast_write(c1, f);
		}
		else {
			ast_write(c0, f);
		}
	}

	chan_misdn_log(1, ch1->bc->port, "I SEND: Splitting conference with Number:%d\n", ch1->bc->pid + 1);

	misdn_lib_split_bridge(ch1->bc, ch2->bc);

	return AST_BRIDGE_COMPLETE;
}

/** AST INDICATIONS END **/

static int dialtone_indicate(struct chan_list *cl)
{
	const struct tone_zone_sound *ts = NULL;
	struct ast_channel *ast = cl->ast;
	int nd = 0;

	if (!ast) {
		chan_misdn_log(0, cl->bc->port, "No Ast in dialtone_indicate\n");
		return -1;
	}

	misdn_cfg_get(cl->bc->port, MISDN_CFG_NODIALTONE, &nd, sizeof(nd));

	if (nd) {
		chan_misdn_log(1, cl->bc->port, "Not sending Dialtone, because config wants it\n");
		return 0;
	}
	
	chan_misdn_log(3, cl->bc->port, " --> Dial\n");
	ts = ast_get_indication_tone(ast->zone, "dial");
	cl->ts = ts;	
	
	if (ts) {
		cl->notxtone = 0;
		cl->norxtone = 0;
		/* This prods us in misdn_write */
		ast_playtones_start(ast, 0, ts->data, 0);
	}

	return 0;
}

static int hanguptone_indicate(struct chan_list *cl)
{
	misdn_lib_send_tone(cl->bc, TONE_HANGUP);
	return 0;
}

static int stop_indicate(struct chan_list *cl)
{
	struct ast_channel *ast = cl->ast;

	if (!ast) {
		chan_misdn_log(0, cl->bc->port, "No Ast in stop_indicate\n");
		return -1;
	}

	chan_misdn_log(3, cl->bc->port, " --> None\n");
	misdn_lib_tone_generator_stop(cl->bc);
	ast_playtones_stop(ast);

	cl->ts = NULL;
	/*ast_deactivate_generator(ast);*/

	return 0;
}


static int start_bc_tones(struct chan_list* cl)
{
	misdn_lib_tone_generator_stop(cl->bc);
	cl->notxtone = 0;
	cl->norxtone = 0;
	return 0;
}

static int stop_bc_tones(struct chan_list *cl)
{
	if (!cl) return -1;

	cl->notxtone = 1;
	cl->norxtone = 1;
	
	return 0;
}


static struct chan_list *init_chan_list(int orig)
{
	struct chan_list *cl;

	cl = calloc(1, sizeof(struct chan_list));
	if (!cl) {
		chan_misdn_log(-1, 0, "misdn_request: malloc failed!");
		return NULL;
	}
	
	cl->originator = orig;
	cl->need_queue_hangup = 1;
	cl->need_hangup = 1;
	cl->need_busy = 1;
	cl->overlap_dial_task = -1;

	return cl;
}

static struct ast_channel *misdn_request(const char *type, int format, void *data, int *cause)
{
	struct ast_channel *tmp = NULL;
	char group[BUFFERSIZE + 1] = "";
	char dial_str[128];
	char *buf2 = ast_strdupa(data);
	char *ext;
	char *port_str;
	char *p = NULL;
	int channel = 0;
	int port = 0;
	struct misdn_bchannel *newbc = NULL;
	int dec = 0;

	struct chan_list *cl = init_chan_list(ORG_AST);

	snprintf(dial_str, sizeof(dial_str), "%s/%s", misdn_type, (char *) data);

	/*
	 * data is ---v
	 * Dial(mISDN/g:group_name[/extension[/options]])
	 * Dial(mISDN/port[:preselected_channel][/extension[/options]])
	 *
	 * The dial extension could be empty if you are using MISDN_KEYPAD
	 * to control ISDN provider features.
	 */
	port_str = strsep(&buf2, "/");
	if (!ast_strlen_zero(port_str)) {
		if (port_str[0] == 'g' && port_str[1] == ':' ) {
			/* We make a group call lets checkout which ports are in my group */
			port_str += 2;
			ast_copy_string(group, port_str, sizeof(group));
			chan_misdn_log(2, 0, " --> Group Call group: %s\n", group);
		} else if ((p = strchr(port_str, ':'))) {
			/* we have a preselected channel */
			*p = 0;
			channel = atoi(++p);
			port = atoi(port_str);
			chan_misdn_log(2, port, " --> Call on preselected Channel (%d).\n", channel);
		} else {
			port = atoi(port_str);
		}
	} else {
		ast_log(LOG_WARNING, " --> ! IND : Dial(%s) WITHOUT Port or Group, check extensions.conf\n", dial_str);
		return NULL;
	}

	ext = strsep(&buf2, "/");
	if (!ext) {
		ext = "";
	}

	if (misdn_cfg_is_group_method(group, METHOD_STANDARD_DEC)) {
		chan_misdn_log(4, port, " --> STARTING STANDARD DEC...\n");
		dec = 1;
	}

	if (!ast_strlen_zero(group)) {
		char cfg_group[BUFFERSIZE + 1];
		struct robin_list *rr = NULL;

		/* Group dial */

		if (misdn_cfg_is_group_method(group, METHOD_ROUND_ROBIN)) {
			chan_misdn_log(4, port, " --> STARTING ROUND ROBIN...\n");
			rr = get_robin_position(group);
		}

		if (rr) {
			int port_start = 0;
			int port_bak = rr->port;
			int chan_bak = rr->channel;

			if (!rr->port)
				rr->port = misdn_cfg_get_next_port_spin(rr->port);
			
			for (; rr->port > 0; rr->port = misdn_cfg_get_next_port_spin(rr->port)) {
				int port_up;
				int check;
				int max_chan;
				int last_chance = 0;

				misdn_cfg_get(rr->port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);
				if (strcasecmp(cfg_group, group))
					continue;

				misdn_cfg_get(rr->port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(int));
				port_up = misdn_lib_port_up(rr->port, check);

				if (check && !port_up) 
					chan_misdn_log(1, rr->port, "L1 is not Up on this Port\n");

				if (check && port_up < 0)
					ast_log(LOG_WARNING,"This port (%d) is blocked\n", rr->port);

				if ((port_start == rr->port) && (port_up <= 0))
					break;

				if (!port_start)
					port_start = rr->port;

				if (port_up <= 0)
					continue;

				max_chan = misdn_lib_get_maxchans(rr->port);

				for (++rr->channel; !last_chance && rr->channel <= max_chan; ++rr->channel) {
					if (rr->port == port_bak && rr->channel == chan_bak)
						last_chance = 1;

					chan_misdn_log(1, 0, "trying port:%d channel:%d\n", rr->port, rr->channel);
					newbc = misdn_lib_get_free_bc(rr->port, rr->channel, 0, 0);
					if (newbc) {
						chan_misdn_log(4, rr->port, " Success! Found port:%d channel:%d\n", newbc->port, newbc->channel);
						if (port_up)
							chan_misdn_log(4, rr->port, "portup:%d\n",  port_up);
						port = rr->port;
						break;
					}
				}

				if (newbc || last_chance)
					break;

				rr->channel = 0;
			}
			if (!newbc) {
				rr->port = port_bak;
				rr->channel = chan_bak;
			}
		} else {		
			for (port = misdn_cfg_get_next_port(0); port > 0;
				 port = misdn_cfg_get_next_port(port)) {

				misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);

				chan_misdn_log(3, port, "Group [%s] Port [%d]\n", group, port);
				if (!strcasecmp(cfg_group, group)) {
					int port_up;
					int check;
					misdn_cfg_get(port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(int));
					port_up = misdn_lib_port_up(port, check);

					chan_misdn_log(4, port, "portup:%d\n", port_up);

					if (port_up > 0) {
						newbc = misdn_lib_get_free_bc(port, 0, 0, dec);
						if (newbc)
							break;
					}
				}
			}
		}
		
		/* Group dial failed ?*/
		if (!newbc) {
			ast_log(LOG_WARNING, 
					"Could not Dial out on group '%s'.\n"
					"\tEither the L2 and L1 on all of these ports where DOWN (see 'show application misdn_check_l2l1')\n"
					"\tOr there was no free channel on none of the ports\n\n"
					, group);
			return NULL;
		}
	} else {
		/* 'Normal' Port dial * Port dial */
		if (channel)
			chan_misdn_log(1, port, " --> preselected_channel: %d\n", channel);
		newbc = misdn_lib_get_free_bc(port, channel, 0, dec);

		if (!newbc) {
			ast_log(LOG_WARNING, "Could not create channel on port:%d with extensions:%s\n", port, ext);
			return NULL;
		}
	}
	

	/* create ast_channel and link all the objects together */
	cl->bc = newbc;
	
	tmp = misdn_new(cl, AST_STATE_RESERVED, ext, NULL, format, port, channel);
	if (!tmp) {
		ast_log(LOG_ERROR,"Could not create Asterisk object\n");
		return NULL;
	}

	cl->ast=tmp;
	
	/* register chan in local list */
	cl_queue_chan(&cl_te, cl) ;
	
	/* fill in the config into the objects */
	read_config(cl, ORG_AST);

	/* important */
	cl->need_hangup = 0;
	
	return tmp;
}


static int misdn_send_text(struct ast_channel *chan, const char *text)
{
	struct chan_list *tmp = chan->tech_pvt;
	
	if (tmp && tmp->bc) {
		ast_copy_string(tmp->bc->display, text, sizeof(tmp->bc->display));
		misdn_lib_send_event(tmp->bc, EVENT_INFORMATION);
	} else {
		ast_log(LOG_WARNING, "No chan_list but send_text request?\n");
		return -1;
	}
	
	return 0;
}

static struct ast_channel_tech misdn_tech = {
	.type = "mISDN",
	.description = "Channel driver for mISDN Support (Bri/Pri)",
	.capabilities = AST_FORMAT_ALAW ,
	.requester = misdn_request,
	.send_digit_begin = misdn_digit_begin,
	.send_digit_end = misdn_digit_end,
	.call = misdn_call,
	.bridge = misdn_bridge, 
	.hangup = misdn_hangup,
	.answer = misdn_answer,
	.read = misdn_read,
	.write = misdn_write,
	.indicate = misdn_indication,
	.fixup = misdn_fixup,
	.send_text = misdn_send_text,
	.properties = 0
};

static struct ast_channel_tech misdn_tech_wo_bridge = {
	.type = "mISDN",
	.description = "Channel driver for mISDN Support (Bri/Pri)",
	.capabilities = AST_FORMAT_ALAW ,
	.requester = misdn_request,
	.send_digit_begin = misdn_digit_begin,
	.send_digit_end = misdn_digit_end,
	.call = misdn_call,
	.hangup = misdn_hangup,
	.answer = misdn_answer,
	.read = misdn_read,
	.write = misdn_write,
	.indicate = misdn_indication,
	.fixup = misdn_fixup,
	.send_text = misdn_send_text,
	.properties = 0
};


static int glob_channel = 0;

static void update_name(struct ast_channel *tmp, int port, int c) 
{
	int chan_offset = 0;
	int tmp_port = misdn_cfg_get_next_port(0);
	for (; tmp_port > 0; tmp_port = misdn_cfg_get_next_port(tmp_port)) {
		if (tmp_port == port)
			break;
		chan_offset += misdn_lib_port_is_pri(tmp_port) ? 30 : 2;	
	}
	if (c < 0)
		c = 0;

	ast_string_field_build(tmp, name, "%s/%d-u%d",
		misdn_type, chan_offset + c, glob_channel++);

	chan_misdn_log(3, port, " --> updating channel name to [%s]\n", tmp->name);
}

static struct ast_channel *misdn_new(struct chan_list *chlist, int state,  char *exten, char *callerid, int format, int port, int c)
{
	struct ast_channel *tmp;
	char *cid_name = 0, *cid_num = 0;
	int chan_offset = 0;
	int tmp_port = misdn_cfg_get_next_port(0);
	int bridging;

	for (; tmp_port > 0; tmp_port = misdn_cfg_get_next_port(tmp_port)) {
		if (tmp_port == port)
			break;
		chan_offset += misdn_lib_port_is_pri(tmp_port) ? 30 : 2;
	}
	if (c < 0)
		c = 0;

	if (callerid) {
		ast_callerid_parse(callerid, &cid_name, &cid_num);
	}

	tmp = ast_channel_alloc(1, state, cid_num, cid_name, "", exten, "", 0, "%s/%d-u%d", misdn_type, chan_offset + c, glob_channel++);
	if (tmp) {
		chan_misdn_log(2, 0, " --> * NEW CHANNEL dad:%s oad:%s\n", exten, callerid);

		tmp->nativeformats = prefformat;

		tmp->readformat = format;
		tmp->rawreadformat = format;
		tmp->writeformat = format;
		tmp->rawwriteformat = format;
    
		tmp->tech_pvt = chlist;

		misdn_cfg_get(0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));

		if (bridging)
			tmp->tech = &misdn_tech;
		else
			tmp->tech = &misdn_tech_wo_bridge;

		tmp->writeformat = format;
		tmp->readformat = format;
		tmp->priority=1;

		if (exten) 
			ast_copy_string(tmp->exten, exten, sizeof(tmp->exten));
		else
			chan_misdn_log(1, 0, "misdn_new: no exten given.\n");

		if (callerid)
			/* Don't use ast_set_callerid() here because it will
			 * generate a needless NewCallerID event */
			tmp->cid.cid_ani = ast_strdup(cid_num);

		if (pipe(chlist->pipe) < 0)
			perror("Pipe failed\n");
		tmp->fds[0] = chlist->pipe[0];

		if (state == AST_STATE_RING)
			tmp->rings = 1;
		else
			tmp->rings = 0;
		
	} else {
		chan_misdn_log(-1, 0, "Unable to allocate channel structure\n");
	}
	
	return tmp;
}

static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help = list;
	for (; help; help = help->next) {
		if (help->bc == bc) return help;
	}

	chan_misdn_log(6, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n", bc->oad, bc->dad);

	return NULL;
}

static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid)
{
	struct chan_list *help = list;
	for (; help; help = help->next) {
		if ( help->bc && (help->bc->pid == pid) ) return help;
	}

	chan_misdn_log(6, 0, "$$$ find_chan: No channel found for pid:%d\n", pid);

	return NULL;
}

static struct chan_list *find_holded(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help = list;

	if (bc->pri) return NULL;

	chan_misdn_log(6, bc->port, "$$$ find_holded: channel:%d oad:%s dad:%s\n", bc->channel, bc->oad, bc->dad);
	for (;help; help = help->next) {
		chan_misdn_log(4, bc->port, "$$$ find_holded: --> holded:%d channel:%d\n", help->state == MISDN_HOLDED, help->hold_info.channel);
		if ((help->state == MISDN_HOLDED) && 
			(help->hold_info.port == bc->port))
			return help;
	}
	chan_misdn_log(6, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n", bc->oad, bc->dad);

	return NULL;
}


static struct chan_list *find_holded_l3(struct chan_list *list, unsigned long l3_id, int w) 
{
	struct chan_list *help = list;

	for (; help; help = help->next) {
		if ( (help->state == MISDN_HOLDED) &&
			 (help->l3id == l3_id)   
			) 
			return help;
	}

	return NULL;
}

static void cl_queue_chan(struct chan_list **list, struct chan_list *chan)
{
	chan_misdn_log(4, chan->bc ? chan->bc->port : 0, "* Queuing chan %p\n", chan);
  
	ast_mutex_lock(&cl_te_lock);
	if (!*list) {
		*list = chan;
	} else {
		struct chan_list *help = *list;
		for (; help->next; help = help->next); 
		help->next = chan;
	}
	chan->next = NULL;
	ast_mutex_unlock(&cl_te_lock);
}

static void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan) 
{
	struct chan_list *help;

	if (chan->dsp) 
		ast_dsp_free(chan->dsp);
	if (chan->trans)
		ast_translator_free_path(chan->trans);

	ast_mutex_lock(&cl_te_lock);
	if (!*list) {
		ast_mutex_unlock(&cl_te_lock);
		return;
	}
  
	if (*list == chan) {
		*list = (*list)->next;
		ast_mutex_unlock(&cl_te_lock);
		return;
	}
  
	for (help = *list; help->next; help = help->next) {
		if (help->next == chan) {
			help->next = help->next->next;
			ast_mutex_unlock(&cl_te_lock);
			return;
		}
	}
	
	ast_mutex_unlock(&cl_te_lock);
}

/** Channel Queue End **/


static int pbx_start_chan(struct chan_list *ch)
{
	int ret = ast_pbx_start(ch->ast);	

	if (ret >= 0) 
		ch->need_hangup = 0;
	else
		ch->need_hangup = 1;

	return ret;
}

static void hangup_chan(struct chan_list *ch)
{
	int port = ch ? (ch->bc ? ch->bc->port : 0) : 0;
	if (!ch) {
		cb_log(1, 0, "Cannot hangup chan, no ch\n");
		return;
	}

	cb_log(5, port, "hangup_chan called\n");

	if (ch->need_hangup) {
		cb_log(2, port, " --> hangup\n");
		send_cause2ast(ch->ast, ch->bc, ch);
		ch->need_hangup = 0;
		ch->need_queue_hangup = 0;
		if (ch->ast)
			ast_hangup(ch->ast);
		return;
	}

	if (!ch->need_queue_hangup) {
		cb_log(2, port, " --> No need to queue hangup\n");
	}

	ch->need_queue_hangup = 0;
	if (ch->ast) {
		send_cause2ast(ch->ast, ch->bc, ch);

		if (ch->ast)
			ast_queue_hangup(ch->ast);
		cb_log(2, port, " --> queue_hangup\n");
	} else {
		cb_log(1, port, "Cannot hangup chan, no ast\n");
	}
}

/** Isdn asks us to release channel, pendant to misdn_hangup **/
static void release_chan(struct misdn_bchannel *bc) {
	struct ast_channel *ast = NULL;

	ast_mutex_lock(&release_lock);
	{
		struct chan_list *ch=find_chan_by_bc(cl_te, bc);
		if (!ch)  {
			chan_misdn_log(1, bc->port, "release_chan: Ch not found!\n");
			ast_mutex_unlock(&release_lock);
			return;
		}

		if (ch->ast) {
			ast = ch->ast;
		} 

		chan_misdn_log(5, bc->port, "release_chan: bc with l3id: %x\n", bc->l3_id);

		/*releasing jitterbuffer*/
		if (ch->jb ) {
			misdn_jb_destroy(ch->jb);
			ch->jb = NULL;
		} else {
			if (!bc->nojitter)
				chan_misdn_log(5, bc->port, "Jitterbuffer already destroyed.\n");
		}

		if (ch->overlap_dial) {
			if (ch->overlap_dial_task != -1) {
				misdn_tasks_remove(ch->overlap_dial_task);
				ch->overlap_dial_task = -1;
			}
			ast_mutex_destroy(&ch->overlap_tv_lock);
		}

		if (ch->originator == ORG_AST) {
			misdn_out_calls[bc->port]--;
		} else {
			misdn_in_calls[bc->port]--;
		}

		if (ch) {
			close(ch->pipe[0]);
			close(ch->pipe[1]);

			if (ast && MISDN_ASTERISK_TECH_PVT(ast)) {
				chan_misdn_log(1, bc->port, "* RELEASING CHANNEL pid:%d ctx:%s dad:%s oad:%s state: %s\n", bc ? bc->pid : -1, ast->context, ast->exten, ast->cid.cid_num, misdn_get_ch_state(ch));
				chan_misdn_log(3, bc->port, " --> * State Down\n");
				MISDN_ASTERISK_TECH_PVT(ast) = NULL;

				if (ast->_state != AST_STATE_RESERVED) {
					chan_misdn_log(3, bc->port, " --> Setting AST State to down\n");
					ast_setstate(ast, AST_STATE_DOWN);
				}
			}

			ch->state = MISDN_CLEANING;
			cl_dequeue_chan(&cl_te, ch);

			free(ch);
		} else {
			/* chan is already cleaned, so exiting  */
		}
	}
	ast_mutex_unlock(&release_lock);
/*** release end **/
}

static void misdn_transfer_bc(struct chan_list *tmp_ch, struct chan_list *holded_chan)
{
	chan_misdn_log(4, 0, "TRANSFERRING %s to %s\n", holded_chan->ast->name, tmp_ch->ast->name);

	tmp_ch->state = MISDN_HOLD_DISCONNECT;

	ast_moh_stop(ast_bridged_channel(holded_chan->ast));

	holded_chan->state=MISDN_CONNECTED;
	/* misdn_lib_transfer(holded_chan->bc); */
	ast_channel_masquerade(holded_chan->ast, ast_bridged_channel(tmp_ch->ast));
}


static void do_immediate_setup(struct misdn_bchannel *bc, struct chan_list *ch, struct ast_channel *ast)
{
	char predial[256]="";
	char *p = predial;
  
	struct ast_frame fr;

	strncpy(predial, ast->exten, sizeof(predial) -1 );

	ch->state = MISDN_DIALING;

	if (!ch->noautorespond_on_setup) {
		if (bc->nt) {
			int ret; 
			ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
		} else {
			int ret;
			if ( misdn_lib_is_ptp(bc->port)) {
				ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			} else {
				ret = misdn_lib_send_event(bc, EVENT_PROCEEDING );
			}
		}
	} else {
		ch->state = MISDN_INCOMING_SETUP;
	}

	chan_misdn_log(1, bc->port, "* Starting Ast ctx:%s dad:%s oad:%s with 's' extension\n", ast->context, ast->exten, ast->cid.cid_num);
  
	strcpy(ast->exten, "s");
 
	if (!ast_canmatch_extension(ast, ast->context, ast->exten, 1, bc->oad) || pbx_start_chan(ch) < 0) {
		ast = NULL;
		bc->out_cause = AST_CAUSE_UNALLOCATED;
		hangup_chan(ch);
		hanguptone_indicate(ch);

		if (bc->nt)
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
		else
			misdn_lib_send_event(bc, EVENT_DISCONNECT );
	}
  
  
	while (!ast_strlen_zero(p) ) {
		fr.frametype = AST_FRAME_DTMF;
		fr.subclass = *p;
		fr.src = NULL;
		fr.data = NULL;
		fr.datalen = 0;
		fr.samples = 0;
		fr.mallocd = 0;
		fr.offset = 0;
		fr.delivery = ast_tv(0,0);

		if (ch->ast && MISDN_ASTERISK_PVT(ch->ast) && MISDN_ASTERISK_TECH_PVT(ch->ast)) {
			ast_queue_frame(ch->ast, &fr);
		}
		p++;
	}
}



static void send_cause2ast(struct ast_channel *ast, struct misdn_bchannel *bc, struct chan_list *ch) {
	if (!ast) {
		chan_misdn_log(1, 0, "send_cause2ast: No Ast\n");
		return;
	}
	if (!bc) {
		chan_misdn_log(1, 0, "send_cause2ast: No BC\n");
		return;
	}
	if (!ch) {
		chan_misdn_log(1, 0, "send_cause2ast: No Ch\n");
		return;
	}

	ast->hangupcause = bc->cause;

	switch (bc->cause) {

	case AST_CAUSE_UNALLOCATED:
	case AST_CAUSE_NO_ROUTE_TRANSIT_NET:
	case AST_CAUSE_NO_ROUTE_DESTINATION:
 	case 4:	/* Send special information tone */
 	case AST_CAUSE_NUMBER_CHANGED:
 	case AST_CAUSE_DESTINATION_OUT_OF_ORDER:
		/* Congestion Cases */
		/*
		 * Not Queueing the Congestion anymore, since we want to hear
		 * the inband message
		 *
		chan_misdn_log(1, bc ? bc->port : 0, " --> * SEND: Queue Congestion pid:%d\n", bc ? bc->pid : -1);
		ch->state = MISDN_BUSY;
		
		ast_queue_control(ast, AST_CONTROL_CONGESTION);
		*/
		break;

	case AST_CAUSE_CALL_REJECTED:
	case AST_CAUSE_USER_BUSY:
		ch->state = MISDN_BUSY;

		if (!ch->need_busy) {
			chan_misdn_log(1, bc ? bc->port : 0, "Queued busy already\n");
			break;
		}

		chan_misdn_log(1, bc ? bc->port : 0, " --> * SEND: Queue Busy pid:%d\n", bc ? bc->pid : -1);
		
		ast_queue_control(ast, AST_CONTROL_BUSY);
		
		ch->need_busy = 0;
		
		break;
	}
}


/*! \brief Import parameters from the dialplan environment variables */
void import_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch)
{
	const char *tmp;

	tmp = pbx_builtin_getvar_helper(chan, "MISDN_PID");
	if (tmp) {
		ch->other_pid = atoi(tmp);
		chan_misdn_log(3, bc->port, " --> IMPORT_PID: importing pid:%s\n", tmp);
		if (ch->other_pid > 0) {
			ch->other_ch = find_chan_by_pid(cl_te, ch->other_pid);
			if (ch->other_ch)
				ch->other_ch->other_ch = ch;
		}
	}

	tmp = pbx_builtin_getvar_helper(chan, "MISDN_ADDRESS_COMPLETE");
	if (tmp && (atoi(tmp) == 1)) {
		bc->sending_complete = 1;
	}

	tmp = pbx_builtin_getvar_helper(chan, "MISDN_USERUSER");
	if (tmp) {
		ast_log(LOG_NOTICE, "MISDN_USERUSER: %s\n", tmp);
		ast_copy_string(bc->uu, tmp, sizeof(bc->uu));
		bc->uulen = strlen(bc->uu);
	}

	tmp = pbx_builtin_getvar_helper(chan, "MISDN_KEYPAD");
	if (tmp) {
		ast_copy_string(bc->keypad, tmp, sizeof(bc->keypad));
	}
}

/*! \brief Export parameters to the dialplan environment variables */
void export_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch)
{
	char tmp[32];
	chan_misdn_log(3, bc->port, " --> EXPORT_PID: pid:%d\n", bc->pid);
	snprintf(tmp, sizeof(tmp), "%d", bc->pid);
	pbx_builtin_setvar_helper(chan, "_MISDN_PID", tmp);

	if (bc->sending_complete) {
		snprintf(tmp, sizeof(tmp), "%d", bc->sending_complete);
		pbx_builtin_setvar_helper(chan, "MISDN_ADDRESS_COMPLETE", tmp);
	}

	if (bc->urate) {
		snprintf(tmp, sizeof(tmp), "%d", bc->urate);
		pbx_builtin_setvar_helper(chan, "MISDN_URATE", tmp);
	}

	if (bc->uulen && (bc->uulen < sizeof(bc->uu))) {
		bc->uu[bc->uulen] = 0;
		pbx_builtin_setvar_helper(chan, "MISDN_USERUSER", bc->uu);
	}

	if (!ast_strlen_zero(bc->keypad)) 
		pbx_builtin_setvar_helper(chan, "MISDN_KEYPAD", bc->keypad);
}

int add_in_calls(int port)
{
	int max_in_calls;
	
	misdn_cfg_get(port, MISDN_CFG_MAX_IN, &max_in_calls, sizeof(max_in_calls));
	misdn_in_calls[port]++;

	if (max_in_calls >= 0 && max_in_calls < misdn_in_calls[port]) {
		ast_log(LOG_NOTICE, "Marking Incoming Call on port[%d]\n", port);
		return misdn_in_calls[port] - max_in_calls;
	}
	
	return 0;
}

int add_out_calls(int port)
{
	int max_out_calls;
	
	misdn_cfg_get(port, MISDN_CFG_MAX_OUT, &max_out_calls, sizeof(max_out_calls));

	if (max_out_calls >= 0 && max_out_calls <= misdn_out_calls[port]) {
		ast_log(LOG_NOTICE, "Rejecting Outgoing Call on port[%d]\n", port);
		return (misdn_out_calls[port] + 1) - max_out_calls;
	}

	misdn_out_calls[port]++;
	
	return 0;
}

static void start_pbx(struct chan_list *ch, struct misdn_bchannel *bc, struct ast_channel *chan) {
	if (pbx_start_chan(ch) < 0) {
		hangup_chan(ch);
		chan_misdn_log(-1, bc->port, "ast_pbx_start returned <0 in SETUP\n");
		if (bc->nt) {
			hanguptone_indicate(ch);
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
		} else
			misdn_lib_send_event(bc, EVENT_RELEASE);
	}
}

static void wait_for_digits(struct chan_list *ch, struct misdn_bchannel *bc, struct ast_channel *chan) {
	ch->state=MISDN_WAITING4DIGS;
	misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
	if (bc->nt && !bc->dad[0])
		dialtone_indicate(ch);
}


/************************************************************/
/*  Receive Events from isdn_lib  here                     */
/************************************************************/
static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data)
{
	int msn_valid;
	struct chan_list *ch = find_chan_by_bc(cl_te, bc);
	
	if (event != EVENT_BCHAN_DATA && event != EVENT_TONE_GENERATE) { /*  Debug Only Non-Bchan */
		int debuglevel = 1;
		if ( event == EVENT_CLEANUP && !user_data)
			debuglevel = 5;

		chan_misdn_log(debuglevel, bc->port, "I IND :%s oad:%s dad:%s pid:%d state:%s\n", manager_isdn_get_info(event), bc->oad, bc->dad, bc->pid, ch ? misdn_get_ch_state(ch) : "none");
		if (debuglevel == 1) {
			misdn_lib_log_ies(bc);
			chan_misdn_log(4, bc->port, " --> bc_state:%s\n", bc_state2str(bc->bc_state));
		}
	}
	
	if (!ch) {
		switch(event) {
		case EVENT_SETUP:
		case EVENT_DISCONNECT:
		case EVENT_PORT_ALARM:
		case EVENT_RETRIEVE:
		case EVENT_NEW_BC:
		case EVENT_FACILITY:
			break;
		case EVENT_RELEASE_COMPLETE:
			chan_misdn_log(1, bc->port, " --> no Ch, so we've already released.\n");
			break;
		case EVENT_CLEANUP:
		case EVENT_TONE_GENERATE:
		case EVENT_BCHAN_DATA:
			return -1;
		default:
			chan_misdn_log(1, bc->port, "Chan not existing at the moment bc->l3id:%x bc:%p event:%s port:%d channel:%d\n", bc->l3_id, bc, manager_isdn_get_info(event), bc->port, bc->channel);
			return -1;
		}
	}
	
	if (ch) {
		switch (event) {
		case EVENT_TONE_GENERATE:
			break;
		case EVENT_DISCONNECT:
		case EVENT_RELEASE:
		case EVENT_RELEASE_COMPLETE:
		case EVENT_CLEANUP:
		case EVENT_TIMEOUT:
			if (!ch->ast)
				chan_misdn_log(3, bc->port, "ast_hangup already called, so we have no ast ptr anymore in event(%s)\n", manager_isdn_get_info(event));
			break;
		default:
			if (!ch->ast  || !MISDN_ASTERISK_PVT(ch->ast) || !MISDN_ASTERISK_TECH_PVT(ch->ast)) {
				if (event != EVENT_BCHAN_DATA)
					ast_log(LOG_NOTICE, "No Ast or No private Pointer in Event (%d:%s)\n", event, manager_isdn_get_info(event));
				return -1;
			}
		}
	}
	
	
	switch (event) {
	case EVENT_PORT_ALARM:
		{
			int boa = 0;
			misdn_cfg_get(bc->port, MISDN_CFG_ALARM_BLOCK, &boa, sizeof(int));
			if (boa) {
				cb_log(1, bc->port, " --> blocking\n");
				misdn_lib_port_block(bc->port); 
			}
		}
		break;
	case EVENT_BCHAN_ACTIVATED:
		break;
		
	case EVENT_NEW_CHANNEL:
		update_name(ch->ast,bc->port,bc->channel);
		break;
		
	case EVENT_NEW_L3ID:
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		break;

	case EVENT_NEW_BC:
		if (!ch) {
			ch = find_holded(cl_te,bc);
		}
		
		if (!ch) {
			ast_log(LOG_WARNING, "NEW_BC without chan_list?\n");
			break;
		}

		if (bc)
			ch->bc = (struct misdn_bchannel *)user_data;
		break;
		
	case EVENT_DTMF_TONE:
	{
		/*  sending INFOS as DTMF-Frames :) */
		struct ast_frame fr;

		memset(&fr, 0, sizeof(fr));
		fr.frametype = AST_FRAME_DTMF;
		fr.subclass = bc->dtmf ;
		fr.src = NULL;
		fr.data = NULL;
		fr.datalen = 0;
		fr.samples = 0;
		fr.mallocd = 0;
		fr.offset = 0;
		fr.delivery = ast_tv(0,0);
		
		if (!ch->ignore_dtmf) {
			chan_misdn_log(2, bc->port, " --> DTMF:%c\n", bc->dtmf);
			ast_queue_frame(ch->ast, &fr);
		} else {
			chan_misdn_log(2, bc->port, " --> Ignoring DTMF:%c due to bridge flags\n", bc->dtmf);
		}
	}
		break;
	case EVENT_STATUS:
		break;
    
	case EVENT_INFORMATION:
	{
		if ( ch->state != MISDN_CONNECTED ) 
			stop_indicate(ch);
	
		if (!ch->ast)
			break;

		if (ch->state == MISDN_WAITING4DIGS ) {
			/*  Ok, incomplete Setup, waiting till extension exists */
			if (ast_strlen_zero(bc->info_dad) && ! ast_strlen_zero(bc->keypad)) {
				chan_misdn_log(1, bc->port, " --> using keypad as info\n");
				ast_copy_string(bc->info_dad, bc->keypad, sizeof(bc->info_dad));
			}

			strncat(bc->dad, bc->info_dad, sizeof(bc->dad) - strlen(bc->dad) - 1);
			ast_copy_string(ch->ast->exten, bc->dad, sizeof(ch->ast->exten));

			/* Check for Pickup Request first */
			if (!strcmp(ch->ast->exten, ast_pickup_ext())) {
				if (ast_pickup_call(ch->ast)) {
					hangup_chan(ch);
				} else {
					struct ast_channel *chan = ch->ast;
					ch->state = MISDN_CALLING_ACKNOWLEDGE;
					ast_setstate(chan, AST_STATE_DOWN);
					hangup_chan(ch);
					ch->ast = NULL;
					break;
				}
			}
			
			if (!ast_canmatch_extension(ch->ast, ch->context, bc->dad, 1, bc->oad)) {
				if (ast_exists_extension(ch->ast, ch->context, "i", 1, bc->oad)) {
					ast_log(LOG_WARNING,
						"Extension '%s@%s' can never match. Jumping to 'i' extension. port:%d\n",
						bc->dad, ch->context, bc->port);
					strcpy(ch->ast->exten, "i");

					ch->state = MISDN_DIALING;
					start_pbx(ch, bc, ch->ast);
					break;
				}

				ast_log(LOG_WARNING,
					"Extension '%s@%s' can never match. Disconnecting. port:%d\n"
					"\tMaybe you want to add an 'i' extension to catch this case.\n",
					bc->dad, ch->context, bc->port);

				if (bc->nt)
					hanguptone_indicate(ch);
				ch->state = MISDN_EXTCANTMATCH;
				bc->out_cause = AST_CAUSE_UNALLOCATED;

				misdn_lib_send_event(bc, EVENT_DISCONNECT);
				break;
			}

			if (ch->overlap_dial) {
				ast_mutex_lock(&ch->overlap_tv_lock);
				ch->overlap_tv = ast_tvnow();
				ast_mutex_unlock(&ch->overlap_tv_lock);
				if (ch->overlap_dial_task == -1) {
					ch->overlap_dial_task = 
						misdn_tasks_add_variable(ch->overlap_dial, misdn_overlap_dial_task, ch);
				}
				break;
			}

			if (ast_exists_extension(ch->ast, ch->context, bc->dad, 1, bc->oad))  {
				
				ch->state = MISDN_DIALING;
				start_pbx(ch, bc, ch->ast);
			}
		} else {
			/*  sending INFOS as DTMF-Frames :) */
			struct ast_frame fr;
			int digits;

			memset(&fr, 0, sizeof(fr));
			fr.frametype = AST_FRAME_DTMF;
			fr.subclass = bc->info_dad[0] ;
			fr.src = NULL;
			fr.data = NULL;
			fr.datalen = 0;
			fr.samples = 0;
			fr.mallocd = 0;
			fr.offset = 0;
			fr.delivery = ast_tv(0,0);

			misdn_cfg_get(0, MISDN_GEN_APPEND_DIGITS2EXTEN, &digits, sizeof(int));
			if (ch->state != MISDN_CONNECTED ) {
				if (digits) {
					strncat(bc->dad, bc->info_dad, sizeof(bc->dad) - strlen(bc->dad) - 1);
					ast_copy_string(ch->ast->exten, bc->dad, sizeof(ch->ast->exten));
					ast_cdr_update(ch->ast);
				}
				
				ast_queue_frame(ch->ast, &fr);
			}
		}
	}
		break;
	case EVENT_SETUP:
	{
		struct chan_list *ch = find_chan_by_bc(cl_te, bc);
		struct ast_channel *chan;
		int exceed;
		int pres, screen;
		int ai;
		int im;

		if (ch) {
			switch (ch->state) {
			case MISDN_NOTHING:
				ch = NULL;
				break;
			default:
				chan_misdn_log(1, bc->port, " --> Ignoring Call we have already one\n");
				return RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE; /*  Ignore MSNs which are not in our List */
			}
		}

		msn_valid = misdn_cfg_is_msn_valid(bc->port, bc->dad);
		if (!bc->nt && ! msn_valid) {
			chan_misdn_log(1, bc->port, " --> Ignoring Call, its not in our MSN List\n");
			return RESPONSE_IGNORE_SETUP; /*  Ignore MSNs which are not in our List */
		}

		if (bc->cw) {
			int cause;
			chan_misdn_log(0, bc->port, " --> Call Waiting on PMP sending RELEASE_COMPLETE\n");
			misdn_cfg_get(bc->port, MISDN_CFG_REJECT_CAUSE, &cause, sizeof(cause));
			bc->out_cause = cause ? cause : AST_CAUSE_NORMAL_CLEARING;
			return RESPONSE_RELEASE_SETUP;
		}

		print_bearer(bc);

		ch = init_chan_list(ORG_MISDN);

		if (!ch) {
			chan_misdn_log(-1, bc->port, "cb_events: malloc for chan_list failed!\n");
			return 0;
		}

		ch->bc = bc;
		ch->l3id = bc->l3_id;
		ch->addr = bc->addr;
		ch->originator = ORG_MISDN;

		chan = misdn_new(ch, AST_STATE_RESERVED, bc->dad, bc->oad, AST_FORMAT_ALAW, bc->port, bc->channel);
		if (!chan) {
			misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
			ast_log(LOG_ERROR, "cb_events: misdn_new failed !\n"); 
			return 0;
		}

		ch->ast = chan;

		if ((exceed = add_in_calls(bc->port))) {
			char tmp[16];
			snprintf(tmp, sizeof(tmp), "%d", exceed);
			pbx_builtin_setvar_helper(chan, "MAX_OVERFLOW", tmp);
		}

		read_config(ch, ORG_MISDN);

		export_ch(chan, bc, ch);

		ch->ast->rings = 1;
		ast_setstate(ch->ast, AST_STATE_RINGING);

		switch (bc->pres) {
		case 1:
			pres = AST_PRES_RESTRICTED;
			chan_misdn_log(2, bc->port, " --> PRES: Restricted (1)\n");
			break;
		case 2:
			pres = AST_PRES_UNAVAILABLE;
			chan_misdn_log(2, bc->port, " --> PRES: Unavailable (2)\n");
			break;
		default:
			pres = AST_PRES_ALLOWED;
			chan_misdn_log(2, bc->port, " --> PRES: Allowed (%d)\n", bc->pres);
			break;
		}

		switch (bc->screen) {
		default:
		case 0:
			screen = AST_PRES_USER_NUMBER_UNSCREENED;
			chan_misdn_log(2, bc->port, " --> SCREEN: Unscreened (%d)\n", bc->screen);
			break;
		case 1:
			screen = AST_PRES_USER_NUMBER_PASSED_SCREEN;
			chan_misdn_log(2, bc->port, " --> SCREEN: Passed screen (1)\n");
			break;
		case 2:
			screen = AST_PRES_USER_NUMBER_FAILED_SCREEN;
			chan_misdn_log(2, bc->port, " --> SCREEN: failed screen (2)\n");
			break;
		case 3:
			screen = AST_PRES_NETWORK_NUMBER;
			chan_misdn_log(2, bc->port, " --> SCREEN: Network Number (3)\n");
			break;
		}

		chan->cid.cid_pres = pres | screen;

		pbx_builtin_setvar_helper(chan, "TRANSFERCAPABILITY", ast_transfercapability2str(bc->capability));
		chan->transfercapability = bc->capability;

		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
			pbx_builtin_setvar_helper(chan, "CALLTYPE", "DIGITAL");
			break;
		default:
			pbx_builtin_setvar_helper(chan, "CALLTYPE", "SPEECH");
		}

		/** queue new chan **/
		cl_queue_chan(&cl_te, ch);

		if (!strstr(ch->allowed_bearers, "all")) {
			int i;

			for (i = 0; i < ARRAY_LEN(allowed_bearers_array); ++i) {
				if (allowed_bearers_array[i].cap == bc->capability) {
					if (strstr(ch->allowed_bearers, allowed_bearers_array[i].name)) {
						/* The bearer capability is allowed */
						if (allowed_bearers_array[i].deprecated) {
							chan_misdn_log(0, bc->port, "%s in allowed_bearers list is deprecated\n",
								allowed_bearers_array[i].name);
						}
						break;
					}
				}
			}	/* end for */
			if (i == ARRAY_LEN(allowed_bearers_array)) {
				/* We did not find the bearer capability */
				chan_misdn_log(0, bc->port, "Bearer capability not allowed: %s(%d)\n",
					bearer2str(bc->capability), bc->capability);
				bc->out_cause = AST_CAUSE_INCOMPATIBLE_DESTINATION;

				ch->state = MISDN_EXTCANTMATCH;
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
				return RESPONSE_OK;
			}
		}

		/* Check for Pickup Request first */
		if (!strcmp(chan->exten, ast_pickup_ext())) {
			if (!ch->noautorespond_on_setup) {
				int ret;/** Sending SETUP_ACK**/
				ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			} else {
				ch->state = MISDN_INCOMING_SETUP;
			}
			if (ast_pickup_call(chan)) {
				hangup_chan(ch);
			} else {
				ch->state = MISDN_CALLING_ACKNOWLEDGE;
				ast_setstate(chan, AST_STATE_DOWN);
				hangup_chan(ch);
				ch->ast = NULL;
				break;
			}
		}

		/*
		 * added support for s extension hope it will help those poor cretains
		 * which haven't overlap dial.
		 */
		misdn_cfg_get(bc->port, MISDN_CFG_ALWAYS_IMMEDIATE, &ai, sizeof(ai));
		if (ai) {
			do_immediate_setup(bc, ch, chan);
			break;
		}

		/* check if we should jump into s when we have no dad */
		misdn_cfg_get(bc->port, MISDN_CFG_IMMEDIATE, &im, sizeof(im));
		if (im && ast_strlen_zero(bc->dad)) {
			do_immediate_setup(bc, ch, chan);
			break;
		}

		chan_misdn_log(5, bc->port, "CONTEXT:%s\n", ch->context);
		if(!ast_canmatch_extension(ch->ast, ch->context, bc->dad, 1, bc->oad)) {
			if (ast_exists_extension(ch->ast, ch->context, "i", 1, bc->oad)) {
				ast_log(LOG_WARNING,
					"Extension '%s@%s' can never match. Jumping to 'i' extension. port:%d\n",
					bc->dad, ch->context, bc->port);
				strcpy(ch->ast->exten, "i");
				misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE);
				ch->state = MISDN_DIALING;
				start_pbx(ch, bc, chan);
				break;
			}

			ast_log(LOG_WARNING,
				"Extension '%s@%s' can never match. Disconnecting. port:%d\n"
				"\tMaybe you want to add an 'i' extension to catch this case.\n",
				bc->dad, ch->context, bc->port);
			if (bc->nt)
				hanguptone_indicate(ch);

			ch->state = MISDN_EXTCANTMATCH;
			bc->out_cause = AST_CAUSE_UNALLOCATED;

			if (bc->nt)
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
			else
				misdn_lib_send_event(bc, EVENT_RELEASE );

			break;
		}

		/* Whatever happens, when sending_complete is set or we are PTMP TE, we will definitely 
		 * jump into the dialplan, when the dialed extension does not exist, the 's' extension 
		 * will be used by Asterisk automatically. */
		if (bc->sending_complete || (!bc->nt && !misdn_lib_is_ptp(bc->port))) {
			if (!ch->noautorespond_on_setup) {
				ch->state=MISDN_DIALING;
				misdn_lib_send_event(bc, EVENT_PROCEEDING );
			} else {
				ch->state = MISDN_INCOMING_SETUP;
			}
			start_pbx(ch, bc, chan);
			break;
		}


		/*
		 * When we are NT and overlapdial is set and if 
		 * the number is empty, we wait for the ISDN timeout
		 * instead of our own timer.
		 */
		if (ch->overlap_dial && bc->nt && !bc->dad[0] ) {
			wait_for_digits(ch, bc, chan);
			break;
		}

		/* 
		 * If overlapdial we will definitely send a SETUP_ACKNOWLEDGE and wait for more 
		 * Infos with a Interdigit Timeout.
		 * */
		if (ch->overlap_dial) {
			ast_mutex_lock(&ch->overlap_tv_lock);
			ch->overlap_tv = ast_tvnow();
			ast_mutex_unlock(&ch->overlap_tv_lock);

			wait_for_digits(ch, bc, chan);
			if (ch->overlap_dial_task == -1) 
				ch->overlap_dial_task = 
					misdn_tasks_add_variable(ch->overlap_dial, misdn_overlap_dial_task, ch);

			break;
		}

		/* If the extension does not exist and we're not TE_PTMP we wait for more digits 
		 * without interdigit timeout.
		 * */
		if (!ast_exists_extension(ch->ast, ch->context, bc->dad, 1, bc->oad))  {
			wait_for_digits(ch, bc, chan);
			break;
		}

		/*
		 * If the extension exists let's just jump into it.
		 * */
		if (ast_exists_extension(ch->ast, ch->context, bc->dad, 1, bc->oad)) {
			if (bc->need_more_infos)
				misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			else
				misdn_lib_send_event(bc, EVENT_PROCEEDING);

			ch->state = MISDN_DIALING;
			start_pbx(ch, bc, chan);
			break;
		}
	}
	break;

	case EVENT_SETUP_ACKNOWLEDGE:
	{
		ch->state = MISDN_CALLING_ACKNOWLEDGE;

		if (bc->channel) 
			update_name(ch->ast,bc->port,bc->channel);
		
		if (!ast_strlen_zero(bc->infos_pending)) {
			/* TX Pending Infos */
			strncat(bc->dad, bc->infos_pending, sizeof(bc->dad) - strlen(bc->dad) - 1);

			if (!ch->ast)
				break;
			ast_copy_string(ch->ast->exten, bc->dad, sizeof(ch->ast->exten));
			ast_copy_string(bc->info_dad, bc->infos_pending, sizeof(bc->info_dad));
			ast_copy_string(bc->infos_pending, "", sizeof(bc->infos_pending));

			misdn_lib_send_event(bc, EVENT_INFORMATION);
		}
	}
	break;
	case EVENT_PROCEEDING:
	{
		if (bc->channel) 
			update_name(ch->ast, bc->port, bc->channel);

		if (misdn_cap_is_speech(bc->capability) &&
		     misdn_inband_avail(bc) ) {
			start_bc_tones(ch);
		}

		ch->state = MISDN_PROCEEDING;
		
		if (!ch->ast)
			break;

		ast_queue_control(ch->ast, AST_CONTROL_PROCEEDING);
	}
	break;
	case EVENT_PROGRESS:
		if (bc->channel) 
			update_name(ch->ast, bc->port, bc->channel);

		if (!bc->nt ) {
			if ( misdn_cap_is_speech(bc->capability) &&
			     misdn_inband_avail(bc)
				) {
				start_bc_tones(ch);
			}
			
			ch->state = MISDN_PROGRESS;

			if (!ch->ast)
				break;
			ast_queue_control(ch->ast, AST_CONTROL_PROGRESS);
		}
		break;
		
		
	case EVENT_ALERTING:
	{
		if (bc->channel) 
			update_name(ch->ast, bc->port, bc->channel);

		ch->state = MISDN_ALERTING;
		
		if (!ch->ast)
			break;

		ast_queue_control(ch->ast, AST_CONTROL_RINGING);
		ast_setstate(ch->ast, AST_STATE_RINGING);
		
		cb_log(7, bc->port, " --> Set State Ringing\n");
		
		if (misdn_cap_is_speech(bc->capability) && misdn_inband_avail(bc)) {
			cb_log(1, bc->port, "Starting Tones, we have inband Data\n");
			start_bc_tones(ch);
		} else {
			cb_log(3, bc->port, " --> We have no inband Data, the other end must create ringing\n");
			if (ch->far_alerting) {
				cb_log(1, bc->port, " --> The other end can not do ringing eh ?.. we must do all ourself..");
				start_bc_tones(ch);
				/*tone_indicate(ch, TONE_FAR_ALERTING);*/
			}
		}
	}
	break;
	case EVENT_CONNECT:
	{
		struct ast_channel *bridged;

		/*we answer when we've got our very new L3 ID from the NT stack */
		misdn_lib_send_event(bc, EVENT_CONNECT_ACKNOWLEDGE);

		if (!ch->ast)
			break;

		bridged = ast_bridged_channel(ch->ast);
		stop_indicate(ch);

		if (bridged && !strcasecmp(bridged->tech->type, "mISDN")) {
			struct chan_list *bridged_ch = MISDN_ASTERISK_TECH_PVT(bridged);

			chan_misdn_log(1, bc->port, " --> copying cpndialplan:%d and cad:%s to the A-Channel\n", bc->cpnnumplan, bc->cad);
			if (bridged_ch) {
				bridged_ch->bc->cpnnumplan = bc->cpnnumplan;
				ast_copy_string(bridged_ch->bc->cad, bc->cad, sizeof(bridged_ch->bc->cad));
			}
		}
	}
	ch->l3id=bc->l3_id;
	ch->addr=bc->addr;

	start_bc_tones(ch);
	
	ch->state = MISDN_CONNECTED;
	
	ast_queue_control(ch->ast, AST_CONTROL_ANSWER);
	break;
	case EVENT_CONNECT_ACKNOWLEDGE:
	{
		ch->l3id = bc->l3_id;
		ch->addr = bc->addr;

		start_bc_tones(ch);

		ch->state = MISDN_CONNECTED;
	}
	break;
	case EVENT_DISCONNECT:
		/*we might not have an ch->ast ptr here anymore*/
		if (ch) {
			struct chan_list *holded_ch = find_holded(cl_te, bc);

			chan_misdn_log(3, bc->port, " --> org:%d nt:%d, inbandavail:%d state:%d\n", ch->originator, bc->nt, misdn_inband_avail(bc), ch->state);
			if (ch->originator == ORG_AST && !bc->nt && misdn_inband_avail(bc) && ch->state != MISDN_CONNECTED) {
				/* If there's inband information available (e.g. a
				   recorded message saying what was wrong with the
				   dialled number, or perhaps even giving an
				   alternative number, then play it instead of
				   immediately releasing the call */
				chan_misdn_log(1, bc->port, " --> Inband Info Avail, not sending RELEASE\n");
		
				ch->state = MISDN_DISCONNECTED;
				start_bc_tones(ch);

				if (ch->ast) {
					ch->ast->hangupcause = bc->cause;
					if (bc->cause == AST_CAUSE_USER_BUSY)
						ast_queue_control(ch->ast, AST_CONTROL_BUSY);
				}
				ch->need_busy = 0;
				break;
			}

			/*Check for holded channel, to implement transfer*/
			if (holded_ch && holded_ch != ch && ch->ast && ch->state == MISDN_CONNECTED) {
				cb_log(1, bc->port, " --> found holded ch\n");
				misdn_transfer_bc(ch, holded_ch) ;
			}

			bc->need_disconnect = 0;

			stop_bc_tones(ch);
			hangup_chan(ch);
#if 0
		} else {
			ch = find_holded_l3(cl_te, bc->l3_id,1);
			if (ch) {
				hangup_chan(ch);
			}
#endif
		}
		bc->out_cause = -1;
		if (bc->need_release)
			misdn_lib_send_event(bc, EVENT_RELEASE);
		break;
	
	case EVENT_RELEASE:
		{
			bc->need_disconnect = 0;
			bc->need_release = 0;

			hangup_chan(ch);
			release_chan(bc);
		}
		break;
	case EVENT_RELEASE_COMPLETE:
	{
		bc->need_disconnect = 0;
		bc->need_release = 0;
		bc->need_release_complete = 0;

		stop_bc_tones(ch);
		hangup_chan(ch);

		if (ch)
			ch->state = MISDN_CLEANING;

		release_chan(bc);
	}
	break;
	case EVENT_BCHAN_ERROR:
	case EVENT_CLEANUP:
	{
		stop_bc_tones(ch);
		
		switch (ch->state) {
		case MISDN_CALLING:
			bc->cause = AST_CAUSE_DESTINATION_OUT_OF_ORDER;
			break;
		default:
			break;
		}
		
		hangup_chan(ch);
		release_chan(bc);
	}
	break;

	case EVENT_TONE_GENERATE:
	{
		int tone_len = bc->tone_cnt;
		struct ast_channel *ast = ch->ast;
		void *tmp;
		int res;
		int (*generate)(struct ast_channel *chan, void *tmp, int datalen, int samples);

		chan_misdn_log(9, bc->port, "TONE_GEN: len:%d\n", tone_len);

		if (!ast)
			break;

		if (!ast->generator)
			break;

		tmp = ast->generatordata;
		ast->generatordata = NULL;
		generate = ast->generator->generate;

		if (tone_len < 0 || tone_len > 512 ) {
			ast_log(LOG_NOTICE, "TONE_GEN: len was %d, set to 128\n", tone_len);
			tone_len = 128;
		}

		res = generate(ast, tmp, tone_len, tone_len);
		ast->generatordata = tmp;
		
		if (res) {
			ast_log(LOG_WARNING, "Auto-deactivating generator\n");
			ast_deactivate_generator(ast);
		} else {
			bc->tone_cnt = 0;
		}
	}
	break;

	case EVENT_BCHAN_DATA:
	{
		if (!misdn_cap_is_speech(ch->bc->capability)) {
			struct ast_frame frame;
			/*In Data Modes we queue frames*/
			frame.frametype  = AST_FRAME_VOICE; /*we have no data frames yet*/
			frame.subclass = AST_FORMAT_ALAW;
			frame.datalen = bc->bframe_len;
			frame.samples = bc->bframe_len;
			frame.mallocd = 0;
			frame.offset = 0;
			frame.delivery = ast_tv(0,0);
			frame.src = NULL;
			frame.data = bc->bframe;

			if (ch->ast) 
				ast_queue_frame(ch->ast, &frame);
		} else {
			fd_set wrfs;
			struct timeval tv = { 0, 0 };
			int t;

			FD_ZERO(&wrfs);
			FD_SET(ch->pipe[1], &wrfs);

			t = select(FD_SETSIZE, NULL, &wrfs, NULL, &tv);

			if (!t) {
				chan_misdn_log(9, bc->port, "Select Timed out\n");
				break;
			}
			
			if (t < 0) {
				chan_misdn_log(-1, bc->port, "Select Error (err=%s)\n", strerror(errno));
				break;
			}
			
			if (FD_ISSET(ch->pipe[1], &wrfs)) {
				chan_misdn_log(9, bc->port, "writing %d bytes to asterisk\n", bc->bframe_len);
				if (write(ch->pipe[1], bc->bframe, bc->bframe_len) <= 0) {
					chan_misdn_log(0, bc->port, "Write returned <=0 (err=%s) --> hanging up channel\n", strerror(errno));

					stop_bc_tones(ch);
					hangup_chan(ch);
					release_chan(bc);
				}
			} else {
				chan_misdn_log(1, bc->port, "Write Pipe full!\n");
			}
		}
	}
	break;
	case EVENT_TIMEOUT:
	{
		if (ch && bc)
			chan_misdn_log(1, bc->port, "--> state: %s\n", misdn_get_ch_state(ch));

		switch (ch->state) {
		case MISDN_DIALING:
		case MISDN_PROGRESS:
			if (bc->nt && !ch->nttimeout)
				break;
			
		case MISDN_CALLING:
		case MISDN_ALERTING:
		case MISDN_PROCEEDING:
		case MISDN_CALLING_ACKNOWLEDGE:
			if (bc->nt) {
				bc->progress_indicator = INFO_PI_INBAND_AVAILABLE;
				hanguptone_indicate(ch);
			}
				
			bc->out_cause = AST_CAUSE_UNALLOCATED;
			misdn_lib_send_event(bc, EVENT_DISCONNECT);
			break;

		case MISDN_WAITING4DIGS:
			if (bc->nt) {
				bc->progress_indicator = INFO_PI_INBAND_AVAILABLE;
				bc->out_cause = AST_CAUSE_UNALLOCATED;
				hanguptone_indicate(ch);
				misdn_lib_send_event(bc, EVENT_DISCONNECT);
			} else {
				bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
				misdn_lib_send_event(bc, EVENT_RELEASE);
			}
				
			break;

		case MISDN_CLEANING: 
			chan_misdn_log(1,bc->port," --> in state cleaning .. so ignoring, the stack should clean it for us\n");
			break;

		default:
			misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
		}
	}
	break;

    
	/****************************/
	/** Supplementary Services **/
	/****************************/
	case EVENT_RETRIEVE:
	{
		struct ast_channel *hold_ast;

		if (!ch) {
			chan_misdn_log(4, bc->port, " --> no CH, searching in holded\n");
			ch = find_holded_l3(cl_te, bc->l3_id, 1);
		}

		if (!ch) {
			ast_log(LOG_WARNING, "Found no Holded channel, cannot Retrieve\n");
			misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
			break;
		}

		/*remember the channel again*/
		ch->bc = bc;
		ch->state = MISDN_CONNECTED;

		ch->hold_info.port = 0;
		ch->hold_info.channel = 0;

		hold_ast = ast_bridged_channel(ch->ast);

		if (hold_ast) {
			ast_moh_stop(hold_ast);
		}
	
		if (misdn_lib_send_event(bc, EVENT_RETRIEVE_ACKNOWLEDGE) < 0) {
			chan_misdn_log(4, bc->port, " --> RETRIEVE_ACK failed\n");
			misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
		}
	}
	break;
    
	case EVENT_HOLD:
	{
		int hold_allowed;
		struct ast_channel *bridged;

		misdn_cfg_get(bc->port, MISDN_CFG_HOLD_ALLOWED, &hold_allowed, sizeof(int));

		if (!hold_allowed) {

			chan_misdn_log(-1, bc->port, "Hold not allowed this port.\n");
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			break;
		}

		bridged = ast_bridged_channel(ch->ast);
		if (bridged) {
			chan_misdn_log(2, bc->port, "Bridge Partner is of type: %s\n", bridged->tech->type);
			ch->state = MISDN_HOLDED;
			ch->l3id = bc->l3_id;
			
			misdn_lib_send_event(bc, EVENT_HOLD_ACKNOWLEDGE);

			/* XXX This should queue an AST_CONTROL_HOLD frame on this channel
			 * instead of starting moh on the bridged channel directly */
			ast_moh_start(bridged, NULL, NULL);

			/*forget the channel now*/
			ch->bc = NULL;
			ch->hold_info.port = bc->port;
			ch->hold_info.channel = bc->channel;

		} else {
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			chan_misdn_log(0, bc->port, "We aren't bridged to anybody\n");
		}
	} 
	break;
	
	case EVENT_FACILITY:
		if (!ch) {
			/* This may come from a call we don't know nothing about, so we ignore it. */
			chan_misdn_log(-1, bc->port, "Got EVENT_FACILITY but we don't have a ch!\n");
			break;
		}

		print_facility(&(bc->fac_in), bc);
		
		switch (bc->fac_in.Function) {
		case Fac_CD:
			{
				struct ast_channel *bridged = ast_bridged_channel(ch->ast);
				struct chan_list *ch_br;
				if (bridged && MISDN_ASTERISK_TECH_PVT(bridged)) {
					ch_br = MISDN_ASTERISK_TECH_PVT(bridged);
					/*ch->state = MISDN_FACILITY_DEFLECTED;*/
					if (ch_br->bc) {
						if (ast_exists_extension(bridged, ch->context, (char *)bc->fac_in.u.CDeflection.DeflectedToNumber, 1, bc->oad)) {
							ch_br->state = MISDN_DIALING;
							if (pbx_start_chan(ch_br) < 0) {
								chan_misdn_log(-1, ch_br->bc->port, "ast_pbx_start returned < 0 in misdn_overlap_dial_task\n");
							}
						}
					}
				}
				misdn_lib_send_event(bc, EVENT_DISCONNECT);
			} 
			break;
		case Fac_AOCDCurrency:
			{
				bc->AOCDtype = Fac_AOCDCurrency;
				memcpy(&(bc->AOCD.currency), &(bc->fac_in.u.AOCDcur), sizeof(struct FacAOCDCurrency));
				export_aoc_vars(ch->originator, ch->ast, bc);
			}
			break;
		case Fac_AOCDChargingUnit:
			{
				bc->AOCDtype = Fac_AOCDChargingUnit;
				memcpy(&(bc->AOCD.chargingUnit), &(bc->fac_in.u.AOCDchu), sizeof(struct FacAOCDChargingUnit));
				export_aoc_vars(ch->originator, ch->ast, bc);
			}
			break;
		default:
			chan_misdn_log(0, bc->port," --> not yet handled: facility type:%d\n", bc->fac_in.Function);
		}
		
		break;

	case EVENT_RESTART:

		if (!bc->dummy) {
			stop_bc_tones(ch);
			release_chan(bc);
		}
		break;

	default:
		chan_misdn_log(1, 0, "Got Unknown Event\n");
		break;
	}
	
	return RESPONSE_OK;
}

/** TE STUFF END **/

/******************************************
 *
 *   Asterisk Channel Endpoint END
 *
 *
 *******************************************/



static int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_log(LOG_VERBOSE, "-- Unregistering mISDN Channel Driver --\n");

	misdn_tasks_destroy();
	
	if (!g_config_initialized)
		return 0;
	
	ast_cli_unregister_multiple(chan_misdn_clis, sizeof(chan_misdn_clis) / sizeof(struct ast_cli_entry));
	
	/* ast_unregister_application("misdn_crypt"); */
	ast_unregister_application("misdn_set_opt");
	ast_unregister_application("misdn_facility");
	ast_unregister_application("misdn_check_l2l1");
  
	ast_channel_unregister(&misdn_tech);

	free_robin_list();
	misdn_cfg_destroy();
	misdn_lib_destroy();
  
	if (misdn_debug)
		free(misdn_debug);
	if (misdn_debug_only)
		free(misdn_debug_only);
 	free(misdn_ports);
 	
	return 0;
}

static int load_module(void)
{
	int i, port;
	int ntflags = 0, ntkc = 0;
	char ports[256] = "";
	char tempbuf[BUFFERSIZE + 1];
	char ntfile[BUFFERSIZE + 1];
	struct misdn_lib_iface iface = {
		.cb_event = cb_events,
		.cb_log = chan_misdn_log,
		.cb_jb_empty = chan_misdn_jb_empty,
	};

	max_ports = misdn_lib_maxports_get();
	
	if (max_ports <= 0) {
		ast_log(LOG_ERROR, "Unable to initialize mISDN\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	
	if (misdn_cfg_init(max_ports)) {
		ast_log(LOG_ERROR, "Unable to initialize misdn_config.\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	g_config_initialized = 1;
	
	misdn_debug = (int *) malloc(sizeof(int) * (max_ports + 1));
	if (!misdn_debug) {
		ast_log(LOG_ERROR, "Out of memory for misdn_debug\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	misdn_ports = (int *) malloc(sizeof(int) * (max_ports + 1));
	if (!misdn_ports) {
		ast_log(LOG_ERROR, "Out of memory for misdn_ports\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	misdn_cfg_get(0, MISDN_GEN_DEBUG, &misdn_debug[0], sizeof(int));
	for (i = 1; i <= max_ports; i++) {
		misdn_debug[i] = misdn_debug[0];
		misdn_ports[i] = i;
	}
	*misdn_ports = 0;
	misdn_debug_only = (int *) calloc(max_ports + 1, sizeof(int));

	misdn_cfg_get(0, MISDN_GEN_TRACEFILE, tempbuf, BUFFERSIZE);
	if (!ast_strlen_zero(tempbuf))
		tracing = 1;

	misdn_in_calls = (int *) malloc(sizeof(int) * (max_ports + 1));
	misdn_out_calls = (int *) malloc(sizeof(int) * (max_ports + 1));

	for (i = 1; i <= max_ports; i++) {
		misdn_in_calls[i] = 0;
		misdn_out_calls[i] = 0;
	}

	ast_mutex_init(&cl_te_lock);
	ast_mutex_init(&release_lock);

	misdn_cfg_update_ptp();
	misdn_cfg_get_ports_string(ports);

	if (!ast_strlen_zero(ports))
		chan_misdn_log(0, 0, "Got: %s from get_ports\n", ports);
	if (misdn_lib_init(ports, &iface, NULL))
		chan_misdn_log(0, 0, "No te ports initialized\n");

	misdn_cfg_get(0, MISDN_GEN_NTDEBUGFLAGS, &ntflags, sizeof(int));
	misdn_cfg_get(0, MISDN_GEN_NTDEBUGFILE, &ntfile, BUFFERSIZE);
	misdn_lib_nt_debug_init(ntflags, ntfile);

	misdn_cfg_get( 0, MISDN_GEN_NTKEEPCALLS, &ntkc, sizeof(int));
	misdn_lib_nt_keepcalls(ntkc);

	if (ast_channel_register(&misdn_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", misdn_type);
		unload_module();
		return -1;
	}
  
	ast_cli_register_multiple(chan_misdn_clis, sizeof(chan_misdn_clis) / sizeof(struct ast_cli_entry));

	ast_register_application("misdn_set_opt", misdn_set_opt_exec, "misdn_set_opt",
		"misdn_set_opt(:<opt><optarg>:<opt><optarg>...):\n"
		"Sets mISDN opts. and optargs\n"
		"\n"
		"The available options are:\n"
		"    a - Have Asterisk detect DTMF tones on called channel\n"
		"    c - Make crypted outgoing call, optarg is keyindex\n"
		"    d - Send display text to called phone, text is the optarg\n"
		"    e - Perform echo cancelation on this channel,\n"
		"        takes taps as optarg (32,64,128,256)\n"
		"   e! - Disable echo cancelation on this channel\n"
		"    f - Enable fax detection\n"
		"    h - Make digital outgoing call\n"
		"   h1 - Make HDLC mode digital outgoing call\n"
		"    i - Ignore detected DTMF tones, don't signal them to Asterisk,\n"
		"        they will be transported inband.\n"
		"   jb - Set jitter buffer length, optarg is length\n"
		"   jt - Set jitter buffer upper threshold, optarg is threshold\n"
		"   jn - Disable jitter buffer\n"
		"    n - Disable mISDN DSP on channel.\n"
		"        Disables: echo cancel, DTMF detection, and volume control.\n"
		"    p - Caller ID presentation,\n"
		"        optarg is either 'allowed' or 'restricted'\n"
		"    s - Send Non-inband DTMF as inband\n"
		"   vr - Rx gain control, optarg is gain\n"
		"   vt - Tx gain control, optarg is gain\n"
		);

	
	ast_register_application("misdn_facility", misdn_facility_exec, "misdn_facility",
				 "misdn_facility(<FACILITY_TYPE>|<ARG1>|..)\n"
				 "Sends the Facility Message FACILITY_TYPE with \n"
				 "the given Arguments to the current ISDN Channel\n"
				 "Supported Facilities are:\n"
				 "\n"
				 "type=calldeflect args=Nr where to deflect\n"
		);


	ast_register_application("misdn_check_l2l1", misdn_check_l2l1, "misdn_check_l2l1",
				 "misdn_check_l2l1(<port>||g:<groupname>,timeout)"
				 "Checks if the L2 and L1 are up on either the given <port> or\n"
				 "on the ports in the group with <groupname>\n"
				 "If the L1/L2 are down, check_l2l1 gets up the L1/L2 and waits\n"
				 "for <timeout> seconds that this happens. Otherwise, nothing happens\n"
				 "\n"
				 "This application, ensures the L1/L2 state of the Ports in a group\n"
				 "it is intended to make the pmp_l1_check option redundant and to\n"
				 "fix a buggy switch config from your provider\n"
				 "\n"
				 "a sample dialplan would look like:\n\n"
				 "exten => _X.,1,misdn_check_l2l1(g:out|2)\n"
				 "exten => _X.,n,dial(mISDN/g:out/${EXTEN})\n"
				 "\n"
		);


	misdn_cfg_get(0, MISDN_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);

	/* start the l1 watchers */
	
	for (port = misdn_cfg_get_next_port(0); port >= 0; port = misdn_cfg_get_next_port(port)) {
		int l1timeout;
		misdn_cfg_get(port, MISDN_CFG_L1_TIMEOUT, &l1timeout, sizeof(l1timeout));
		if (l1timeout) {
			chan_misdn_log(4, 0, "Adding L1watcher task: port:%d timeout:%ds\n", port, l1timeout);
			misdn_tasks_add(l1timeout * 1000, misdn_l1_task, &misdn_ports[port]);  
		}
	}

	chan_misdn_log(0, 0, "-- mISDN Channel Driver Registered --\n");

	return 0;
}



static int reload(void)
{
	reload_config();

	return 0;
}

/*** SOME APPS ;)***/

static int misdn_facility_exec(struct ast_channel *chan, void *data)
{
	struct chan_list *ch = MISDN_ASTERISK_TECH_PVT(chan);
	char *tok, *tokb;

	chan_misdn_log(0, 0, "TYPE: %s\n", chan->tech->type);
	
	if (strcasecmp(chan->tech->type, "mISDN")) {
		ast_log(LOG_WARNING, "misdn_facility makes only sense with chan_misdn channels!\n");
		return -1;
	}
	
	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "misdn_facility Requires arguments\n");
		return -1;
	}

	tok = strtok_r((char*) data, "|", &tokb) ;

	if (!tok) {
		ast_log(LOG_WARNING, "misdn_facility Requires arguments\n");
		return -1;
	}

	if (!strcasecmp(tok, "calldeflect")) {
		tok = strtok_r(NULL, "|", &tokb) ;
		
		if (!tok) {
			ast_log(LOG_WARNING, "Facility: Call Defl Requires arguments\n");
		}

		if (strlen(tok) >= sizeof(ch->bc->fac_out.u.CDeflection.DeflectedToNumber)) {
			ast_log(LOG_WARNING, "Facility: Number argument too long (up to 15 digits are allowed). Ignoring.\n");
			return 0; 
		}
		ch->bc->fac_out.Function = Fac_CD;
		ast_copy_string((char *)ch->bc->fac_out.u.CDeflection.DeflectedToNumber, tok, sizeof(ch->bc->fac_out.u.CDeflection.DeflectedToNumber));
		misdn_lib_send_event(ch->bc, EVENT_FACILITY);
	} else {
		chan_misdn_log(1, ch->bc->port, "Unknown Facility: %s\n", tok);
	}

	return 0;
}

static int misdn_check_l2l1(struct ast_channel *chan, void *data)
{
	char group[BUFFERSIZE + 1];
	char *port_str;
	int port = 0;
	int timeout;
	int dowait = 0;
	int port_up;

	AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(grouppar);
			AST_APP_ARG(timeout);
	);

	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "misdn_check_l2l1 Requires arguments\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc != 2) {
		ast_log(LOG_WARNING, "Wrong argument count\n");
		return 0;
	}

	/*ast_log(LOG_NOTICE, "Arguments: group/port '%s' timeout '%s'\n", args.grouppar, args.timeout);*/
	timeout = atoi(args.timeout);
	port_str = args.grouppar;

	if (port_str[0] == 'g' && port_str[1] == ':' ) {
		/* We make a group call lets checkout which ports are in my group */
		port_str += 2;
		ast_copy_string(group, port_str, sizeof(group));
		chan_misdn_log(2, 0, "Checking Ports in group: %s\n", group);

		for (	port = misdn_cfg_get_next_port(port); 
			port > 0;
			port = misdn_cfg_get_next_port(port)) {
			char cfg_group[BUFFERSIZE + 1];

			chan_misdn_log(2, 0, "trying port %d\n", port);

			misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);

			if (!strcasecmp(cfg_group, group)) {
				port_up = misdn_lib_port_up(port, 1);

				if (!port_up) {
					chan_misdn_log(2, 0, " --> port '%d'\n", port);
					misdn_lib_get_port_up(port);
					dowait = 1;
				}
			}
		}

	} else {
		port = atoi(port_str);
		chan_misdn_log(2, 0, "Checking Port: %d\n",port);
		port_up = misdn_lib_port_up(port, 1);
		if (!port_up) {
			misdn_lib_get_port_up(port);
			dowait = 1;
		}
	}

	if (dowait) {
		chan_misdn_log(2, 0, "Waiting for '%d' seconds\n", timeout);
		sleep(timeout);
	}

	return 0;
}

static int misdn_set_opt_exec(struct ast_channel *chan, void *data)
{
	struct chan_list *ch = MISDN_ASTERISK_TECH_PVT(chan);
	char *tok, *tokb;
	int  keyidx = 0;
	int rxgain = 0;
	int txgain = 0;
	int change_jitter = 0;

	if (strcasecmp(chan->tech->type, "mISDN")) {
		ast_log(LOG_WARNING, "misdn_set_opt makes only sense with chan_misdn channels!\n");
		return -1;
	}
	
	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "misdn_set_opt Requires arguments\n");
		return -1;
	}

	for (tok = strtok_r((char*) data, ":", &tokb);
	     tok;
	     tok = strtok_r(NULL, ":", &tokb) ) {
		int neglect = 0;

		if (tok[0] == '!' ) {
			neglect = 1;
			tok++;
		}
		
		switch(tok[0]) {
			
		case 'd' :
			ast_copy_string(ch->bc->display, ++tok, sizeof(ch->bc->display));
			chan_misdn_log(1, ch->bc->port, "SETOPT: Display:%s\n", ch->bc->display);
			break;
			
		case 'n':
			chan_misdn_log(1, ch->bc->port, "SETOPT: No DSP\n");
			ch->bc->nodsp = 1;
			break;

		case 'j':
			chan_misdn_log(1, ch->bc->port, "SETOPT: jitter\n");
			tok++;
			change_jitter = 1;

			switch ( tok[0] ) {
			case 'b':
				ch->jb_len = atoi(++tok);
				chan_misdn_log(1, ch->bc->port, " --> buffer_len:%d\n", ch->jb_len);
				break;
			case 't' :
				ch->jb_upper_threshold = atoi(++tok);
				chan_misdn_log(1, ch->bc->port, " --> upper_threshold:%d\n", ch->jb_upper_threshold);
				break;
			case 'n':
				ch->bc->nojitter = 1;
				chan_misdn_log(1, ch->bc->port, " --> nojitter\n");
				break;
			default:
				ch->jb_len = 4000;
				ch->jb_upper_threshold = 0;
				chan_misdn_log(1, ch->bc->port, " --> buffer_len:%d (default)\n", ch->jb_len);
				chan_misdn_log(1, ch->bc->port, " --> upper_threshold:%d (default)\n", ch->jb_upper_threshold);
			}
			break;
		case 'v':
			tok++;

			switch (tok[0]) {
			case 'r' :
				rxgain = atoi(++tok);
				if (rxgain < -8)
					rxgain = -8;
				if (rxgain > 8)
					rxgain = 8;
				ch->bc->rxgain = rxgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n", rxgain);
				break;
			case 't':
				txgain = atoi(++tok);
				if (txgain < -8)
					txgain = -8;
				if (txgain > 8)
					txgain = 8;
				ch->bc->txgain = txgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n", txgain);
				break;
			}
			break;
      
		case 'c':
			keyidx = atoi(++tok);
			{
				char keys[4096];
				char *key = NULL, *tmp = keys;
				int i;
				misdn_cfg_get(0, MISDN_GEN_CRYPT_KEYS, keys, sizeof(keys));

				for (i = 0; i < keyidx; i++) {
					key = strsep(&tmp, ",");
				}

				if (key) {
					ast_copy_string(ch->bc->crypt_key, key, sizeof(ch->bc->crypt_key));
				}

				chan_misdn_log(0, ch->bc->port, "SETOPT: crypt with key:%s\n", ch->bc->crypt_key);
				break;
			}
		case 'e':
			chan_misdn_log(1, ch->bc->port, "SETOPT: EchoCancel\n");
			
			if (neglect) {
				chan_misdn_log(1, ch->bc->port, " --> disabled\n");
#ifdef MISDN_1_2
				*ch->bc->pipeline = 0;
#else
				ch->bc->ec_enable = 0;
#endif
			} else {
#ifdef MISDN_1_2
				update_pipeline_config(ch->bc);
#else
				ch->bc->ec_enable = 1;
				ch->bc->orig = ch->originator;
				tok++;
				if (*tok) {
					ch->bc->ec_deftaps = atoi(tok);
				}
#endif
			}
			
			break;
		case 'h':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Digital\n");
			
			if (strlen(tok) > 1 && tok[1] == '1') {
				chan_misdn_log(1, ch->bc->port, "SETOPT: HDLC \n");
				if (!ch->bc->hdlc) {
					ch->bc->hdlc = 1;
				}
			}
			ch->bc->capability = INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
			break;
            
		case 's':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Send DTMF\n");
			ch->bc->send_dtmf = 1;
			break;
			
		case 'f':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Faxdetect\n");
			ch->faxdetect = 1;
			misdn_cfg_get(ch->bc->port, MISDN_CFG_FAXDETECT_TIMEOUT, &ch->faxdetect_timeout, sizeof(ch->faxdetect_timeout));
			break;

		case 'a':
			chan_misdn_log(1, ch->bc->port, "SETOPT: AST_DSP (for DTMF)\n");
			ch->ast_dsp = 1;
			break;

		case 'p':
			chan_misdn_log(1, ch->bc->port, "SETOPT: callerpres: %s\n", &tok[1]);
			/* CRICH: callingpres!!! */
			if (strstr(tok,"allowed")) {
				ch->bc->pres = 0;
			} else if (strstr(tok, "restricted")) {
				ch->bc->pres = 1;
			} else if (strstr(tok, "not_screened")) {
				chan_misdn_log(0, ch->bc->port, "SETOPT: callerpres: not_screened is deprecated\n");
				ch->bc->pres = 1;
			}
			break;
	  	case 'i' :
			chan_misdn_log(1, ch->bc->port, "Ignoring dtmf tones, just use them inband\n");
			ch->ignore_dtmf=1;
			break;
		default:
			break;
		}
	}

	if (change_jitter)
		config_jitterbuffer(ch);

	if (ch->faxdetect || ch->ast_dsp) {
		if (!ch->dsp)
			ch->dsp = ast_dsp_new();
		if (ch->dsp)
			ast_dsp_set_features(ch->dsp, DSP_FEATURE_DTMF_DETECT| DSP_FEATURE_FAX_DETECT);
		if (!ch->trans)
			ch->trans = ast_translator_build_path(AST_FORMAT_SLINEAR, AST_FORMAT_ALAW);
	}

	if (ch->ast_dsp) {
		chan_misdn_log(1, ch->bc->port, "SETOPT: with AST_DSP we deactivate mISDN_dsp\n");
		ch->bc->nodsp = 1;
	}
	
	return 0;
}


int chan_misdn_jb_empty ( struct misdn_bchannel *bc, char *buf, int len) 
{
	struct chan_list *ch = find_chan_by_bc(cl_te, bc);
	
	if (ch && ch->jb) {
		return misdn_jb_empty(ch->jb, buf, len);
	}
	
	return -1;
}



/*******************************************************/
/***************** JITTERBUFFER ************************/
/*******************************************************/


/* allocates the jb-structure and initialize the elements*/
struct misdn_jb *misdn_jb_init(int size, int upper_threshold)
{
	int i;
	struct misdn_jb *jb;

	jb = malloc(sizeof(struct misdn_jb));
	if (!jb) {
	    chan_misdn_log(-1, 0, "No free Mem for jb\n");
	    return NULL;
	}
	jb->size = size;
	jb->upper_threshold = upper_threshold;
	jb->wp = 0;
	jb->rp = 0;
	jb->state_full = 0;
	jb->state_empty = 0;
	jb->bytes_wrote = 0;
	jb->samples = malloc(size * sizeof(char));
	if (!jb->samples) {
		free(jb);
		chan_misdn_log(-1, 0, "No free Mem for jb->samples\n");
		return NULL;
	}

	jb->ok = malloc(size * sizeof(char));
	if (!jb->ok) {
		free(jb->samples);
		free(jb);
		chan_misdn_log(-1, 0, "No free Mem for jb->ok\n");
		return NULL;
	}

	for (i = 0; i < size; i++)
		jb->ok[i] = 0;

	ast_mutex_init(&jb->mutexjb);

	return jb;
}

/* frees the data and destroys the given jitterbuffer struct */
void misdn_jb_destroy(struct misdn_jb *jb)
{
	ast_mutex_destroy(&jb->mutexjb);
	
	free(jb->ok);
	free(jb->samples);
	free(jb);
}

/* fills the jitterbuffer with len data returns < 0 if there was an
   error (buffer overflow). */
int misdn_jb_fill(struct misdn_jb *jb, const char *data, int len)
{
	int i, j, rp, wp;

	if (!jb || ! data)
		return 0;

	ast_mutex_lock(&jb->mutexjb);
	
	wp = jb->wp;
	rp = jb->rp;
	
	for (i = 0; i < len; i++) {
		jb->samples[wp] = data[i];
		jb->ok[wp] = 1;
		wp = (wp != jb->size - 1) ? wp + 1 : 0;

		if (wp == jb->rp)
			jb->state_full = 1;
	}

	if (wp >= rp)
		jb->state_buffer = wp - rp;
	else
		jb->state_buffer = jb->size - rp + wp;
	chan_misdn_log(9, 0, "misdn_jb_fill: written:%d | Buffer status:%d p:%p\n", len, jb->state_buffer, jb);

	if (jb->state_full) {
		jb->wp = wp;

		rp = wp;
		for (j = 0; j < jb->upper_threshold; j++)
			rp = (rp != 0) ? rp - 1 : jb->size - 1;
		jb->rp = rp;
		jb->state_full = 0;
		jb->state_empty = 1;

		ast_mutex_unlock(&jb->mutexjb);

		return -1;
	}

	if (!jb->state_empty) {
		jb->bytes_wrote += len;
		if (jb->bytes_wrote >= jb->upper_threshold) {
			jb->state_empty = 1;
			jb->bytes_wrote = 0;
		}
	}
	jb->wp = wp;

	ast_mutex_unlock(&jb->mutexjb);
	
	return 0;
}

/* gets len bytes out of the jitterbuffer if available, else only the
available data is returned and the return value indicates the number
of data. */
int misdn_jb_empty(struct misdn_jb *jb, char *data, int len)
{
	int i, wp, rp, read = 0;

	ast_mutex_lock(&jb->mutexjb);

	rp = jb->rp;
	wp = jb->wp;

	if (jb->state_empty) {	
		for (i = 0; i < len; i++) {
			if (wp == rp) {
				jb->rp = rp;
				jb->state_empty = 0;

				ast_mutex_unlock(&jb->mutexjb);

				return read;
			} else {
				if (jb->ok[rp] == 1) {
					data[i] = jb->samples[rp];
					jb->ok[rp] = 0;
					rp = (rp != jb->size - 1) ? rp + 1 : 0;
					read += 1;
				}
			}
		}

		if (wp >= rp)
			jb->state_buffer = wp - rp;
		else
			jb->state_buffer = jb->size - rp + wp;
		chan_misdn_log(9, 0, "misdn_jb_empty: read:%d | Buffer status:%d p:%p\n", len, jb->state_buffer, jb);

		jb->rp = rp;
	} else
		chan_misdn_log(9, 0, "misdn_jb_empty: Wait...requested:%d p:%p\n", len, jb);

	ast_mutex_unlock(&jb->mutexjb);

	return read;
}




/*******************************************************/
/*************** JITTERBUFFER  END *********************/
/*******************************************************/




static void chan_misdn_log(int level, int port, char *tmpl, ...)
{
	va_list ap;
	char buf[1024];
	char port_buf[8];

	if (! ((0 <= port) && (port <= max_ports))) {
		ast_log(LOG_WARNING, "cb_log called with out-of-range port number! (%d)\n", port);
		port = 0;
		level = -1;
	}

	snprintf(port_buf, sizeof(port_buf), "P[%2d] ", port);

	va_start(ap, tmpl);
	vsnprintf(buf, sizeof(buf), tmpl, ap);
	va_end(ap);

	if (level == -1)
		ast_log(LOG_WARNING, "%s", buf);

	else if (misdn_debug_only[port] ? 
			(level == 1 && misdn_debug[port]) || (level == misdn_debug[port]) 
		 : level <= misdn_debug[port]) {
		
		ast_console_puts(port_buf);
		ast_console_puts(buf);
	}
	
	if ((level <= misdn_debug[0]) && !ast_strlen_zero(global_tracefile) ) {
		time_t tm = time(NULL);
		char *tmp = ctime(&tm), *p;

		FILE *fp = fopen(global_tracefile, "a+");

		p = strchr(tmp, '\n');
		if (p)
			*p = ':';
		
		if (!fp) {
			ast_console_puts("Error opening Tracefile: [ ");
			ast_console_puts(global_tracefile);
			ast_console_puts(" ] ");
			
			ast_console_puts(strerror(errno));
			ast_console_puts("\n");
			return ;
		}
		
		fputs(tmp, fp);
		fputs(" ", fp);
		fputs(port_buf, fp);
		fputs(" ", fp);
		fputs(buf, fp);

		fclose(fp);
	}
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Channel driver for mISDN Support (BRI/PRI)",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
