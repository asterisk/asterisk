/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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
 * \brief Routines implementing call features as call pickup, parking and transfer
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/causes.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/utils.h"
#include "asterisk/adsi.h"
#include "asterisk/devicestate.h"
#include "asterisk/monitor.h"
#include "asterisk/audiohook.h"
#include "asterisk/global_datastores.h"
#include "asterisk/astobj2.h"
#include "asterisk/cel.h"
#include "asterisk/test.h"

/*
 * Party A - transferee
 * Party B - transferer
 * Party C - target of transfer
 *
 * DTMF attended transfer works within the channel bridge.
 * Unfortunately, when either party A or B in the channel bridge
 * hangs up, that channel is not completely hung up until the
 * transfer completes.  This is a real problem depending upon
 * the channel technology involved.
 *
 * For chan_dahdi, the channel is crippled until the hangup is
 * complete.  Either the channel is not useable (analog) or the
 * protocol disconnect messages are held up (PRI/BRI/SS7) and
 * the media is not released.
 *
 * For chan_sip, a call limit of one is going to block that
 * endpoint from any further calls until the hangup is complete.
 *
 * For party A this is a minor problem.  The party A channel
 * will only be in this condition while party B is dialing and
 * when party B and C are conferring.  The conversation between
 * party B and C is expected to be a short one.  Party B is
 * either asking a question of party C or announcing party A.
 * Also party A does not have much incentive to hangup at this
 * point.
 *
 * For party B this can be a major problem during a blonde
 * transfer.  (A blonde transfer is our term for an attended
 * transfer that is converted into a blind transfer. :))  Party
 * B could be the operator.  When party B hangs up, he assumes
 * that he is out of the original call entirely.  The party B
 * channel will be in this condition while party C is ringing,
 * while attempting to recall party B, and while waiting between
 * call attempts.
 *
 * WARNING:
 * The ATXFER_NULL_TECH conditional is a hack to fix the
 * problem.  It will replace the party B channel technology with
 * a NULL channel driver.  The consequences of this code is that
 * the 'h' extension will not be able to access any channel
 * technology specific information like SIP statistics for the
 * call.
 *
 * Uncomment the ATXFER_NULL_TECH define below to replace the
 * party B channel technology in the channel bridge to complete
 * hanging up the channel technology.
 */
//#define ATXFER_NULL_TECH	1

/*** DOCUMENTATION
	<application name="Bridge" language="en_US">
		<synopsis>
			Bridge two channels.
		</synopsis>
		<syntax>
			<parameter name="channel" required="true">
				<para>The current channel is bridged to the specified <replaceable>channel</replaceable>.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="p">
						<para>Play a courtesy tone to <replaceable>channel</replaceable>.</para>
					</option>
					<option name="h">
						<para>Allow the called party to hang up by sending the
						<replaceable>*</replaceable> DTMF digit.</para>
					</option>
					<option name="H">
						<para>Allow the calling party to hang up by pressing the
						<replaceable>*</replaceable> DTMF digit.</para>
					</option>
					<option name="k">
						<para>Allow the called party to enable parking of the call by sending
						the DTMF sequence defined for call parking in features.conf.</para>
					</option>
					<option name="K">
						<para>Allow the calling party to enable parking of the call by sending
						 the DTMF sequence defined for call parking in features.conf.</para>
					</option>
					<option name="L(x[:y][:z])">
						<para>Limit the call to <replaceable>x</replaceable> ms. Play a warning
						when <replaceable>y</replaceable> ms are left. Repeat the warning every
						<replaceable>z</replaceable> ms. The following special variables can be
						used with this option:</para>
						<variablelist>
							<variable name="LIMIT_PLAYAUDIO_CALLER">
								<para>Play sounds to the caller. yes|no (default yes)</para>
							</variable>
							<variable name="LIMIT_PLAYAUDIO_CALLEE">   
								<para>Play sounds to the callee. yes|no</para>
							</variable>
							<variable name="LIMIT_TIMEOUT_FILE">
								<para>File to play when time is up.</para>
							</variable>
							<variable name="LIMIT_CONNECT_FILE">
								<para>File to play when call begins.</para>
							</variable>
							<variable name="LIMIT_WARNING_FILE">
								<para>File to play as warning if <replaceable>y</replaceable> is
								defined. The default is to say the time remaining.</para>
							</variable>
						</variablelist>
					</option>
					<option name="S(x)">
						<para>Hang up the call after <replaceable>x</replaceable> seconds *after* the called party has answered the call.</para>
					</option>
					<option name="t">
						<para>Allow the called party to transfer the calling party by sending the
						DTMF sequence defined in features.conf.</para>
					</option>
					<option name="T">
						<para>Allow the calling party to transfer the called party by sending the
						DTMF sequence defined in features.conf.</para>
					</option>
					<option name="w">
						<para>Allow the called party to enable recording of the call by sending
						the DTMF sequence defined for one-touch recording in features.conf.</para>
					</option>
					<option name="W">
						<para>Allow the calling party to enable recording of the call by sending
						the DTMF sequence defined for one-touch recording in features.conf.</para>
					</option>
					<option name="x">
						<para>Cause the called party to be hung up after the bridge, instead of being
						restarted in the dialplan.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Allows the ability to bridge two channels via the dialplan.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="BRIDGERESULT">
					<para>The result of the bridge attempt as a text string.</para>
					<value name="SUCCESS" />
					<value name="FAILURE" />
					<value name="LOOP" />
					<value name="NONEXISTENT" />
					<value name="INCOMPATIBLE" />
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="ParkedCall" language="en_US">
		<synopsis>
			Answer a parked call.
		</synopsis>
		<syntax>
			<parameter name="exten" required="true" />
		</syntax>
		<description>
			<para>Used to connect to a parked call. This application is always
			registered internally and does not need to be explicitly added
			into the dialplan, although you should include the <literal>parkedcalls</literal>
			context. If no extension is provided, then the first available
			parked call will be acquired.</para>
		</description>
		<see-also>
			<ref type="application">Park</ref>
			<ref type="application">ParkAndAnnounce</ref>
		</see-also>
	</application>
	<application name="Park" language="en_US">
		<synopsis>
			Park yourself.
		</synopsis>
		<syntax>
			<parameter name="timeout">
				<para>A custom parking timeout for this parked call. Value in milliseconds.</para>
			</parameter>
			<parameter name="return_context">
				<para>The context to return the call to after it times out.</para>
			</parameter>
			<parameter name="return_exten">
				<para>The extension to return the call to after it times out.</para>
			</parameter>
			<parameter name="return_priority">
				<para>The priority to return the call to after it times out.</para>
			</parameter>
			<parameter name="options">
				<para>A list of options for this parked call.</para>
				<optionlist>
					<option name="r">
						<para>Send ringing instead of MOH to the parked call.</para>
					</option>
					<option name="R">
						<para>Randomize the selection of a parking space.</para>
					</option>
					<option name="s">
						<para>Silence announcement of the parking space number.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Used to park yourself (typically in combination with a supervised
			transfer to know the parking space). This application is always
			registered internally and does not need to be explicitly added
			into the dialplan, although you should include the <literal>parkedcalls</literal>
			context (or the context specified in <filename>features.conf</filename>).</para>
			<para>If you set the <variable>PARKINGLOT</variable> variable, the call will be parked
			in the specifed parking context. Note setting this variable overrides the <variable>
			PARKINGLOT</variable> set by the <literal>CHANNEL</literal> function.</para>
			<para>If you set the <variable>PARKINGEXTEN</variable> variable to an extension in your
			parking context, Park() will park the call on that extension, unless
			it already exists. In that case, execution will continue at next priority.</para>
			<para>If you set the <variable>PARKINGLOT</variable> variable, Park() will park the call
			in that parkinglot.</para>
			<para>If you set the <variable>PARKINGDYNAMIC</variable> variable, this parkinglot from features.conf
			will be used as template for the newly created dynamic lot.</para>
			<para>If you set the <variable>PARKINGDYNCONTEXT</variable> variable the newly created dynamic
			parking lot will use this context.</para>
			<para>If you set the <variable>PARKINGDYNPOS</variable> variable the newly created dynamic parkinglot
			will use those parking postitions.</para>
		</description>
		<see-also>
			<ref type="application">ParkAndAnnounce</ref>
			<ref type="application">ParkedCall</ref>
		</see-also>
	</application>
	<manager name="ParkedCalls" language="en_US">
		<synopsis>
			List parked calls.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>List parked calls.</para>
		</description>
	</manager>
	<manager name="Park" language="en_US">
		<synopsis>
			Park a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel name to park.</para>
			</parameter>
			<parameter name="Channel2" required="true">
				<para>Channel to announce park info to (and return to if timeout).</para>
			</parameter>
			<parameter name="Timeout">
				<para>Number of milliseconds to wait before callback.</para>
			</parameter>
			<parameter name="Parkinglot">
				<para>Parking lot to park channel in.</para>
			</parameter>
		</syntax>
		<description>
			<para>Park a channel.</para>
		</description>
	</manager>
	<manager name="Bridge" language="en_US">
		<synopsis>
			Bridge two channels already in the PBX.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel1" required="true">
				<para>Channel to Bridge to Channel2.</para>
			</parameter>
			<parameter name="Channel2" required="true">
				<para>Channel to Bridge to Channel1.</para>
			</parameter>
			<parameter name="Tone">
				<para>Play courtesy tone to Channel 2.</para>
				<enumlist>
					<enum name="yes" />
					<enum name="no" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Bridge together two channels already in the PBX.</para>
		</description>
	</manager>
 ***/

#define DEFAULT_PARK_TIME							45000	/*!< ms */
#define DEFAULT_PARK_EXTENSION						"700"
#define DEFAULT_TRANSFER_DIGIT_TIMEOUT				3000	/*!< ms */
#define DEFAULT_FEATURE_DIGIT_TIMEOUT				1000	/*!< ms */
#define DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER	15000	/*!< ms */
#define DEFAULT_ATXFER_DROP_CALL					0		/*!< Do not drop call. */
#define DEFAULT_ATXFER_LOOP_DELAY					10000	/*!< ms */
#define DEFAULT_ATXFER_CALLBACK_RETRIES				2

#define AST_MAX_WATCHERS 256
#define MAX_DIAL_FEATURE_OPTIONS 30

struct feature_group_exten {
	AST_LIST_ENTRY(feature_group_exten) entry;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(exten);
	);
	struct ast_call_feature *feature;
};

struct feature_group {
	AST_LIST_ENTRY(feature_group) entry;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(gname);
	);
	AST_LIST_HEAD_NOLOCK(, feature_group_exten) features;
};

static AST_RWLIST_HEAD_STATIC(feature_groups, feature_group);

static char *parkedcall = "ParkedCall";

static char pickup_ext[AST_MAX_EXTENSION];                 /*!< Call pickup extension */

/*! \brief Description of one parked call, added to a list while active, then removed.
	The list belongs to a parkinglot 
*/
struct parkeduser {
	struct ast_channel *chan;                   /*!< Parking channel */
	struct timeval start;                       /*!< Time the parking started */
	int parkingnum;                             /*!< Parking lot */
	char parkingexten[AST_MAX_EXTENSION];       /*!< If set beforehand, parking extension used for this call */
	char context[AST_MAX_CONTEXT];              /*!< Where to go if our parking time expires */
	char exten[AST_MAX_EXTENSION];
	int priority;
	int parkingtime;                            /*!< Maximum length in parking lot before return */
	unsigned int notquiteyet:1;
	unsigned int options_specified:1;
	char peername[1024];
	unsigned char moh_trys;
	struct ast_parkinglot *parkinglot;
	AST_LIST_ENTRY(parkeduser) list;
};

/*! \brief Structure for parking lots which are put in a container. */
struct ast_parkinglot {
	char name[AST_MAX_CONTEXT];
	char parkext[AST_MAX_EXTENSION];				/*!< Parkingextension */
	char parking_con[AST_MAX_EXTENSION];		/*!< Context for which parking is made accessible */
	char parking_con_dial[AST_MAX_EXTENSION];	/*!< Context for dialback for parking (KLUDGE) */
	int parking_start;				/*!< First available extension for parking */
	int parking_stop;				/*!< Last available extension for parking */
	int parking_offset;
	int parkfindnext;
	int parkingtime;				/*!< Default parking time */
	char mohclass[MAX_MUSICCLASS];                  /*!< Music class used for parking */
	int parkaddhints;                               /*!< Add parking hints automatically */
	int parkedcalltransfers;                        /*!< Enable DTMF based transfers on bridge when picking up parked calls */
	int parkedcallreparking;                        /*!< Enable DTMF based parking on bridge when picking up parked calls */
	int parkedcallhangup;                           /*!< Enable DTMF based hangup on a bridge when pickup up parked calls */
	int parkedcallrecording;                        /*!< Enable DTMF based recording on a bridge when picking up parked calls */
	unsigned short the_mark:1;                      /*!< Used during reloads, that which bears the_mark shall be deleted! */
	AST_LIST_HEAD(parkinglot_parklist, parkeduser) parkings; /*!< List of active parkings in this parkinglot */
};

/*! \brief The list of parking lots configured. Always at least one  - the default parking lot */
static struct ao2_container *parkinglots;
 
struct ast_parkinglot *default_parkinglot;
char parking_ext[AST_MAX_EXTENSION];            /*!< Extension you type to park the call */

static char courtesytone[256];                             /*!< Courtesy tone */
static int parkedplay = 0;                                 /*!< Who to play the courtesy tone to */
static int parkeddynamic = 0;                              /*!< Enable creation of parkinglots dynamically */
static char xfersound[256];                                /*!< Call transfer sound */
static char xferfailsound[256];                            /*!< Call transfer failure sound */
static char pickupsound[256];                              /*!< Pickup sound */
static char pickupfailsound[256];                          /*!< Pickup failure sound */

static int adsipark;

static int transferdigittimeout;
static int featuredigittimeout;
static int comebacktoorigin = 1;

static int atxfernoanswertimeout;
static unsigned int atxferdropcall;
static unsigned int atxferloopdelay;
static unsigned int atxfercallbackretries;

static char *registrar = "features";		   /*!< Registrar for operations */

/* module and CLI command definitions */
static char *parkcall = PARK_APP_NAME;

static struct ast_app *monitor_app = NULL;
static int monitor_ok = 1;

static struct ast_app *mixmonitor_app = NULL;
static int mixmonitor_ok = 1;

static struct ast_app *stopmixmonitor_app = NULL;
static int stopmixmonitor_ok = 1;

static pthread_t parking_thread;
struct ast_dial_features {
	struct ast_flags features_caller;
	struct ast_flags features_callee;
	int is_caller;
};

#if defined(ATXFER_NULL_TECH)
static struct ast_frame *null_read(struct ast_channel *chan)
{
	/* Hangup channel. */
	return NULL;
}

static struct ast_frame *null_exception(struct ast_channel *chan)
{
	/* Hangup channel. */
	return NULL;
}

static int null_write(struct ast_channel *chan, struct ast_frame *frame)
{
	/* Hangup channel. */
	return -1;
}

static int null_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	/* No problem fixing up the channel. */
	return 0;
}

static int null_hangup(struct ast_channel *chan)
{
	chan->tech_pvt = NULL;
	return 0;
}

static const struct ast_channel_tech null_tech = {
	.type = "NULL",
	.description = "NULL channel driver for atxfer",
	.capabilities = -1,
	.read = null_read,
	.exception = null_exception,
	.write = null_write,
	.fixup = null_fixup,
	.hangup = null_hangup,
};
#endif	/* defined(ATXFER_NULL_TECH) */

#if defined(ATXFER_NULL_TECH)
/*!
 * \internal
 * \brief Set the channel technology to the NULL technology.
 *
 * \param chan Channel to change technology.
 *
 * \return Nothing
 */
static void set_null_chan_tech(struct ast_channel *chan)
{
	int idx;

	ast_channel_lock(chan);

	/* Hangup the channel's physical side */
	if (chan->tech->hangup) {
		chan->tech->hangup(chan);
	}
	if (chan->tech_pvt) {
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n",
			chan->name);
		ast_free(chan->tech_pvt);
		chan->tech_pvt = NULL;
	}

	/* Install the NULL technology and wake up anyone waiting on it. */
	chan->tech = &null_tech;
	for (idx = 0; idx < AST_MAX_FDS; ++idx) {
		switch (idx) {
		case AST_ALERT_FD:
		case AST_TIMING_FD:
		case AST_GENERATOR_FD:
			/* Don't clear these fd's. */
			break;
		default:
			ast_channel_set_fd(chan, idx, -1);
			break;
		}
	}
	ast_queue_frame(chan, &ast_null_frame);

	ast_channel_unlock(chan);
}
#endif	/* defined(ATXFER_NULL_TECH) */

#if defined(ATXFER_NULL_TECH)
/*!
 * \internal
 * \brief Set the channel name to something unique.
 *
 * \param chan Channel to change name.
 *
 * \return Nothing
 */
static void set_new_chan_name(struct ast_channel *chan)
{
	static int seq_num_last;
	int seq_num;
	int len;
	char *chan_name;
	char dummy[1];

	/* Create the new channel name string. */
	ast_channel_lock(chan);
	seq_num = ast_atomic_fetchadd_int(&seq_num_last, +1);
	len = snprintf(dummy, sizeof(dummy), "%s<XFER_%x>", chan->name, seq_num) + 1;
	chan_name = alloca(len);
	snprintf(chan_name, len, "%s<XFER_%x>", chan->name, seq_num);
	ast_channel_unlock(chan);

	ast_change_name(chan, chan_name);
}
#endif	/* defined(ATXFER_NULL_TECH) */

static void *dial_features_duplicate(void *data)
{
	struct ast_dial_features *df = data, *df_copy;
 
 	if (!(df_copy = ast_calloc(1, sizeof(*df)))) {
 		return NULL;
 	}
 
 	memcpy(df_copy, df, sizeof(*df));
 
 	return df_copy;
}

static void dial_features_destroy(void *data)
{
 	struct ast_dial_features *df = data;
 	if (df) {
 		ast_free(df);
 	}
}

static const struct ast_datastore_info dial_features_info = {
 	.type = "dial-features",
 	.destroy = dial_features_destroy,
 	.duplicate = dial_features_duplicate,
 };
 
/* Forward declarations */
static struct ast_parkinglot *parkinglot_addref(struct ast_parkinglot *parkinglot);
static void parkinglot_unref(struct ast_parkinglot *parkinglot);
static void parkinglot_destroy(void *obj);
int manage_parkinglot(struct ast_parkinglot *curlot, const struct pollfd *pfds, const int nfds, struct pollfd **new_pfds, int *new_nfds, int *fs);
struct ast_parkinglot *find_parkinglot(const char *name);
static struct ast_parkinglot *create_parkinglot(const char *name);
static struct ast_parkinglot *copy_parkinglot(const char *name, const struct ast_parkinglot *parkinglot);

static int find_parkinglot_by_position_cb(void *obj, void *args, int flags)
{
	struct ast_parkinglot *parkinglot = obj;
	int *parkpos = args;

	if (*parkpos >= parkinglot->parking_start && *parkpos <= parkinglot->parking_stop) {
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

static int find_parkinglot_by_exten_cb(void *obj, void *args, int flags)
{
	struct ast_parkinglot *parkinglot = obj;
	const char *parkext = args;

	if (!strcmp(parkinglot->parkext, parkext)) {
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

int ast_parking_ext_valid(const char *exten_str, struct ast_channel *chan, const char *context)
{
	struct ast_exten *exten;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	const char *app_at_exten;

	exten = pbx_find_extension(chan, NULL, &q, context, exten_str, 1, NULL, NULL, E_MATCH);
	if (!exten) {
		return 0;
	}

	app_at_exten = ast_get_extension_app(exten);
	if (!app_at_exten || strcmp(PARK_APP_NAME, app_at_exten)) {
		return 0;
	}

	return 1;
}

const char *ast_pickup_ext(void)
{
	return pickup_ext;
}

struct ast_bridge_thread_obj 
{
	struct ast_bridge_config bconfig;
	struct ast_channel *chan;
	struct ast_channel *peer;
	unsigned int return_to_pbx:1;
};

static int parkinglot_hash_cb(const void *obj, const int flags)
{
	const struct ast_parkinglot *parkinglot = obj;

	return ast_str_case_hash(parkinglot->name);
}

static int parkinglot_cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_parkinglot *parkinglot = obj, *parkinglot2 = arg;

	return !strcasecmp(parkinglot->name, parkinglot2->name) ? CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \brief store context, extension and priority 
 * \param chan, context, ext, pri
*/
static void set_c_e_p(struct ast_channel *chan, const char *context, const char *ext, int pri)
{
	ast_copy_string(chan->context, context, sizeof(chan->context));
	ast_copy_string(chan->exten, ext, sizeof(chan->exten));
	chan->priority = pri;
}

/*!
 * \brief Check goto on transfer
 * \param chan
 *
 * Check if channel has 'GOTO_ON_BLINDXFR' set, if not exit.
 * When found make sure the types are compatible. Check if channel is valid
 * if so start the new channel else hangup the call. 
*/
static void check_goto_on_transfer(struct ast_channel *chan) 
{
	struct ast_channel *xferchan;
	const char *val = pbx_builtin_getvar_helper(chan, "GOTO_ON_BLINDXFR");
	char *x, *goto_on_transfer;
	struct ast_frame *f;

	if (ast_strlen_zero(val))
		return;

	goto_on_transfer = ast_strdupa(val);

	if (!(xferchan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", chan->linkedid, 0, "%s", chan->name)))
		return;

	for (x = goto_on_transfer; x && *x; x++) {
		if (*x == '^')
			*x = ',';
	}
	/* Make formats okay */
	xferchan->readformat = chan->readformat;
	xferchan->writeformat = chan->writeformat;
	ast_channel_masquerade(xferchan, chan);
	ast_parseable_goto(xferchan, goto_on_transfer);
	xferchan->_state = AST_STATE_UP;
	ast_clear_flag(xferchan, AST_FLAGS_ALL);	
	ast_channel_clear_softhangup(xferchan, AST_SOFTHANGUP_ALL);
	if ((f = ast_read(xferchan))) {
		ast_frfree(f);
		f = NULL;
		ast_pbx_start(xferchan);
	} else {
		ast_hangup(xferchan);
	}
}

static struct ast_channel *feature_request_and_dial(struct ast_channel *caller,
	const char *caller_name, struct ast_channel *requestor,
	struct ast_channel *transferee, const char *type, struct ast_format_cap *cap, void *data,
	int timeout, int *outstate, const char *language);

/*!
 * \brief bridge the call 
 * \param data thread bridge.
 *
 * Set Last Data for respective channels, reset cdr for channels
 * bridge call, check if we're going back to dialplan
 * if not hangup both legs of the call
*/
static void *bridge_call_thread(void *data)
{
	struct ast_bridge_thread_obj *tobj = data;
	int res;

	tobj->chan->appl = !tobj->return_to_pbx ? "Transferred Call" : "ManagerBridge";
	tobj->chan->data = tobj->peer->name;
	tobj->peer->appl = !tobj->return_to_pbx ? "Transferred Call" : "ManagerBridge";
	tobj->peer->data = tobj->chan->name;

	ast_bridge_call(tobj->peer, tobj->chan, &tobj->bconfig);

	if (tobj->return_to_pbx) {
		if (!ast_check_hangup(tobj->peer)) {
			ast_log(LOG_VERBOSE, "putting peer %s into PBX again\n", tobj->peer->name);
			res = ast_pbx_start(tobj->peer);
			if (res != AST_PBX_SUCCESS)
				ast_log(LOG_WARNING, "FAILED continuing PBX on peer %s\n", tobj->peer->name);
		} else
			ast_hangup(tobj->peer);
		if (!ast_check_hangup(tobj->chan)) {
			ast_log(LOG_VERBOSE, "putting chan %s into PBX again\n", tobj->chan->name);
			res = ast_pbx_start(tobj->chan);
			if (res != AST_PBX_SUCCESS)
				ast_log(LOG_WARNING, "FAILED continuing PBX on chan %s\n", tobj->chan->name);
		} else
			ast_hangup(tobj->chan);
	} else {
		ast_hangup(tobj->chan);
		ast_hangup(tobj->peer);
	}

	ast_free(tobj);

	return NULL;
}

/*!
 * \brief create thread for the parked call
 * \param data
 *
 * Create thread and attributes, call bridge_call_thread
*/
static void bridge_call_thread_launch(void *data) 
{
	pthread_t thread;
	pthread_attr_t attr;
	struct sched_param sched;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create(&thread, &attr, bridge_call_thread, data);
	pthread_attr_destroy(&attr);
	memset(&sched, 0, sizeof(sched));
	pthread_setschedparam(thread, SCHED_RR, &sched);
}

/*!
 * \brief Announce call parking by ADSI
 * \param chan .
 * \param parkingexten .
 * Create message to show for ADSI, display message.
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int adsi_announce_park(struct ast_channel *chan, char *parkingexten)
{
	int res;
	int justify[5] = {ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT};
	char tmp[256];
	char *message[5] = {NULL, NULL, NULL, NULL, NULL};

	snprintf(tmp, sizeof(tmp), "Parked on %s", parkingexten);
	message[0] = tmp;
	res = ast_adsi_load_session(chan, NULL, 0, 1);
	if (res == -1)
		return res;
	return ast_adsi_print(chan, message, justify, 1);
}

/*! \brief Find parking lot name from channel */
static const char *findparkinglotname(struct ast_channel *chan)
{
	const char *temp, *parkinglot = NULL;

	/* Check if the channel has a parking lot */
	if (!ast_strlen_zero(chan->parkinglot))
		parkinglot = chan->parkinglot;

	/* Channel variables override everything */

	if ((temp  = pbx_builtin_getvar_helper(chan, "PARKINGLOT")))
		return temp;

	return parkinglot;
}

/*! \brief Notify metermaids that we've changed an extension */
static void notify_metermaids(const char *exten, char *context, enum ast_device_state state)
{
	ast_debug(4, "Notification of state change to metermaids %s@%s\n to state '%s'",
		exten, context, ast_devstate2str(state));

	ast_devstate_changed(state, "park:%s@%s", exten, context);
}

/*! \brief metermaids callback from devicestate.c */
static enum ast_device_state metermaidstate(const char *data)
{
	char *context;
	char *exten;

	context = ast_strdupa(data);

	exten = strsep(&context, "@");
	if (!context)
		return AST_DEVICE_INVALID;

	ast_debug(4, "Checking state of exten %s in context %s\n", exten, context);

	if (!ast_exists_extension(NULL, context, exten, 1, NULL))
		return AST_DEVICE_NOT_INUSE;

	return AST_DEVICE_INUSE;
}

/*! Options to pass to park_call_full */
enum ast_park_call_options {
	/*! Provide ringing to the parked caller instead of music on hold */
	AST_PARK_OPT_RINGING =   (1 << 0),
	/*! Randomly choose a parking spot for the caller instead of choosing
	 *  the first one that is available. */
	AST_PARK_OPT_RANDOMIZE = (1 << 1),
	/*! Do not announce the parking number */
	AST_PARK_OPT_SILENCE = (1 << 2),
};

struct ast_park_call_args {
	/*! How long to wait in the parking lot before the call gets sent back
	 *  to the specified return extension (or a best guess at where it came
	 *  from if not explicitly specified). */
	int timeout;
	/*! An output parameter to store the parking space where the parked caller
	 *  was placed. */
	int *extout;
	const char *orig_chan_name;
	const char *return_con;
	const char *return_ext;
	int return_pri;
	uint32_t flags;
	/*! Parked user that has already obtained a parking space */
	struct parkeduser *pu;
	struct ast_parkinglot *parkinglot; /*! parkinglot to be parked in, based on parkext */
};

static struct parkeduser *park_space_reserve(struct ast_channel *chan, struct ast_channel *peer, struct ast_park_call_args *args)
{
	struct parkeduser *pu;
	int i, parking_space = -1, parking_range;
	const char *parkinglotname = NULL;
	const char *parkingexten;
	struct ast_parkinglot *parkinglot = NULL;

	if (args->parkinglot) {
		parkinglot = args->parkinglot;
		parkinglotname = parkinglot->name;
	} else if (peer) {
		parkinglotname = findparkinglotname(peer);
	} else { /* peer was NULL, check chan (ParkAndAnnounce / res_agi) */
		parkinglotname = findparkinglotname(chan);
	}

	if (!args->parkinglot) {
		if (parkinglotname) {
			parkinglot = find_parkinglot(parkinglotname);
		} else {
			ast_debug(4, "This could be an indication channel driver needs updating, using default lot.\n");
			parkinglot = parkinglot_addref(default_parkinglot);
		}
		ast_debug(1, "Found chanvar Parkinglot: %s\n", parkinglot->name);
	}

	/* Dynamically create parkinglot */
	if (!parkinglot && parkeddynamic && !ast_strlen_zero(parkinglotname)) {
		const char *dyn_context, *dyn_range;
		const char *parkinglotname_copy = NULL;
		struct ast_parkinglot *parkinglot_copy = NULL;
		int dyn_start, dyn_end;

		ast_channel_lock(chan);
		parkinglotname_copy = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGDYNAMIC"), ""));
		dyn_context = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGDYNCONTEXT"), ""));
		dyn_range = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGDYNPOS"), ""));
		ast_channel_unlock(chan);

		if (!ast_strlen_zero(parkinglotname_copy)) {
			parkinglot_copy = find_parkinglot(parkinglotname_copy);
		}
		if (!parkinglot_copy) {
			parkinglot_copy = parkinglot_addref(default_parkinglot);
			ast_debug(1, "Using default parking lot for copy\n");
		}
		if (!(parkinglot = copy_parkinglot(parkinglotname, parkinglot_copy))) {
			ast_log(LOG_ERROR, "Could not build dynamic parking lot!\n");
		} else {
			if (!ast_strlen_zero(dyn_context)) {
				ast_copy_string(parkinglot->parking_con, dyn_context, sizeof(parkinglot->parking_con));
			}
			if (!ast_strlen_zero(dyn_range)) {
				if (sscanf(dyn_range, "%30d-%30d", &dyn_start, &dyn_end) != 2) {
					ast_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers\n");
				} else {
					parkinglot->parking_start = dyn_start;
					parkinglot->parking_stop = dyn_end;
				}
			}
			ao2_link(parkinglots, parkinglot);
		}

		if (parkinglot_copy) {
			/* unref our tempory copy */
			parkinglot_unref(parkinglot_copy);
			parkinglot_copy = NULL;
		}
	}

	if (!parkinglot) {
		parkinglot = parkinglot_addref(default_parkinglot);
	}

	ast_debug(1, "Parkinglot: %s\n", parkinglot->name);

	/* Allocate memory for parking data */
	if (!(pu = ast_calloc(1, sizeof(*pu)))) {
		parkinglot_unref(parkinglot);
		return NULL;
	}

	/* Lock parking list */
	AST_LIST_LOCK(&parkinglot->parkings);
	/* Check for channel variable PARKINGEXTEN */
	ast_channel_lock(chan);
	parkingexten = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGEXTEN"), ""));
	ast_channel_unlock(chan);
	if (!ast_strlen_zero(parkingexten)) {
		/*!\note The API forces us to specify a numeric parking slot, even
		 * though the architecture would tend to support non-numeric extensions
		 * (as are possible with SIP, for example).  Hence, we enforce that
		 * limitation here.  If extout was not numeric, we could permit
		 * arbitrary non-numeric extensions.
		 */
	if (sscanf(parkingexten, "%30d", &parking_space) != 1 || parking_space < 0) {
			AST_LIST_UNLOCK(&parkinglot->parkings);
			parkinglot_unref(parkinglot);
	    free(pu);
	    ast_log(LOG_WARNING, "PARKINGEXTEN does not indicate a valid parking slot: '%s'.\n", parkingexten);
	    return NULL;
	}
	snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", parking_space);

		if (ast_exists_extension(NULL, parkinglot->parking_con, pu->parkingexten, 1, NULL)) {
			ast_log(LOG_WARNING, "Requested parking extension already exists: %s@%s\n", parkingexten, parkinglot->parking_con);
			AST_LIST_UNLOCK(&parkinglot->parkings);
			parkinglot_unref(parkinglot);
			ast_free(pu);
			return NULL;
		}
	} else {
		int start;
		struct parkeduser *cur = NULL;

		/* Select parking space within range */
		parking_range = parkinglot->parking_stop - parkinglot->parking_start + 1;

		if (ast_test_flag(args, AST_PARK_OPT_RANDOMIZE)) {
			start = ast_random() % (parkinglot->parking_stop - parkinglot->parking_start + 1);
		} else {
			start = parkinglot->parking_start;
		}

		for (i = start; 1; i++) {
			if (i == parkinglot->parking_stop + 1) {
				i = parkinglot->parking_start - 1;
				break;
			}

			AST_LIST_TRAVERSE(&parkinglot->parkings, cur, list) {
				if (cur->parkingnum == i) {
					break;
				}
			}
			if (!cur) {
				parking_space = i;
				break;
			}
		}

		if (i == start - 1 && cur) {
			ast_log(LOG_WARNING, "No more parking spaces\n");
			ast_free(pu);
			AST_LIST_UNLOCK(&parkinglot->parkings);
			parkinglot_unref(parkinglot);
			return NULL;
		}
		/* Set pointer for next parking */
		if (parkinglot->parkfindnext) 
			parkinglot->parking_offset = parking_space - parkinglot->parking_start + 1;
		snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", parking_space);
	}

	pu->notquiteyet = 1;
	pu->parkingnum = parking_space;
	pu->parkinglot = parkinglot;
	AST_LIST_INSERT_TAIL(&parkinglot->parkings, pu, list);

	return pu;
}

/* Park a call */
static int park_call_full(struct ast_channel *chan, struct ast_channel *peer, struct ast_park_call_args *args)
{
	struct ast_context *con;
	int parkingnum_copy;
	struct parkeduser *pu = args->pu;
	const char *event_from;

	if (pu == NULL)
		args->pu = pu = park_space_reserve(chan, peer, args);
	if (pu == NULL)
		return 1; /* Continue execution if possible */

	snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", pu->parkingnum);

	chan->appl = "Parked Call";
	chan->data = NULL;

	pu->chan = chan;

	/* Put the parked channel on hold if we have two different channels */
	if (chan != peer) {
		if (ast_test_flag(args, AST_PARK_OPT_RINGING)) {
			ast_indicate(pu->chan, AST_CONTROL_RINGING);
		} else {
			ast_indicate_data(pu->chan, AST_CONTROL_HOLD, 
				S_OR(pu->parkinglot->mohclass, NULL),
				!ast_strlen_zero(pu->parkinglot->mohclass) ? strlen(pu->parkinglot->mohclass) + 1 : 0);
		}
	}
	
	pu->start = ast_tvnow();
	pu->parkingtime = (args->timeout > 0) ? args->timeout : pu->parkinglot->parkingtime;
	parkingnum_copy = pu->parkingnum;
	if (args->extout)
		*(args->extout) = pu->parkingnum;

	if (peer) { 
		/* This is so ugly that it hurts, but implementing get_base_channel() on local channels
			could have ugly side effects.  We could have transferer<->local,1<->local,2<->parking
			and we need the callback name to be that of transferer.  Since local,1/2 have the same
			name we can be tricky and just grab the bridged channel from the other side of the local
		*/
		if (!strcasecmp(peer->tech->type, "Local")) {
			struct ast_channel *tmpchan, *base_peer;
			char other_side[AST_CHANNEL_NAME];
			char *c;
			ast_copy_string(other_side, S_OR(args->orig_chan_name, peer->name), sizeof(other_side));
			if ((c = strrchr(other_side, ';'))) {
				*++c = '1';
			}
			if ((tmpchan = ast_channel_get_by_name(other_side))) {
				ast_channel_lock(tmpchan);
				if ((base_peer = ast_bridged_channel(tmpchan))) {
					ast_copy_string(pu->peername, base_peer->name, sizeof(pu->peername));
				}
				ast_channel_unlock(tmpchan);
				tmpchan = ast_channel_unref(tmpchan);
			}
		} else {
			ast_copy_string(pu->peername, S_OR(args->orig_chan_name, peer->name), sizeof(pu->peername));
		}
	}

	/* Remember what had been dialed, so that if the parking
	   expires, we try to come back to the same place */

	pu->options_specified = (!ast_strlen_zero(args->return_con) || !ast_strlen_zero(args->return_ext) || args->return_pri);

	/* If extension has options specified, they override all other possibilities
	such as the returntoorigin flag and transferred context. Information on
	extension options is lost here, so we set a flag */

	ast_copy_string(pu->context, 
		S_OR(args->return_con, S_OR(chan->macrocontext, chan->context)), 
		sizeof(pu->context));
	ast_copy_string(pu->exten, 
		S_OR(args->return_ext, S_OR(chan->macroexten, chan->exten)), 
		sizeof(pu->exten));
	pu->priority = args->return_pri ? args->return_pri : 
		(chan->macropriority ? chan->macropriority : chan->priority);

	/* If parking a channel directly, don't quiet yet get parking running on it.
	 * All parking lot entries are put into the parking lot with notquiteyet on. */
	if (peer != chan) 
		pu->notquiteyet = 0;

	/* Wake up the (presumably select()ing) thread */
	pthread_kill(parking_thread, SIGURG);
	ast_verb(2, "Parked %s on %d (lot %s). Will timeout back to extension [%s] %s, %d in %d seconds\n", pu->chan->name, pu->parkingnum, pu->parkinglot->name, pu->context, pu->exten, pu->priority, (pu->parkingtime/1000));

	ast_cel_report_event(pu->chan, AST_CEL_PARK_START, NULL, pu->parkinglot->name, peer);

	if (peer) {
		event_from = peer->name;
	} else {
		event_from = pbx_builtin_getvar_helper(chan, "BLINDTRANSFER");
	}

	ast_manager_event(pu->chan, EVENT_FLAG_CALL, "ParkedCall",
		"Exten: %s\r\n"
		"Channel: %s\r\n"
		"Parkinglot: %s\r\n"
		"From: %s\r\n"
		"Timeout: %ld\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"Uniqueid: %s\r\n",
		pu->parkingexten, pu->chan->name, pu->parkinglot->name, event_from ? event_from : "",
		(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL),
		S_COR(pu->chan->caller.id.number.valid, pu->chan->caller.id.number.str, "<unknown>"),
		S_COR(pu->chan->caller.id.name.valid, pu->chan->caller.id.name.str, "<unknown>"),
		pu->chan->uniqueid
		);

	if (peer && adsipark && ast_adsi_available(peer)) {
		adsi_announce_park(peer, pu->parkingexten);	/* Only supports parking numbers */
		ast_adsi_unload_session(peer);
	}

	con = ast_context_find_or_create(NULL, NULL, pu->parkinglot->parking_con, registrar);
	if (!con)	/* Still no context? Bad */
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", pu->parkinglot->parking_con);
	if (con) {
		if (!ast_add_extension2(con, 1, pu->parkingexten, 1, NULL, NULL, parkedcall, ast_strdup(pu->parkingexten), ast_free_ptr, registrar))
			notify_metermaids(pu->parkingexten, pu->parkinglot->parking_con, AST_DEVICE_INUSE);
	}

	AST_LIST_UNLOCK(&pu->parkinglot->parkings);

	/* Only say number if it's a number and the channel hasn't been masqueraded away */
	if (peer && !ast_test_flag(args, AST_PARK_OPT_SILENCE) && (ast_strlen_zero(args->orig_chan_name) || !strcasecmp(peer->name, args->orig_chan_name))) {
		/* If a channel is masqueraded into peer while playing back the parking slot number do not continue playing it back. This is the case if an attended transfer occurs. */
		ast_set_flag(peer, AST_FLAG_MASQ_NOSTREAM);
		/* Tell the peer channel the number of the parking space */
		ast_say_digits(peer, pu->parkingnum, "", peer->language);
		ast_clear_flag(peer, AST_FLAG_MASQ_NOSTREAM);
	}
	if (peer == chan) { /* pu->notquiteyet = 1 */
		/* Wake up parking thread if we're really done */
		ast_indicate_data(pu->chan, AST_CONTROL_HOLD, 
			S_OR(pu->parkinglot->mohclass, NULL),
			!ast_strlen_zero(pu->parkinglot->mohclass) ? strlen(pu->parkinglot->mohclass) + 1 : 0);
		pu->notquiteyet = 0;
		pthread_kill(parking_thread, SIGURG);
	}
	return 0;
}

/*! \brief Park a call */
int ast_park_call(struct ast_channel *chan, struct ast_channel *peer, int timeout, const char *parkexten, int *extout)
{
	struct ast_parkinglot *found_lot = ao2_callback(parkinglots, 0, find_parkinglot_by_exten_cb, (void *) parkexten);

	struct ast_park_call_args args = {
		.timeout = timeout,
		.extout = extout,
		.parkinglot = found_lot,
	};

	return park_call_full(chan, peer, &args);
}

/*!
 * \param rchan is the transferee
 * \param peer is the transferer
 */
static int masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout, int play_announcement, struct ast_park_call_args *args)
{
	struct ast_channel *chan;
	struct ast_frame *f;
	struct ast_park_call_args park_args = {0,};

	if (!args) {
		args = &park_args;
		args->timeout = timeout;
		args->extout = extout;
	}

	if ((args->pu = park_space_reserve(rchan, peer, args)) == NULL) {
		if (peer) {
			ast_stream_and_wait(peer, "pbx-parkingfailed", "");
		}
		return AST_FEATURE_RETURN_PARKFAILED;
	}

	/* Make a new, fake channel that we'll use to masquerade in the real one */
	if (!(chan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, rchan->accountcode, rchan->exten, rchan->context, rchan->linkedid, rchan->amaflags, "Parked/%s",rchan->name))) {
		ast_log(LOG_WARNING, "Unable to create parked channel\n");
		return -1;
	}

	/* Make formats okay */
	chan->readformat = rchan->readformat;
	chan->writeformat = rchan->writeformat;
	ast_channel_masquerade(chan, rchan);

	/* Setup the extensions and such */
	set_c_e_p(chan, rchan->context, rchan->exten, rchan->priority);

	/* Setup the macro extension and such */
	ast_copy_string(chan->macrocontext,rchan->macrocontext,sizeof(chan->macrocontext));
	ast_copy_string(chan->macroexten,rchan->macroexten,sizeof(chan->macroexten));
	chan->macropriority = rchan->macropriority;

	/* Make the masq execute */
	if ((f = ast_read(chan)))
		ast_frfree(f);

	if (peer == rchan) {
		peer = chan;
	}

	if (peer && (!play_announcement && args == &park_args)) {
		args->orig_chan_name = ast_strdupa(peer->name);
	}

	/* parking space reserved, return code check unnecessary */
	park_call_full(chan, peer, args);

	return 0;
}

/* Park call via masqueraded channel */
int ast_masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout)
{
	return masq_park_call(rchan, peer, timeout, extout, 0, NULL);
}

static int masq_park_call_announce(struct ast_channel *rchan, struct ast_channel *peer, struct ast_park_call_args *args)
{
	return masq_park_call(rchan, peer, 0, NULL, 1, args);
}

#ifdef TEST_FRAMEWORK
static int fake_fixup(struct ast_channel *clonechan, struct ast_channel *original)
{
	return 0;
}

static struct ast_channel *create_test_channel(const struct ast_channel_tech *fake_tech)
{
	struct ast_channel *test_channel1;
	struct ast_format tmp_fmt;
	if (!(test_channel1 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
	NULL, NULL, 0, 0, "TestChannel1"))) {
		return NULL;
	}

	/* normally this is done in the channel driver */
	ast_format_cap_add(test_channel1->nativeformats, ast_format_set(&tmp_fmt, AST_FORMAT_GSM, 0));

	ast_format_set(&test_channel1->writeformat, AST_FORMAT_GSM, 0);
	ast_format_set(&test_channel1->rawwriteformat, AST_FORMAT_GSM, 0);
	ast_format_set(&test_channel1->readformat, AST_FORMAT_GSM, 0);
	ast_format_set(&test_channel1->rawreadformat, AST_FORMAT_GSM, 0);

	test_channel1->tech = fake_tech;

	return test_channel1;
}

static int unpark_test_channel(struct ast_channel *toremove, struct ast_park_call_args *args)
{
	struct ast_context *con;
	struct parkeduser *pu_toremove;
	args->pu->notquiteyet = 1; /* go ahead and stop processing the test parking */
	AST_LIST_LOCK(&args->pu->parkinglot->parkings);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&args->pu->parkinglot->parkings, pu_toremove, list) {
		con = ast_context_find(args->pu->parkinglot->parking_con);
		if (con) {
			if (ast_context_remove_extension2(con, args->pu->parkingexten, 1, NULL, 0)) {
				ast_log(LOG_WARNING, "Whoa, failed to remove the parking extension!\n");
				return -1;
			} else {
				notify_metermaids(args->pu->parkingexten, pu_toremove->parkinglot->parking_con, AST_DEVICE_NOT_INUSE);
			}
		} else {
			ast_log(LOG_WARNING, "Whoa, no parking context?\n");
			return -1;
		}
		if (pu_toremove == args->pu) {
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&args->pu->parkinglot->parkings);

	/* the only way this would be unsafe is if a timeout occurred, which is set at 45 sec */
	ast_free(args->pu);
	args->pu = NULL;

	ast_hangup(toremove);
	return 0;
}

AST_TEST_DEFINE(features_test)
{
	int saved_parkeddynamic;
	struct ast_channel *test_channel1 = NULL;
	struct ast_channel *parked_chan = NULL;
	struct ast_parkinglot *dynlot = NULL;
	struct ast_park_call_args args = {
		.timeout = DEFAULT_PARK_TIME,
	};

	int res = -1;

	static const struct ast_channel_tech fake_tech = {
		.fixup = fake_fixup, /* silence warning from masquerade */
	};

	static const char unique_parkinglot[] = "myuniquetestparkinglot3141592654";
	static const char parkinglot_range[] = "750-760";

	switch (cmd) {
	case TEST_INIT:
		info->name = "features_test";
		info->category = "/main/features/";
		info->summary = "Features unit test";
		info->description =
			"Tests whether parking respects PARKINGLOT settings";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* changing a config option is a bad practice, but must be done in this case */
	saved_parkeddynamic = parkeddynamic;
	parkeddynamic = 1;

	if (!(test_channel1 = create_test_channel(&fake_tech))) {
		goto exit_features_test;
	}

	ast_test_status_update(test, "Test parking functionality with defaults\n");
	if (park_call_full(test_channel1, NULL, &args)) {
		goto exit_features_test;
	}
	if (unpark_test_channel(test_channel1, &args)) {
		goto exit_features_test;
	}

	ast_test_status_update(test, "Check that certain parking options are respected\n");
	if (!(test_channel1 = create_test_channel(&fake_tech))) {
		goto exit_features_test;
	}
	pbx_builtin_setvar_helper(test_channel1, "PARKINGLOT", unique_parkinglot);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNPOS", parkinglot_range);
	if (park_call_full(test_channel1, NULL, &args)) {
		goto exit_features_test;
	}
	/* grab newly created parking lot for destruction in the end */
	dynlot = args.pu->parkinglot;
	if (!args.pu->parkingnum == 750 || strcasecmp(args.pu->parkinglot->name, unique_parkinglot)) {
		ast_test_status_update(test, "Parking settings were not respected\n");
		goto exit_features_test;
	} else {
		ast_test_status_update(test, "Parking settings for non-masquerading park verified\n");
	}
	if (unpark_test_channel(test_channel1, &args)) {
		goto exit_features_test;
	}

	ast_test_status_update(test, "Check #2 that certain parking options are respected\n");
	if (!(test_channel1 = create_test_channel(&fake_tech))) {
		goto exit_features_test;
	}
	pbx_builtin_setvar_helper(test_channel1, "PARKINGLOT", unique_parkinglot);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNPOS", parkinglot_range);
	if (masq_park_call(test_channel1, NULL, 0, NULL, 0, &args) == AST_FEATURE_RETURN_PARKFAILED) {
		goto exit_features_test;
	}
	/* hangup zombie channel */
	ast_hangup(test_channel1);
	test_channel1 = NULL;
	if (!args.pu->parkingnum == 750 || strcasecmp(args.pu->parkinglot->name, unique_parkinglot)) {
		ast_test_status_update(test, "Parking settings were not respected\n");
		goto exit_features_test;
	} else {
		ast_test_status_update(test, "Parking settings for masquerading park verified\n");
	}
	/* find the real channel */
	parked_chan = ast_channel_get_by_name("TestChannel1");
	if (unpark_test_channel(parked_chan, &args)) {
		goto exit_features_test;
	}

	res = 0;

exit_features_test:

	if (test_channel1) {
		ast_hangup(test_channel1);
	}

	/* careful, if PARKINGDYNCONTEXT is tested, need to delete context */
	ao2_unlink(parkinglots, dynlot);
	parkeddynamic = saved_parkeddynamic;
	return res ? AST_TEST_FAIL : AST_TEST_PASS;
}
#endif


/*!
 * \brief set caller and callee according to the direction
 * \param caller, callee, peer, chan, sense
 *
 * Detect who triggered feature and set callee/caller variables accordingly
*/
static void set_peers(struct ast_channel **caller, struct ast_channel **callee,
	struct ast_channel *peer, struct ast_channel *chan, int sense)
{
	if (sense == FEATURE_SENSE_PEER) {
		*caller = peer;
		*callee = chan;
	} else {
		*callee = peer;
		*caller = chan;
	}
}

static int parkcall_helper(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, struct ast_park_call_args *args)
{
	int res = 0;

	if (args) {
		ast_debug(1, "Parkinglot specified for builtin_parkcall: %s\n", args->parkinglot->name);
	}

	/* we used to set chan's exten and priority to "s" and 1
	   here, but this generates (in some cases) an invalid
	   extension, and if "s" exists, could errantly
	   cause execution of extensions you don't expect. It
	   makes more sense to let nature take its course
	   when chan finishes, and let the pbx do its thing
	   and hang up when the park is over.
	*/
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res)
		res = ast_safe_sleep(chan, 1000);

	if (!res) { /* one direction used to call park_call.... */
		struct ast_channel *parker;
		struct ast_channel *parkee;
		set_peers(&parker, &parkee, peer, chan, sense);
		res = masq_park_call_announce(parkee, parker, args);
		/* PBX should hangup zombie channel if a masquerade actually occurred (res=0) */
	}

	return res;
}

/*! 
 * \brief support routing for one touch call parking
 * \param chan channel parking call
 * \param peer channel to be parked
 * \param config unsed
 * \param code unused
 * \param sense feature options
 *
 * \param data
 * Setup channel, set return exten,priority to 's,1'
 * answer chan, sleep chan, park call
*/
static int builtin_parkcall(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	return parkcall_helper(chan, peer, config, code, sense, NULL);
}

/*! \brief Play message to both caller and callee in bridged call, plays synchronously, autoservicing the
	other channel during the message, so please don't use this for very long messages
 */
static int play_message_in_bridged_call(struct ast_channel *caller_chan, struct ast_channel *callee_chan, const char *audiofile)
{
	/* First play for caller, put other channel on auto service */
	if (ast_autoservice_start(callee_chan))
		return -1;
	ast_autoservice_ignore(callee_chan, AST_FRAME_DTMF_END);
	if (ast_stream_and_wait(caller_chan, audiofile, "")) {
		ast_log(LOG_WARNING, "Failed to play automon message!\n");
		ast_autoservice_stop(callee_chan);
		return -1;
	}
	if (ast_autoservice_stop(callee_chan))
		return -1;
	/* Then play for callee, put other channel on auto service */
	if (ast_autoservice_start(caller_chan))
		return -1;
	ast_autoservice_ignore(caller_chan, AST_FRAME_DTMF_END);
	if (ast_stream_and_wait(callee_chan, audiofile, "")) {
		ast_log(LOG_WARNING, "Failed to play automon message !\n");
		ast_autoservice_stop(caller_chan);
		return -1;
	}
	if (ast_autoservice_stop(caller_chan))
		return -1;
	return(0);
}

/*!
 * \brief Monitor a channel by DTMF
 * \param chan channel requesting monitor
 * \param peer channel to be monitored
 * \param config
 * \param code
 * \param sense feature options
 *
 * \param data
 * Check monitor app enabled, setup channels, both caller/callee chans not null
 * get TOUCH_MONITOR variable for filename if exists, exec monitor app.
 * \retval AST_FEATURE_RETURN_SUCCESS on success.
 * \retval -1 on error.
*/
static int builtin_automonitor(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	char *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL, *touch_filename = NULL;
	int x = 0;
	size_t len;
	struct ast_channel *caller_chan, *callee_chan;
	const char *automon_message_start = NULL;
	const char *automon_message_stop = NULL;

	if (!monitor_ok) {
		ast_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
		return -1;
	}

	if (!monitor_app && !(monitor_app = pbx_findapp("Monitor"))) {
		monitor_ok = 0;
		ast_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
		return -1;
	}

	set_peers(&caller_chan, &callee_chan, peer, chan, sense);
	if (caller_chan) {	/* Find extra messages */
		automon_message_start = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_MESSAGE_START");
		automon_message_stop = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_MESSAGE_STOP");
	}

	if (!ast_strlen_zero(courtesytone)) {	/* Play courtesy tone if configured */
		if(play_message_in_bridged_call(caller_chan, callee_chan, courtesytone) == -1) {
			return -1;
		}
	}
	
	if (callee_chan->monitor) {
		ast_verb(4, "User hit '%s' to stop recording call.\n", code);
		if (!ast_strlen_zero(automon_message_stop)) {
			play_message_in_bridged_call(caller_chan, callee_chan, automon_message_stop);
		}
		callee_chan->monitor->stop(callee_chan, 1);
		return AST_FEATURE_RETURN_SUCCESS;
	}

	if (caller_chan && callee_chan) {
		const char *touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_FORMAT");
		const char *touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR");
		const char *touch_monitor_prefix = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_PREFIX");

		if (!touch_format)
			touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_FORMAT");

		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR");
	
		if (!touch_monitor_prefix)
			touch_monitor_prefix = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_PREFIX");
	
		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "%s-%ld-%s", S_OR(touch_monitor_prefix, "auto"), (long)time(NULL), touch_monitor);
			snprintf(args, len, "%s,%s,m", S_OR(touch_format, "wav"), touch_filename);
		} else {
			caller_chan_id = ast_strdupa(S_COR(caller_chan->caller.id.number.valid,
				caller_chan->caller.id.number.str, caller_chan->name));
			callee_chan_id = ast_strdupa(S_COR(callee_chan->caller.id.number.valid,
				callee_chan->caller.id.number.str, callee_chan->name));
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "%s-%ld-%s-%s", S_OR(touch_monitor_prefix, "auto"), (long)time(NULL), caller_chan_id, callee_chan_id);
			snprintf(args, len, "%s,%s,m", S_OR(touch_format, "wav"), touch_filename);
		}

		for(x = 0; x < strlen(args); x++) {
			if (args[x] == '/')
				args[x] = '-';
		}
		
		ast_verb(4, "User hit '%s' to record call. filename: %s\n", code, args);

		pbx_exec(callee_chan, monitor_app, args);
		pbx_builtin_setvar_helper(callee_chan, "TOUCH_MONITOR_OUTPUT", touch_filename);
		pbx_builtin_setvar_helper(caller_chan, "TOUCH_MONITOR_OUTPUT", touch_filename);

		if (!ast_strlen_zero(automon_message_start)) {	/* Play start message for both channels */
			play_message_in_bridged_call(caller_chan, callee_chan, automon_message_start);
		}
	
		return AST_FEATURE_RETURN_SUCCESS;
	}
	
	ast_log(LOG_NOTICE,"Cannot record the call. One or both channels have gone away.\n");	
	return -1;
}

static int builtin_automixmonitor(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	char *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL, *touch_filename = NULL;
	int x = 0;
	size_t len;
	struct ast_channel *caller_chan, *callee_chan;
	const char *mixmonitor_spy_type = "MixMonitor";
	int count = 0;

	if (!mixmonitor_ok) {
		ast_log(LOG_ERROR,"Cannot record the call. The mixmonitor application is disabled.\n");
		return -1;
	}

	if (!(mixmonitor_app = pbx_findapp("MixMonitor"))) {
		mixmonitor_ok = 0;
		ast_log(LOG_ERROR,"Cannot record the call. The mixmonitor application is disabled.\n");
		return -1;
	}

	set_peers(&caller_chan, &callee_chan, peer, chan, sense);

	if (!ast_strlen_zero(courtesytone)) {
		if (ast_autoservice_start(callee_chan))
			return -1;
		ast_autoservice_ignore(callee_chan, AST_FRAME_DTMF_END);
		if (ast_stream_and_wait(caller_chan, courtesytone, "")) {
			ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
			ast_autoservice_stop(callee_chan);
			return -1;
		}
		if (ast_autoservice_stop(callee_chan))
			return -1;
	}

	ast_channel_lock(callee_chan);
	count = ast_channel_audiohook_count_by_source(callee_chan, mixmonitor_spy_type, AST_AUDIOHOOK_TYPE_SPY);
	ast_channel_unlock(callee_chan);

	/* This means a mixmonitor is attached to the channel, running or not is unknown. */
	if (count > 0) {
		
		ast_verb(3, "User hit '%s' to stop recording call.\n", code);

		/* Make sure they are running */
		ast_channel_lock(callee_chan);
		count = ast_channel_audiohook_count_by_source_running(callee_chan, mixmonitor_spy_type, AST_AUDIOHOOK_TYPE_SPY);
		ast_channel_unlock(callee_chan);
		if (count > 0) {
			if (!stopmixmonitor_ok) {
				ast_log(LOG_ERROR,"Cannot stop recording the call. The stopmixmonitor application is disabled.\n");
				return -1;
			}
			if (!(stopmixmonitor_app = pbx_findapp("StopMixMonitor"))) {
				stopmixmonitor_ok = 0;
				ast_log(LOG_ERROR,"Cannot stop recording the call. The stopmixmonitor application is disabled.\n");
				return -1;
			} else {
				pbx_exec(callee_chan, stopmixmonitor_app, "");
				return AST_FEATURE_RETURN_SUCCESS;
			}
		}
		
		ast_log(LOG_WARNING,"Stopped MixMonitors are attached to the channel.\n");	
	}			

	if (caller_chan && callee_chan) {
		const char *touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MIXMONITOR_FORMAT");
		const char *touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MIXMONITOR");

		if (!touch_format)
			touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MIXMONITOR_FORMAT");

		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MIXMONITOR");

		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "auto-%ld-%s", (long)time(NULL), touch_monitor);
			snprintf(args, len, "%s.%s,b", touch_filename, (touch_format) ? touch_format : "wav");
		} else {
			caller_chan_id = ast_strdupa(S_COR(caller_chan->caller.id.number.valid,
				caller_chan->caller.id.number.str, caller_chan->name));
			callee_chan_id = ast_strdupa(S_COR(callee_chan->caller.id.number.valid,
				callee_chan->caller.id.number.str, callee_chan->name));
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "auto-%ld-%s-%s", (long)time(NULL), caller_chan_id, callee_chan_id);
			snprintf(args, len, "%s.%s,b", touch_filename, S_OR(touch_format, "wav"));
		}

		for( x = 0; x < strlen(args); x++) {
			if (args[x] == '/')
				args[x] = '-';
		}

		ast_verb(3, "User hit '%s' to record call. filename: %s\n", code, touch_filename);

		pbx_exec(callee_chan, mixmonitor_app, args);
		pbx_builtin_setvar_helper(callee_chan, "TOUCH_MIXMONITOR_OUTPUT", touch_filename);
		pbx_builtin_setvar_helper(caller_chan, "TOUCH_MIXMONITOR_OUTPUT", touch_filename);
		return AST_FEATURE_RETURN_SUCCESS;
	
	}

	ast_log(LOG_NOTICE,"Cannot record the call. One or both channels have gone away.\n");
	return -1;

}

static int builtin_disconnect(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	ast_verb(4, "User hit '%s' to disconnect call.\n", code);
	return AST_FEATURE_RETURN_HANGUP;
}

static int finishup(struct ast_channel *chan)
{
	ast_indicate(chan, AST_CONTROL_UNHOLD);

	return ast_autoservice_stop(chan);
}

/*!
 * \brief Find the context for the transfer
 * \param transferer
 * \param transferee
 * 
 * Grab the TRANSFER_CONTEXT, if fails try grabbing macrocontext.
 * \return a context string
*/
static const char *real_ctx(struct ast_channel *transferer, struct ast_channel *transferee)
{
	const char *s = pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT");
	if (ast_strlen_zero(s)) {
		s = pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT");
	}
	if (ast_strlen_zero(s)) { /* Use the non-macro context to transfer the call XXX ? */
		s = transferer->macrocontext;
	}
	if (ast_strlen_zero(s)) {
		s = transferer->context;
	}
	return s;  
}

/*!
 * \brief Blind transfer user to another extension
 * \param chan channel to be transfered
 * \param peer channel initiated blind transfer
 * \param config
 * \param code
 * \param data
 * \param sense  feature options
 * 
 * Place chan on hold, check if transferred to parkinglot extension,
 * otherwise check extension exists and transfer caller.
 * \retval AST_FEATURE_RETURN_SUCCESS.
 * \retval -1 on failure.
*/
static int builtin_blindtransfer(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	struct ast_channel *transferer;
	struct ast_channel *transferee;
	const char *transferer_real_context;
	struct ast_parkinglot *found_lot = NULL;
	char xferto[256];
	int res, parkstatus = 0;

	set_peers(&transferer, &transferee, peer, chan, sense);
	transferer_real_context = real_ctx(transferer, transferee);
	/* Start autoservice on chan while we talk to the originator */
	ast_autoservice_start(transferee);
	ast_autoservice_ignore(transferee, AST_FRAME_DTMF_END);
	ast_indicate(transferee, AST_CONTROL_HOLD);

	memset(xferto, 0, sizeof(xferto));

	/* Transfer */
	res = ast_stream_and_wait(transferer, "pbx-transfer", AST_DIGIT_ANY);
	if (res < 0) {
		finishup(transferee);
		return -1; /* error ? */
	}
	if (res > 0)	/* If they've typed a digit already, handle it */
		xferto[0] = (char) res;

	ast_stopstream(transferer);
	res = ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout);
	if (res < 0) {  /* hangup or error, (would be 0 for invalid and 1 for valid) */
		finishup(transferee);
		return -1;
	}
	if (res == 0) {
		if (xferto[0]) {
			ast_log(LOG_WARNING, "Extension '%s' does not exist in context '%s'\n",
				xferto, transferer_real_context);
		} else {
			/* Does anyone care about this case? */
			ast_log(LOG_WARNING, "No digits dialed.\n");
		}
		ast_stream_and_wait(transferer, "pbx-invalid", "");
		finishup(transferee);
		return AST_FEATURE_RETURN_SUCCESS;
	}

	found_lot = ao2_callback(parkinglots, 0, find_parkinglot_by_exten_cb, &xferto);
	if (found_lot) {
		struct ast_park_call_args args = {
			.parkinglot = found_lot,
		};
		res = finishup(transferee);
		if (res) {
		} else if (!(parkstatus = masq_park_call_announce(transferee, transferer, &args))) {	/* success */
			/* We return non-zero, but tell the PBX not to hang the channel when
			   the thread dies -- We have to be careful now though.  We are responsible for 
			   hanging up the channel, else it will never be hung up! */

			return 0;
		} else {
			ast_log(LOG_WARNING, "Unable to park call %s, parkstatus = %d\n", transferee->name, parkstatus);
		}
		ast_autoservice_start(transferee);
	} else {
		ast_cel_report_event(transferer, AST_CEL_BLINDTRANSFER, NULL, xferto, transferee);
		pbx_builtin_setvar_helper(transferer, "BLINDTRANSFER", transferee->name);
		pbx_builtin_setvar_helper(transferee, "BLINDTRANSFER", transferer->name);
		res=finishup(transferee);
		if (!transferer->cdr) { /* this code should never get called (in a perfect world) */
			transferer->cdr=ast_cdr_alloc();
			if (transferer->cdr) {
				ast_cdr_init(transferer->cdr, transferer); /* initialize our channel's cdr */
				ast_cdr_start(transferer->cdr);
			}
		}
		if (transferer->cdr) {
			struct ast_cdr *swap = transferer->cdr;
			ast_debug(1, "transferer=%s; transferee=%s; lastapp=%s; lastdata=%s; chan=%s; dstchan=%s\n",
					transferer->name, transferee->name, transferer->cdr->lastapp, transferer->cdr->lastdata, 
					transferer->cdr->channel, transferer->cdr->dstchannel);
			ast_debug(1, "TRANSFEREE; lastapp=%s; lastdata=%s, chan=%s; dstchan=%s\n",
					transferee->cdr->lastapp, transferee->cdr->lastdata, transferee->cdr->channel, transferee->cdr->dstchannel);
			ast_debug(1, "transferer_real_context=%s; xferto=%s\n", transferer_real_context, xferto);
			/* swap cdrs-- it will save us some time & work */
			transferer->cdr = transferee->cdr;
			transferee->cdr = swap;
		}
		if (!transferee->pbx) {
			/* Doh!  Use our handy async_goto functions */
			ast_verb(3, "Transferring %s to '%s' (context %s) priority 1\n"
								,transferee->name, xferto, transferer_real_context);
			if (ast_async_goto(transferee, transferer_real_context, xferto, 1))
				ast_log(LOG_WARNING, "Async goto failed :-(\n");
		} else {
			/* Set the channel's new extension, since it exists, using transferer context */
			ast_set_flag(transferee, AST_FLAG_BRIDGE_HANGUP_DONT); /* don't let the after-bridge code run the h-exten */
			ast_debug(1, "ABOUT TO AST_ASYNC_GOTO, have a pbx... set HANGUP_DONT on chan=%s\n", transferee->name);
			if (ast_channel_connected_line_macro(transferee, transferer, &transferer->connected, 1, 0)) {
				ast_channel_update_connected_line(transferer, &transferer->connected, NULL);
			}
			set_c_e_p(transferee, transferer_real_context, xferto, 0);
		}
		check_goto_on_transfer(transferer);
		return res;
	}
	if (parkstatus != AST_FEATURE_RETURN_PARKFAILED
		&& ast_stream_and_wait(transferer, xferfailsound, "")) {
		finishup(transferee);
		return -1;
	}
	ast_stopstream(transferer);
	res = finishup(transferee);
	if (res) {
		ast_verb(2, "Hungup during autoservice stop on '%s'\n", transferee->name);
		return res;
	}
	return AST_FEATURE_RETURN_SUCCESS;
}

/*!
 * \brief make channels compatible
 * \param c
 * \param newchan
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int check_compat(struct ast_channel *c, struct ast_channel *newchan)
{
	if (ast_channel_make_compatible(c, newchan) < 0) {
		ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n",
			c->name, newchan->name);
		ast_hangup(newchan);
		return -1;
	}
	return 0;
}

/*!
 * \internal
 * \brief Builtin attended transfer failed cleanup.
 * \since 1.10
 *
 * \param transferee Party A in the transfer.
 * \param transferer Party B in the transfer.
 * \param connected_line Saved connected line info about party A.
 *
 * \note The connected_line data is freed.
 *
 * \return Nothing
 */
static void atxfer_fail_cleanup(struct ast_channel *transferee, struct ast_channel *transferer, struct ast_party_connected_line *connected_line)
{
	finishup(transferee);

	/*
	 * Restore party B connected line info about party A.
	 *
	 * Party B was the caller to party C and is the last known mode
	 * for party B.
	 */
	if (ast_channel_connected_line_macro(transferee, transferer, connected_line, 1, 0)) {
		ast_channel_update_connected_line(transferer, connected_line, NULL);
	}
	ast_party_connected_line_free(connected_line);
}

/*!
 * \brief Attended transfer
 * \param chan transfered user
 * \param peer person transfering call
 * \param config
 * \param code
 * \param sense feature options
 *
 * \param data
 * Get extension to transfer to, if you cannot generate channel (or find extension)
 * return to host channel. After called channel answered wait for hangup of transferer,
 * bridge call between transfer peer (taking them off hold) to attended transfer channel.
 *
 * \return -1 on failure
*/
static int builtin_atxfer(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	struct ast_channel *transferer;/* Party B */
	struct ast_channel *transferee;/* Party A */
	const char *transferer_real_context;
	char xferto[256] = "";
	int res;
	int outstate=0;
	struct ast_channel *newchan;
	struct ast_channel *xferchan;
	struct ast_bridge_thread_obj *tobj;
	struct ast_bridge_config bconfig;
	int l;
	struct ast_party_connected_line connected_line;
	struct ast_datastore *features_datastore;
	struct ast_dial_features *dialfeatures = NULL;
	struct ast_parkinglot *parkinglot;
	char *transferer_tech;
	char *transferer_name;
	char *transferer_name_orig;
	char *dash;

	ast_debug(1, "Executing Attended Transfer %s, %s (sense=%d) \n", chan->name, peer->name, sense);
	set_peers(&transferer, &transferee, peer, chan, sense);
	transferer_real_context = real_ctx(transferer, transferee);

	/* Start autoservice on transferee while we talk to the transferer */
	ast_autoservice_start(transferee);
	ast_indicate(transferee, AST_CONTROL_HOLD);

	/* Transfer */
	res = ast_stream_and_wait(transferer, "pbx-transfer", AST_DIGIT_ANY);
	if (res < 0) {
		finishup(transferee);
		return -1;
	}
	if (res > 0) /* If they've typed a digit already, handle it */
		xferto[0] = (char) res;

	/* this is specific of atxfer */
	res = ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout);
	if (res < 0) {  /* hangup or error, (would be 0 for invalid and 1 for valid) */
		finishup(transferee);
		return -1;
	}
	l = strlen(xferto);
	if (res == 0) {
		if (l) {
			ast_log(LOG_WARNING, "Extension '%s' does not exist in context '%s'\n",
				xferto, transferer_real_context);
		} else {
			/* Does anyone care about this case? */
			ast_log(LOG_WARNING, "No digits dialed for atxfer.\n");
		}
		ast_stream_and_wait(transferer, "pbx-invalid", "");
		finishup(transferee);
		return AST_FEATURE_RETURN_SUCCESS;
	}

	/* If we are attended transfering to parking, just use parkcall_helper instead of trying to track all of
	 * the different variables for handling this properly with a builtin_atxfer */
	parkinglot = ao2_callback(parkinglots, 0, find_parkinglot_by_exten_cb, &xferto);
	if (parkinglot) {
		struct ast_park_call_args args = {
			.parkinglot = parkinglot,
		};
		finishup(transferee);
		return parkcall_helper(chan, peer, config, code, sense, &args);
	}

	/* Append context to dialed transfer number. */
	snprintf(xferto + l, sizeof(xferto) - l, "@%s/n", transferer_real_context);

	/* If we are performing an attended transfer and we have two channels involved then
	   copy sound file information to play upon attended transfer completion */
	if (transferee) {
		const char *chan1_attended_sound = pbx_builtin_getvar_helper(transferer, "ATTENDED_TRANSFER_COMPLETE_SOUND");
		const char *chan2_attended_sound = pbx_builtin_getvar_helper(transferee, "ATTENDED_TRANSFER_COMPLETE_SOUND");

		if (!ast_strlen_zero(chan1_attended_sound)) {
			pbx_builtin_setvar_helper(transferer, "BRIDGE_PLAY_SOUND", chan1_attended_sound);
		}
		if (!ast_strlen_zero(chan2_attended_sound)) {
			pbx_builtin_setvar_helper(transferee, "BRIDGE_PLAY_SOUND", chan2_attended_sound);
		}
	}

	/* Extract redial transferer information from the channel name. */
	transferer_name_orig = ast_strdupa(transferer->name);
	transferer_name = ast_strdupa(transferer_name_orig);
	transferer_tech = strsep(&transferer_name, "/");
	dash = strrchr(transferer_name, '-');
	if (dash) {
		/* Trim off channel name sequence/serial number. */
		*dash = '\0';
	}

	/* Stop autoservice so we can monitor all parties involved in the transfer. */
	if (ast_autoservice_stop(transferee) < 0) {
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return -1;
	}

	/* Save connected line info for party B about party A in case transfer fails. */
	ast_party_connected_line_init(&connected_line);
	ast_channel_lock(transferer);
	ast_party_connected_line_copy(&connected_line, &transferer->connected);
	ast_channel_unlock(transferer);
	connected_line.source = AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER;

	/* Dial party C */
	newchan = feature_request_and_dial(transferer, transferer_name_orig, transferer,
		transferee, "Local", transferer->nativeformats, xferto,
		atxfernoanswertimeout, &outstate, transferer->language);
	ast_debug(2, "Dial party C result: newchan:%d, outstate:%d\n", !!newchan, outstate);

	if (!ast_check_hangup(transferer)) {
		int hangup_dont = 0;

		/* Transferer (party B) is up */
		ast_debug(1, "Actually doing an attended transfer.\n");

		/* Start autoservice on transferee while the transferer deals with party C. */
		ast_autoservice_start(transferee);

		ast_indicate(transferer, -1);
		if (!newchan) {
			/* any reason besides user requested cancel and busy triggers the failed sound */
			switch (outstate) {
			case AST_CONTROL_UNHOLD:/* Caller requested cancel or party C answer timeout. */
			case AST_CONTROL_BUSY:
			case AST_CONTROL_CONGESTION:
				if (ast_stream_and_wait(transferer, xfersound, "")) {
					ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
				}
				break;
			default:
				if (ast_stream_and_wait(transferer, xferfailsound, "")) {
					ast_log(LOG_WARNING, "Failed to play transfer failed sound!\n");
				}
				break;
			}
			atxfer_fail_cleanup(transferee, transferer, &connected_line);
			return AST_FEATURE_RETURN_SUCCESS;
		}

		if (check_compat(transferer, newchan)) {
			if (ast_stream_and_wait(transferer, xferfailsound, "")) {
				ast_log(LOG_WARNING, "Failed to play transfer failed sound!\n");
			}
			atxfer_fail_cleanup(transferee, transferer, &connected_line);
			return AST_FEATURE_RETURN_SUCCESS;
		}
		memset(&bconfig,0,sizeof(struct ast_bridge_config));
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_DISCONNECT);
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_DISCONNECT);

		/* ast_bridge_call clears AST_FLAG_BRIDGE_HANGUP_DONT, but we don't
		   want that to happen here because we're also in another bridge already
		 */
		if (ast_test_flag(chan, AST_FLAG_BRIDGE_HANGUP_DONT)) {
			hangup_dont = 1;
		}
		/* Let party B and party C talk as long as they want. */
		ast_bridge_call(transferer, newchan, &bconfig);
		if (hangup_dont) {
			ast_set_flag(chan, AST_FLAG_BRIDGE_HANGUP_DONT);
		}

		if (ast_check_hangup(newchan) || !ast_check_hangup(transferer)) {
			ast_hangup(newchan);
			if (ast_stream_and_wait(transferer, xfersound, "")) {
				ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
			}
			atxfer_fail_cleanup(transferee, transferer, &connected_line);
			return AST_FEATURE_RETURN_SUCCESS;
		}

		/* Transferer (party B) is confirmed hung up at this point. */
		if (check_compat(transferee, newchan)) {
			finishup(transferee);
			ast_party_connected_line_free(&connected_line);
			return -1;
		}

		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		if ((ast_autoservice_stop(transferee) < 0)
			|| (ast_waitfordigit(transferee, 100) < 0)
			|| (ast_waitfordigit(newchan, 100) < 0)
			|| ast_check_hangup(transferee)
			|| ast_check_hangup(newchan)) {
			ast_hangup(newchan);
			ast_party_connected_line_free(&connected_line);
			return -1;
		}
	} else if (!ast_check_hangup(transferee)) {
		/* Transferer (party B) has hung up at this point.  Doing blonde transfer. */
		ast_debug(1, "Actually doing a blonde transfer.\n");

		if (!newchan && !atxferdropcall) {
			/* Party C is not available, try to call party B back. */
			unsigned int tries = 0;

			if (ast_strlen_zero(transferer_name) || ast_strlen_zero(transferer_tech)) {
				ast_log(LOG_WARNING,
					"Transferer channel name: '%s' cannot be used for callback.\n",
					transferer_name_orig);
				ast_indicate(transferee, AST_CONTROL_UNHOLD);
				ast_party_connected_line_free(&connected_line);
				return -1;
			}

			tries = 0;
			for (;;) {
				/* Try to get party B back. */
				ast_debug(1, "We're trying to callback %s/%s\n",
					transferer_tech, transferer_name);
				newchan = feature_request_and_dial(transferer, transferer_name_orig,
					transferee, transferee, transferer_tech,
					transferee->nativeformats, transferer_name,
					atxfernoanswertimeout, &outstate, transferer->language);
				ast_debug(2, "Dial party B result: newchan:%d, outstate:%d\n",
					!!newchan, outstate);
				if (newchan || ast_check_hangup(transferee)) {
					break;
				}
				++tries;
				if (atxfercallbackretries <= tries) {
					/* No more callback tries remaining. */
					break;
				}

				if (atxferloopdelay) {
					/* Transfer failed, sleeping */
					ast_debug(1, "Sleeping for %d ms before retrying atxfer.\n",
						atxferloopdelay);
					ast_safe_sleep(transferee, atxferloopdelay);
					if (ast_check_hangup(transferee)) {
						ast_party_connected_line_free(&connected_line);
						return -1;
					}
				}

				/* Retry dialing party C. */
				ast_debug(1, "We're retrying to call %s/%s\n", "Local", xferto);
				newchan = feature_request_and_dial(transferer, transferer_name_orig,
					transferer, transferee, "Local",
					transferee->nativeformats, xferto,
					atxfernoanswertimeout, &outstate, transferer->language);
				ast_debug(2, "Redial party C result: newchan:%d, outstate:%d\n",
					!!newchan, outstate);
				if (newchan || ast_check_hangup(transferee)) {
					break;
				}
			}
		}
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		if (!newchan) {
			/* No party C or could not callback party B. */
			ast_party_connected_line_free(&connected_line);
			return -1;
		}

		/* newchan is up, we should prepare transferee and bridge them */
		if (ast_check_hangup(newchan)) {
			ast_hangup(newchan);
			ast_party_connected_line_free(&connected_line);
			return -1;
		}
		if (check_compat(transferee, newchan)) {
			ast_party_connected_line_free(&connected_line);
			return -1;
		}
	} else {
		/*
		 * Both the transferer and transferee have hungup.  If newchan
		 * is up, hang it up as it has no one to talk to.
		 */
		ast_debug(1, "Everyone is hungup.\n");
		if (newchan) {
			ast_hangup(newchan);
		}
		ast_party_connected_line_free(&connected_line);
		return -1;
	}

	/* Initiate the channel transfer of party A to party C (or recalled party B). */
	ast_cel_report_event(transferee, AST_CEL_ATTENDEDTRANSFER, NULL, NULL, newchan);

	xferchan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", transferee->linkedid, 0, "Transfered/%s", transferee->name);
	if (!xferchan) {
		ast_hangup(newchan);
		ast_party_connected_line_free(&connected_line);
		return -1;
	}

	/* Give party A a momentary ringback tone during transfer. */
	xferchan->visible_indication = AST_CONTROL_RINGING;

	/* Make formats okay */
	xferchan->readformat = transferee->readformat;
	xferchan->writeformat = transferee->writeformat;

	ast_channel_masquerade(xferchan, transferee);
	ast_explicit_goto(xferchan, transferee->context, transferee->exten, transferee->priority);
	xferchan->_state = AST_STATE_UP;
	ast_clear_flag(xferchan, AST_FLAGS_ALL);

	/* Do the masquerade manually to make sure that is is completed. */
	ast_do_masquerade(xferchan);

	newchan->_state = AST_STATE_UP;
	ast_clear_flag(newchan, AST_FLAGS_ALL);
	tobj = ast_calloc(1, sizeof(*tobj));
	if (!tobj) {
		ast_hangup(xferchan);
		ast_hangup(newchan);
		ast_party_connected_line_free(&connected_line);
		return -1;
	}

	ast_channel_lock(newchan);
	if ((features_datastore = ast_channel_datastore_find(newchan, &dial_features_info, NULL))) {
		dialfeatures = features_datastore->data;
	}
	ast_channel_unlock(newchan);

	if (dialfeatures) {
		/* newchan should always be the callee and shows up as callee in dialfeatures, but for some reason
		   I don't currently understand, the abilities of newchan seem to be stored on the caller side */
		ast_copy_flags(&(config->features_callee), &(dialfeatures->features_caller), AST_FLAGS_ALL);
		dialfeatures = NULL;
	}

	ast_channel_lock(xferchan);
	if ((features_datastore = ast_channel_datastore_find(xferchan, &dial_features_info, NULL))) {
		dialfeatures = features_datastore->data;
	}
	ast_channel_unlock(xferchan);

	if (dialfeatures) {
		ast_copy_flags(&(config->features_caller), &(dialfeatures->features_caller), AST_FLAGS_ALL);
	}

	tobj->chan = newchan;
	tobj->peer = xferchan;
	tobj->bconfig = *config;

	if (tobj->bconfig.end_bridge_callback_data_fixup) {
		tobj->bconfig.end_bridge_callback_data_fixup(&tobj->bconfig, tobj->peer, tobj->chan);
	}

	/*
	 * xferchan is transferee, and newchan is the transfer target
	 * So...in a transfer, who is the caller and who is the callee?
	 *
	 * When the call is originally made, it is clear who is caller and callee.
	 * When a transfer occurs, it is my humble opinion that the transferee becomes
	 * the caller, and the transfer target is the callee.
	 *
	 * The problem is that these macros were set with the intention of the original
	 * caller and callee taking those roles.  A transfer can totally mess things up,
	 * to be technical.  What sucks even more is that you can't effectively change
	 * the macros in the dialplan during the call from the transferer to the transfer
	 * target because the transferee is stuck with whatever role he originally had.
	 *
	 * I think the answer here is just to make sure that it is well documented that
	 * during a transfer, the transferee is the "caller" and the transfer target
	 * is the "callee."
	 *
	 * This means that if party B calls party A, and party B transfers party A to
	 * party C, then A has switched roles for the call.  Now party A will have the
	 * caller macro called on his channel instead of the callee macro.
	 *
	 * Luckily, the method by which the party B to party C bridge is
	 * launched above ensures that the transferee is the "chan" on
	 * the bridge and the transfer target is the "peer," so my idea
	 * for the roles post-transfer does not require extensive code
	 * changes.
	 */

	/* Transfer party C connected line to party A */
	ast_channel_lock(transferer);
	/*
	 * Due to a limitation regarding when callerID is set on a Local channel,
	 * we use the transferer's connected line information here.
	 */
	ast_party_connected_line_copy(&connected_line, &transferer->connected);
	ast_channel_unlock(transferer);
	connected_line.source = AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER;
	if (ast_channel_connected_line_macro(newchan, xferchan, &connected_line, 1, 0)) {
		ast_channel_update_connected_line(xferchan, &connected_line, NULL);
	}

	/* Transfer party A connected line to party C */
	ast_channel_lock(xferchan);
	ast_connected_line_copy_from_caller(&connected_line, &xferchan->caller);
	ast_channel_unlock(xferchan);
	connected_line.source = AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER;
	if (ast_channel_connected_line_macro(xferchan, newchan, &connected_line, 0, 0)) {
		ast_channel_update_connected_line(newchan, &connected_line, NULL);
	}

	if (ast_stream_and_wait(newchan, xfersound, ""))
		ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
	bridge_call_thread_launch(tobj);

	ast_party_connected_line_free(&connected_line);
	return -1;/* The transferee is masqueraded and the original bridged channels can be hungup. */
}

/* add atxfer and automon as undefined so you can only use em if you configure them */
#define FEATURES_COUNT ARRAY_LEN(builtin_features)

AST_RWLOCK_DEFINE_STATIC(features_lock);

static struct ast_call_feature builtin_features[] = {
	{ AST_FEATURE_REDIRECT, "Blind Transfer", "blindxfer", "#", "#", builtin_blindtransfer, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_REDIRECT, "Attended Transfer", "atxfer", "", "", builtin_atxfer, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_AUTOMON, "One Touch Monitor", "automon", "", "", builtin_automonitor, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_DISCONNECT, "Disconnect Call", "disconnect", "*", "*", builtin_disconnect, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_PARKCALL, "Park Call", "parkcall", "", "", builtin_parkcall, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_AUTOMIXMON, "One Touch MixMonitor", "automixmon", "", "", builtin_automixmonitor, AST_FEATURE_FLAG_NEEDSDTMF, "" },
};


static AST_RWLIST_HEAD_STATIC(feature_list, ast_call_feature);

/*! \brief register new feature into feature_list*/
void ast_register_feature(struct ast_call_feature *feature)
{
	if (!feature) {
		ast_log(LOG_NOTICE,"You didn't pass a feature!\n");
		return;
	}
  
	AST_RWLIST_WRLOCK(&feature_list);
	AST_RWLIST_INSERT_HEAD(&feature_list,feature,feature_entry);
	AST_RWLIST_UNLOCK(&feature_list);

	ast_verb(2, "Registered Feature '%s'\n",feature->sname);
}

/*! 
 * \brief Add new feature group
 * \param fgname feature group name.
 *
 * Add new feature group to the feature group list insert at head of list.
 * \note This function MUST be called while feature_groups is locked.
*/
static struct feature_group *register_group(const char *fgname)
{
	struct feature_group *fg;

	if (!fgname) {
		ast_log(LOG_NOTICE, "You didn't pass a new group name!\n");
		return NULL;
	}

	if (!(fg = ast_calloc_with_stringfields(1, struct feature_group, 128))) {
		return NULL;
	}

	ast_string_field_set(fg, gname, fgname);

	AST_LIST_INSERT_HEAD(&feature_groups, fg, entry);

	ast_verb(2, "Registered group '%s'\n", fg->gname);

	return fg;
}

/*! 
 * \brief Add feature to group
 * \param fg feature group
 * \param exten
 * \param feature feature to add.
 *
 * Check fg and feature specified, add feature to list
 * \note This function MUST be called while feature_groups is locked. 
*/
static void register_group_feature(struct feature_group *fg, const char *exten, struct ast_call_feature *feature)
{
	struct feature_group_exten *fge;

	if (!fg) {
		ast_log(LOG_NOTICE, "You didn't pass a group!\n");
		return;
	}

	if (!feature) {
		ast_log(LOG_NOTICE, "You didn't pass a feature!\n");
		return;
	}

	if (!(fge = ast_calloc_with_stringfields(1, struct feature_group_exten, 128))) {
		return;
	}

	ast_string_field_set(fge, exten, S_OR(exten, feature->exten));

	fge->feature = feature;

	AST_LIST_INSERT_HEAD(&fg->features, fge, entry);

	ast_verb(2, "Registered feature '%s' for group '%s' at exten '%s'\n",
					feature->sname, fg->gname, fge->exten);
}

void ast_unregister_feature(struct ast_call_feature *feature)
{
	if (!feature) {
		return;
	}

	AST_RWLIST_WRLOCK(&feature_list);
	AST_RWLIST_REMOVE(&feature_list, feature, feature_entry);
	AST_RWLIST_UNLOCK(&feature_list);

	ast_free(feature);
}

/*! \brief Remove all features in the list */
static void ast_unregister_features(void)
{
	struct ast_call_feature *feature;

	AST_RWLIST_WRLOCK(&feature_list);
	while ((feature = AST_RWLIST_REMOVE_HEAD(&feature_list, feature_entry))) {
		ast_free(feature);
	}
	AST_RWLIST_UNLOCK(&feature_list);
}

/*! \brief find a call feature by name */
static struct ast_call_feature *find_dynamic_feature(const char *name)
{
	struct ast_call_feature *tmp;

	AST_RWLIST_TRAVERSE(&feature_list, tmp, feature_entry) {
		if (!strcasecmp(tmp->sname, name)) {
			break;
		}
	}

	return tmp;
}

/*! \brief Remove all feature groups in the list */
static void ast_unregister_groups(void)
{
	struct feature_group *fg;
	struct feature_group_exten *fge;

	AST_RWLIST_WRLOCK(&feature_groups);
	while ((fg = AST_LIST_REMOVE_HEAD(&feature_groups, entry))) {
		while ((fge = AST_LIST_REMOVE_HEAD(&fg->features, entry))) {
			ast_string_field_free_memory(fge);
			ast_free(fge);
		}

		ast_string_field_free_memory(fg);
		ast_free(fg);
	}
	AST_RWLIST_UNLOCK(&feature_groups);
}

/*! 
 * \brief Find a group by name 
 * \param name feature name
 * \retval feature group on success.
 * \retval NULL on failure.
*/
static struct feature_group *find_group(const char *name)
{
	struct feature_group *fg = NULL;

	AST_LIST_TRAVERSE(&feature_groups, fg, entry) {
		if (!strcasecmp(fg->gname, name))
			break;
	}

	return fg;
}

void ast_rdlock_call_features(void)
{
	ast_rwlock_rdlock(&features_lock);
}

void ast_unlock_call_features(void)
{
	ast_rwlock_unlock(&features_lock);
}

struct ast_call_feature *ast_find_call_feature(const char *name)
{
	int x;
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (!strcasecmp(name, builtin_features[x].sname))
			return &builtin_features[x];
	}
	return NULL;
}

/*!
 * \brief exec an app by feature 
 * \param chan,peer,config,code,sense,data
 *
 * Find a feature, determine which channel activated
 * \retval AST_FEATURE_RETURN_NO_HANGUP_PEER
 * \retval -1 error.
 * \retval -2 when an application cannot be found.
*/
static int feature_exec_app(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	struct ast_app *app;
	struct ast_call_feature *feature = data;
	struct ast_channel *work, *idle;
	int res;

	if (!feature) { /* shouldn't ever happen! */
		ast_log(LOG_NOTICE, "Found feature before, but at execing we've lost it??\n");
		return -1; 
	}

	if (sense == FEATURE_SENSE_CHAN) {
		if (!ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLER))
			return AST_FEATURE_RETURN_KEEPTRYING;
		if (ast_test_flag(feature, AST_FEATURE_FLAG_ONSELF)) {
			work = chan;
			idle = peer;
		} else {
			work = peer;
			idle = chan;
		}
	} else {
		if (!ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLEE))
			return AST_FEATURE_RETURN_KEEPTRYING;
		if (ast_test_flag(feature, AST_FEATURE_FLAG_ONSELF)) {
			work = peer;
			idle = chan;
		} else {
			work = chan;
			idle = peer;
		}
	}

	if (!(app = pbx_findapp(feature->app))) {
		ast_log(LOG_WARNING, "Could not find application (%s)\n", feature->app);
		return -2;
	}

	ast_autoservice_start(idle);
	ast_autoservice_ignore(idle, AST_FRAME_DTMF_END);
	
	if(work && idle) {
		pbx_builtin_setvar_helper(work, "DYNAMIC_PEERNAME", idle->name);
		pbx_builtin_setvar_helper(idle, "DYNAMIC_PEERNAME", work->name);
		pbx_builtin_setvar_helper(work, "DYNAMIC_FEATURENAME", feature->sname);
		pbx_builtin_setvar_helper(idle, "DYNAMIC_FEATURENAME", feature->sname);
	}

	if (!ast_strlen_zero(feature->moh_class))
		ast_moh_start(idle, feature->moh_class, NULL);

	res = pbx_exec(work, app, feature->app_args);

	if (!ast_strlen_zero(feature->moh_class))
		ast_moh_stop(idle);

	ast_autoservice_stop(idle);

	if (res) {
		return AST_FEATURE_RETURN_SUCCESSBREAK;
	}
	return AST_FEATURE_RETURN_SUCCESS;	/*! \todo XXX should probably return res */
}

static void unmap_features(void)
{
	int x;

	ast_rwlock_wrlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++)
		strcpy(builtin_features[x].exten, builtin_features[x].default_exten);
	ast_rwlock_unlock(&features_lock);
}

static int remap_feature(const char *name, const char *value)
{
	int x, res = -1;

	ast_rwlock_wrlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (strcasecmp(builtin_features[x].sname, name))
			continue;

		ast_copy_string(builtin_features[x].exten, value, sizeof(builtin_features[x].exten));
		res = 0;
		break;
	}
	ast_rwlock_unlock(&features_lock);

	return res;
}

/*!
 * \brief Helper function for feature_interpret and ast_feature_detect
 * \param chan,peer,config,code,sense,dynamic_features_buf,features,operation,feature
 *
 * Lock features list, browse for code, unlock list
 * If a feature is found and the operation variable is set, that feature's
 * operation is executed.  The first feature found is copied to the feature parameter.
 * \retval res on success.
 * \retval -1 on failure.
*/
static int feature_interpret_helper(struct ast_channel *chan, struct ast_channel *peer,
	struct ast_bridge_config *config, const char *code, int sense, char *dynamic_features_buf,
	struct ast_flags *features, int operation, struct ast_call_feature *feature)
{
	int x;
	struct feature_group *fg = NULL;
	struct feature_group_exten *fge;
	struct ast_call_feature *tmpfeature;
	char *tmp, *tok;
	int res = AST_FEATURE_RETURN_PASSDIGITS;
	int feature_detected = 0;

	if (!(peer && chan && config) && operation) {
		return -1; /* can not run feature operation */
	}

	ast_rwlock_rdlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if ((ast_test_flag(features, builtin_features[x].feature_mask)) &&
		    !ast_strlen_zero(builtin_features[x].exten)) {
			/* Feature is up for consideration */
			if (!strcmp(builtin_features[x].exten, code)) {
				ast_debug(3, "Feature detected: fname=%s sname=%s exten=%s\n", builtin_features[x].fname, builtin_features[x].sname, builtin_features[x].exten);
				if (operation) {
					res = builtin_features[x].operation(chan, peer, config, code, sense, NULL);
				}
				memcpy(feature, &builtin_features[x], sizeof(feature));
				feature_detected = 1;
				break;
			} else if (!strncmp(builtin_features[x].exten, code, strlen(code))) {
				if (res == AST_FEATURE_RETURN_PASSDIGITS)
					res = AST_FEATURE_RETURN_STOREDIGITS;
			}
		}
	}
	ast_rwlock_unlock(&features_lock);

	if (ast_strlen_zero(dynamic_features_buf) || feature_detected) {
		return res;
	}

	tmp = dynamic_features_buf;

	while ((tok = strsep(&tmp, "#"))) {
		AST_RWLIST_RDLOCK(&feature_groups);

		fg = find_group(tok);

		if (fg) {
			AST_LIST_TRAVERSE(&fg->features, fge, entry) {
				if (!strcmp(fge->exten, code)) {
					if (operation) {
						res = fge->feature->operation(chan, peer, config, code, sense, fge->feature);
					}
					memcpy(feature, fge->feature, sizeof(feature));
					if (res != AST_FEATURE_RETURN_KEEPTRYING) {
						AST_RWLIST_UNLOCK(&feature_groups);
						break;
					}
					res = AST_FEATURE_RETURN_PASSDIGITS;
				} else if (!strncmp(fge->exten, code, strlen(code))) {
					res = AST_FEATURE_RETURN_STOREDIGITS;
				}
			}
			if (fge) {
				break;
			}
		}

		AST_RWLIST_UNLOCK(&feature_groups);

		AST_RWLIST_RDLOCK(&feature_list);

		if (!(tmpfeature = find_dynamic_feature(tok))) {
			AST_RWLIST_UNLOCK(&feature_list);
			continue;
		}

		/* Feature is up for consideration */
		if (!strcmp(tmpfeature->exten, code)) {
			ast_verb(3, " Feature Found: %s exten: %s\n",tmpfeature->sname, tok);
			if (operation) {
				res = tmpfeature->operation(chan, peer, config, code, sense, tmpfeature);
			}
			memcpy(feature, tmpfeature, sizeof(feature));
			if (res != AST_FEATURE_RETURN_KEEPTRYING) {
				AST_RWLIST_UNLOCK(&feature_list);
				break;
			}
			res = AST_FEATURE_RETURN_PASSDIGITS;
		} else if (!strncmp(tmpfeature->exten, code, strlen(code)))
			res = AST_FEATURE_RETURN_STOREDIGITS;

		AST_RWLIST_UNLOCK(&feature_list);
	}

	return res;
}

/*!
 * \brief Check the dynamic features
 * \param chan,peer,config,code,sense
 *
 * \retval res on success.
 * \retval -1 on failure.
*/

static int feature_interpret(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense) {

	char dynamic_features_buf[128];
	const char *peer_dynamic_features, *chan_dynamic_features;
	struct ast_flags features;
	struct ast_call_feature feature;
	if (sense == FEATURE_SENSE_CHAN) {
		ast_copy_flags(&features, &(config->features_caller), AST_FLAGS_ALL);
	}
	else {
		ast_copy_flags(&features, &(config->features_callee), AST_FLAGS_ALL);
	}

	ast_channel_lock(peer);
	peer_dynamic_features = ast_strdupa(S_OR(pbx_builtin_getvar_helper(peer, "DYNAMIC_FEATURES"),""));
	ast_channel_unlock(peer);

	ast_channel_lock(chan);
	chan_dynamic_features = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "DYNAMIC_FEATURES"),""));
	ast_channel_unlock(chan);

	snprintf(dynamic_features_buf, sizeof(dynamic_features_buf), "%s%s%s", S_OR(chan_dynamic_features, ""), chan_dynamic_features && peer_dynamic_features ? "#" : "", S_OR(peer_dynamic_features,""));

	ast_debug(3, "Feature interpret: chan=%s, peer=%s, code=%s, sense=%d, features=%d, dynamic=%s\n", chan->name, peer->name, code, sense, features.flags, dynamic_features_buf);

	return feature_interpret_helper(chan, peer, config, code, sense, dynamic_features_buf, &features, 1, &feature);
}


int ast_feature_detect(struct ast_channel *chan, struct ast_flags *features, const char *code, struct ast_call_feature *feature) {

	return feature_interpret_helper(chan, NULL, NULL, code, 0, NULL, features, 0, feature);
}

static void set_config_flags(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config)
{
	int x;

	ast_clear_flag(config, AST_FLAGS_ALL);

	ast_rwlock_rdlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (!ast_test_flag(builtin_features + x, AST_FEATURE_FLAG_NEEDSDTMF))
			continue;

		if (ast_test_flag(&(config->features_caller), builtin_features[x].feature_mask))
			ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);

		if (ast_test_flag(&(config->features_callee), builtin_features[x].feature_mask))
			ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
	}
	ast_rwlock_unlock(&features_lock);

	if (chan && peer && !(ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_0) && ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_1))) {
		const char *dynamic_features = pbx_builtin_getvar_helper(chan, "DYNAMIC_FEATURES");

		if (dynamic_features) {
			char *tmp = ast_strdupa(dynamic_features);
			char *tok;
			struct ast_call_feature *feature;

			/* while we have a feature */
			while ((tok = strsep(&tmp, "#"))) {
				struct feature_group *fg;

				AST_RWLIST_RDLOCK(&feature_groups);
				AST_RWLIST_TRAVERSE(&feature_groups, fg, entry) {
					struct feature_group_exten *fge;

					AST_LIST_TRAVERSE(&fg->features, fge, entry) {
						if (ast_test_flag(fge->feature, AST_FEATURE_FLAG_BYCALLER)) {
							ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
						}
						if (ast_test_flag(fge->feature, AST_FEATURE_FLAG_BYCALLEE)) {
							ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
						}
					}
				}
				AST_RWLIST_UNLOCK(&feature_groups);

				AST_RWLIST_RDLOCK(&feature_list);
				if ((feature = find_dynamic_feature(tok)) && ast_test_flag(feature, AST_FEATURE_FLAG_NEEDSDTMF)) {
					if (ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLER)) {
						ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
					}
					if (ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLEE)) {
						ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
					}
				}
				AST_RWLIST_UNLOCK(&feature_list);
			}
		}
	}
}

/*!
 * \internal
 * \brief Get feature and dial.
 *
 * \param caller Channel to represent as the calling channel for the dialed channel.
 * \param caller_name Original caller channel name.
 * \param requestor Channel to say is requesting the dial (usually the caller).
 * \param transferee Channel that the dialed channel will be transferred to.
 * \param type Channel technology type to dial.
 * \param format Codec formats for dialed channel.
 * \param data Dialed channel extra parameters for ast_request() and ast_call().
 * \param timeout Time limit for dialed channel to answer in ms. Must be greater than zero.
 * \param outstate Status of dialed channel if unsuccessful.
 * \param language Language of the caller.
 *
 * \note
 * outstate can be:
 * 0, AST_CONTROL_BUSY, AST_CONTROL_CONGESTION,
 * AST_CONTROL_ANSWER, or AST_CONTROL_UNHOLD.  If
 * AST_CONTROL_UNHOLD then the caller channel cancelled the
 * transfer or the dialed channel did not answer before the
 * timeout.
 *
 * \details
 * Request channel, set channel variables, initiate call,
 * check if they want to disconnect, go into loop, check if timeout has elapsed,
 * check if person to be transfered hung up, check for answer break loop,
 * set cdr return channel.
 *
 * \retval Channel Connected channel for transfer.
 * \retval NULL on failure to get third party connected.
 *
 * \note This is similar to __ast_request_and_dial() in channel.c
 */
static struct ast_channel *feature_request_and_dial(struct ast_channel *caller,
	const char *caller_name, struct ast_channel *requestor,
	struct ast_channel *transferee, const char *type, struct ast_format_cap *cap, void *data,
	int timeout, int *outstate, const char *language)
{
	int state = 0;
	int cause = 0;
	int to;
	int caller_hungup;
	int transferee_hungup;
	struct ast_channel *chan;
	struct ast_channel *monitor_chans[3];
	struct ast_channel *active_channel;
	int res;
	int ready = 0;
	struct timeval started;
	int x, len = 0;
	char *disconnect_code = NULL, *dialed_code = NULL;
	struct ast_format_cap *tmp_cap;
	struct ast_format best_audio_fmt;
	struct ast_frame *f;
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_frames;

	tmp_cap = ast_format_cap_alloc_nolock();
	if (!tmp_cap) {
		return NULL;
	}
	ast_best_codec(cap, &best_audio_fmt);
	ast_format_cap_add(tmp_cap, &best_audio_fmt);

	caller_hungup = ast_check_hangup(caller);

	if (!(chan = ast_request(type, tmp_cap, requestor, data, &cause))) {
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		switch (cause) {
		case AST_CAUSE_BUSY:
			state = AST_CONTROL_BUSY;
			break;
		case AST_CAUSE_CONGESTION:
			state = AST_CONTROL_CONGESTION;
			break;
		default:
			state = 0;
			break;
		}
		goto done;
	}

	ast_string_field_set(chan, language, language);
	ast_channel_inherit_variables(caller, chan);
	pbx_builtin_setvar_helper(chan, "TRANSFERERNAME", caller_name);

	ast_channel_lock(chan);
	ast_connected_line_copy_from_caller(&chan->connected, &requestor->caller);
	ast_channel_unlock(chan);

	if (ast_call(chan, data, timeout)) {
		ast_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, (char *)data);
		switch (chan->hangupcause) {
		case AST_CAUSE_BUSY:
			state = AST_CONTROL_BUSY;
			break;
		case AST_CAUSE_CONGESTION:
			state = AST_CONTROL_CONGESTION;
			break;
		default:
			state = 0;
			break;
		}
		goto done;
	}

	/* support dialing of the featuremap disconnect code while performing an attended tranfer */
	ast_rwlock_rdlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (strcasecmp(builtin_features[x].sname, "disconnect"))
			continue;

		disconnect_code = builtin_features[x].exten;
		len = strlen(disconnect_code) + 1;
		dialed_code = alloca(len);
		memset(dialed_code, 0, len);
		break;
	}
	ast_rwlock_unlock(&features_lock);
	x = 0;
	started = ast_tvnow();
	to = timeout;
	AST_LIST_HEAD_INIT_NOLOCK(&deferred_frames);

	ast_poll_channel_add(caller, chan);

	transferee_hungup = 0;
	while (!ast_check_hangup(transferee) && (chan->_state != AST_STATE_UP)) {
		int num_chans = 0;

		monitor_chans[num_chans++] = transferee;
		monitor_chans[num_chans++] = chan;
		if (!caller_hungup) {
			if (ast_check_hangup(caller)) {
				caller_hungup = 1;

#if defined(ATXFER_NULL_TECH)
				/* Change caller's name to ensure that it will remain unique. */
				set_new_chan_name(caller);

				/*
				 * Get rid of caller's physical technology so it is free for
				 * other calls.
				 */
				set_null_chan_tech(caller);
#endif	/* defined(ATXFER_NULL_TECH) */
			} else {
				/* caller is not hungup so monitor it. */
				monitor_chans[num_chans++] = caller;
			}
		}

		/* see if the timeout has been violated */
		if (ast_tvdiff_ms(ast_tvnow(), started) > timeout) {
			state = AST_CONTROL_UNHOLD;
			ast_log(LOG_NOTICE, "We exceeded our AT-timeout for %s\n", chan->name);
			break; /*doh! timeout*/
		}

		active_channel = ast_waitfor_n(monitor_chans, num_chans, &to);
		if (!active_channel)
			continue;

		f = NULL;
		if (transferee == active_channel) {
			struct ast_frame *dup_f;

			f = ast_read(transferee);
			if (f == NULL) { /*doh! where'd he go?*/
				transferee_hungup = 1;
				state = 0;
				break;
			}
			if (ast_is_deferrable_frame(f)) {
				dup_f = ast_frisolate(f);
				if (dup_f) {
					if (dup_f == f) {
						f = NULL;
					}
					AST_LIST_INSERT_HEAD(&deferred_frames, dup_f, frame_list);
				}
			}
		} else if (chan == active_channel) {
			if (!ast_strlen_zero(chan->call_forward)) {
				chan = ast_call_forward(caller, chan, NULL, tmp_cap, NULL, &state);
				if (!chan) {
					break;
				}
				continue;
			}
			f = ast_read(chan);
			if (f == NULL) { /*doh! where'd he go?*/
				switch (chan->hangupcause) {
				case AST_CAUSE_BUSY:
					state = AST_CONTROL_BUSY;
					break;
				case AST_CAUSE_CONGESTION:
					state = AST_CONTROL_CONGESTION;
					break;
				default:
					state = 0;
					break;
				}
				break;
			}

			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass.integer == AST_CONTROL_RINGING) {
					ast_verb(3, "%s is ringing\n", chan->name);
					ast_indicate(caller, AST_CONTROL_RINGING);
				} else if (f->subclass.integer == AST_CONTROL_BUSY) {
					state = f->subclass.integer;
					ast_verb(3, "%s is busy\n", chan->name);
					ast_indicate(caller, AST_CONTROL_BUSY);
					ast_frfree(f);
					break;
				} else if (f->subclass.integer == AST_CONTROL_CONGESTION) {
					state = f->subclass.integer;
					ast_verb(3, "%s is congested\n", chan->name);
					ast_indicate(caller, AST_CONTROL_CONGESTION);
					ast_frfree(f);
					break;
				} else if (f->subclass.integer == AST_CONTROL_ANSWER) {
					/* This is what we are hoping for */
					state = f->subclass.integer;
					ast_frfree(f);
					ready=1;
					break;
				} else if (f->subclass.integer == AST_CONTROL_CONNECTED_LINE) {
					if (caller_hungup) {
						struct ast_party_connected_line connected;

						/* Just save it for the transfer. */
						ast_party_connected_line_set_init(&connected, &caller->connected);
						res = ast_connected_line_parse_data(f->data.ptr, f->datalen,
							&connected);
						if (!res) {
							ast_channel_set_connected_line(caller, &connected, NULL);
						}
						ast_party_connected_line_free(&connected);
					} else {
						ast_autoservice_start(transferee);
						if (ast_channel_connected_line_macro(chan, caller, f, 1, 1)) {
							ast_indicate_data(caller, AST_CONTROL_CONNECTED_LINE,
								f->data.ptr, f->datalen);
						}
						ast_autoservice_stop(transferee);
					}
				} else if (f->subclass.integer == AST_CONTROL_REDIRECTING) {
					if (!caller_hungup) {
						ast_autoservice_start(transferee);
						if (ast_channel_redirecting_macro(chan, caller, f, 1, 1)) {
							ast_indicate_data(caller, AST_CONTROL_REDIRECTING,
								f->data.ptr, f->datalen);
						}
						ast_autoservice_stop(transferee);
					}
				} else if (f->subclass.integer != -1 && f->subclass.integer != AST_CONTROL_PROGRESS) {
					ast_log(LOG_NOTICE, "Don't know what to do about control frame: %d\n", f->subclass.integer);
				}
				/* else who cares */
			} else if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO) {
				ast_write(caller, f);
			}
		} else if (caller == active_channel) {
			f = ast_read(caller);
			if (f) {
				if (f->frametype == AST_FRAME_DTMF) {
					dialed_code[x++] = f->subclass.integer;
					dialed_code[x] = '\0';
					if (strlen(dialed_code) == len) {
						x = 0;
					} else if (x && strncmp(dialed_code, disconnect_code, x)) {
						x = 0;
						dialed_code[x] = '\0';
					}
					if (*dialed_code && !strcmp(dialed_code, disconnect_code)) {
						/* Caller Canceled the call */
						state = AST_CONTROL_UNHOLD;
						ast_frfree(f);
						break;
					}
				} else if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO) {
					ast_write(chan, f);
				}
			}
		}
		if (f)
			ast_frfree(f);
	} /* end while */

	ast_poll_channel_del(caller, chan);

	/*
	 * We need to free all the deferred frames, but we only need to
	 * queue the deferred frames if no hangup was received.
	 */
	ast_channel_lock(transferee);
	transferee_hungup = (transferee_hungup || ast_check_hangup(transferee));
	while ((f = AST_LIST_REMOVE_HEAD(&deferred_frames, frame_list))) {
		if (!transferee_hungup) {
			ast_queue_frame_head(transferee, f);
		}
		ast_frfree(f);
	}
	ast_channel_unlock(transferee);

done:
	ast_indicate(caller, -1);
	if (chan && (ready || chan->_state == AST_STATE_UP)) {
		state = AST_CONTROL_ANSWER;
	} else if (chan) {
		ast_hangup(chan);
		chan = NULL;
	}

	tmp_cap = ast_format_cap_destroy(tmp_cap);

	if (outstate)
		*outstate = state;

	return chan;
}

void ast_channel_log(char *title, struct ast_channel *chan);

void ast_channel_log(char *title, struct ast_channel *chan) /* for debug, this is handy enough to justify keeping it in the source */
{
       ast_log(LOG_NOTICE, "______ %s (%lx)______\n", title, (unsigned long)chan);
       ast_log(LOG_NOTICE, "CHAN: name: %s;  appl: %s; data: %s; contxt: %s;  exten: %s; pri: %d;\n",
		       chan->name, chan->appl, chan->data, chan->context, chan->exten, chan->priority);
       ast_log(LOG_NOTICE, "CHAN: acctcode: %s;  dialcontext: %s; amaflags: %x; maccontxt: %s;  macexten: %s; macpri: %d;\n",
		       chan->accountcode, chan->dialcontext, chan->amaflags, chan->macrocontext, chan->macroexten, chan->macropriority);
       ast_log(LOG_NOTICE, "CHAN: masq: %p;  masqr: %p; _bridge: %p; uniqueID: %s; linkedID:%s\n",
		       chan->masq, chan->masqr,
		       chan->_bridge, chan->uniqueid, chan->linkedid);
       if (chan->masqr)
	       ast_log(LOG_NOTICE, "CHAN: masquerading as: %s;  cdr: %p;\n",
		               chan->masqr->name, chan->masqr->cdr);
       if (chan->_bridge)
	       ast_log(LOG_NOTICE, "CHAN: Bridged to %s\n", chan->_bridge->name);

	ast_log(LOG_NOTICE, "===== done ====\n");
}

/*!
 * \brief return the first unlocked cdr in a possible chain
*/
static struct ast_cdr *pick_unlocked_cdr(struct ast_cdr *cdr)
{
	struct ast_cdr *cdr_orig = cdr;
	while (cdr) {
		if (!ast_test_flag(cdr,AST_CDR_FLAG_LOCKED))
			return cdr;
		cdr = cdr->next;
	}
	return cdr_orig; /* everybody LOCKED or some other weirdness, like a NULL */
}

static void set_bridge_features_on_config(struct ast_bridge_config *config, const char *features)
{
	const char *feature;

	if (ast_strlen_zero(features)) {
		return;
	}

	for (feature = features; *feature; feature++) {
		switch (*feature) {
		case 'T' :
		case 't' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_REDIRECT);
			break;
		case 'K' :
		case 'k' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_PARKCALL);
			break;
		case 'H' :
		case 'h' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_DISCONNECT);
			break;
		case 'W' :
		case 'w' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_AUTOMON);
			break;
		default :
			ast_log(LOG_WARNING, "Skipping unknown feature code '%c'\n", *feature);
		}
	}
}

static void add_features_datastores(struct ast_channel *caller, struct ast_channel *callee, struct ast_bridge_config *config)
{
	struct ast_datastore *ds_callee_features = NULL, *ds_caller_features = NULL;
	struct ast_dial_features *callee_features = NULL, *caller_features = NULL;

	ast_channel_lock(caller);
	ds_caller_features = ast_channel_datastore_find(caller, &dial_features_info, NULL);
	ast_channel_unlock(caller);
	if (!ds_caller_features) {
		if (!(ds_caller_features = ast_datastore_alloc(&dial_features_info, NULL))) {
			ast_log(LOG_WARNING, "Unable to create channel datastore for caller features. Aborting!\n");
			return;
		}
		if (!(caller_features = ast_calloc(1, sizeof(*caller_features)))) {
			ast_log(LOG_WARNING, "Unable to allocate memory for callee feature flags. Aborting!\n");
			ast_datastore_free(ds_caller_features);
			return;
		}
		ds_caller_features->inheritance = DATASTORE_INHERIT_FOREVER;
		caller_features->is_caller = 1;
		ast_copy_flags(&(caller_features->features_callee), &(config->features_callee), AST_FLAGS_ALL);
		ast_copy_flags(&(caller_features->features_caller), &(config->features_caller), AST_FLAGS_ALL);
		ds_caller_features->data = caller_features;
		ast_channel_lock(caller);
		ast_channel_datastore_add(caller, ds_caller_features);
		ast_channel_unlock(caller);
	} else {
		/* If we don't return here, then when we do a builtin_atxfer we will copy the disconnect
		 * flags over from the atxfer to the caller */
		return;
	}

	ast_channel_lock(callee);
	ds_callee_features = ast_channel_datastore_find(callee, &dial_features_info, NULL);
	ast_channel_unlock(callee);
	if (!ds_callee_features) {
		if (!(ds_callee_features = ast_datastore_alloc(&dial_features_info, NULL))) {
			ast_log(LOG_WARNING, "Unable to create channel datastore for callee features. Aborting!\n");
			return;
		}
		if (!(callee_features = ast_calloc(1, sizeof(*callee_features)))) {
			ast_log(LOG_WARNING, "Unable to allocate memory for callee feature flags. Aborting!\n");
			ast_datastore_free(ds_callee_features);
			return;
		}
		ds_callee_features->inheritance = DATASTORE_INHERIT_FOREVER;
		callee_features->is_caller = 0;
		ast_copy_flags(&(callee_features->features_callee), &(config->features_caller), AST_FLAGS_ALL);
		ast_copy_flags(&(callee_features->features_caller), &(config->features_callee), AST_FLAGS_ALL);
		ds_callee_features->data = callee_features;
		ast_channel_lock(callee);
		ast_channel_datastore_add(callee, ds_callee_features);
		ast_channel_unlock(callee);
	}

	return;
}

/*!
 * \brief bridge the call and set CDR
 * \param chan,peer,config
 * 
 * Set start time, check for two channels,check if monitor on
 * check for feature activation, create new CDR
 * \retval res on success.
 * \retval -1 on failure to bridge.
*/
int ast_bridge_call(struct ast_channel *chan,struct ast_channel *peer,struct ast_bridge_config *config)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_frame *f;
	struct ast_channel *who;
	char chan_featurecode[FEATURE_MAX_LEN + 1]="";
	char peer_featurecode[FEATURE_MAX_LEN + 1]="";
	char orig_channame[AST_MAX_EXTENSION];
	char orig_peername[AST_MAX_EXTENSION];
	int res;
	int diff;
	int hasfeatures=0;
	int hadfeatures=0;
	int autoloopflag;
	int we_disabled_peer_cdr = 0;
	struct ast_option_header *aoh;
	struct ast_cdr *bridge_cdr = NULL;
	struct ast_cdr *orig_peer_cdr = NULL;
	struct ast_cdr *chan_cdr = chan->cdr; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *peer_cdr = peer->cdr; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *new_chan_cdr = NULL; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *new_peer_cdr = NULL; /* the proper chan cdr, if there are forked cdrs */

	if (chan && peer) {
		pbx_builtin_setvar_helper(chan, "BRIDGEPEER", peer->name);
		pbx_builtin_setvar_helper(peer, "BRIDGEPEER", chan->name);
	} else if (chan) {
		pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", NULL);
	}

	set_bridge_features_on_config(config, pbx_builtin_getvar_helper(chan, "BRIDGE_FEATURES"));
	add_features_datastores(chan, peer, config);

	/* This is an interesting case.  One example is if a ringing channel gets redirected to
	 * an extension that picks up a parked call.  This will make sure that the call taken
	 * out of parking gets told that the channel it just got bridged to is still ringing. */
	if (chan->_state == AST_STATE_RINGING && peer->visible_indication != AST_CONTROL_RINGING) {
		ast_indicate(peer, AST_CONTROL_RINGING);
	}

	if (monitor_ok) {
		const char *monitor_exec;
		struct ast_channel *src = NULL;
		if (!monitor_app) {
			if (!(monitor_app = pbx_findapp("Monitor")))
				monitor_ok=0;
		}
		if ((monitor_exec = pbx_builtin_getvar_helper(chan, "AUTO_MONITOR"))) 
			src = chan;
		else if ((monitor_exec = pbx_builtin_getvar_helper(peer, "AUTO_MONITOR")))
			src = peer;
		if (monitor_app && src) {
			char *tmp = ast_strdupa(monitor_exec);
			pbx_exec(src, monitor_app, tmp);
		}
	}

	set_config_flags(chan, peer, config);

	/* Answer if need be */
	if (chan->_state != AST_STATE_UP) {
		if (ast_raw_answer(chan, 1)) {
			return -1;
		}
	}

#ifdef FOR_DEBUG
	/* show the two channels and cdrs involved in the bridge for debug & devel purposes */
	ast_channel_log("Pre-bridge CHAN Channel info", chan);
	ast_channel_log("Pre-bridge PEER Channel info", peer);
#endif
	/* two channels are being marked as linked here */
	ast_channel_set_linkgroup(chan,peer);

	/* copy the userfield from the B-leg to A-leg if applicable */
	if (chan->cdr && peer->cdr && !ast_strlen_zero(peer->cdr->userfield)) {
		char tmp[256];
		if (!ast_strlen_zero(chan->cdr->userfield)) {
			snprintf(tmp, sizeof(tmp), "%s;%s", chan->cdr->userfield, peer->cdr->userfield);
			ast_cdr_appenduserfield(chan, tmp);
		} else
			ast_cdr_setuserfield(chan, peer->cdr->userfield);
		/* Don't delete the CDR; just disable it. */
		ast_set_flag(peer->cdr, AST_CDR_FLAG_POST_DISABLED);
		we_disabled_peer_cdr = 1;
	}
	ast_copy_string(orig_channame,chan->name,sizeof(orig_channame));
	ast_copy_string(orig_peername,peer->name,sizeof(orig_peername));
	orig_peer_cdr = peer_cdr;
	
	if (!chan_cdr || (chan_cdr && !ast_test_flag(chan_cdr, AST_CDR_FLAG_POST_DISABLED))) {
		
		if (chan_cdr) {
			ast_set_flag(chan_cdr, AST_CDR_FLAG_MAIN);
			ast_cdr_update(chan);
			bridge_cdr = ast_cdr_dup_unique_swap(chan_cdr);
			/* rip any forked CDR's off of the chan_cdr and attach
			 * them to the bridge_cdr instead */
			bridge_cdr->next = chan_cdr->next;
			chan_cdr->next = NULL;
			ast_copy_string(bridge_cdr->lastapp, S_OR(chan->appl, ""), sizeof(bridge_cdr->lastapp));
			ast_copy_string(bridge_cdr->lastdata, S_OR(chan->data, ""), sizeof(bridge_cdr->lastdata));
			if (peer_cdr && !ast_strlen_zero(peer_cdr->userfield)) {
				ast_copy_string(bridge_cdr->userfield, peer_cdr->userfield, sizeof(bridge_cdr->userfield));
			}
			ast_cdr_setaccount(peer, chan->accountcode);

		} else {
			/* better yet, in a xfer situation, find out why the chan cdr got zapped (pun unintentional) */
			bridge_cdr = ast_cdr_alloc(); /* this should be really, really rare/impossible? */
			ast_copy_string(bridge_cdr->channel, chan->name, sizeof(bridge_cdr->channel));
			ast_copy_string(bridge_cdr->dstchannel, peer->name, sizeof(bridge_cdr->dstchannel));
			ast_copy_string(bridge_cdr->uniqueid, chan->uniqueid, sizeof(bridge_cdr->uniqueid));
			ast_copy_string(bridge_cdr->lastapp, S_OR(chan->appl, ""), sizeof(bridge_cdr->lastapp));
			ast_copy_string(bridge_cdr->lastdata, S_OR(chan->data, ""), sizeof(bridge_cdr->lastdata));
			ast_cdr_setcid(bridge_cdr, chan);
			bridge_cdr->disposition = (chan->_state == AST_STATE_UP) ?  AST_CDR_ANSWERED : AST_CDR_NULL;
			bridge_cdr->amaflags = chan->amaflags ? chan->amaflags :  ast_default_amaflags;
			ast_copy_string(bridge_cdr->accountcode, chan->accountcode, sizeof(bridge_cdr->accountcode));
			/* Destination information */
			ast_copy_string(bridge_cdr->dst, chan->exten, sizeof(bridge_cdr->dst));
			ast_copy_string(bridge_cdr->dcontext, chan->context, sizeof(bridge_cdr->dcontext));
			if (peer_cdr) {
				bridge_cdr->start = peer_cdr->start;
				ast_copy_string(bridge_cdr->userfield, peer_cdr->userfield, sizeof(bridge_cdr->userfield));
			} else {
				ast_cdr_start(bridge_cdr);
			}
		}
		ast_debug(4, "bridge answer set, chan answer set\n");
		/* peer_cdr->answer will be set when a macro runs on the peer;
		   in that case, the bridge answer will be delayed while the
		   macro plays on the peer channel. The peer answered the call
		   before the macro started playing. To the phone system,
		   this is billable time for the call, even tho the caller
		   hears nothing but ringing while the macro does its thing. */

		/* Another case where the peer cdr's time will be set, is when
		   A self-parks by pickup up phone and dialing 700, then B
		   picks up A by dialing its parking slot; there may be more 
		   practical paths that get the same result, tho... in which
		   case you get the previous answer time from the Park... which
		   is before the bridge's start time, so I added in the 
		   tvcmp check to the if below */

		if (peer_cdr && !ast_tvzero(peer_cdr->answer) && ast_tvcmp(peer_cdr->answer, bridge_cdr->start) >= 0) {
			ast_cdr_setanswer(bridge_cdr, peer_cdr->answer);
			ast_cdr_setdisposition(bridge_cdr, peer_cdr->disposition);
			if (chan_cdr) {
				ast_cdr_setanswer(chan_cdr, peer_cdr->answer);
				ast_cdr_setdisposition(chan_cdr, peer_cdr->disposition);
			}
		} else {
			ast_cdr_answer(bridge_cdr);
			if (chan_cdr) {
				ast_cdr_answer(chan_cdr); /* for the sake of cli status checks */
			}
		}
		if (ast_test_flag(chan,AST_FLAG_BRIDGE_HANGUP_DONT) && (chan_cdr || peer_cdr)) {
			if (chan_cdr) {
				ast_set_flag(chan_cdr, AST_CDR_FLAG_BRIDGED);
			}
			if (peer_cdr) {
				ast_set_flag(peer_cdr, AST_CDR_FLAG_BRIDGED);
			}
		}
		/* the DIALED flag may be set if a dialed channel is transfered
		 * and then bridged to another channel.  In order for the
		 * bridge CDR to be written, the DIALED flag must not be
		 * present. */
		ast_clear_flag(bridge_cdr, AST_CDR_FLAG_DIALED);
	}
	ast_cel_report_event(chan, AST_CEL_BRIDGE_START, NULL, NULL, NULL);
	for (;;) {
		struct ast_channel *other;	/* used later */
	
		res = ast_channel_bridge(chan, peer, config, &f, &who);
		
		/* When frame is not set, we are probably involved in a situation
		   where we've timed out.
		   When frame is set, we'll come this code twice; once for DTMF_BEGIN
		   and also for DTMF_END. If we flow into the following 'if' for both, then 
		   our wait times are cut in half, as both will subtract from the
		   feature_timer. Not good!
		*/
		if (config->feature_timer && (!f || f->frametype == AST_FRAME_DTMF_END)) {
			/* Update feature timer for next pass */
			diff = ast_tvdiff_ms(ast_tvnow(), config->feature_start_time);
			if (res == AST_BRIDGE_RETRY) {
				/* The feature fully timed out but has not been updated. Skip
				 * the potential round error from the diff calculation and
				 * explicitly set to expired. */
				config->feature_timer = -1;
			} else {
				config->feature_timer -= diff;
			}

			if (hasfeatures) {
				if (config->feature_timer <= 0) {
					/* Not *really* out of time, just out of time for
					   digits to come in for features. */
					ast_debug(1, "Timed out for feature!\n");
					if (!ast_strlen_zero(peer_featurecode)) {
						ast_dtmf_stream(chan, peer, peer_featurecode, 0, 0);
						memset(peer_featurecode, 0, sizeof(peer_featurecode));
					}
					if (!ast_strlen_zero(chan_featurecode)) {
						ast_dtmf_stream(peer, chan, chan_featurecode, 0, 0);
						memset(chan_featurecode, 0, sizeof(chan_featurecode));
					}
					if (f)
						ast_frfree(f);
					hasfeatures = !ast_strlen_zero(chan_featurecode) || !ast_strlen_zero(peer_featurecode);
					if (!hasfeatures) {
						/* No more digits expected - reset the timer */
						config->feature_timer = 0;
					}
					hadfeatures = hasfeatures;
					/* Continue as we were */
					continue;
				} else if (!f) {
					/* The bridge returned without a frame and there is a feature in progress.
					 * However, we don't think the feature has quite yet timed out, so just
					 * go back into the bridge. */
					continue;
 				}
			} else {
				if (config->feature_timer <=0) {
					/* We ran out of time */
					config->feature_timer = 0;
					who = chan;
					if (f)
						ast_frfree(f);
					f = NULL;
					res = 0;
				}
			}
		}
		if (res < 0) {
			if (!ast_test_flag(chan, AST_FLAG_ZOMBIE) && !ast_test_flag(peer, AST_FLAG_ZOMBIE) && !ast_check_hangup(chan) && !ast_check_hangup(peer)) {
				ast_log(LOG_WARNING, "Bridge failed on channels %s and %s\n", chan->name, peer->name);
			}
			goto before_you_go;
		}
		
		if (!f || (f->frametype == AST_FRAME_CONTROL &&
				(f->subclass.integer == AST_CONTROL_HANGUP || f->subclass.integer == AST_CONTROL_BUSY ||
					f->subclass.integer == AST_CONTROL_CONGESTION))) {
			res = -1;
			break;
		}
		/* many things should be sent to the 'other' channel */
		other = (who == chan) ? peer : chan;
		if (f->frametype == AST_FRAME_CONTROL) {
			switch (f->subclass.integer) {
			case AST_CONTROL_RINGING:
			case AST_CONTROL_FLASH:
			case -1:
				ast_indicate(other, f->subclass.integer);
				break;
			case AST_CONTROL_CONNECTED_LINE:
				if (!ast_channel_connected_line_macro(who, other, f, who != chan, 1)) {
					break;
				}
				ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				break;
			case AST_CONTROL_REDIRECTING:
				if (!ast_channel_redirecting_macro(who, other, f, who != chan, 1)) {
					break;
				}
				ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				break;
			case AST_CONTROL_AOC:
			case AST_CONTROL_HOLD:
			case AST_CONTROL_UNHOLD:
				ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				break;
			case AST_CONTROL_OPTION:
				aoh = f->data.ptr;
				/* Forward option Requests */
				if (aoh && aoh->flag == AST_OPTION_FLAG_REQUEST) {
					ast_channel_setoption(other, ntohs(aoh->option), aoh->data, 
						f->datalen - sizeof(struct ast_option_header), 0);
				}
				break;
			}
		} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
			/* eat it */
		} else if (f->frametype == AST_FRAME_DTMF) {
			char *featurecode;
			int sense;

			hadfeatures = hasfeatures;
			/* This cannot overrun because the longest feature is one shorter than our buffer */
			if (who == chan) {
				sense = FEATURE_SENSE_CHAN;
				featurecode = chan_featurecode;
			} else  {
				sense = FEATURE_SENSE_PEER;
				featurecode = peer_featurecode;
			}
			/*! append the event to featurecode. we rely on the string being zero-filled, and
			 * not overflowing it. 
			 * \todo XXX how do we guarantee the latter ?
			 */
			featurecode[strlen(featurecode)] = f->subclass.integer;
			/* Get rid of the frame before we start doing "stuff" with the channels */
			ast_frfree(f);
			f = NULL;
			config->feature_timer = 0;
			res = feature_interpret(chan, peer, config, featurecode, sense);
			switch(res) {
			case AST_FEATURE_RETURN_PASSDIGITS:
				ast_dtmf_stream(other, who, featurecode, 0, 0);
				/* Fall through */
			case AST_FEATURE_RETURN_SUCCESS:
				memset(featurecode, 0, sizeof(chan_featurecode));
				break;
			}
			if (res >= AST_FEATURE_RETURN_PASSDIGITS) {
				res = 0;
			} else {
				break;
			}
			hasfeatures = !ast_strlen_zero(chan_featurecode) || !ast_strlen_zero(peer_featurecode);
			if (hadfeatures && !hasfeatures) {
				/* Feature completed or timed out */
				config->feature_timer = 0;
			} else if (hasfeatures) {
				if (config->timelimit) {
					/* No warning next time - we are waiting for future */
					ast_set_flag(config, AST_FEATURE_WARNING_ACTIVE);
				}
				config->feature_start_time = ast_tvnow();
				config->feature_timer = featuredigittimeout;
				ast_debug(1, "Set feature timer to %ld ms\n", config->feature_timer);
			}
		}
		if (f)
			ast_frfree(f);

	}
	ast_cel_report_event(chan, AST_CEL_BRIDGE_END, NULL, NULL, NULL);
   before_you_go:

	if (ast_test_flag(chan,AST_FLAG_BRIDGE_HANGUP_DONT)) {
		ast_clear_flag(chan,AST_FLAG_BRIDGE_HANGUP_DONT); /* its job is done */
		if (bridge_cdr) {
			ast_cdr_discard(bridge_cdr);
			/* QUESTION: should we copy bridge_cdr fields to the peer before we throw it away? */
		}
		return res; /* if we shouldn't do the h-exten, we shouldn't do the bridge cdr, either! */
	}

	if (config->end_bridge_callback) {
		config->end_bridge_callback(config->end_bridge_callback_data);
	}

	/* run the hangup exten on the chan object IFF it was NOT involved in a parking situation 
	 * if it were, then chan belongs to a different thread now, and might have been hung up long
     * ago.
	 */
	if (!ast_test_flag(&(config->features_caller),AST_FEATURE_NO_H_EXTEN)
		&& ast_exists_extension(chan, chan->context, "h", 1,
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))) {
		struct ast_cdr *swapper = NULL;
		char savelastapp[AST_MAX_EXTENSION];
		char savelastdata[AST_MAX_EXTENSION];
		char save_exten[AST_MAX_EXTENSION];
		int  save_prio;
		int  found = 0;	/* set if we find at least one match */
		int  spawn_error = 0;
		
		autoloopflag = ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP);
		ast_set_flag(chan, AST_FLAG_IN_AUTOLOOP);
		if (bridge_cdr && ast_opt_end_cdr_before_h_exten) {
			ast_cdr_end(bridge_cdr);
		}
		/* swap the bridge cdr and the chan cdr for a moment, and let the endbridge
		   dialplan code operate on it */
		ast_channel_lock(chan);
		if (bridge_cdr) {
			swapper = chan->cdr;
			ast_copy_string(savelastapp, bridge_cdr->lastapp, sizeof(bridge_cdr->lastapp));
			ast_copy_string(savelastdata, bridge_cdr->lastdata, sizeof(bridge_cdr->lastdata));
			chan->cdr = bridge_cdr;
		}
		ast_copy_string(save_exten, chan->exten, sizeof(save_exten));
		save_prio = chan->priority;
		ast_copy_string(chan->exten, "h", sizeof(chan->exten));
		chan->priority = 1;
		ast_channel_unlock(chan);
		while ((spawn_error = ast_spawn_extension(chan, chan->context, chan->exten,
			chan->priority,
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL),
			&found, 1)) == 0) {
			chan->priority++;
		}
		if (spawn_error
			&& (!ast_exists_extension(chan, chan->context, chan->exten, chan->priority,
				S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))
				|| ast_check_hangup(chan))) {
			/* if the extension doesn't exist or a hangup occurred, this isn't really a spawn error */
			spawn_error = 0;
		}
		if (found && spawn_error) {
			/* Something bad happened, or a hangup has been requested. */
			ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
			ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
		}
		/* swap it back */
		ast_channel_lock(chan);
		ast_copy_string(chan->exten, save_exten, sizeof(chan->exten));
		chan->priority = save_prio;
		if (bridge_cdr) {
			if (chan->cdr == bridge_cdr) {
				chan->cdr = swapper;
			} else {
				bridge_cdr = NULL;
			}
		}
		if (!spawn_error) {
			ast_set_flag(chan, AST_FLAG_BRIDGE_HANGUP_RUN);
		}
		ast_channel_unlock(chan);
		/* protect the lastapp/lastdata against the effects of the hangup/dialplan code */
		if (bridge_cdr) {
			ast_copy_string(bridge_cdr->lastapp, savelastapp, sizeof(bridge_cdr->lastapp));
			ast_copy_string(bridge_cdr->lastdata, savelastdata, sizeof(bridge_cdr->lastdata));
		}
		ast_set2_flag(chan, autoloopflag, AST_FLAG_IN_AUTOLOOP);
	}
	
	/* obey the NoCDR() wishes. -- move the DISABLED flag to the bridge CDR if it was set on the channel during the bridge... */
	new_chan_cdr = pick_unlocked_cdr(chan->cdr); /* the proper chan cdr, if there are forked cdrs */
	if (bridge_cdr && new_chan_cdr && ast_test_flag(new_chan_cdr, AST_CDR_FLAG_POST_DISABLED))
		ast_set_flag(bridge_cdr, AST_CDR_FLAG_POST_DISABLED);

	/* we can post the bridge CDR at this point */
	if (bridge_cdr) {
		ast_cdr_end(bridge_cdr);
		ast_cdr_detach(bridge_cdr);
	}
	
	/* do a specialized reset on the beginning channel
	   CDR's, if they still exist, so as not to mess up
	   issues in future bridges;
	   
	   Here are the rules of the game:
	   1. The chan and peer channel pointers will not change
	      during the life of the bridge.
	   2. But, in transfers, the channel names will change.
	      between the time the bridge is started, and the
	      time the channel ends. 
	      Usually, when a channel changes names, it will
	      also change CDR pointers.
	   3. Usually, only one of the two channels (chan or peer)
	      will change names.
	   4. Usually, if a channel changes names during a bridge,
	      it is because of a transfer. Usually, in these situations,
	      it is normal to see 2 bridges running simultaneously, and
	      it is not unusual to see the two channels that change
	      swapped between bridges.
	   5. After a bridge occurs, we have 2 or 3 channels' CDRs
	      to attend to; if the chan or peer changed names,
	      we have the before and after attached CDR's.
	*/

	if (new_chan_cdr) {
		struct ast_channel *chan_ptr = NULL;

		if (strcasecmp(orig_channame, chan->name) != 0) { 
			/* old channel */
			if ((chan_ptr = ast_channel_get_by_name(orig_channame))) {
				ast_channel_lock(chan_ptr);
				if (!ast_bridged_channel(chan_ptr)) {
					struct ast_cdr *cur;
					for (cur = chan_ptr->cdr; cur; cur = cur->next) {
						if (cur == chan_cdr) {
							break;
						}
					}
					if (cur) {
						ast_cdr_specialized_reset(chan_cdr, 0);
					}
				}
				ast_channel_unlock(chan_ptr);
				chan_ptr = ast_channel_unref(chan_ptr);
			}
			/* new channel */
			ast_cdr_specialized_reset(new_chan_cdr, 0);
		} else {
			ast_cdr_specialized_reset(chan->cdr, 0); /* nothing changed, reset the chan cdr  */
		}
	}

	{
		struct ast_channel *chan_ptr = NULL;
		new_peer_cdr = pick_unlocked_cdr(peer->cdr); /* the proper chan cdr, if there are forked cdrs */
		if (new_chan_cdr && ast_test_flag(new_chan_cdr, AST_CDR_FLAG_POST_DISABLED) && new_peer_cdr && !ast_test_flag(new_peer_cdr, AST_CDR_FLAG_POST_DISABLED))
			ast_set_flag(new_peer_cdr, AST_CDR_FLAG_POST_DISABLED); /* DISABLED is viral-- it will propagate across a bridge */
		if (strcasecmp(orig_peername, peer->name) != 0) { 
			/* old channel */
			if ((chan_ptr = ast_channel_get_by_name(orig_peername))) {
				ast_channel_lock(chan_ptr);
				if (!ast_bridged_channel(chan_ptr)) {
					struct ast_cdr *cur;
					for (cur = chan_ptr->cdr; cur; cur = cur->next) {
						if (cur == peer_cdr) {
							break;
						}
					}
					if (cur) {
						ast_cdr_specialized_reset(peer_cdr, 0);
					}
				}
				ast_channel_unlock(chan_ptr);
				chan_ptr = ast_channel_unref(chan_ptr);
			}
			/* new channel */
			if (new_peer_cdr) {
				ast_cdr_specialized_reset(new_peer_cdr, 0);
			}
		} else {
			if (we_disabled_peer_cdr) {
				ast_clear_flag(peer->cdr, AST_CDR_FLAG_POST_DISABLED);
			}
			ast_cdr_specialized_reset(peer->cdr, 0); /* nothing changed, reset the peer cdr  */
		}
	}
	
	return res;
}

/*! \brief Output parking event to manager */
static void post_manager_event(const char *s, struct parkeduser *pu)
{
	manager_event(EVENT_FLAG_CALL, s,
		"Exten: %s\r\n"
		"Channel: %s\r\n"
		"Parkinglot: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"UniqueID: %s\r\n",
		pu->parkingexten, 
		pu->chan->name,
		pu->parkinglot->name,
		S_COR(pu->chan->caller.id.number.valid, pu->chan->caller.id.number.str, "<unknown>"),
		S_COR(pu->chan->caller.id.name.valid, pu->chan->caller.id.name.str, "<unknown>"),
		pu->chan->uniqueid
		);
}

static char *callback_dialoptions(struct ast_flags *features_callee, struct ast_flags *features_caller, char *options, size_t len)
{
	int i = 0;
	enum {
		OPT_CALLEE_REDIRECT   = 't',
		OPT_CALLER_REDIRECT   = 'T',
		OPT_CALLEE_AUTOMON    = 'w',
		OPT_CALLER_AUTOMON    = 'W',
		OPT_CALLEE_DISCONNECT = 'h',
		OPT_CALLER_DISCONNECT = 'H',
		OPT_CALLEE_PARKCALL   = 'k',
		OPT_CALLER_PARKCALL   = 'K',
	};

	memset(options, 0, len);
	if (ast_test_flag(features_caller, AST_FEATURE_REDIRECT) && i < len) {
		options[i++] = OPT_CALLER_REDIRECT;
	}
	if (ast_test_flag(features_caller, AST_FEATURE_AUTOMON) && i < len) {
		options[i++] = OPT_CALLER_AUTOMON;
	}
	if (ast_test_flag(features_caller, AST_FEATURE_DISCONNECT) && i < len) {
		options[i++] = OPT_CALLER_DISCONNECT;
	}
	if (ast_test_flag(features_caller, AST_FEATURE_PARKCALL) && i < len) {
		options[i++] = OPT_CALLER_PARKCALL;
	}

	if (ast_test_flag(features_callee, AST_FEATURE_REDIRECT) && i < len) {
		options[i++] = OPT_CALLEE_REDIRECT;
	}
	if (ast_test_flag(features_callee, AST_FEATURE_AUTOMON) && i < len) {
		options[i++] = OPT_CALLEE_AUTOMON;
	}
	if (ast_test_flag(features_callee, AST_FEATURE_DISCONNECT) && i < len) {
		options[i++] = OPT_CALLEE_DISCONNECT;
	}
	if (ast_test_flag(features_callee, AST_FEATURE_PARKCALL) && i < len) {
		options[i++] = OPT_CALLEE_PARKCALL;
	}

	return options;
}

/*! \brief Run management on parkinglots, called once per parkinglot */
int manage_parkinglot(struct ast_parkinglot *curlot, const struct pollfd *pfds, const int nfds, struct pollfd **new_pfds, int *new_nfds, int *ms)
{
	struct parkeduser *pu;
	int res = 0;
	char parkingslot[AST_MAX_EXTENSION];

	/* Lock parking list */
	AST_LIST_LOCK(&curlot->parkings);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&curlot->parkings, pu, list) {
		struct ast_channel *chan = pu->chan;	/* shorthand */
		int tms;        /* timeout for this item */
		int x;          /* fd index in channel */
		struct ast_context *con;

		if (pu->notquiteyet) { /* Pretend this one isn't here yet */
			continue;
		}
		tms = ast_tvdiff_ms(ast_tvnow(), pu->start);
		if (tms > pu->parkingtime) {
			/* Stop music on hold */
			ast_indicate(pu->chan, AST_CONTROL_UNHOLD);
			/* Get chan, exten from derived kludge */
			if (pu->peername[0]) {
				char *peername = ast_strdupa(pu->peername);
				char *dash = strrchr(peername, '-');
				char *peername_flat; /* using something like DAHDI/52 for an extension name is NOT a good idea */
				int i;

				if (dash) {
					*dash = '\0';
				}

				peername_flat = ast_strdupa(peername);
				for (i = 0; peername_flat[i]; i++) {
					if (peername_flat[i] == '/') {
						peername_flat[i]= '_';
					}
				}

				con = ast_context_find_or_create(NULL, NULL, pu->parkinglot->parking_con_dial, registrar);
				if (!con) {
					ast_log(LOG_ERROR, "Parking dial context '%s' does not exist and unable to create\n", pu->parkinglot->parking_con_dial);
				} else {
					char returnexten[AST_MAX_EXTENSION];
					struct ast_datastore *features_datastore;
					struct ast_dial_features *dialfeatures = NULL;

					ast_channel_lock(chan);

					if ((features_datastore = ast_channel_datastore_find(chan, &dial_features_info, NULL)))
						dialfeatures = features_datastore->data;

					ast_channel_unlock(chan);

					if (!strncmp(peername, "Parked/", 7)) {
						peername += 7;
					}

					if (dialfeatures) {
						char buf[MAX_DIAL_FEATURE_OPTIONS] = {0,};
						snprintf(returnexten, sizeof(returnexten), "%s,30,%s", peername, callback_dialoptions(&(dialfeatures->features_callee), &(dialfeatures->features_caller), buf, sizeof(buf)));
					} else { /* Existing default */
						ast_log(LOG_NOTICE, "Dial features not found on %s, using default!\n", chan->name);
						snprintf(returnexten, sizeof(returnexten), "%s,30,t", peername);
					}

					ast_add_extension2(con, 1, peername_flat, 1, NULL, NULL, "Dial", ast_strdup(returnexten), ast_free_ptr, registrar);
				}
				if (pu->options_specified == 1) {
					/* Park() was called with overriding return arguments, respect those arguments */
					set_c_e_p(chan, pu->context, pu->exten, pu->priority);
				} else {
					if (comebacktoorigin) {
						set_c_e_p(chan, pu->parkinglot->parking_con_dial, peername_flat, 1);
					} else {
						snprintf(parkingslot, sizeof(parkingslot), "%d", pu->parkingnum);
						pbx_builtin_setvar_helper(chan, "PARKINGSLOT", parkingslot);
						set_c_e_p(chan, "parkedcallstimeout", peername_flat, 1);
					}
				}
			} else {
				/* They've been waiting too long, send them back to where they came.  Theoretically they
				   should have their original extensions and such, but we copy to be on the safe side */
				set_c_e_p(chan, pu->context, pu->exten, pu->priority);
			}
			post_manager_event("ParkedCallTimeOut", pu);
			ast_cel_report_event(pu->chan, AST_CEL_PARK_END, NULL, "ParkedCallTimeOut", NULL);

			ast_verb(2, "Timeout for %s parked on %d (%s). Returning to %s,%s,%d\n", pu->chan->name, pu->parkingnum, pu->parkinglot->name, pu->chan->context, pu->chan->exten, pu->chan->priority);
			/* Start up the PBX, or hang them up */
			if (ast_pbx_start(chan))  {
				ast_log(LOG_WARNING, "Unable to restart the PBX for user on '%s', hanging them up...\n", pu->chan->name);
				ast_hangup(chan);
			}
			/* And take them out of the parking lot */
			con = ast_context_find(pu->parkinglot->parking_con);
			if (con) {
				if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0))
					ast_log(LOG_WARNING, "Whoa, failed to remove the parking extension!\n");
				else
					notify_metermaids(pu->parkingexten, curlot->parking_con, AST_DEVICE_NOT_INUSE);
			} else
				ast_log(LOG_WARNING, "Whoa, no parking context?\n");
			AST_LIST_REMOVE_CURRENT(list);
			free(pu);
		} else {	/* still within parking time, process descriptors */
			for (x = 0; x < AST_MAX_FDS; x++) {
				struct ast_frame *f;
				int y;

				if (chan->fds[x] == -1) {
					continue;	/* nothing on this descriptor */
				}

				for (y = 0; y < nfds; y++) {
					if (pfds[y].fd == chan->fds[x]) {
						/* Found poll record! */
						break;
					}
				}
				if (y == nfds) {
					/* Not found */
					continue;
				}

				if (!(pfds[y].revents & (POLLIN | POLLERR | POLLPRI))) {
					/* Next x */
					continue;
				}

				if (pfds[y].revents & POLLPRI) {
					ast_set_flag(chan, AST_FLAG_EXCEPTION);
				} else {
					ast_clear_flag(chan, AST_FLAG_EXCEPTION);
				}
				chan->fdno = x;

				/* See if they need servicing */
				f = ast_read(pu->chan);
				/* Hangup? */
				if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer ==  AST_CONTROL_HANGUP))) {
					if (f)
						ast_frfree(f);
					post_manager_event("ParkedCallGiveUp", pu);
					ast_cel_report_event(pu->chan, AST_CEL_PARK_END, NULL, "ParkedCallGiveUp", NULL);

					/* There's a problem, hang them up*/
					ast_verb(2, "%s got tired of being parked\n", chan->name);
					ast_hangup(chan);
					/* And take them out of the parking lot */
					con = ast_context_find(curlot->parking_con);
					if (con) {
						if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0))
							ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
						else
							notify_metermaids(pu->parkingexten, curlot->parking_con, AST_DEVICE_NOT_INUSE);
					} else
						ast_log(LOG_WARNING, "Whoa, no parking context for parking lot %s?\n", curlot->name);
					AST_LIST_REMOVE_CURRENT(list);
					parkinglot_unref(pu->parkinglot);
					free(pu);
					break;
				} else {
					/* XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
					ast_frfree(f);
					if (pu->moh_trys < 3 && !chan->generatordata) {
						ast_debug(1, "MOH on parked call stopped by outside source.  Restarting on channel %s.\n", chan->name);
						ast_indicate_data(chan, AST_CONTROL_HOLD, 
							S_OR(curlot->mohclass, NULL),
							(!ast_strlen_zero(curlot->mohclass) ? strlen(curlot->mohclass) + 1 : 0));
						pu->moh_trys++;
					}
					goto std;	/* XXX Ick: jumping into an else statement??? XXX */
				}
			} /* End for */
			if (x >= AST_MAX_FDS) {
std:			for (x = 0; x < AST_MAX_FDS; x++) {	/* mark fds for next round */
					if (chan->fds[x] > -1) {
						void *tmp = ast_realloc(*new_pfds, (*new_nfds + 1) * sizeof(struct pollfd));
						if (!tmp) {
							continue;
						}
						*new_pfds = tmp;
						(*new_pfds)[*new_nfds].fd = chan->fds[x];
						(*new_pfds)[*new_nfds].events = POLLIN | POLLERR | POLLPRI;
						(*new_pfds)[*new_nfds].revents = 0;
						(*new_nfds)++;
					}
				}
				/* Keep track of our shortest wait */
				if (tms < *ms || *ms < 0) {
					*ms = tms;
				}
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&curlot->parkings);

	return res;
}

/*! 
 * \brief Take care of parked calls and unpark them if needed 
 * \param ignore unused var.
 * 
 * Start inf loop, lock parking lot, check if any parked channels have gone above timeout
 * if so, remove channel from parking lot and return it to the extension that parked it.
 * Check if parked channel decided to hangup, wait until next FD via select().
*/
static void *do_parking_thread(void *ignore)
{
	struct pollfd *pfds = NULL, *new_pfds = NULL;
	int nfds = 0, new_nfds = 0;

	for (;;) {
		struct ao2_iterator iter;
		struct ast_parkinglot *curlot;
		int ms = -1;	/* poll2 timeout, uninitialized */
		iter = ao2_iterator_init(parkinglots, 0);

		while ((curlot = ao2_iterator_next(&iter))) {
			manage_parkinglot(curlot, pfds, nfds, &new_pfds, &new_nfds, &ms);
			ao2_ref(curlot, -1);
		}
		ao2_iterator_destroy(&iter);

		/* Recycle */
		ast_free(pfds);
		pfds = new_pfds;
		nfds = new_nfds;
		new_pfds = NULL;
		new_nfds = 0;

		/* Wait for something to happen */
		ast_poll(pfds, nfds, ms);
		pthread_testcancel();
	}
	/* If this WERE reached, we'd need to free(pfds) */
	return NULL;	/* Never reached */
}

/*! \brief Find parkinglot by name */
struct ast_parkinglot *find_parkinglot(const char *name)
{
	struct ast_parkinglot *parkinglot;

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	parkinglot = ao2_find(parkinglots, (void *) name, 0);
	if (parkinglot) {
		ast_debug(1, "Found Parkinglot: %s\n", parkinglot->name);
	}

	return parkinglot;
}

/*! \brief Copy parkinglot and store it with new name */
struct ast_parkinglot *copy_parkinglot(const char *name, const struct ast_parkinglot *parkinglot) {
	struct ast_parkinglot *copylot;

	if (ast_strlen_zero(name)) { /* No name specified */
		return NULL;
	}
	if ((copylot = find_parkinglot(name))) { /* Parkinglot with that name already exists */
		if (copylot) {
			ao2_ref(copylot, -1);
		}
		return NULL;
	}

	copylot = create_parkinglot(name);
	ast_debug(1, "Building parking lot %s\n", name);

	memcpy(copylot, parkinglot, sizeof(struct ast_parkinglot));
	ast_copy_string(copylot->name, name, sizeof(copylot->name));
	AST_LIST_HEAD_INIT(&copylot->parkings);

	return copylot;
}

AST_APP_OPTIONS(park_call_options, BEGIN_OPTIONS
	AST_APP_OPTION('r', AST_PARK_OPT_RINGING),
	AST_APP_OPTION('R', AST_PARK_OPT_RANDOMIZE),
	AST_APP_OPTION('s', AST_PARK_OPT_SILENCE),
END_OPTIONS );

/*! \brief Park a call */
static int park_call_exec(struct ast_channel *chan, const char *data)
{
	/* Cache the original channel name in case we get masqueraded in the middle
	 * of a park--it is still theoretically possible for a transfer to happen before
	 * we get here, but it is _really_ unlikely */
	char *orig_chan_name = ast_strdupa(chan->name);
	char orig_exten[AST_MAX_EXTENSION];
	int orig_priority = chan->priority;

	/* Data is unused at the moment but could contain a parking
	   lot context eventually */
	int res = 0;

	char *parse = NULL;
	AST_DECLARE_APP_ARGS(app_args,
		AST_APP_ARG(timeout);
		AST_APP_ARG(return_con);
		AST_APP_ARG(return_ext);
		AST_APP_ARG(return_pri);
		AST_APP_ARG(options);
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(app_args, parse);

	ast_copy_string(orig_exten, chan->exten, sizeof(orig_exten));

	/* Setup the exten/priority to be s/1 since we don't know
	   where this call should return */
	strcpy(chan->exten, "s");
	chan->priority = 1;

	/* Answer if call is not up */
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);

	/* Sleep to allow VoIP streams to settle down */
	if (!res)
		res = ast_safe_sleep(chan, 1000);

	/* Park the call */
	if (!res) {
		struct ast_park_call_args args = {
			.orig_chan_name = orig_chan_name,
		};
		struct ast_flags flags = { 0 };

		if (parse) {
			if (!ast_strlen_zero(app_args.timeout)) {
				if (sscanf(app_args.timeout, "%30d", &args.timeout) != 1) {
					ast_log(LOG_WARNING, "Invalid timeout '%s' provided\n", app_args.timeout);
					args.timeout = 0;
				}
			}
			if (!ast_strlen_zero(app_args.return_con)) {
				args.return_con = app_args.return_con;
			}
			if (!ast_strlen_zero(app_args.return_ext)) {
				args.return_ext = app_args.return_ext;
			}
			if (!ast_strlen_zero(app_args.return_pri)) {
				if (sscanf(app_args.return_pri, "%30d", &args.return_pri) != 1) {
					ast_log(LOG_WARNING, "Invalid priority '%s' specified\n", app_args.return_pri);
					args.return_pri = 0;
				}
			}
		}

		ast_app_parse_options(park_call_options, &flags, NULL, app_args.options);
		args.flags = flags.flags;

		args.parkinglot = ao2_callback(parkinglots, 0, find_parkinglot_by_exten_cb, &orig_exten);
		res = masq_park_call_announce(chan, chan, &args);
		/* Continue on in the dialplan */
		if (res == 1) {
			ast_copy_string(chan->exten, orig_exten, sizeof(chan->exten));
			chan->priority = orig_priority;
			res = 0;
		} else if (!res) {
			res = 1;
		}
	}

	return res;
}

/*! \brief Pickup parked call */
static int park_exec_full(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_channel *peer=NULL;
	struct parkeduser *pu;
	struct ast_context *con;
	int park = 0;
	struct ast_bridge_config config;
	struct ast_parkinglot *parkinglot;

	if (data) {
		park = atoi((char *) data);
	}

	parkinglot = ao2_callback(parkinglots, 0, find_parkinglot_by_position_cb, (void *) &park);
	if (!parkinglot)
		parkinglot = default_parkinglot;

	AST_LIST_LOCK(&parkinglot->parkings);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&parkinglot->parkings, pu, list) {
		if (!pu->notquiteyet && (!data || pu->parkingnum == park)) {
			if (pu->chan->pbx) { /* do not allow call to be picked up until the PBX thread is finished */
				AST_LIST_UNLOCK(&parkinglot->parkings);
				return -1;
			}
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&parkinglot->parkings);

	if (pu) {
		peer = pu->chan;
		con = ast_context_find(parkinglot->parking_con);
		if (con) {
			if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0))
				ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
			else
				notify_metermaids(pu->parkingexten, parkinglot->parking_con, AST_DEVICE_NOT_INUSE);
		} else
			ast_log(LOG_WARNING, "Whoa, no parking context?\n");

		ast_cel_report_event(pu->chan, AST_CEL_PARK_END, NULL, "UnParkedCall", chan);
		ast_manager_event(pu->chan, EVENT_FLAG_CALL, "UnParkedCall",
			"Exten: %s\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n",
			pu->parkingexten, pu->chan->name, chan->name,
			S_COR(pu->chan->caller.id.number.valid, pu->chan->caller.id.number.str, "<unknown>"),
			S_COR(pu->chan->caller.id.name.valid, pu->chan->caller.id.name.str, "<unknown>")
			);

		ast_free(pu);
	}
	/* JK02: it helps to answer the channel if not already up */
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	//XXX Why do we unlock here ?
	// uncomment it for now, till my setup with debug_threads and detect_deadlocks starts to complain
	//ASTOBJ_UNLOCK(parkinglot);

	if (peer) {
		struct ast_datastore *features_datastore;
		struct ast_dial_features *dialfeatures = NULL;

		/* Play a courtesy to the source(s) configured to prefix the bridge connecting */

		if (!ast_strlen_zero(courtesytone)) {
			int error = 0;
			ast_indicate(peer, AST_CONTROL_UNHOLD);
			if (parkedplay == 0) {
				error = ast_stream_and_wait(chan, courtesytone, "");
			} else if (parkedplay == 1) {
				error = ast_stream_and_wait(peer, courtesytone, "");
			} else if (parkedplay == 2) {
				if (!ast_streamfile(chan, courtesytone, chan->language) &&
						!ast_streamfile(peer, courtesytone, chan->language)) {
					/*! \todo XXX we would like to wait on both! */
					res = ast_waitstream(chan, "");
					if (res >= 0)
						res = ast_waitstream(peer, "");
					if (res < 0)
						error = 1;
				}
			}
			if (error) {
				ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
				ast_hangup(peer);
				return -1;
			}
		} else
			ast_indicate(peer, AST_CONTROL_UNHOLD);

		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		/* This runs sorta backwards, since we give the incoming channel control, as if it
		   were the person called. */
		ast_verb(3, "Channel %s connected to parked call %d\n", chan->name, park);

		pbx_builtin_setvar_helper(chan, "PARKEDCHANNEL", peer->name);
		ast_cdr_setdestchan(chan->cdr, peer->name);
		memset(&config, 0, sizeof(struct ast_bridge_config));

		/* Get datastore for peer and apply it's features to the callee side of the bridge config */
		ast_channel_lock(peer);
		if ((features_datastore = ast_channel_datastore_find(peer, &dial_features_info, NULL))) {
			dialfeatures = features_datastore->data;
		}
		ast_channel_unlock(peer);

		/* When the datastores for both caller and callee are created, both the callee and caller channels
		 * use the features_caller flag variable to represent themselves. With that said, the config.features_callee
		 * flags should be copied from the datastore's caller feature flags regardless if peer was a callee
		 * or caller. */
		if (dialfeatures) {
			ast_copy_flags(&(config.features_callee), &(dialfeatures->features_caller), AST_FLAGS_ALL);
		}

		if ((parkinglot->parkedcalltransfers == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->parkedcalltransfers == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
		}
		if ((parkinglot->parkedcalltransfers == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->parkedcalltransfers == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
		}
		if ((parkinglot->parkedcallreparking == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->parkedcallreparking == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_PARKCALL);
		}
		if ((parkinglot->parkedcallreparking == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->parkedcallreparking == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_PARKCALL);
		}
		if ((parkinglot->parkedcallhangup == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->parkedcallhangup == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_DISCONNECT);
		}
		if ((parkinglot->parkedcallhangup == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->parkedcallhangup == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_DISCONNECT);
		}
		if ((parkinglot->parkedcallrecording == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->parkedcallrecording == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
		}
		if ((parkinglot->parkedcallrecording == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->parkedcallrecording == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);
		}

		parkinglot_unref(parkinglot);
		res = ast_bridge_call(chan, peer, &config);

		pbx_builtin_setvar_helper(chan, "PARKEDCHANNEL", peer->name);
		ast_cdr_setdestchan(chan->cdr, peer->name);

		/* Simulate the PBX hanging up */
		ast_hangup(peer);
		return -1;
	} else {
		/*! \todo XXX Play a message XXX */
		if (ast_stream_and_wait(chan, "pbx-invalidpark", ""))
			ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", "pbx-invalidpark", chan->name);
		ast_verb(3, "Channel %s tried to talk to nonexistent parked call %d\n", chan->name, park);
		res = -1;
	}

	return -1;
}

static int park_exec(struct ast_channel *chan, const char *data) 
{
	return park_exec_full(chan, data);
}

/*! \brief Unreference parkinglot object. If no more references,
	then go ahead and delete it */
static void parkinglot_unref(struct ast_parkinglot *parkinglot) 
{
	int refcount = ao2_ref(parkinglot, -1);
	ast_debug(3, "Multiparking: %s refcount now %d\n", parkinglot->name, refcount - 1);
}

static struct ast_parkinglot *parkinglot_addref(struct ast_parkinglot *parkinglot)
{
	int refcount = ao2_ref(parkinglot, +1);
	ast_debug(3, "Multiparking: %s refcount now %d\n", parkinglot->name, refcount + 1);
	return parkinglot;
}

/*! \brief Allocate parking lot structure */
static struct ast_parkinglot *create_parkinglot(const char *name)
{
	struct ast_parkinglot *newlot = (struct ast_parkinglot *) NULL;

	if (!name)
		return NULL;

	newlot = ao2_alloc(sizeof(*newlot), parkinglot_destroy);
	if (!newlot)
		return NULL;
	
	ast_copy_string(newlot->name, name, sizeof(newlot->name));
	AST_LIST_HEAD_INIT(&newlot->parkings);

	return newlot;
}

/*! \brief Destroy a parking lot */
static void parkinglot_destroy(void *obj)
{
	struct ast_parkinglot *ruin = obj;
	struct ast_context *con;
	con = ast_context_find(ruin->parking_con);
	if (con)
		ast_context_destroy(con, registrar);
}

/*! 
 * \brief Add parking hints for all defined parking lots 
 * \param context
 * \param start starting parkinglot number
 * \param stop ending parkinglot number
*/
static void park_add_hints(char *context, int start, int stop)
{
	int numext;
	char device[AST_MAX_EXTENSION];
	char exten[10];

	for (numext = start; numext <= stop; numext++) {
		snprintf(exten, sizeof(exten), "%d", numext);
		snprintf(device, sizeof(device), "park:%s@%s", exten, context);
		ast_add_extension(context, 1, exten, PRIORITY_HINT, NULL, NULL, device, NULL, NULL, registrar);
	}
}

/*! \brief Build parkinglot from configuration and chain it in if it doesn't already exist */
static struct ast_parkinglot *build_parkinglot(char *name, struct ast_variable *var)
{
	struct ast_parkinglot *parkinglot;
	struct ast_context *con = NULL;

	struct ast_variable *confvar = var;
	int error = 0;
	int start = 0, end = 0;
	int oldparkinglot = 0;

	parkinglot = find_parkinglot(name);
	if (parkinglot)
		oldparkinglot = 1;
	else
		parkinglot = create_parkinglot(name);

	if (!parkinglot)
		return NULL;

	ao2_lock(parkinglot);

	ast_debug(1, "Building parking lot %s\n", name);

	/* Do some config stuff */
	while(confvar) {
		if (!strcasecmp(confvar->name, "context")) {
			ast_copy_string(parkinglot->parking_con, confvar->value, sizeof(parkinglot->parking_con));
		} else if (!strcasecmp(confvar->name, "parkext")) {
			ast_copy_string(parkinglot->parkext, confvar->value, sizeof(parkinglot->parkext));
		} else if (!strcasecmp(confvar->name, "parkingtime")) {
			if ((sscanf(confvar->value, "%30d", &parkinglot->parkingtime) != 1) || (parkinglot->parkingtime < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid parkingtime\n", confvar->value);
				parkinglot->parkingtime = DEFAULT_PARK_TIME;
			} else
				parkinglot->parkingtime = parkinglot->parkingtime * 1000;
		} else if (!strcasecmp(confvar->name, "parkpos")) {
			if (sscanf(confvar->value, "%30d-%30d", &start, &end) != 2) {
				ast_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers at line %d of parking.conf\n", confvar->lineno);
				error = 1;
			} else {
				parkinglot->parking_start = start;
				parkinglot->parking_stop = end;
			}
		} else if (!strcasecmp(confvar->name, "findslot")) {
			parkinglot->parkfindnext = (!strcasecmp(confvar->value, "next"));
		} else if (!strcasecmp(confvar->name, "parkedcalltransfers")) {
			ast_debug(1, "Setting parking lot %s %s to %s\n", name, confvar->name, confvar->value);
			if (!strcasecmp(confvar->value, "both")) {
				parkinglot->parkedcalltransfers = AST_FEATURE_FLAG_BYBOTH;
			} else if (!strcasecmp(confvar->value, "caller")) {
				parkinglot->parkedcalltransfers = AST_FEATURE_FLAG_BYCALLER;
		        } else if (!strcasecmp(confvar->value, "callee")) {
				parkinglot->parkedcalltransfers = AST_FEATURE_FLAG_BYCALLEE;
			}
		} else if (!strcasecmp(confvar->name, "parkedcallreparking")) {
			ast_debug(1, "Setting parking lot %s %s to %s\n", name, confvar->name, confvar->value);
			if (!strcasecmp(confvar->value, "both")) {
				parkinglot->parkedcallreparking = AST_FEATURE_FLAG_BYBOTH;
			} else if (!strcasecmp(confvar->value, "caller")) {
				parkinglot->parkedcallreparking = AST_FEATURE_FLAG_BYCALLER;
			} else if (!strcasecmp(confvar->value, "callee")) {
				parkinglot->parkedcallreparking = AST_FEATURE_FLAG_BYCALLEE;
			}
		} else if (!strcasecmp(confvar->name, "parkedcallhangup")) {
			ast_debug(1, "Setting parking lot %s %s to %s\n", name, confvar->name, confvar->value);
			if (!strcasecmp(confvar->value, "both")) {
				parkinglot->parkedcallhangup = AST_FEATURE_FLAG_BYBOTH;
			} else if (!strcasecmp(confvar->value, "caller")) {
				parkinglot->parkedcallhangup = AST_FEATURE_FLAG_BYCALLER;
			} else if (!strcasecmp(confvar->value, "callee")) {
				parkinglot->parkedcallhangup = AST_FEATURE_FLAG_BYCALLEE;
			}
		} else if (!strcasecmp(confvar->name, "parkedcallrecording")) {
			ast_debug(1, "Setting parking lot %s %s to %s\n", name, confvar->name, confvar->value);
			if (!strcasecmp(confvar->value, "both")) {
				parkinglot->parkedcallrecording = AST_FEATURE_FLAG_BYBOTH;
			} else if (!strcasecmp(confvar->value, "caller")) {
				parkinglot->parkedcallrecording = AST_FEATURE_FLAG_BYCALLER;
			} else if (!strcasecmp(confvar->value, "callee")) {
				parkinglot->parkedcallrecording = AST_FEATURE_FLAG_BYCALLEE;
			}
		}
		confvar = confvar->next;
	}
	/* make sure parkingtime is set if not specified */
	if (parkinglot->parkingtime == 0) {
		parkinglot->parkingtime = DEFAULT_PARK_TIME;
	}
	if (ast_strlen_zero(parkinglot->parkext)) {
		ast_debug(2, "no parkext specified for %s - setting it to %s\n", parkinglot->name, DEFAULT_PARK_EXTENSION);
		ast_copy_string(parkinglot->parkext, DEFAULT_PARK_EXTENSION, sizeof(parkinglot->parkext));
	}

	if (!var) {	/* Default parking lot */
		ast_copy_string(parkinglot->parking_con, "parkedcalls", sizeof(parkinglot->parking_con));
		ast_copy_string(parkinglot->parking_con_dial, "park-dial", sizeof(parkinglot->parking_con_dial));
		ast_copy_string(parkinglot->mohclass, "default", sizeof(parkinglot->mohclass));
	}

	/* Check for errors */
	if (ast_strlen_zero(parkinglot->parking_con)) {
		ast_log(LOG_WARNING, "Parking lot %s lacks context\n", name);
		error = 1;
	}

	/* Create context */
	if (!error && !(con = ast_context_find_or_create(NULL, NULL, parkinglot->parking_con, registrar))) {
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parkinglot->parking_con);
		error = 1;
	}

	/* Add a parking extension into the context */
	if (!error && !oldparkinglot) {
		if (!ast_strlen_zero(parkinglot->parkext)) {
			if (ast_add_extension2(con, 1, parkinglot->parkext, 1, NULL, NULL, parkcall, strdup(""), ast_free_ptr, registrar) == -1)
				error = 1;
		}
	}

	/* Add parking hints */
	if (parkinglot->parkaddhints)
		park_add_hints(parkinglot->parking_con, parkinglot->parking_start, parkinglot->parking_stop);

	ao2_unlock(parkinglot);

	if (error) {
		ast_log(LOG_WARNING, "Parking %s not open for business. Configuration error.\n", name);
		parkinglot_destroy(parkinglot);
		parkinglot_unref(parkinglot);
		return NULL;
	}
	ast_debug(1, "Parking %s now open for business. (start exten %d end %d)\n", name, start, end);
	parkinglot->the_mark = 0;

	/* Move it into the list, if it wasn't already there */
	if (!oldparkinglot) {
		ao2_link(parkinglots, parkinglot);
	}
	parkinglot_unref(parkinglot);

	return parkinglot;
}

static int load_config(void)
{
	int start = 0, end = 0;
	int res;
	int i;
	struct ast_context *con = NULL;
	struct ast_config *cfg = NULL;
	struct ast_variable *var = NULL;
	struct feature_group *fg = NULL;
	struct ast_flags config_flags = { 0 };
	char *ctg; 
	static const char * const categories[] = { 
		/* Categories in features.conf that are not
		 * to be parsed as group categories
		 */
		"general",
		"featuremap",
		"applicationmap"
	};

	default_parkinglot = build_parkinglot(DEFAULT_PARKINGLOT, NULL);
	if (default_parkinglot) {
		ao2_lock(default_parkinglot);
		ast_copy_string(default_parkinglot->parkext, DEFAULT_PARK_EXTENSION, sizeof(default_parkinglot->parkext));
		default_parkinglot->parking_start = 701;
		default_parkinglot->parking_stop = 750;
		default_parkinglot->parking_offset = 0;
		default_parkinglot->parkfindnext = 0;
		default_parkinglot->parkingtime = DEFAULT_PARK_TIME;
		ao2_unlock(default_parkinglot);
	}

	if (default_parkinglot) {
		ast_debug(1, "Configuration of default parkinglot done.\n");
	} else {
		ast_log(LOG_ERROR, "Configuration of default parkinglot failed.\n");
		return -1;
	}

	/* Reset to defaults */
	strcpy(default_parkinglot->parkext, DEFAULT_PARK_EXTENSION);
	strcpy(pickup_ext, "*8");
	courtesytone[0] = '\0';
	strcpy(xfersound, "beep");
	strcpy(xferfailsound, "beeperr");
	pickupsound[0] = '\0';
	pickupfailsound[0] = '\0';
	adsipark = 0;
	comebacktoorigin = 1;
	parkeddynamic = 0;

	default_parkinglot->parkaddhints = 0;
	default_parkinglot->parkedcalltransfers = 0;
	default_parkinglot->parkedcallreparking = 0;
	default_parkinglot->parkedcallrecording = 0;
	default_parkinglot->parkedcallhangup = 0;

	transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
	featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
	atxfernoanswertimeout = DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER;
	atxferloopdelay = DEFAULT_ATXFER_LOOP_DELAY;
	atxferdropcall = DEFAULT_ATXFER_DROP_CALL;
	atxfercallbackretries = DEFAULT_ATXFER_CALLBACK_RETRIES;

	cfg = ast_config_load2("features.conf", "features", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING,"Could not load features.conf\n");
		return 0;
	}
	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		if (!strcasecmp(var->name, "parkext")) {
			ast_copy_string(default_parkinglot->parkext, var->value, sizeof(default_parkinglot->parkext));
		} else if (!strcasecmp(var->name, "context")) {
			ast_copy_string(default_parkinglot->parking_con, var->value, sizeof(default_parkinglot->parking_con));
		} else if (!strcasecmp(var->name, "parkingtime")) {
			if ((sscanf(var->value, "%30d", &default_parkinglot->parkingtime) != 1) || (default_parkinglot->parkingtime < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid parkingtime\n", var->value);
				default_parkinglot->parkingtime = DEFAULT_PARK_TIME;
			} else
				default_parkinglot->parkingtime = default_parkinglot->parkingtime * 1000;
		} else if (!strcasecmp(var->name, "parkpos")) {
			if (sscanf(var->value, "%30d-%30d", &start, &end) != 2) {
				ast_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers at line %d of features.conf\n", var->lineno);
			} else if (default_parkinglot) {
				default_parkinglot->parking_start = start;
				default_parkinglot->parking_stop = end;
			} else {
				ast_log(LOG_WARNING, "No default parking lot!\n");
			}
		} else if (!strcasecmp(var->name, "findslot")) {
			default_parkinglot->parkfindnext = (!strcasecmp(var->value, "next"));
		} else if (!strcasecmp(var->name, "parkinghints")) {
			default_parkinglot->parkaddhints = ast_true(var->value);
		} else if (!strcasecmp(var->name, "parkedcalltransfers")) {
			if (!strcasecmp(var->value, "both"))
				default_parkinglot->parkedcalltransfers = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				default_parkinglot->parkedcalltransfers = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				default_parkinglot->parkedcalltransfers = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "parkedcallreparking")) {
			if (!strcasecmp(var->value, "both"))
				default_parkinglot->parkedcallreparking = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				default_parkinglot->parkedcallreparking = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				default_parkinglot->parkedcallreparking = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "parkedcallhangup")) {
			if (!strcasecmp(var->value, "both"))
				default_parkinglot->parkedcallhangup = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				default_parkinglot->parkedcallhangup = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				default_parkinglot->parkedcallhangup = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "parkedcallrecording")) {
			if (!strcasecmp(var->value, "both"))
				default_parkinglot->parkedcallrecording = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				default_parkinglot->parkedcallrecording = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				default_parkinglot->parkedcallrecording = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "parkeddynamic")) {
			parkeddynamic = ast_true(var->value);
		} else if (!strcasecmp(var->name, "adsipark")) {
			adsipark = ast_true(var->value);
		} else if (!strcasecmp(var->name, "transferdigittimeout")) {
			if ((sscanf(var->value, "%30d", &transferdigittimeout) != 1) || (transferdigittimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid transferdigittimeout\n", var->value);
				transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
			} else
				transferdigittimeout = transferdigittimeout * 1000;
		} else if (!strcasecmp(var->name, "featuredigittimeout")) {
			if ((sscanf(var->value, "%30d", &featuredigittimeout) != 1) || (featuredigittimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid featuredigittimeout\n", var->value);
				featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
			}
		} else if (!strcasecmp(var->name, "atxfernoanswertimeout")) {
			if ((sscanf(var->value, "%30d", &atxfernoanswertimeout) != 1) || (atxfernoanswertimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxfernoanswertimeout\n", var->value);
				atxfernoanswertimeout = DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER;
			} else
				atxfernoanswertimeout = atxfernoanswertimeout * 1000;
		} else if (!strcasecmp(var->name, "atxferloopdelay")) {
			if ((sscanf(var->value, "%30u", &atxferloopdelay) != 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxferloopdelay\n", var->value);
				atxferloopdelay = DEFAULT_ATXFER_LOOP_DELAY;
			} else 
				atxferloopdelay *= 1000;
		} else if (!strcasecmp(var->name, "atxferdropcall")) {
			atxferdropcall = ast_true(var->value);
		} else if (!strcasecmp(var->name, "atxfercallbackretries")) {
			if ((sscanf(var->value, "%30u", &atxfercallbackretries) != 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxfercallbackretries\n", var->value);
				atxfercallbackretries = DEFAULT_ATXFER_CALLBACK_RETRIES;
			}
		} else if (!strcasecmp(var->name, "courtesytone")) {
			ast_copy_string(courtesytone, var->value, sizeof(courtesytone));
		}  else if (!strcasecmp(var->name, "parkedplay")) {
			if (!strcasecmp(var->value, "both"))
				parkedplay = 2;
			else if (!strcasecmp(var->value, "parked"))
				parkedplay = 1;
			else
				parkedplay = 0;
		} else if (!strcasecmp(var->name, "xfersound")) {
			ast_copy_string(xfersound, var->value, sizeof(xfersound));
		} else if (!strcasecmp(var->name, "xferfailsound")) {
			ast_copy_string(xferfailsound, var->value, sizeof(xferfailsound));
		} else if (!strcasecmp(var->name, "pickupexten")) {
			ast_copy_string(pickup_ext, var->value, sizeof(pickup_ext));
		} else if (!strcasecmp(var->name, "pickupsound")) {
			ast_copy_string(pickupsound, var->value, sizeof(pickupsound));
		} else if (!strcasecmp(var->name, "pickupfailsound")) {
			ast_copy_string(pickupfailsound, var->value, sizeof(pickupfailsound));
		} else if (!strcasecmp(var->name, "comebacktoorigin")) {
			comebacktoorigin = ast_true(var->value);
		} else if (!strcasecmp(var->name, "parkedmusicclass")) {
			ast_copy_string(default_parkinglot->mohclass, var->value, sizeof(default_parkinglot->mohclass));
		}
	}

	unmap_features();
	for (var = ast_variable_browse(cfg, "featuremap"); var; var = var->next) {
		if (remap_feature(var->name, var->value))
			ast_log(LOG_NOTICE, "Unknown feature '%s'\n", var->name);
	}

	/* Map a key combination to an application*/
	ast_unregister_features();
	for (var = ast_variable_browse(cfg, "applicationmap"); var; var = var->next) {
		char *tmp_val = ast_strdupa(var->value);
		char *activateon; 
		struct ast_call_feature *feature;
		AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(exten);
			AST_APP_ARG(activatedby);
			AST_APP_ARG(app);
			AST_APP_ARG(app_args);
			AST_APP_ARG(moh_class);
		);

		AST_STANDARD_APP_ARGS(args, tmp_val);
		if (strchr(args.app, '(')) {
			/* New syntax */
			args.moh_class = args.app_args;
			args.app_args = strchr(args.app, '(');
			*args.app_args++ = '\0';
			if (args.app_args[strlen(args.app_args) - 1] == ')') {
				args.app_args[strlen(args.app_args) - 1] = '\0';
			}
		}

		activateon = strsep(&args.activatedby, "/");	

		/*! \todo XXX var_name or app_args ? */
		if (ast_strlen_zero(args.app) || ast_strlen_zero(args.exten) || ast_strlen_zero(activateon) || ast_strlen_zero(var->name)) {
			ast_log(LOG_NOTICE, "Please check the feature Mapping Syntax, either extension, name, or app aren't provided %s %s %s %s\n",
				args.app, args.exten, activateon, var->name);
			continue;
		}

		AST_RWLIST_RDLOCK(&feature_list);
		if ((feature = find_dynamic_feature(var->name))) {
			AST_RWLIST_UNLOCK(&feature_list);
			ast_log(LOG_WARNING, "Dynamic Feature '%s' specified more than once!\n", var->name);
			continue;
		}
		AST_RWLIST_UNLOCK(&feature_list);
				
		if (!(feature = ast_calloc(1, sizeof(*feature)))) {
			continue;
		}

		ast_copy_string(feature->sname, var->name, FEATURE_SNAME_LEN);
		ast_copy_string(feature->app, args.app, FEATURE_APP_LEN);
		ast_copy_string(feature->exten, args.exten, FEATURE_EXTEN_LEN);
		
		if (args.app_args) {
			ast_copy_string(feature->app_args, args.app_args, FEATURE_APP_ARGS_LEN);
		}

		if (args.moh_class) {
			ast_copy_string(feature->moh_class, args.moh_class, FEATURE_MOH_LEN);
		}

		ast_copy_string(feature->exten, args.exten, sizeof(feature->exten));
		feature->operation = feature_exec_app;
		ast_set_flag(feature, AST_FEATURE_FLAG_NEEDSDTMF);

		/* Allow caller and calle to be specified for backwards compatability */
		if (!strcasecmp(activateon, "self") || !strcasecmp(activateon, "caller"))
			ast_set_flag(feature, AST_FEATURE_FLAG_ONSELF);
		else if (!strcasecmp(activateon, "peer") || !strcasecmp(activateon, "callee"))
			ast_set_flag(feature, AST_FEATURE_FLAG_ONPEER);
		else {
			ast_log(LOG_NOTICE, "Invalid 'ActivateOn' specification for feature '%s',"
				" must be 'self', or 'peer'\n", var->name);
			continue;
		}

		if (ast_strlen_zero(args.activatedby))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
		else if (!strcasecmp(args.activatedby, "caller"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLER);
		else if (!strcasecmp(args.activatedby, "callee"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLEE);
		else if (!strcasecmp(args.activatedby, "both"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
		else {
			ast_log(LOG_NOTICE, "Invalid 'ActivatedBy' specification for feature '%s',"
				" must be 'caller', or 'callee', or 'both'\n", var->name);
			continue;
		}

		ast_register_feature(feature);

		ast_verb(2, "Mapping Feature '%s' to app '%s(%s)' with code '%s'\n", var->name, args.app, args.app_args, args.exten);
	}

	ast_unregister_groups();
	AST_RWLIST_WRLOCK(&feature_groups);

	ctg = NULL;
	while ((ctg = ast_category_browse(cfg, ctg))) {
		/* Is this a parkinglot definition ? */
		if (!strncasecmp(ctg, "parkinglot_", strlen("parkinglot_"))) {
			ast_debug(2, "Found configuration section %s, assume parking context\n", ctg);
			if(!build_parkinglot(ctg, ast_variable_browse(cfg, ctg)))
				ast_log(LOG_ERROR, "Could not build parking lot %s. Configuration error.\n", ctg);
			else
				ast_debug(1, "Configured parking context %s\n", ctg);
			continue;
		}
		/* No, check if it's a group */
		for (i = 0; i < ARRAY_LEN(categories); i++) {
			if (!strcasecmp(categories[i], ctg))
				break;
		}

		if (i < ARRAY_LEN(categories))
			continue;

		if (!(fg = register_group(ctg)))
			continue;

		for (var = ast_variable_browse(cfg, ctg); var; var = var->next) {
			struct ast_call_feature *feature;

			AST_RWLIST_RDLOCK(&feature_list);
			if (!(feature = find_dynamic_feature(var->name)) && 
			    !(feature = ast_find_call_feature(var->name))) {
				AST_RWLIST_UNLOCK(&feature_list);
				ast_log(LOG_WARNING, "Feature '%s' was not found.\n", var->name);
				continue;
			}
			AST_RWLIST_UNLOCK(&feature_list);

			register_group_feature(fg, var->value, feature);
		}
	}

	AST_RWLIST_UNLOCK(&feature_groups);

	ast_config_destroy(cfg);

	if (!(con = ast_context_find_or_create(NULL, NULL, default_parkinglot->parking_con, registrar))) {
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", default_parkinglot->parking_con);
		return -1;
	}
	res = ast_add_extension2(con, 1, default_parkinglot->parkext, 1, NULL, NULL, parkcall, NULL, NULL, registrar);
	if (default_parkinglot->parkaddhints)
		park_add_hints(default_parkinglot->parking_con, default_parkinglot->parking_start, default_parkinglot->parking_stop);
	if (!res)
		notify_metermaids(default_parkinglot->parkext, default_parkinglot->parking_con, AST_DEVICE_INUSE); 
	return res;

}

/*!
 * \brief CLI command to list configured features
 * \param e
 * \param cmd
 * \param a
 *
 * \retval CLI_SUCCESS on success.
 * \retval NULL when tab completion is used.
 */
static char *handle_feature_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;
	struct ast_call_feature *feature;
	struct ao2_iterator iter;
	struct ast_parkinglot *curlot;
#define HFS_FORMAT "%-25s %-7s %-7s\n"

	switch (cmd) {
	
	case CLI_INIT:
		e->command = "features show";
		e->usage =
			"Usage: features show\n"
			"       Lists configured features\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, HFS_FORMAT, "Builtin Feature", "Default", "Current");
	ast_cli(a->fd, HFS_FORMAT, "---------------", "-------", "-------");

	ast_cli(a->fd, HFS_FORMAT, "Pickup", "*8", ast_pickup_ext());          /* default hardcoded above, so we'll hardcode it here */

	ast_rwlock_rdlock(&features_lock);
	for (i = 0; i < FEATURES_COUNT; i++)
		ast_cli(a->fd, HFS_FORMAT, builtin_features[i].fname, builtin_features[i].default_exten, builtin_features[i].exten);
	ast_rwlock_unlock(&features_lock);

	ast_cli(a->fd, "\n");
	ast_cli(a->fd, HFS_FORMAT, "Dynamic Feature", "Default", "Current");
	ast_cli(a->fd, HFS_FORMAT, "---------------", "-------", "-------");
	if (AST_RWLIST_EMPTY(&feature_list)) {
		ast_cli(a->fd, "(none)\n");
	} else {
		AST_RWLIST_RDLOCK(&feature_list);
		AST_RWLIST_TRAVERSE(&feature_list, feature, feature_entry) {
			ast_cli(a->fd, HFS_FORMAT, feature->sname, "no def", feature->exten);
		}
		AST_RWLIST_UNLOCK(&feature_list);
	}

	ast_cli(a->fd, "\nFeature Groups:\n");
	ast_cli(a->fd, "---------------\n");
	if (AST_RWLIST_EMPTY(&feature_groups)) {
		ast_cli(a->fd, "(none)\n");
	} else {
		struct feature_group *fg;
		struct feature_group_exten *fge;

		AST_RWLIST_RDLOCK(&feature_groups);
		AST_RWLIST_TRAVERSE(&feature_groups, fg, entry) {
			ast_cli(a->fd, "===> Group: %s\n", fg->gname);
			AST_LIST_TRAVERSE(&fg->features, fge, entry) {
				ast_cli(a->fd, "===> --> %s (%s)\n", fge->feature->sname, fge->exten);
			}
		}
		AST_RWLIST_UNLOCK(&feature_groups);
	}

	iter = ao2_iterator_init(parkinglots, 0);
	while ((curlot = ao2_iterator_next(&iter))) {
		ast_cli(a->fd, "\nCall parking (Parking lot: %s)\n", curlot->name);
		ast_cli(a->fd, "------------\n");
		ast_cli(a->fd,"%-22s:      %s\n", "Parking extension", curlot->parkext);
		ast_cli(a->fd,"%-22s:      %s\n", "Parking context", curlot->parking_con);
		ast_cli(a->fd,"%-22s:      %d-%d\n", "Parked call extensions", curlot->parking_start, curlot->parking_stop);
		ast_cli(a->fd,"%-22s:      %d\n", "Parkingtime", curlot->parkingtime);
		ast_cli(a->fd,"%-22s:      %s\n", "MusicOnHold class", curlot->mohclass);
		ast_cli(a->fd,"\n");
		ao2_ref(curlot, -1);
	}
	ao2_iterator_destroy(&iter);

	return CLI_SUCCESS;
}

static int parkinglot_markall_cb(void *obj, void *arg, int flags)
{
	struct ast_parkinglot *parkinglot = obj;
	parkinglot->the_mark = 1;
	return 0;
}

static int parkinglot_is_marked_cb(void *obj, void *arg, int flags)
{
	struct ast_parkinglot *parkinglot = obj;
	return parkinglot->the_mark ? CMP_MATCH : 0;
}

int ast_features_reload(void)
{
	int res;

	ao2_t_callback(parkinglots, OBJ_NODATA, parkinglot_markall_cb, NULL, "callback to mark all parkinglots");
	res = load_config(); /* Reload configuration */
	ao2_t_callback(parkinglots, OBJ_NODATA | OBJ_UNLINK, parkinglot_is_marked_cb, NULL, "callback to remove all marked parkinglots");
	
	return res;
}

static char *handle_features_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {	
	case CLI_INIT:
		e->command = "features reload";
		e->usage =
			"Usage: features reload\n"
			"       Reloads configured call features from features.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	ast_features_reload();

	return CLI_SUCCESS;
}

/*!
 * \brief Actual bridge
 * \param chan
 * \param tmpchan
 * 
 * Stop hold music, lock both channels, masq channels,
 * after bridge return channel to next priority.
*/
static void do_bridge_masquerade(struct ast_channel *chan, struct ast_channel *tmpchan)
{
	ast_moh_stop(chan);
	ast_channel_lock_both(chan, tmpchan);
	ast_setstate(tmpchan, chan->_state);
	tmpchan->readformat = chan->readformat;
	tmpchan->writeformat = chan->writeformat;
	ast_channel_masquerade(tmpchan, chan);
	ast_channel_unlock(chan);
	ast_channel_unlock(tmpchan);

	/* must be done without any channel locks held */
	ast_do_masquerade(tmpchan);

	/* when returning from bridge, the channel will continue at the next priority */
	ast_explicit_goto(tmpchan, chan->context, chan->exten, chan->priority + 1);
}

/*!
 * \brief Bridge channels together
 * \param s
 * \param m
 * 
 * Make sure valid channels were specified, 
 * send errors if any of the channels could not be found/locked, answer channels if needed,
 * create the placeholder channels and grab the other channels 
 * make the channels compatible, send error if we fail doing so 
 * setup the bridge thread object and start the bridge.
 * 
 * \retval 0 on success or on incorrect use.
 * \retval 1 on failure to bridge channels.
*/
static int action_bridge(struct mansession *s, const struct message *m)
{
	const char *channela = astman_get_header(m, "Channel1");
	const char *channelb = astman_get_header(m, "Channel2");
	const char *playtone = astman_get_header(m, "Tone");
	struct ast_channel *chana = NULL, *chanb = NULL, *chans[2];
	struct ast_channel *tmpchana = NULL, *tmpchanb = NULL;
	struct ast_bridge_thread_obj *tobj = NULL;

	/* make sure valid channels were specified */
	if (ast_strlen_zero(channela) || ast_strlen_zero(channelb)) {
		astman_send_error(s, m, "Missing channel parameter in request");
		return 0;
	}

	/* Start with chana */
	chana = ast_channel_get_by_name_prefix(channela, strlen(channela));

	/* send errors if any of the channels could not be found/locked */
	if (!chana) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Channel1 does not exists: %s", channela);
		astman_send_error(s, m, buf);
		return 0;
	}

	/* Answer the channels if needed */
	if (chana->_state != AST_STATE_UP)
		ast_answer(chana);

	/* create the placeholder channels and grab the other channels */
	if (!(tmpchana = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, 
		NULL, NULL, chana->linkedid, 0, "Bridge/%s", chana->name))) {
		astman_send_error(s, m, "Unable to create temporary channel!");
		chana = ast_channel_unref(chana);
		return 1;
	}

	do_bridge_masquerade(chana, tmpchana);

	chana = ast_channel_unref(chana);

	/* now do chanb */
	chanb = ast_channel_get_by_name_prefix(channelb, strlen(channelb));
	/* send errors if any of the channels could not be found/locked */
	if (!chanb) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Channel2 does not exists: %s", channelb);
		ast_hangup(tmpchana);
		astman_send_error(s, m, buf);
		return 0;
	}

	/* Answer the channels if needed */
	if (chanb->_state != AST_STATE_UP)
		ast_answer(chanb);

	/* create the placeholder channels and grab the other channels */
	if (!(tmpchanb = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, 
		NULL, NULL, chanb->linkedid, 0, "Bridge/%s", chanb->name))) {
		astman_send_error(s, m, "Unable to create temporary channels!");
		ast_hangup(tmpchana);
		chanb = ast_channel_unref(chanb);
		return 1;
	}

	do_bridge_masquerade(chanb, tmpchanb);

	chanb = ast_channel_unref(chanb);

	/* make the channels compatible, send error if we fail doing so */
	if (ast_channel_make_compatible(tmpchana, tmpchanb)) {
		ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for manager bridge\n", tmpchana->name, tmpchanb->name);
		astman_send_error(s, m, "Could not make channels compatible for manager bridge");
		ast_hangup(tmpchana);
		ast_hangup(tmpchanb);
		return 1;
	}

	/* setup the bridge thread object and start the bridge */
	if (!(tobj = ast_calloc(1, sizeof(*tobj)))) {
		ast_log(LOG_WARNING, "Unable to spawn a new bridge thread on %s and %s: %s\n", tmpchana->name, tmpchanb->name, strerror(errno));
		astman_send_error(s, m, "Unable to spawn a new bridge thread");
		ast_hangup(tmpchana);
		ast_hangup(tmpchanb);
		return 1;
	}

	tobj->chan = tmpchana;
	tobj->peer = tmpchanb;
	tobj->return_to_pbx = 1;

	if (ast_true(playtone)) {
		if (!ast_strlen_zero(xfersound) && !ast_streamfile(tmpchanb, xfersound, tmpchanb->language)) {
			if (ast_waitstream(tmpchanb, "") < 0)
				ast_log(LOG_WARNING, "Failed to play a courtesy tone on chan %s\n", tmpchanb->name);
		}
	}

	chans[0] = tmpchana;
	chans[1] = tmpchanb;

	ast_manager_event_multichan(EVENT_FLAG_CALL, "BridgeAction", 2, chans,
				"Response: Success\r\n"
				"Channel1: %s\r\n"
				"Channel2: %s\r\n", tmpchana->name, tmpchanb->name);

	bridge_call_thread_launch(tobj);

	astman_send_ack(s, m, "Launched bridge thread with success");

	return 0;
}

/*!
 * \brief CLI command to list parked calls
 * \param e 
 * \param cmd
 * \param a
 *  
 * Check right usage, lock parking lot, display parked calls, unlock parking lot list.
 * \retval CLI_SUCCESS on success.
 * \retval CLI_SHOWUSAGE on incorrect number of arguments.
 * \retval NULL when tab completion is used.
*/
static char *handle_parkedcalls(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct parkeduser *cur;
	int numparked = 0;
	struct ao2_iterator iter;
	struct ast_parkinglot *curlot;

	switch (cmd) {
	case CLI_INIT:
		e->command = "parkedcalls show";
		e->usage =
			"Usage: parkedcalls show\n"
			"       List currently parked calls\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "%4s %25s (%-15s %-12s %-4s) %-6s \n", "Num", "Channel"
		, "Context", "Extension", "Pri", "Timeout");

	iter = ao2_iterator_init(parkinglots, 0);
	while ((curlot = ao2_iterator_next(&iter))) {
		int lotparked = 0;
		/* subtract ref for iterator and for configured parking lot */
		ast_cli(a->fd, "*** Parking lot: %s (%d)\n", curlot->name, ao2_ref(curlot, 0) - 2);

		AST_LIST_LOCK(&curlot->parkings);
		AST_LIST_TRAVERSE(&curlot->parkings, cur, list) {
			ast_cli(a->fd, "%-10.10s %25s (%-15s %-12s %-4d) %6lds\n"
				,cur->parkingexten, cur->chan->name, cur->context, cur->exten
				,cur->priority,
				(long)(cur->start.tv_sec + (cur->parkingtime/1000) - time(NULL)) );
			numparked++;
			numparked += lotparked;
		}
		AST_LIST_UNLOCK(&curlot->parkings);
		if (lotparked)
			ast_cli(a->fd, "   %d parked call%s in parking lot %s\n", lotparked, ESS(lotparked), curlot->name);

		ao2_ref(curlot, -1);
	}

	ast_cli(a->fd, "---\n%d parked call%s in total.\n", numparked, ESS(numparked));

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_features[] = {
	AST_CLI_DEFINE(handle_feature_show, "Lists configured features"),
	AST_CLI_DEFINE(handle_features_reload, "Reloads configured features"),
	AST_CLI_DEFINE(handle_parkedcalls, "List currently parked calls"),
};

/*! 
 * \brief Dump parking lot status
 * \param s
 * \param m
 * 
 * Lock parking lot, iterate list and append parked calls status, unlock parking lot.
 * \return Always RESULT_SUCCESS 
*/
static int manager_parking_status(struct mansession *s, const struct message *m)
{
	struct parkeduser *cur;
	const char *id = astman_get_header(m, "ActionID");
	char idText[256] = "";
	struct ao2_iterator iter;
	struct ast_parkinglot *curlot;

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	astman_send_ack(s, m, "Parked calls will follow");

	iter = ao2_iterator_init(parkinglots, 0);
	while ((curlot = ao2_iterator_next(&iter))) {

		AST_LIST_LOCK(&curlot->parkings);
		AST_LIST_TRAVERSE(&curlot->parkings, cur, list) {
			astman_append(s, "Event: ParkedCall\r\n"
				"Exten: %d\r\n"
				"Channel: %s\r\n"
				"From: %s\r\n"
				"Timeout: %ld\r\n"
				"CallerIDNum: %s\r\n"
				"CallerIDName: %s\r\n"
				"%s"
				"\r\n",
				cur->parkingnum, cur->chan->name, cur->peername,
				(long) cur->start.tv_sec + (long) (cur->parkingtime / 1000) - (long) time(NULL),
				S_COR(cur->chan->caller.id.number.valid, cur->chan->caller.id.number.str, ""),	/* XXX in other places it is <unknown> */
				S_COR(cur->chan->caller.id.name.valid, cur->chan->caller.id.name.str, ""),
				idText);
		}
		AST_LIST_UNLOCK(&curlot->parkings);
		ao2_ref(curlot, -1);
	}

	astman_append(s,
		"Event: ParkedCallsComplete\r\n"
		"%s"
		"\r\n",idText);


	return RESULT_SUCCESS;
}

/*!
 * \brief Create manager event for parked calls
 * \param s
 * \param m
 *
 * Get channels involved in park, create event.
 * \return Always 0
*/
static int manager_park(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *channel2 = astman_get_header(m, "Channel2");
	const char *timeout = astman_get_header(m, "Timeout");
	const char *parkinglotname = astman_get_header(m, "Parkinglot");
	char buf[BUFSIZ];
	int res = 0;
	struct ast_channel *ch1, *ch2;
	struct ast_park_call_args args = {0,};

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}

	if (ast_strlen_zero(channel2)) {
		astman_send_error(s, m, "Channel2 not specified");
		return 0;
	}

	if (!(ch1 = ast_channel_get_by_name(channel))) {
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", channel);
		astman_send_error(s, m, buf);
		return 0;
	}

	if (!(ch2 = ast_channel_get_by_name(channel2))) {
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", channel2);
		astman_send_error(s, m, buf);
		ast_channel_unref(ch1);
		return 0;
	}

	if (!ast_strlen_zero(timeout)) {
		sscanf(timeout, "%30d", &args.timeout);
	}
	if (!ast_strlen_zero(parkinglotname)) {
		args.parkinglot = find_parkinglot(parkinglotname);
	}

	ast_channel_lock(ch1);
	while (ast_channel_trylock(ch2)) {
		CHANNEL_DEADLOCK_AVOIDANCE(ch1);
	}

	res = masq_park_call(ch1, ch2, 0, NULL, 0, &args);
	if (!res) {
		ast_softhangup(ch2, AST_SOFTHANGUP_EXPLICIT);
		astman_send_ack(s, m, "Park successful");
	} else {
		astman_send_error(s, m, "Park failure");
	}

	ast_channel_unlock(ch1);
	ast_channel_unlock(ch2);

	ch1 = ast_channel_unref(ch1);
	ch2 = ast_channel_unref(ch2);

	return 0;
}

static int find_channel_by_group(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *c = data;
	struct ast_channel *chan = obj;

	int i = !chan->pbx &&
		/* Accessing 'chan' here is safe without locking, because there is no way for
		   the channel do disappear from under us at this point.  pickupgroup *could*
		   change while we're here, but that isn't a problem. */
		(c != chan) &&
		(chan->pickupgroup & c->callgroup) &&
		((chan->_state == AST_STATE_RINGING) || (chan->_state == AST_STATE_RING)) &&
		!c->masq;

	return i ? CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \brief Pickup a call
 * \param chan channel that initiated pickup.
 *
 * Walk list of channels, checking it is not itself, channel is pbx one,
 * check that the callgroup for both channels are the same and the channel is ringing.
 * Answer calling channel, flag channel as answered on queue, masq channels together.
*/
int ast_pickup_call(struct ast_channel *chan)
{
	struct ast_channel *cur, *chans[2] = { chan, };
	struct ast_party_connected_line connected_caller;
	int res;
	const char *chan_name;
	const char *cur_name;

	if (!(cur = ast_channel_callback(find_channel_by_group, NULL, chan, 0))) {
		ast_debug(1, "No call pickup possible...\n");
		if (!ast_strlen_zero(pickupfailsound)) {
			ast_stream_and_wait(chan, pickupfailsound, "");
		}
		return -1;
	}

	chans[1] = cur;

	ast_channel_lock_both(cur, chan);

	cur_name = ast_strdupa(cur->name);
	chan_name = ast_strdupa(chan->name);

	ast_debug(1, "Call pickup on chan '%s' by '%s'\n", cur_name, chan_name);

	connected_caller = cur->connected;
	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
	if (ast_channel_connected_line_macro(NULL, chan, &connected_caller, 0, 0)) {
		ast_channel_update_connected_line(chan, &connected_caller, NULL);
	}

	ast_party_connected_line_collect_caller(&connected_caller, &chan->caller);
	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
	ast_channel_queue_connected_line_update(chan, &connected_caller, NULL);

	ast_channel_unlock(cur);
	ast_channel_unlock(chan);

	if (ast_answer(chan)) {
		ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan_name);
	}

	if (ast_queue_control(chan, AST_CONTROL_ANSWER)) {
		ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan_name);
	}

	if ((res = ast_channel_masquerade(cur, chan))) {
		ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan_name, cur_name);
	}

	if (!ast_strlen_zero(pickupsound)) {
		ast_stream_and_wait(cur, pickupsound, "");
	}

	/* If you want UniqueIDs, set channelvars in manager.conf to CHANNEL(uniqueid) */
	ast_manager_event_multichan(EVENT_FLAG_CALL, "Pickup", 2, chans,
		"Channel: %s\r\nTargetChannel: %s\r\n", chan->name, cur->name);

	cur = ast_channel_unref(cur);

	return res;
}

static char *app_bridge = "Bridge";

enum {
	BRIDGE_OPT_PLAYTONE = (1 << 0),
	OPT_CALLEE_HANGUP =	(1 << 1),
	OPT_CALLER_HANGUP =	(1 << 2),
	OPT_DURATION_LIMIT = (1 << 3),
	OPT_DURATION_STOP =	(1 << 4),
	OPT_CALLEE_TRANSFER = (1 << 5),
	OPT_CALLER_TRANSFER = (1 << 6),
	OPT_CALLEE_MONITOR = (1 << 7),
	OPT_CALLER_MONITOR = (1 << 8),
	OPT_CALLEE_PARK = (1 << 9),
	OPT_CALLER_PARK = (1 << 10),
	OPT_CALLEE_KILL = (1 << 11),
};
 
enum {
	OPT_ARG_DURATION_LIMIT = 0,
	OPT_ARG_DURATION_STOP,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(bridge_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION('p', BRIDGE_OPT_PLAYTONE),
	AST_APP_OPTION('h', OPT_CALLEE_HANGUP),
	AST_APP_OPTION('H', OPT_CALLER_HANGUP),
	AST_APP_OPTION('k', OPT_CALLEE_PARK),
	AST_APP_OPTION('K', OPT_CALLER_PARK),
	AST_APP_OPTION_ARG('L', OPT_DURATION_LIMIT, OPT_ARG_DURATION_LIMIT),
	AST_APP_OPTION_ARG('S', OPT_DURATION_STOP, OPT_ARG_DURATION_STOP),
	AST_APP_OPTION('t', OPT_CALLEE_TRANSFER),
	AST_APP_OPTION('T', OPT_CALLER_TRANSFER),
	AST_APP_OPTION('w', OPT_CALLEE_MONITOR),
	AST_APP_OPTION('W', OPT_CALLER_MONITOR),
	AST_APP_OPTION('x', OPT_CALLEE_KILL),
END_OPTIONS );

int ast_bridge_timelimit(struct ast_channel *chan, struct ast_bridge_config *config,
	char *parse, struct timeval *calldurationlimit)
{
	char *stringp = ast_strdupa(parse);
	char *limit_str, *warning_str, *warnfreq_str;
	const char *var;
	int play_to_caller = 0, play_to_callee = 0;
	int delta;

	limit_str = strsep(&stringp, ":");
	warning_str = strsep(&stringp, ":");
	warnfreq_str = strsep(&stringp, ":");

	config->timelimit = atol(limit_str);
	if (warning_str)
		config->play_warning = atol(warning_str);
	if (warnfreq_str)
		config->warning_freq = atol(warnfreq_str);

	if (!config->timelimit) {
		ast_log(LOG_WARNING, "Bridge does not accept L(%s), hanging up.\n", limit_str);
		config->timelimit = config->play_warning = config->warning_freq = 0;
		config->warning_sound = NULL;
		return -1; /* error */
	} else if ( (delta = config->play_warning - config->timelimit) > 0) {
		int w = config->warning_freq;

		/* If the first warning is requested _after_ the entire call would end,
		   and no warning frequency is requested, then turn off the warning. If
		   a warning frequency is requested, reduce the 'first warning' time by
		   that frequency until it falls within the call's total time limit.
		   Graphically:
				  timelim->|    delta        |<-playwarning
			0__________________|_________________|
					 | w  |    |    |    |

		   so the number of intervals to cut is 1+(delta-1)/w
		*/

		if (w == 0) {
			config->play_warning = 0;
		} else {
			config->play_warning -= w * ( 1 + (delta-1)/w );
			if (config->play_warning < 1)
				config->play_warning = config->warning_freq = 0;
		}
	}
	
	ast_channel_lock(chan);

	var = pbx_builtin_getvar_helper(chan, "LIMIT_PLAYAUDIO_CALLER");
	play_to_caller = var ? ast_true(var) : 1;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_PLAYAUDIO_CALLEE");
	play_to_callee = var ? ast_true(var) : 0;

	if (!play_to_caller && !play_to_callee)
		play_to_caller = 1;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_WARNING_FILE");
	config->warning_sound = !ast_strlen_zero(var) ? ast_strdup(var) : ast_strdup("timeleft");

	/* The code looking at config wants a NULL, not just "", to decide
	 * that the message should not be played, so we replace "" with NULL.
	 * Note, pbx_builtin_getvar_helper _can_ return NULL if the variable is
	 * not found.
	 */

	var = pbx_builtin_getvar_helper(chan, "LIMIT_TIMEOUT_FILE");
	config->end_sound = !ast_strlen_zero(var) ? ast_strdup(var) : NULL;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_CONNECT_FILE");
	config->start_sound = !ast_strlen_zero(var) ? ast_strdup(var) : NULL;

	ast_channel_unlock(chan);

	/* undo effect of S(x) in case they are both used */
	calldurationlimit->tv_sec = 0;
	calldurationlimit->tv_usec = 0;

	/* more efficient to do it like S(x) does since no advanced opts */
	if (!config->play_warning && !config->start_sound && !config->end_sound && config->timelimit) {
		calldurationlimit->tv_sec = config->timelimit / 1000;
		calldurationlimit->tv_usec = (config->timelimit % 1000) * 1000;
		ast_verb(3, "Setting call duration limit to %.3lf seconds.\n",
			calldurationlimit->tv_sec + calldurationlimit->tv_usec / 1000000.0);
		config->timelimit = play_to_caller = play_to_callee =
		config->play_warning = config->warning_freq = 0;
	} else {
		ast_verb(4, "Limit Data for this call:\n");
		ast_verb(4, "timelimit      = %ld ms (%.3lf s)\n", config->timelimit, config->timelimit / 1000.0);
		ast_verb(4, "play_warning   = %ld ms (%.3lf s)\n", config->play_warning, config->play_warning / 1000.0);
		ast_verb(4, "play_to_caller = %s\n", play_to_caller ? "yes" : "no");
		ast_verb(4, "play_to_callee = %s\n", play_to_callee ? "yes" : "no");
		ast_verb(4, "warning_freq   = %ld ms (%.3lf s)\n", config->warning_freq, config->warning_freq / 1000.0);
		ast_verb(4, "start_sound    = %s\n", S_OR(config->start_sound, ""));
		ast_verb(4, "warning_sound  = %s\n", config->warning_sound);
		ast_verb(4, "end_sound      = %s\n", S_OR(config->end_sound, ""));
	}
	if (play_to_caller)
		ast_set_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING);
	if (play_to_callee)
		ast_set_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING);
	return 0;
}


/*!
 * \brief Bridge channels
 * \param chan
 * \param data channel to bridge with.
 * 
 * Split data, check we aren't bridging with ourself, check valid channel,
 * answer call if not already, check compatible channels, setup bridge config
 * now bridge call, if transfered party hangs up return to PBX extension.
*/
static int bridge_exec(struct ast_channel *chan, const char *data)
{
	struct ast_channel *current_dest_chan, *final_dest_chan, *chans[2];
	char *tmp_data  = NULL;
	struct ast_flags opts = { 0, };
	struct ast_bridge_config bconfig = { { 0, }, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct timeval calldurationlimit = { 0, };

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dest_chan);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Bridge require at least 1 argument specifying the other end of the bridge\n");
		return -1;
	}

	tmp_data = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, tmp_data);
	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(bridge_exec_options, &opts, opt_args, args.options);

	/* avoid bridge with ourselves */
	if (!strcmp(chan->name, args.dest_chan)) {
		ast_log(LOG_WARNING, "Unable to bridge channel %s with itself\n", chan->name);
		ast_manager_event(chan, EVENT_FLAG_CALL, "BridgeExec",
					"Response: Failed\r\n"
					"Reason: Unable to bridge channel to itself\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n",
					chan->name, args.dest_chan);
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "LOOP");
		return 0;
	}

	/* make sure we have a valid end point */
	if (!(current_dest_chan = ast_channel_get_by_name_prefix(args.dest_chan,
			strlen(args.dest_chan)))) {
		ast_log(LOG_WARNING, "Bridge failed because channel %s does not exists or we "
			"cannot get its lock\n", args.dest_chan);
		ast_manager_event(chan, EVENT_FLAG_CALL, "BridgeExec",
					"Response: Failed\r\n"
					"Reason: Cannot grab end point\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n", chan->name, args.dest_chan);
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "NONEXISTENT");
		return 0;
	}

	/* answer the channel if needed */
	if (current_dest_chan->_state != AST_STATE_UP) {
		ast_answer(current_dest_chan);
	}

	/* try to allocate a place holder where current_dest_chan will be placed */
	if (!(final_dest_chan = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, 
		NULL, NULL, current_dest_chan->linkedid, 0, "Bridge/%s", current_dest_chan->name))) {
		ast_log(LOG_WARNING, "Cannot create placeholder channel for chan %s\n", args.dest_chan);
		ast_manager_event(chan, EVENT_FLAG_CALL, "BridgeExec",
					"Response: Failed\r\n"
					"Reason: cannot create placeholder\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n", chan->name, args.dest_chan);
	}

	ast_channel_unlock(current_dest_chan);

	do_bridge_masquerade(current_dest_chan, final_dest_chan);

	chans[0] = current_dest_chan;
	chans[1] = final_dest_chan;

	/* now current_dest_chan is a ZOMBIE and with softhangup set to 1 and final_dest_chan is our end point */
	/* try to make compatible, send error if we fail */
	if (ast_channel_make_compatible(chan, final_dest_chan) < 0) {
		ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, final_dest_chan->name);
		ast_manager_event_multichan(EVENT_FLAG_CALL, "BridgeExec", 2, chans,
					"Response: Failed\r\n"
					"Reason: Could not make channels compatible for bridge\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n", chan->name, final_dest_chan->name);
		ast_hangup(final_dest_chan); /* may be we should return this channel to the PBX? */
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "INCOMPATIBLE");
		current_dest_chan = ast_channel_unref(current_dest_chan);
		return 0;
	}

	/* Report that the bridge will be successfull */
	ast_manager_event_multichan(EVENT_FLAG_CALL, "BridgeExec", 2, chans,
				"Response: Success\r\n"
				"Channel1: %s\r\n"
				"Channel2: %s\r\n", chan->name, final_dest_chan->name);

	/* we have 2 valid channels to bridge, now it is just a matter of setting up the bridge config and starting the bridge */	
	if (ast_test_flag(&opts, BRIDGE_OPT_PLAYTONE) && !ast_strlen_zero(xfersound)) {
		if (!ast_streamfile(final_dest_chan, xfersound, final_dest_chan->language)) {
			if (ast_waitstream(final_dest_chan, "") < 0)
				ast_log(LOG_WARNING, "Failed to play courtesy tone on %s\n", final_dest_chan->name);
		}
	}
	
	current_dest_chan = ast_channel_unref(current_dest_chan);
	
	if (ast_test_flag(&opts, OPT_DURATION_LIMIT) && !ast_strlen_zero(opt_args[OPT_ARG_DURATION_LIMIT])) {
		if (ast_bridge_timelimit(chan, &bconfig, opt_args[OPT_ARG_DURATION_LIMIT], &calldurationlimit))
			goto done;
	}

	if (ast_test_flag(&opts, OPT_CALLEE_TRANSFER))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_REDIRECT);
	if (ast_test_flag(&opts, OPT_CALLER_TRANSFER))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_REDIRECT);
	if (ast_test_flag(&opts, OPT_CALLEE_HANGUP))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_DISCONNECT);
	if (ast_test_flag(&opts, OPT_CALLER_HANGUP))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_DISCONNECT);
	if (ast_test_flag(&opts, OPT_CALLEE_MONITOR))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_AUTOMON);
	if (ast_test_flag(&opts, OPT_CALLER_MONITOR))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_AUTOMON);
	if (ast_test_flag(&opts, OPT_CALLEE_PARK))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_PARKCALL);
	if (ast_test_flag(&opts, OPT_CALLER_PARK))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_PARKCALL);

	ast_bridge_call(chan, final_dest_chan, &bconfig);

	/* the bridge has ended, set BRIDGERESULT to SUCCESS. If the other channel has not been hung up, return it to the PBX */
	pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "SUCCESS");
	if (!ast_check_hangup(final_dest_chan) && !ast_test_flag(&opts, OPT_CALLEE_KILL)) {
		ast_debug(1, "starting new PBX in %s,%s,%d for chan %s\n",
			final_dest_chan->context, final_dest_chan->exten,
			final_dest_chan->priority, final_dest_chan->name);

		if (ast_pbx_start(final_dest_chan) != AST_PBX_SUCCESS) {
			ast_log(LOG_WARNING, "FAILED continuing PBX on dest chan %s\n", final_dest_chan->name);
			ast_hangup(final_dest_chan);
		} else
			ast_debug(1, "SUCCESS continuing PBX on chan %s\n", final_dest_chan->name);
	} else {
		ast_debug(1, "hangup chan %s since the other endpoint has hung up or the x flag was passed\n", final_dest_chan->name);
		ast_hangup(final_dest_chan);
	}
done:
	if (bconfig.warning_sound) {
		ast_free((char *)bconfig.warning_sound);
	}
	if (bconfig.end_sound) {
		ast_free((char *)bconfig.end_sound);
	}
	if (bconfig.start_sound) {
		ast_free((char *)bconfig.start_sound);
	}

	return 0;
}

int ast_features_init(void)
{
	int res;

	ast_register_application2(app_bridge, bridge_exec, NULL, NULL, NULL);

	parkinglots = ao2_container_alloc(7, parkinglot_hash_cb, parkinglot_cmp_cb);

	if ((res = load_config()))
		return res;
	ast_cli_register_multiple(cli_features, ARRAY_LEN(cli_features));
	ast_pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	res = ast_register_application2(parkedcall, park_exec, NULL, NULL, NULL);
	if (!res)
		res = ast_register_application2(parkcall, park_call_exec, NULL, NULL, NULL);
	if (!res) {
		ast_manager_register_xml("ParkedCalls", 0, manager_parking_status);
		ast_manager_register_xml("Park", EVENT_FLAG_CALL, manager_park);
		ast_manager_register_xml("Bridge", EVENT_FLAG_CALL, action_bridge);
	}

	res |= ast_devstate_prov_add("Park", metermaidstate);
#ifdef TEST_FRAMEWORK
	res |= AST_TEST_REGISTER(features_test);
#endif

	return res;
}
