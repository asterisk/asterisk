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

/*!
 * \note
 * To use the CCBS/CCNR supplementary service feature and other
 * supplementary services using FACILITY messages requires a
 * modified version of mISDN.
 *
 * \note
 * The latest modified mISDN v1.1.x based version is available at:
 * http://svn.digium.com/svn/thirdparty/mISDN/trunk
 * http://svn.digium.com/svn/thirdparty/mISDNuser/trunk
 *
 * \note
 * Taged versions of the modified mISDN code are available under:
 * http://svn.digium.com/svn/thirdparty/mISDN/tags
 * http://svn.digium.com/svn/thirdparty/mISDNuser/tags
 */

/* Define to enable cli commands to generate canned CCBS messages. */
// #define CCBS_TEST_MESSAGES	1

/*** MODULEINFO
	<depend>isdnnet</depend>
	<depend>misdn</depend>
	<depend>suppserv</depend>
 ***/
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/file.h>
#include <semaphore.h>
#include <ctype.h>
#include <time.h>

#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/io.h"
#include "asterisk/frame.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"
#include "asterisk/musiconhold.h"
#include "asterisk/dsp.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/indications.h"
#include "asterisk/app.h"
#include "asterisk/features.h"
#include "asterisk/term.h"
#include "asterisk/sched.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/causes.h"

#include "chan_misdn_config.h"
#include "isdn_lib.h"

static char global_tracefile[BUFFERSIZE + 1];

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

static char *complete_ch(struct ast_cli_args *a);
static char *complete_debug_port(struct ast_cli_args *a);
static char *complete_show_config(struct ast_cli_args *a);

/* BEGIN: chan_misdn.h */

#if defined(AST_MISDN_ENHANCEMENTS)
/*
 * This timeout duration is to clean up any call completion records that
 * are forgotten about by the switch.
 */
#define MISDN_CC_RECORD_AGE_MAX		(6UL * 60 * 60)	/* seconds */

#define MISDN_CC_REQUEST_WAIT_MAX	5	/* seconds */

/*!
 * \brief Caller that initialized call completion services
 *
 * \details
 * This data is the payload for a datastore that is put on the channel that
 * initializes call completion services.  This datastore is set to be inherited
 * by the outbound mISDN channel.  When one of these channels hangs up, the
 * channel pointer will be set to NULL.  That way, we can ensure that we do not
 * touch this channel after it gets destroyed.
 */
struct misdn_cc_caller {
	/*! \brief The channel that initialized call completion services */
	struct ast_channel *chan;
};

struct misdn_cc_notify {
	/*! \brief Dialplan: Notify extension priority */
	int priority;

	/*! \brief Dialplan: Notify extension context */
	char context[AST_MAX_CONTEXT];

	/*! \brief Dialplan: Notify extension number (User-A) */
	char exten[AST_MAX_EXTENSION];
};

/*! \brief mISDN call completion record */
struct misdn_cc_record {
	/*! \brief Call completion record linked list */
	AST_LIST_ENTRY(misdn_cc_record) list;

	/*! \brief Time the record was created. */
	time_t time_created;

	/*! \brief MISDN_CC_RECORD_ID value */
	long record_id;

	/*!
	 * \brief Logical Layer 1 port associated with this
	 * call completion record
	 */
	int port;

	/*! \brief TRUE if point-to-point mode (CCBS-T/CCNR-T mode) */
	int ptp;

	/*! \brief Mode specific parameters */
	union {
		/*! \brief point-to-point specific parameters. */
		struct {
			/*!
			 * \brief Call-completion signaling link.
			 * NULL if signaling link not established.
			 */
			struct misdn_bchannel *bc;

			/*!
			 * \brief TRUE if we requested the request retention option
			 * to be enabled.
			 */
			int requested_retention;

			/*!
			 * \brief TRUE if the request retention option is enabled.
			 */
			int retention_enabled;
		} ptp;

		/*! \brief point-to-multi-point specific parameters. */
		struct {
			/*! \brief CallLinkageID (valid when port determined) */
			int linkage_id;

			/*! \breif CCBSReference (valid when activated is TRUE) */
			int reference_id;

			/*! \brief globalRecall(0),	specificRecall(1) */
			int recall_mode;
		} ptmp;
	} mode;

	/*! \brief TRUE if call completion activated */
	int activated;

	/*! \brief Outstanding message ID (valid when outstanding_message) */
	int invoke_id;

	/*! \brief TRUE if waiting for a response from a message (invoke_id is valid) */
	int outstanding_message;

	/*! \brief TRUE if activation has been requested */
	int activation_requested;

	/*!
	 * \brief TRUE if User-A is free
	 * \note PTMP - Used to answer CCBSStatusRequest.
	 * PTP - Determines how to respond to CCBS_T_RemoteUserFree.
	 */
	int party_a_free;

	/*! \brief Error code received from last outstanding message. */
	enum FacErrorCode error_code;

	/*! \brief Reject code received from last outstanding message. */
	enum FacRejectCode reject_code;

	/*!
	 * \brief Saved struct misdn_bchannel call information when
	 * attempted to call User-B
	 */
	struct {
		/*! \brief User-A caller id information */
		struct misdn_party_id caller;

		/*! \brief User-B number information */
		struct misdn_party_dialing dialed;

		/*! \brief The BC, HLC (optional) and LLC (optional) contents from the SETUP message. */
		struct Q931_Bc_Hlc_Llc setup_bc_hlc_llc;

		/*! \brief SETUP message bearer capability field code value */
		int capability;

		/*! \brief TRUE if call made in digital HDLC mode */
		int hdlc;
	} redial;

	/*! \brief Dialplan location to indicate User-B free and User-A is free */
	struct misdn_cc_notify remote_user_free;

	/*! \brief Dialplan location to indicate User-B free and User-A is busy */
	struct misdn_cc_notify b_free;
};

/*! \brief mISDN call completion record database */
static AST_LIST_HEAD_STATIC(misdn_cc_records_db, misdn_cc_record);
/*! \brief Next call completion record ID to use */
static __u16 misdn_cc_record_id;
/*! \brief Next invoke ID to use */
static __s16 misdn_invoke_id;

static const char misdn_no_response_from_network[] = "No response from network";
static const char misdn_cc_record_not_found[] = "Call completion record not found";

/* mISDN channel variable names */
#define MISDN_CC_RECORD_ID	"MISDN_CC_RECORD_ID"
#define MISDN_CC_STATUS		"MISDN_CC_STATUS"
#define MISDN_ERROR_MSG		"MISDN_ERROR_MSG"
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

static ast_mutex_t release_lock;

enum misdn_chan_state {
	MISDN_NOTHING = 0,         /*!< at beginning */
	MISDN_WAITING4DIGS,        /*!< when waiting for info */
	MISDN_EXTCANTMATCH,        /*!< when asterisk couldn't match our ext */
	MISDN_INCOMING_SETUP,      /*!< for incoming setup */
	MISDN_DIALING,             /*!< when pbx_start */
	MISDN_PROGRESS,            /*!< we have progress */
	MISDN_PROCEEDING,          /*!< we have progress */
	MISDN_CALLING,             /*!< when misdn_call is called */
	MISDN_CALLING_ACKNOWLEDGE, /*!< when we get SETUP_ACK */
	MISDN_ALERTING,            /*!< when Alerting */
	MISDN_BUSY,                /*!< when BUSY */
	MISDN_CONNECTED,           /*!< when connected */
	MISDN_DISCONNECTED,        /*!< when connected */
	MISDN_CLEANING,            /*!< when hangup from * but we were connected before */
};

/*! Asterisk created the channel (outgoing call) */
#define ORG_AST 1
/*! mISDN created the channel (incoming call) */
#define ORG_MISDN 2

enum misdn_hold_state {
	MISDN_HOLD_IDLE,		/*!< HOLD not active */
	MISDN_HOLD_ACTIVE,		/*!< Call is held */
	MISDN_HOLD_TRANSFER,	/*!< Held call is being transferred */
	MISDN_HOLD_DISCONNECT,	/*!< Held call is being disconnected */
};
struct hold_info {
	/*!
	 * \brief Call HOLD state.
	 */
	enum misdn_hold_state state;
	/*!
	 * \brief Logical port the channel call record is HELD on
	 * because the B channel is no longer associated.
	 */
	int port;

	/*!
	 * \brief Original B channel number the HELD call was using.
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

	int norxtone;	/*!< Boolean assigned values but the value is not used. */

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

	/*!
	 * \brief Associated B channel structure.
	 */
	struct misdn_bchannel *bc;

#if defined(AST_MISDN_ENHANCEMENTS)
	/*!
	 * \brief Peer channel for which call completion was initialized.
	 */
	struct misdn_cc_caller *peer;

	/*! \brief Associated call completion record ID (-1 if not associated) */
	long record_id;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	/*!
	 * \brief HELD channel call information
	 */
	struct hold_info hold;

	/*!
	 * \brief From associated B channel: Layer 3 process ID
	 * \note Used to find the HELD channel call record when retrieving a call.
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
	struct ast_tone_zone_sound *ts;

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

	/*!
	 * \brief Next channel call record in the list.
	 */
	struct chan_list *next;
};


int MAXTICS = 8;


void export_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);
void import_ch(struct ast_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);
static struct ast_frame *process_ast_dsp(struct chan_list *tmp, struct ast_frame *frame);

struct robin_list {
	char *group;
	int port;
	int channel;
	struct robin_list *next;
	struct robin_list *prev;
};
static struct robin_list *robin = NULL;


static void free_robin_list(void)
{
	struct robin_list *r;
	struct robin_list *next;

	for (r = robin, robin = NULL; r; r = next) {
		next = r->next;
		ast_free(r->group);
		ast_free(r);
	}
}

static struct robin_list *get_robin_position(char *group)
{
	struct robin_list *new;
	struct robin_list *iter = robin;
	for (; iter; iter = iter->next) {
		if (!strcasecmp(iter->group, group)) {
			return iter;
		}
	}
	new = ast_calloc(1, sizeof(*new));
	if (!new) {
		return NULL;
	}
	new->group = ast_strdup(group);
	if (!new->group) {
		ast_free(new);
		return NULL;
	}
	new->channel = 1;
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

static struct ast_channel *misdn_new(struct chan_list *cl, int state,  char *exten, char *callerid, int format, const char *linkedid, int port, int c);
static void send_digit_to_chan(struct chan_list *cl, char digit);

static int pbx_start_chan(struct chan_list *ch);

#define MISDN_ASTERISK_TECH_PVT(ast) ast->tech_pvt

#include "asterisk/strings.h"

/* #define MISDN_DEBUG 1 */

static const char misdn_type[] = "mISDN";

static int tracing = 0;

/*! \brief Only alaw and mulaw is allowed for now */
static int prefformat =  AST_FORMAT_ALAW ; /*  AST_FORMAT_SLINEAR ;  AST_FORMAT_ULAW | */

static int *misdn_debug;
static int *misdn_debug_only;
static int max_ports;

static int *misdn_in_calls;
static int *misdn_out_calls;

/*!
 * \brief Global channel call record list head.
 */
static struct chan_list *cl_te=NULL;
static ast_mutex_t cl_te_lock;

static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data);

static void send_cause2ast(struct ast_channel *ast, struct misdn_bchannel*bc, struct chan_list *ch);

static void cl_queue_chan(struct chan_list **list, struct chan_list *chan);
static void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan);
static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc);
static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid);

static int dialtone_indicate(struct chan_list *cl);
static void hanguptone_indicate(struct chan_list *cl);
static int stop_indicate(struct chan_list *cl);

static int start_bc_tones(struct chan_list *cl);
static int stop_bc_tones(struct chan_list *cl);
static void release_chan_early(struct chan_list *ch);
static void release_chan(struct chan_list *ch, struct misdn_bchannel *bc);

#if defined(AST_MISDN_ENHANCEMENTS)
static const char misdn_command_name[] = "misdn_command";
static int misdn_command_exec(struct ast_channel *chan, const char *data);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
static int misdn_check_l2l1(struct ast_channel *chan, const char *data);
static int misdn_set_opt_exec(struct ast_channel *chan, const char *data);
static int misdn_facility_exec(struct ast_channel *chan, const char *data);

int chan_misdn_jb_empty(struct misdn_bchannel *bc, char *buf, int len);

void debug_numtype(int port, int numtype, char *type);

int add_out_calls(int port);
int add_in_calls(int port);


#ifdef MISDN_1_2
static int update_pipeline_config(struct misdn_bchannel *bc);
#else
static int update_ec_config(struct misdn_bchannel *bc);
#endif



/*************** Helpers *****************/

static struct chan_list *get_chan_by_ast(struct ast_channel *ast)
{
	struct chan_list *tmp;

	for (tmp = cl_te; tmp; tmp = tmp->next) {
		if (tmp->ast == ast) {
			return tmp;
		}
	}

	return NULL;
}

static struct chan_list *get_chan_by_ast_name(const char *name)
{
	struct chan_list *tmp;

	for (tmp = cl_te; tmp; tmp = tmp->next) {
		if (tmp->ast && strcmp(tmp->ast->name, name) == 0) {
			return tmp;
		}
	}

	return NULL;
}

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Destroy the misdn_cc_ds_info datastore payload
 *
 * \param[in] data the datastore payload, a reference to an misdn_cc_caller
 *
 * \details
 * Since the payload is a reference to an astobj2 object, we just decrement its
 * reference count.  Before doing so, we NULL out the channel pointer inside of
 * the misdn_cc_caller instance.  This function will be called in one of two
 * cases.  In both cases, we no longer need the channel pointer:
 *
 *  - The original channel that initialized call completion services, the same
 *    channel that is stored here, has been destroyed early.  This could happen
 *    if it transferred the mISDN channel, for example.
 *
 *  - The mISDN channel that had this datastore inherited on to it is now being
 *    destroyed.  If this is the case, then the call completion events have
 *    already occurred and the appropriate channel variables have already been
 *    set on the original channel that requested call completion services.
 *
 * \return Nothing
 */
static void misdn_cc_ds_destroy(void *data)
{
	struct misdn_cc_caller *cc_caller = data;

	ao2_lock(cc_caller);
	cc_caller->chan = NULL;
	ao2_unlock(cc_caller);

	ao2_ref(cc_caller, -1);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Duplicate the misdn_cc_ds_info datastore payload
 *
 * \param[in] data the datastore payload, a reference to an misdn_cc_caller
 *
 * \details
 * All we need to do is bump the reference count and return the same instance.
 *
 * \return A reference to an instance of a misdn_cc_caller
 */
static void *misdn_cc_ds_duplicate(void *data)
{
	struct misdn_cc_caller *cc_caller = data;

	ao2_ref(cc_caller, +1);

	return cc_caller;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static const struct ast_datastore_info misdn_cc_ds_info = {
	.type      = "misdn_cc",
	.destroy   = misdn_cc_ds_destroy,
	.duplicate = misdn_cc_ds_duplicate,
};
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Set a channel var on the peer channel for call completion services
 *
 * \param[in] peer The peer that initialized call completion services
 * \param[in] var The variable name to set
 * \param[in] value The variable value to set
 *
 * This function may be called from outside of the channel thread.  It handles
 * the fact that the peer channel may be hung up and destroyed at any time.
 *
 * \return nothing
 */
static void misdn_cc_set_peer_var(struct misdn_cc_caller *peer, const char *var,
	const char *value)
{
	ao2_lock(peer);

	/*! \todo XXX This nastiness can go away once ast_channel is ref counted! */
	while (peer->chan && ast_channel_trylock(peer->chan)) {
		ao2_unlock(peer);
		sched_yield();
		ao2_lock(peer);
	}

	if (peer->chan) {
		pbx_builtin_setvar_helper(peer->chan, var, value);
		ast_channel_unlock(peer->chan);
	}

	ao2_unlock(peer);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Get a reference to the CC caller if it exists
 */
static struct misdn_cc_caller *misdn_cc_caller_get(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct misdn_cc_caller *cc_caller;

	ast_channel_lock(chan);

	if (!(datastore = ast_channel_datastore_find(chan, &misdn_cc_ds_info, NULL))) {
		ast_channel_unlock(chan);
		return NULL;
	}

	ao2_ref(datastore->data, +1);
	cc_caller = datastore->data;

	ast_channel_unlock(chan);

	return cc_caller;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Find the call completion record given the record id.
 *
 * \param record_id
 *
 * \retval pointer to found call completion record
 * \retval NULL if not found
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static struct misdn_cc_record *misdn_cc_find_by_id(long record_id)
{
	struct misdn_cc_record *current;

	AST_LIST_TRAVERSE(&misdn_cc_records_db, current, list) {
		if (current->record_id == record_id) {
			/* Found the record */
			break;
		}
	}

	return current;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Find the call completion record given the port and call linkage id.
 *
 * \param port Logical port number
 * \param linkage_id Call linkage ID number from switch.
 *
 * \retval pointer to found call completion record
 * \retval NULL if not found
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static struct misdn_cc_record *misdn_cc_find_by_linkage(int port, int linkage_id)
{
	struct misdn_cc_record *current;

	AST_LIST_TRAVERSE(&misdn_cc_records_db, current, list) {
		if (current->port == port
			&& !current->ptp
			&& current->mode.ptmp.linkage_id == linkage_id) {
			/* Found the record */
			break;
		}
	}

	return current;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Find the call completion record given the port and outstanding invocation id.
 *
 * \param port Logical port number
 * \param invoke_id Outstanding message invocation ID number.
 *
 * \retval pointer to found call completion record
 * \retval NULL if not found
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static struct misdn_cc_record *misdn_cc_find_by_invoke(int port, int invoke_id)
{
	struct misdn_cc_record *current;

	AST_LIST_TRAVERSE(&misdn_cc_records_db, current, list) {
		if (current->outstanding_message
			&& current->invoke_id == invoke_id
			&& current->port == port) {
			/* Found the record */
			break;
		}
	}

	return current;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Find the call completion record given the port and CCBS reference id.
 *
 * \param port Logical port number
 * \param reference_id CCBS reference ID number from switch.
 *
 * \retval pointer to found call completion record
 * \retval NULL if not found
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static struct misdn_cc_record *misdn_cc_find_by_reference(int port, int reference_id)
{
	struct misdn_cc_record *current;

	AST_LIST_TRAVERSE(&misdn_cc_records_db, current, list) {
		if (current->activated
			&& current->port == port
			&& !current->ptp
			&& current->mode.ptmp.reference_id == reference_id) {
			/* Found the record */
			break;
		}
	}

	return current;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Find the call completion record given the B channel pointer
 *
 * \param bc B channel control structure pointer.
 *
 * \retval pointer to found call completion record
 * \retval NULL if not found
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static struct misdn_cc_record *misdn_cc_find_by_bc(const struct misdn_bchannel *bc)
{
	struct misdn_cc_record *current;

	if (bc) {
		AST_LIST_TRAVERSE(&misdn_cc_records_db, current, list) {
			if (current->ptp
				&& current->mode.ptp.bc == bc) {
				/* Found the record */
				break;
			}
		}
	} else {
		current = NULL;
	}

	return current;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Delete the given call completion record
 *
 * \param doomed Call completion record to destroy
 *
 * \return Nothing
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static void misdn_cc_delete(struct misdn_cc_record *doomed)
{
	struct misdn_cc_record *current;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&misdn_cc_records_db, current, list) {
		if (current == doomed) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(current);
			return;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* The doomed node is not in the call completion database */
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Delete all old call completion records
 *
 * \return Nothing
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static void misdn_cc_remove_old(void)
{
	struct misdn_cc_record *current;
	time_t now;

	now = time(NULL);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&misdn_cc_records_db, current, list) {
		if (MISDN_CC_RECORD_AGE_MAX < now - current->time_created) {
			if (current->ptp && current->mode.ptp.bc) {
				/* Close the old call-completion signaling link */
				current->mode.ptp.bc->fac_out.Function = Fac_None;
				current->mode.ptp.bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
				misdn_lib_send_event(current->mode.ptp.bc, EVENT_RELEASE_COMPLETE);
			}

			/* Remove the old call completion record */
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(current);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Allocate the next record id.
 *
 * \retval New record id on success.
 * \retval -1 on error.
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static long misdn_cc_record_id_new(void)
{
	long record_id;
	long first_id;

	record_id = ++misdn_cc_record_id;
	first_id = record_id;
	while (misdn_cc_find_by_id(record_id)) {
		record_id = ++misdn_cc_record_id;
		if (record_id == first_id) {
			/*
			 * We have a resource leak.
			 * We should never need to allocate 64k records.
			 */
			chan_misdn_log(0, 0, " --> ERROR Too many call completion records!\n");
			record_id = -1;
			break;
		}
	}

	return record_id;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Create a new call completion record
 *
 * \retval pointer to new call completion record
 * \retval NULL if failed
 *
 * \note Assumes the misdn_cc_records_db lock is already obtained.
 */
static struct misdn_cc_record *misdn_cc_new(void)
{
	struct misdn_cc_record *cc_record;
	long record_id;

	misdn_cc_remove_old();

	cc_record = ast_calloc(1, sizeof(*cc_record));
	if (cc_record) {
		record_id = misdn_cc_record_id_new();
		if (record_id < 0) {
			ast_free(cc_record);
			return NULL;
		}

		/* Initialize the new record */
		cc_record->record_id = record_id;
		cc_record->port = -1;/* Invalid port so it will never be found this way */
		cc_record->invoke_id = ++misdn_invoke_id;
		cc_record->party_a_free = 1;/* Default User-A as free */
		cc_record->error_code = FacError_None;
		cc_record->reject_code = FacReject_None;
		cc_record->time_created = time(NULL);

		/* Insert the new record into the database */
		AST_LIST_INSERT_HEAD(&misdn_cc_records_db, cc_record, list);
	}
	return cc_record;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Destroy the call completion record database
 *
 * \return Nothing
 */
static void misdn_cc_destroy(void)
{
	struct misdn_cc_record *current;

	while ((current = AST_LIST_REMOVE_HEAD(&misdn_cc_records_db, list))) {
		/* Do a misdn_cc_delete(current) inline */
		ast_free(current);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Initialize the call completion record database
 *
 * \return Nothing
 */
static void misdn_cc_init(void)
{
	misdn_cc_record_id = 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Check the status of an outstanding invocation request.
 *
 * \param data Points to an integer containing the call completion record id.
 *
 * \retval 0 if got a response.
 * \retval -1 if no response yet.
 */
static int misdn_cc_response_check(void *data)
{
	int not_responded;
	struct misdn_cc_record *cc_record;

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(*(long *) data);
	if (cc_record) {
		if (cc_record->outstanding_message) {
			not_responded = -1;
		} else {
			not_responded = 0;
		}
	} else {
		/* No record so there is no response to check. */
		not_responded = 0;
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);

	return not_responded;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Wait for a response from the switch for an outstanding
 * invocation request.
 *
 * \param chan Asterisk channel to operate upon.
 * \param wait_seconds Number of seconds to wait
 * \param record_id Call completion record ID.
 *
 * \return Nothing
 */
static void misdn_cc_response_wait(struct ast_channel *chan, int wait_seconds, long record_id)
{
	unsigned count;

	for (count = 2 * MISDN_CC_REQUEST_WAIT_MAX; count--;) {
		/* Sleep in 500 ms increments */
		if (ast_safe_sleep_conditional(chan, 500, misdn_cc_response_check, &record_id) != 0) {
			/* We got hung up or our response came in. */
			break;
		}
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert the mISDN reject code to a string
 *
 * \param code mISDN reject code.
 *
 * \return The mISDN reject code as a string
 */
static const char *misdn_to_str_reject_code(enum FacRejectCode code)
{
	static const struct {
		enum FacRejectCode code;
		char *name;
	} arr[] = {
/* *INDENT-OFF* */
		{ FacReject_None,                           "No reject occurred" },
		{ FacReject_Unknown,                        "Unknown reject code" },

		{ FacReject_Gen_UnrecognizedComponent,      "General: Unrecognized Component" },
		{ FacReject_Gen_MistypedComponent,          "General: Mistyped Component" },
		{ FacReject_Gen_BadlyStructuredComponent,   "General: Badly Structured Component" },

		{ FacReject_Inv_DuplicateInvocation,        "Invoke: Duplicate Invocation" },
		{ FacReject_Inv_UnrecognizedOperation,      "Invoke: Unrecognized Operation" },
		{ FacReject_Inv_MistypedArgument,           "Invoke: Mistyped Argument" },
		{ FacReject_Inv_ResourceLimitation,         "Invoke: Resource Limitation" },
		{ FacReject_Inv_InitiatorReleasing,         "Invoke: Initiator Releasing" },
		{ FacReject_Inv_UnrecognizedLinkedID,       "Invoke: Unrecognized Linked ID" },
		{ FacReject_Inv_LinkedResponseUnexpected,   "Invoke: Linked Response Unexpected" },
		{ FacReject_Inv_UnexpectedChildOperation,   "Invoke: Unexpected Child Operation" },

		{ FacReject_Res_UnrecognizedInvocation,     "Result: Unrecognized Invocation" },
		{ FacReject_Res_ResultResponseUnexpected,   "Result: Result Response Unexpected" },
		{ FacReject_Res_MistypedResult,             "Result: Mistyped Result" },

		{ FacReject_Err_UnrecognizedInvocation,     "Error: Unrecognized Invocation" },
		{ FacReject_Err_ErrorResponseUnexpected,    "Error: Error Response Unexpected" },
		{ FacReject_Err_UnrecognizedError,          "Error: Unrecognized Error" },
		{ FacReject_Err_UnexpectedError,            "Error: Unexpected Error" },
		{ FacReject_Err_MistypedParameter,          "Error: Mistyped Parameter" },
/* *INDENT-ON* */
	};

	unsigned index;

	for (index = 0; index < ARRAY_LEN(arr); ++index) {
		if (arr[index].code == code) {
			return arr[index].name;
		}
	}

	return "unknown";
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert the mISDN error code to a string
 *
 * \param code mISDN error code.
 *
 * \return The mISDN error code as a string
 */
static const char *misdn_to_str_error_code(enum FacErrorCode code)
{
	static const struct {
		enum FacErrorCode code;
		char *name;
	} arr[] = {
/* *INDENT-OFF* */
		{ FacError_None,                            "No error occurred" },
		{ FacError_Unknown,                         "Unknown OID error code" },

		{ FacError_Gen_NotSubscribed,               "General: Not Subscribed" },
		{ FacError_Gen_NotAvailable,                "General: Not Available" },
		{ FacError_Gen_NotImplemented,              "General: Not Implemented" },
		{ FacError_Gen_InvalidServedUserNr,         "General: Invalid Served User Number" },
		{ FacError_Gen_InvalidCallState,            "General: Invalid Call State" },
		{ FacError_Gen_BasicServiceNotProvided,     "General: Basic Service Not Provided" },
		{ FacError_Gen_NotIncomingCall,             "General: Not Incoming Call" },
		{ FacError_Gen_SupplementaryServiceInteractionNotAllowed,"General: Supplementary Service Interaction Not Allowed" },
		{ FacError_Gen_ResourceUnavailable,         "General: Resource Unavailable" },

		{ FacError_Div_InvalidDivertedToNr,         "Diversion: Invalid Diverted To Number" },
		{ FacError_Div_SpecialServiceNr,            "Diversion: Special Service Number" },
		{ FacError_Div_DiversionToServedUserNr,     "Diversion: Diversion To Served User Number" },
		{ FacError_Div_IncomingCallAccepted,        "Diversion: Incoming Call Accepted" },
		{ FacError_Div_NumberOfDiversionsExceeded,  "Diversion: Number Of Diversions Exceeded" },
		{ FacError_Div_NotActivated,                "Diversion: Not Activated" },
		{ FacError_Div_RequestAlreadyAccepted,      "Diversion: Request Already Accepted" },

		{ FacError_AOC_NoChargingInfoAvailable,     "AOC: No Charging Info Available" },

		{ FacError_CCBS_InvalidCallLinkageID,       "CCBS: Invalid Call Linkage ID" },
		{ FacError_CCBS_InvalidCCBSReference,       "CCBS: Invalid CCBS Reference" },
		{ FacError_CCBS_LongTermDenial,             "CCBS: Long Term Denial" },
		{ FacError_CCBS_ShortTermDenial,            "CCBS: Short Term Denial" },
		{ FacError_CCBS_IsAlreadyActivated,         "CCBS: Is Already Activated" },
		{ FacError_CCBS_AlreadyAccepted,            "CCBS: Already Accepted" },
		{ FacError_CCBS_OutgoingCCBSQueueFull,      "CCBS: Outgoing CCBS Queue Full" },
		{ FacError_CCBS_CallFailureReasonNotBusy,   "CCBS: Call Failure Reason Not Busy" },
		{ FacError_CCBS_NotReadyForCall,            "CCBS: Not Ready For Call" },

		{ FacError_CCBS_T_LongTermDenial,           "CCBS-T: Long Term Denial" },
		{ FacError_CCBS_T_ShortTermDenial,          "CCBS-T: Short Term Denial" },

		{ FacError_ECT_LinkIdNotAssignedByNetwork,  "ECT: Link ID Not Assigned By Network" },
/* *INDENT-ON* */
	};

	unsigned index;

	for (index = 0; index < ARRAY_LEN(arr); ++index) {
		if (arr[index].code == code) {
			return arr[index].name;
		}
	}

	return "unknown";
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert mISDN redirecting reason to diversion reason.
 *
 * \param reason mISDN redirecting reason code.
 *
 * \return Supported diversion reason code.
 */
static unsigned misdn_to_diversion_reason(enum mISDN_REDIRECTING_REASON reason)
{
	unsigned diversion_reason;

	switch (reason) {
	case mISDN_REDIRECTING_REASON_CALL_FWD:
		diversion_reason = 1;/* cfu */
		break;
	case mISDN_REDIRECTING_REASON_CALL_FWD_BUSY:
		diversion_reason = 2;/* cfb */
		break;
	case mISDN_REDIRECTING_REASON_NO_REPLY:
		diversion_reason = 3;/* cfnr */
		break;
	default:
		diversion_reason = 0;/* unknown */
		break;
	}

	return diversion_reason;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert diversion reason to mISDN redirecting reason
 *
 * \param diversion_reason Diversion reason to convert
 *
 * \return Supported redirecting reason code.
 */
static enum mISDN_REDIRECTING_REASON diversion_reason_to_misdn(unsigned diversion_reason)
{
	enum mISDN_REDIRECTING_REASON reason;

	switch (diversion_reason) {
	case 1:/* cfu */
		reason = mISDN_REDIRECTING_REASON_CALL_FWD;
		break;
	case 2:/* cfb */
		reason = mISDN_REDIRECTING_REASON_CALL_FWD_BUSY;
		break;
	case 3:/* cfnr */
		reason = mISDN_REDIRECTING_REASON_NO_REPLY;
		break;
	default:
		reason = mISDN_REDIRECTING_REASON_UNKNOWN;
		break;
	}

	return reason;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert the mISDN presentation to PresentedNumberUnscreened type
 *
 * \param presentation mISDN presentation to convert
 * \param number_present TRUE if the number is present
 *
 * \return PresentedNumberUnscreened type
 */
static unsigned misdn_to_PresentedNumberUnscreened_type(int presentation, int number_present)
{
	unsigned type;

	switch (presentation) {
	case 0:/* allowed */
		if (number_present) {
			type = 0;/* presentationAllowedNumber */
		} else {
			type = 2;/* numberNotAvailableDueToInterworking */
		}
		break;
	case 1:/* restricted */
		if (number_present) {
			type = 3;/* presentationRestrictedNumber */
		} else {
			type = 1;/* presentationRestricted */
		}
		break;
	default:
		type = 2;/* numberNotAvailableDueToInterworking */
		break;
	}

	return type;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert the PresentedNumberUnscreened type to mISDN presentation
 *
 * \param type PresentedNumberUnscreened type
 *
 * \return mISDN presentation
 */
static int PresentedNumberUnscreened_to_misdn_pres(unsigned type)
{
	int presentation;

	switch (type) {
	default:
	case 0:/* presentationAllowedNumber */
		presentation = 0;/* allowed */
		break;

	case 1:/* presentationRestricted */
	case 3:/* presentationRestrictedNumber */
		presentation = 1;/* restricted */
		break;

	case 2:/* numberNotAvailableDueToInterworking */
		presentation = 2;/* unavailable */
		break;
	}

	return presentation;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert the mISDN numbering plan to PartyNumber numbering plan
 *
 * \param number_plan mISDN numbering plan
 *
 * \return PartyNumber numbering plan
 */
static unsigned misdn_to_PartyNumber_plan(enum mISDN_NUMBER_PLAN number_plan)
{
	unsigned party_plan;

	switch (number_plan) {
	default:
	case NUMPLAN_UNKNOWN:
		party_plan = 0;/* unknown */
		break;

	case NUMPLAN_ISDN:
		party_plan = 1;/* public */
		break;

	case NUMPLAN_DATA:
		party_plan = 3;/* data */
		break;

	case NUMPLAN_TELEX:
		party_plan = 4;/* telex */
		break;

	case NUMPLAN_NATIONAL:
		party_plan = 8;/* nationalStandard */
		break;

	case NUMPLAN_PRIVATE:
		party_plan = 5;/* private */
		break;
	}

	return party_plan;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert PartyNumber numbering plan to mISDN numbering plan
 *
 * \param party_plan PartyNumber numbering plan
 *
 * \return mISDN numbering plan
 */
static enum mISDN_NUMBER_PLAN PartyNumber_to_misdn_plan(unsigned party_plan)
{
	enum mISDN_NUMBER_PLAN number_plan;

	switch (party_plan) {
	default:
	case 0:/* unknown */
		number_plan = NUMPLAN_UNKNOWN;
		break;
	case 1:/* public */
		number_plan = NUMPLAN_ISDN;
		break;
	case 3:/* data */
		number_plan = NUMPLAN_DATA;
		break;
	case 4:/* telex */
		number_plan = NUMPLAN_TELEX;
		break;
	case 8:/* nationalStandard */
		number_plan = NUMPLAN_NATIONAL;
		break;
	case 5:/* private */
		number_plan = NUMPLAN_PRIVATE;
		break;
	}

	return number_plan;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert mISDN type-of-number to PartyNumber public type-of-number
 *
 * \param ton mISDN type-of-number
 *
 * \return PartyNumber public type-of-number
 */
static unsigned misdn_to_PartyNumber_ton_public(enum mISDN_NUMBER_TYPE ton)
{
	unsigned party_ton;

	switch (ton) {
	default:
	case NUMTYPE_UNKNOWN:
		party_ton = 0;/* unknown */
		break;

	case NUMTYPE_INTERNATIONAL:
		party_ton = 1;/* internationalNumber */
		break;

	case NUMTYPE_NATIONAL:
		party_ton = 2;/* nationalNumber */
		break;

	case NUMTYPE_NETWORK_SPECIFIC:
		party_ton = 3;/* networkSpecificNumber */
		break;

	case NUMTYPE_SUBSCRIBER:
		party_ton = 4;/* subscriberNumber */
		break;

	case NUMTYPE_ABBREVIATED:
		party_ton = 6;/* abbreviatedNumber */
		break;
	}

	return party_ton;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert the PartyNumber public type-of-number to mISDN type-of-number
 *
 * \param party_ton PartyNumber public type-of-number
 *
 * \return mISDN type-of-number
 */
static enum mISDN_NUMBER_TYPE PartyNumber_to_misdn_ton_public(unsigned party_ton)
{
	enum mISDN_NUMBER_TYPE ton;

	switch (party_ton) {
	default:
	case 0:/* unknown */
		ton = NUMTYPE_UNKNOWN;
		break;

	case 1:/* internationalNumber */
		ton = NUMTYPE_INTERNATIONAL;
		break;

	case 2:/* nationalNumber */
		ton = NUMTYPE_NATIONAL;
		break;

	case 3:/* networkSpecificNumber */
		ton = NUMTYPE_NETWORK_SPECIFIC;
		break;

	case 4:/* subscriberNumber */
		ton = NUMTYPE_SUBSCRIBER;
		break;

	case 6:/* abbreviatedNumber */
		ton = NUMTYPE_ABBREVIATED;
		break;
	}

	return ton;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert mISDN type-of-number to PartyNumber private type-of-number
 *
 * \param ton mISDN type-of-number
 *
 * \return PartyNumber private type-of-number
 */
static unsigned misdn_to_PartyNumber_ton_private(enum mISDN_NUMBER_TYPE ton)
{
	unsigned party_ton;

	switch (ton) {
	default:
	case NUMTYPE_UNKNOWN:
		party_ton = 0;/* unknown */
		break;

	case NUMTYPE_INTERNATIONAL:
		party_ton = 1;/* level2RegionalNumber */
		break;

	case NUMTYPE_NATIONAL:
		party_ton = 2;/* level1RegionalNumber */
		break;

	case NUMTYPE_NETWORK_SPECIFIC:
		party_ton = 3;/* pTNSpecificNumber */
		break;

	case NUMTYPE_SUBSCRIBER:
		party_ton = 4;/* localNumber */
		break;

	case NUMTYPE_ABBREVIATED:
		party_ton = 6;/* abbreviatedNumber */
		break;
	}

	return party_ton;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Convert the PartyNumber private type-of-number to mISDN type-of-number
 *
 * \param party_ton PartyNumber private type-of-number
 *
 * \return mISDN type-of-number
 */
static enum mISDN_NUMBER_TYPE PartyNumber_to_misdn_ton_private(unsigned party_ton)
{
	enum mISDN_NUMBER_TYPE ton;

	switch (party_ton) {
	default:
	case 0:/* unknown */
		ton = NUMTYPE_UNKNOWN;
		break;

	case 1:/* level2RegionalNumber */
		ton = NUMTYPE_INTERNATIONAL;
		break;

	case 2:/* level1RegionalNumber */
		ton = NUMTYPE_NATIONAL;
		break;

	case 3:/* pTNSpecificNumber */
		ton = NUMTYPE_NETWORK_SPECIFIC;
		break;

	case 4:/* localNumber */
		ton = NUMTYPE_SUBSCRIBER;
		break;

	case 6:/* abbreviatedNumber */
		ton = NUMTYPE_ABBREVIATED;
		break;
	}

	return ton;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

/*!
 * \internal
 * \brief Convert the mISDN type of number code to a string
 *
 * \param number_type mISDN type of number code.
 *
 * \return The mISDN type of number code as a string
 */
static const char *misdn_to_str_ton(enum mISDN_NUMBER_TYPE number_type)
{
	const char *str;

	switch (number_type) {
	default:
	case NUMTYPE_UNKNOWN:
		str = "Unknown";
		break;

	case NUMTYPE_INTERNATIONAL:
		str = "International";
		break;

	case NUMTYPE_NATIONAL:
		str = "National";
		break;

	case NUMTYPE_NETWORK_SPECIFIC:
		str = "Network Specific";
		break;

	case NUMTYPE_SUBSCRIBER:
		str = "Subscriber";
		break;

	case NUMTYPE_ABBREVIATED:
		str = "Abbreviated";
		break;
	}

	return str;
}

/*!
 * \internal
 * \brief Convert the mISDN type of number code to Asterisk type of number code
 *
 * \param number_type mISDN type of number code.
 *
 * \return Asterisk type of number code
 */
static int misdn_to_ast_ton(enum mISDN_NUMBER_TYPE number_type)
{
	int ast_number_type;

	switch (number_type) {
	default:
	case NUMTYPE_UNKNOWN:
		ast_number_type = NUMTYPE_UNKNOWN << 4;
		break;

	case NUMTYPE_INTERNATIONAL:
		ast_number_type = NUMTYPE_INTERNATIONAL << 4;
		break;

	case NUMTYPE_NATIONAL:
		ast_number_type = NUMTYPE_NATIONAL << 4;
		break;

	case NUMTYPE_NETWORK_SPECIFIC:
		ast_number_type = NUMTYPE_NETWORK_SPECIFIC << 4;
		break;

	case NUMTYPE_SUBSCRIBER:
		ast_number_type = NUMTYPE_SUBSCRIBER << 4;
		break;

	case NUMTYPE_ABBREVIATED:
		ast_number_type = NUMTYPE_ABBREVIATED << 4;
		break;
	}

	return ast_number_type;
}

/*!
 * \internal
 * \brief Convert the Asterisk type of number code to mISDN type of number code
 *
 * \param ast_number_type Asterisk type of number code.
 *
 * \return mISDN type of number code
 */
static enum mISDN_NUMBER_TYPE ast_to_misdn_ton(unsigned ast_number_type)
{
	enum mISDN_NUMBER_TYPE number_type;

	switch ((ast_number_type >> 4) & 0x07) {
	default:
	case NUMTYPE_UNKNOWN:
		number_type = NUMTYPE_UNKNOWN;
		break;

	case NUMTYPE_INTERNATIONAL:
		number_type = NUMTYPE_INTERNATIONAL;
		break;

	case NUMTYPE_NATIONAL:
		number_type = NUMTYPE_NATIONAL;
		break;

	case NUMTYPE_NETWORK_SPECIFIC:
		number_type = NUMTYPE_NETWORK_SPECIFIC;
		break;

	case NUMTYPE_SUBSCRIBER:
		number_type = NUMTYPE_SUBSCRIBER;
		break;

	case NUMTYPE_ABBREVIATED:
		number_type = NUMTYPE_ABBREVIATED;
		break;
	}

	return number_type;
}

/*!
 * \internal
 * \brief Convert the mISDN numbering plan code to a string
 *
 * \param number_plan mISDN numbering plan code.
 *
 * \return The mISDN numbering plan code as a string
 */
static const char *misdn_to_str_plan(enum mISDN_NUMBER_PLAN number_plan)
{
	const char *str;

	switch (number_plan) {
	default:
	case NUMPLAN_UNKNOWN:
		str = "Unknown";
		break;

	case NUMPLAN_ISDN:
		str = "ISDN";
		break;

	case NUMPLAN_DATA:
		str = "Data";
		break;

	case NUMPLAN_TELEX:
		str = "Telex";
		break;

	case NUMPLAN_NATIONAL:
		str = "National";
		break;

	case NUMPLAN_PRIVATE:
		str = "Private";
		break;
	}

	return str;
}

/*!
 * \internal
 * \brief Convert the mISDN numbering plan code to Asterisk numbering plan code
 *
 * \param number_plan mISDN numbering plan code.
 *
 * \return Asterisk numbering plan code
 */
static int misdn_to_ast_plan(enum mISDN_NUMBER_PLAN number_plan)
{
	int ast_number_plan;

	switch (number_plan) {
	default:
	case NUMPLAN_UNKNOWN:
		ast_number_plan = NUMPLAN_UNKNOWN;
		break;

	case NUMPLAN_ISDN:
		ast_number_plan = NUMPLAN_ISDN;
		break;

	case NUMPLAN_DATA:
		ast_number_plan = NUMPLAN_DATA;
		break;

	case NUMPLAN_TELEX:
		ast_number_plan = NUMPLAN_TELEX;
		break;

	case NUMPLAN_NATIONAL:
		ast_number_plan = NUMPLAN_NATIONAL;
		break;

	case NUMPLAN_PRIVATE:
		ast_number_plan = NUMPLAN_PRIVATE;
		break;
	}

	return ast_number_plan;
}

/*!
 * \internal
 * \brief Convert the Asterisk numbering plan code to mISDN numbering plan code
 *
 * \param ast_number_plan Asterisk numbering plan code.
 *
 * \return mISDN numbering plan code
 */
static enum mISDN_NUMBER_PLAN ast_to_misdn_plan(unsigned ast_number_plan)
{
	enum mISDN_NUMBER_PLAN number_plan;

	switch (ast_number_plan & 0x0F) {
	default:
	case NUMPLAN_UNKNOWN:
		number_plan = NUMPLAN_UNKNOWN;
		break;

	case NUMPLAN_ISDN:
		number_plan = NUMPLAN_ISDN;
		break;

	case NUMPLAN_DATA:
		number_plan = NUMPLAN_DATA;
		break;

	case NUMPLAN_TELEX:
		number_plan = NUMPLAN_TELEX;
		break;

	case NUMPLAN_NATIONAL:
		number_plan = NUMPLAN_NATIONAL;
		break;

	case NUMPLAN_PRIVATE:
		number_plan = NUMPLAN_PRIVATE;
		break;
	}

	return number_plan;
}

/*!
 * \internal
 * \brief Convert the mISDN presentation code to a string
 *
 * \param presentation mISDN number presentation restriction code.
 *
 * \return The mISDN presentation code as a string
 */
static const char *misdn_to_str_pres(int presentation)
{
	const char *str;

	switch (presentation) {
	case 0:
		str = "Allowed";
		break;

	case 1:
		str = "Restricted";
		break;

	case 2:
		str = "Unavailable";
		break;

	default:
		str = "Unknown";
		break;
	}

	return str;
}

/*!
 * \internal
 * \brief Convert the mISDN presentation code to Asterisk presentation code
 *
 * \param presentation mISDN number presentation restriction code.
 *
 * \return Asterisk presentation code
 */
static int misdn_to_ast_pres(int presentation)
{
	switch (presentation) {
	default:
	case 0:
		presentation = AST_PRES_ALLOWED;
		break;

	case 1:
		presentation = AST_PRES_RESTRICTED;
		break;

	case 2:
		presentation = AST_PRES_UNAVAILABLE;
		break;
	}

	return presentation;
}

/*!
 * \internal
 * \brief Convert the Asterisk presentation code to mISDN presentation code
 *
 * \param presentation Asterisk number presentation restriction code.
 *
 * \return mISDN presentation code
 */
static int ast_to_misdn_pres(int presentation)
{
	switch (presentation & AST_PRES_RESTRICTION) {
	default:
	case AST_PRES_ALLOWED:
		presentation = 0;
		break;

	case AST_PRES_RESTRICTED:
		presentation = 1;
		break;

	case AST_PRES_UNAVAILABLE:
		presentation = 2;
		break;
	}

	return presentation;
}

/*!
 * \internal
 * \brief Convert the mISDN screening code to a string
 *
 * \param screening mISDN number screening code.
 *
 * \return The mISDN screening code as a string
 */
static const char *misdn_to_str_screen(int screening)
{
	const char *str;

	switch (screening) {
	case 0:
		str = "Unscreened";
		break;

	case 1:
		str = "Passed Screen";
		break;

	case 2:
		str = "Failed Screen";
		break;

	case 3:
		str = "Network Number";
		break;

	default:
		str = "Unknown";
		break;
	}

	return str;
}

/*!
 * \internal
 * \brief Convert the mISDN screening code to Asterisk screening code
 *
 * \param screening mISDN number screening code.
 *
 * \return Asterisk screening code
 */
static int misdn_to_ast_screen(int screening)
{
	switch (screening) {
	default:
	case 0:
		screening = AST_PRES_USER_NUMBER_UNSCREENED;
		break;

	case 1:
		screening = AST_PRES_USER_NUMBER_PASSED_SCREEN;
		break;

	case 2:
		screening = AST_PRES_USER_NUMBER_FAILED_SCREEN;
		break;

	case 3:
		screening = AST_PRES_NETWORK_NUMBER;
		break;
	}

	return screening;
}

/*!
 * \internal
 * \brief Convert the Asterisk screening code to mISDN screening code
 *
 * \param screening Asterisk number screening code.
 *
 * \return mISDN screening code
 */
static int ast_to_misdn_screen(int screening)
{
	switch (screening & AST_PRES_NUMBER_TYPE) {
	default:
	case AST_PRES_USER_NUMBER_UNSCREENED:
		screening = 0;
		break;

	case AST_PRES_USER_NUMBER_PASSED_SCREEN:
		screening = 1;
		break;

	case AST_PRES_USER_NUMBER_FAILED_SCREEN:
		screening = 2;
		break;

	case AST_PRES_NETWORK_NUMBER:
		screening = 3;
		break;
	}

	return screening;
}

/*!
 * \internal
 * \brief Convert Asterisk redirecting reason to mISDN redirecting reason code.
 *
 * \param ast Asterisk redirecting reason code.
 *
 * \return mISDN reason code
 */
static enum mISDN_REDIRECTING_REASON ast_to_misdn_reason(const enum AST_REDIRECTING_REASON ast)
{
	unsigned index;

	static const struct misdn_reasons {
		enum AST_REDIRECTING_REASON ast;
		enum mISDN_REDIRECTING_REASON q931;
	} misdn_reason_table[] = {
	/* *INDENT-OFF* */
		{ AST_REDIRECTING_REASON_UNKNOWN,        mISDN_REDIRECTING_REASON_UNKNOWN },
		{ AST_REDIRECTING_REASON_USER_BUSY,      mISDN_REDIRECTING_REASON_CALL_FWD_BUSY },
		{ AST_REDIRECTING_REASON_NO_ANSWER,      mISDN_REDIRECTING_REASON_NO_REPLY },
		{ AST_REDIRECTING_REASON_UNAVAILABLE,    mISDN_REDIRECTING_REASON_NO_REPLY },
		{ AST_REDIRECTING_REASON_UNCONDITIONAL,  mISDN_REDIRECTING_REASON_CALL_FWD },
		{ AST_REDIRECTING_REASON_TIME_OF_DAY,    mISDN_REDIRECTING_REASON_UNKNOWN },
		{ AST_REDIRECTING_REASON_DO_NOT_DISTURB, mISDN_REDIRECTING_REASON_UNKNOWN },
		{ AST_REDIRECTING_REASON_DEFLECTION,     mISDN_REDIRECTING_REASON_DEFLECTION },
		{ AST_REDIRECTING_REASON_FOLLOW_ME,      mISDN_REDIRECTING_REASON_UNKNOWN },
		{ AST_REDIRECTING_REASON_OUT_OF_ORDER,   mISDN_REDIRECTING_REASON_OUT_OF_ORDER },
		{ AST_REDIRECTING_REASON_AWAY,           mISDN_REDIRECTING_REASON_UNKNOWN },
		{ AST_REDIRECTING_REASON_CALL_FWD_DTE,   mISDN_REDIRECTING_REASON_CALL_FWD_DTE }
	/* *INDENT-ON* */
	};

	for (index = 0; index < ARRAY_LEN(misdn_reason_table); ++index) {
		if (misdn_reason_table[index].ast == ast) {
			return misdn_reason_table[index].q931;
		}
	}
	return mISDN_REDIRECTING_REASON_UNKNOWN;
}

/*!
 * \internal
 * \brief Convert the mISDN redirecting reason to Asterisk redirecting reason code
 *
 * \param q931 mISDN redirecting reason code.
 *
 * \return Asterisk redirecting reason code
 */
static enum AST_REDIRECTING_REASON misdn_to_ast_reason(const enum mISDN_REDIRECTING_REASON q931)
{
	enum AST_REDIRECTING_REASON ast;

	switch (q931) {
	default:
	case mISDN_REDIRECTING_REASON_UNKNOWN:
		ast = AST_REDIRECTING_REASON_UNKNOWN;
		break;

	case mISDN_REDIRECTING_REASON_CALL_FWD_BUSY:
		ast = AST_REDIRECTING_REASON_USER_BUSY;
		break;

	case mISDN_REDIRECTING_REASON_NO_REPLY:
		ast = AST_REDIRECTING_REASON_NO_ANSWER;
		break;

	case mISDN_REDIRECTING_REASON_DEFLECTION:
		ast = AST_REDIRECTING_REASON_DEFLECTION;
		break;

	case mISDN_REDIRECTING_REASON_OUT_OF_ORDER:
		ast = AST_REDIRECTING_REASON_OUT_OF_ORDER;
		break;

	case mISDN_REDIRECTING_REASON_CALL_FWD_DTE:
		ast = AST_REDIRECTING_REASON_CALL_FWD_DTE;
		break;

	case mISDN_REDIRECTING_REASON_CALL_FWD:
		ast = AST_REDIRECTING_REASON_UNCONDITIONAL;
		break;
	}

	return ast;
}



struct allowed_bearers {
	char *name;         /*!< Bearer capability name string used in /etc/misdn.conf allowed_bearers */
	char *display;      /*!< Bearer capability displayable name */
	int cap;            /*!< SETUP message bearer capability field code value */
	int deprecated;     /*!< TRUE if this entry is deprecated. (Misspelled or bad name to use) */
};

/* *INDENT-OFF* */
static const struct allowed_bearers allowed_bearers_array[] = {
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
	}

	return "Unknown Bearer";
}

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Fill in facility PartyNumber information
 *
 * \param party PartyNumber structure to fill in.
 * \param id Information to put in PartyNumber structure.
 *
 * \return Nothing
 */
static void misdn_PartyNumber_fill(struct FacPartyNumber *party, const struct misdn_party_id *id)
{
	ast_copy_string((char *) party->Number, id->number, sizeof(party->Number));
	party->LengthOfNumber = strlen((char *) party->Number);
	party->Type = misdn_to_PartyNumber_plan(id->number_plan);
	switch (party->Type) {
	case 1:/* public */
		party->TypeOfNumber = misdn_to_PartyNumber_ton_public(id->number_type);
		break;
	case 5:/* private */
		party->TypeOfNumber = misdn_to_PartyNumber_ton_private(id->number_type);
		break;
	default:
		party->TypeOfNumber = 0;/* Dont't care */
		break;
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Extract the information from PartyNumber
 *
 * \param id Where to put extracted PartyNumber information
 * \param party PartyNumber information to extract
 *
 * \return Nothing
 */
static void misdn_PartyNumber_extract(struct misdn_party_id *id, const struct FacPartyNumber *party)
{
	if (party->LengthOfNumber) {
		ast_copy_string(id->number, (char *) party->Number, sizeof(id->number));
		id->number_plan = PartyNumber_to_misdn_plan(party->Type);
		switch (party->Type) {
		case 1:/* public */
			id->number_type = PartyNumber_to_misdn_ton_public(party->TypeOfNumber);
			break;
		case 5:/* private */
			id->number_type = PartyNumber_to_misdn_ton_private(party->TypeOfNumber);
			break;
		default:
			id->number_type = NUMTYPE_UNKNOWN;
			break;
		}
	} else {
		/* Number not present */
		id->number_type = NUMTYPE_UNKNOWN;
		id->number_plan = NUMPLAN_ISDN;
		id->number[0] = 0;
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Fill in facility Address information
 *
 * \param Address Address structure to fill in.
 * \param id Information to put in Address structure.
 *
 * \return Nothing
 */
static void misdn_Address_fill(struct FacAddress *Address, const struct misdn_party_id *id)
{
	misdn_PartyNumber_fill(&Address->Party, id);

	/* Subaddresses are not supported yet */
	Address->Subaddress.Length = 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Fill in facility PresentedNumberUnscreened information
 *
 * \param presented PresentedNumberUnscreened structure to fill in.
 * \param id Information to put in PresentedNumberUnscreened structure.
 *
 * \return Nothing
 */
static void misdn_PresentedNumberUnscreened_fill(struct FacPresentedNumberUnscreened *presented, const struct misdn_party_id *id)
{
	presented->Type = misdn_to_PresentedNumberUnscreened_type(id->presentation, id->number[0] ? 1 : 0);
	misdn_PartyNumber_fill(&presented->Unscreened, id);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Extract the information from PartyNumber
 *
 * \param id Where to put extracted PresentedNumberUnscreened information
 * \param presented PresentedNumberUnscreened information to extract
 *
 * \return Nothing
 */
static void misdn_PresentedNumberUnscreened_extract(struct misdn_party_id *id, const struct FacPresentedNumberUnscreened *presented)
{
	id->presentation = PresentedNumberUnscreened_to_misdn_pres(presented->Type);
	id->screening = 0;/* unscreened */
	misdn_PartyNumber_extract(id, &presented->Unscreened);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static const char Level_Spacing[] = "          ";/* Work for up to 10 levels */
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_PartyNumber(unsigned Level, const struct FacPartyNumber *Party, const struct misdn_bchannel *bc)
{
	if (Party->LengthOfNumber) {
		const char *Spacing;

		Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
		chan_misdn_log(1, bc->port, " -->%s PartyNumber: Type:%d\n",
			Spacing, Party->Type);
		switch (Party->Type) {
		case 0: /* Unknown PartyNumber */
			chan_misdn_log(1, bc->port, " -->%s  Unknown: %s\n",
				Spacing, Party->Number);
			break;
		case 1: /* Public PartyNumber */
			chan_misdn_log(1, bc->port, " -->%s  Public TON:%d %s\n",
				Spacing, Party->TypeOfNumber, Party->Number);
			break;
		case 2: /* NSAP encoded PartyNumber */
			chan_misdn_log(1, bc->port, " -->%s  NSAP: %s\n",
				Spacing, Party->Number);
			break;
		case 3: /* Data PartyNumber (Not used) */
			chan_misdn_log(1, bc->port, " -->%s  Data: %s\n",
				Spacing, Party->Number);
			break;
		case 4: /* Telex PartyNumber (Not used) */
			chan_misdn_log(1, bc->port, " -->%s  Telex: %s\n",
				Spacing, Party->Number);
			break;
		case 5: /* Private PartyNumber */
			chan_misdn_log(1, bc->port, " -->%s  Private TON:%d %s\n",
				Spacing, Party->TypeOfNumber, Party->Number);
			break;
		case 8: /* National Standard PartyNumber (Not used) */
			chan_misdn_log(1, bc->port, " -->%s  National: %s\n",
				Spacing, Party->Number);
			break;
		default:
			break;
		}
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_Subaddress(unsigned Level, const struct FacPartySubaddress *Subaddress, const struct misdn_bchannel *bc)
{
	if (Subaddress->Length) {
		const char *Spacing;

		Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
		chan_misdn_log(1, bc->port, " -->%s Subaddress: Type:%d\n",
			Spacing, Subaddress->Type);
		switch (Subaddress->Type) {
		case 0: /* UserSpecified */
			if (Subaddress->u.UserSpecified.OddCountPresent) {
				chan_misdn_log(1, bc->port, " -->%s  User BCD OddCount:%d NumOctets:%d\n",
					Spacing, Subaddress->u.UserSpecified.OddCount, Subaddress->Length);
			} else {
				chan_misdn_log(1, bc->port, " -->%s  User: %s\n",
					Spacing, Subaddress->u.UserSpecified.Information);
			}
			break;
		case 1: /* NSAP */
			chan_misdn_log(1, bc->port, " -->%s  NSAP: %s\n",
				Spacing, Subaddress->u.Nsap);
			break;
		default:
			break;
		}
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_Address(unsigned Level, const struct FacAddress *Address, const struct misdn_bchannel *bc)
{
	print_facility_PartyNumber(Level, &Address->Party, bc);
	print_facility_Subaddress(Level, &Address->Subaddress, bc);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_PresentedNumberUnscreened(unsigned Level, const struct FacPresentedNumberUnscreened *Presented, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	chan_misdn_log(1, bc->port, " -->%s Unscreened Type:%d\n", Spacing, Presented->Type);
	switch (Presented->Type) {
	case 0: /* presentationAllowedNumber */
		chan_misdn_log(1, bc->port, " -->%s  Allowed:\n", Spacing);
		print_facility_PartyNumber(Level + 2, &Presented->Unscreened, bc);
		break;
	case 1: /* presentationRestricted */
		chan_misdn_log(1, bc->port, " -->%s  Restricted\n", Spacing);
		break;
	case 2: /* numberNotAvailableDueToInterworking */
		chan_misdn_log(1, bc->port, " -->%s  Not Available\n", Spacing);
		break;
	case 3: /* presentationRestrictedNumber */
		chan_misdn_log(1, bc->port, " -->%s  Restricted:\n", Spacing);
		print_facility_PartyNumber(Level + 2, &Presented->Unscreened, bc);
		break;
	default:
		break;
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_AddressScreened(unsigned Level, const struct FacAddressScreened *Address, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	chan_misdn_log(1, bc->port, " -->%s ScreeningIndicator:%d\n", Spacing, Address->ScreeningIndicator);
	print_facility_PartyNumber(Level, &Address->Party, bc);
	print_facility_Subaddress(Level, &Address->Subaddress, bc);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_PresentedAddressScreened(unsigned Level, const struct FacPresentedAddressScreened *Presented, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	chan_misdn_log(1, bc->port, " -->%s Screened Type:%d\n", Spacing, Presented->Type);
	switch (Presented->Type) {
	case 0: /* presentationAllowedAddress */
		chan_misdn_log(1, bc->port, " -->%s  Allowed:\n", Spacing);
		print_facility_AddressScreened(Level + 2, &Presented->Address, bc);
		break;
	case 1: /* presentationRestricted */
		chan_misdn_log(1, bc->port, " -->%s  Restricted\n", Spacing);
		break;
	case 2: /* numberNotAvailableDueToInterworking */
		chan_misdn_log(1, bc->port, " -->%s  Not Available\n", Spacing);
		break;
	case 3: /* presentationRestrictedAddress */
		chan_misdn_log(1, bc->port, " -->%s  Restricted:\n", Spacing);
		print_facility_AddressScreened(Level + 2, &Presented->Address, bc);
		break;
	default:
		break;
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_Q931_Bc_Hlc_Llc(unsigned Level, const struct Q931_Bc_Hlc_Llc *Q931ie, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	chan_misdn_log(1, bc->port, " -->%s Q931ie:\n", Spacing);
	if (Q931ie->Bc.Length) {
		chan_misdn_log(1, bc->port, " -->%s  Bc Len:%d\n", Spacing, Q931ie->Bc.Length);
	}
	if (Q931ie->Hlc.Length) {
		chan_misdn_log(1, bc->port, " -->%s  Hlc Len:%d\n", Spacing, Q931ie->Hlc.Length);
	}
	if (Q931ie->Llc.Length) {
		chan_misdn_log(1, bc->port, " -->%s  Llc Len:%d\n", Spacing, Q931ie->Llc.Length);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_Q931_Bc_Hlc_Llc_Uu(unsigned Level, const struct Q931_Bc_Hlc_Llc_Uu *Q931ie, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	chan_misdn_log(1, bc->port, " -->%s Q931ie:\n", Spacing);
	if (Q931ie->Bc.Length) {
		chan_misdn_log(1, bc->port, " -->%s  Bc Len:%d\n", Spacing, Q931ie->Bc.Length);
	}
	if (Q931ie->Hlc.Length) {
		chan_misdn_log(1, bc->port, " -->%s  Hlc Len:%d\n", Spacing, Q931ie->Hlc.Length);
	}
	if (Q931ie->Llc.Length) {
		chan_misdn_log(1, bc->port, " -->%s  Llc Len:%d\n", Spacing, Q931ie->Llc.Length);
	}
	if (Q931ie->UserInfo.Length) {
		chan_misdn_log(1, bc->port, " -->%s  UserInfo Len:%d\n", Spacing, Q931ie->UserInfo.Length);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_CallInformation(unsigned Level, const struct FacCallInformation *CallInfo, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	chan_misdn_log(1, bc->port, " -->%s CCBSReference:%d\n",
		Spacing, CallInfo->CCBSReference);
	chan_misdn_log(1, bc->port, " -->%s AddressOfB:\n", Spacing);
	print_facility_Address(Level + 1, &CallInfo->AddressOfB, bc);
	print_facility_Q931_Bc_Hlc_Llc(Level, &CallInfo->Q931ie, bc);
	if (CallInfo->SubaddressOfA.Length) {
		chan_misdn_log(1, bc->port, " -->%s SubaddressOfA:\n", Spacing);
		print_facility_Subaddress(Level + 1, &CallInfo->SubaddressOfA, bc);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_ServedUserNr(unsigned Level, const struct FacPartyNumber *Party, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	if (Party->LengthOfNumber) {
		print_facility_PartyNumber(Level, Party, bc);
	} else {
		chan_misdn_log(1, bc->port, " -->%s All Numbers\n", Spacing);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void print_facility_IntResult(unsigned Level, const struct FacForwardingRecord *ForwardingRecord, const struct misdn_bchannel *bc)
{
	const char *Spacing;

	Spacing = &Level_Spacing[sizeof(Level_Spacing) - 1 - Level];
	chan_misdn_log(1, bc->port, " -->%s Procedure:%d BasicService:%d\n",
		Spacing,
		ForwardingRecord->Procedure,
		ForwardingRecord->BasicService);
	chan_misdn_log(1, bc->port, " -->%s ForwardedTo:\n", Spacing);
	print_facility_Address(Level + 1, &ForwardingRecord->ForwardedTo, bc);
	chan_misdn_log(1, bc->port, " -->%s ServedUserNr:\n", Spacing);
	print_facility_ServedUserNr(Level + 1, &ForwardingRecord->ServedUser, bc);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

static void print_facility(const struct FacParm *fac, const const struct misdn_bchannel *bc)
{
#if defined(AST_MISDN_ENHANCEMENTS)
	unsigned Index;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	switch (fac->Function) {
#if defined(AST_MISDN_ENHANCEMENTS)
	case Fac_ActivationDiversion:
		chan_misdn_log(1, bc->port, " --> ActivationDiversion: InvokeID:%d\n",
			fac->u.ActivationDiversion.InvokeID);
		switch (fac->u.ActivationDiversion.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: Procedure:%d BasicService:%d\n",
				fac->u.ActivationDiversion.Component.Invoke.Procedure,
				fac->u.ActivationDiversion.Component.Invoke.BasicService);
			chan_misdn_log(1, bc->port, " -->   ForwardedTo:\n");
			print_facility_Address(3, &fac->u.ActivationDiversion.Component.Invoke.ForwardedTo, bc);
			chan_misdn_log(1, bc->port, " -->   ServedUserNr:\n");
			print_facility_ServedUserNr(3, &fac->u.ActivationDiversion.Component.Invoke.ServedUser, bc);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result\n");
			break;
		default:
			break;
		}
		break;
	case Fac_DeactivationDiversion:
		chan_misdn_log(1, bc->port, " --> DeactivationDiversion: InvokeID:%d\n",
			fac->u.DeactivationDiversion.InvokeID);
		switch (fac->u.DeactivationDiversion.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: Procedure:%d BasicService:%d\n",
				fac->u.DeactivationDiversion.Component.Invoke.Procedure,
				fac->u.DeactivationDiversion.Component.Invoke.BasicService);
			chan_misdn_log(1, bc->port, " -->   ServedUserNr:\n");
			print_facility_ServedUserNr(3, &fac->u.DeactivationDiversion.Component.Invoke.ServedUser, bc);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result\n");
			break;
		default:
			break;
		}
		break;
	case Fac_ActivationStatusNotificationDiv:
		chan_misdn_log(1, bc->port, " --> ActivationStatusNotificationDiv: InvokeID:%d Procedure:%d BasicService:%d\n",
			fac->u.ActivationStatusNotificationDiv.InvokeID,
			fac->u.ActivationStatusNotificationDiv.Procedure,
			fac->u.ActivationStatusNotificationDiv.BasicService);
		chan_misdn_log(1, bc->port, " -->  ForwardedTo:\n");
		print_facility_Address(2, &fac->u.ActivationStatusNotificationDiv.ForwardedTo, bc);
		chan_misdn_log(1, bc->port, " -->  ServedUserNr:\n");
		print_facility_ServedUserNr(2, &fac->u.ActivationStatusNotificationDiv.ServedUser, bc);
		break;
	case Fac_DeactivationStatusNotificationDiv:
		chan_misdn_log(1, bc->port, " --> DeactivationStatusNotificationDiv: InvokeID:%d Procedure:%d BasicService:%d\n",
			fac->u.DeactivationStatusNotificationDiv.InvokeID,
			fac->u.DeactivationStatusNotificationDiv.Procedure,
			fac->u.DeactivationStatusNotificationDiv.BasicService);
		chan_misdn_log(1, bc->port, " -->  ServedUserNr:\n");
		print_facility_ServedUserNr(2, &fac->u.DeactivationStatusNotificationDiv.ServedUser, bc);
		break;
	case Fac_InterrogationDiversion:
		chan_misdn_log(1, bc->port, " --> InterrogationDiversion: InvokeID:%d\n",
			fac->u.InterrogationDiversion.InvokeID);
		switch (fac->u.InterrogationDiversion.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: Procedure:%d BasicService:%d\n",
				fac->u.InterrogationDiversion.Component.Invoke.Procedure,
				fac->u.InterrogationDiversion.Component.Invoke.BasicService);
			chan_misdn_log(1, bc->port, " -->   ServedUserNr:\n");
			print_facility_ServedUserNr(3, &fac->u.InterrogationDiversion.Component.Invoke.ServedUser, bc);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result:\n");
			if (fac->u.InterrogationDiversion.Component.Result.NumRecords) {
				for (Index = 0; Index < fac->u.InterrogationDiversion.Component.Result.NumRecords; ++Index) {
					chan_misdn_log(1, bc->port, " -->   IntResult[%d]:\n", Index);
					print_facility_IntResult(3, &fac->u.InterrogationDiversion.Component.Result.List[Index], bc);
				}
			}
			break;
		default:
			break;
		}
		break;
	case Fac_DiversionInformation:
		chan_misdn_log(1, bc->port, " --> DiversionInformation: InvokeID:%d Reason:%d BasicService:%d\n",
			fac->u.DiversionInformation.InvokeID,
			fac->u.DiversionInformation.DiversionReason,
			fac->u.DiversionInformation.BasicService);
		if (fac->u.DiversionInformation.ServedUserSubaddress.Length) {
			chan_misdn_log(1, bc->port, " -->  ServedUserSubaddress:\n");
			print_facility_Subaddress(2, &fac->u.DiversionInformation.ServedUserSubaddress, bc);
		}
		if (fac->u.DiversionInformation.CallingAddressPresent) {
			chan_misdn_log(1, bc->port, " -->  CallingAddress:\n");
			print_facility_PresentedAddressScreened(2, &fac->u.DiversionInformation.CallingAddress, bc);
		}
		if (fac->u.DiversionInformation.OriginalCalledPresent) {
			chan_misdn_log(1, bc->port, " -->  OriginalCalledNr:\n");
			print_facility_PresentedNumberUnscreened(2, &fac->u.DiversionInformation.OriginalCalled, bc);
		}
		if (fac->u.DiversionInformation.LastDivertingPresent) {
			chan_misdn_log(1, bc->port, " -->  LastDivertingNr:\n");
			print_facility_PresentedNumberUnscreened(2, &fac->u.DiversionInformation.LastDiverting, bc);
		}
		if (fac->u.DiversionInformation.LastDivertingReasonPresent) {
			chan_misdn_log(1, bc->port, " -->  LastDivertingReason:%d\n", fac->u.DiversionInformation.LastDivertingReason);
		}
		if (fac->u.DiversionInformation.UserInfo.Length) {
			chan_misdn_log(1, bc->port, " -->  UserInfo Length:%d\n", fac->u.DiversionInformation.UserInfo.Length);
		}
		break;
	case Fac_CallDeflection:
		chan_misdn_log(1, bc->port, " --> CallDeflection: InvokeID:%d\n",
			fac->u.CallDeflection.InvokeID);
		switch (fac->u.CallDeflection.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke:\n");
			if (fac->u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUserPresent) {
				chan_misdn_log(1, bc->port, " -->   PresentationAllowed:%d\n",
					fac->u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUser);
			}
			chan_misdn_log(1, bc->port, " -->   DeflectionAddress:\n");
			print_facility_Address(3, &fac->u.CallDeflection.Component.Invoke.Deflection, bc);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result\n");
			break;
		default:
			break;
		}
		break;
	case Fac_CallRerouteing:
		chan_misdn_log(1, bc->port, " --> CallRerouteing: InvokeID:%d\n",
			fac->u.CallRerouteing.InvokeID);
		switch (fac->u.CallRerouteing.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: Reason:%d Counter:%d\n",
				fac->u.CallRerouteing.Component.Invoke.ReroutingReason,
				fac->u.CallRerouteing.Component.Invoke.ReroutingCounter);
			chan_misdn_log(1, bc->port, " -->   CalledAddress:\n");
			print_facility_Address(3, &fac->u.CallRerouteing.Component.Invoke.CalledAddress, bc);
			print_facility_Q931_Bc_Hlc_Llc_Uu(2, &fac->u.CallRerouteing.Component.Invoke.Q931ie, bc);
			chan_misdn_log(1, bc->port, " -->   LastReroutingNr:\n");
			print_facility_PresentedNumberUnscreened(3, &fac->u.CallRerouteing.Component.Invoke.LastRerouting, bc);
			chan_misdn_log(1, bc->port, " -->   SubscriptionOption:%d\n",
				fac->u.CallRerouteing.Component.Invoke.SubscriptionOption);
			if (fac->u.CallRerouteing.Component.Invoke.CallingPartySubaddress.Length) {
				chan_misdn_log(1, bc->port, " -->   CallingParty:\n");
				print_facility_Subaddress(3, &fac->u.CallRerouteing.Component.Invoke.CallingPartySubaddress, bc);
			}
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result\n");
			break;
		default:
			break;
		}
		break;
	case Fac_InterrogateServedUserNumbers:
		chan_misdn_log(1, bc->port, " --> InterrogateServedUserNumbers: InvokeID:%d\n",
			fac->u.InterrogateServedUserNumbers.InvokeID);
		switch (fac->u.InterrogateServedUserNumbers.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke\n");
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result:\n");
			if (fac->u.InterrogateServedUserNumbers.Component.Result.NumRecords) {
				for (Index = 0; Index < fac->u.InterrogateServedUserNumbers.Component.Result.NumRecords; ++Index) {
					chan_misdn_log(1, bc->port, " -->   ServedUserNr[%d]:\n", Index);
					print_facility_PartyNumber(3, &fac->u.InterrogateServedUserNumbers.Component.Result.List[Index], bc);
				}
			}
			break;
		default:
			break;
		}
		break;
	case Fac_DivertingLegInformation1:
		chan_misdn_log(1, bc->port, " --> DivertingLegInformation1: InvokeID:%d Reason:%d SubscriptionOption:%d\n",
			fac->u.DivertingLegInformation1.InvokeID,
			fac->u.DivertingLegInformation1.DiversionReason,
			fac->u.DivertingLegInformation1.SubscriptionOption);
		if (fac->u.DivertingLegInformation1.DivertedToPresent) {
			chan_misdn_log(1, bc->port, " -->  DivertedToNr:\n");
			print_facility_PresentedNumberUnscreened(2, &fac->u.DivertingLegInformation1.DivertedTo, bc);
		}
		break;
	case Fac_DivertingLegInformation2:
		chan_misdn_log(1, bc->port, " --> DivertingLegInformation2: InvokeID:%d Reason:%d Count:%d\n",
			fac->u.DivertingLegInformation2.InvokeID,
			fac->u.DivertingLegInformation2.DiversionReason,
			fac->u.DivertingLegInformation2.DiversionCounter);
		if (fac->u.DivertingLegInformation2.DivertingPresent) {
			chan_misdn_log(1, bc->port, " -->  DivertingNr:\n");
			print_facility_PresentedNumberUnscreened(2, &fac->u.DivertingLegInformation2.Diverting, bc);
		}
		if (fac->u.DivertingLegInformation2.OriginalCalledPresent) {
			chan_misdn_log(1, bc->port, " -->  OriginalCalledNr:\n");
			print_facility_PresentedNumberUnscreened(2, &fac->u.DivertingLegInformation2.OriginalCalled, bc);
		}
		break;
	case Fac_DivertingLegInformation3:
		chan_misdn_log(1, bc->port, " --> DivertingLegInformation3: InvokeID:%d PresentationAllowed:%d\n",
			fac->u.DivertingLegInformation3.InvokeID,
			fac->u.DivertingLegInformation3.PresentationAllowedIndicator);
		break;

#else	/* !defined(AST_MISDN_ENHANCEMENTS) */

	case Fac_CD:
		chan_misdn_log(1, bc->port, " --> calldeflect to: %s, presentable: %s\n", fac->u.CDeflection.DeflectedToNumber,
			fac->u.CDeflection.PresentationAllowed ? "yes" : "no");
		break;
#endif	/* !defined(AST_MISDN_ENHANCEMENTS) */
	case Fac_AOCDCurrency:
		if (fac->u.AOCDcur.chargeNotAvailable) {
			chan_misdn_log(1, bc->port, " --> AOCD currency: charge not available\n");
		} else if (fac->u.AOCDcur.freeOfCharge) {
			chan_misdn_log(1, bc->port, " --> AOCD currency: free of charge\n");
		} else if (fac->u.AOCDchu.billingId >= 0) {
			chan_misdn_log(1, bc->port, " --> AOCD currency: currency:%s amount:%d multiplier:%d typeOfChargingInfo:%s billingId:%d\n",
				fac->u.AOCDcur.currency, fac->u.AOCDcur.currencyAmount, fac->u.AOCDcur.multiplier,
				(fac->u.AOCDcur.typeOfChargingInfo == 0) ? "subTotal" : "total", fac->u.AOCDcur.billingId);
		} else {
			chan_misdn_log(1, bc->port, " --> AOCD currency: currency:%s amount:%d multiplier:%d typeOfChargingInfo:%s\n",
				fac->u.AOCDcur.currency, fac->u.AOCDcur.currencyAmount, fac->u.AOCDcur.multiplier,
				(fac->u.AOCDcur.typeOfChargingInfo == 0) ? "subTotal" : "total");
		}
		break;
	case Fac_AOCDChargingUnit:
		if (fac->u.AOCDchu.chargeNotAvailable) {
			chan_misdn_log(1, bc->port, " --> AOCD charging unit: charge not available\n");
		} else if (fac->u.AOCDchu.freeOfCharge) {
			chan_misdn_log(1, bc->port, " --> AOCD charging unit: free of charge\n");
		} else if (fac->u.AOCDchu.billingId >= 0) {
			chan_misdn_log(1, bc->port, " --> AOCD charging unit: recordedUnits:%d typeOfChargingInfo:%s billingId:%d\n",
				fac->u.AOCDchu.recordedUnits, (fac->u.AOCDchu.typeOfChargingInfo == 0) ? "subTotal" : "total", fac->u.AOCDchu.billingId);
		} else {
			chan_misdn_log(1, bc->port, " --> AOCD charging unit: recordedUnits:%d typeOfChargingInfo:%s\n",
				fac->u.AOCDchu.recordedUnits, (fac->u.AOCDchu.typeOfChargingInfo == 0) ? "subTotal" : "total");
		}
		break;
#if defined(AST_MISDN_ENHANCEMENTS)
	case Fac_ERROR:
		chan_misdn_log(1, bc->port, " --> ERROR: InvokeID:%d, Code:0x%02x\n",
			fac->u.ERROR.invokeId, fac->u.ERROR.errorValue);
		break;
	case Fac_RESULT:
		chan_misdn_log(1, bc->port, " --> RESULT: InvokeID:%d\n",
			fac->u.RESULT.InvokeID);
		break;
	case Fac_REJECT:
		if (fac->u.REJECT.InvokeIDPresent) {
			chan_misdn_log(1, bc->port, " --> REJECT: InvokeID:%d, Code:0x%02x\n",
				fac->u.REJECT.InvokeID, fac->u.REJECT.Code);
		} else {
			chan_misdn_log(1, bc->port, " --> REJECT: Code:0x%02x\n",
				fac->u.REJECT.Code);
		}
		break;
	case Fac_EctExecute:
		chan_misdn_log(1, bc->port, " --> EctExecute: InvokeID:%d\n",
			fac->u.EctExecute.InvokeID);
		break;
	case Fac_ExplicitEctExecute:
		chan_misdn_log(1, bc->port, " --> ExplicitEctExecute: InvokeID:%d LinkID:%d\n",
			fac->u.ExplicitEctExecute.InvokeID,
			fac->u.ExplicitEctExecute.LinkID);
		break;
	case Fac_RequestSubaddress:
		chan_misdn_log(1, bc->port, " --> RequestSubaddress: InvokeID:%d\n",
			fac->u.RequestSubaddress.InvokeID);
		break;
	case Fac_SubaddressTransfer:
		chan_misdn_log(1, bc->port, " --> SubaddressTransfer: InvokeID:%d\n",
			fac->u.SubaddressTransfer.InvokeID);
		print_facility_Subaddress(1, &fac->u.SubaddressTransfer.Subaddress, bc);
		break;
	case Fac_EctLinkIdRequest:
		chan_misdn_log(1, bc->port, " --> EctLinkIdRequest: InvokeID:%d\n",
			fac->u.EctLinkIdRequest.InvokeID);
		switch (fac->u.EctLinkIdRequest.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke\n");
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: LinkID:%d\n",
				fac->u.EctLinkIdRequest.Component.Result.LinkID);
			break;
		default:
			break;
		}
		break;
	case Fac_EctInform:
		chan_misdn_log(1, bc->port, " --> EctInform: InvokeID:%d Status:%d\n",
			fac->u.EctInform.InvokeID,
			fac->u.EctInform.Status);
		if (fac->u.EctInform.RedirectionPresent) {
			chan_misdn_log(1, bc->port, " -->  Redirection Number\n");
			print_facility_PresentedNumberUnscreened(2, &fac->u.EctInform.Redirection, bc);
		}
		break;
	case Fac_EctLoopTest:
		chan_misdn_log(1, bc->port, " --> EctLoopTest: InvokeID:%d\n",
			fac->u.EctLoopTest.InvokeID);
		switch (fac->u.EctLoopTest.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: CallTransferID:%d\n",
				fac->u.EctLoopTest.Component.Invoke.CallTransferID);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: LoopResult:%d\n",
				fac->u.EctLoopTest.Component.Result.LoopResult);
			break;
		default:
			break;
		}
		break;
	case Fac_StatusRequest:
		chan_misdn_log(1, bc->port, " --> StatusRequest: InvokeID:%d\n",
			fac->u.StatusRequest.InvokeID);
		switch (fac->u.StatusRequest.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: Compatibility:%d\n",
				fac->u.StatusRequest.Component.Invoke.CompatibilityMode);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: Status:%d\n",
				fac->u.StatusRequest.Component.Result.Status);
			break;
		default:
			break;
		}
		break;
	case Fac_CallInfoRetain:
		chan_misdn_log(1, bc->port, " --> CallInfoRetain: InvokeID:%d, LinkageID:%d\n",
			fac->u.CallInfoRetain.InvokeID, fac->u.CallInfoRetain.CallLinkageID);
		break;
	case Fac_CCBSDeactivate:
		chan_misdn_log(1, bc->port, " --> CCBSDeactivate: InvokeID:%d\n",
			fac->u.CCBSDeactivate.InvokeID);
		switch (fac->u.CCBSDeactivate.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: CCBSReference:%d\n",
				fac->u.CCBSDeactivate.Component.Invoke.CCBSReference);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result\n");
			break;
		default:
			break;
		}
		break;
	case Fac_CCBSErase:
		chan_misdn_log(1, bc->port, " --> CCBSErase: InvokeID:%d, CCBSReference:%d RecallMode:%d, Reason:%d\n",
			fac->u.CCBSErase.InvokeID, fac->u.CCBSErase.CCBSReference,
			fac->u.CCBSErase.RecallMode, fac->u.CCBSErase.Reason);
		chan_misdn_log(1, bc->port, " -->  AddressOfB\n");
		print_facility_Address(2, &fac->u.CCBSErase.AddressOfB, bc);
		print_facility_Q931_Bc_Hlc_Llc(1, &fac->u.CCBSErase.Q931ie, bc);
		break;
	case Fac_CCBSRemoteUserFree:
		chan_misdn_log(1, bc->port, " --> CCBSRemoteUserFree: InvokeID:%d, CCBSReference:%d RecallMode:%d\n",
			fac->u.CCBSRemoteUserFree.InvokeID, fac->u.CCBSRemoteUserFree.CCBSReference,
			fac->u.CCBSRemoteUserFree.RecallMode);
		chan_misdn_log(1, bc->port, " -->  AddressOfB\n");
		print_facility_Address(2, &fac->u.CCBSRemoteUserFree.AddressOfB, bc);
		print_facility_Q931_Bc_Hlc_Llc(1, &fac->u.CCBSRemoteUserFree.Q931ie, bc);
		break;
	case Fac_CCBSCall:
		chan_misdn_log(1, bc->port, " --> CCBSCall: InvokeID:%d, CCBSReference:%d\n",
			fac->u.CCBSCall.InvokeID, fac->u.CCBSCall.CCBSReference);
		break;
	case Fac_CCBSStatusRequest:
		chan_misdn_log(1, bc->port, " --> CCBSStatusRequest: InvokeID:%d\n",
			fac->u.CCBSStatusRequest.InvokeID);
		switch (fac->u.CCBSStatusRequest.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: CCBSReference:%d RecallMode:%d\n",
				fac->u.CCBSStatusRequest.Component.Invoke.CCBSReference,
				fac->u.CCBSStatusRequest.Component.Invoke.RecallMode);
			print_facility_Q931_Bc_Hlc_Llc(2, &fac->u.CCBSStatusRequest.Component.Invoke.Q931ie, bc);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: Free:%d\n",
				fac->u.CCBSStatusRequest.Component.Result.Free);
			break;
		default:
			break;
		}
		break;
	case Fac_CCBSBFree:
		chan_misdn_log(1, bc->port, " --> CCBSBFree: InvokeID:%d, CCBSReference:%d RecallMode:%d\n",
			fac->u.CCBSBFree.InvokeID, fac->u.CCBSBFree.CCBSReference,
			fac->u.CCBSBFree.RecallMode);
		chan_misdn_log(1, bc->port, " -->  AddressOfB\n");
		print_facility_Address(2, &fac->u.CCBSBFree.AddressOfB, bc);
		print_facility_Q931_Bc_Hlc_Llc(1, &fac->u.CCBSBFree.Q931ie, bc);
		break;
	case Fac_EraseCallLinkageID:
		chan_misdn_log(1, bc->port, " --> EraseCallLinkageID: InvokeID:%d, LinkageID:%d\n",
			fac->u.EraseCallLinkageID.InvokeID, fac->u.EraseCallLinkageID.CallLinkageID);
		break;
	case Fac_CCBSStopAlerting:
		chan_misdn_log(1, bc->port, " --> CCBSStopAlerting: InvokeID:%d, CCBSReference:%d\n",
			fac->u.CCBSStopAlerting.InvokeID, fac->u.CCBSStopAlerting.CCBSReference);
		break;
	case Fac_CCBSRequest:
		chan_misdn_log(1, bc->port, " --> CCBSRequest: InvokeID:%d\n",
			fac->u.CCBSRequest.InvokeID);
		switch (fac->u.CCBSRequest.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: LinkageID:%d\n",
				fac->u.CCBSRequest.Component.Invoke.CallLinkageID);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: CCBSReference:%d RecallMode:%d\n",
				fac->u.CCBSRequest.Component.Result.CCBSReference,
				fac->u.CCBSRequest.Component.Result.RecallMode);
			break;
		default:
			break;
		}
		break;
	case Fac_CCBSInterrogate:
		chan_misdn_log(1, bc->port, " --> CCBSInterrogate: InvokeID:%d\n",
			fac->u.CCBSInterrogate.InvokeID);
		switch (fac->u.CCBSInterrogate.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke\n");
			if (fac->u.CCBSInterrogate.Component.Invoke.CCBSReferencePresent) {
				chan_misdn_log(1, bc->port, " -->   CCBSReference:%d\n",
					fac->u.CCBSInterrogate.Component.Invoke.CCBSReference);
			}
			if (fac->u.CCBSInterrogate.Component.Invoke.AParty.LengthOfNumber) {
				chan_misdn_log(1, bc->port, " -->   AParty\n");
				print_facility_PartyNumber(3, &fac->u.CCBSInterrogate.Component.Invoke.AParty, bc);
			}
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: RecallMode:%d\n",
				fac->u.CCBSInterrogate.Component.Result.RecallMode);
			if (fac->u.CCBSInterrogate.Component.Result.NumRecords) {
				for (Index = 0; Index < fac->u.CCBSInterrogate.Component.Result.NumRecords; ++Index) {
					chan_misdn_log(1, bc->port, " -->   CallDetails[%d]:\n", Index);
					print_facility_CallInformation(3, &fac->u.CCBSInterrogate.Component.Result.CallDetails[Index], bc);
				}
			}
			break;
		default:
			break;
		}
		break;
	case Fac_CCNRRequest:
		chan_misdn_log(1, bc->port, " --> CCNRRequest: InvokeID:%d\n",
			fac->u.CCNRRequest.InvokeID);
		switch (fac->u.CCNRRequest.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke: LinkageID:%d\n",
				fac->u.CCNRRequest.Component.Invoke.CallLinkageID);
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: CCBSReference:%d RecallMode:%d\n",
				fac->u.CCNRRequest.Component.Result.CCBSReference,
				fac->u.CCNRRequest.Component.Result.RecallMode);
			break;
		default:
			break;
		}
		break;
	case Fac_CCNRInterrogate:
		chan_misdn_log(1, bc->port, " --> CCNRInterrogate: InvokeID:%d\n",
			fac->u.CCNRInterrogate.InvokeID);
		switch (fac->u.CCNRInterrogate.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke\n");
			if (fac->u.CCNRInterrogate.Component.Invoke.CCBSReferencePresent) {
				chan_misdn_log(1, bc->port, " -->   CCBSReference:%d\n",
					fac->u.CCNRInterrogate.Component.Invoke.CCBSReference);
			}
			if (fac->u.CCNRInterrogate.Component.Invoke.AParty.LengthOfNumber) {
				chan_misdn_log(1, bc->port, " -->   AParty\n");
				print_facility_PartyNumber(3, &fac->u.CCNRInterrogate.Component.Invoke.AParty, bc);
			}
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: RecallMode:%d\n",
				fac->u.CCNRInterrogate.Component.Result.RecallMode);
			if (fac->u.CCNRInterrogate.Component.Result.NumRecords) {
				for (Index = 0; Index < fac->u.CCNRInterrogate.Component.Result.NumRecords; ++Index) {
					chan_misdn_log(1, bc->port, " -->   CallDetails[%d]:\n", Index);
					print_facility_CallInformation(3, &fac->u.CCNRInterrogate.Component.Result.CallDetails[Index], bc);
				}
			}
			break;
		default:
			break;
		}
		break;
	case Fac_CCBS_T_Call:
		chan_misdn_log(1, bc->port, " --> CCBS_T_Call: InvokeID:%d\n",
			fac->u.CCBS_T_Call.InvokeID);
		break;
	case Fac_CCBS_T_Suspend:
		chan_misdn_log(1, bc->port, " --> CCBS_T_Suspend: InvokeID:%d\n",
			fac->u.CCBS_T_Suspend.InvokeID);
		break;
	case Fac_CCBS_T_Resume:
		chan_misdn_log(1, bc->port, " --> CCBS_T_Resume: InvokeID:%d\n",
			fac->u.CCBS_T_Resume.InvokeID);
		break;
	case Fac_CCBS_T_RemoteUserFree:
		chan_misdn_log(1, bc->port, " --> CCBS_T_RemoteUserFree: InvokeID:%d\n",
			fac->u.CCBS_T_RemoteUserFree.InvokeID);
		break;
	case Fac_CCBS_T_Available:
		chan_misdn_log(1, bc->port, " --> CCBS_T_Available: InvokeID:%d\n",
			fac->u.CCBS_T_Available.InvokeID);
		break;
	case Fac_CCBS_T_Request:
		chan_misdn_log(1, bc->port, " --> CCBS_T_Request: InvokeID:%d\n",
			fac->u.CCBS_T_Request.InvokeID);
		switch (fac->u.CCBS_T_Request.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke\n");
			chan_misdn_log(1, bc->port, " -->   DestinationAddress:\n");
			print_facility_Address(3, &fac->u.CCBS_T_Request.Component.Invoke.Destination, bc);
			print_facility_Q931_Bc_Hlc_Llc(2, &fac->u.CCBS_T_Request.Component.Invoke.Q931ie, bc);
			if (fac->u.CCBS_T_Request.Component.Invoke.RetentionSupported) {
				chan_misdn_log(1, bc->port, " -->   RetentionSupported:1\n");
			}
			if (fac->u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicatorPresent) {
				chan_misdn_log(1, bc->port, " -->   PresentationAllowed:%d\n",
					fac->u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicator);
			}
			if (fac->u.CCBS_T_Request.Component.Invoke.Originating.Party.LengthOfNumber) {
				chan_misdn_log(1, bc->port, " -->   OriginatingAddress:\n");
				print_facility_Address(3, &fac->u.CCBS_T_Request.Component.Invoke.Originating, bc);
			}
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: RetentionSupported:%d\n",
				fac->u.CCBS_T_Request.Component.Result.RetentionSupported);
			break;
		default:
			break;
		}
		break;
	case Fac_CCNR_T_Request:
		chan_misdn_log(1, bc->port, " --> CCNR_T_Request: InvokeID:%d\n",
			fac->u.CCNR_T_Request.InvokeID);
		switch (fac->u.CCNR_T_Request.ComponentType) {
		case FacComponent_Invoke:
			chan_misdn_log(1, bc->port, " -->  Invoke\n");
			chan_misdn_log(1, bc->port, " -->   DestinationAddress:\n");
			print_facility_Address(3, &fac->u.CCNR_T_Request.Component.Invoke.Destination, bc);
			print_facility_Q931_Bc_Hlc_Llc(2, &fac->u.CCNR_T_Request.Component.Invoke.Q931ie, bc);
			if (fac->u.CCNR_T_Request.Component.Invoke.RetentionSupported) {
				chan_misdn_log(1, bc->port, " -->   RetentionSupported:1\n");
			}
			if (fac->u.CCNR_T_Request.Component.Invoke.PresentationAllowedIndicatorPresent) {
				chan_misdn_log(1, bc->port, " -->   PresentationAllowed:%d\n",
					fac->u.CCNR_T_Request.Component.Invoke.PresentationAllowedIndicator);
			}
			if (fac->u.CCNR_T_Request.Component.Invoke.Originating.Party.LengthOfNumber) {
				chan_misdn_log(1, bc->port, " -->   OriginatingAddress:\n");
				print_facility_Address(3, &fac->u.CCNR_T_Request.Component.Invoke.Originating, bc);
			}
			break;
		case FacComponent_Result:
			chan_misdn_log(1, bc->port, " -->  Result: RetentionSupported:%d\n",
				fac->u.CCNR_T_Request.Component.Result.RetentionSupported);
			break;
		default:
			break;
		}
		break;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	case Fac_None:
		/* No facility so print nothing */
		break;
	default:
		chan_misdn_log(1, bc->port, " --> unknown facility\n");
		break;
	}
}

static void print_bearer(struct misdn_bchannel *bc)
{
	chan_misdn_log(2, bc->port, " --> Bearer: %s\n", bearer2str(bc->capability));

	switch(bc->law) {
	case INFO_CODEC_ALAW:
		chan_misdn_log(2, bc->port, " --> Codec: Alaw\n");
		break;
	case INFO_CODEC_ULAW:
		chan_misdn_log(2, bc->port, " --> Codec: Ulaw\n");
		break;
	}
}

/*!
 * \internal
 * \brief Prefix a string to another string in place.
 *
 * \param str_prefix String to prefix to the main string.
 * \param str_main String to get the prefix added to it.
 * \param size Buffer size of the main string (Includes null terminator).
 *
 * \note The str_main buffer size must be greater than one.
 *
 * \return Nothing
 */
static void misdn_prefix_string(const char *str_prefix, char *str_main, size_t size)
{
	size_t len_over;
	size_t len_total;
	size_t len_main;
	size_t len_prefix;

	len_prefix = strlen(str_prefix);
	if (!len_prefix) {
		/* There is no prefix to prepend. */
		return;
	}
	len_main = strlen(str_main);
	len_total = len_prefix + len_main;
	if (size <= len_total) {
		/* We need to truncate since the buffer is too small. */
		len_over = len_total + 1 - size;
		if (len_over <= len_main) {
			len_main -= len_over;
		} else {
			len_over -= len_main;
			len_main = 0;
			len_prefix -= len_over;
		}
	}
	if (len_main) {
		memmove(str_main + len_prefix, str_main, len_main);
	}
	memcpy(str_main, str_prefix, len_prefix);
	str_main[len_prefix + len_main] = '\0';
}

/*!
 * \internal
 * \brief Add a configured prefix to the given number.
 *
 * \param port Logical port number
 * \param number_type Type-of-number passed in.
 * \param number Given number string to add prefix
 * \param size Buffer size number string occupies.
 *
 * \return Nothing
 */
static void misdn_add_number_prefix(int port, enum mISDN_NUMBER_TYPE number_type, char *number, size_t size)
{
	enum misdn_cfg_elements type_prefix;
	char num_prefix[MISDN_MAX_NUMBER_LEN];

	/* Get prefix string. */
	switch (number_type) {
	case NUMTYPE_UNKNOWN:
		type_prefix = MISDN_CFG_TON_PREFIX_UNKNOWN;
		break;
	case NUMTYPE_INTERNATIONAL:
		type_prefix = MISDN_CFG_TON_PREFIX_INTERNATIONAL;
		break;
	case NUMTYPE_NATIONAL:
		type_prefix = MISDN_CFG_TON_PREFIX_NATIONAL;
		break;
	case NUMTYPE_NETWORK_SPECIFIC:
		type_prefix = MISDN_CFG_TON_PREFIX_NETWORK_SPECIFIC;
		break;
	case NUMTYPE_SUBSCRIBER:
		type_prefix = MISDN_CFG_TON_PREFIX_SUBSCRIBER;
		break;
	case NUMTYPE_ABBREVIATED:
		type_prefix = MISDN_CFG_TON_PREFIX_ABBREVIATED;
		break;
	default:
		/* Type-of-number does not have a prefix that can be added. */
		return;
	}
	misdn_cfg_get(port, type_prefix, num_prefix, sizeof(num_prefix));

	misdn_prefix_string(num_prefix, number, size);
}

static void export_aoc_vars(int originator, struct ast_channel *ast, struct misdn_bchannel *bc)
{
	char buf[128];

	if (!bc->AOCD_need_export || !ast) {
		return;
	}

	if (originator == ORG_AST) {
		ast = ast_bridged_channel(ast);
		if (!ast) {
			return;
		}
	}

	switch (bc->AOCDtype) {
	case Fac_AOCDCurrency:
		pbx_builtin_setvar_helper(ast, "AOCD_Type", "currency");
		if (bc->AOCD.currency.chargeNotAvailable) {
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "no");
		} else {
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "yes");
			if (bc->AOCD.currency.freeOfCharge) {
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "yes");
			} else {
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "no");
				if (snprintf(buf, sizeof(buf), "%d %s", bc->AOCD.currency.currencyAmount * bc->AOCD.currency.multiplier, bc->AOCD.currency.currency) < sizeof(buf)) {
					pbx_builtin_setvar_helper(ast, "AOCD_Amount", buf);
					if (bc->AOCD.currency.billingId >= 0 && snprintf(buf, sizeof(buf), "%d", bc->AOCD.currency.billingId) < sizeof(buf)) {
						pbx_builtin_setvar_helper(ast, "AOCD_BillingId", buf);
					}
				}
			}
		}
		break;
	case Fac_AOCDChargingUnit:
		pbx_builtin_setvar_helper(ast, "AOCD_Type", "charging_unit");
		if (bc->AOCD.chargingUnit.chargeNotAvailable) {
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "no");
		} else {
			pbx_builtin_setvar_helper(ast, "AOCD_ChargeAvailable", "yes");
			if (bc->AOCD.chargingUnit.freeOfCharge) {
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "yes");
			} else {
				pbx_builtin_setvar_helper(ast, "AOCD_FreeOfCharge", "no");
				if (snprintf(buf, sizeof(buf), "%d", bc->AOCD.chargingUnit.recordedUnits) < sizeof(buf)) {
					pbx_builtin_setvar_helper(ast, "AOCD_RecordedUnits", buf);
					if (bc->AOCD.chargingUnit.billingId >= 0 && snprintf(buf, sizeof(buf), "%d", bc->AOCD.chargingUnit.billingId) < sizeof(buf)) {
						pbx_builtin_setvar_helper(ast, "AOCD_BillingId", buf);
					}
				}
			}
		}
		break;
	default:
		break;
	}

	bc->AOCD_need_export = 0;
}

/*************** Helpers END *************/

static void sighandler(int sig)
{
}

static void *misdn_tasks_thread_func(void *data)
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
		if (wait < 0) {
			wait = 8000;
		}
		if (poll(NULL, 0, wait) < 0) {
			chan_misdn_log(4, 0, "Waking up misdn_tasks thread\n");
		}
		ast_sched_runq(misdn_tasks);
	}
	return NULL;
}

static void misdn_tasks_init(void)
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

	while (sem_wait(&blocker) && --i) {
	}
	sem_destroy(&blocker);
}

static void misdn_tasks_destroy(void)
{
	if (misdn_tasks) {
		chan_misdn_log(4, 0, "Killing misdn_tasks thread\n");
		if (pthread_cancel(misdn_tasks_thread) == 0) {
			cb_log(4, 0, "Joining misdn_tasks thread\n");
			pthread_join(misdn_tasks_thread, NULL);
		}
		sched_context_destroy(misdn_tasks);
	}
}

static inline void misdn_tasks_wakeup(void)
{
	pthread_kill(misdn_tasks_thread, SIGUSR1);
}

static inline int _misdn_tasks_add_variable(int timeout, ast_sched_cb callback, const void *data, int variable)
{
	int task_id;

	if (!misdn_tasks) {
		misdn_tasks_init();
	}
	task_id = ast_sched_add_variable(misdn_tasks, timeout, callback, data, variable);
	misdn_tasks_wakeup();

	return task_id;
}

static int misdn_tasks_add(int timeout, ast_sched_cb callback, const void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 0);
}

static int misdn_tasks_add_variable(int timeout, ast_sched_cb callback, const void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 1);
}

static void misdn_tasks_remove(int task_id)
{
	AST_SCHED_DEL(misdn_tasks, task_id);
}

static int misdn_l1_task(const void *vdata)
{
	const int *data = vdata;

	misdn_lib_isdn_l1watcher(*data);
	chan_misdn_log(5, *data, "L1watcher timeout\n");
	return 1;
}

static int misdn_overlap_dial_task(const void *data)
{
	struct timeval tv_end, tv_now;
	int diff;
	struct chan_list *ch = (struct chan_list *) data;
	char *dad;

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
	if (100 < diff) {
		return diff;
	}

	/* if we are 100ms near the timeout, we are satisfied.. */
	stop_indicate(ch);

	if (ast_strlen_zero(ch->bc->dialed.number)) {
		dad = "s";
		strcpy(ch->ast->exten, dad);
	} else {
		dad = ch->bc->dialed.number;
	}

	if (ast_exists_extension(ch->ast, ch->context, dad, 1, ch->bc->caller.number)) {
		ch->state = MISDN_DIALING;
		if (pbx_start_chan(ch) < 0) {
			chan_misdn_log(-1, ch->bc->port, "ast_pbx_start returned < 0 in misdn_overlap_dial_task\n");
			goto misdn_overlap_dial_task_disconnect;
		}
	} else {
misdn_overlap_dial_task_disconnect:
		hanguptone_indicate(ch);
		ch->bc->out_cause = AST_CAUSE_UNALLOCATED;
		ch->state = MISDN_CLEANING;
		misdn_lib_send_event(ch->bc, EVENT_DISCONNECT);
	}
	ch->overlap_dial_task = -1;
	return 0;
}

static void send_digit_to_chan(struct chan_list *cl, char digit)
{
	static const char * const dtmf_tones[] = {
/* *INDENT-OFF* */
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
		"!941+1477/100,!0/100", /* # */
/* *INDENT-ON* */
	};
	struct ast_channel *chan = cl->ast;

	if (digit >= '0' && digit <='9') {
		ast_playtones_start(chan, 0, dtmf_tones[digit - '0'], 0);
	} else if (digit >= 'A' && digit <= 'D') {
		ast_playtones_start(chan, 0, dtmf_tones[digit - 'A' + 10], 0);
	} else if (digit == '*') {
		ast_playtones_start(chan, 0, dtmf_tones[14], 0);
	} else if (digit == '#') {
		ast_playtones_start(chan, 0, dtmf_tones[15], 0);
	} else {
		/* not handled */
		ast_debug(1, "Unable to handle DTMF tone '%c' for '%s'\n", digit, chan->name);
	}
}

/*** CLI HANDLING ***/
static char *handle_cli_misdn_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int level;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn set debug [on|off]";
		e->usage =
			"Usage: misdn set debug {on|off|<level>} [only] | [port <port> [only]]\n"
			"       Set the debug level of the mISDN channel.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_debug_port(a);
	}

	if (a->argc < 4 || a->argc > 7) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[3], "on")) {
		level = 1;
	} else if (!strcasecmp(a->argv[3], "off")) {
		level = 0;
	} else if (isdigit(a->argv[3][0])) {
		level = atoi(a->argv[3]);
	} else {
		return CLI_SHOWUSAGE;
	}

	switch (a->argc) {
	case 4:
	case 5:
		{
			int i;
			int only = 0;
			if (a->argc == 5) {
				if (strncasecmp(a->argv[4], "only", strlen(a->argv[4]))) {
					return CLI_SHOWUSAGE;
				} else {
					only = 1;
				}
			}

			for (i = 0; i <= max_ports; i++) {
				misdn_debug[i] = level;
				misdn_debug_only[i] = only;
			}
			ast_cli(a->fd, "changing debug level for all ports to %d%s\n", misdn_debug[0], only ? " (only)" : "");
		}
		break;
	case 6:
	case 7:
		{
			int port;
			if (strncasecmp(a->argv[4], "port", strlen(a->argv[4])))
				return CLI_SHOWUSAGE;
			port = atoi(a->argv[5]);
			if (port <= 0 || port > max_ports) {
				switch (max_ports) {
				case 0:
					ast_cli(a->fd, "port number not valid! no ports available so you won't get lucky with any number here...\n");
					break;
				case 1:
					ast_cli(a->fd, "port number not valid! only port 1 is available.\n");
					break;
				default:
					ast_cli(a->fd, "port number not valid! only ports 1 to %d are available.\n", max_ports);
				}
				return 0;
			}
			if (a->argc == 7) {
				if (strncasecmp(a->argv[6], "only", strlen(a->argv[6]))) {
					return CLI_SHOWUSAGE;
				} else {
					misdn_debug_only[port] = 1;
				}
			} else {
				misdn_debug_only[port] = 0;
			}
			misdn_debug[port] = level;
			ast_cli(a->fd, "changing debug level to %d%s for port %d\n", misdn_debug[port], misdn_debug_only[port] ? " (only)" : "", port);
		}
	}

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_set_crypt_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn set crypt debug";
		e->usage =
			"Usage: misdn set crypt debug <level>\n"
			"       Set the crypt debug level of the mISDN channel. Level\n"
			"       must be 1 or 2.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	/* XXX Is this supposed to not do anything? XXX */

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_port_block(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn port block";
		e->usage =
			"Usage: misdn port block <port>\n"
			"       Block the specified port by <port>.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	misdn_lib_port_block(atoi(a->argv[3]));

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_port_unblock(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn port unblock";
		e->usage =
			"Usage: misdn port unblock <port>\n"
			"       Unblock the port specified by <port>.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	misdn_lib_port_unblock(atoi(a->argv[3]));

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_restart_port(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn restart port";
		e->usage =
			"Usage: misdn restart port <port>\n"
			"       Restart the given port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	misdn_lib_port_restart(atoi(a->argv[3]));

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_restart_pid(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn restart pid";
		e->usage =
			"Usage: misdn restart pid <pid>\n"
			"       Restart the given pid\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	misdn_lib_pid_restart(atoi(a->argv[3]));

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_port_up(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn port up";
		e->usage =
			"Usage: misdn port up <port>\n"
			"       Try to establish L1 on the given port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	misdn_lib_get_port_up(atoi(a->argv[3]));

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_port_down(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn port down";
		e->usage =
			"Usage: misdn port down <port>\n"
			"       Try to deactivate the L1 on the given port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	misdn_lib_get_port_down(atoi(a->argv[3]));

	return CLI_SUCCESS;
}

static inline void show_config_description(int fd, enum misdn_cfg_elements elem)
{
	char section[BUFFERSIZE];
	char name[BUFFERSIZE];
	char desc[BUFFERSIZE];
	char def[BUFFERSIZE];
	char tmp[BUFFERSIZE];

	misdn_cfg_get_name(elem, tmp, sizeof(tmp));
	term_color(name, tmp, COLOR_BRWHITE, 0, sizeof(tmp));
	misdn_cfg_get_desc(elem, desc, sizeof(desc), def, sizeof(def));

	if (elem < MISDN_CFG_LAST) {
		term_color(section, "PORTS SECTION", COLOR_YELLOW, 0, sizeof(section));
	} else {
		term_color(section, "GENERAL SECTION", COLOR_YELLOW, 0, sizeof(section));
	}

	if (*def) {
		ast_cli(fd, "[%s] %s   (Default: %s)\n\t%s\n", section, name, def, desc);
	} else {
		ast_cli(fd, "[%s] %s\n\t%s\n", section, name, desc);
	}
}

static char *handle_cli_misdn_show_config(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char buffer[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	int linebreak;
	int onlyport = -1;
	int ok = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn show config";
		e->usage =
			"Usage: misdn show config [<port> | description <config element> | descriptions [general|ports]]\n"
			"       Use 0 for <port> to only print the general config.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_show_config(a);
	}

	if (a->argc >= 4) {
		if (!strcmp(a->argv[3], "description")) {
			if (a->argc == 5) {
				enum misdn_cfg_elements elem = misdn_cfg_get_elem(a->argv[4]);
				if (elem == MISDN_CFG_FIRST) {
					ast_cli(a->fd, "Unknown element: %s\n", a->argv[4]);
				} else {
					show_config_description(a->fd, elem);
				}
				return CLI_SUCCESS;
			}
			return CLI_SHOWUSAGE;
		} else if (!strcmp(a->argv[3], "descriptions")) {
			if ((a->argc == 4) || ((a->argc == 5) && !strcmp(a->argv[4], "general"))) {
				for (elem = MISDN_GEN_FIRST + 1; elem < MISDN_GEN_LAST; ++elem) {
					show_config_description(a->fd, elem);
					ast_cli(a->fd, "\n");
				}
				ok = 1;
			}
			if ((a->argc == 4) || ((a->argc == 5) && !strcmp(a->argv[4], "ports"))) {
				for (elem = MISDN_CFG_FIRST + 1; elem < MISDN_CFG_LAST - 1 /* the ptp hack, remove the -1 when ptp is gone */; ++elem) {
					show_config_description(a->fd, elem);
					ast_cli(a->fd, "\n");
				}
				ok = 1;
			}
			return ok ? CLI_SUCCESS : CLI_SHOWUSAGE;
		} else if (!sscanf(a->argv[3], "%d", &onlyport) || onlyport < 0) {
			ast_cli(a->fd, "Unknown option: %s\n", a->argv[3]);
			return CLI_SHOWUSAGE;
		}
	}

	if (a->argc == 3 || onlyport == 0) {
		ast_cli(a->fd, "mISDN General-Config:\n");
		for (elem = MISDN_GEN_FIRST + 1, linebreak = 1; elem < MISDN_GEN_LAST; elem++, linebreak++) {
			misdn_cfg_get_config_string(0, elem, buffer, sizeof(buffer));
			ast_cli(a->fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
		}
		ast_cli(a->fd, "\n");
	}

	if (onlyport < 0) {
		int port = misdn_cfg_get_next_port(0);

		for (; port > 0; port = misdn_cfg_get_next_port(port)) {
			ast_cli(a->fd, "\n[PORT %d]\n", port);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string(port, elem, buffer, sizeof(buffer));
				ast_cli(a->fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}
			ast_cli(a->fd, "\n");
		}
	}

	if (onlyport > 0) {
		if (misdn_cfg_is_port_valid(onlyport)) {
			ast_cli(a->fd, "[PORT %d]\n", onlyport);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string(onlyport, elem, buffer, sizeof(buffer));
				ast_cli(a->fd, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}
			ast_cli(a->fd, "\n");
		} else {
			ast_cli(a->fd, "Port %d is not active!\n", onlyport);
		}
	}

	return CLI_SUCCESS;
}

struct state_struct {
	enum misdn_chan_state state;
	char txt[255];
};

static const struct state_struct state_array[] = {
/* *INDENT-OFF* */
	{ MISDN_NOTHING,             "NOTHING" },             /* at beginning */
	{ MISDN_WAITING4DIGS,        "WAITING4DIGS" },        /* when waiting for infos */
	{ MISDN_EXTCANTMATCH,        "EXTCANTMATCH" },        /* when asterisk couldn't match our ext */
	{ MISDN_INCOMING_SETUP,      "INCOMING SETUP" },      /* when pbx_start */
	{ MISDN_DIALING,             "DIALING" },             /* when pbx_start */
	{ MISDN_PROGRESS,            "PROGRESS" },            /* when pbx_start */
	{ MISDN_PROCEEDING,          "PROCEEDING" },          /* when pbx_start */
	{ MISDN_CALLING,             "CALLING" },             /* when misdn_call is called */
	{ MISDN_CALLING_ACKNOWLEDGE, "CALLING_ACKNOWLEDGE" }, /* when misdn_call is called */
	{ MISDN_ALERTING,            "ALERTING" },            /* when Alerting */
	{ MISDN_BUSY,                "BUSY" },                /* when BUSY */
	{ MISDN_CONNECTED,           "CONNECTED" },           /* when connected */
	{ MISDN_DISCONNECTED,        "DISCONNECTED" },        /* when connected */
	{ MISDN_CLEANING,            "CLEANING" },            /* when hangup from * but we were connected before */
/* *INDENT-ON* */
};

static const char *misdn_get_ch_state(struct chan_list *p)
{
	int i;
	static char state[8];

	if (!p) {
		return NULL;
	}

	for (i = 0; i < ARRAY_LEN(state_array); i++) {
		if (state_array[i].state == p->state) {
			return state_array[i].txt;
		}
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
	misdn_cfg_get(0, MISDN_GEN_TRACEFILE, global_tracefile, sizeof(global_tracefile));
	misdn_cfg_get(0, MISDN_GEN_DEBUG, &cfg_debug, sizeof(cfg_debug));

	for (i = 0;  i <= max_ports; i++) {
		misdn_debug[i] = cfg_debug;
		misdn_debug_only[i] = 0;
	}
}

static char *handle_cli_misdn_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn reload";
		e->usage =
			"Usage: misdn reload\n"
			"       Reload internal mISDN config, read from the config\n"
			"       file.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "Reloading mISDN configuration\n");
	reload_config();
	return CLI_SUCCESS;
}

static void print_bc_info(int fd, struct chan_list *help, struct misdn_bchannel *bc)
{
	struct ast_channel *ast = help->ast;

	ast_cli(fd,
		"* Pid:%d Port:%d Ch:%d Mode:%s Orig:%s dialed:%s\n"
		"  --> caller:\"%s\" <%s>\n"
		"  --> redirecting-from:\"%s\" <%s>\n"
		"  --> redirecting-to:\"%s\" <%s>\n"
		"  --> context:%s state:%s\n",
		bc->pid,
		bc->port,
		bc->channel,
		bc->nt ? "NT" : "TE",
		help->originator == ORG_AST ? "*" : "I",
		ast ? ast->exten : "",
		(ast && ast->cid.cid_name) ? ast->cid.cid_name : "",
		(ast && ast->cid.cid_num) ? ast->cid.cid_num : "",
		bc->redirecting.from.name,
		bc->redirecting.from.number,
		bc->redirecting.to.name,
		bc->redirecting.to.number,
		ast ? ast->context : "",
		misdn_get_ch_state(help));
	if (misdn_debug[bc->port] > 0) {
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
			bc->l3_id,
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
			bc->holded);
	}
}

static char *handle_cli_misdn_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_list *help;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn show channels";
		e->usage =
			"Usage: misdn show channels\n"
			"       Show the internal mISDN channel list\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	help = cl_te;

	ast_cli(a->fd, "Channel List: %p\n", cl_te);

	for (; help; help = help->next) {
		struct misdn_bchannel *bc = help->bc;
		struct ast_channel *ast = help->ast;
		if (!ast) {
			if (!bc) {
				ast_cli(a->fd, "chan_list obj. with l3id:%x has no bc and no ast Leg\n", help->l3id);
				continue;
			}
			ast_cli(a->fd, "bc with pid:%d has no Ast Leg\n", bc->pid);
			continue;
		}

		if (misdn_debug[0] > 2) {
			ast_cli(a->fd, "Bc:%p Ast:%p\n", bc, ast);
		}
		if (bc) {
			print_bc_info(a->fd, help, bc);
		} else {
			if (help->hold.state != MISDN_HOLD_IDLE) {
				ast_cli(a->fd, "ITS A HELD CALL BC:\n");
				ast_cli(a->fd, " --> l3_id: %x\n"
					" --> dialed:%s\n"
					" --> caller:\"%s\" <%s>\n"
					" --> hold_port: %d\n"
					" --> hold_channel: %d\n",
					help->l3id,
					ast->exten,
					ast->cid.cid_name ? ast->cid.cid_name : "",
					ast->cid.cid_num ? ast->cid.cid_num : "",
					help->hold.port,
					help->hold.channel
					);
			} else {
				ast_cli(a->fd, "* Channel in unknown STATE !!! Exten:%s, Callerid:%s\n", ast->exten, ast->cid.cid_num);
			}
		}
	}

 	misdn_dump_chanlist();

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_show_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_list *help;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn show channel";
		e->usage =
			"Usage: misdn show channel <channel>\n"
			"       Show an internal mISDN channel\n.";
		return NULL;
	case CLI_GENERATE:
		return complete_ch(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	help = cl_te;

	for (; help; help = help->next) {
		struct misdn_bchannel *bc = help->bc;
		struct ast_channel *ast = help->ast;

		if (bc && ast) {
			if (!strcasecmp(ast->name, a->argv[3])) {
				print_bc_info(a->fd, help, bc);
				break;
			}
		}
	}

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_set_tics(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn set tics";
		e->usage =
			"Usage: misdn set tics <value>\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	/* XXX Wow, this does... a whole lot of nothing... XXX */
	MAXTICS = atoi(a->argv[3]);

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_show_stacks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int port;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn show stacks";
		e->usage =
			"Usage: misdn show stacks\n"
			"       Show internal mISDN stack_list.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "BEGIN STACK_LIST:\n");
	for (port = misdn_cfg_get_next_port(0); port > 0;
		port = misdn_cfg_get_next_port(port)) {
		char buf[128];

		get_show_stack_details(port, buf);
		ast_cli(a->fd, "  %s  Debug:%d%s\n", buf, misdn_debug[port], misdn_debug_only[port] ? "(only)" : "");
	}

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_show_ports_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int port;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn show ports stats";
		e->usage =
			"Usage: misdn show ports stats\n"
			"       Show mISDNs channel's call statistics per port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "Port\tin_calls\tout_calls\n");
	for (port = misdn_cfg_get_next_port(0); port > 0;
		port = misdn_cfg_get_next_port(port)) {
		ast_cli(a->fd, "%d\t%d\t\t%d\n", port, misdn_in_calls[port], misdn_out_calls[port]);
	}
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_show_port(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int port;
	char buf[128];

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn show port";
		e->usage =
			"Usage: misdn show port <port>\n"
			"       Show detailed information for given port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	port = atoi(a->argv[3]);

	ast_cli(a->fd, "BEGIN STACK_LIST:\n");
	get_show_stack_details(port, buf);
	ast_cli(a->fd, "  %s  Debug:%d%s\n", buf, misdn_debug[port], misdn_debug_only[port] ? "(only)" : "");

	return CLI_SUCCESS;
}

#if defined(AST_MISDN_ENHANCEMENTS) && defined(CCBS_TEST_MESSAGES)
static const struct FacParm Fac_Msgs[] = {
/* *INDENT-OFF* */
	[0].Function = Fac_ERROR,
	[0].u.ERROR.invokeId = 8,
	[0].u.ERROR.errorValue = FacError_CCBS_AlreadyAccepted,

	[1].Function = Fac_RESULT,
	[1].u.RESULT.InvokeID = 9,

	[2].Function = Fac_REJECT,
	[2].u.REJECT.Code = FacReject_Gen_BadlyStructuredComponent,

	[3].Function = Fac_REJECT,
	[3].u.REJECT.InvokeIDPresent = 1,
	[3].u.REJECT.InvokeID = 10,
	[3].u.REJECT.Code = FacReject_Inv_InitiatorReleasing,

	[4].Function = Fac_REJECT,
	[4].u.REJECT.InvokeIDPresent = 1,
	[4].u.REJECT.InvokeID = 11,
	[4].u.REJECT.Code = FacReject_Res_MistypedResult,

	[5].Function = Fac_REJECT,
	[5].u.REJECT.InvokeIDPresent = 1,
	[5].u.REJECT.InvokeID = 12,
	[5].u.REJECT.Code = FacReject_Err_ErrorResponseUnexpected,

	[6].Function = Fac_StatusRequest,
	[6].u.StatusRequest.InvokeID = 13,
	[6].u.StatusRequest.ComponentType = FacComponent_Invoke,
	[6].u.StatusRequest.Component.Invoke.Q931ie.Bc.Length = 2,
	[6].u.StatusRequest.Component.Invoke.Q931ie.Bc.Contents = "AB",
	[6].u.StatusRequest.Component.Invoke.Q931ie.Llc.Length = 3,
	[6].u.StatusRequest.Component.Invoke.Q931ie.Llc.Contents = "CDE",
	[6].u.StatusRequest.Component.Invoke.Q931ie.Hlc.Length = 4,
	[6].u.StatusRequest.Component.Invoke.Q931ie.Hlc.Contents = "FGHI",
	[6].u.StatusRequest.Component.Invoke.CompatibilityMode = 1,

	[7].Function = Fac_StatusRequest,
	[7].u.StatusRequest.InvokeID = 14,
	[7].u.StatusRequest.ComponentType = FacComponent_Result,
	[7].u.StatusRequest.Component.Result.Status = 2,

	[8].Function = Fac_CallInfoRetain,
	[8].u.CallInfoRetain.InvokeID = 15,
	[8].u.CallInfoRetain.CallLinkageID = 115,

	[9].Function = Fac_EraseCallLinkageID,
	[9].u.EraseCallLinkageID.InvokeID = 16,
	[9].u.EraseCallLinkageID.CallLinkageID = 105,

	[10].Function = Fac_CCBSDeactivate,
	[10].u.CCBSDeactivate.InvokeID = 17,
	[10].u.CCBSDeactivate.ComponentType = FacComponent_Invoke,
	[10].u.CCBSDeactivate.Component.Invoke.CCBSReference = 2,

	[11].Function = Fac_CCBSDeactivate,
	[11].u.CCBSDeactivate.InvokeID = 18,
	[11].u.CCBSDeactivate.ComponentType = FacComponent_Result,

	[12].Function = Fac_CCBSErase,
	[12].u.CCBSErase.InvokeID = 19,
	[12].u.CCBSErase.Q931ie.Bc.Length = 2,
	[12].u.CCBSErase.Q931ie.Bc.Contents = "JK",
	[12].u.CCBSErase.AddressOfB.Party.Type = 0,
	[12].u.CCBSErase.AddressOfB.Party.LengthOfNumber = 5,
	[12].u.CCBSErase.AddressOfB.Party.Number = "33403",
	[12].u.CCBSErase.AddressOfB.Subaddress.Type = 0,
	[12].u.CCBSErase.AddressOfB.Subaddress.Length = 4,
	[12].u.CCBSErase.AddressOfB.Subaddress.u.UserSpecified.Information = "3748",
	[12].u.CCBSErase.RecallMode = 1,
	[12].u.CCBSErase.CCBSReference = 102,
	[12].u.CCBSErase.Reason = 3,

	[13].Function = Fac_CCBSErase,
	[13].u.CCBSErase.InvokeID = 20,
	[13].u.CCBSErase.Q931ie.Bc.Length = 2,
	[13].u.CCBSErase.Q931ie.Bc.Contents = "JK",
	[13].u.CCBSErase.AddressOfB.Party.Type = 1,
	[13].u.CCBSErase.AddressOfB.Party.LengthOfNumber = 11,
	[13].u.CCBSErase.AddressOfB.Party.TypeOfNumber = 1,
	[13].u.CCBSErase.AddressOfB.Party.Number = "18003020102",
	[13].u.CCBSErase.AddressOfB.Subaddress.Type = 0,
	[13].u.CCBSErase.AddressOfB.Subaddress.Length = 4,
	[13].u.CCBSErase.AddressOfB.Subaddress.u.UserSpecified.OddCountPresent = 1,
	[13].u.CCBSErase.AddressOfB.Subaddress.u.UserSpecified.OddCount = 1,
	[13].u.CCBSErase.AddressOfB.Subaddress.u.UserSpecified.Information = "3748",
	[13].u.CCBSErase.RecallMode = 1,
	[13].u.CCBSErase.CCBSReference = 102,
	[13].u.CCBSErase.Reason = 3,

	[14].Function = Fac_CCBSErase,
	[14].u.CCBSErase.InvokeID = 21,
	[14].u.CCBSErase.Q931ie.Bc.Length = 2,
	[14].u.CCBSErase.Q931ie.Bc.Contents = "JK",
	[14].u.CCBSErase.AddressOfB.Party.Type = 2,
	[14].u.CCBSErase.AddressOfB.Party.LengthOfNumber = 4,
	[14].u.CCBSErase.AddressOfB.Party.Number = "1803",
	[14].u.CCBSErase.AddressOfB.Subaddress.Type = 1,
	[14].u.CCBSErase.AddressOfB.Subaddress.Length = 4,
	[14].u.CCBSErase.AddressOfB.Subaddress.u.Nsap = "6492",
	[14].u.CCBSErase.RecallMode = 1,
	[14].u.CCBSErase.CCBSReference = 102,
	[14].u.CCBSErase.Reason = 3,

	[15].Function = Fac_CCBSErase,
	[15].u.CCBSErase.InvokeID = 22,
	[15].u.CCBSErase.Q931ie.Bc.Length = 2,
	[15].u.CCBSErase.Q931ie.Bc.Contents = "JK",
	[15].u.CCBSErase.AddressOfB.Party.Type = 3,
	[15].u.CCBSErase.AddressOfB.Party.LengthOfNumber = 4,
	[15].u.CCBSErase.AddressOfB.Party.Number = "1803",
	[15].u.CCBSErase.RecallMode = 1,
	[15].u.CCBSErase.CCBSReference = 102,
	[15].u.CCBSErase.Reason = 3,

	[16].Function = Fac_CCBSErase,
	[16].u.CCBSErase.InvokeID = 23,
	[16].u.CCBSErase.Q931ie.Bc.Length = 2,
	[16].u.CCBSErase.Q931ie.Bc.Contents = "JK",
	[16].u.CCBSErase.AddressOfB.Party.Type = 4,
	[16].u.CCBSErase.AddressOfB.Party.LengthOfNumber = 4,
	[16].u.CCBSErase.AddressOfB.Party.Number = "1803",
	[16].u.CCBSErase.RecallMode = 1,
	[16].u.CCBSErase.CCBSReference = 102,
	[16].u.CCBSErase.Reason = 3,

	[17].Function = Fac_CCBSErase,
	[17].u.CCBSErase.InvokeID = 24,
	[17].u.CCBSErase.Q931ie.Bc.Length = 2,
	[17].u.CCBSErase.Q931ie.Bc.Contents = "JK",
	[17].u.CCBSErase.AddressOfB.Party.Type = 5,
	[17].u.CCBSErase.AddressOfB.Party.LengthOfNumber = 11,
	[17].u.CCBSErase.AddressOfB.Party.TypeOfNumber = 4,
	[17].u.CCBSErase.AddressOfB.Party.Number = "18003020102",
	[17].u.CCBSErase.RecallMode = 1,
	[17].u.CCBSErase.CCBSReference = 102,
	[17].u.CCBSErase.Reason = 3,

	[18].Function = Fac_CCBSErase,
	[18].u.CCBSErase.InvokeID = 25,
	[18].u.CCBSErase.Q931ie.Bc.Length = 2,
	[18].u.CCBSErase.Q931ie.Bc.Contents = "JK",
	[18].u.CCBSErase.AddressOfB.Party.Type = 8,
	[18].u.CCBSErase.AddressOfB.Party.LengthOfNumber = 4,
	[18].u.CCBSErase.AddressOfB.Party.Number = "1803",
	[18].u.CCBSErase.RecallMode = 1,
	[18].u.CCBSErase.CCBSReference = 102,
	[18].u.CCBSErase.Reason = 3,

	[19].Function = Fac_CCBSRemoteUserFree,
	[19].u.CCBSRemoteUserFree.InvokeID = 26,
	[19].u.CCBSRemoteUserFree.Q931ie.Bc.Length = 2,
	[19].u.CCBSRemoteUserFree.Q931ie.Bc.Contents = "JK",
	[19].u.CCBSRemoteUserFree.AddressOfB.Party.Type = 8,
	[19].u.CCBSRemoteUserFree.AddressOfB.Party.LengthOfNumber = 4,
	[19].u.CCBSRemoteUserFree.AddressOfB.Party.Number = "1803",
	[19].u.CCBSRemoteUserFree.RecallMode = 1,
	[19].u.CCBSRemoteUserFree.CCBSReference = 102,

	[20].Function = Fac_CCBSCall,
	[20].u.CCBSCall.InvokeID = 27,
	[20].u.CCBSCall.CCBSReference = 115,

	[21].Function = Fac_CCBSStatusRequest,
	[21].u.CCBSStatusRequest.InvokeID = 28,
	[21].u.CCBSStatusRequest.ComponentType = FacComponent_Invoke,
	[21].u.CCBSStatusRequest.Component.Invoke.Q931ie.Bc.Length = 2,
	[21].u.CCBSStatusRequest.Component.Invoke.Q931ie.Bc.Contents = "JK",
	[21].u.CCBSStatusRequest.Component.Invoke.RecallMode = 1,
	[21].u.CCBSStatusRequest.Component.Invoke.CCBSReference = 102,

	[22].Function = Fac_CCBSStatusRequest,
	[22].u.CCBSStatusRequest.InvokeID = 29,
	[22].u.CCBSStatusRequest.ComponentType = FacComponent_Result,
	[22].u.CCBSStatusRequest.Component.Result.Free = 1,

	[23].Function = Fac_CCBSBFree,
	[23].u.CCBSBFree.InvokeID = 30,
	[23].u.CCBSBFree.Q931ie.Bc.Length = 2,
	[23].u.CCBSBFree.Q931ie.Bc.Contents = "JK",
	[23].u.CCBSBFree.AddressOfB.Party.Type = 8,
	[23].u.CCBSBFree.AddressOfB.Party.LengthOfNumber = 4,
	[23].u.CCBSBFree.AddressOfB.Party.Number = "1803",
	[23].u.CCBSBFree.RecallMode = 1,
	[23].u.CCBSBFree.CCBSReference = 14,

	[24].Function = Fac_CCBSStopAlerting,
	[24].u.CCBSStopAlerting.InvokeID = 31,
	[24].u.CCBSStopAlerting.CCBSReference = 37,

	[25].Function = Fac_CCBSRequest,
	[25].u.CCBSRequest.InvokeID = 32,
	[25].u.CCBSRequest.ComponentType = FacComponent_Invoke,
	[25].u.CCBSRequest.Component.Invoke.CallLinkageID = 57,

	[26].Function = Fac_CCBSRequest,
	[26].u.CCBSRequest.InvokeID = 33,
	[26].u.CCBSRequest.ComponentType = FacComponent_Result,
	[26].u.CCBSRequest.Component.Result.RecallMode = 1,
	[26].u.CCBSRequest.Component.Result.CCBSReference = 102,

	[27].Function = Fac_CCBSInterrogate,
	[27].u.CCBSInterrogate.InvokeID = 34,
	[27].u.CCBSInterrogate.ComponentType = FacComponent_Invoke,
	[27].u.CCBSInterrogate.Component.Invoke.AParty.Type = 8,
	[27].u.CCBSInterrogate.Component.Invoke.AParty.LengthOfNumber = 4,
	[27].u.CCBSInterrogate.Component.Invoke.AParty.Number = "1803",
	[27].u.CCBSInterrogate.Component.Invoke.CCBSReferencePresent = 1,
	[27].u.CCBSInterrogate.Component.Invoke.CCBSReference = 76,

	[28].Function = Fac_CCBSInterrogate,
	[28].u.CCBSInterrogate.InvokeID = 35,
	[28].u.CCBSInterrogate.ComponentType = FacComponent_Invoke,
	[28].u.CCBSInterrogate.Component.Invoke.AParty.Type = 8,
	[28].u.CCBSInterrogate.Component.Invoke.AParty.LengthOfNumber = 4,
	[28].u.CCBSInterrogate.Component.Invoke.AParty.Number = "1803",

	[29].Function = Fac_CCBSInterrogate,
	[29].u.CCBSInterrogate.InvokeID = 36,
	[29].u.CCBSInterrogate.ComponentType = FacComponent_Invoke,
	[29].u.CCBSInterrogate.Component.Invoke.CCBSReferencePresent = 1,
	[29].u.CCBSInterrogate.Component.Invoke.CCBSReference = 76,

	[30].Function = Fac_CCBSInterrogate,
	[30].u.CCBSInterrogate.InvokeID = 37,
	[30].u.CCBSInterrogate.ComponentType = FacComponent_Invoke,

	[31].Function = Fac_CCBSInterrogate,
	[31].u.CCBSInterrogate.InvokeID = 38,
	[31].u.CCBSInterrogate.ComponentType = FacComponent_Result,
	[31].u.CCBSInterrogate.Component.Result.RecallMode = 1,

	[32].Function = Fac_CCBSInterrogate,
	[32].u.CCBSInterrogate.InvokeID = 39,
	[32].u.CCBSInterrogate.ComponentType = FacComponent_Result,
	[32].u.CCBSInterrogate.Component.Result.RecallMode = 1,
	[32].u.CCBSInterrogate.Component.Result.NumRecords = 1,
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].CCBSReference = 12,
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].Q931ie.Bc.Length = 2,
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].Q931ie.Bc.Contents = "JK",
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].AddressOfB.Party.Type = 8,
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].AddressOfB.Party.LengthOfNumber = 4,
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].AddressOfB.Party.Number = "1803",
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].SubaddressOfA.Type = 1,
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].SubaddressOfA.Length = 4,
	[32].u.CCBSInterrogate.Component.Result.CallDetails[0].SubaddressOfA.u.Nsap = "6492",

	[33].Function = Fac_CCBSInterrogate,
	[33].u.CCBSInterrogate.InvokeID = 40,
	[33].u.CCBSInterrogate.ComponentType = FacComponent_Result,
	[33].u.CCBSInterrogate.Component.Result.RecallMode = 1,
	[33].u.CCBSInterrogate.Component.Result.NumRecords = 2,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[0].CCBSReference = 12,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[0].Q931ie.Bc.Length = 2,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[0].Q931ie.Bc.Contents = "JK",
	[33].u.CCBSInterrogate.Component.Result.CallDetails[0].AddressOfB.Party.Type = 8,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[0].AddressOfB.Party.LengthOfNumber = 4,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[0].AddressOfB.Party.Number = "1803",
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].CCBSReference = 102,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].Q931ie.Bc.Length = 2,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].Q931ie.Bc.Contents = "LM",
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].AddressOfB.Party.Type = 8,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].AddressOfB.Party.LengthOfNumber = 4,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].AddressOfB.Party.Number = "6229",
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].AddressOfB.Subaddress.Type = 1,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].AddressOfB.Subaddress.Length = 4,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].AddressOfB.Subaddress.u.Nsap = "8592",
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].SubaddressOfA.Type = 1,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].SubaddressOfA.Length = 4,
	[33].u.CCBSInterrogate.Component.Result.CallDetails[1].SubaddressOfA.u.Nsap = "6492",

	[34].Function = Fac_CCNRRequest,
	[34].u.CCNRRequest.InvokeID = 512,
	[34].u.CCNRRequest.ComponentType = FacComponent_Invoke,
	[34].u.CCNRRequest.Component.Invoke.CallLinkageID = 57,

	[35].Function = Fac_CCNRRequest,
	[35].u.CCNRRequest.InvokeID = 150,
	[35].u.CCNRRequest.ComponentType = FacComponent_Result,
	[35].u.CCNRRequest.Component.Result.RecallMode = 1,
	[35].u.CCNRRequest.Component.Result.CCBSReference = 102,

	[36].Function = Fac_CCNRInterrogate,
	[36].u.CCNRInterrogate.InvokeID = -129,
	[36].u.CCNRInterrogate.ComponentType = FacComponent_Invoke,

	[37].Function = Fac_CCNRInterrogate,
	[37].u.CCNRInterrogate.InvokeID = -3,
	[37].u.CCNRInterrogate.ComponentType = FacComponent_Result,
	[37].u.CCNRInterrogate.Component.Result.RecallMode = 1,

	[38].Function = Fac_CCBS_T_Call,
	[38].u.EctExecute.InvokeID = 41,

	[39].Function = Fac_CCBS_T_Suspend,
	[39].u.EctExecute.InvokeID = 42,

	[40].Function = Fac_CCBS_T_Resume,
	[40].u.EctExecute.InvokeID = 43,

	[41].Function = Fac_CCBS_T_RemoteUserFree,
	[41].u.EctExecute.InvokeID = 44,

	[42].Function = Fac_CCBS_T_Available,
	[42].u.EctExecute.InvokeID = 45,

	[43].Function = Fac_CCBS_T_Request,
	[43].u.CCBS_T_Request.InvokeID = 46,
	[43].u.CCBS_T_Request.ComponentType = FacComponent_Invoke,
	[43].u.CCBS_T_Request.Component.Invoke.Destination.Party.Type = 8,
	[43].u.CCBS_T_Request.Component.Invoke.Destination.Party.LengthOfNumber = 4,
	[43].u.CCBS_T_Request.Component.Invoke.Destination.Party.Number = "6229",
	[43].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Length = 2,
	[43].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Contents = "LM",
	[43].u.CCBS_T_Request.Component.Invoke.RetentionSupported = 1,
	[43].u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicatorPresent = 1,
	[43].u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicator = 1,
	[43].u.CCBS_T_Request.Component.Invoke.Originating.Party.Type = 8,
	[43].u.CCBS_T_Request.Component.Invoke.Originating.Party.LengthOfNumber = 4,
	[43].u.CCBS_T_Request.Component.Invoke.Originating.Party.Number = "9864",

	[44].Function = Fac_CCBS_T_Request,
	[44].u.CCBS_T_Request.InvokeID = 47,
	[44].u.CCBS_T_Request.ComponentType = FacComponent_Invoke,
	[44].u.CCBS_T_Request.Component.Invoke.Destination.Party.Type = 8,
	[44].u.CCBS_T_Request.Component.Invoke.Destination.Party.LengthOfNumber = 4,
	[44].u.CCBS_T_Request.Component.Invoke.Destination.Party.Number = "6229",
	[44].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Length = 2,
	[44].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Contents = "LM",
	[44].u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicatorPresent = 1,
	[44].u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicator = 1,
	[44].u.CCBS_T_Request.Component.Invoke.Originating.Party.Type = 8,
	[44].u.CCBS_T_Request.Component.Invoke.Originating.Party.LengthOfNumber = 4,
	[44].u.CCBS_T_Request.Component.Invoke.Originating.Party.Number = "9864",

	[45].Function = Fac_CCBS_T_Request,
	[45].u.CCBS_T_Request.InvokeID = 48,
	[45].u.CCBS_T_Request.ComponentType = FacComponent_Invoke,
	[45].u.CCBS_T_Request.Component.Invoke.Destination.Party.Type = 8,
	[45].u.CCBS_T_Request.Component.Invoke.Destination.Party.LengthOfNumber = 4,
	[45].u.CCBS_T_Request.Component.Invoke.Destination.Party.Number = "6229",
	[45].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Length = 2,
	[45].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Contents = "LM",
	[45].u.CCBS_T_Request.Component.Invoke.Originating.Party.Type = 8,
	[45].u.CCBS_T_Request.Component.Invoke.Originating.Party.LengthOfNumber = 4,
	[45].u.CCBS_T_Request.Component.Invoke.Originating.Party.Number = "9864",

	[46].Function = Fac_CCBS_T_Request,
	[46].u.CCBS_T_Request.InvokeID = 49,
	[46].u.CCBS_T_Request.ComponentType = FacComponent_Invoke,
	[46].u.CCBS_T_Request.Component.Invoke.Destination.Party.Type = 8,
	[46].u.CCBS_T_Request.Component.Invoke.Destination.Party.LengthOfNumber = 4,
	[46].u.CCBS_T_Request.Component.Invoke.Destination.Party.Number = "6229",
	[46].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Length = 2,
	[46].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Contents = "LM",
	[46].u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicatorPresent = 1,
	[46].u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicator = 1,

	[47].Function = Fac_CCBS_T_Request,
	[47].u.CCBS_T_Request.InvokeID = 50,
	[47].u.CCBS_T_Request.ComponentType = FacComponent_Invoke,
	[47].u.CCBS_T_Request.Component.Invoke.Destination.Party.Type = 8,
	[47].u.CCBS_T_Request.Component.Invoke.Destination.Party.LengthOfNumber = 4,
	[47].u.CCBS_T_Request.Component.Invoke.Destination.Party.Number = "6229",
	[47].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Length = 2,
	[47].u.CCBS_T_Request.Component.Invoke.Q931ie.Bc.Contents = "LM",

	[48].Function = Fac_CCBS_T_Request,
	[48].u.CCBS_T_Request.InvokeID = 51,
	[48].u.CCBS_T_Request.ComponentType = FacComponent_Result,
	[48].u.CCBS_T_Request.Component.Result.RetentionSupported = 1,

	[49].Function = Fac_CCNR_T_Request,
	[49].u.CCNR_T_Request.InvokeID = 52,
	[49].u.CCNR_T_Request.ComponentType = FacComponent_Invoke,
	[49].u.CCNR_T_Request.Component.Invoke.Destination.Party.Type = 8,
	[49].u.CCNR_T_Request.Component.Invoke.Destination.Party.LengthOfNumber = 4,
	[49].u.CCNR_T_Request.Component.Invoke.Destination.Party.Number = "6229",
	[49].u.CCNR_T_Request.Component.Invoke.Q931ie.Bc.Length = 2,
	[49].u.CCNR_T_Request.Component.Invoke.Q931ie.Bc.Contents = "LM",

	[50].Function = Fac_CCNR_T_Request,
	[50].u.CCNR_T_Request.InvokeID = 53,
	[50].u.CCNR_T_Request.ComponentType = FacComponent_Result,
	[50].u.CCNR_T_Request.Component.Result.RetentionSupported = 1,

	[51].Function = Fac_EctExecute,
	[51].u.EctExecute.InvokeID = 54,

	[52].Function = Fac_ExplicitEctExecute,
	[52].u.ExplicitEctExecute.InvokeID = 55,
	[52].u.ExplicitEctExecute.LinkID = 23,

	[53].Function = Fac_RequestSubaddress,
	[53].u.RequestSubaddress.InvokeID = 56,

	[54].Function = Fac_SubaddressTransfer,
	[54].u.SubaddressTransfer.InvokeID = 57,
	[54].u.SubaddressTransfer.Subaddress.Type = 1,
	[54].u.SubaddressTransfer.Subaddress.Length = 4,
	[54].u.SubaddressTransfer.Subaddress.u.Nsap = "6492",

	[55].Function = Fac_EctLinkIdRequest,
	[55].u.EctLinkIdRequest.InvokeID = 58,
	[55].u.EctLinkIdRequest.ComponentType = FacComponent_Invoke,

	[56].Function = Fac_EctLinkIdRequest,
	[56].u.EctLinkIdRequest.InvokeID = 59,
	[56].u.EctLinkIdRequest.ComponentType = FacComponent_Result,
	[56].u.EctLinkIdRequest.Component.Result.LinkID = 76,

	[57].Function = Fac_EctInform,
	[57].u.EctInform.InvokeID = 60,
	[57].u.EctInform.Status = 1,
	[57].u.EctInform.RedirectionPresent = 1,
	[57].u.EctInform.Redirection.Type = 0,
	[57].u.EctInform.Redirection.Unscreened.Type = 8,
	[57].u.EctInform.Redirection.Unscreened.LengthOfNumber = 4,
	[57].u.EctInform.Redirection.Unscreened.Number = "6229",

	[58].Function = Fac_EctInform,
	[58].u.EctInform.InvokeID = 61,
	[58].u.EctInform.Status = 1,
	[58].u.EctInform.RedirectionPresent = 1,
	[58].u.EctInform.Redirection.Type = 1,

	[59].Function = Fac_EctInform,
	[59].u.EctInform.InvokeID = 62,
	[59].u.EctInform.Status = 1,
	[59].u.EctInform.RedirectionPresent = 1,
	[59].u.EctInform.Redirection.Type = 2,

	[60].Function = Fac_EctInform,
	[60].u.EctInform.InvokeID = 63,
	[60].u.EctInform.Status = 1,
	[60].u.EctInform.RedirectionPresent = 1,
	[60].u.EctInform.Redirection.Type = 3,
	[60].u.EctInform.Redirection.Unscreened.Type = 8,
	[60].u.EctInform.Redirection.Unscreened.LengthOfNumber = 4,
	[60].u.EctInform.Redirection.Unscreened.Number = "3340",

	[61].Function = Fac_EctInform,
	[61].u.EctInform.InvokeID = 64,
	[61].u.EctInform.Status = 1,
	[61].u.EctInform.RedirectionPresent = 0,

	[62].Function = Fac_EctLoopTest,
	[62].u.EctLoopTest.InvokeID = 65,
	[62].u.EctLoopTest.ComponentType = FacComponent_Invoke,
	[62].u.EctLoopTest.Component.Invoke.CallTransferID = 7,

	[63].Function = Fac_EctLoopTest,
	[63].u.EctLoopTest.InvokeID = 66,
	[63].u.EctLoopTest.ComponentType = FacComponent_Result,
	[63].u.EctLoopTest.Component.Result.LoopResult = 2,

	[64].Function = Fac_ActivationDiversion,
	[64].u.ActivationDiversion.InvokeID = 67,
	[64].u.ActivationDiversion.ComponentType = FacComponent_Invoke,
	[64].u.ActivationDiversion.Component.Invoke.Procedure = 2,
	[64].u.ActivationDiversion.Component.Invoke.BasicService = 3,
	[64].u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Type = 4,
	[64].u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.LengthOfNumber = 4,
	[64].u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Number = "1803",
	[64].u.ActivationDiversion.Component.Invoke.ServedUser.Type = 4,
	[64].u.ActivationDiversion.Component.Invoke.ServedUser.LengthOfNumber = 4,
	[64].u.ActivationDiversion.Component.Invoke.ServedUser.Number = "5398",

	[65].Function = Fac_ActivationDiversion,
	[65].u.ActivationDiversion.InvokeID = 68,
	[65].u.ActivationDiversion.ComponentType = FacComponent_Invoke,
	[65].u.ActivationDiversion.Component.Invoke.Procedure = 1,
	[65].u.ActivationDiversion.Component.Invoke.BasicService = 5,
	[65].u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Type = 4,
	[65].u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.LengthOfNumber = 4,
	[65].u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Number = "1803",

	[66].Function = Fac_ActivationDiversion,
	[66].u.ActivationDiversion.InvokeID = 69,
	[66].u.ActivationDiversion.ComponentType = FacComponent_Result,

	[67].Function = Fac_DeactivationDiversion,
	[67].u.DeactivationDiversion.InvokeID = 70,
	[67].u.DeactivationDiversion.ComponentType = FacComponent_Invoke,
	[67].u.DeactivationDiversion.Component.Invoke.Procedure = 1,
	[67].u.DeactivationDiversion.Component.Invoke.BasicService = 5,

	[68].Function = Fac_DeactivationDiversion,
	[68].u.DeactivationDiversion.InvokeID = 71,
	[68].u.DeactivationDiversion.ComponentType = FacComponent_Result,

	[69].Function = Fac_ActivationStatusNotificationDiv,
	[69].u.ActivationStatusNotificationDiv.InvokeID = 72,
	[69].u.ActivationStatusNotificationDiv.Procedure = 1,
	[69].u.ActivationStatusNotificationDiv.BasicService = 5,
	[69].u.ActivationStatusNotificationDiv.ForwardedTo.Party.Type = 4,
	[69].u.ActivationStatusNotificationDiv.ForwardedTo.Party.LengthOfNumber = 4,
	[69].u.ActivationStatusNotificationDiv.ForwardedTo.Party.Number = "1803",

	[70].Function = Fac_DeactivationStatusNotificationDiv,
	[70].u.DeactivationStatusNotificationDiv.InvokeID = 73,
	[70].u.DeactivationStatusNotificationDiv.Procedure = 1,
	[70].u.DeactivationStatusNotificationDiv.BasicService = 5,

	[71].Function = Fac_InterrogationDiversion,
	[71].u.InterrogationDiversion.InvokeID = 74,
	[71].u.InterrogationDiversion.ComponentType = FacComponent_Invoke,
	[71].u.InterrogationDiversion.Component.Invoke.Procedure = 1,
	[71].u.InterrogationDiversion.Component.Invoke.BasicService = 5,

	[72].Function = Fac_InterrogationDiversion,
	[72].u.InterrogationDiversion.InvokeID = 75,
	[72].u.InterrogationDiversion.ComponentType = FacComponent_Invoke,
	[72].u.InterrogationDiversion.Component.Invoke.Procedure = 1,

	[73].Function = Fac_InterrogationDiversion,
	[73].u.InterrogationDiversion.InvokeID = 76,
	[73].u.InterrogationDiversion.ComponentType = FacComponent_Result,
	[73].u.InterrogationDiversion.Component.Result.NumRecords = 2,
	[73].u.InterrogationDiversion.Component.Result.List[0].Procedure = 2,
	[73].u.InterrogationDiversion.Component.Result.List[0].BasicService = 5,
	[73].u.InterrogationDiversion.Component.Result.List[0].ForwardedTo.Party.Type = 4,
	[73].u.InterrogationDiversion.Component.Result.List[0].ForwardedTo.Party.LengthOfNumber = 4,
	[73].u.InterrogationDiversion.Component.Result.List[0].ForwardedTo.Party.Number = "1803",
	[73].u.InterrogationDiversion.Component.Result.List[1].Procedure = 1,
	[73].u.InterrogationDiversion.Component.Result.List[1].BasicService = 3,
	[73].u.InterrogationDiversion.Component.Result.List[1].ForwardedTo.Party.Type = 4,
	[73].u.InterrogationDiversion.Component.Result.List[1].ForwardedTo.Party.LengthOfNumber = 4,
	[73].u.InterrogationDiversion.Component.Result.List[1].ForwardedTo.Party.Number = "1903",
	[73].u.InterrogationDiversion.Component.Result.List[1].ServedUser.Type = 4,
	[73].u.InterrogationDiversion.Component.Result.List[1].ServedUser.LengthOfNumber = 4,
	[73].u.InterrogationDiversion.Component.Result.List[1].ServedUser.Number = "5398",

	[74].Function = Fac_DiversionInformation,
	[74].u.DiversionInformation.InvokeID = 77,
	[74].u.DiversionInformation.DiversionReason = 3,
	[74].u.DiversionInformation.BasicService = 5,
	[74].u.DiversionInformation.ServedUserSubaddress.Type = 1,
	[74].u.DiversionInformation.ServedUserSubaddress.Length = 4,
	[74].u.DiversionInformation.ServedUserSubaddress.u.Nsap = "6492",
	[74].u.DiversionInformation.CallingAddressPresent = 1,
	[74].u.DiversionInformation.CallingAddress.Type = 0,
	[74].u.DiversionInformation.CallingAddress.Address.ScreeningIndicator = 3,
	[74].u.DiversionInformation.CallingAddress.Address.Party.Type = 4,
	[74].u.DiversionInformation.CallingAddress.Address.Party.LengthOfNumber = 4,
	[74].u.DiversionInformation.CallingAddress.Address.Party.Number = "1803",
	[74].u.DiversionInformation.OriginalCalledPresent = 1,
	[74].u.DiversionInformation.OriginalCalled.Type = 1,
	[74].u.DiversionInformation.LastDivertingPresent = 1,
	[74].u.DiversionInformation.LastDiverting.Type = 2,
	[74].u.DiversionInformation.LastDivertingReasonPresent = 1,
	[74].u.DiversionInformation.LastDivertingReason = 3,
	[74].u.DiversionInformation.UserInfo.Length = 5,
	[74].u.DiversionInformation.UserInfo.Contents = "79828",

	[75].Function = Fac_DiversionInformation,
	[75].u.DiversionInformation.InvokeID = 78,
	[75].u.DiversionInformation.DiversionReason = 3,
	[75].u.DiversionInformation.BasicService = 5,
	[75].u.DiversionInformation.CallingAddressPresent = 1,
	[75].u.DiversionInformation.CallingAddress.Type = 1,
	[75].u.DiversionInformation.OriginalCalledPresent = 1,
	[75].u.DiversionInformation.OriginalCalled.Type = 2,
	[75].u.DiversionInformation.LastDivertingPresent = 1,
	[75].u.DiversionInformation.LastDiverting.Type = 1,

	[76].Function = Fac_DiversionInformation,
	[76].u.DiversionInformation.InvokeID = 79,
	[76].u.DiversionInformation.DiversionReason = 2,
	[76].u.DiversionInformation.BasicService = 3,
	[76].u.DiversionInformation.CallingAddressPresent = 1,
	[76].u.DiversionInformation.CallingAddress.Type = 2,

	[77].Function = Fac_DiversionInformation,
	[77].u.DiversionInformation.InvokeID = 80,
	[77].u.DiversionInformation.DiversionReason = 3,
	[77].u.DiversionInformation.BasicService = 5,
	[77].u.DiversionInformation.CallingAddressPresent = 1,
	[77].u.DiversionInformation.CallingAddress.Type = 3,
	[77].u.DiversionInformation.CallingAddress.Address.ScreeningIndicator = 2,
	[77].u.DiversionInformation.CallingAddress.Address.Party.Type = 4,
	[77].u.DiversionInformation.CallingAddress.Address.Party.LengthOfNumber = 4,
	[77].u.DiversionInformation.CallingAddress.Address.Party.Number = "1803",

	[78].Function = Fac_DiversionInformation,
	[78].u.DiversionInformation.InvokeID = 81,
	[78].u.DiversionInformation.DiversionReason = 2,
	[78].u.DiversionInformation.BasicService = 4,
	[78].u.DiversionInformation.UserInfo.Length = 5,
	[78].u.DiversionInformation.UserInfo.Contents = "79828",

	[79].Function = Fac_DiversionInformation,
	[79].u.DiversionInformation.InvokeID = 82,
	[79].u.DiversionInformation.DiversionReason = 2,
	[79].u.DiversionInformation.BasicService = 4,

	[80].Function = Fac_CallDeflection,
	[80].u.CallDeflection.InvokeID = 83,
	[80].u.CallDeflection.ComponentType = FacComponent_Invoke,
	[80].u.CallDeflection.Component.Invoke.Deflection.Party.Type = 4,
	[80].u.CallDeflection.Component.Invoke.Deflection.Party.LengthOfNumber = 4,
	[80].u.CallDeflection.Component.Invoke.Deflection.Party.Number = "1803",
	[80].u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUserPresent = 1,
	[80].u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUser = 1,

	[81].Function = Fac_CallDeflection,
	[81].u.CallDeflection.InvokeID = 84,
	[81].u.CallDeflection.ComponentType = FacComponent_Invoke,
	[81].u.CallDeflection.Component.Invoke.Deflection.Party.Type = 4,
	[81].u.CallDeflection.Component.Invoke.Deflection.Party.LengthOfNumber = 4,
	[81].u.CallDeflection.Component.Invoke.Deflection.Party.Number = "1803",
	[81].u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUserPresent = 1,
	[81].u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUser = 0,

	[82].Function = Fac_CallDeflection,
	[82].u.CallDeflection.InvokeID = 85,
	[82].u.CallDeflection.ComponentType = FacComponent_Invoke,
	[82].u.CallDeflection.Component.Invoke.Deflection.Party.Type = 4,
	[82].u.CallDeflection.Component.Invoke.Deflection.Party.LengthOfNumber = 4,
	[82].u.CallDeflection.Component.Invoke.Deflection.Party.Number = "1803",

	[83].Function = Fac_CallDeflection,
	[83].u.CallDeflection.InvokeID = 86,
	[83].u.CallDeflection.ComponentType = FacComponent_Result,

	[84].Function = Fac_CallRerouteing,
	[84].u.CallRerouteing.InvokeID = 87,
	[84].u.CallRerouteing.ComponentType = FacComponent_Invoke,
	[84].u.CallRerouteing.Component.Invoke.ReroutingReason = 3,
	[84].u.CallRerouteing.Component.Invoke.ReroutingCounter = 2,
	[84].u.CallRerouteing.Component.Invoke.CalledAddress.Party.Type = 4,
	[84].u.CallRerouteing.Component.Invoke.CalledAddress.Party.LengthOfNumber = 4,
	[84].u.CallRerouteing.Component.Invoke.CalledAddress.Party.Number = "1803",
	[84].u.CallRerouteing.Component.Invoke.Q931ie.Bc.Length = 2,
	[84].u.CallRerouteing.Component.Invoke.Q931ie.Bc.Contents = "RT",
	[84].u.CallRerouteing.Component.Invoke.Q931ie.Hlc.Length = 3,
	[84].u.CallRerouteing.Component.Invoke.Q931ie.Hlc.Contents = "RTG",
	[84].u.CallRerouteing.Component.Invoke.Q931ie.Llc.Length = 2,
	[84].u.CallRerouteing.Component.Invoke.Q931ie.Llc.Contents = "MY",
	[84].u.CallRerouteing.Component.Invoke.Q931ie.UserInfo.Length = 5,
	[84].u.CallRerouteing.Component.Invoke.Q931ie.UserInfo.Contents = "YEHAW",
	[84].u.CallRerouteing.Component.Invoke.LastRerouting.Type = 1,
	[84].u.CallRerouteing.Component.Invoke.SubscriptionOption = 2,
	[84].u.CallRerouteing.Component.Invoke.CallingPartySubaddress.Type = 1,
	[84].u.CallRerouteing.Component.Invoke.CallingPartySubaddress.Length = 4,
	[84].u.CallRerouteing.Component.Invoke.CallingPartySubaddress.u.Nsap = "6492",

	[85].Function = Fac_CallRerouteing,
	[85].u.CallRerouteing.InvokeID = 88,
	[85].u.CallRerouteing.ComponentType = FacComponent_Invoke,
	[85].u.CallRerouteing.Component.Invoke.ReroutingReason = 3,
	[85].u.CallRerouteing.Component.Invoke.ReroutingCounter = 2,
	[85].u.CallRerouteing.Component.Invoke.CalledAddress.Party.Type = 4,
	[85].u.CallRerouteing.Component.Invoke.CalledAddress.Party.LengthOfNumber = 4,
	[85].u.CallRerouteing.Component.Invoke.CalledAddress.Party.Number = "1803",
	[85].u.CallRerouteing.Component.Invoke.Q931ie.Bc.Length = 2,
	[85].u.CallRerouteing.Component.Invoke.Q931ie.Bc.Contents = "RT",
	[85].u.CallRerouteing.Component.Invoke.LastRerouting.Type = 1,
	[85].u.CallRerouteing.Component.Invoke.SubscriptionOption = 2,

	[86].Function = Fac_CallRerouteing,
	[86].u.CallRerouteing.InvokeID = 89,
	[86].u.CallRerouteing.ComponentType = FacComponent_Invoke,
	[86].u.CallRerouteing.Component.Invoke.ReroutingReason = 3,
	[86].u.CallRerouteing.Component.Invoke.ReroutingCounter = 2,
	[86].u.CallRerouteing.Component.Invoke.CalledAddress.Party.Type = 4,
	[86].u.CallRerouteing.Component.Invoke.CalledAddress.Party.LengthOfNumber = 4,
	[86].u.CallRerouteing.Component.Invoke.CalledAddress.Party.Number = "1803",
	[86].u.CallRerouteing.Component.Invoke.Q931ie.Bc.Length = 2,
	[86].u.CallRerouteing.Component.Invoke.Q931ie.Bc.Contents = "RT",
	[86].u.CallRerouteing.Component.Invoke.LastRerouting.Type = 2,

	[87].Function = Fac_CallRerouteing,
	[87].u.CallRerouteing.InvokeID = 90,
	[87].u.CallRerouteing.ComponentType = FacComponent_Result,

	[88].Function = Fac_InterrogateServedUserNumbers,
	[88].u.InterrogateServedUserNumbers.InvokeID = 91,
	[88].u.InterrogateServedUserNumbers.ComponentType = FacComponent_Invoke,

	[89].Function = Fac_InterrogateServedUserNumbers,
	[89].u.InterrogateServedUserNumbers.InvokeID = 92,
	[89].u.InterrogateServedUserNumbers.ComponentType = FacComponent_Result,
	[89].u.InterrogateServedUserNumbers.Component.Result.NumRecords = 2,
	[89].u.InterrogateServedUserNumbers.Component.Result.List[0].Type = 4,
	[89].u.InterrogateServedUserNumbers.Component.Result.List[0].LengthOfNumber = 4,
	[89].u.InterrogateServedUserNumbers.Component.Result.List[0].Number = "1803",
	[89].u.InterrogateServedUserNumbers.Component.Result.List[1].Type = 4,
	[89].u.InterrogateServedUserNumbers.Component.Result.List[1].LengthOfNumber = 4,
	[89].u.InterrogateServedUserNumbers.Component.Result.List[1].Number = "5786",

	[90].Function = Fac_DivertingLegInformation1,
	[90].u.DivertingLegInformation1.InvokeID = 93,
	[90].u.DivertingLegInformation1.DiversionReason = 4,
	[90].u.DivertingLegInformation1.SubscriptionOption = 1,
	[90].u.DivertingLegInformation1.DivertedToPresent = 1,
	[90].u.DivertingLegInformation1.DivertedTo.Type = 2,

	[91].Function = Fac_DivertingLegInformation1,
	[91].u.DivertingLegInformation1.InvokeID = 94,
	[91].u.DivertingLegInformation1.DiversionReason = 4,
	[91].u.DivertingLegInformation1.SubscriptionOption = 1,

	[92].Function = Fac_DivertingLegInformation2,
	[92].u.DivertingLegInformation2.InvokeID = 95,
	[92].u.DivertingLegInformation2.DiversionCounter = 3,
	[92].u.DivertingLegInformation2.DiversionReason = 2,
	[92].u.DivertingLegInformation2.DivertingPresent = 1,
	[92].u.DivertingLegInformation2.Diverting.Type = 2,
	[92].u.DivertingLegInformation2.OriginalCalledPresent = 1,
	[92].u.DivertingLegInformation2.OriginalCalled.Type = 1,

	[93].Function = Fac_DivertingLegInformation2,
	[93].u.DivertingLegInformation2.InvokeID = 96,
	[93].u.DivertingLegInformation2.DiversionCounter = 3,
	[93].u.DivertingLegInformation2.DiversionReason = 2,
	[93].u.DivertingLegInformation2.OriginalCalledPresent = 1,
	[93].u.DivertingLegInformation2.OriginalCalled.Type = 1,

	[94].Function = Fac_DivertingLegInformation2,
	[94].u.DivertingLegInformation2.InvokeID = 97,
	[94].u.DivertingLegInformation2.DiversionCounter = 1,
	[94].u.DivertingLegInformation2.DiversionReason = 2,

	[95].Function = Fac_DivertingLegInformation3,
	[95].u.DivertingLegInformation3.InvokeID = 98,
	[95].u.DivertingLegInformation3.PresentationAllowedIndicator = 1,
/* *INDENT-ON* */
};
#endif	/* defined(AST_MISDN_ENHANCEMENTS) && defined(CCBS_TEST_MESSAGES) */

static char *handle_cli_misdn_send_facility(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *channame;
	const char *nr;
	struct chan_list *tmp;
	int port;
	const char *served_nr;
	struct misdn_bchannel dummy, *bc=&dummy;
	unsigned max_len;

 	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn send facility";
		e->usage = "Usage: misdn send facility <type> <channel|port> \"<args>\" \n"
		"\t type is one of:\n"
		"\t - calldeflect\n"
		"\t - CFActivate\n"
		"\t - CFDeactivate\n";

		return NULL;
	case CLI_GENERATE:
		return complete_ch(a);
	}

	if (a->argc < 5) {
		return CLI_SHOWUSAGE;
	}

	if (strstr(a->argv[3], "calldeflect")) {
		if (a->argc < 6) {
			ast_verbose("calldeflect requires 1 arg: ToNumber\n\n");
			return 0;
		}
		channame = a->argv[4];
		nr = a->argv[5];

		ast_verbose("Sending Calldeflection (%s) to %s\n", nr, channame);
		tmp = get_chan_by_ast_name(channame);
		if (!tmp) {
			ast_verbose("Sending CD with nr %s to %s failed: Channel does not exist.\n", nr, channame);
			return 0;
		}

#if defined(AST_MISDN_ENHANCEMENTS)
		max_len = sizeof(tmp->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.Number) - 1;
		if (max_len < strlen(nr)) {
			ast_verbose("Sending CD with nr %s to %s failed: Number too long (up to %u digits are allowed).\n",
				nr, channame, max_len);
			return 0;
		}
		tmp->bc->fac_out.Function = Fac_CallDeflection;
		tmp->bc->fac_out.u.CallDeflection.InvokeID = ++misdn_invoke_id;
		tmp->bc->fac_out.u.CallDeflection.ComponentType = FacComponent_Invoke;
		tmp->bc->fac_out.u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUserPresent = 1;
		tmp->bc->fac_out.u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUser = 0;
		tmp->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.Type = 0;/* unknown */
		tmp->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.LengthOfNumber = strlen(nr);
		strcpy((char *) tmp->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.Number, nr);
		tmp->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Subaddress.Length = 0;

#else	/* !defined(AST_MISDN_ENHANCEMENTS) */

		max_len = sizeof(tmp->bc->fac_out.u.CDeflection.DeflectedToNumber) - 1;
		if (max_len < strlen(nr)) {
			ast_verbose("Sending CD with nr %s to %s failed: Number too long (up to %u digits are allowed).\n",
				nr, channame, max_len);
			return 0;
		}
		tmp->bc->fac_out.Function = Fac_CD;
		tmp->bc->fac_out.u.CDeflection.PresentationAllowed = 0;
		//tmp->bc->fac_out.u.CDeflection.DeflectedToSubaddress[0] = 0;
		strcpy((char *) tmp->bc->fac_out.u.CDeflection.DeflectedToNumber, nr);
#endif	/* !defined(AST_MISDN_ENHANCEMENTS) */

		/* Send message */
		print_facility(&tmp->bc->fac_out, tmp->bc);
		misdn_lib_send_event(tmp->bc, EVENT_FACILITY);
	} else if (strstr(a->argv[3], "CFActivate")) {
		if (a->argc < 7) {
			ast_verbose("CFActivate requires 2 args: 1.FromNumber, 2.ToNumber\n\n");
			return 0;
		}
		port = atoi(a->argv[4]);
		served_nr = a->argv[5];
		nr = a->argv[6];

		misdn_make_dummy(bc, port, 0, misdn_lib_port_is_nt(port), 0);

		ast_verbose("Sending CFActivate  Port:(%d) FromNr. (%s) to Nr. (%s)\n", port, served_nr, nr);

#if defined(AST_MISDN_ENHANCEMENTS)
		bc->fac_out.Function = Fac_ActivationDiversion;
		bc->fac_out.u.ActivationDiversion.InvokeID = ++misdn_invoke_id;
		bc->fac_out.u.ActivationDiversion.ComponentType = FacComponent_Invoke;
		bc->fac_out.u.ActivationDiversion.Component.Invoke.BasicService = 0;/* allServices */
		bc->fac_out.u.ActivationDiversion.Component.Invoke.Procedure = 0;/* cfu (Call Forward Unconditional) */
		ast_copy_string((char *) bc->fac_out.u.ActivationDiversion.Component.Invoke.ServedUser.Number,
			served_nr, sizeof(bc->fac_out.u.ActivationDiversion.Component.Invoke.ServedUser.Number));
		bc->fac_out.u.ActivationDiversion.Component.Invoke.ServedUser.LengthOfNumber =
			strlen((char *) bc->fac_out.u.ActivationDiversion.Component.Invoke.ServedUser.Number);
		bc->fac_out.u.ActivationDiversion.Component.Invoke.ServedUser.Type = 0;/* unknown */
		ast_copy_string((char *) bc->fac_out.u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Number,
			nr, sizeof(bc->fac_out.u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Number));
		bc->fac_out.u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.LengthOfNumber =
			strlen((char *) bc->fac_out.u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Number);
		bc->fac_out.u.ActivationDiversion.Component.Invoke.ForwardedTo.Party.Type = 0;/* unknown */
		bc->fac_out.u.ActivationDiversion.Component.Invoke.ForwardedTo.Subaddress.Length = 0;

#else	/* !defined(AST_MISDN_ENHANCEMENTS) */

		bc->fac_out.Function = Fac_CFActivate;
		bc->fac_out.u.CFActivate.BasicService = 0; /* All Services */
		bc->fac_out.u.CFActivate.Procedure = 0; /* Unconditional */
		ast_copy_string((char *) bc->fac_out.u.CFActivate.ServedUserNumber, served_nr, sizeof(bc->fac_out.u.CFActivate.ServedUserNumber));
		ast_copy_string((char *) bc->fac_out.u.CFActivate.ForwardedToNumber, nr, sizeof(bc->fac_out.u.CFActivate.ForwardedToNumber));
#endif	/* !defined(AST_MISDN_ENHANCEMENTS) */

		/* Send message */
		print_facility(&bc->fac_out, bc);
		misdn_lib_send_event(bc, EVENT_FACILITY);
	} else if (strstr(a->argv[3], "CFDeactivate")) {
		if (a->argc < 6) {
			ast_verbose("CFDeactivate requires 1 arg: FromNumber\n\n");
			return 0;
		}
		port = atoi(a->argv[4]);
		served_nr = a->argv[5];

		misdn_make_dummy(bc, port, 0, misdn_lib_port_is_nt(port), 0);
		ast_verbose("Sending CFDeactivate  Port:(%d) FromNr. (%s)\n", port, served_nr);

#if defined(AST_MISDN_ENHANCEMENTS)
		bc->fac_out.Function = Fac_DeactivationDiversion;
		bc->fac_out.u.DeactivationDiversion.InvokeID = ++misdn_invoke_id;
		bc->fac_out.u.DeactivationDiversion.ComponentType = FacComponent_Invoke;
		bc->fac_out.u.DeactivationDiversion.Component.Invoke.BasicService = 0;/* allServices */
		bc->fac_out.u.DeactivationDiversion.Component.Invoke.Procedure = 0;/* cfu (Call Forward Unconditional) */
		ast_copy_string((char *) bc->fac_out.u.DeactivationDiversion.Component.Invoke.ServedUser.Number,
			served_nr, sizeof(bc->fac_out.u.DeactivationDiversion.Component.Invoke.ServedUser.Number));
		bc->fac_out.u.DeactivationDiversion.Component.Invoke.ServedUser.LengthOfNumber =
			strlen((char *) bc->fac_out.u.DeactivationDiversion.Component.Invoke.ServedUser.Number);
		bc->fac_out.u.DeactivationDiversion.Component.Invoke.ServedUser.Type = 0;/* unknown */

#else	/* !defined(AST_MISDN_ENHANCEMENTS) */

		bc->fac_out.Function = Fac_CFDeactivate;
		bc->fac_out.u.CFDeactivate.BasicService = 0; /* All Services */
		bc->fac_out.u.CFDeactivate.Procedure = 0; /* Unconditional */
		ast_copy_string((char *) bc->fac_out.u.CFActivate.ServedUserNumber, served_nr, sizeof(bc->fac_out.u.CFActivate.ServedUserNumber));
#endif	/* !defined(AST_MISDN_ENHANCEMENTS) */

		/* Send message */
		print_facility(&bc->fac_out, bc);
		misdn_lib_send_event(bc, EVENT_FACILITY);
#if defined(AST_MISDN_ENHANCEMENTS) && defined(CCBS_TEST_MESSAGES)
	} else if (strstr(a->argv[3], "test")) {
		int msg_number;

		if (a->argc < 5) {
			ast_verbose("test (<port> [<msg#>]) | (<channel-name> <msg#>)\n\n");
			return 0;
		}
		port = atoi(a->argv[4]);

		channame = argv[4];
		tmp = get_chan_by_ast_name(channame);
		if (tmp) {
			/* We are going to send this FACILITY message out on an existing connection */
			msg_number = atoi(argv[5]);
			if (msg_number < ARRAY_LEN(Fac_Msgs)) {
				tmp->bc->fac_out = Fac_Msgs[msg_number];

				/* Send message */
				print_facility(&tmp->bc->fac_out, tmp->bc);
				misdn_lib_send_event(tmp->bc, EVENT_FACILITY);
			} else {
				ast_verbose("test <channel-name> <msg#>\n\n");
			}
		} else if (a->argc < 6) {
			for (msg_number = 0; msg_number < ARRAY_LEN(Fac_Msgs); ++msg_number) {
				misdn_make_dummy(bc, port, 0, misdn_lib_port_is_nt(port), 0);
				bc->fac_out = Fac_Msgs[msg_number];

				/* Send message */
				print_facility(&bc->fac_out, bc);
				misdn_lib_send_event(bc, EVENT_FACILITY);
				sleep(1);
			}
		} else {
			msg_number = atoi(a->argv[5]);
			if (msg_number < ARRAY_LEN(Fac_Msgs)) {
				misdn_make_dummy(bc, port, 0, misdn_lib_port_is_nt(port), 0);
				bc->fac_out = Fac_Msgs[msg_number];

				/* Send message */
				print_facility(&bc->fac_out, bc);
				misdn_lib_send_event(bc, EVENT_FACILITY);
			} else {
				ast_verbose("test <port> [<msg#>]\n\n");
			}
		}
	} else if (strstr(argv[3], "register")) {
		if (argc < 5) {
			ast_cli(fd, "register <port>\n\n");
			return 0;
		}
		port = atoi(argv[4]);

		bc = misdn_lib_get_register_bc(port);
		if (!bc) {
			ast_cli(fd, "Could not allocate REGISTER bc struct\n\n");
			return 0;
		}
		bc->fac_out = Fac_Msgs[45];

		/* Send message */
		print_facility(&bc->fac_out, bc);
		misdn_lib_send_event(bc, EVENT_REGISTER);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) && defined(CCBS_TEST_MESSAGES) */
	}

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_send_restart(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int port;
	int channel;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn send restart";
		e->usage =
			"Usage: misdn send restart [port [channel]]\n"
			"       Send a restart for every bchannel on the given port.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 4 || a->argc > 5) {
		return CLI_SHOWUSAGE;
	}

	port = atoi(a->argv[3]);

	if (a->argc == 5) {
		channel = atoi(a->argv[4]);
		misdn_lib_send_restart(port, channel);
	} else {
 		misdn_lib_send_restart(port, -1);
	}

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_send_digit(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *channame;
	const char *msg;
	struct chan_list *tmp;
	int i, msglen;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn send digit";
		e->usage =
			"Usage: misdn send digit <channel> \"<msg>\" \n"
			"       Send <digit> to <channel> as DTMF Tone\n"
			"       when channel is a mISDN channel\n";
		return NULL;
	case CLI_GENERATE:
		return complete_ch(a);
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	channame = a->argv[3];
	msg = a->argv[4];
	msglen = strlen(msg);

	ast_cli(a->fd, "Sending %s to %s\n", msg, channame);

	tmp = get_chan_by_ast_name(channame);
	if (!tmp) {
		ast_cli(a->fd, "Sending %s to %s failed Channel does not exist\n", msg, channame);
		return CLI_SUCCESS;
	}
#if 1
	for (i = 0; i < msglen; i++) {
		ast_cli(a->fd, "Sending: %c\n", msg[i]);
		send_digit_to_chan(tmp, msg[i]);
		/* res = ast_safe_sleep(tmp->ast, 250); */
		usleep(250000);
		/* res = ast_waitfor(tmp->ast,100); */
	}
#else
	ast_dtmf_stream(tmp->ast, NULL, msg, 250);
#endif

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_toggle_echocancel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *channame;
	struct chan_list *tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn toggle echocancel";
		e->usage =
			"Usage: misdn toggle echocancel <channel>\n"
			"       Toggle EchoCancel on mISDN Channel.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_ch(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	channame = a->argv[3];

	ast_cli(a->fd, "Toggling EchoCancel on %s\n", channame);

	tmp = get_chan_by_ast_name(channame);
	if (!tmp) {
		ast_cli(a->fd, "Toggling EchoCancel %s failed Channel does not exist\n", channame);
		return CLI_SUCCESS;
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

	return CLI_SUCCESS;
}

static char *handle_cli_misdn_send_display(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *channame;
	const char *msg;
	struct chan_list *tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "misdn send display";
		e->usage =
			"Usage: misdn send display <channel> \"<msg>\" \n"
			"       Send <msg> to <channel> as Display Message\n"
			"       when channel is a mISDN channel\n";
		return NULL;
	case CLI_GENERATE:
		return complete_ch(a);
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	channame = a->argv[3];
	msg = a->argv[4];

	ast_cli(a->fd, "Sending %s to %s\n", msg, channame);
	tmp = get_chan_by_ast_name(channame);

	if (tmp && tmp->bc) {
		ast_copy_string(tmp->bc->display, msg, sizeof(tmp->bc->display));
		misdn_lib_send_event(tmp->bc, EVENT_INFORMATION);
	} else {
		ast_cli(a->fd, "No such channel %s\n", channame);
		return CLI_SUCCESS;
	}

	return CLI_SUCCESS;
}

static char *complete_ch(struct ast_cli_args *a)
{
	return ast_complete_channels(a->line, a->word, a->pos, a->n, 3);
}

static char *complete_debug_port(struct ast_cli_args *a)
{
	if (a->n) {
		return NULL;
	}

	switch (a->pos) {
	case 4:
		if (a->word[0] == 'p') {
			return ast_strdup("port");
		} else if (a->word[0] == 'o') {
			return ast_strdup("only");
		}
		break;
	case 6:
		if (a->word[0] == 'o') {
			return ast_strdup("only");
		}
		break;
	}
	return NULL;
}

static char *complete_show_config(struct ast_cli_args *a)
{
	char buffer[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	int wordlen = strlen(a->word);
	int which = 0;
	int port = 0;

	switch (a->pos) {
	case 3:
		if ((!strncmp(a->word, "description", wordlen)) && (++which > a->n)) {
			return ast_strdup("description");
		}
		if ((!strncmp(a->word, "descriptions", wordlen)) && (++which > a->n)) {
			return ast_strdup("descriptions");
		}
		if ((!strncmp(a->word, "0", wordlen)) && (++which > a->n)) {
			return ast_strdup("0");
		}
		while ((port = misdn_cfg_get_next_port(port)) != -1) {
			snprintf(buffer, sizeof(buffer), "%d", port);
			if ((!strncmp(a->word, buffer, wordlen)) && (++which > a->n)) {
				return ast_strdup(buffer);
			}
		}
		break;
	case 4:
		if (strstr(a->line, "description ")) {
			for (elem = MISDN_CFG_FIRST + 1; elem < MISDN_GEN_LAST; ++elem) {
				if ((elem == MISDN_CFG_LAST) || (elem == MISDN_GEN_FIRST)) {
					continue;
				}
				misdn_cfg_get_name(elem, buffer, sizeof(buffer));
				if (!wordlen || !strncmp(a->word, buffer, wordlen)) {
					if (++which > a->n) {
						return ast_strdup(buffer);
					}
				}
			}
		} else if (strstr(a->line, "descriptions ")) {
			if ((!wordlen || !strncmp(a->word, "general", wordlen)) && (++which > a->n)) {
				return ast_strdup("general");
			}
			if ((!wordlen || !strncmp(a->word, "ports", wordlen)) && (++which > a->n)) {
				return ast_strdup("ports");
			}
		}
		break;
	}
	return NULL;
}

static struct ast_cli_entry chan_misdn_clis[] = {
/* *INDENT-OFF* */
	AST_CLI_DEFINE(handle_cli_misdn_port_block,        "Block the given port"),
	AST_CLI_DEFINE(handle_cli_misdn_port_down,         "Try to deactivate the L1 on the given port"),
	AST_CLI_DEFINE(handle_cli_misdn_port_unblock,      "Unblock the given port"),
	AST_CLI_DEFINE(handle_cli_misdn_port_up,           "Try to establish L1 on the given port"),
	AST_CLI_DEFINE(handle_cli_misdn_reload,            "Reload internal mISDN config, read from the config file"),
	AST_CLI_DEFINE(handle_cli_misdn_restart_pid,       "Restart the given pid"),
	AST_CLI_DEFINE(handle_cli_misdn_restart_port,      "Restart the given port"),
	AST_CLI_DEFINE(handle_cli_misdn_show_channel,      "Show an internal mISDN channel"),
	AST_CLI_DEFINE(handle_cli_misdn_show_channels,     "Show the internal mISDN channel list"),
	AST_CLI_DEFINE(handle_cli_misdn_show_config,       "Show internal mISDN config, read from the config file"),
	AST_CLI_DEFINE(handle_cli_misdn_show_port,         "Show detailed information for given port"),
	AST_CLI_DEFINE(handle_cli_misdn_show_ports_stats,  "Show mISDNs channel's call statistics per port"),
	AST_CLI_DEFINE(handle_cli_misdn_show_stacks,       "Show internal mISDN stack_list"),
	AST_CLI_DEFINE(handle_cli_misdn_send_facility,     "Sends a Facility Message to the mISDN Channel"),
	AST_CLI_DEFINE(handle_cli_misdn_send_digit,        "Send DTMF digit to mISDN Channel"),
	AST_CLI_DEFINE(handle_cli_misdn_send_display,      "Send Text to mISDN Channel"),
	AST_CLI_DEFINE(handle_cli_misdn_send_restart,      "Send a restart for every bchannel on the given port"),
	AST_CLI_DEFINE(handle_cli_misdn_set_crypt_debug,   "Set CryptDebuglevel of chan_misdn, at the moment, level={1,2}"),
	AST_CLI_DEFINE(handle_cli_misdn_set_debug,         "Set Debuglevel of chan_misdn"),
	AST_CLI_DEFINE(handle_cli_misdn_set_tics,          "???"),
	AST_CLI_DEFINE(handle_cli_misdn_toggle_echocancel, "Toggle EchoCancel on mISDN Channel"),
/* *INDENT-ON* */
};

/*! \brief Updates caller ID information from config */
static void update_config(struct chan_list *ch)
{
	struct ast_channel *ast;
	struct misdn_bchannel *bc;
	int port;
	int hdlc = 0;
	int pres;
	int screen;

	if (!ch) {
		ast_log(LOG_WARNING, "Cannot configure without chanlist\n");
		return;
	}

	ast = ch->ast;
	bc = ch->bc;
	if (! ast || ! bc) {
		ast_log(LOG_WARNING, "Cannot configure without ast || bc\n");
		return;
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
		chan_misdn_log(2, port, " --> pres: %x\n", ast->connected.id.number_presentation);

		bc->caller.presentation = ast_to_misdn_pres(ast->connected.id.number_presentation);
		chan_misdn_log(2, port, " --> PRES: %s(%d)\n", misdn_to_str_pres(bc->caller.presentation), bc->caller.presentation);

		bc->caller.screening = ast_to_misdn_screen(ast->connected.id.number_presentation);
		chan_misdn_log(2, port, " --> SCREEN: %s(%d)\n", misdn_to_str_screen(bc->caller.screening), bc->caller.screening);
	} else {
		bc->caller.screening = screen;
		bc->caller.presentation = pres;
	}
}


static void config_jitterbuffer(struct chan_list *ch)
{
	struct misdn_bchannel *bc = ch->bc;
	int len = ch->jb_len;
	int threshold = ch->jb_upper_threshold;

	chan_misdn_log(5, bc->port, "config_jb: Called\n");

	if (!len) {
		chan_misdn_log(1, bc->port, "config_jb: Deactivating Jitterbuffer\n");
		bc->nojitter = 1;
	} else {
		if (len <= 100 || len > 8000) {
			chan_misdn_log(0, bc->port, "config_jb: Jitterbuffer out of Bounds, setting to 1000\n");
			len = 1000;
		}

		if (threshold > len) {
			chan_misdn_log(0, bc->port, "config_jb: Jitterbuffer Threshold > Jitterbuffer setting to Jitterbuffer -1\n");
		}

		if (ch->jb) {
			cb_log(0, bc->port, "config_jb: We've got a Jitterbuffer Already on this port.\n");
			misdn_jb_destroy(ch->jb);
			ch->jb = NULL;
		}

		ch->jb = misdn_jb_init(len, threshold);

		if (!ch->jb) {
			bc->nojitter = 1;
		}
	}
}


void debug_numtype(int port, int numtype, char *type)
{
	switch (numtype) {
	case NUMTYPE_UNKNOWN:
		chan_misdn_log(2, port, " --> %s: Unknown\n", type);
		break;
	case NUMTYPE_INTERNATIONAL:
		chan_misdn_log(2, port, " --> %s: International\n", type);
		break;
	case NUMTYPE_NATIONAL:
		chan_misdn_log(2, port, " --> %s: National\n", type);
		break;
	case NUMTYPE_NETWORK_SPECIFIC:
		chan_misdn_log(2, port, " --> %s: Network Specific\n", type);
		break;
	case NUMTYPE_SUBSCRIBER:
		chan_misdn_log(2, port, " --> %s: Subscriber\n", type);
		break;
	case NUMTYPE_ABBREVIATED:
		chan_misdn_log(2, port, " --> %s: Abbreviated\n", type);
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

	if (*bc->pipeline) {
		return 0;
	}

	misdn_cfg_get(bc->port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(ec));
	if (ec == 1) {
		ast_copy_string(bc->pipeline, "mg2ec", sizeof(bc->pipeline));
	} else if (ec > 1) {
		snprintf(bc->pipeline, sizeof(bc->pipeline), "mg2ec(deftaps=%d)", ec);
	}

	return 0;
}
#else
static int update_ec_config(struct misdn_bchannel *bc)
{
	int ec;
	int port = bc->port;

	misdn_cfg_get(port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(ec));

	if (ec == 1) {
		bc->ec_enable = 1;
	} else if (ec > 1) {
		bc->ec_enable = 1;
		bc->ec_deftaps = ec;
	}

	return 0;
}
#endif


static int read_config(struct chan_list *ch)
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

	misdn_cfg_get(port, MISDN_CFG_LANGUAGE, lang, sizeof(lang));
	ast_string_field_set(ast, language, lang);

	misdn_cfg_get(port, MISDN_CFG_MUSICCLASS, ch->mohinterpret, sizeof(ch->mohinterpret));

	misdn_cfg_get(port, MISDN_CFG_TXGAIN, &bc->txgain, sizeof(bc->txgain));
	misdn_cfg_get(port, MISDN_CFG_RXGAIN, &bc->rxgain, sizeof(bc->rxgain));

	misdn_cfg_get(port, MISDN_CFG_INCOMING_EARLY_AUDIO, &ch->incoming_early_audio, sizeof(ch->incoming_early_audio));

	misdn_cfg_get(port, MISDN_CFG_SENDDTMF, &bc->send_dtmf, sizeof(bc->send_dtmf));

	misdn_cfg_get(port, MISDN_CFG_ASTDTMF, &ch->ast_dsp, sizeof(int));
	if (ch->ast_dsp) {
		ch->ignore_dtmf = 1;
	}

	misdn_cfg_get(port, MISDN_CFG_NEED_MORE_INFOS, &bc->need_more_infos, sizeof(bc->need_more_infos));
	misdn_cfg_get(port, MISDN_CFG_NTTIMEOUT, &ch->nttimeout, sizeof(ch->nttimeout));

	misdn_cfg_get(port, MISDN_CFG_NOAUTORESPOND_ON_SETUP, &ch->noautorespond_on_setup, sizeof(ch->noautorespond_on_setup));

	misdn_cfg_get(port, MISDN_CFG_FAR_ALERTING, &ch->far_alerting, sizeof(ch->far_alerting));

	misdn_cfg_get(port, MISDN_CFG_ALLOWED_BEARERS, &ch->allowed_bearers, sizeof(ch->allowed_bearers));

  	misdn_cfg_get(port, MISDN_CFG_FAXDETECT, faxdetect, sizeof(faxdetect));

	misdn_cfg_get(port, MISDN_CFG_HDLC, &hdlc, sizeof(hdlc));
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
	misdn_cfg_get(port, MISDN_CFG_JITTERBUFFER, &ch->jb_len, sizeof(ch->jb_len));
	misdn_cfg_get(port, MISDN_CFG_JITTERBUFFER_UPPER_THRESHOLD, &ch->jb_upper_threshold, sizeof(ch->jb_upper_threshold));

	config_jitterbuffer(ch);

	misdn_cfg_get(bc->port, MISDN_CFG_CONTEXT, ch->context, sizeof(ch->context));

	ast_copy_string(ast->context, ch->context, sizeof(ast->context));

#ifdef MISDN_1_2
	update_pipeline_config(bc);
#else
	update_ec_config(bc);
#endif

	misdn_cfg_get(bc->port, MISDN_CFG_EARLY_BCONNECT, &bc->early_bconnect, sizeof(bc->early_bconnect));

	misdn_cfg_get(port, MISDN_CFG_DISPLAY_CONNECTED, &bc->display_connected, sizeof(bc->display_connected));
	misdn_cfg_get(port, MISDN_CFG_DISPLAY_SETUP, &bc->display_setup, sizeof(bc->display_setup));
	misdn_cfg_get(port, MISDN_CFG_OUTGOING_COLP, &bc->outgoing_colp, sizeof(bc->outgoing_colp));

	misdn_cfg_get(port, MISDN_CFG_PICKUPGROUP, &pg, sizeof(pg));
	misdn_cfg_get(port, MISDN_CFG_CALLGROUP, &cg, sizeof(cg));
	chan_misdn_log(5, port, " --> * CallGrp:%s PickupGrp:%s\n", ast_print_group(buf, sizeof(buf), cg), ast_print_group(buf2, sizeof(buf2), pg));
	ast->pickupgroup = pg;
	ast->callgroup = cg;

	if (ch->originator == ORG_AST) {
		char callerid[BUFFERSIZE + 1];

		/* ORIGINATOR Asterisk (outgoing call) */

		misdn_cfg_get(port, MISDN_CFG_TE_CHOOSE_CHANNEL, &(bc->te_choose_channel), sizeof(bc->te_choose_channel));

 		if (strstr(faxdetect, "outgoing") || strstr(faxdetect, "both")) {
 			ch->faxdetect = strstr(faxdetect, "nojump") ? 2 : 1;
 		}

		misdn_cfg_get(port, MISDN_CFG_CALLERID, callerid, sizeof(callerid));
		if (!ast_strlen_zero(callerid)) {
			char *cid_name = NULL;
			char *cid_num = NULL;

			ast_callerid_parse(callerid, &cid_name, &cid_num);
			if (cid_name) {
				ast_copy_string(bc->caller.name, cid_name, sizeof(bc->caller.name));
			} else {
				bc->caller.name[0] = '\0';
			}
			if (cid_num) {
				ast_copy_string(bc->caller.number, cid_num, sizeof(bc->caller.number));
			} else {
				bc->caller.number[0] = '\0';
			}
			chan_misdn_log(1, port, " --> * Setting caller to \"%s\" <%s>\n", bc->caller.name, bc->caller.number);
		}

		misdn_cfg_get(port, MISDN_CFG_DIALPLAN, &bc->dialed.number_type, sizeof(bc->dialed.number_type));
		bc->dialed.number_plan = NUMPLAN_ISDN;
		debug_numtype(port, bc->dialed.number_type, "TON");

		ch->overlap_dial = 0;
	} else {
		/* ORIGINATOR MISDN (incoming call) */

 		if (strstr(faxdetect, "incoming") || strstr(faxdetect, "both")) {
 			ch->faxdetect = (strstr(faxdetect, "nojump")) ? 2 : 1;
 		}

		/* Add configured prefix to caller.number */
		misdn_add_number_prefix(bc->port, bc->caller.number_type, bc->caller.number, sizeof(bc->caller.number));

		if (ast_strlen_zero(bc->dialed.number) && !ast_strlen_zero(bc->keypad)) {
			ast_copy_string(bc->dialed.number, bc->keypad, sizeof(bc->dialed.number));
		}

		/* Add configured prefix to dialed.number */
		misdn_add_number_prefix(bc->port, bc->dialed.number_type, bc->dialed.number, sizeof(bc->dialed.number));

		ast_copy_string(ast->exten, bc->dialed.number, sizeof(ast->exten));

		misdn_cfg_get(bc->port, MISDN_CFG_OVERLAP_DIAL, &ch->overlap_dial, sizeof(ch->overlap_dial));
		ast_mutex_init(&ch->overlap_tv_lock);
	} /* ORIG MISDN END */

	ch->overlap_dial_task = -1;

	if (ch->faxdetect  || ch->ast_dsp) {
		misdn_cfg_get(port, MISDN_CFG_FAXDETECT_TIMEOUT, &ch->faxdetect_timeout, sizeof(ch->faxdetect_timeout));
		if (!ch->dsp) {
			ch->dsp = ast_dsp_new();
		}
		if (ch->dsp) {
			ast_dsp_set_features(ch->dsp, DSP_FEATURE_DIGIT_DETECT | (ch->faxdetect ? DSP_FEATURE_FAX_DETECT : 0));
		}
		if (!ch->trans) {
			ch->trans = ast_translator_build_path(AST_FORMAT_SLINEAR, AST_FORMAT_ALAW);
		}
	}

	/* AOCD initialization */
	bc->AOCDtype = Fac_None;

	return 0;
}

/*!
 * \internal
 * \brief Send a connected line update to the other channel
 *
 * \param ast Current Asterisk channel
 * \param id Party id information to send to the other side
 * \param source Why are we sending this update
 *
 * \return Nothing
 */
static void misdn_queue_connected_line_update(struct ast_channel *ast, const struct misdn_party_id *id, enum AST_CONNECTED_LINE_UPDATE_SOURCE source)
{
	struct ast_party_connected_line connected;

	ast_party_connected_line_init(&connected);
	connected.id.number = (char *) id->number;
	connected.id.number_type = misdn_to_ast_ton(id->number_type)
		| misdn_to_ast_plan(id->number_plan);
	connected.id.number_presentation = misdn_to_ast_pres(id->presentation)
		| misdn_to_ast_screen(id->screening);
	connected.source = source;
	ast_channel_queue_connected_line_update(ast, &connected);
}

/*!
 * \internal
 * \brief Get the connected line information out of the Asterisk channel.
 *
 * \param ast Current Asterisk channel
 * \param bc Associated B channel
 * \param originator Who originally created this channel. ORG_AST or ORG_MISDN
 *
 * \return Nothing
 */
static void misdn_get_connected_line(struct ast_channel *ast, struct misdn_bchannel *bc, int originator)
{
	int number_type;

	if (originator == ORG_MISDN) {
		/* ORIGINATOR MISDN (incoming call) */

		ast_copy_string(bc->connected.name, S_OR(ast->connected.id.name, ""), sizeof(bc->connected.name));
		ast_copy_string(bc->connected.number, S_OR(ast->connected.id.number, ""), sizeof(bc->connected.number));
		bc->connected.presentation = ast_to_misdn_pres(ast->connected.id.number_presentation);
		bc->connected.screening = ast_to_misdn_screen(ast->connected.id.number_presentation);

		misdn_cfg_get(bc->port, MISDN_CFG_CPNDIALPLAN, &number_type, sizeof(number_type));
		if (number_type < 0) {
			bc->connected.number_type = ast_to_misdn_ton(ast->connected.id.number_type);
			bc->connected.number_plan = ast_to_misdn_plan(ast->connected.id.number_type);
		} else {
			/* Force us to send in CONNECT message */
			bc->connected.number_type = number_type;
			bc->connected.number_plan = NUMPLAN_ISDN;
		}
		debug_numtype(bc->port, bc->connected.number_type, "CTON");
	} else {
		/* ORIGINATOR Asterisk (outgoing call) */

		ast_copy_string(bc->caller.name, S_OR(ast->connected.id.name, ""), sizeof(bc->caller.name));
		ast_copy_string(bc->caller.number, S_OR(ast->connected.id.number, ""), sizeof(bc->caller.number));
		bc->caller.presentation = ast_to_misdn_pres(ast->connected.id.number_presentation);
		bc->caller.screening = ast_to_misdn_screen(ast->connected.id.number_presentation);

		misdn_cfg_get(bc->port, MISDN_CFG_LOCALDIALPLAN, &number_type, sizeof(number_type));
		if (number_type < 0) {
			bc->caller.number_type = ast_to_misdn_ton(ast->connected.id.number_type);
			bc->caller.number_plan = ast_to_misdn_plan(ast->connected.id.number_type);
		} else {
			/* Force us to send in SETUP message */
			bc->caller.number_type = number_type;
			bc->caller.number_plan = NUMPLAN_ISDN;
		}
		debug_numtype(bc->port, bc->caller.number_type, "LTON");
	}
}

/*!
 * \internal
 * \brief Notify peer that the connected line has changed.
 *
 * \param ast Current Asterisk channel
 * \param bc Associated B channel
 * \param originator Who originally created this channel. ORG_AST or ORG_MISDN
 *
 * \return Nothing
 */
static void misdn_update_connected_line(struct ast_channel *ast, struct misdn_bchannel *bc, int originator)
{
	struct chan_list *ch;

	misdn_get_connected_line(ast, bc, originator);
	if (originator == ORG_MISDN) {
		bc->redirecting.to = bc->connected;
	} else {
		bc->redirecting.to = bc->caller;
	}
	switch (bc->outgoing_colp) {
	case 1:/* restricted */
		bc->redirecting.to.presentation = 1;/* restricted */
		break;
	case 2:/* blocked */
		/* Don't tell the remote party that the call was transferred. */
		return;
	default:
		break;
	}

	ch = MISDN_ASTERISK_TECH_PVT(ast);
	if (ch->state == MISDN_CONNECTED
		|| originator != ORG_MISDN) {
		int is_ptmp;

		is_ptmp = !misdn_lib_is_ptp(bc->port);
		if (is_ptmp) {
			/* Send NOTIFY(transfer-active, redirecting.to data) */
			bc->redirecting.to_changed = 1;
			bc->notify_description_code = mISDN_NOTIFY_CODE_CALL_TRANSFER_ACTIVE;
			misdn_lib_send_event(bc, EVENT_NOTIFY);
#if defined(AST_MISDN_ENHANCEMENTS)
		} else {
			/* Send EctInform(transfer-active, redirecting.to data) */
			bc->fac_out.Function = Fac_EctInform;
			bc->fac_out.u.EctInform.InvokeID = ++misdn_invoke_id;
			bc->fac_out.u.EctInform.Status = 1;/* active */
			bc->fac_out.u.EctInform.RedirectionPresent = 1;/* Must be present when status is active */
			misdn_PresentedNumberUnscreened_fill(&bc->fac_out.u.EctInform.Redirection,
				&bc->redirecting.to);

			/* Send message */
			print_facility(&bc->fac_out, bc);
			misdn_lib_send_event(bc, EVENT_FACILITY);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
		}
	}
}

/*!
 * \internal
 * \brief Copy the redirecting information out of the Asterisk channel
 *
 * \param bc Associated B channel
 * \param ast Current Asterisk channel
 *
 * \return Nothing
 */
static void misdn_copy_redirecting_from_ast(struct misdn_bchannel *bc, struct ast_channel *ast)
{
	ast_copy_string(bc->redirecting.from.name, S_OR(ast->redirecting.from.name, ""), sizeof(bc->redirecting.from.name));
	ast_copy_string(bc->redirecting.from.number, S_OR(ast->cid.cid_rdnis, ""), sizeof(bc->redirecting.from.number));
	bc->redirecting.from.presentation = ast_to_misdn_pres(ast->redirecting.from.number_presentation);
	bc->redirecting.from.screening = ast_to_misdn_screen(ast->redirecting.from.number_presentation);
	bc->redirecting.from.number_type = ast_to_misdn_ton(ast->redirecting.from.number_type);
	bc->redirecting.from.number_plan = ast_to_misdn_plan(ast->redirecting.from.number_type);

	ast_copy_string(bc->redirecting.to.name, S_OR(ast->redirecting.to.name, ""), sizeof(bc->redirecting.to.name));
	ast_copy_string(bc->redirecting.to.number, S_OR(ast->redirecting.to.number, ""), sizeof(bc->redirecting.to.number));
	bc->redirecting.to.presentation = ast_to_misdn_pres(ast->redirecting.to.number_presentation);
	bc->redirecting.to.screening = ast_to_misdn_screen(ast->redirecting.to.number_presentation);
	bc->redirecting.to.number_type = ast_to_misdn_ton(ast->redirecting.to.number_type);
	bc->redirecting.to.number_plan = ast_to_misdn_plan(ast->redirecting.to.number_type);

	bc->redirecting.reason = ast_to_misdn_reason(ast->redirecting.reason);
	bc->redirecting.count = ast->redirecting.count;
}

/*!
 * \internal
 * \brief Copy the redirecting info into the Asterisk channel
 *
 * \param ast Current Asterisk channel
 * \param redirect Associated B channel redirecting info
 *
 * \return Nothing
 */
static void misdn_copy_redirecting_to_ast(struct ast_channel *ast, const struct misdn_party_redirecting *redirect)
{
	struct ast_party_redirecting redirecting;

	ast_party_redirecting_set_init(&redirecting, &ast->redirecting);

	redirecting.from.number = (char *) redirect->from.number;
	redirecting.from.number_type =
		misdn_to_ast_ton(redirect->from.number_type)
		| misdn_to_ast_plan(redirect->from.number_plan);
	redirecting.from.number_presentation =
		misdn_to_ast_pres(redirect->from.presentation)
		| misdn_to_ast_screen(redirect->from.screening);

	redirecting.to.number = (char *) redirect->to.number;
	redirecting.to.number_type =
		misdn_to_ast_ton(redirect->to.number_type)
		| misdn_to_ast_plan(redirect->to.number_plan);
	redirecting.to.number_presentation =
		misdn_to_ast_pres(redirect->to.presentation)
		| misdn_to_ast_screen(redirect->to.screening);

	redirecting.reason = misdn_to_ast_reason(redirect->reason);
	redirecting.count = redirect->count;

	ast_channel_set_redirecting(ast, &redirecting);
}

/*!
 * \internal
 * \brief Notify peer that the redirecting information has changed.
 *
 * \param ast Current Asterisk channel
 * \param bc Associated B channel
 * \param originator Who originally created this channel. ORG_AST or ORG_MISDN
 *
 * \return Nothing
 */
static void misdn_update_redirecting(struct ast_channel *ast, struct misdn_bchannel *bc, int originator)
{
	int is_ptmp;

	misdn_copy_redirecting_from_ast(bc, ast);
	switch (bc->outgoing_colp) {
	case 1:/* restricted */
		bc->redirecting.to.presentation = 1;/* restricted */
		break;
	case 2:/* blocked */
		/* Don't tell the remote party that the call was redirected. */
		return;
	default:
		break;
	}

	if (originator != ORG_MISDN) {
		return;
	}

	is_ptmp = !misdn_lib_is_ptp(bc->port);
	if (is_ptmp) {
		/* Send NOTIFY(call-is-diverting, redirecting.to data) */
		bc->redirecting.to_changed = 1;
		bc->notify_description_code = mISDN_NOTIFY_CODE_CALL_IS_DIVERTING;
		misdn_lib_send_event(bc, EVENT_NOTIFY);
#if defined(AST_MISDN_ENHANCEMENTS)
	} else {
		int match;	/* TRUE if the dialed number matches the redirecting to number */

		match = (strcmp(ast->exten, bc->redirecting.to.number) == 0) ? 1 : 0;
		if (!bc->div_leg_3_tx_pending
			|| !match) {
			/* Send DivertingLegInformation1 */
			bc->fac_out.Function = Fac_DivertingLegInformation1;
			bc->fac_out.u.DivertingLegInformation1.InvokeID = ++misdn_invoke_id;
			bc->fac_out.u.DivertingLegInformation1.DiversionReason =
				misdn_to_diversion_reason(bc->redirecting.reason);
			bc->fac_out.u.DivertingLegInformation1.SubscriptionOption = 2;/* notificationWithDivertedToNr */
			bc->fac_out.u.DivertingLegInformation1.DivertedToPresent = 1;
			misdn_PresentedNumberUnscreened_fill(&bc->fac_out.u.DivertingLegInformation1.DivertedTo, &bc->redirecting.to);
			print_facility(&bc->fac_out, bc);
			misdn_lib_send_event(bc, EVENT_FACILITY);
		}
		bc->div_leg_3_tx_pending = 0;

		/* Send DivertingLegInformation3 */
		bc->fac_out.Function = Fac_DivertingLegInformation3;
		bc->fac_out.u.DivertingLegInformation3.InvokeID = ++misdn_invoke_id;
		bc->fac_out.u.DivertingLegInformation3.PresentationAllowedIndicator =
			bc->redirecting.to.presentation == 0 ? 1 : 0;
		print_facility(&bc->fac_out, bc);
		misdn_lib_send_event(bc, EVENT_FACILITY);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	}
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
	int number_type;
	struct chan_list *ch;
	struct misdn_bchannel *newbc;
	char *dest_cp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(intf);	/* The interface token is discarded. */
		AST_APP_ARG(ext);	/* extension token */
		AST_APP_ARG(opts);	/* options token */
	);

	if (!ast) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on ast_channel *ast where ast == NULL\n");
		return -1;
	}

	if (((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) || !dest) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", ast->name);
		ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}

	ch = MISDN_ASTERISK_TECH_PVT(ast);
	if (!ch) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, chan_list *ch==NULL\n", ast->name);
		ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}

	newbc = ch->bc;
	if (!newbc) {
		ast_log(LOG_WARNING, " --> ! misdn_call called on %s, newbc==NULL\n", ast->name);
		ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}

	port = newbc->port;

#if defined(AST_MISDN_ENHANCEMENTS)
	if ((ch->peer = misdn_cc_caller_get(ast))) {
		chan_misdn_log(3, port, " --> Found CC caller data, peer:%s\n",
			ch->peer->chan ? "available" : "NULL");
	}

	if (ch->record_id != -1) {
		struct misdn_cc_record *cc_record;

		/* This is a call completion retry call */
		AST_LIST_LOCK(&misdn_cc_records_db);
		cc_record = misdn_cc_find_by_id(ch->record_id);
		if (!cc_record) {
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			ast_log(LOG_WARNING, " --> ! misdn_call called on %s, cc_record==NULL\n", ast->name);
			ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
			ast_setstate(ast, AST_STATE_DOWN);
			return -1;
		}

		/* Setup calling parameters to retry the call. */
		newbc->dialed = cc_record->redial.dialed;
		newbc->caller = cc_record->redial.caller;
		memset(&newbc->redirecting, 0, sizeof(newbc->redirecting));
		newbc->capability = cc_record->redial.capability;
		newbc->hdlc = cc_record->redial.hdlc;
		newbc->sending_complete = 1;

		if (cc_record->ptp) {
			newbc->fac_out.Function = Fac_CCBS_T_Call;
			newbc->fac_out.u.CCBS_T_Call.InvokeID = ++misdn_invoke_id;
		} else {
			newbc->fac_out.Function = Fac_CCBSCall;
			newbc->fac_out.u.CCBSCall.InvokeID = ++misdn_invoke_id;
			newbc->fac_out.u.CCBSCall.CCBSReference = cc_record->mode.ptmp.reference_id;
		}
		AST_LIST_UNLOCK(&misdn_cc_records_db);

		ast_copy_string(ast->exten, newbc->dialed.number, sizeof(ast->exten));

		chan_misdn_log(1, port, "* Call completion to: %s\n", newbc->dialed.number);
		chan_misdn_log(2, port, " --> * tech:%s context:%s\n", ast->name, ast->context);
	} else
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	{
		/*
		 * dest is ---v
		 * Dial(mISDN/g:group_name[/extension[/options]])
		 * Dial(mISDN/port[:preselected_channel][/extension[/options]])
		 *
		 * The dial extension could be empty if you are using MISDN_KEYPAD
		 * to control ISDN provider features.
		 */
		dest_cp = ast_strdupa(dest);
		AST_NONSTANDARD_APP_ARGS(args, dest_cp, '/');
		if (!args.ext) {
			args.ext = "";
		}

		chan_misdn_log(1, port, "* CALL: %s\n", dest);
		chan_misdn_log(2, port, " --> * dialed:%s tech:%s context:%s\n", args.ext, ast->name, ast->context);

		ast_copy_string(ast->exten, args.ext, sizeof(ast->exten));
		ast_copy_string(newbc->dialed.number, args.ext, sizeof(newbc->dialed.number));

		if (ast_strlen_zero(newbc->caller.name)	&& !ast_strlen_zero(ast->connected.id.name)) {
			ast_copy_string(newbc->caller.name, ast->connected.id.name, sizeof(newbc->caller.name));
			chan_misdn_log(3, port, " --> * set caller:\"%s\" <%s>\n", newbc->caller.name, newbc->caller.number);
		}
		if (ast_strlen_zero(newbc->caller.number) && !ast_strlen_zero(ast->connected.id.number)) {
			ast_copy_string(newbc->caller.number, ast->connected.id.number, sizeof(newbc->caller.number));
			chan_misdn_log(3, port, " --> * set caller:\"%s\" <%s>\n", newbc->caller.name, newbc->caller.number);
		}

		misdn_cfg_get(port, MISDN_CFG_LOCALDIALPLAN, &number_type, sizeof(number_type));
		if (number_type < 0) {
			newbc->caller.number_type = ast_to_misdn_ton(ast->connected.id.number_type);
			newbc->caller.number_plan = ast_to_misdn_plan(ast->connected.id.number_type);
		} else {
			/* Force us to send in SETUP message */
			newbc->caller.number_type = number_type;
			newbc->caller.number_plan = NUMPLAN_ISDN;
		}
		debug_numtype(port, newbc->caller.number_type, "LTON");

		newbc->capability = ast->transfercapability;
		pbx_builtin_setvar_helper(ast, "TRANSFERCAPABILITY", ast_transfercapability2str(newbc->capability));
		if (ast->transfercapability == INFO_CAPABILITY_DIGITAL_UNRESTRICTED) {
			chan_misdn_log(2, port, " --> * Call with flag Digital\n");
		}

		/* update caller screening and presentation */
		update_config(ch);

		/* fill in some ies from channel dialplan variables */
		import_ch(ast, newbc, ch);

		/* Finally The Options Override Everything */
		if (!ast_strlen_zero(args.opts)) {
			misdn_set_opt_exec(ast, args.opts);
		} else {
			chan_misdn_log(2, port, "NO OPTS GIVEN\n");
		}
		if (newbc->set_presentation) {
			newbc->caller.presentation = newbc->presentation;
		}

		misdn_copy_redirecting_from_ast(newbc, ast);
		switch (newbc->outgoing_colp) {
		case 1:/* restricted */
		case 2:/* blocked */
			newbc->redirecting.from.presentation = 1;/* restricted */
			break;
		default:
			break;
		}
#if defined(AST_MISDN_ENHANCEMENTS)
		if (newbc->redirecting.from.number[0] && misdn_lib_is_ptp(port)) {
			if (newbc->redirecting.count < 1) {
				newbc->redirecting.count = 1;
			}

			/* Create DivertingLegInformation2 facility */
			newbc->fac_out.Function = Fac_DivertingLegInformation2;
			newbc->fac_out.u.DivertingLegInformation2.InvokeID = ++misdn_invoke_id;
			newbc->fac_out.u.DivertingLegInformation2.DivertingPresent = 1;
			misdn_PresentedNumberUnscreened_fill(
				&newbc->fac_out.u.DivertingLegInformation2.Diverting,
				&newbc->redirecting.from);
			switch (newbc->outgoing_colp) {
			case 2:/* blocked */
				/* Block the number going out */
				newbc->fac_out.u.DivertingLegInformation2.Diverting.Type = 1;/* presentationRestricted */

				/* Don't tell about any previous diversions or why for that matter. */
				newbc->fac_out.u.DivertingLegInformation2.DiversionCounter = 1;
				newbc->fac_out.u.DivertingLegInformation2.DiversionReason = 0;/* unknown */
				break;
			default:
				newbc->fac_out.u.DivertingLegInformation2.DiversionCounter =
					newbc->redirecting.count;
				newbc->fac_out.u.DivertingLegInformation2.DiversionReason =
					misdn_to_diversion_reason(newbc->redirecting.reason);
				break;
			}
			newbc->fac_out.u.DivertingLegInformation2.OriginalCalledPresent = 0;
			if (1 < newbc->fac_out.u.DivertingLegInformation2.DiversionCounter) {
				newbc->fac_out.u.DivertingLegInformation2.OriginalCalledPresent = 1;
				newbc->fac_out.u.DivertingLegInformation2.OriginalCalled.Type = 2;/* numberNotAvailableDueToInterworking */
			}

			/*
			 * Expect a DivertingLegInformation3 to update the COLR of the
			 * redirecting-to party we are attempting to call now.
			 */
			newbc->div_leg_3_rx_wanted = 1;
		}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

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
	}

	exceed = add_out_calls(port);
	if (exceed != 0) {
		char tmp[16];

		snprintf(tmp, sizeof(tmp), "%d", exceed);
		pbx_builtin_setvar_helper(ast, "MAX_OVERFLOW", tmp);
		ast->hangupcause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}

#if defined(AST_MISDN_ENHANCEMENTS)
	if (newbc->fac_out.Function != Fac_None) {
		print_facility(&newbc->fac_out, newbc);
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	r = misdn_lib_send_event(newbc, EVENT_SETUP);

	/** we should have l3id after sending setup **/
	ch->l3id = newbc->l3_id;

	if (r == -ENOCHAN) {
		chan_misdn_log(0, port, " --> * Theres no Channel at the moment .. !\n");
		chan_misdn_log(1, port, " --> * SEND: State Down pid:%d\n", newbc ? newbc->pid : -1);
		ast->hangupcause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
		ast_setstate(ast, AST_STATE_DOWN);
		return -1;
	}

	chan_misdn_log(2, port, " --> * SEND: State Dialing pid:%d\n", newbc ? newbc->pid : 1);

	ast_setstate(ast, AST_STATE_DIALING);
	ast->hangupcause = AST_CAUSE_NORMAL_CLEARING;

	if (newbc->nt) {
		stop_bc_tones(ch);
	}

	ch->state = MISDN_CALLING;

	return 0;
}


static int misdn_answer(struct ast_channel *ast)
{
	struct chan_list *p;
	const char *tmp;

	if (!ast || !(p = MISDN_ASTERISK_TECH_PVT(ast))) {
		return -1;
	}

	chan_misdn_log(1, p ? (p->bc ? p->bc->port : 0) : 0, "* ANSWER:\n");

	if (!p) {
		ast_log(LOG_WARNING, " --> Channel not connected ??\n");
		ast_queue_hangup_with_cause(ast, AST_CAUSE_NETWORK_OUT_OF_ORDER);
	}

	if (!p->bc) {
		chan_misdn_log(1, 0, " --> Got Answer, but there is no bc obj ??\n");

		ast_queue_hangup_with_cause(ast, AST_CAUSE_PROTOCOL_ERROR);
	}

	ast_channel_lock(ast);
	tmp = pbx_builtin_getvar_helper(ast, "CRYPT_KEY");
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
	ast_channel_unlock(ast);

	p->state = MISDN_CONNECTED;
	stop_indicate(p);

	if (ast_strlen_zero(p->bc->connected.number)) {
		chan_misdn_log(2,p->bc->port," --> empty connected number using dialed number\n");
		ast_copy_string(p->bc->connected.number, p->bc->dialed.number, sizeof(p->bc->connected.number));

		/*
		 * Use the misdn_set_opt() application to set the presentation
		 * before we answer or you can use the CONECTEDLINE() function
		 * to set everything before using the Answer() application.
		 */
		p->bc->connected.presentation = p->bc->presentation;
		p->bc->connected.screening = 0;	/* unscreened */
		p->bc->connected.number_type = p->bc->dialed.number_type;
		p->bc->connected.number_plan = p->bc->dialed.number_plan;
	}

	switch (p->bc->outgoing_colp) {
	case 1:/* restricted */
	case 2:/* blocked */
		p->bc->connected.presentation = 1;/* restricted */
		break;
	default:
		break;
	}

#if defined(AST_MISDN_ENHANCEMENTS)
	if (p->bc->div_leg_3_tx_pending) {
		p->bc->div_leg_3_tx_pending = 0;

		/* Send DivertingLegInformation3 */
		p->bc->fac_out.Function = Fac_DivertingLegInformation3;
		p->bc->fac_out.u.DivertingLegInformation3.InvokeID = ++misdn_invoke_id;
		p->bc->fac_out.u.DivertingLegInformation3.PresentationAllowedIndicator =
			(p->bc->connected.presentation == 0) ? 1 : 0;
		print_facility(&p->bc->fac_out, p->bc);
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	misdn_lib_send_event(p->bc, EVENT_CONNECT);
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
	char buf[2] = { digit, 0 };

	if (!ast || !(p = MISDN_ASTERISK_TECH_PVT(ast))) {
		return -1;
	}

	bc = p->bc;
	chan_misdn_log(1, bc ? bc->port : 0, "* IND : Digit %c\n", digit);

	if (!bc) {
		ast_log(LOG_WARNING, " --> !! Got Digit Event without having bchannel Object\n");
		return -1;
	}

	switch (p->state) {
	case MISDN_CALLING:
		if (strlen(bc->infos_pending) < sizeof(bc->infos_pending) - 1) {
			strncat(bc->infos_pending, buf, sizeof(bc->infos_pending) - strlen(bc->infos_pending) - 1);
		}
		break;
	case MISDN_CALLING_ACKNOWLEDGE:
		ast_copy_string(bc->info_dad, buf, sizeof(bc->info_dad));
		if (strlen(bc->dialed.number) < sizeof(bc->dialed.number) - 1) {
			strncat(bc->dialed.number, buf, sizeof(bc->dialed.number) - strlen(bc->dialed.number) - 1);
		}
		ast_copy_string(p->ast->exten, bc->dialed.number, sizeof(p->ast->exten));
		misdn_lib_send_event(bc, EVENT_INFORMATION);
		break;
	default:
		/* Do not send Digits in CONNECTED State, when
		 * the other side is also mISDN. */
		if (p->other_ch) {
			return 0;
		}

		if (bc->send_dtmf) {
			send_digit_to_chan(p, digit);
		}
		break;
	}

	return 0;
}


static int misdn_fixup(struct ast_channel *oldast, struct ast_channel *ast)
{
	struct chan_list *p;

	if (!ast || !(p = MISDN_ASTERISK_TECH_PVT(ast))) {
		return -1;
	}

	chan_misdn_log(1, p->bc ? p->bc->port : 0, "* IND: Got Fixup State:%s L3id:%x\n", misdn_get_ch_state(p), p->l3id);

	p->ast = ast;

	return 0;
}



static int misdn_indication(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct chan_list *p;

	if (!ast || !(p = MISDN_ASTERISK_TECH_PVT(ast))) {
		ast_log(LOG_WARNING, "Returned -1 in misdn_indication\n");
		return -1;
	}

	if (!p->bc) {
		if (p->hold.state == MISDN_HOLD_IDLE) {
			chan_misdn_log(1, 0, "* IND : Indication [%d] ignored on %s\n", cond,
				ast->name);
			ast_log(LOG_WARNING, "Private Pointer but no bc ?\n");
		} else {
			chan_misdn_log(1, 0, "* IND : Indication [%d] ignored on hold %s\n",
				cond, ast->name);
		}
		return -1;
	}

	chan_misdn_log(5, p->bc->port, "* IND : Indication [%d] on %s\n\n", cond, ast->name);

	switch (cond) {
	case AST_CONTROL_BUSY:
		chan_misdn_log(1, p->bc->port, "* IND :\tbusy pid:%d\n", p->bc->pid);
		ast_setstate(ast, AST_STATE_BUSY);

		p->bc->out_cause = AST_CAUSE_USER_BUSY;
		if (p->state != MISDN_CONNECTED) {
			start_bc_tones(p);
			misdn_lib_send_event(p->bc, EVENT_DISCONNECT);
		}
		return -1;
	case AST_CONTROL_RING:
		chan_misdn_log(1, p->bc->port, "* IND :\tring pid:%d\n", p->bc->pid);
		return -1;
	case AST_CONTROL_RINGING:
		chan_misdn_log(1, p->bc->port, "* IND :\tringing pid:%d\n", p->bc->pid);
		switch (p->state) {
		case MISDN_ALERTING:
			chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d but I was Ringing before, so ignoring it\n", p->bc->pid);
			break;
		case MISDN_CONNECTED:
			chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d but Connected, so just send TONE_ALERTING without state changes \n", p->bc->pid);
			return -1;
		default:
			p->state = MISDN_ALERTING;
			chan_misdn_log(2, p->bc->port, " --> * IND :\tringing pid:%d\n", p->bc->pid);
			misdn_lib_send_event(p->bc, EVENT_ALERTING);

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

			chan_misdn_log(3, p->bc->port, " --> * SEND: State Ring pid:%d\n", p->bc->pid);
			ast_setstate(ast, AST_STATE_RING);

			if (!p->bc->nt && (p->originator == ORG_MISDN) && !p->incoming_early_audio) {
				chan_misdn_log(2, p->bc->port, " --> incoming_early_audio off\n");
			} else {
				return -1;
			}
		}
		break;
	case AST_CONTROL_ANSWER:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tanswer pid:%d\n", p->bc->pid);
		start_bc_tones(p);
		break;
	case AST_CONTROL_TAKEOFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\ttakeoffhook pid:%d\n", p->bc->pid);
		return -1;
	case AST_CONTROL_OFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\toffhook pid:%d\n", p->bc->pid);
		return -1;
	case AST_CONTROL_FLASH:
		chan_misdn_log(1, p->bc->port, " --> *\tflash pid:%d\n", p->bc->pid);
		break;
	case AST_CONTROL_PROGRESS:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tprogress pid:%d\n", p->bc->pid);
		misdn_lib_send_event(p->bc, EVENT_PROGRESS);
		break;
	case AST_CONTROL_PROCEEDING:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tproceeding pid:%d\n", p->bc->pid);
		misdn_lib_send_event(p->bc, EVENT_PROCEEDING);
		break;
	case AST_CONTROL_CONGESTION:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tcongestion pid:%d\n", p->bc->pid);

		p->bc->out_cause = AST_CAUSE_SWITCH_CONGESTION;
		start_bc_tones(p);
		misdn_lib_send_event(p->bc, EVENT_DISCONNECT);

		if (p->bc->nt) {
			hanguptone_indicate(p);
		}
		break;
	case -1 :
		chan_misdn_log(1, p->bc->port, " --> * IND :\t-1! (stop indication) pid:%d\n", p->bc->pid);

		stop_indicate(p);

		if (p->state == MISDN_CONNECTED) {
			start_bc_tones(p);
		}
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, p->mohinterpret);
		chan_misdn_log(1, p->bc->port, " --> *\tHOLD pid:%d\n", p->bc->pid);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		chan_misdn_log(1, p->bc->port, " --> *\tUNHOLD pid:%d\n", p->bc->pid);
		break;
	case AST_CONTROL_CONNECTED_LINE:
		chan_misdn_log(1, p->bc->port, "* IND :\tconnected line update pid:%d\n", p->bc->pid);
		misdn_update_connected_line(ast, p->bc, p->originator);
		break;
	case AST_CONTROL_REDIRECTING:
		chan_misdn_log(1, p->bc->port, "* IND :\tredirecting info update pid:%d\n", p->bc->pid);
		misdn_update_redirecting(ast, p->bc, p->originator);
		break;
	default:
		chan_misdn_log(1, p->bc->port, " --> * Unknown Indication:%d pid:%d\n", cond, p->bc->pid);
		return -1;
	}

	return 0;
}

static int misdn_hangup(struct ast_channel *ast)
{
	struct chan_list *p;
	struct misdn_bchannel *bc;
	const char *var;

	if (!ast || !(p = MISDN_ASTERISK_TECH_PVT(ast))) {
		return -1;
	}
	MISDN_ASTERISK_TECH_PVT(ast) = NULL;

	ast_debug(1, "misdn_hangup(%s)\n", ast->name);

	if (p->hold.state == MISDN_HOLD_IDLE) {
		bc = p->bc;
	} else {
		p->hold.state = MISDN_HOLD_DISCONNECT;
		bc = misdn_lib_find_held_bc(p->hold.port, p->l3id);
		if (!bc) {
			chan_misdn_log(4, p->hold.port,
				"misdn_hangup: Could not find held bc for (%s)\n", ast->name);
			release_chan_early(p);
			return 0;
		}
	}

	if (ast->_state == AST_STATE_RESERVED || p->state == MISDN_NOTHING) {
		/* between request and call */
		ast_debug(1, "State Reserved (or nothing) => chanIsAvail\n");
		release_chan_early(p);
		if (bc) {
			misdn_lib_release(bc);
		}
		return 0;
	}
	if (!bc) {
		ast_log(LOG_WARNING, "Hangup with private but no bc ? state:%s l3id:%x\n",
			misdn_get_ch_state(p), p->l3id);
		release_chan_early(p);
		return 0;
	}

	p->ast = NULL;
	p->need_hangup = 0;
	p->need_queue_hangup = 0;
	p->need_busy = 0;

	if (!bc->nt) {
		stop_bc_tones(p);
	}

	bc->out_cause = ast->hangupcause ? ast->hangupcause : AST_CAUSE_NORMAL_CLEARING;

	ast_channel_lock(ast);
	var = pbx_builtin_getvar_helper(ast, "HANGUPCAUSE");
	if (!var) {
		var = pbx_builtin_getvar_helper(ast, "PRI_CAUSE");
	}
	if (var) {
		int tmpcause;

		tmpcause = atoi(var);
		bc->out_cause = tmpcause ? tmpcause : AST_CAUSE_NORMAL_CLEARING;
	}

	var = pbx_builtin_getvar_helper(ast, "MISDN_USERUSER");
	if (var) {
		ast_log(LOG_NOTICE, "MISDN_USERUSER: %s\n", var);
		ast_copy_string(bc->uu, var, sizeof(bc->uu));
		bc->uulen = strlen(bc->uu);
	}
	ast_channel_unlock(ast);

	chan_misdn_log(1, bc->port,
		"* IND : HANGUP\tpid:%d context:%s dialed:%s caller:\"%s\" <%s> State:%s\n",
		bc->pid,
		ast->context,
		ast->exten,
		ast->cid.cid_name ? ast->cid.cid_name : "",
		ast->cid.cid_num ? ast->cid.cid_num : "",
		misdn_get_ch_state(p));
	chan_misdn_log(3, bc->port, " --> l3id:%x\n", p->l3id);
	chan_misdn_log(3, bc->port, " --> cause:%d\n", bc->cause);
	chan_misdn_log(2, bc->port, " --> out_cause:%d\n", bc->out_cause);

	switch (p->state) {
	case MISDN_INCOMING_SETUP:
		/*
		 * This is the only place in misdn_hangup, where we
		 * can call release_chan, else it might create a lot of trouble.
		 */
		ast_log(LOG_NOTICE, "release channel, in INCOMING_SETUP state.. no other events happened\n");
		release_chan(p, bc);
		misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
		return 0;
	case MISDN_DIALING:
		if (p->hold.state == MISDN_HOLD_IDLE) {
			start_bc_tones(p);
			hanguptone_indicate(p);
		}

		if (bc->need_disconnect) {
			misdn_lib_send_event(bc, EVENT_DISCONNECT);
		}
		break;
	case MISDN_CALLING_ACKNOWLEDGE:
		if (p->hold.state == MISDN_HOLD_IDLE) {
			start_bc_tones(p);
			hanguptone_indicate(p);
		}

		if (bc->need_disconnect) {
			misdn_lib_send_event(bc, EVENT_DISCONNECT);
		}
		break;

	case MISDN_CALLING:
	case MISDN_ALERTING:
	case MISDN_PROGRESS:
	case MISDN_PROCEEDING:
		if (p->originator != ORG_AST && p->hold.state == MISDN_HOLD_IDLE) {
			hanguptone_indicate(p);
		}

		if (bc->need_disconnect) {
			misdn_lib_send_event(bc, EVENT_DISCONNECT);
		}
		break;
	case MISDN_CONNECTED:
		/*  Alerting or Disconnect */
		if (bc->nt && p->hold.state == MISDN_HOLD_IDLE) {
			start_bc_tones(p);
			hanguptone_indicate(p);
			bc->progress_indicator = INFO_PI_INBAND_AVAILABLE;
		}
		if (bc->need_disconnect) {
			misdn_lib_send_event(bc, EVENT_DISCONNECT);
		}
		break;
	case MISDN_DISCONNECTED:
		if (bc->need_release) {
			misdn_lib_send_event(bc, EVENT_RELEASE);
		}
		break;

	case MISDN_CLEANING:
		return 0;

	case MISDN_BUSY:
		break;
	default:
		if (bc->nt) {
			bc->out_cause = -1;
			if (bc->need_release) {
				misdn_lib_send_event(bc, EVENT_RELEASE);
			}
		} else {
			if (bc->need_disconnect) {
				misdn_lib_send_event(bc, EVENT_DISCONNECT);
			}
		}
		break;
	}

	p->state = MISDN_CLEANING;
	chan_misdn_log(3, bc->port, " --> Channel: %s hungup new state:%s\n", ast->name,
		misdn_get_ch_state(p));

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

	ast_debug(1, "Detected inband DTMF digit: %c\n", f->subclass);

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
 						ast_verb(3, "Redirecting %s to fax extension (context:%s)\n", ast->name, context);
  						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
  						pbx_builtin_setvar_helper(ast,"FAXEXTEN",ast->exten);
 						if (ast_async_goto(ast, context, "fax", 1)) {
 							ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, context);
						}
  					} else {
 						ast_log(LOG_NOTICE, "Fax detected but no fax extension, context:%s exten:%s\n", context, ast->exten);
					}
 				} else {
					ast_debug(1, "Already in a fax extension, not redirecting\n");
				}
 				break;
 			case 2:
 				ast_verb(3, "Not redirecting %s to fax extension, nojump is set.\n", ast->name);
 				break;
			default:
				break;
 			}
 		} else {
			ast_debug(1, "Fax already handled\n");
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
	struct timeval tv = { 0, 20000 };
	int len, t;

	if (!ast) {
		chan_misdn_log(1, 0, "misdn_read called without ast\n");
		return NULL;
	}
 	if (!(tmp = MISDN_ASTERISK_TECH_PVT(ast))) {
		chan_misdn_log(1, 0, "misdn_read called without ast->pvt\n");
		return NULL;
	}

	if (!tmp->bc && tmp->hold.state == MISDN_HOLD_IDLE) {
		chan_misdn_log(1, 0, "misdn_read called without bc\n");
		return NULL;
	}

	FD_ZERO(&rrfs);
	FD_SET(tmp->pipe[0], &rrfs);

	t = select(FD_SETSIZE, &rrfs, NULL, NULL, &tv);
	if (!t) {
		chan_misdn_log(3, tmp->bc->port, "read Select Timed out\n");
		len = 160;
	}

	if (t < 0) {
		chan_misdn_log(-1, tmp->bc->port, "Select Error (err=%s)\n", strerror(errno));
		return NULL;
	}

	if (FD_ISSET(tmp->pipe[0], &rrfs)) {
		len = read(tmp->pipe[0], tmp->ast_rd_buf, sizeof(tmp->ast_rd_buf));

		if (len <= 0) {
			/* we hangup here, since our pipe is closed */
			chan_misdn_log(2, tmp->bc->port, "misdn_read: Pipe closed, hanging up\n");
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
	tmp->frame.delivery = ast_tv(0, 0);
	tmp->frame.src = NULL;
	tmp->frame.data.ptr = tmp->ast_rd_buf;

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
		if (tmp->ast_dsp) {
			return process_ast_dsp(tmp, &tmp->frame);
		} else {
			return &tmp->frame;
		}
	}
}


static int misdn_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct chan_list *ch;
	int i  = 0;

	if (!ast || !(ch = MISDN_ASTERISK_TECH_PVT(ast))) {
		return -1;
	}

	if (ch->hold.state != MISDN_HOLD_IDLE) {
		chan_misdn_log(7, 0, "misdn_write: Returning because hold active\n");
		return 0;
	}

	if (!ch->bc) {
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


	if (!frame->samples) {
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

	if (!ch->bc->addr) {
		chan_misdn_log(8, ch->bc->port, "misdn_write: no addr for bc dropping:%d\n", frame->samples);
		return 0;
	}

#ifdef MISDN_DEBUG
	{
		int i;
		int max = 5 > frame->samples ? frame->samples : 5;

		ast_debug(1, "write2mISDN %p %d bytes: ", p, frame->samples);

		for (i = 0; i < max; i++) {
			ast_debug(1, "%2.2x ", ((char *) frame->data.ptr)[i]);
		}
	}
#endif

	switch (ch->bc->bc_state) {
	case BCHAN_ACTIVATED:
	case BCHAN_BRIDGED:
		break;
	default:
		if (!ch->dropped_frame_cnt) {
			chan_misdn_log(5, ch->bc->port,
				"BC not active (nor bridged) dropping: %d frames addr:%x exten:%s cid:%s ch->state:%s bc_state:%d l3id:%x\n",
				frame->samples, ch->bc->addr, ast->exten, ast->cid.cid_num, misdn_get_ch_state(ch), ch->bc->bc_state, ch->bc->l3_id);
		}

		if (++ch->dropped_frame_cnt > 100) {
			ch->dropped_frame_cnt = 0;
			chan_misdn_log(5, ch->bc->port, "BC not active (nor bridged) dropping: %d frames addr:%x  dropped > 100 frames!\n", frame->samples, ch->bc->addr);
		}

		return 0;
	}

	chan_misdn_log(9, ch->bc->port, "Sending :%d bytes to MISDN\n", frame->samples);
	if (!ch->bc->nojitter && misdn_cap_is_speech(ch->bc->capability)) {
		/* Buffered Transmit (triggered by read from isdn side)*/
		if (misdn_jb_fill(ch->jb, frame->data.ptr, frame->samples) < 0) {
			if (ch->bc->active) {
				cb_log(0, ch->bc->port, "Misdn Jitterbuffer Overflow.\n");
			}
		}

	} else {
		/* transmit without jitterbuffer */
		i = misdn_lib_tx2misdn_frm(ch->bc, frame->data.ptr, frame->samples);
	}

	return 0;
}

static enum ast_bridge_result misdn_bridge(struct ast_channel *c0,
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

	if (!(ch1 && ch2)) {
		return -1;
	}

	misdn_cfg_get(ch1->bc->port, MISDN_CFG_BRIDGING, &p1_b, sizeof(p1_b));
	misdn_cfg_get(ch2->bc->port, MISDN_CFG_BRIDGING, &p2_b, sizeof(p2_b));

	if (! p1_b || ! p2_b) {
		ast_log(LOG_NOTICE, "Falling back to Asterisk bridging\n");
		return AST_BRIDGE_FAILED;
	}

	misdn_cfg_get(0, MISDN_GEN_BRIDGING, &bridging, sizeof(bridging));
	if (bridging) {
		/* trying to make a mISDN_dsp conference */
		chan_misdn_log(1, ch1->bc->port, "I SEND: Making conference with Number:%d\n", ch1->bc->pid + 1);
		misdn_lib_bridge(ch1->bc, ch2->bc);
	}

	ast_verb(3, "Native bridging %s and %s\n", c0->name, c1->name);

	chan_misdn_log(1, ch1->bc->port, "* Making Native Bridge between \"%s\" <%s> and \"%s\" <%s>\n",
		ch1->bc->caller.name,
		ch1->bc->caller.number,
		ch2->bc->caller.name,
		ch2->bc->caller.number);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0)) {
		ch1->ignore_dtmf = 1;
	}

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1)) {
		ch2->ignore_dtmf = 1;
	}

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

			if (!f) {
				chan_misdn_log(4, ch1->bc->port, "Read Null Frame\n");
			} else {
				chan_misdn_log(4, ch1->bc->port, "Read Frame Control class:%d\n", f->subclass);
			}

			*fo = f;
			*rc = who;
			break;
		}

		if (f->frametype == AST_FRAME_DTMF) {
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

		ast_write((who == c0) ? c1 : c0, f);
	}

	chan_misdn_log(1, ch1->bc->port, "I SEND: Splitting conference with Number:%d\n", ch1->bc->pid + 1);

	misdn_lib_split_bridge(ch1->bc, ch2->bc);

	return AST_BRIDGE_COMPLETE;
}

/** AST INDICATIONS END **/

static int dialtone_indicate(struct chan_list *cl)
{
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

	cl->ts = ast_get_indication_tone(ast->zone, "dial");

	if (cl->ts) {
		cl->notxtone = 0;
		cl->norxtone = 0;
		/* This prods us in misdn_write */
		ast_playtones_start(ast, 0, cl->ts->data, 0);
	}

	return 0;
}

static void hanguptone_indicate(struct chan_list *cl)
{
	misdn_lib_send_tone(cl->bc, TONE_HANGUP);
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

	if (cl->ts) {
		cl->ts = ast_tone_zone_sound_unref(cl->ts);
	}

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
	if (!cl) {
		return -1;
	}

	cl->notxtone = 1;
	cl->norxtone = 1;

	return 0;
}


static struct chan_list *init_chan_list(int orig)
{
	struct chan_list *cl;

	cl = ast_calloc(1, sizeof(*cl));
	if (!cl) {
		chan_misdn_log(-1, 0, "misdn_request: malloc failed!");
		return NULL;
	}

	cl->originator = orig;
	cl->need_queue_hangup = 1;
	cl->need_hangup = 1;
	cl->need_busy = 1;
	cl->overlap_dial_task = -1;
#if defined(AST_MISDN_ENHANCEMENTS)
	cl->record_id = -1;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	return cl;
}

static struct ast_channel *misdn_request(const char *type, int format, const struct ast_channel *requestor, void *data, int *cause)
{
	struct ast_channel *ast;
	char group[BUFFERSIZE + 1] = "";
	char dial_str[128];
	char *dest_cp;
	char *p = NULL;
	int channel = 0;
	int port = 0;
	struct misdn_bchannel *newbc = NULL;
	int dec = 0;
#if defined(AST_MISDN_ENHANCEMENTS)
	int cc_retry_call = 0;	/* TRUE if this is a call completion retry call */
	long record_id = -1;
	struct misdn_cc_record *cc_record;
	const char *err_msg;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	struct chan_list *cl;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(intf);	/* interface token */
		AST_APP_ARG(ext);	/* extension token */
		AST_APP_ARG(opts);	/* options token */
	);

	snprintf(dial_str, sizeof(dial_str), "%s/%s", misdn_type, (char *) data);

	/*
	 * data is ---v
	 * Dial(mISDN/g:group_name[/extension[/options]])
	 * Dial(mISDN/port[:preselected_channel][/extension[/options]])
	 * Dial(mISDN/cc/cc-record-id)
	 *
	 * The dial extension could be empty if you are using MISDN_KEYPAD
	 * to control ISDN provider features.
	 */
	dest_cp = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, dest_cp, '/');
	if (!args.ext) {
		args.ext = "";
	}

	if (!ast_strlen_zero(args.intf)) {
		if (args.intf[0] == 'g' && args.intf[1] == ':') {
			/* We make a group call lets checkout which ports are in my group */
			args.intf += 2;
			ast_copy_string(group, args.intf, sizeof(group));
			chan_misdn_log(2, 0, " --> Group Call group: %s\n", group);
#if defined(AST_MISDN_ENHANCEMENTS)
		} else if (strcmp(args.intf, "cc") == 0) {
			cc_retry_call = 1;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
		} else if ((p = strchr(args.intf, ':'))) {
			/* we have a preselected channel */
			*p++ = 0;
			channel = atoi(p);
			port = atoi(args.intf);
			chan_misdn_log(2, port, " --> Call on preselected Channel (%d).\n", channel);
		} else {
			port = atoi(args.intf);
		}
	} else {
		ast_log(LOG_WARNING, " --> ! IND : Dial(%s) WITHOUT Port or Group, check extensions.conf\n", dial_str);
		return NULL;
	}

#if defined(AST_MISDN_ENHANCEMENTS)
	if (cc_retry_call) {
		if (ast_strlen_zero(args.ext)) {
			ast_log(LOG_WARNING, " --> ! IND : Dial(%s) WITHOUT cc-record-id, check extensions.conf\n", dial_str);
			return NULL;
		}
		if (!isdigit(*args.ext)) {
			ast_log(LOG_WARNING, " --> ! IND : Dial(%s) cc-record-id must be a number.\n", dial_str);
			return NULL;
		}
		record_id = atol(args.ext);

		AST_LIST_LOCK(&misdn_cc_records_db);
		cc_record = misdn_cc_find_by_id(record_id);
		if (!cc_record) {
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			err_msg = misdn_cc_record_not_found;
			ast_log(LOG_WARNING, " --> ! IND : Dial(%s) %s.\n", dial_str, err_msg);
			return NULL;
		}
		if (!cc_record->activated) {
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			err_msg = "Call completion has not been activated";
			ast_log(LOG_WARNING, " --> ! IND : Dial(%s) %s.\n", dial_str, err_msg);
			return NULL;
		}
		port = cc_record->port;
		AST_LIST_UNLOCK(&misdn_cc_records_db);
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

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
			int robin_channel = rr->channel;
			int port_start;
			int next_chan = 1;

			do {
				port_start = 0;
				for (port = misdn_cfg_get_next_port_spin(rr->port); port > 0 && port != port_start;
					 port = misdn_cfg_get_next_port_spin(port)) {

					if (!port_start) {
						port_start = port;
					}

					if (port >= port_start) {
						next_chan = 1;
					}

					if (port <= port_start && next_chan) {
						int maxbchans = misdn_lib_get_maxchans(port);

						if (++robin_channel >= maxbchans) {
							robin_channel = 1;
						}
						next_chan = 0;
					}

					misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, sizeof(cfg_group));

					if (!strcasecmp(cfg_group, group)) {
						int port_up;
						int check;

						misdn_cfg_get(port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(check));
						port_up = misdn_lib_port_up(port, check);

						if (check && !port_up) {
							chan_misdn_log(1, port, "L1 is not Up on this Port\n");
						}

						if (check && port_up < 0) {
							ast_log(LOG_WARNING, "This port (%d) is blocked\n", port);
						}

						if (port_up > 0)	{
							newbc = misdn_lib_get_free_bc(port, robin_channel, 0, 0);
							if (newbc) {
								chan_misdn_log(4, port, " Success! Found port:%d channel:%d\n", newbc->port, newbc->channel);
								if (port_up) {
									chan_misdn_log(4, port, "portup:%d\n",  port_up);
								}
								rr->port = newbc->port;
								rr->channel = newbc->channel;
								break;
							}
						}
					}
				}
			} while (!newbc && robin_channel != rr->channel);
		} else {
			for (port = misdn_cfg_get_next_port(0); port > 0;
				port = misdn_cfg_get_next_port(port)) {
				misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, sizeof(cfg_group));

				chan_misdn_log(3, port, "Group [%s] Port [%d]\n", group, port);
				if (!strcasecmp(cfg_group, group)) {
					int port_up;
					int check;

					misdn_cfg_get(port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(check));
					port_up = misdn_lib_port_up(port, check);

					chan_misdn_log(4, port, "portup:%d\n", port_up);

					if (port_up > 0) {
						newbc = misdn_lib_get_free_bc(port, 0, 0, dec);
						if (newbc) {
							break;
						}
					}
				}
			}
		}

		/* Group dial failed ?*/
		if (!newbc) {
			ast_log(LOG_WARNING,
				"Could not Dial out on group '%s'.\n"
				"\tEither the L2 and L1 on all of these ports where DOWN (see 'show application misdn_check_l2l1')\n"
				"\tOr there was no free channel on none of the ports\n\n",
				group);
			return NULL;
		}
	} else {
		/* 'Normal' Port dial * Port dial */
		if (channel) {
			chan_misdn_log(1, port, " --> preselected_channel: %d\n", channel);
		}
		newbc = misdn_lib_get_free_bc(port, channel, 0, dec);
		if (!newbc) {
			ast_log(LOG_WARNING, "Could not create channel on port:%d for Dial(%s)\n", port, dial_str);
			return NULL;
		}
	}

	/* create ast_channel and link all the objects together */
	cl = init_chan_list(ORG_AST);
	if (!cl) {
		ast_log(LOG_ERROR, "Could not create call record for Dial(%s)\n", dial_str);
		return NULL;
	}
	cl->bc = newbc;

	ast = misdn_new(cl, AST_STATE_RESERVED, args.ext, NULL, format, requestor ? requestor->linkedid : NULL, port, channel);
	if (!ast) {
		ast_free(cl);
		ast_log(LOG_ERROR, "Could not create Asterisk channel for Dial(%s)\n", dial_str);
		return NULL;
	}
	cl->ast = ast;

#if defined(AST_MISDN_ENHANCEMENTS)
	cl->record_id = record_id;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	/* register chan in local list */
	cl_queue_chan(&cl_te, cl);

	/* fill in the config into the objects */
	read_config(cl);

	/* important */
	cl->need_hangup = 0;

	return ast;
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
	.type = misdn_type,
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
	.properties = 0,
};

static struct ast_channel_tech misdn_tech_wo_bridge = {
	.type = misdn_type,
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
	.properties = 0,
};


static int glob_channel = 0;

static void update_name(struct ast_channel *tmp, int port, int c)
{
	int chan_offset = 0;
	int tmp_port = misdn_cfg_get_next_port(0);
	char newname[255];

	for (; tmp_port > 0; tmp_port = misdn_cfg_get_next_port(tmp_port)) {
		if (tmp_port == port) {
			break;
		}
		chan_offset += misdn_lib_port_is_pri(tmp_port) ? 30 : 2;
	}
	if (c < 0) {
		c = 0;
	}

	snprintf(newname, sizeof(newname), "%s/%d-", misdn_type, chan_offset + c);
	if (strncmp(tmp->name, newname, strlen(newname))) {
		snprintf(newname, sizeof(newname), "%s/%d-u%d", misdn_type, chan_offset + c, glob_channel++);
		ast_channel_lock(tmp);
		ast_change_name(tmp, newname);
		ast_channel_unlock(tmp);
		chan_misdn_log(3, port, " --> updating channel name to [%s]\n", tmp->name);
	}
}

static struct ast_channel *misdn_new(struct chan_list *chlist, int state,  char *exten, char *callerid, int format, const char *linkedid, int port, int c)
{
	struct ast_channel *tmp;
	char *cid_name = 0, *cid_num = 0;
	int chan_offset = 0;
	int tmp_port = misdn_cfg_get_next_port(0);
	int bridging;

	for (; tmp_port > 0; tmp_port = misdn_cfg_get_next_port(tmp_port)) {
		if (tmp_port == port) {
			break;
		}
		chan_offset += misdn_lib_port_is_pri(tmp_port) ? 30 : 2;
	}
	if (c < 0) {
		c = 0;
	}

	if (callerid) {
		ast_callerid_parse(callerid, &cid_name, &cid_num);
	}

	tmp = ast_channel_alloc(1, state, cid_num, cid_name, "", exten, "", linkedid, 0, "%s/%s%d-u%d", misdn_type, c ? "" : "tmp", chan_offset + c, glob_channel++);
	if (tmp) {
		chan_misdn_log(2, 0, " --> * NEW CHANNEL dialed:%s caller:%s\n", exten, callerid);

		tmp->nativeformats = prefformat;

		tmp->readformat = format;
		tmp->rawreadformat = format;
		tmp->writeformat = format;
		tmp->rawwriteformat = format;

		tmp->tech_pvt = chlist;

		misdn_cfg_get(0, MISDN_GEN_BRIDGING, &bridging, sizeof(bridging));
		tmp->tech = bridging ? &misdn_tech : &misdn_tech_wo_bridge;

		tmp->writeformat = format;
		tmp->readformat = format;
		tmp->priority = 1;

		if (exten) {
			ast_copy_string(tmp->exten, exten, sizeof(tmp->exten));
		} else {
			chan_misdn_log(1, 0, "misdn_new: no exten given.\n");
		}

		if (callerid) {
			/* Don't use ast_set_callerid() here because it will
			 * generate a needless NewCallerID event */
			tmp->cid.cid_ani = ast_strdup(cid_num);
		}

		if (pipe(chlist->pipe) < 0) {
			ast_log(LOG_ERROR, "Pipe failed\n");
		}
		ast_channel_set_fd(tmp, 0, chlist->pipe[0]);

		tmp->rings = (state == AST_STATE_RING) ? 1 : 0;

		ast_jb_configure(tmp, misdn_get_global_jbconf());
	} else {
		chan_misdn_log(-1, 0, "Unable to allocate channel structure\n");
	}

	return tmp;
}

static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help = list;

	for (; help; help = help->next) {
		if (help->bc == bc) {
			return help;
		}
	}

	chan_misdn_log(6, bc->port,
		"$$$ find_chan_by_bc: No channel found for dialed:%s caller:\"%s\" <%s>\n",
		bc->dialed.number,
		bc->caller.name,
		bc->caller.number);

	return NULL;
}

static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid)
{
	struct chan_list *help = list;

	for (; help; help = help->next) {
		if (help->bc && (help->bc->pid == pid)) {
			return help;
		}
	}

	chan_misdn_log(6, 0, "$$$ find_chan_by_pid: No channel found for pid:%d\n", pid);

	return NULL;
}

static struct chan_list *find_hold_call(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help = list;

	if (bc->pri) {
		return NULL;
	}

	chan_misdn_log(6, bc->port, "$$$ find_hold_call: channel:%d dialed:%s caller:\"%s\" <%s>\n",
		bc->channel,
		bc->dialed.number,
		bc->caller.name,
		bc->caller.number);
	for (; help; help = help->next) {
		chan_misdn_log(4, bc->port, "$$$ find_hold_call: --> hold:%d channel:%d\n", help->hold.state, help->hold.channel);
		if (help->hold.state == MISDN_HOLD_ACTIVE && help->hold.port == bc->port) {
			return help;
		}
	}
	chan_misdn_log(6, bc->port,
		"$$$ find_hold_call: No channel found for dialed:%s caller:\"%s\" <%s>\n",
		bc->dialed.number,
		bc->caller.name,
		bc->caller.number);

	return NULL;
}


static struct chan_list *find_hold_call_l3(struct chan_list *list, unsigned long l3_id)
{
	struct chan_list *help = list;

	for (; help; help = help->next) {
		if (help->hold.state != MISDN_HOLD_IDLE && help->l3id == l3_id) {
			return help;
		}
	}

	return NULL;
}

#define TRANSFER_ON_HELD_CALL_HANGUP 1
#if defined(TRANSFER_ON_HELD_CALL_HANGUP)
/*!
 * \internal
 * \brief Find a suitable active call to go with a held call so we could try a transfer.
 *
 * \param list Channel list.
 * \param bc B channel record.
 *
 * \return Found call record or NULL.
 *
 * \note There could be a possibility where we find the wrong active call to transfer.
 * This concern is mitigated by the fact that there could be at most one other call
 * on a PTMP BRI link to another device.  Maybe the l3_id could help in locating an
 * active call on the same TEI?
 */
static struct chan_list *find_hold_active_call(struct chan_list *list, struct misdn_bchannel *bc)
{
	for (; list; list = list->next) {
		if (list->hold.state == MISDN_HOLD_IDLE && list->bc && list->bc->port == bc->port
			&& list->ast) {
			switch (list->state) {
			case MISDN_PROCEEDING:
			case MISDN_PROGRESS:
			case MISDN_ALERTING:
			case MISDN_CONNECTED:
				return list;
			default:
				break;
			}
		}
	}
	return NULL;
}
#endif	/* defined(TRANSFER_ON_HELD_CALL_HANGUP) */

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

	if (chan->dsp) {
		ast_dsp_free(chan->dsp);
	}
	if (chan->trans) {
		ast_translator_free_path(chan->trans);
	}

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

	ch->need_hangup = (ret >= 0) ? 0 : 1;

	return ret;
}

static void hangup_chan(struct chan_list *ch, struct misdn_bchannel *bc)
{
	int port;

	if (!ch) {
		cb_log(1, 0, "Cannot hangup chan, no ch\n");
		return;
	}

	port = bc->port;
	cb_log(5, port, "hangup_chan called\n");

	if (ch->need_hangup) {
		cb_log(2, port, " --> hangup\n");
		ch->need_hangup = 0;
		ch->need_queue_hangup = 0;
		if (ch->ast) {
			send_cause2ast(ch->ast, bc, ch);
			ast_hangup(ch->ast);
		}
		return;
	}

	if (!ch->need_queue_hangup) {
		cb_log(2, port, " --> No need to queue hangup\n");
	}

	ch->need_queue_hangup = 0;
	if (ch->ast) {
		send_cause2ast(ch->ast, bc, ch);
		ast_queue_hangup_with_cause(ch->ast, bc->cause);
		cb_log(2, port, " --> queue_hangup\n");
	} else {
		cb_log(1, port, "Cannot hangup chan, no ast\n");
	}
}

/*!
 * \internal
 * \brief ISDN asked us to release channel, pendant to misdn_hangup.
 *
 * \param ch Call channel record to release.
 * \param bc Current B channel record associated with ch.
 *
 * \return Nothing
 *
 * \note ch must not be referenced after calling.
 */
static void release_chan(struct chan_list *ch, struct misdn_bchannel *bc)
{
	struct ast_channel *ast;

	ch->state = MISDN_CLEANING;

	ast_mutex_lock(&release_lock);

#if defined(AST_MISDN_ENHANCEMENTS)
	if (ch->peer) {
		ao2_ref(ch->peer, -1);
		ch->peer = NULL;
	}
#endif /* AST_MISDN_ENHANCEMENTS */

	cl_dequeue_chan(&cl_te, ch);

	chan_misdn_log(5, bc->port, "release_chan: bc with pid:%d l3id: %x\n", bc->pid, bc->l3_id);

	/* releasing jitterbuffer */
	if (ch->jb) {
		misdn_jb_destroy(ch->jb);
		ch->jb = NULL;
	} else {
		if (!bc->nojitter) {
			chan_misdn_log(5, bc->port, "Jitterbuffer already destroyed.\n");
		}
	}

	if (ch->overlap_dial) {
		if (ch->overlap_dial_task != -1) {
			misdn_tasks_remove(ch->overlap_dial_task);
			ch->overlap_dial_task = -1;
		}
		ast_mutex_destroy(&ch->overlap_tv_lock);
	}

	if (ch->originator == ORG_AST) {
		--misdn_out_calls[bc->port];
	} else {
		--misdn_in_calls[bc->port];
	}

	close(ch->pipe[0]);
	close(ch->pipe[1]);

	ast = ch->ast;
	if (ast) {
		MISDN_ASTERISK_TECH_PVT(ast) = NULL;
		chan_misdn_log(1, bc->port,
			"* RELEASING CHANNEL pid:%d context:%s dialed:%s caller:\"%s\" <%s>\n",
			bc->pid,
			ast->context,
			ast->exten,
			ast->cid.cid_name ? ast->cid.cid_name : "",
			ast->cid.cid_num ? ast->cid.cid_num : "");

		if (ast->_state != AST_STATE_RESERVED) {
			chan_misdn_log(3, bc->port, " --> Setting AST State to down\n");
			ast_setstate(ast, AST_STATE_DOWN);
		}
	}

	ast_free(ch);

	ast_mutex_unlock(&release_lock);
}

/*!
 * \internal
 * \brief Do everything in release_chan() that makes sense without a bc.
 *
 * \param ch Call channel record to release.
 *
 * \return Nothing
 *
 * \note ch must not be referenced after calling.
 */
static void release_chan_early(struct chan_list *ch)
{
	struct ast_channel *ast;

	ch->state = MISDN_CLEANING;

	ast_mutex_lock(&release_lock);

#if defined(AST_MISDN_ENHANCEMENTS)
	if (ch->peer) {
		ao2_ref(ch->peer, -1);
		ch->peer = NULL;
	}
#endif /* AST_MISDN_ENHANCEMENTS */

	cl_dequeue_chan(&cl_te, ch);

	/* releasing jitterbuffer */
	if (ch->jb) {
		misdn_jb_destroy(ch->jb);
		ch->jb = NULL;
	}

	if (ch->overlap_dial) {
		if (ch->overlap_dial_task != -1) {
			misdn_tasks_remove(ch->overlap_dial_task);
			ch->overlap_dial_task = -1;
		}
		ast_mutex_destroy(&ch->overlap_tv_lock);
	}

	if (ch->hold.state != MISDN_HOLD_IDLE) {
		if (ch->originator == ORG_AST) {
			--misdn_out_calls[ch->hold.port];
		} else {
			--misdn_in_calls[ch->hold.port];
		}
	}

	close(ch->pipe[0]);
	close(ch->pipe[1]);

	ast = ch->ast;
	if (ast) {
		MISDN_ASTERISK_TECH_PVT(ast) = NULL;
		if (ast->_state != AST_STATE_RESERVED) {
			ast_setstate(ast, AST_STATE_DOWN);
		}
	}

	ast_free(ch);

	ast_mutex_unlock(&release_lock);
}

/*!
 * \internal
 * \brief Attempt to transfer the active channel party to the held channel party.
 *
 * \param active_ch Channel currently connected.
 * \param held_ch Channel currently on hold.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_attempt_transfer(struct chan_list *active_ch, struct chan_list *held_ch)
{
	int retval;
	struct ast_channel *bridged;

	switch (active_ch->state) {
	case MISDN_PROCEEDING:
	case MISDN_PROGRESS:
	case MISDN_ALERTING:
	case MISDN_CONNECTED:
		break;
	default:
		return -1;
	}

	bridged = ast_bridged_channel(held_ch->ast);
	if (bridged) {
		ast_queue_control(held_ch->ast, AST_CONTROL_UNHOLD);
		held_ch->hold.state = MISDN_HOLD_TRANSFER;

		chan_misdn_log(1, held_ch->hold.port, "TRANSFERRING %s to %s\n",
			held_ch->ast->name, active_ch->ast->name);
		retval = ast_channel_masquerade(active_ch->ast, bridged);
	} else {
		/*
		 * Could not transfer.  Held channel is not bridged anymore.
		 * Held party probably got tired of waiting and hung up.
		 */
		retval = -1;
	}

	return retval;
}


static void do_immediate_setup(struct misdn_bchannel *bc, struct chan_list *ch, struct ast_channel *ast)
{
	char *predial;
	struct ast_frame fr;

	predial = ast_strdupa(ast->exten);

	ch->state = MISDN_DIALING;

	if (!ch->noautorespond_on_setup) {
		if (bc->nt) {
			misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE);
		} else {
			if (misdn_lib_is_ptp(bc->port)) {
				misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE);
			} else {
				misdn_lib_send_event(bc, EVENT_PROCEEDING);
			}
		}
	} else {
		ch->state = MISDN_INCOMING_SETUP;
	}

	chan_misdn_log(1, bc->port,
		"* Starting Ast context:%s dialed:%s caller:\"%s\" <%s> with 's' extension\n",
		ast->context,
		ast->exten,
		ast->cid.cid_name ? ast->cid.cid_name : "",
		ast->cid.cid_num ? ast->cid.cid_num : "");

	strcpy(ast->exten, "s");

	if (!ast_canmatch_extension(ast, ast->context, ast->exten, 1, bc->caller.number) || pbx_start_chan(ch) < 0) {
		ast = NULL;
		bc->out_cause = AST_CAUSE_UNALLOCATED;
		hangup_chan(ch, bc);
		hanguptone_indicate(ch);

		misdn_lib_send_event(bc, bc->nt ? EVENT_RELEASE_COMPLETE : EVENT_DISCONNECT);
	}


	while (!ast_strlen_zero(predial)) {
		fr.frametype = AST_FRAME_DTMF;
		fr.subclass = *predial;
		fr.src = NULL;
		fr.data.ptr = NULL;
		fr.datalen = 0;
		fr.samples = 0;
		fr.mallocd = 0;
		fr.offset = 0;
		fr.delivery = ast_tv(0,0);

		if (ch->ast && MISDN_ASTERISK_TECH_PVT(ch->ast)) {
			ast_queue_frame(ch->ast, &fr);
		}
		predial++;
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

	ast_channel_lock(chan);
	tmp = pbx_builtin_getvar_helper(chan, "MISDN_PID");
	if (tmp) {
		ch->other_pid = atoi(tmp);
		chan_misdn_log(3, bc->port, " --> IMPORT_PID: importing pid:%s\n", tmp);
		if (ch->other_pid > 0) {
			ch->other_ch = find_chan_by_pid(cl_te, ch->other_pid);
			if (ch->other_ch) {
				ch->other_ch->other_ch = ch;
			}
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
	ast_channel_unlock(chan);
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

	if (bc->uulen) {
		pbx_builtin_setvar_helper(chan, "MISDN_USERUSER", bc->uu);
	}

	if (!ast_strlen_zero(bc->keypad)) {
		pbx_builtin_setvar_helper(chan, "MISDN_KEYPAD", bc->keypad);
	}
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

static void start_pbx(struct chan_list *ch, struct misdn_bchannel *bc, struct ast_channel *chan)
{
	if (pbx_start_chan(ch) < 0) {
		hangup_chan(ch, bc);
		chan_misdn_log(-1, bc->port, "ast_pbx_start returned <0 in SETUP\n");
		if (bc->nt) {
			hanguptone_indicate(ch);
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
		} else {
			misdn_lib_send_event(bc, EVENT_RELEASE);
		}
	}
}

static void wait_for_digits(struct chan_list *ch, struct misdn_bchannel *bc, struct ast_channel *chan)
{
	ch->state = MISDN_WAITING4DIGS;
	misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE);
	if (bc->nt && !bc->dialed.number[0]) {
		dialtone_indicate(ch);
	}
}

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Handle the FACILITY CCBSStatusRequest message.
 *
 * \param port Logical port number.
 * \param facility Facility ie contents.
 *
 * \return Nothing
 */
static void misdn_cc_handle_ccbs_status_request(int port, const struct FacParm *facility)
{
	struct misdn_cc_record *cc_record;
	struct misdn_bchannel dummy;

	switch (facility->u.CCBSStatusRequest.ComponentType) {
	case FacComponent_Invoke:
		/* Build message */
		misdn_make_dummy(&dummy, port, 0, misdn_lib_port_is_nt(port), 0);
		dummy.fac_out.Function = Fac_CCBSStatusRequest;
		dummy.fac_out.u.CCBSStatusRequest.InvokeID = facility->u.CCBSStatusRequest.InvokeID;
		dummy.fac_out.u.CCBSStatusRequest.ComponentType = FacComponent_Result;

		/* Answer User-A free question */
		AST_LIST_LOCK(&misdn_cc_records_db);
		cc_record = misdn_cc_find_by_reference(port, facility->u.CCBSStatusRequest.Component.Invoke.CCBSReference);
		if (cc_record) {
			dummy.fac_out.u.CCBSStatusRequest.Component.Result.Free = cc_record->party_a_free;
		} else {
			/* No record so say User-A is free */
			dummy.fac_out.u.CCBSStatusRequest.Component.Result.Free = 1;
		}
		AST_LIST_UNLOCK(&misdn_cc_records_db);

		/* Send message */
		print_facility(&dummy.fac_out, &dummy);
		misdn_lib_send_event(&dummy, EVENT_FACILITY);
		break;

	default:
		chan_misdn_log(0, port, " --> not yet handled: facility type:0x%04X\n", facility->Function);
		break;
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Start a PBX to notify that User-B is available.
 *
 * \param record_id Call completion record ID
 * \param notify Dialplan location to start processing.
 *
 * \return Nothing
 */
static void misdn_cc_pbx_notify(long record_id, const struct misdn_cc_notify *notify)
{
	struct ast_channel *chan;
	char id_str[32];

	static unsigned short sequence = 0;

	/* Create a channel to notify with */
	snprintf(id_str, sizeof(id_str), "%ld", record_id);
	chan = ast_channel_alloc(0, AST_STATE_DOWN, id_str, NULL, NULL,
		notify->exten, notify->context, NULL, 0,
		"mISDN-CC/%ld-%X", record_id, (unsigned) ++sequence);
	if (!chan) {
		ast_log(LOG_ERROR, "Unable to allocate channel!\n");
		return;
	}
	chan->priority = notify->priority;
	if (chan->cid.cid_dnid) {
		ast_free(chan->cid.cid_dnid);
	}
	chan->cid.cid_dnid = ast_strdup(notify->exten);

	if (ast_pbx_start(chan)) {
		ast_log(LOG_WARNING, "Unable to start pbx channel %s!\n", chan->name);
		ast_channel_release(chan);
	} else {
		ast_verb(1, "Started pbx for call completion notify channel %s\n", chan->name);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Handle the FACILITY CCBS_T_RemoteUserFree message.
 *
 * \param bc B channel control structure message came in on
 *
 * \return Nothing
 */
static void misdn_cc_handle_T_remote_user_free(struct misdn_bchannel *bc)
{
	struct misdn_cc_record *cc_record;
	struct misdn_cc_notify notify;
	long record_id;

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_bc(bc);
	if (cc_record) {
		if (cc_record->party_a_free) {
			notify = cc_record->remote_user_free;
		} else {
			/* Send CCBS_T_Suspend message */
			bc->fac_out.Function = Fac_CCBS_T_Suspend;
			bc->fac_out.u.CCBS_T_Suspend.InvokeID = ++misdn_invoke_id;
			print_facility(&bc->fac_out, bc);
			misdn_lib_send_event(bc, EVENT_FACILITY);

			notify = cc_record->b_free;
		}
		record_id = cc_record->record_id;
		AST_LIST_UNLOCK(&misdn_cc_records_db);
		if (notify.context[0]) {
			/* Party A is free or B-Free notify has been setup. */
			misdn_cc_pbx_notify(record_id, &notify);
		}
	} else {
		AST_LIST_UNLOCK(&misdn_cc_records_db);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Handle the FACILITY CCBSRemoteUserFree message.
 *
 * \param port Logical port number.
 * \param facility Facility ie contents.
 *
 * \return Nothing
 */
static void misdn_cc_handle_remote_user_free(int port, const struct FacParm *facility)
{
	struct misdn_cc_record *cc_record;
	struct misdn_cc_notify notify;
	long record_id;

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_reference(port, facility->u.CCBSRemoteUserFree.CCBSReference);
	if (cc_record) {
		notify = cc_record->remote_user_free;
		record_id = cc_record->record_id;
		AST_LIST_UNLOCK(&misdn_cc_records_db);
		misdn_cc_pbx_notify(record_id, &notify);
	} else {
		AST_LIST_UNLOCK(&misdn_cc_records_db);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Handle the FACILITY CCBSBFree message.
 *
 * \param port Logical port number.
 * \param facility Facility ie contents.
 *
 * \return Nothing
 */
static void misdn_cc_handle_b_free(int port, const struct FacParm *facility)
{
	struct misdn_cc_record *cc_record;
	struct misdn_cc_notify notify;
	long record_id;

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_reference(port, facility->u.CCBSBFree.CCBSReference);
	if (cc_record && cc_record->b_free.context[0]) {
		/* B-Free notify has been setup. */
		notify = cc_record->b_free;
		record_id = cc_record->record_id;
		AST_LIST_UNLOCK(&misdn_cc_records_db);
		misdn_cc_pbx_notify(record_id, &notify);
	} else {
		AST_LIST_UNLOCK(&misdn_cc_records_db);
	}
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

/*!
 * \internal
 * \brief Handle the incoming facility ie contents
 *
 * \param event Message type facility ie came in on
 * \param bc B channel control structure message came in on
 * \param ch Associated channel call record
 *
 * \return Nothing
 */
static void misdn_facility_ie_handler(enum event_e event, struct misdn_bchannel *bc, struct chan_list *ch)
{
#if defined(AST_MISDN_ENHANCEMENTS)
	const char *diagnostic_msg;
	struct misdn_cc_record *cc_record;
	char buf[32];
	struct misdn_party_id party_id;
	long new_record_id;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	print_facility(&bc->fac_in, bc);
	switch (bc->fac_in.Function) {
#if defined(AST_MISDN_ENHANCEMENTS)
	case Fac_ActivationDiversion:
		switch (bc->fac_in.u.ActivationDiversion.ComponentType) {
		case FacComponent_Result:
			/* Positive ACK to activation */
			/* We don't handle this yet */
			break;
		default:
			chan_misdn_log(0, bc->port," --> not yet handled: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
	case Fac_DeactivationDiversion:
		switch (bc->fac_in.u.DeactivationDiversion.ComponentType) {
		case FacComponent_Result:
			/* Positive ACK to deactivation */
			/* We don't handle this yet */
			break;
		default:
			chan_misdn_log(0, bc->port," --> not yet handled: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
	case Fac_ActivationStatusNotificationDiv:
		/* Sent to other MSN numbers on the line when a user activates call forwarding. */
		/* Sent in the first call control message of an outgoing call from the served user. */
		/* We do not have anything to do for this message. */
		break;
	case Fac_DeactivationStatusNotificationDiv:
		/* Sent to other MSN numbers on the line when a user deactivates call forwarding. */
		/* We do not have anything to do for this message. */
		break;
#if 0	/* We don't handle this yet */
	case Fac_InterrogationDiversion:
		/* We don't handle this yet */
		break;
	case Fac_InterrogateServedUserNumbers:
		/* We don't handle this yet */
		break;
#endif	/* We don't handle this yet */
	case Fac_DiversionInformation:
		/* Sent to the served user when a call is forwarded. */
		/* We do not have anything to do for this message. */
		break;
	case Fac_CallDeflection:
		if (ch && ch->ast) {
			switch (bc->fac_in.u.CallDeflection.ComponentType) {
			case FacComponent_Invoke:
				ast_copy_string(bc->redirecting.from.number, bc->dialed.number,
					sizeof(bc->redirecting.from.number));
				bc->redirecting.from.name[0] = 0;
				bc->redirecting.from.number_plan = bc->dialed.number_plan;
				bc->redirecting.from.number_type = bc->dialed.number_type;
				bc->redirecting.from.screening = 0;/* Unscreened */
				if (bc->fac_in.u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUserPresent) {
					bc->redirecting.from.presentation =
						bc->fac_in.u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUser
						? 0 /* Allowed */ : 1 /* Restricted */;
				} else {
					bc->redirecting.from.presentation = 0;/* Allowed */
				}

				/* Add configured prefix to the call deflection number */
				memset(&party_id, 0, sizeof(party_id));
				misdn_PartyNumber_extract(&party_id,
					&bc->fac_in.u.CallDeflection.Component.Invoke.Deflection.Party);
				misdn_add_number_prefix(bc->port, party_id.number_type,
					party_id.number, sizeof(party_id.number));
				//party_id.presentation = 0;/* Allowed */
				//party_id.screening = 0;/* Unscreened */
				bc->redirecting.to = party_id;

				++bc->redirecting.count;
				bc->redirecting.reason = mISDN_REDIRECTING_REASON_DEFLECTION;

				misdn_copy_redirecting_to_ast(ch->ast, &bc->redirecting);
				ast_string_field_set(ch->ast, call_forward, bc->redirecting.to.number);

				/* Send back positive ACK */
				bc->fac_out.Function = Fac_CallDeflection;
				bc->fac_out.u.CallDeflection.InvokeID = bc->fac_in.u.CallDeflection.InvokeID;
				bc->fac_out.u.CallDeflection.ComponentType = FacComponent_Result;
				print_facility(&bc->fac_out, bc);
				misdn_lib_send_event(bc, EVENT_DISCONNECT);

				/* This line is BUSY to further attempts by this dialing attempt. */
				ast_queue_control(ch->ast, AST_CONTROL_BUSY);
				break;

			case FacComponent_Result:
				/* Positive ACK to call deflection */
				/*
				 * Sent in DISCONNECT or FACILITY message depending upon network option.
				 * It is in the FACILITY message if the call is still offered to the user
				 * while trying to alert the deflected to party.
				 */
				/* Ignore the ACK */
				break;

			default:
				break;
			}
		}
		break;
#if 0	/* We don't handle this yet */
	case Fac_CallRerouteing:
		/* Private-Public ISDN interworking message */
		/* We don't handle this yet */
		break;
#endif	/* We don't handle this yet */
	case Fac_DivertingLegInformation1:
		/* Private-Public ISDN interworking message */
		bc->div_leg_3_rx_wanted = 0;
		if (ch && ch->ast) {
			bc->redirecting.reason =
				diversion_reason_to_misdn(bc->fac_in.u.DivertingLegInformation1.DiversionReason);
			if (bc->fac_in.u.DivertingLegInformation1.DivertedToPresent) {
				misdn_PresentedNumberUnscreened_extract(&bc->redirecting.to,
					&bc->fac_in.u.DivertingLegInformation1.DivertedTo);

				/* Add configured prefix to redirecting.to.number */
				misdn_add_number_prefix(bc->port, bc->redirecting.to.number_type,
					bc->redirecting.to.number, sizeof(bc->redirecting.to.number));
			} else {
				bc->redirecting.to.number[0] = '\0';
				bc->redirecting.to.number_plan = NUMPLAN_ISDN;
				bc->redirecting.to.number_type = NUMTYPE_UNKNOWN;
				bc->redirecting.to.presentation = 1;/* restricted */
				bc->redirecting.to.screening = 0;/* unscreened */
			}
			misdn_copy_redirecting_to_ast(ch->ast, &bc->redirecting);
			bc->div_leg_3_rx_wanted = 1;
		}
		break;
	case Fac_DivertingLegInformation2:
		/* Private-Public ISDN interworking message */
		switch (event) {
		case EVENT_SETUP:
			/* Comes in on a SETUP with redirecting.from information */
			bc->div_leg_3_tx_pending = 1;
			if (ch && ch->ast) {
				/*
				 * Setup the redirecting.to informtion so we can identify
				 * if the user wants to manually supply the COLR for this
				 * redirected to number if further redirects could happen.
				 *
				 * All the user needs to do is set the REDIRECTING(to-pres)
				 * to the COLR and REDIRECTING(to-num) = ${EXTEN} to be safe
				 * after determining that the incoming call was redirected by
				 * checking if there is a REDIRECTING(from-num).
				 */
				ast_copy_string(bc->redirecting.to.number, bc->dialed.number,
					sizeof(bc->redirecting.to.number));
				bc->redirecting.to.number_plan = bc->dialed.number_plan;
				bc->redirecting.to.number_type = bc->dialed.number_type;
				bc->redirecting.to.presentation = 1;/* restricted */
				bc->redirecting.to.screening = 0;/* unscreened */

				bc->redirecting.reason =
					diversion_reason_to_misdn(bc->fac_in.u.DivertingLegInformation2.DiversionReason);
				bc->redirecting.count = bc->fac_in.u.DivertingLegInformation2.DiversionCounter;
				if (bc->fac_in.u.DivertingLegInformation2.DivertingPresent) {
					/* This information is redundant if there was a redirecting ie in the SETUP. */
					misdn_PresentedNumberUnscreened_extract(&bc->redirecting.from,
						&bc->fac_in.u.DivertingLegInformation2.Diverting);

					/* Add configured prefix to redirecting.from.number */
					misdn_add_number_prefix(bc->port, bc->redirecting.from.number_type,
						bc->redirecting.from.number, sizeof(bc->redirecting.from.number));
				}
#if 0
				if (bc->fac_in.u.DivertingLegInformation2.OriginalCalledPresent) {
					/* We have no place to put the OriginalCalled number */
				}
#endif
				misdn_copy_redirecting_to_ast(ch->ast, &bc->redirecting);
			}
			break;
		default:
			chan_misdn_log(0, bc->port," --> Expected in a SETUP message: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
	case Fac_DivertingLegInformation3:
		/* Private-Public ISDN interworking message */
		if (bc->div_leg_3_rx_wanted) {
			bc->div_leg_3_rx_wanted = 0;

			if (ch && ch->ast) {
				ch->ast->redirecting.to.number_presentation =
					bc->fac_in.u.DivertingLegInformation3.PresentationAllowedIndicator
					? AST_PRES_ALLOWED | AST_PRES_USER_NUMBER_UNSCREENED
					: AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_UNSCREENED;
				ast_channel_queue_redirecting_update(ch->ast, &ch->ast->redirecting);
			}
		}
		break;

#else	/* !defined(AST_MISDN_ENHANCEMENTS) */

	case Fac_CD:
		if (ch && ch->ast) {
			ast_copy_string(bc->redirecting.from.number, bc->dialed.number,
				sizeof(bc->redirecting.from.number));
			bc->redirecting.from.name[0] = 0;
			bc->redirecting.from.number_plan = bc->dialed.number_plan;
			bc->redirecting.from.number_type = bc->dialed.number_type;
			bc->redirecting.from.screening = 0;/* Unscreened */
			bc->redirecting.from.presentation =
				bc->fac_in.u.CDeflection.PresentationAllowed
				? 0 /* Allowed */ : 1 /* Restricted */;

			ast_copy_string(bc->redirecting.to.number,
				(char *) bc->fac_in.u.CDeflection.DeflectedToNumber,
				sizeof(bc->redirecting.to.number));
			bc->redirecting.to.name[0] = 0;
			bc->redirecting.to.number_plan = NUMPLAN_UNKNOWN;
			bc->redirecting.to.number_type = NUMTYPE_UNKNOWN;
			bc->redirecting.to.presentation = 0;/* Allowed */
			bc->redirecting.to.screening = 0;/* Unscreened */

			++bc->redirecting.count;
			bc->redirecting.reason = mISDN_REDIRECTING_REASON_DEFLECTION;

			misdn_copy_redirecting_to_ast(ch->ast, &bc->redirecting);
			ast_string_field_set(ch->ast, call_forward, bc->redirecting.to.number);

			misdn_lib_send_event(bc, EVENT_DISCONNECT);

			/* This line is BUSY to further attempts by this dialing attempt. */
			ast_queue_control(ch->ast, AST_CONTROL_BUSY);
		}
		break;
#endif	/* !defined(AST_MISDN_ENHANCEMENTS) */
	case Fac_AOCDCurrency:
		if (ch && ch->ast) {
			bc->AOCDtype = Fac_AOCDCurrency;
			memcpy(&bc->AOCD.currency, &bc->fac_in.u.AOCDcur, sizeof(bc->AOCD.currency));
			bc->AOCD_need_export = 1;
			export_aoc_vars(ch->originator, ch->ast, bc);
		}
		break;
	case Fac_AOCDChargingUnit:
		if (ch && ch->ast) {
			bc->AOCDtype = Fac_AOCDChargingUnit;
			memcpy(&bc->AOCD.chargingUnit, &bc->fac_in.u.AOCDchu, sizeof(bc->AOCD.chargingUnit));
			bc->AOCD_need_export = 1;
			export_aoc_vars(ch->originator, ch->ast, bc);
		}
		break;
#if defined(AST_MISDN_ENHANCEMENTS)
	case Fac_ERROR:
		diagnostic_msg = misdn_to_str_error_code(bc->fac_in.u.ERROR.errorValue);
		chan_misdn_log(1, bc->port, " --> Facility error code: %s\n", diagnostic_msg);
		switch (event) {
		case EVENT_DISCONNECT:
		case EVENT_RELEASE:
		case EVENT_RELEASE_COMPLETE:
			/* Possible call failure as a result of Fac_CCBSCall/Fac_CCBS_T_Call */
			if (ch && ch->peer) {
				misdn_cc_set_peer_var(ch->peer, MISDN_ERROR_MSG, diagnostic_msg);
			}
			break;
		default:
			break;
		}
		AST_LIST_LOCK(&misdn_cc_records_db);
		cc_record = misdn_cc_find_by_invoke(bc->port, bc->fac_in.u.ERROR.invokeId);
		if (cc_record) {
			cc_record->outstanding_message = 0;
			cc_record->error_code = bc->fac_in.u.ERROR.errorValue;
		}
		AST_LIST_UNLOCK(&misdn_cc_records_db);
		break;
	case Fac_REJECT:
		diagnostic_msg = misdn_to_str_reject_code(bc->fac_in.u.REJECT.Code);
		chan_misdn_log(1, bc->port, " --> Facility reject code: %s\n", diagnostic_msg);
		switch (event) {
		case EVENT_DISCONNECT:
		case EVENT_RELEASE:
		case EVENT_RELEASE_COMPLETE:
			/* Possible call failure as a result of Fac_CCBSCall/Fac_CCBS_T_Call */
			if (ch && ch->peer) {
				misdn_cc_set_peer_var(ch->peer, MISDN_ERROR_MSG, diagnostic_msg);
			}
			break;
		default:
			break;
		}
		if (bc->fac_in.u.REJECT.InvokeIDPresent) {
			AST_LIST_LOCK(&misdn_cc_records_db);
			cc_record = misdn_cc_find_by_invoke(bc->port, bc->fac_in.u.REJECT.InvokeID);
			if (cc_record) {
				cc_record->outstanding_message = 0;
				cc_record->reject_code = bc->fac_in.u.REJECT.Code;
			}
			AST_LIST_UNLOCK(&misdn_cc_records_db);
		}
		break;
	case Fac_RESULT:
		AST_LIST_LOCK(&misdn_cc_records_db);
		cc_record = misdn_cc_find_by_invoke(bc->port, bc->fac_in.u.RESULT.InvokeID);
		if (cc_record) {
			cc_record->outstanding_message = 0;
		}
		AST_LIST_UNLOCK(&misdn_cc_records_db);
		break;
#if 0	/* We don't handle this yet */
	case Fac_EctExecute:
		/* We don't handle this yet */
		break;
	case Fac_ExplicitEctExecute:
		/* We don't handle this yet */
		break;
	case Fac_EctLinkIdRequest:
		/* We don't handle this yet */
		break;
#endif	/* We don't handle this yet */
	case Fac_SubaddressTransfer:
		/* We do not have anything to do for this message since we do not handle subaddreses. */
		break;
	case Fac_RequestSubaddress:
		/* We do not have anything to do for this message since we do not handle subaddreses. */
		break;
	case Fac_EctInform:
		/* Private-Public ISDN interworking message */
		if (ch && ch->ast && bc->fac_in.u.EctInform.RedirectionPresent) {
			/* Add configured prefix to the redirection number */
			memset(&party_id, 0, sizeof(party_id));
			misdn_PresentedNumberUnscreened_extract(&party_id,
				&bc->fac_in.u.EctInform.Redirection);
			misdn_add_number_prefix(bc->port, party_id.number_type,
				party_id.number, sizeof(party_id.number));

			/*
			 * It would be preferable to update the connected line information
			 * only when the message callStatus is active.  However, the
			 * optional redirection number may not be present in the active
			 * message if an alerting message were received earlier.
			 *
			 * The consequences if we wind up sending two updates is benign.
			 * The other end will think that it got transferred twice.
			 */
			misdn_queue_connected_line_update(ch->ast, &party_id,
				(bc->fac_in.u.EctInform.Status == 0 /* alerting */)
					? AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER_ALERTING
					: AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER);
		}
		break;
#if 0	/* We don't handle this yet */
	case Fac_EctLoopTest:
		/* The use of this message is unclear on how it works to detect loops. */
		/* We don't handle this yet */
		break;
#endif	/* We don't handle this yet */
	case Fac_CallInfoRetain:
		switch (event) {
		case EVENT_ALERTING:
		case EVENT_DISCONNECT:
			/* CCBS/CCNR is available */
			if (ch && ch->peer) {
				AST_LIST_LOCK(&misdn_cc_records_db);
				if (ch->record_id == -1) {
					cc_record = misdn_cc_new();
				} else {
					/*
					 * We are doing a call-completion attempt
					 * or the switch is sending us extra call-completion
					 * availability indications (erroneously?).
					 *
					 * Assume that the network request retention option
					 * is not on and that the current call-completion
					 * request is disabled.
					 */
					cc_record = misdn_cc_find_by_id(ch->record_id);
					if (cc_record) {
						if (cc_record->ptp && cc_record->mode.ptp.bc) {
							/*
							 * What?  We are getting mixed messages from the
							 * switch.  We are currently setup for
							 * point-to-point.  Now we are switching to
							 * point-to-multipoint.
							 *
							 * Close the call-completion signaling link
							 */
							cc_record->mode.ptp.bc->fac_out.Function = Fac_None;
							cc_record->mode.ptp.bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
							misdn_lib_send_event(cc_record->mode.ptp.bc, EVENT_RELEASE_COMPLETE);
						}

						/*
						 * Resetup the existing record for a possible new
						 * call-completion request.
						 */
						new_record_id = misdn_cc_record_id_new();
						if (new_record_id < 0) {
							/* Looks like we must keep the old id anyway. */
						} else {
							cc_record->record_id = new_record_id;
							ch->record_id = new_record_id;
						}
						cc_record->ptp = 0;
						cc_record->port = bc->port;
						memset(&cc_record->mode, 0, sizeof(cc_record->mode));
						cc_record->mode.ptmp.linkage_id = bc->fac_in.u.CallInfoRetain.CallLinkageID;
						cc_record->invoke_id = ++misdn_invoke_id;
						cc_record->activated = 0;
						cc_record->outstanding_message = 0;
						cc_record->activation_requested = 0;
						cc_record->error_code = FacError_None;
						cc_record->reject_code = FacReject_None;
						memset(&cc_record->remote_user_free, 0, sizeof(cc_record->remote_user_free));
						memset(&cc_record->b_free, 0, sizeof(cc_record->b_free));
						cc_record->time_created = time(NULL);

						cc_record = NULL;
					} else {
						/*
						 * Where did the record go?  We will have to recapture
						 * the call setup information.  Unfortunately, some
						 * setup information may have been changed.
						 */
						ch->record_id = -1;
						cc_record = misdn_cc_new();
					}
				}
				if (cc_record) {
					ch->record_id = cc_record->record_id;
					cc_record->ptp = 0;
					cc_record->port = bc->port;
					cc_record->mode.ptmp.linkage_id = bc->fac_in.u.CallInfoRetain.CallLinkageID;

					/* Record call information for possible call-completion attempt. */
					cc_record->redial.caller = bc->caller;
					cc_record->redial.dialed = bc->dialed;
					cc_record->redial.setup_bc_hlc_llc = bc->setup_bc_hlc_llc;
					cc_record->redial.capability = bc->capability;
					cc_record->redial.hdlc = bc->hdlc;
				}
				AST_LIST_UNLOCK(&misdn_cc_records_db);

				/* Set MISDN_CC_RECORD_ID in original channel */
				if (ch->record_id != -1) {
					snprintf(buf, sizeof(buf), "%ld", ch->record_id);
				} else {
					buf[0] = 0;
				}
				misdn_cc_set_peer_var(ch->peer, MISDN_CC_RECORD_ID, buf);
			}
			break;
		default:
			chan_misdn_log(0, bc->port,
				" --> Expected in a DISCONNECT or ALERTING message: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
	case Fac_CCBS_T_Call:
	case Fac_CCBSCall:
		switch (event) {
		case EVENT_SETUP:
			/*
			 * This is a call completion retry call.
			 * If we had anything to do we would do it here.
			 */
			break;
		default:
			chan_misdn_log(0, bc->port, " --> Expected in a SETUP message: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
	case Fac_CCBSDeactivate:
		switch (bc->fac_in.u.CCBSDeactivate.ComponentType) {
		case FacComponent_Result:
			AST_LIST_LOCK(&misdn_cc_records_db);
			cc_record = misdn_cc_find_by_invoke(bc->port, bc->fac_in.u.CCBSDeactivate.InvokeID);
			if (cc_record) {
				cc_record->outstanding_message = 0;
			}
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			break;

		default:
			chan_misdn_log(0, bc->port, " --> not yet handled: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
	case Fac_CCBSErase:
		AST_LIST_LOCK(&misdn_cc_records_db);
		cc_record = misdn_cc_find_by_reference(bc->port, bc->fac_in.u.CCBSErase.CCBSReference);
		if (cc_record) {
			misdn_cc_delete(cc_record);
		}
		AST_LIST_UNLOCK(&misdn_cc_records_db);
		break;
	case Fac_CCBSRemoteUserFree:
		misdn_cc_handle_remote_user_free(bc->port, &bc->fac_in);
		break;
	case Fac_CCBSBFree:
		misdn_cc_handle_b_free(bc->port, &bc->fac_in);
		break;
	case Fac_CCBSStatusRequest:
		misdn_cc_handle_ccbs_status_request(bc->port, &bc->fac_in);
		break;
	case Fac_EraseCallLinkageID:
		AST_LIST_LOCK(&misdn_cc_records_db);
		cc_record = misdn_cc_find_by_linkage(bc->port,
			bc->fac_in.u.EraseCallLinkageID.CallLinkageID);
		if (cc_record && !cc_record->activation_requested) {
			/*
			 * The T-RETENTION timer expired before we requested
			 * call completion activation.  Call completion is no
			 * longer available.
			 */
			misdn_cc_delete(cc_record);
		}
		AST_LIST_UNLOCK(&misdn_cc_records_db);
		break;
	case Fac_CCBSStopAlerting:
		/* We do not have anything to do for this message. */
		break;
	case Fac_CCBSRequest:
	case Fac_CCNRRequest:
		switch (bc->fac_in.u.CCBSRequest.ComponentType) {
		case FacComponent_Result:
			AST_LIST_LOCK(&misdn_cc_records_db);
			cc_record = misdn_cc_find_by_invoke(bc->port, bc->fac_in.u.CCBSRequest.InvokeID);
			if (cc_record && !cc_record->ptp) {
				cc_record->outstanding_message = 0;
				cc_record->activated = 1;
				cc_record->mode.ptmp.recall_mode = bc->fac_in.u.CCBSRequest.Component.Result.RecallMode;
				cc_record->mode.ptmp.reference_id = bc->fac_in.u.CCBSRequest.Component.Result.CCBSReference;
			}
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			break;

		default:
			chan_misdn_log(0, bc->port, " --> not yet handled: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
#if 0	/* We don't handle this yet */
	case Fac_CCBSInterrogate:
	case Fac_CCNRInterrogate:
		/* We don't handle this yet */
		break;
	case Fac_StatusRequest:
		/* We don't handle this yet */
		break;
#endif	/* We don't handle this yet */
#if 0	/* We don't handle this yet */
	case Fac_CCBS_T_Suspend:
	case Fac_CCBS_T_Resume:
		/* We don't handle this yet */
		break;
#endif	/* We don't handle this yet */
	case Fac_CCBS_T_RemoteUserFree:
		misdn_cc_handle_T_remote_user_free(bc);
		break;
	case Fac_CCBS_T_Available:
		switch (event) {
		case EVENT_ALERTING:
		case EVENT_DISCONNECT:
			/* CCBS-T/CCNR-T is available */
			if (ch && ch->peer) {
				int set_id = 1;

				AST_LIST_LOCK(&misdn_cc_records_db);
				if (ch->record_id == -1) {
					cc_record = misdn_cc_new();
				} else {
					/*
					 * We are doing a call-completion attempt
					 * or the switch is sending us extra call-completion
					 * availability indications (erroneously?).
					 */
					cc_record = misdn_cc_find_by_id(ch->record_id);
					if (cc_record) {
						if (cc_record->ptp && cc_record->mode.ptp.retention_enabled) {
							/*
							 * Call-completion is still activated.
							 * The user does not have to request it again.
							 */
							chan_misdn_log(1, bc->port, " --> Call-completion request retention option is enabled\n");

							set_id = 0;
						} else {
							if (cc_record->ptp && cc_record->mode.ptp.bc) {
								/*
								 * The network request retention option
								 * is not on and the current call-completion
								 * request is to be disabled.
								 *
								 * We should get here only if EVENT_DISCONNECT
								 *
								 * Close the call-completion signaling link
								 */
								cc_record->mode.ptp.bc->fac_out.Function = Fac_None;
								cc_record->mode.ptp.bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
								misdn_lib_send_event(cc_record->mode.ptp.bc, EVENT_RELEASE_COMPLETE);
							}

							/*
							 * Resetup the existing record for a possible new
							 * call-completion request.
							 */
							new_record_id = misdn_cc_record_id_new();
							if (new_record_id < 0) {
								/* Looks like we must keep the old id anyway. */
							} else {
								cc_record->record_id = new_record_id;
								ch->record_id = new_record_id;
							}
							cc_record->ptp = 1;
							cc_record->port = bc->port;
							memset(&cc_record->mode, 0, sizeof(cc_record->mode));
							cc_record->invoke_id = ++misdn_invoke_id;
							cc_record->activated = 0;
							cc_record->outstanding_message = 0;
							cc_record->activation_requested = 0;
							cc_record->error_code = FacError_None;
							cc_record->reject_code = FacReject_None;
							memset(&cc_record->remote_user_free, 0, sizeof(cc_record->remote_user_free));
							memset(&cc_record->b_free, 0, sizeof(cc_record->b_free));
							cc_record->time_created = time(NULL);
						}
						cc_record = NULL;
					} else {
						/*
						 * Where did the record go?  We will have to recapture
						 * the call setup information.  Unfortunately, some
						 * setup information may have been changed.
						 */
						ch->record_id = -1;
						cc_record = misdn_cc_new();
					}
				}
				if (cc_record) {
					ch->record_id = cc_record->record_id;
					cc_record->ptp = 1;
					cc_record->port = bc->port;

					/* Record call information for possible call-completion attempt. */
					cc_record->redial.caller = bc->caller;
					cc_record->redial.dialed = bc->dialed;
					cc_record->redial.setup_bc_hlc_llc = bc->setup_bc_hlc_llc;
					cc_record->redial.capability = bc->capability;
					cc_record->redial.hdlc = bc->hdlc;
				}
				AST_LIST_UNLOCK(&misdn_cc_records_db);

				/* Set MISDN_CC_RECORD_ID in original channel */
				if (ch->record_id != -1 && set_id) {
					snprintf(buf, sizeof(buf), "%ld", ch->record_id);
				} else {
					buf[0] = 0;
				}
				misdn_cc_set_peer_var(ch->peer, MISDN_CC_RECORD_ID, buf);
			}
			break;
		default:
			chan_misdn_log(0, bc->port,
				" --> Expected in a DISCONNECT or ALERTING message: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;
	case Fac_CCBS_T_Request:
	case Fac_CCNR_T_Request:
		switch (bc->fac_in.u.CCBS_T_Request.ComponentType) {
		case FacComponent_Result:
			AST_LIST_LOCK(&misdn_cc_records_db);
			cc_record = misdn_cc_find_by_invoke(bc->port, bc->fac_in.u.CCBS_T_Request.InvokeID);
			if (cc_record && cc_record->ptp) {
				cc_record->outstanding_message = 0;
				cc_record->activated = 1;
				cc_record->mode.ptp.retention_enabled =
					cc_record->mode.ptp.requested_retention
					? bc->fac_in.u.CCBS_T_Request.Component.Result.RetentionSupported
					? 1 : 0
					: 0;
			}
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			break;

		case FacComponent_Invoke:
			/* We cannot be User-B in ptp mode. */
		default:
			chan_misdn_log(0, bc->port, " --> not yet handled: facility type:0x%04X\n",
				bc->fac_in.Function);
			break;
		}
		break;

#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	case Fac_None:
		break;
	default:
		chan_misdn_log(0, bc->port, " --> not yet handled: facility type:0x%04X\n",
			bc->fac_in.Function);
		break;
	}
}

/*!
 * \internal
 * \brief Determine if the given dialed party matches our MSN.
 * \since 1.6.3
 *
 * \param port ISDN port
 * \param dialed Dialed party information of incoming call.
 *
 * \retval non-zero if MSN is valid.
 * \retval 0 if MSN invalid.
 */
static int misdn_is_msn_valid(int port, const struct misdn_party_dialing *dialed)
{
	char number[sizeof(dialed->number)];

	ast_copy_string(number, dialed->number, sizeof(number));
	misdn_add_number_prefix(port, dialed->number_type, number, sizeof(number));
	return misdn_cfg_is_msn_valid(port, number);
}

/************************************************************/
/*  Receive Events from isdn_lib  here                     */
/************************************************************/
static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data)
{
#if defined(AST_MISDN_ENHANCEMENTS)
	struct misdn_cc_record *cc_record;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	struct chan_list *held_ch;
	struct chan_list *ch = find_chan_by_bc(cl_te, bc);

	if (event != EVENT_BCHAN_DATA && event != EVENT_TONE_GENERATE) {
		int debuglevel = 1;

		/*  Debug Only Non-Bchan */
		if (event == EVENT_CLEANUP && !user_data) {
			debuglevel = 5;
		}

		chan_misdn_log(debuglevel, bc->port,
			"I IND :%s caller:\"%s\" <%s> dialed:%s pid:%d state:%s\n",
			manager_isdn_get_info(event),
			bc->caller.name,
			bc->caller.number,
			bc->dialed.number,
			bc->pid,
			ch ? misdn_get_ch_state(ch) : "none");
		if (debuglevel == 1) {
			misdn_lib_log_ies(bc);
			chan_misdn_log(4, bc->port, " --> bc_state:%s\n", bc_state2str(bc->bc_state));
		}
	}

	if (!ch) {
		switch(event) {
		case EVENT_SETUP:
		case EVENT_DISCONNECT:
		case EVENT_RELEASE:
		case EVENT_RELEASE_COMPLETE:
		case EVENT_PORT_ALARM:
		case EVENT_RETRIEVE:
		case EVENT_NEW_BC:
		case EVENT_FACILITY:
		case EVENT_REGISTER:
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
			if (!ch->ast) {
				chan_misdn_log(3, bc->port, "ast_hangup already called, so we have no ast ptr anymore in event(%s)\n", manager_isdn_get_info(event));
			}
			break;
		default:
			if (!ch->ast || !MISDN_ASTERISK_TECH_PVT(ch->ast)) {
				if (event != EVENT_BCHAN_DATA) {
					ast_log(LOG_NOTICE, "No Ast or No private Pointer in Event (%d:%s)\n", event, manager_isdn_get_info(event));
				}
				return -1;
			}
			break;
		}
	}


	switch (event) {
	case EVENT_PORT_ALARM:
		{
			int boa = 0;
			misdn_cfg_get(bc->port, MISDN_CFG_ALARM_BLOCK, &boa, sizeof(boa));
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
			ch = find_hold_call(cl_te,bc);
		}

		if (!ch) {
			ast_log(LOG_WARNING, "NEW_BC without chan_list?\n");
			break;
		}

		if (bc) {
			ch->bc = (struct misdn_bchannel *) user_data;
		}
		break;

	case EVENT_DTMF_TONE:
	{
		/*  sending INFOS as DTMF-Frames :) */
		struct ast_frame fr;

		memset(&fr, 0, sizeof(fr));
		fr.frametype = AST_FRAME_DTMF;
		fr.subclass = bc->dtmf ;
		fr.src = NULL;
		fr.data.ptr = NULL;
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
		break;
	}
	case EVENT_STATUS:
		break;

	case EVENT_INFORMATION:
		if (ch->state != MISDN_CONNECTED) {
			stop_indicate(ch);
		}

		if (!ch->ast) {
			break;
		}

		if (ch->state == MISDN_WAITING4DIGS) {
			/*  Ok, incomplete Setup, waiting till extension exists */
			if (ast_strlen_zero(bc->info_dad) && ! ast_strlen_zero(bc->keypad)) {
				chan_misdn_log(1, bc->port, " --> using keypad as info\n");
				ast_copy_string(bc->info_dad, bc->keypad, sizeof(bc->info_dad));
			}

			strncat(bc->dialed.number, bc->info_dad, sizeof(bc->dialed.number) - strlen(bc->dialed.number) - 1);
			ast_copy_string(ch->ast->exten, bc->dialed.number, sizeof(ch->ast->exten));

			/* Check for Pickup Request first */
			if (!strcmp(ch->ast->exten, ast_pickup_ext())) {
				if (ast_pickup_call(ch->ast)) {
					hangup_chan(ch, bc);
				} else {
					ch->state = MISDN_CALLING_ACKNOWLEDGE;
					hangup_chan(ch, bc);
					ch->ast = NULL;
					break;
				}
			}

			if (!ast_canmatch_extension(ch->ast, ch->context, bc->dialed.number, 1, bc->caller.number)) {
				if (ast_exists_extension(ch->ast, ch->context, "i", 1, bc->caller.number)) {
					ast_log(LOG_WARNING,
						"Extension '%s@%s' can never match. Jumping to 'i' extension. port:%d\n",
						bc->dialed.number, ch->context, bc->port);
					strcpy(ch->ast->exten, "i");

					ch->state = MISDN_DIALING;
					start_pbx(ch, bc, ch->ast);
					break;
				}

				ast_log(LOG_WARNING,
					"Extension '%s@%s' can never match. Disconnecting. port:%d\n"
					"\tMaybe you want to add an 'i' extension to catch this case.\n",
					bc->dialed.number, ch->context, bc->port);

				if (bc->nt) {
					hanguptone_indicate(ch);
				}
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

			if (ast_exists_extension(ch->ast, ch->context, bc->dialed.number, 1, bc->caller.number))  {
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
			fr.data.ptr = NULL;
			fr.datalen = 0;
			fr.samples = 0;
			fr.mallocd = 0;
			fr.offset = 0;
			fr.delivery = ast_tv(0,0);

			misdn_cfg_get(0, MISDN_GEN_APPEND_DIGITS2EXTEN, &digits, sizeof(digits));
			if (ch->state != MISDN_CONNECTED) {
				if (digits) {
					strncat(bc->dialed.number, bc->info_dad, sizeof(bc->dialed.number) - strlen(bc->dialed.number) - 1);
					ast_copy_string(ch->ast->exten, bc->dialed.number, sizeof(ch->ast->exten));
					ast_cdr_update(ch->ast);
				}

				ast_queue_frame(ch->ast, &fr);
			}
		}
		break;
	case EVENT_SETUP:
	{
		struct chan_list *ch = find_chan_by_bc(cl_te, bc);
		struct ast_channel *chan;
		int exceed;
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

		if (!bc->nt && !misdn_is_msn_valid(bc->port, &bc->dialed)) {
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

		chan = misdn_new(ch, AST_STATE_RESERVED, bc->dialed.number, bc->caller.number, AST_FORMAT_ALAW, NULL, bc->port, bc->channel);
		if (!chan) {
			ast_free(ch);
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

		read_config(ch);

		export_ch(chan, bc, ch);

		ch->ast->rings = 1;
		ast_setstate(ch->ast, AST_STATE_RINGING);

		/* Update asterisk channel caller information */
		chan_misdn_log(2, bc->port, " --> TON: %s(%d)\n", misdn_to_str_ton(bc->caller.number_type), bc->caller.number_type);
		chan_misdn_log(2, bc->port, " --> PLAN: %s(%d)\n", misdn_to_str_plan(bc->caller.number_plan), bc->caller.number_plan);
		chan->cid.cid_ton = misdn_to_ast_ton(bc->caller.number_type)
			| misdn_to_ast_plan(bc->caller.number_plan);

		chan_misdn_log(2, bc->port, " --> PRES: %s(%d)\n", misdn_to_str_pres(bc->caller.presentation), bc->caller.presentation);
		chan_misdn_log(2, bc->port, " --> SCREEN: %s(%d)\n", misdn_to_str_screen(bc->caller.screening), bc->caller.screening);
		chan->cid.cid_pres = misdn_to_ast_pres(bc->caller.presentation)
			| misdn_to_ast_screen(bc->caller.screening);

		ast_set_callerid(chan, bc->caller.number, NULL, bc->caller.number);

		if (!ast_strlen_zero(bc->redirecting.from.number)) {
			/* Add configured prefix to redirecting.from.number */
			misdn_add_number_prefix(bc->port, bc->redirecting.from.number_type, bc->redirecting.from.number, sizeof(bc->redirecting.from.number));

			/* Update asterisk channel redirecting information */
			misdn_copy_redirecting_to_ast(chan, &bc->redirecting);
		}

		pbx_builtin_setvar_helper(chan, "TRANSFERCAPABILITY", ast_transfercapability2str(bc->capability));
		chan->transfercapability = bc->capability;

		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
			pbx_builtin_setvar_helper(chan, "CALLTYPE", "DIGITAL");
			break;
		default:
			pbx_builtin_setvar_helper(chan, "CALLTYPE", "SPEECH");
			break;
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
			}
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

		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}

		/* Check for Pickup Request first */
		if (!strcmp(chan->exten, ast_pickup_ext())) {
			if (!ch->noautorespond_on_setup) {
				/* Sending SETUP_ACK */
				misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE);
			} else {
				ch->state = MISDN_INCOMING_SETUP;
			}
			if (ast_pickup_call(chan)) {
				hangup_chan(ch, bc);
			} else {
				ch->state = MISDN_CALLING_ACKNOWLEDGE;
				hangup_chan(ch, bc);
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

		/* check if we should jump into s when we have no dialed.number */
		misdn_cfg_get(bc->port, MISDN_CFG_IMMEDIATE, &im, sizeof(im));
		if (im && ast_strlen_zero(bc->dialed.number)) {
			do_immediate_setup(bc, ch, chan);
			break;
		}

		chan_misdn_log(5, bc->port, "CONTEXT:%s\n", ch->context);
		if (!ast_canmatch_extension(ch->ast, ch->context, bc->dialed.number, 1, bc->caller.number)) {
			if (ast_exists_extension(ch->ast, ch->context, "i", 1, bc->caller.number)) {
				ast_log(LOG_WARNING,
					"Extension '%s@%s' can never match. Jumping to 'i' extension. port:%d\n",
					bc->dialed.number, ch->context, bc->port);
				strcpy(ch->ast->exten, "i");
				misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE);
				ch->state = MISDN_DIALING;
				start_pbx(ch, bc, chan);
				break;
			}

			ast_log(LOG_WARNING,
				"Extension '%s@%s' can never match. Disconnecting. port:%d\n"
				"\tMaybe you want to add an 'i' extension to catch this case.\n",
				bc->dialed.number, ch->context, bc->port);
			if (bc->nt) {
				hanguptone_indicate(ch);
			}

			ch->state = MISDN_EXTCANTMATCH;
			bc->out_cause = AST_CAUSE_UNALLOCATED;

			misdn_lib_send_event(bc, bc->nt ? EVENT_RELEASE_COMPLETE : EVENT_RELEASE);
			break;
		}

		/* Whatever happens, when sending_complete is set or we are PTMP TE, we will definitely
		 * jump into the dialplan, when the dialed extension does not exist, the 's' extension
		 * will be used by Asterisk automatically. */
		if (bc->sending_complete || (!bc->nt && !misdn_lib_is_ptp(bc->port))) {
			if (!ch->noautorespond_on_setup) {
				ch->state=MISDN_DIALING;
				misdn_lib_send_event(bc, EVENT_PROCEEDING);
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
		if (ch->overlap_dial && bc->nt && !bc->dialed.number[0]) {
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
			if (ch->overlap_dial_task == -1) {
				ch->overlap_dial_task =
					misdn_tasks_add_variable(ch->overlap_dial, misdn_overlap_dial_task, ch);
			}
			break;
		}

		/* If the extension does not exist and we're not TE_PTMP we wait for more digits
		 * without interdigit timeout.
		 * */
		if (!ast_exists_extension(ch->ast, ch->context, bc->dialed.number, 1, bc->caller.number))  {
			wait_for_digits(ch, bc, chan);
			break;
		}

		/*
		 * If the extension exists let's just jump into it.
		 * */
		if (ast_exists_extension(ch->ast, ch->context, bc->dialed.number, 1, bc->caller.number)) {
			misdn_lib_send_event(bc, bc->need_more_infos ? EVENT_SETUP_ACKNOWLEDGE : EVENT_PROCEEDING);
			ch->state = MISDN_DIALING;
			start_pbx(ch, bc, chan);
			break;
		}
		break;
	}
#if defined(AST_MISDN_ENHANCEMENTS)
	case EVENT_REGISTER:
		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}
		/*
		 * Shut down this connection immediately.
		 * The current design of chan_misdn data structures
		 * does not allow the proper handling of inbound call records
		 * without an assigned B channel.  Therefore, we cannot
		 * be the CCBS User-B party in a point-to-point setup.
		 */
		bc->fac_out.Function = Fac_None;
		bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
		misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
		break;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	case EVENT_SETUP_ACKNOWLEDGE:
		ch->state = MISDN_CALLING_ACKNOWLEDGE;

		if (bc->channel) {
			update_name(ch->ast,bc->port,bc->channel);
		}

		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}

		if (!ast_strlen_zero(bc->infos_pending)) {
			/* TX Pending Infos */
			strncat(bc->dialed.number, bc->infos_pending, sizeof(bc->dialed.number) - strlen(bc->dialed.number) - 1);

			if (!ch->ast) {
				break;
			}
			ast_copy_string(ch->ast->exten, bc->dialed.number, sizeof(ch->ast->exten));
			ast_copy_string(bc->info_dad, bc->infos_pending, sizeof(bc->info_dad));
			ast_copy_string(bc->infos_pending, "", sizeof(bc->infos_pending));

			misdn_lib_send_event(bc, EVENT_INFORMATION);
		}
		break;
	case EVENT_PROCEEDING:
		if (misdn_cap_is_speech(bc->capability) &&
			misdn_inband_avail(bc)) {
			start_bc_tones(ch);
		}

		ch->state = MISDN_PROCEEDING;

		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}

		if (!ch->ast) {
			break;
		}

		ast_queue_control(ch->ast, AST_CONTROL_PROCEEDING);
		break;
	case EVENT_PROGRESS:
		if (bc->channel) {
			update_name(ch->ast, bc->port, bc->channel);
		}

		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}

		if (!bc->nt) {
			if (misdn_cap_is_speech(bc->capability) &&
				misdn_inband_avail(bc)) {
				start_bc_tones(ch);
			}

			ch->state = MISDN_PROGRESS;

			if (!ch->ast) {
				break;
			}
			ast_queue_control(ch->ast, AST_CONTROL_PROGRESS);
		}
		break;
	case EVENT_ALERTING:
		ch->state = MISDN_ALERTING;

		if (!ch->ast) {
			break;
		}

		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}

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
		break;
	case EVENT_CONNECT:
		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}
#if defined(AST_MISDN_ENHANCEMENTS)
		if (bc->div_leg_3_rx_wanted) {
			bc->div_leg_3_rx_wanted = 0;

			if (ch->ast) {
				ch->ast->redirecting.to.number_presentation =
					AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_UNSCREENED;
				ast_channel_queue_redirecting_update(ch->ast, &ch->ast->redirecting);
			}
		}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

		/* we answer when we've got our very new L3 ID from the NT stack */
		misdn_lib_send_event(bc, EVENT_CONNECT_ACKNOWLEDGE);

		if (!ch->ast) {
			break;
		}

		stop_indicate(ch);

#if defined(AST_MISDN_ENHANCEMENTS)
		if (ch->record_id != -1) {
			/*
			 * We will delete the associated call completion
			 * record since we now have a completed call.
			 * We will not wait/depend on the network to tell
			 * us to delete it.
			 */
			AST_LIST_LOCK(&misdn_cc_records_db);
			cc_record = misdn_cc_find_by_id(ch->record_id);
			if (cc_record) {
				if (cc_record->ptp && cc_record->mode.ptp.bc) {
					/* Close the call-completion signaling link */
					cc_record->mode.ptp.bc->fac_out.Function = Fac_None;
					cc_record->mode.ptp.bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
					misdn_lib_send_event(cc_record->mode.ptp.bc, EVENT_RELEASE_COMPLETE);
				}
				misdn_cc_delete(cc_record);
			}
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			ch->record_id = -1;
			if (ch->peer) {
				misdn_cc_set_peer_var(ch->peer, MISDN_CC_RECORD_ID, "");

				ao2_ref(ch->peer, -1);
				ch->peer = NULL;
			}
		}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

		/* Add configured prefix to connected.number */
		misdn_add_number_prefix(bc->port, bc->connected.number_type, bc->connected.number, sizeof(bc->connected.number));

		/* Update the connected line information on the other channel */
		misdn_queue_connected_line_update(ch->ast, &bc->connected, AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER);

		ch->l3id = bc->l3_id;
		ch->addr = bc->addr;

		start_bc_tones(ch);

		ch->state = MISDN_CONNECTED;

		ast_queue_control(ch->ast, AST_CONTROL_ANSWER);
		break;
	case EVENT_CONNECT_ACKNOWLEDGE:
		ch->l3id = bc->l3_id;
		ch->addr = bc->addr;

		start_bc_tones(ch);

		ch->state = MISDN_CONNECTED;
		break;
	case EVENT_DISCONNECT:
		/* we might not have an ch->ast ptr here anymore */
		if (ch) {
			if (bc->fac_in.Function != Fac_None) {
				misdn_facility_ie_handler(event, bc, ch);
			}

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
					if (bc->cause == AST_CAUSE_USER_BUSY) {
						ast_queue_control(ch->ast, AST_CONTROL_BUSY);
					}
				}
				ch->need_busy = 0;
				break;
			}

			bc->need_disconnect = 0;
			stop_bc_tones(ch);

			/* Check for held channel, to implement transfer */
			held_ch = find_hold_call(cl_te, bc);
			if (!held_ch || !ch->ast || misdn_attempt_transfer(ch, held_ch)) {
				hangup_chan(ch, bc);
			}
		} else {
			held_ch = find_hold_call_l3(cl_te, bc->l3_id);
			if (held_ch) {
				if (bc->fac_in.Function != Fac_None) {
					misdn_facility_ie_handler(event, bc, held_ch);
				}

				if (held_ch->hold.state == MISDN_HOLD_ACTIVE) {
					bc->need_disconnect = 0;

#if defined(TRANSFER_ON_HELD_CALL_HANGUP)
					/*
					 * Some phones disconnect the held call and the active call at the
					 * same time to do the transfer.  Unfortunately, either call could
					 * be disconnected first.
					 */
					ch = find_hold_active_call(cl_te, bc);
					if (!ch || misdn_attempt_transfer(ch, held_ch)) {
						held_ch->hold.state = MISDN_HOLD_DISCONNECT;
						hangup_chan(held_ch, bc);
					}
#else
					hangup_chan(held_ch, bc);
#endif	/* defined(TRANSFER_ON_HELD_CALL_HANGUP) */
				}
			}
		}
		bc->out_cause = -1;
		if (bc->need_release) {
			misdn_lib_send_event(bc, EVENT_RELEASE);
		}
		break;
	case EVENT_RELEASE:
		if (!ch) {
			ch = find_hold_call_l3(cl_te, bc->l3_id);
			if (!ch) {
				chan_misdn_log(1, bc->port,
					" --> no Ch, so we've already released. (%s)\n",
					manager_isdn_get_info(event));
				return -1;
			}
		}
		if (bc->fac_in.Function != Fac_None) {
			misdn_facility_ie_handler(event, bc, ch);
		}

		bc->need_disconnect = 0;
		bc->need_release = 0;

		hangup_chan(ch, bc);
		release_chan(ch, bc);
		break;
	case EVENT_RELEASE_COMPLETE:
		if (!ch) {
			ch = find_hold_call_l3(cl_te, bc->l3_id);
		}

		bc->need_disconnect = 0;
		bc->need_release = 0;
		bc->need_release_complete = 0;

		if (ch) {
			if (bc->fac_in.Function != Fac_None) {
				misdn_facility_ie_handler(event, bc, ch);
			}

			stop_bc_tones(ch);
			hangup_chan(ch, bc);
			release_chan(ch, bc);
		} else {
#if defined(AST_MISDN_ENHANCEMENTS)
			/*
			 * A call-completion signaling link established with
			 * REGISTER does not have a struct chan_list record
			 * associated with it.
			 */
			AST_LIST_LOCK(&misdn_cc_records_db);
			cc_record = misdn_cc_find_by_bc(bc);
			if (cc_record) {
				/* The call-completion signaling link is closed. */
				misdn_cc_delete(cc_record);
			}
			AST_LIST_UNLOCK(&misdn_cc_records_db);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

			chan_misdn_log(1, bc->port,
				" --> no Ch, so we've already released. (%s)\n",
				manager_isdn_get_info(event));
		}
		break;
	case EVENT_BCHAN_ERROR:
	case EVENT_CLEANUP:
		stop_bc_tones(ch);

		switch (ch->state) {
		case MISDN_CALLING:
			bc->cause = AST_CAUSE_DESTINATION_OUT_OF_ORDER;
			break;
		default:
			break;
		}

		hangup_chan(ch, bc);
		release_chan(ch, bc);
		break;
	case EVENT_TONE_GENERATE:
	{
		int tone_len = bc->tone_cnt;
		struct ast_channel *ast = ch->ast;
		void *tmp;
		int res;
		int (*generate)(struct ast_channel *chan, void *tmp, int datalen, int samples);

		chan_misdn_log(9, bc->port, "TONE_GEN: len:%d\n", tone_len);

		if (!ast) {
			break;
		}

		if (!ast->generator) {
			break;
		}

		tmp = ast->generatordata;
		ast->generatordata = NULL;
		generate = ast->generator->generate;

		if (tone_len < 0 || tone_len > 512) {
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
		break;
	}
	case EVENT_BCHAN_DATA:
		if (ch->bc->AOCD_need_export) {
			export_aoc_vars(ch->originator, ch->ast, ch->bc);
		}
		if (!misdn_cap_is_speech(ch->bc->capability)) {
			struct ast_frame frame;

			/* In Data Modes we queue frames */
			frame.frametype = AST_FRAME_VOICE; /* we have no data frames yet */
			frame.subclass = AST_FORMAT_ALAW;
			frame.datalen = bc->bframe_len;
			frame.samples = bc->bframe_len;
			frame.mallocd = 0;
			frame.offset = 0;
			frame.delivery = ast_tv(0, 0);
			frame.src = NULL;
			frame.data.ptr = bc->bframe;

			if (ch->ast) {
				ast_queue_frame(ch->ast, &frame);
			}
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
					hangup_chan(ch, bc);
					release_chan(ch, bc);
				}
			} else {
				chan_misdn_log(1, bc->port, "Write Pipe full!\n");
			}
		}
		break;
	case EVENT_TIMEOUT:
		if (ch && bc) {
			chan_misdn_log(1, bc->port, "--> state: %s\n", misdn_get_ch_state(ch));
		}

		switch (ch->state) {
		case MISDN_DIALING:
		case MISDN_PROGRESS:
			if (bc->nt && !ch->nttimeout) {
				break;
			}
			/* fall-through */
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
			chan_misdn_log(1, bc->port, " --> in state cleaning .. so ignoring, the stack should clean it for us\n");
			break;
		default:
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
			break;
		}
		break;

	/****************************/
	/** Supplementary Services **/
	/****************************/
	case EVENT_RETRIEVE:
		if (!ch) {
			chan_misdn_log(4, bc->port, " --> no CH, searching for held call\n");
			ch = find_hold_call_l3(cl_te, bc->l3_id);
			if (!ch || ch->hold.state != MISDN_HOLD_ACTIVE) {
				ast_log(LOG_WARNING, "No held call found, cannot Retrieve\n");
				misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
				break;
			}
		}

		/* remember the channel again */
		ch->bc = bc;

		ch->hold.state = MISDN_HOLD_IDLE;
		ch->hold.port = 0;
		ch->hold.channel = 0;

		ast_queue_control(ch->ast, AST_CONTROL_UNHOLD);

		if (misdn_lib_send_event(bc, EVENT_RETRIEVE_ACKNOWLEDGE) < 0) {
			chan_misdn_log(4, bc->port, " --> RETRIEVE_ACK failed\n");
			misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
		}
		break;
	case EVENT_HOLD:
	{
		int hold_allowed;
		struct ast_channel *bridged;

		misdn_cfg_get(bc->port, MISDN_CFG_HOLD_ALLOWED, &hold_allowed, sizeof(hold_allowed));
		if (!hold_allowed) {
			chan_misdn_log(-1, bc->port, "Hold not allowed this port.\n");
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			break;
		}

		bridged = ast_bridged_channel(ch->ast);
		if (bridged) {
			chan_misdn_log(2, bc->port, "Bridge Partner is of type: %s\n", bridged->tech->type);
			ch->l3id = bc->l3_id;

			/* forget the channel now */
			ch->bc = NULL;
			ch->hold.state = MISDN_HOLD_ACTIVE;
			ch->hold.port = bc->port;
			ch->hold.channel = bc->channel;

			ast_queue_control(ch->ast, AST_CONTROL_HOLD);

			misdn_lib_send_event(bc, EVENT_HOLD_ACKNOWLEDGE);
		} else {
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			chan_misdn_log(0, bc->port, "We aren't bridged to anybody\n");
		}
		break;
	}
	case EVENT_NOTIFY:
		if (bc->redirecting.to_changed) {
			/* Add configured prefix to redirecting.to.number */
			misdn_add_number_prefix(bc->port, bc->redirecting.to.number_type,
				bc->redirecting.to.number, sizeof(bc->redirecting.to.number));
		}
		switch (bc->notify_description_code) {
		case mISDN_NOTIFY_CODE_DIVERSION_ACTIVATED:
			/* Ignore for now. */
			bc->redirecting.to_changed = 0;
			break;
		case mISDN_NOTIFY_CODE_CALL_IS_DIVERTING:
			if (bc->redirecting.to_changed) {
				bc->redirecting.to_changed = 0;
				if (ch && ch->ast) {
					switch (ch->state) {
					case MISDN_ALERTING:
						/* Call is deflecting after we have seen an ALERTING message */
						bc->redirecting.reason = mISDN_REDIRECTING_REASON_NO_REPLY;
						break;
					default:
						/* Call is deflecting for call forwarding unconditional or busy reason. */
						bc->redirecting.reason = mISDN_REDIRECTING_REASON_UNKNOWN;
						break;
					}
					misdn_copy_redirecting_to_ast(ch->ast, &bc->redirecting);
					ast_channel_queue_redirecting_update(ch->ast, &ch->ast->redirecting);
				}
			}
			break;
		case mISDN_NOTIFY_CODE_CALL_TRANSFER_ALERTING:
			/*
			 * It would be preferable to update the connected line information
			 * only when the message callStatus is active.  However, the
			 * optional redirection number may not be present in the active
			 * message if an alerting message were received earlier.
			 *
			 * The consequences if we wind up sending two updates is benign.
			 * The other end will think that it got transferred twice.
			 */
			if (bc->redirecting.to_changed) {
				bc->redirecting.to_changed = 0;
				if (ch && ch->ast) {
					misdn_queue_connected_line_update(ch->ast, &bc->redirecting.to,
						AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER_ALERTING);
				}
			}
			break;
		case mISDN_NOTIFY_CODE_CALL_TRANSFER_ACTIVE:
			if (bc->redirecting.to_changed) {
				bc->redirecting.to_changed = 0;
				if (ch && ch->ast) {
					misdn_queue_connected_line_update(ch->ast, &bc->redirecting.to,
						AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER);
				}
			}
			break;
		default:
			bc->redirecting.to_changed = 0;
			chan_misdn_log(0, bc->port," --> not yet handled: notify code:0x%02X\n",
				bc->notify_description_code);
			break;
		}
		break;
	case EVENT_FACILITY:
		if (bc->fac_in.Function == Fac_None) {
			/* This is a FACILITY message so we MUST have a facility ie */
			chan_misdn_log(0, bc->port," --> Missing facility ie or unknown facility ie contents.\n");
		} else {
			misdn_facility_ie_handler(event, bc, ch);
		}
		break;
	case EVENT_RESTART:
		if (!bc->dummy) {
			stop_bc_tones(ch);
			release_chan(ch, bc);
		}
		break;
	default:
		chan_misdn_log(1, 0, "Got Unknown Event\n");
		break;
	}

	return RESPONSE_OK;
}

/** TE STUFF END **/

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief Get call completion record information.
 *
 * \param chan Asterisk channel to operate upon. (Not used)
 * \param function_name Name of the function that called us.
 * \param function_args Argument string passed to function (Could be NULL)
 * \param buf Buffer to put returned string.
 * \param size Size of the supplied buffer including the null terminator.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_cc_read(struct ast_channel *chan, const char *function_name,
	char *function_args, char *buf, size_t size)
{
	char *parse;
	struct misdn_cc_record *cc_record;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cc_id);		/* Call completion record ID value. */
		AST_APP_ARG(get_name);	/* Name of what to get */
		AST_APP_ARG(other);		/* Any extraneous garbage arguments */
	);

	/* Ensure that the buffer is empty */
	*buf = 0;

	if (ast_strlen_zero(function_args)) {
		ast_log(LOG_ERROR, "Function '%s' requires arguments.\n", function_name);
		return -1;
	}

	parse = ast_strdupa(function_args);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!args.argc || ast_strlen_zero(args.cc_id)) {
		ast_log(LOG_ERROR, "Function '%s' missing call completion record ID.\n",
			function_name);
		return -1;
	}
	if (!isdigit(*args.cc_id)) {
		ast_log(LOG_ERROR, "Function '%s' call completion record ID must be numeric.\n",
			function_name);
		return -1;
	}

	if (ast_strlen_zero(args.get_name)) {
		ast_log(LOG_ERROR, "Function '%s' missing what-to-get parameter.\n",
			function_name);
		return -1;
	}

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(atoi(args.cc_id));
	if (cc_record) {
		if (!strcasecmp("a-all", args.get_name)) {
			snprintf(buf, size, "\"%s\" <%s>", cc_record->redial.caller.name,
				cc_record->redial.caller.number);
		} else if (!strcasecmp("a-name", args.get_name)) {
			ast_copy_string(buf, cc_record->redial.caller.name, size);
		} else if (!strncasecmp("a-num", args.get_name, 5)) {
			ast_copy_string(buf, cc_record->redial.caller.number, size);
		} else if (!strcasecmp("a-ton", args.get_name)) {
			snprintf(buf, size, "%d",
				misdn_to_ast_plan(cc_record->redial.caller.number_plan)
				| misdn_to_ast_ton(cc_record->redial.caller.number_type));
		} else if (!strncasecmp("a-pres", args.get_name, 6)) {
			ast_copy_string(buf, ast_named_caller_presentation(
				misdn_to_ast_pres(cc_record->redial.caller.presentation)
				| misdn_to_ast_screen(cc_record->redial.caller.screening)), size);
		} else if (!strcasecmp("a-busy", args.get_name)) {
			ast_copy_string(buf, cc_record->party_a_free ? "no" : "yes", size);
		} else if (!strncasecmp("b-num", args.get_name, 5)) {
			ast_copy_string(buf, cc_record->redial.dialed.number, size);
		} else if (!strcasecmp("b-ton", args.get_name)) {
			snprintf(buf, size, "%d",
				misdn_to_ast_plan(cc_record->redial.dialed.number_plan)
				| misdn_to_ast_ton(cc_record->redial.dialed.number_type));
		} else if (!strcasecmp("port", args.get_name)) {
			snprintf(buf, size, "%d", cc_record->port);
		} else if (!strcasecmp("available-notify-priority", args.get_name)) {
			snprintf(buf, size, "%d", cc_record->remote_user_free.priority);
		} else if (!strcasecmp("available-notify-exten", args.get_name)) {
			ast_copy_string(buf, cc_record->remote_user_free.exten, size);
		} else if (!strcasecmp("available-notify-context", args.get_name)) {
			ast_copy_string(buf, cc_record->remote_user_free.context, size);
		} else if (!strcasecmp("busy-notify-priority", args.get_name)) {
			snprintf(buf, size, "%d", cc_record->b_free.priority);
		} else if (!strcasecmp("busy-notify-exten", args.get_name)) {
			ast_copy_string(buf, cc_record->b_free.exten, size);
		} else if (!strcasecmp("busy-notify-context", args.get_name)) {
			ast_copy_string(buf, cc_record->b_free.context, size);
		} else {
			AST_LIST_UNLOCK(&misdn_cc_records_db);
			ast_log(LOG_ERROR, "Function '%s': Unknown what-to-get '%s'.\n", function_name, args.get_name);
			return -1;
		}
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);

	return 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static struct ast_custom_function misdn_cc_function = {
	.name = "mISDN_CC",
	.synopsis = "Get call completion record information.",
	.syntax = "mISDN_CC(${MISDN_CC_RECORD_ID},<what-to-get>)",
	.desc =
		"mISDN_CC(${MISDN_CC_RECORD_ID},<what-to-get>)\n"
		"The following can be retrieved:\n"
		"\"a-num\", \"a-name\", \"a-all\", \"a-ton\", \"a-pres\", \"a-busy\",\n"
		"\"b-num\", \"b-ton\", \"port\",\n"
		"  User-A is available for call completion:\n"
		"    \"available-notify-priority\",\n"
		"    \"available-notify-exten\",\n"
		"    \"available-notify-context\",\n"
		"  User-A is busy:\n"
		"    \"busy-notify-priority\",\n"
		"    \"busy-notify-exten\",\n"
		"    \"busy-notify-context\"\n",
	.read = misdn_cc_read,
};
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

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

	if (!g_config_initialized) {
		return 0;
	}

	ast_cli_unregister_multiple(chan_misdn_clis, sizeof(chan_misdn_clis) / sizeof(struct ast_cli_entry));

	/* ast_unregister_application("misdn_crypt"); */
	ast_unregister_application("misdn_set_opt");
	ast_unregister_application("misdn_facility");
	ast_unregister_application("misdn_check_l2l1");
#if defined(AST_MISDN_ENHANCEMENTS)
	ast_unregister_application(misdn_command_name);
	ast_custom_function_unregister(&misdn_cc_function);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	ast_channel_unregister(&misdn_tech);

	free_robin_list();
	misdn_cfg_destroy();
	misdn_lib_destroy();

	ast_free(misdn_out_calls);
	ast_free(misdn_in_calls);
	ast_free(misdn_debug_only);
	ast_free(misdn_ports);
	ast_free(misdn_debug);

#if defined(AST_MISDN_ENHANCEMENTS)
	misdn_cc_destroy();
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

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

	if (misdn_cfg_init(max_ports, 0)) {
		ast_log(LOG_ERROR, "Unable to initialize misdn_config.\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	g_config_initialized = 1;

#if defined(AST_MISDN_ENHANCEMENTS)
	misdn_cc_init();
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	misdn_debug = ast_malloc(sizeof(int) * (max_ports + 1));
	if (!misdn_debug) {
		ast_log(LOG_ERROR, "Out of memory for misdn_debug\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	misdn_ports = ast_malloc(sizeof(int) * (max_ports + 1));
	if (!misdn_ports) {
		ast_free(misdn_debug);
		ast_log(LOG_ERROR, "Out of memory for misdn_ports\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	misdn_cfg_get(0, MISDN_GEN_DEBUG, &misdn_debug[0], sizeof(misdn_debug[0]));
	for (i = 1; i <= max_ports; i++) {
		misdn_debug[i] = misdn_debug[0];
		misdn_ports[i] = i;
	}
	*misdn_ports = 0;
	misdn_debug_only = ast_calloc(max_ports + 1, sizeof(int));
	if (!misdn_debug_only) {
		ast_free(misdn_ports);
		ast_free(misdn_debug);
		ast_log(LOG_ERROR, "Out of memory for misdn_debug_only\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	misdn_cfg_get(0, MISDN_GEN_TRACEFILE, tempbuf, sizeof(tempbuf));
	if (!ast_strlen_zero(tempbuf)) {
		tracing = 1;
	}

	misdn_in_calls = ast_malloc(sizeof(int) * (max_ports + 1));
	if (!misdn_in_calls) {
		ast_free(misdn_debug_only);
		ast_free(misdn_ports);
		ast_free(misdn_debug);
		ast_log(LOG_ERROR, "Out of memory for misdn_in_calls\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	misdn_out_calls = ast_malloc(sizeof(int) * (max_ports + 1));
	if (!misdn_out_calls) {
		ast_free(misdn_in_calls);
		ast_free(misdn_debug_only);
		ast_free(misdn_ports);
		ast_free(misdn_debug);
		ast_log(LOG_ERROR, "Out of memory for misdn_out_calls\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	for (i = 1; i <= max_ports; i++) {
		misdn_in_calls[i] = 0;
		misdn_out_calls[i] = 0;
	}

	ast_mutex_init(&cl_te_lock);
	ast_mutex_init(&release_lock);

	misdn_cfg_update_ptp();
	misdn_cfg_get_ports_string(ports);

	if (!ast_strlen_zero(ports)) {
		chan_misdn_log(0, 0, "Got: %s from get_ports\n", ports);
	}
	if (misdn_lib_init(ports, &iface, NULL)) {
		chan_misdn_log(0, 0, "No te ports initialized\n");
	}

	misdn_cfg_get(0, MISDN_GEN_NTDEBUGFLAGS, &ntflags, sizeof(ntflags));
	misdn_cfg_get(0, MISDN_GEN_NTDEBUGFILE, &ntfile, sizeof(ntfile));
	misdn_cfg_get(0, MISDN_GEN_NTKEEPCALLS, &ntkc, sizeof(ntkc));

	misdn_lib_nt_keepcalls(ntkc);
	misdn_lib_nt_debug_init(ntflags, ntfile);

	if (ast_channel_register(&misdn_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", misdn_type);
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
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
		"misdn_check_l2l1(<port>||g:<groupname>,timeout)\n"
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
		);

#if defined(AST_MISDN_ENHANCEMENTS)
	ast_register_application(misdn_command_name, misdn_command_exec, misdn_command_name,
		"misdn_command(<command>[,<options>])\n"
		"The following commands are defined:\n"
		"cc-initialize\n"
		"  Setup mISDN support for call completion\n"
		"  Must call before doing any Dial() involving call completion.\n"
		"ccnr-request,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>\n"
		"  Request Call Completion No Reply activation\n"
		"ccbs-request,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>\n"
		"  Request Call Completion Busy Subscriber activation\n"
		"cc-b-free,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>\n"
		"  Set the dialplan location to notify when User-B is available but User-A is busy.\n"
		"  Setting this dialplan location is optional.\n"
		"cc-a-busy,${MISDN_CC_RECORD_ID},<yes/no>\n"
		"  Set the busy status of call completion User-A\n"
		"cc-deactivate,${MISDN_CC_RECORD_ID}\n"
		"  Deactivate the identified call completion request\n"
		"\n"
		"MISDN_CC_RECORD_ID is set when Dial() returns and call completion is possible\n"
		"MISDN_CC_STATUS is set to ACTIVATED or ERROR after the call completion\n"
		"activation request.\n"
		"MISDN_ERROR_MSG is set to a descriptive message on error.\n"
		);

	ast_custom_function_register(&misdn_cc_function);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	misdn_cfg_get(0, MISDN_GEN_TRACEFILE, global_tracefile, sizeof(global_tracefile));

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

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
* \brief misdn_command arguments container.
*/
AST_DEFINE_APP_ARGS_TYPE(misdn_command_args,
	AST_APP_ARG(name);			/* Subcommand name */
	AST_APP_ARG(arg)[10 + 1];	/* Subcommand arguments */
);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static void misdn_cc_caller_destroy(void *obj)
{
	/* oh snap! */
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
static struct misdn_cc_caller *misdn_cc_caller_alloc(struct ast_channel *chan)
{
	struct misdn_cc_caller *cc_caller;

	if (!(cc_caller = ao2_alloc(sizeof(*cc_caller), misdn_cc_caller_destroy))) {
		return NULL;
	}

	cc_caller->chan = chan;

	return cc_caller;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command(cc-initialize) subcommand handler
 *
 * \param chan Asterisk channel to operate upon.
 * \param subcommand Arguments for the subcommand
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_cc_initialize(struct ast_channel *chan, struct misdn_command_args *subcommand)
{
	struct misdn_cc_caller *cc_caller;
	struct ast_datastore *datastore;

	if (!(cc_caller = misdn_cc_caller_alloc(chan))) {
		return -1;
	}

	if (!(datastore = ast_datastore_alloc(&misdn_cc_ds_info, NULL))) {
		ao2_ref(cc_caller, -1);
		return -1;
	}

	ast_channel_lock(chan);

	/* Inherit reference */
	datastore->data = cc_caller;
	cc_caller = NULL;

	datastore->inheritance = DATASTORE_INHERIT_FOREVER;

	ast_channel_datastore_add(chan, datastore);

	ast_channel_unlock(chan);

	return 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command(cc-deactivate) subcommand handler
 *
 * \details
 * misdn_command(cc-deactivate,${MISDN_CC_RECORD_ID})
 * Deactivate a call completion service instance.
 *
 * \param chan Asterisk channel to operate upon.
 * \param subcommand Arguments for the subcommand
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_cc_deactivate(struct ast_channel *chan, struct misdn_command_args *subcommand)
{
	long record_id;
	const char *error_str;
	struct misdn_cc_record *cc_record;
	struct misdn_bchannel *bc;
	struct misdn_bchannel dummy;

	static const char cmd_help[] = "%s(%s,${MISDN_CC_RECORD_ID})\n";

	if (ast_strlen_zero(subcommand->arg[0]) || !isdigit(*subcommand->arg[0])) {
		ast_log(LOG_WARNING, cmd_help, misdn_command_name, subcommand->name);
		return -1;
	}
	record_id = atol(subcommand->arg[0]);

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(record_id);
	if (cc_record && 0 <= cc_record->port) {
		if (cc_record->ptp) {
			if (cc_record->mode.ptp.bc) {
				/* Close the call-completion signaling link */
				bc = cc_record->mode.ptp.bc;
				bc->fac_out.Function = Fac_None;
				bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
			}
			misdn_cc_delete(cc_record);
		} else if (cc_record->activated) {
			cc_record->error_code = FacError_None;
			cc_record->reject_code = FacReject_None;
			cc_record->invoke_id = ++misdn_invoke_id;
			cc_record->outstanding_message = 1;

			/* Build message */
			misdn_make_dummy(&dummy, cc_record->port, 0, misdn_lib_port_is_nt(cc_record->port), 0);
			dummy.fac_out.Function = Fac_CCBSDeactivate;
			dummy.fac_out.u.CCBSDeactivate.InvokeID = cc_record->invoke_id;
			dummy.fac_out.u.CCBSDeactivate.ComponentType = FacComponent_Invoke;
			dummy.fac_out.u.CCBSDeactivate.Component.Invoke.CCBSReference = cc_record->mode.ptmp.reference_id;

			/* Send message */
			print_facility(&dummy.fac_out, &dummy);
			misdn_lib_send_event(&dummy, EVENT_FACILITY);
		}
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);

	/* Wait for the response to the call completion deactivation request. */
	misdn_cc_response_wait(chan, MISDN_CC_REQUEST_WAIT_MAX, record_id);

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(record_id);
	if (cc_record) {
		if (cc_record->port < 0) {
			/* The network did not tell us that call completion was available. */
			error_str = NULL;
		} else if (cc_record->outstanding_message) {
			cc_record->outstanding_message = 0;
			error_str = misdn_no_response_from_network;
		} else if (cc_record->reject_code != FacReject_None) {
			error_str = misdn_to_str_reject_code(cc_record->reject_code);
		} else if (cc_record->error_code != FacError_None) {
			error_str = misdn_to_str_error_code(cc_record->error_code);
		} else {
			error_str = NULL;
		}

		misdn_cc_delete(cc_record);
	} else {
		error_str = NULL;
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);
	if (error_str) {
		ast_verb(1, "%s(%s) diagnostic '%s' on channel %s\n",
			misdn_command_name, subcommand->name, error_str, chan->name);
		pbx_builtin_setvar_helper(chan, MISDN_ERROR_MSG, error_str);
	}

	return 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command(cc-a-busy) subcommand handler
 *
 * \details
 * misdn_command(cc-a-busy,${MISDN_CC_RECORD_ID},<yes/no>)
 * Set the status of User-A for a call completion service instance.
 *
 * \param chan Asterisk channel to operate upon.
 * \param subcommand Arguments for the subcommand
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_cc_a_busy(struct ast_channel *chan, struct misdn_command_args *subcommand)
{
	long record_id;
	int party_a_free;
	struct misdn_cc_record *cc_record;
	struct misdn_bchannel *bc;

	static const char cmd_help[] = "%s(%s,${MISDN_CC_RECORD_ID},<yes/no>)\n";

	if (ast_strlen_zero(subcommand->arg[0]) || !isdigit(*subcommand->arg[0])) {
		ast_log(LOG_WARNING, cmd_help, misdn_command_name, subcommand->name);
		return -1;
	}
	record_id = atol(subcommand->arg[0]);

	if (ast_true(subcommand->arg[1])) {
		party_a_free = 0;
	} else if (ast_false(subcommand->arg[1])) {
		party_a_free = 1;
	} else {
		ast_log(LOG_WARNING, cmd_help, misdn_command_name, subcommand->name);
		return -1;
	}

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(record_id);
	if (cc_record && cc_record->party_a_free != party_a_free) {
		/* User-A's status has changed */
		cc_record->party_a_free = party_a_free;

		if (cc_record->ptp && cc_record->mode.ptp.bc) {
			cc_record->error_code = FacError_None;
			cc_record->reject_code = FacReject_None;

			/* Build message */
			bc = cc_record->mode.ptp.bc;
			if (cc_record->party_a_free) {
				bc->fac_out.Function = Fac_CCBS_T_Resume;
				bc->fac_out.u.CCBS_T_Resume.InvokeID = ++misdn_invoke_id;
			} else {
				bc->fac_out.Function = Fac_CCBS_T_Suspend;
				bc->fac_out.u.CCBS_T_Suspend.InvokeID = ++misdn_invoke_id;
			}

			/* Send message */
			print_facility(&bc->fac_out, bc);
			misdn_lib_send_event(bc, EVENT_FACILITY);
		}
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);

	return 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command(cc-b-free) subcommand handler
 *
 * \details
 * misdn_command(cc-b-free,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>)
 * Set the dialplan location to notify when User-B is free and User-A is busy.
 *
 * \param chan Asterisk channel to operate upon.
 * \param subcommand Arguments for the subcommand
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_cc_b_free(struct ast_channel *chan, struct misdn_command_args *subcommand)
{
	unsigned index;
	long record_id;
	int priority;
	char *context;
	char *exten;
	struct misdn_cc_record *cc_record;

	static const char cmd_help[] = "%s(%s,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>)\n";

	/* Check that all arguments are present */
	for (index = 0; index < 4; ++index) {
		if (ast_strlen_zero(subcommand->arg[index])) {
			ast_log(LOG_WARNING, cmd_help, misdn_command_name, subcommand->name);
			return -1;
		}
	}

	/* These must be numeric */
	if (!isdigit(*subcommand->arg[0]) || !isdigit(*subcommand->arg[3])) {
		ast_log(LOG_WARNING, cmd_help, misdn_command_name, subcommand->name);
		return -1;
	}

	record_id = atol(subcommand->arg[0]);
	context = subcommand->arg[1];
	exten = subcommand->arg[2];
	priority = atoi(subcommand->arg[3]);

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(record_id);
	if (cc_record) {
		/* Save User-B free information */
		ast_copy_string(cc_record->b_free.context, context, sizeof(cc_record->b_free.context));
		ast_copy_string(cc_record->b_free.exten, exten, sizeof(cc_record->b_free.exten));
		cc_record->b_free.priority = priority;
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);

	return 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
struct misdn_cc_request {
	enum FacFunction ptmp;
	enum FacFunction ptp;
};
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command(ccbs-request/ccnr-request) subcommand handler helper
 *
 * \details
 * misdn_command(ccbs-request,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>)
 * misdn_command(ccnr-request,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>)
 * Set the dialplan location to notify when User-B is free and User-A is free.
 *
 * \param chan Asterisk channel to operate upon.
 * \param subcommand Arguments for the subcommand
 * \param request Which call-completion request message to generate.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_cc_request(struct ast_channel *chan, struct misdn_command_args *subcommand, const struct misdn_cc_request *request)
{
	unsigned index;
	int request_retention;
	long record_id;
	int priority;
	char *context;
	char *exten;
	const char *error_str;
	struct misdn_cc_record *cc_record;
	struct misdn_bchannel *bc;
	struct misdn_bchannel dummy;
	struct misdn_party_id id;

	static const char cmd_help[] = "%s(%s,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>)\n";

	/* Check that all arguments are present */
	for (index = 0; index < 4; ++index) {
		if (ast_strlen_zero(subcommand->arg[index])) {
			ast_log(LOG_WARNING, cmd_help, misdn_command_name, subcommand->name);
			return -1;
		}
	}

	/* These must be numeric */
	if (!isdigit(*subcommand->arg[0]) || !isdigit(*subcommand->arg[3])) {
		ast_log(LOG_WARNING, cmd_help, misdn_command_name, subcommand->name);
		return -1;
	}

	record_id = atol(subcommand->arg[0]);
	context = subcommand->arg[1];
	exten = subcommand->arg[2];
	priority = atoi(subcommand->arg[3]);

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(record_id);
	if (cc_record) {
		/* Save User-B free information */
		ast_copy_string(cc_record->remote_user_free.context, context,
			sizeof(cc_record->remote_user_free.context));
		ast_copy_string(cc_record->remote_user_free.exten, exten,
			sizeof(cc_record->remote_user_free.exten));
		cc_record->remote_user_free.priority = priority;

		if (0 <= cc_record->port) {
			if (cc_record->ptp) {
				if (!cc_record->mode.ptp.bc) {
					bc = misdn_lib_get_register_bc(cc_record->port);
					if (bc) {
						cc_record->mode.ptp.bc = bc;
						cc_record->error_code = FacError_None;
						cc_record->reject_code = FacReject_None;
						cc_record->invoke_id = ++misdn_invoke_id;
						cc_record->outstanding_message = 1;
						cc_record->activation_requested = 1;

						misdn_cfg_get(bc->port, MISDN_CFG_CC_REQUEST_RETENTION,
							&request_retention, sizeof(request_retention));
						cc_record->mode.ptp.requested_retention = request_retention ? 1 : 0;

						/* Build message */
						bc->fac_out.Function = request->ptp;
						bc->fac_out.u.CCBS_T_Request.InvokeID = cc_record->invoke_id;
						bc->fac_out.u.CCBS_T_Request.ComponentType = FacComponent_Invoke;
						bc->fac_out.u.CCBS_T_Request.Component.Invoke.Q931ie =
							cc_record->redial.setup_bc_hlc_llc;
						memset(&id, 0, sizeof(id));
						id.number_plan = cc_record->redial.dialed.number_plan;
						id.number_type = cc_record->redial.dialed.number_type;
						ast_copy_string(id.number, cc_record->redial.dialed.number,
							sizeof(id.number));
						misdn_Address_fill(
							&bc->fac_out.u.CCBS_T_Request.Component.Invoke.Destination,
							&id);
						misdn_Address_fill(
							&bc->fac_out.u.CCBS_T_Request.Component.Invoke.Originating,
							&cc_record->redial.caller);
						bc->fac_out.u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicatorPresent = 1;
						bc->fac_out.u.CCBS_T_Request.Component.Invoke.PresentationAllowedIndicator =
							(cc_record->redial.caller.presentation != 0) ? 0 : 1;
						bc->fac_out.u.CCBS_T_Request.Component.Invoke.RetentionSupported =
							request_retention ? 1 : 0;

						/* Send message */
						print_facility(&bc->fac_out, bc);
						misdn_lib_send_event(bc, EVENT_REGISTER);
					}
				}
			} else {
				cc_record->error_code = FacError_None;
				cc_record->reject_code = FacReject_None;
				cc_record->invoke_id = ++misdn_invoke_id;
				cc_record->outstanding_message = 1;
				cc_record->activation_requested = 1;

				/* Build message */
				misdn_make_dummy(&dummy, cc_record->port, 0,
					misdn_lib_port_is_nt(cc_record->port), 0);
				dummy.fac_out.Function = request->ptmp;
				dummy.fac_out.u.CCBSRequest.InvokeID = cc_record->invoke_id;
				dummy.fac_out.u.CCBSRequest.ComponentType = FacComponent_Invoke;
				dummy.fac_out.u.CCBSRequest.Component.Invoke.CallLinkageID =
					cc_record->mode.ptmp.linkage_id;

				/* Send message */
				print_facility(&dummy.fac_out, &dummy);
				misdn_lib_send_event(&dummy, EVENT_FACILITY);
			}
		}
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);

	/* Wait for the response to the call completion request. */
	misdn_cc_response_wait(chan, MISDN_CC_REQUEST_WAIT_MAX, record_id);

	AST_LIST_LOCK(&misdn_cc_records_db);
	cc_record = misdn_cc_find_by_id(record_id);
	if (cc_record) {
		if (!cc_record->activated) {
			if (cc_record->port < 0) {
				/* The network did not tell us that call completion was available. */
				error_str = "No port number";
			} else if (cc_record->outstanding_message) {
				cc_record->outstanding_message = 0;
				error_str = misdn_no_response_from_network;
			} else if (cc_record->reject_code != FacReject_None) {
				error_str = misdn_to_str_reject_code(cc_record->reject_code);
			} else if (cc_record->error_code != FacError_None) {
				error_str = misdn_to_str_error_code(cc_record->error_code);
			} else if (cc_record->ptp) {
				if (cc_record->mode.ptp.bc) {
					error_str = "Call-completion already requested";
				} else {
					error_str = "Could not allocate call-completion signaling link";
				}
			} else {
				/* Should never happen. */
				error_str = "Unexpected error";
			}

			/* No need to keep the call completion record. */
			if (cc_record->ptp && cc_record->mode.ptp.bc) {
				/* Close the call-completion signaling link */
				bc = cc_record->mode.ptp.bc;
				bc->fac_out.Function = Fac_None;
				bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
			}
			misdn_cc_delete(cc_record);
		} else {
			error_str = NULL;
		}
	} else {
		error_str = misdn_cc_record_not_found;
	}
	AST_LIST_UNLOCK(&misdn_cc_records_db);
	if (error_str) {
		ast_verb(1, "%s(%s) diagnostic '%s' on channel %s\n",
			misdn_command_name, subcommand->name, error_str, chan->name);
		pbx_builtin_setvar_helper(chan, MISDN_ERROR_MSG, error_str);
		pbx_builtin_setvar_helper(chan, MISDN_CC_STATUS, "ERROR");
	} else {
		pbx_builtin_setvar_helper(chan, MISDN_CC_STATUS, "ACTIVATED");
	}

	return 0;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command(ccbs-request) subcommand handler
 *
 * \details
 * misdn_command(ccbs-request,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>)
 * Set the dialplan location to notify when User-B is free and User-A is free.
 *
 * \param chan Asterisk channel to operate upon.
 * \param subcommand Arguments for the subcommand
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_ccbs_request(struct ast_channel *chan, struct misdn_command_args *subcommand)
{
	static const struct misdn_cc_request request = {
		.ptmp = Fac_CCBSRequest,
		.ptp = Fac_CCBS_T_Request
	};

	return misdn_command_cc_request(chan, subcommand, &request);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command(ccnr-request) subcommand handler
 *
 * \details
 * misdn_command(ccnr-request,${MISDN_CC_RECORD_ID},<notify-context>,<user-a-extension>,<priority>)
 * Set the dialplan location to notify when User-B is free and User-A is free.
 *
 * \param chan Asterisk channel to operate upon.
 * \param subcommand Arguments for the subcommand
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_ccnr_request(struct ast_channel *chan, struct misdn_command_args *subcommand)
{
	static const struct misdn_cc_request request = {
		.ptmp = Fac_CCNRRequest,
		.ptp = Fac_CCNR_T_Request
	};

	return misdn_command_cc_request(chan, subcommand, &request);
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
struct misdn_command_table {
	/*! \brief subcommand name */
	const char *name;

	/*! \brief subcommand handler */
	int (*func)(struct ast_channel *chan, struct misdn_command_args *subcommand);

	/*! \brief TRUE if the subcommand can only be executed on mISDN channels */
	int misdn_only;
};
static const struct misdn_command_table misdn_commands[] = {
/* *INDENT-OFF* */
	/* subcommand-name  subcommand-handler              mISDN only */
	{ "cc-initialize",  misdn_command_cc_initialize,    0 },
	{ "cc-deactivate",  misdn_command_cc_deactivate,    0 },
	{ "cc-a-busy",      misdn_command_cc_a_busy,        0 },
	{ "cc-b-free",      misdn_command_cc_b_free,        0 },
	{ "ccbs-request",   misdn_command_ccbs_request,     0 },
	{ "ccnr-request",   misdn_command_ccnr_request,     0 },
/* *INDENT-ON* */
};
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \internal
 * \brief misdn_command() dialplan application.
 *
 * \param chan Asterisk channel to operate upon.
 * \param data Application options string.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int misdn_command_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	unsigned index;
	struct misdn_command_args subcommand;

	if (ast_strlen_zero((char *) data)) {
		ast_log(LOG_ERROR, "%s requires arguments\n", misdn_command_name);
		return -1;
	}

	ast_log(LOG_DEBUG, "%s(%s)\n", misdn_command_name, (char *) data);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(subcommand, parse);
	if (!subcommand.argc || ast_strlen_zero(subcommand.name)) {
		ast_log(LOG_ERROR, "%s requires a subcommand\n", misdn_command_name);
		return -1;
	}

	for (index = 0; index < ARRAY_LEN(misdn_commands); ++index) {
		if (strcasecmp(misdn_commands[index].name, subcommand.name) == 0) {
			strcpy(subcommand.name, misdn_commands[index].name);
			if (misdn_commands[index].misdn_only
				&& strcasecmp(chan->tech->type, misdn_type) != 0) {
				ast_log(LOG_WARNING,
					"%s(%s) only makes sense with %s channels!\n",
					misdn_command_name, subcommand.name, misdn_type);
				return -1;
			}
			return misdn_commands[index].func(chan, &subcommand);
		}
	}

	ast_log(LOG_WARNING, "%s(%s) subcommand is unknown\n", misdn_command_name,
		subcommand.name);
	return -1;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

static int misdn_facility_exec(struct ast_channel *chan, const char *data)
{
	struct chan_list *ch = MISDN_ASTERISK_TECH_PVT(chan);
	char *parse;
	unsigned max_len;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(facility_type);
		AST_APP_ARG(arg)[99];
	);

	chan_misdn_log(0, 0, "TYPE: %s\n", chan->tech->type);

	if (strcasecmp(chan->tech->type, misdn_type)) {
		ast_log(LOG_WARNING, "misdn_facility only makes sense with %s channels!\n", misdn_type);
		return -1;
	}

	if (ast_strlen_zero((char *) data)) {
		ast_log(LOG_WARNING, "misdn_facility requires arguments: facility_type[,<args>]\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.facility_type)) {
		ast_log(LOG_WARNING, "misdn_facility requires arguments: facility_type[,<args>]\n");
		return -1;
	}

	if (!strcasecmp(args.facility_type, "calldeflect")) {
		if (ast_strlen_zero(args.arg[0])) {
			ast_log(LOG_WARNING, "Facility: Call Deflection requires an argument: Number\n");
		}

#if defined(AST_MISDN_ENHANCEMENTS)
		max_len = sizeof(ch->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.Number) - 1;
		if (max_len < strlen(args.arg[0])) {
			ast_log(LOG_WARNING,
				"Facility: Number argument too long (up to %u digits are allowed). Ignoring.\n",
				max_len);
			return 0;
		}
		ch->bc->fac_out.Function = Fac_CallDeflection;
		ch->bc->fac_out.u.CallDeflection.InvokeID = ++misdn_invoke_id;
		ch->bc->fac_out.u.CallDeflection.ComponentType = FacComponent_Invoke;
		ch->bc->fac_out.u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUserPresent = 1;
		ch->bc->fac_out.u.CallDeflection.Component.Invoke.PresentationAllowedToDivertedToUser = 0;
		ch->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.Type = 0;/* unknown */
		ch->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.LengthOfNumber = strlen(args.arg[0]);
		strcpy((char *) ch->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Party.Number, args.arg[0]);
		ch->bc->fac_out.u.CallDeflection.Component.Invoke.Deflection.Subaddress.Length = 0;

#else	/* !defined(AST_MISDN_ENHANCEMENTS) */

		max_len = sizeof(ch->bc->fac_out.u.CDeflection.DeflectedToNumber) - 1;
		if (max_len < strlen(args.arg[0])) {
			ast_log(LOG_WARNING,
				"Facility: Number argument too long (up to %u digits are allowed). Ignoring.\n",
				max_len);
			return 0;
		}
		ch->bc->fac_out.Function = Fac_CD;
		ch->bc->fac_out.u.CDeflection.PresentationAllowed = 0;
		//ch->bc->fac_out.u.CDeflection.DeflectedToSubaddress[0] = 0;
		strcpy((char *) ch->bc->fac_out.u.CDeflection.DeflectedToNumber, args.arg[0]);
#endif	/* !defined(AST_MISDN_ENHANCEMENTS) */

		/* Send message */
		print_facility(&ch->bc->fac_out, ch->bc);
		misdn_lib_send_event(ch->bc, EVENT_FACILITY);
	} else {
		chan_misdn_log(1, ch->bc->port, "Unknown Facility: %s\n", args.facility_type);
	}

	return 0;
}

static int misdn_check_l2l1(struct ast_channel *chan, const char *data)
{
	char *parse;
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

	if (ast_strlen_zero((char *) data)) {
		ast_log(LOG_WARNING, "misdn_check_l2l1 Requires arguments\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 2) {
		ast_log(LOG_WARNING, "Wrong argument count\n");
		return 0;
	}

	/*ast_log(LOG_NOTICE, "Arguments: group/port '%s' timeout '%s'\n", args.grouppar, args.timeout);*/
	timeout = atoi(args.timeout);
	port_str = args.grouppar;

	if (port_str[0] == 'g' && port_str[1] == ':') {
		/* We make a group call lets checkout which ports are in my group */
		port_str += 2;
		ast_copy_string(group, port_str, sizeof(group));
		chan_misdn_log(2, 0, "Checking Ports in group: %s\n", group);

		for (port = misdn_cfg_get_next_port(port);
			port > 0;
			port = misdn_cfg_get_next_port(port)) {
			char cfg_group[BUFFERSIZE + 1];

			chan_misdn_log(2, 0, "trying port %d\n", port);

			misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, sizeof(cfg_group));

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
		chan_misdn_log(2, 0, "Checking Port: %d\n", port);
		port_up = misdn_lib_port_up(port, 1);
		if (!port_up) {
			misdn_lib_get_port_up(port);
			dowait = 1;
		}
	}

	if (dowait) {
		chan_misdn_log(2, 0, "Waiting for '%d' seconds\n", timeout);
		ast_safe_sleep(chan, timeout * 1000);
	}

	return 0;
}

static int misdn_set_opt_exec(struct ast_channel *chan, const char *data)
{
	struct chan_list *ch = MISDN_ASTERISK_TECH_PVT(chan);
	char *tok;
	char *tokb;
	char *parse;
	int keyidx = 0;
	int rxgain = 0;
	int txgain = 0;
	int change_jitter = 0;

	if (strcasecmp(chan->tech->type, misdn_type)) {
		ast_log(LOG_WARNING, "misdn_set_opt makes sense only with %s channels!\n", misdn_type);
		return -1;
	}

	if (ast_strlen_zero((char *) data)) {
		ast_log(LOG_WARNING, "misdn_set_opt Requires arguments\n");
		return -1;
	}

	parse = ast_strdupa(data);
	for (tok = strtok_r(parse, ":", &tokb);
		tok;
		tok = strtok_r(NULL, ":", &tokb)) {
		int neglect = 0;

		if (tok[0] == '!') {
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

			switch (tok[0]) {
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
				break;
			}
			break;
		case 'v':
			tok++;

			switch (tok[0]) {
			case 'r' :
				rxgain = atoi(++tok);
				if (rxgain < -8) {
					rxgain = -8;
				}
				if (rxgain > 8) {
					rxgain = 8;
				}
				ch->bc->rxgain = rxgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n", rxgain);
				break;
			case 't':
				txgain = atoi(++tok);
				if (txgain < -8) {
					txgain = -8;
				}
				if (txgain > 8) {
					txgain = 8;
				}
				ch->bc->txgain = txgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n", txgain);
				break;
			}
			break;
		case 'c':
			keyidx = atoi(++tok);
			{
				char keys[4096];
				char *key = NULL;
				char *tmp = keys;
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
			if (strstr(tok, "allowed")) {
				ch->bc->presentation = 0;
				ch->bc->set_presentation = 1;
			} else if (strstr(tok, "restricted")) {
				ch->bc->presentation = 1;
				ch->bc->set_presentation = 1;
			} else if (strstr(tok, "not_screened")) {
				chan_misdn_log(0, ch->bc->port, "SETOPT: callerpres: not_screened is deprecated\n");
				ch->bc->presentation = 1;
				ch->bc->set_presentation = 1;
			}
			break;
	  	case 'i' :
			chan_misdn_log(1, ch->bc->port, "Ignoring dtmf tones, just use them inband\n");
			ch->ignore_dtmf = 1;
			break;
		default:
			break;
		}
	}

	if (change_jitter) {
		config_jitterbuffer(ch);
	}

	if (ch->faxdetect || ch->ast_dsp) {
		if (!ch->dsp) {
			ch->dsp = ast_dsp_new();
		}
		if (ch->dsp) {
			ast_dsp_set_features(ch->dsp, DSP_FEATURE_DIGIT_DETECT | DSP_FEATURE_FAX_DETECT);
		}
		if (!ch->trans) {
			ch->trans = ast_translator_build_path(AST_FORMAT_SLINEAR, AST_FORMAT_ALAW);
		}
	}

	if (ch->ast_dsp) {
		chan_misdn_log(1, ch->bc->port, "SETOPT: with AST_DSP we deactivate mISDN_dsp\n");
		ch->bc->nodsp = 1;
	}

	return 0;
}


int chan_misdn_jb_empty(struct misdn_bchannel *bc, char *buf, int len)
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

	jb = ast_malloc(sizeof(*jb));
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
	jb->samples = ast_malloc(size * sizeof(char));
	if (!jb->samples) {
		ast_free(jb);
		chan_misdn_log(-1, 0, "No free Mem for jb->samples\n");
		return NULL;
	}

	jb->ok = ast_malloc(size * sizeof(char));
	if (!jb->ok) {
		ast_free(jb->samples);
		ast_free(jb);
		chan_misdn_log(-1, 0, "No free Mem for jb->ok\n");
		return NULL;
	}

	for (i = 0; i < size; i++) {
		jb->ok[i] = 0;
	}

	ast_mutex_init(&jb->mutexjb);

	return jb;
}

/* frees the data and destroys the given jitterbuffer struct */
void misdn_jb_destroy(struct misdn_jb *jb)
{
	ast_mutex_destroy(&jb->mutexjb);

	ast_free(jb->ok);
	ast_free(jb->samples);
	ast_free(jb);
}

/* fills the jitterbuffer with len data returns < 0 if there was an
   error (buffer overflow). */
int misdn_jb_fill(struct misdn_jb *jb, const char *data, int len)
{
	int i;
	int j;
	int rp;
	int wp;

	if (!jb || ! data) {
		return 0;
	}

	ast_mutex_lock(&jb->mutexjb);

	wp = jb->wp;
	rp = jb->rp;

	for (i = 0; i < len; i++) {
		jb->samples[wp] = data[i];
		jb->ok[wp] = 1;
		wp = (wp != jb->size - 1) ? wp + 1 : 0;

		if (wp == jb->rp) {
			jb->state_full = 1;
		}
	}

	if (wp >= rp) {
		jb->state_buffer = wp - rp;
	} else {
		jb->state_buffer = jb->size - rp + wp;
	}
	chan_misdn_log(9, 0, "misdn_jb_fill: written:%d | Buffer status:%d p:%p\n", len, jb->state_buffer, jb);

	if (jb->state_full) {
		jb->wp = wp;

		rp = wp;
		for (j = 0; j < jb->upper_threshold; j++) {
			rp = (rp != 0) ? rp - 1 : jb->size - 1;
		}
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
	int i;
	int wp;
	int rp;
	int read = 0;

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

		if (wp >= rp) {
			jb->state_buffer = wp - rp;
		} else {
			jb->state_buffer = jb->size - rp + wp;
		}
		chan_misdn_log(9, 0, "misdn_jb_empty: read:%d | Buffer status:%d p:%p\n", len, jb->state_buffer, jb);

		jb->rp = rp;
	} else {
		chan_misdn_log(9, 0, "misdn_jb_empty: Wait...requested:%d p:%p\n", len, jb);
	}

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

	if (!(0 <= port && port <= max_ports)) {
		ast_log(LOG_WARNING, "cb_log called with out-of-range port number! (%d)\n", port);
		port = 0;
		level = -1;
	} else if (!(level == -1
		|| (misdn_debug_only[port]
			? (level == 1 && misdn_debug[port]) || level == misdn_debug[port]
			: level <= misdn_debug[port])
		|| (level <= misdn_debug[0] && !ast_strlen_zero(global_tracefile)))) {
		/*
		 * We are not going to print anything so lets not
		 * go to all the work of generating a string.
		 */
		return;
	}

	snprintf(port_buf, sizeof(port_buf), "P[%2d] ", port);
	va_start(ap, tmpl);
	vsnprintf(buf, sizeof(buf), tmpl, ap);
	va_end(ap);

	if (level == -1) {
		ast_log(LOG_WARNING, "%s", buf);
	} else if (misdn_debug_only[port]
		? (level == 1 && misdn_debug[port]) || level == misdn_debug[port]
		: level <= misdn_debug[port]) {
		ast_console_puts(port_buf);
		ast_console_puts(buf);
	}

	if (level <= misdn_debug[0] && !ast_strlen_zero(global_tracefile)) {
		char ctimebuf[30];
		time_t tm;
		char *tmp;
		char *p;
		FILE *fp;

		fp = fopen(global_tracefile, "a+");
		if (!fp) {
			ast_console_puts("Error opening Tracefile: [ ");
			ast_console_puts(global_tracefile);
			ast_console_puts(" ] ");

			ast_console_puts(strerror(errno));
			ast_console_puts("\n");
			return;
		}

		tm = time(NULL);
		tmp = ctime_r(&tm, ctimebuf);
		p = strchr(tmp, '\n');
		if (p) {
			*p = ':';
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
