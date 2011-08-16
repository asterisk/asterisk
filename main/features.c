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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

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
						the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
					</option>
					<option name="K">
						<para>Allow the calling party to enable parking of the call by sending
						 the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
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
						DTMF sequence defined in <filename>features.conf</filename>.</para>
					</option>
					<option name="T">
						<para>Allow the calling party to transfer the called party by sending the
						DTMF sequence defined in <filename>features.conf</filename>.</para>
					</option>
					<option name="w">
						<para>Allow the called party to enable recording of the call by sending
						the DTMF sequence defined for one-touch recording in <filename>features.conf</filename>.</para>
					</option>
					<option name="W">
						<para>Allow the calling party to enable recording of the call by sending
						the DTMF sequence defined for one-touch recording in <filename>features.conf</filename>.</para>
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
			Retrieve a parked call.
		</synopsis>
		<syntax>
			<parameter name="exten">
				<para>Parking space extension to retrieve a parked call.
				If not provided then the first available parked call in the
				parking lot will be retrieved.</para>
			</parameter>
			<parameter name="parking_lot_name">
				<para>Specify from which parking lot to retrieve a parked call.</para>
				<para>The parking lot used is selected in the following order:</para>
				<para>1) parking_lot_name option</para>
				<para>2) <variable>PARKINGLOT</variable> variable</para>
				<para>3) <literal>CHANNEL(parkinglot)</literal> function
				(Possibly preset by the channel driver.)</para>
				<para>4) Default parking lot.</para>
			</parameter>
		</syntax>
		<description>
			<para>Used to retrieve a parked call from a parking lot.</para>
			<note>
				<para>Parking lots automatically create and manage dialplan extensions in
				the parking lot context.  You do not need to explicitly use this
				application in your dialplan.  Instead, all you should do is include the
				parking lot context in your dialplan.</para>
			</note>
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
			<parameter name="parking_lot_name">
				<para>Specify in which parking lot to park a call.</para>
				<para>The parking lot used is selected in the following order:</para>
				<para>1) parking_lot_name option</para>
				<para>2) <variable>PARKINGLOT</variable> variable</para>
				<para>3) <literal>CHANNEL(parkinglot)</literal> function
				(Possibly preset by the channel driver.)</para>
				<para>4) Default parking lot.</para>
			</parameter>
		</syntax>
		<description>
			<para>Used to park yourself (typically in combination with a supervised
			transfer to know the parking space).</para>
			<para>If you set the <variable>PARKINGEXTEN</variable> variable to a
			parking space extension in the parking lot, Park() will attempt to park the call
			on that extension.  If the extension is already is in use then execution
			will continue at the next priority.</para>
			<para>If the <literal>parkeddynamic</literal> option is enabled in <filename>features.conf</filename>
			the following variables can be used to dynamically create new parking lots.</para>
			<para>If you set the <variable>PARKINGDYNAMIC</variable> variable and this parking lot
			exists then it will be used as a template for the newly created dynamic lot.  Otherwise,
			the default parking lot will be used.</para>
			<para>If you set the <variable>PARKINGDYNCONTEXT</variable> variable then the newly created dynamic
			parking lot will use this context.</para>
			<para>If you set the <variable>PARKINGDYNEXTEN</variable> variable then the newly created dynamic
			parking lot will use this extension to access the parking lot.</para>
			<para>If you set the <variable>PARKINGDYNPOS</variable> variable then the newly created dynamic parking lot
			will use those parking postitions.</para>
			<note>
				<para>This application must be used as the first extension priority
				to be recognized as a parking access extension.  DTMF transfers
				and some channel drivers need this distinction to operate properly.
				The parking access extension in this case is treated like a dialplan
				hint.</para>
			</note>
			<note>
				<para>Parking lots automatically create and manage dialplan extensions in
				the parking lot context.  You do not need to explicitly use this
				application in your dialplan.  Instead, all you should do is include the
				parking lot context in your dialplan.</para>
			</note>
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
				<para>Specify in which parking lot to park the channel.</para>
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

typedef enum {
	FEATURE_INTERPRET_DETECT, /* Used by ast_feature_detect */
	FEATURE_INTERPRET_DO,     /* Used by feature_interpret */
	FEATURE_INTERPRET_CHECK,  /* Used by feature_check */
} feature_interpret_op;

static char *parkedcall = "ParkedCall";

static char pickup_ext[AST_MAX_EXTENSION];                 /*!< Call pickup extension */

/*! Parking lot access ramp dialplan usage entry. */
struct parking_dp_ramp {
	/*! Next node in the parking lot spaces dialplan list. */
	AST_LIST_ENTRY(parking_dp_ramp) node;
	/*! TRUE if the parking lot access extension is exclusive. */
	unsigned int exclusive:1;
	/*! Parking lot access extension */
	char exten[1];
};

/*! Parking lot dialplan access ramp map */
AST_LIST_HEAD_NOLOCK(parking_dp_ramp_map, parking_dp_ramp);

/*! Parking lot spaces dialplan usage entry. */
struct parking_dp_spaces {
	/*! Next node in the parking lot spaces dialplan list. */
	AST_LIST_ENTRY(parking_dp_spaces) node;
	/*! First parking space */
	int start;
	/*! Last parking space */
	int stop;
};

/*! Parking lot dialplan context space map */
AST_LIST_HEAD_NOLOCK(parking_dp_space_map, parking_dp_spaces);

/*! Parking lot context dialplan usage entry. */
struct parking_dp_context {
	/*! Next node in the parking lot contexts dialplan list. */
	AST_LIST_ENTRY(parking_dp_context) node;
	/*! Parking access extensions defined in this context. */
	struct parking_dp_ramp_map access_extens;
	/*! Parking spaces defined in this context. */
	struct parking_dp_space_map spaces;
	/*! Parking hints defined in this context. */
	struct parking_dp_space_map hints;
	/*! Parking lot context name */
	char context[1];
};

/*! Parking lot dialplan usage map. */
AST_LIST_HEAD_NOLOCK(parking_dp_map, parking_dp_context);

/*!
 * \brief Description of one parked call, added to a list while active, then removed.
 * The list belongs to a parkinglot.
 */
struct parkeduser {
	struct ast_channel *chan;                   /*!< Parked channel */
	struct timeval start;                       /*!< Time the park started */
	int parkingnum;                             /*!< Parking lot space used */
	char parkingexten[AST_MAX_EXTENSION];       /*!< If set beforehand, parking extension used for this call */
	char context[AST_MAX_CONTEXT];              /*!< Where to go if our parking time expires */
	char exten[AST_MAX_EXTENSION];
	int priority;
	int parkingtime;                            /*!< Maximum length in parking lot before return */
	/*! Method to entertain the caller when parked: AST_CONTROL_RINGING, AST_CONTROL_HOLD, or 0(none) */
	enum ast_control_frame_type hold_method;
	unsigned int notquiteyet:1;
	unsigned int options_specified:1;
	char peername[1024];
	unsigned char moh_trys;
	/*! Parking lot this entry belongs to.  Holds a parking lot reference. */
	struct ast_parkinglot *parkinglot;
	AST_LIST_ENTRY(parkeduser) list;
};

/*! Parking lot configuration options. */
struct parkinglot_cfg {
	/*! Music class used for parking */
	char mohclass[MAX_MUSICCLASS];
	/*! Extension to park calls in this parking lot. */
	char parkext[AST_MAX_EXTENSION];
	/*! Context for which parking is made accessible */
	char parking_con[AST_MAX_EXTENSION];
	/*! First available extension for parking */
	int parking_start;
	/*! Last available extension for parking */
	int parking_stop;
	/*! Default parking time in ms. */
	int parkingtime;
	/*!
	 * \brief Enable DTMF based transfers on bridge when picking up parked calls.
	 *
	 * \details
	 * none(0)
	 * AST_FEATURE_FLAG_BYCALLEE
	 * AST_FEATURE_FLAG_BYCALLER
	 * AST_FEATURE_FLAG_BYBOTH
	 */
	int parkedcalltransfers;
	/*!
	 * \brief Enable DTMF based parking on bridge when picking up parked calls.
	 *
	 * \details
	 * none(0)
	 * AST_FEATURE_FLAG_BYCALLEE
	 * AST_FEATURE_FLAG_BYCALLER
	 * AST_FEATURE_FLAG_BYBOTH
	 */
	int parkedcallreparking;
	/*!
	 * \brief Enable DTMF based hangup on a bridge when pickup up parked calls.
	 *
	 * \details
	 * none(0)
	 * AST_FEATURE_FLAG_BYCALLEE
	 * AST_FEATURE_FLAG_BYCALLER
	 * AST_FEATURE_FLAG_BYBOTH
	 */
	int parkedcallhangup;
	/*!
	 * \brief Enable DTMF based recording on a bridge when picking up parked calls.
	 *
	 * \details
	 * none(0)
	 * AST_FEATURE_FLAG_BYCALLEE
	 * AST_FEATURE_FLAG_BYCALLER
	 * AST_FEATURE_FLAG_BYBOTH
	 */
	int parkedcallrecording;

	/*! TRUE if findslot is set to next */
	unsigned int parkfindnext:1;
	/*! TRUE if the parking lot is exclusively accessed by parkext */
	unsigned int parkext_exclusive:1;
	/*! Add parking hints automatically */
	unsigned int parkaddhints:1;
	/*! TRUE if configuration is invalid and the parking lot should not be used. */
	unsigned int is_invalid:1;
};

/*! \brief Structure for parking lots which are put in a container. */
struct ast_parkinglot {
	/*! Name of the parking lot. */
	char name[AST_MAX_CONTEXT];
	/*! Parking lot user configuration. */
	struct parkinglot_cfg cfg;

	/*! Parking space to start next park search. */
	int next_parking_space;

	/*! That which bears the_mark shall be deleted if parking lot empty! (Used during reloads.) */
	unsigned int the_mark:1;
	/*! TRUE if the parking lot is disabled. */
	unsigned int disabled:1;

	/*! List of active parkings in this parkinglot */
	AST_LIST_HEAD(parkinglot_parklist, parkeduser) parkings;
};

/*! \brief The configured parking lots container. Always at least one  - the default parking lot */
static struct ao2_container *parkinglots;

/*!
 * \brief Default parking lot.
 * \note Holds a parkinglot reference.
 * \note Will not be NULL while running.
 */
static struct ast_parkinglot *default_parkinglot;

/*! Force a config reload to reload regardless of config file timestamp. */
static int force_reload_load;

static int parkedplay = 0;                                 /*!< Who to play courtesytone to when someone picks up a parked call. */
static int parkeddynamic = 0;                              /*!< Enable creation of parkinglots dynamically */
static char courtesytone[256];                             /*!< Courtesy tone used to pickup parked calls and on-touch-record */
static char xfersound[256];                                /*!< Call transfer sound */
static char xferfailsound[256];                            /*!< Call transfer failure sound */
static char pickupsound[256];                              /*!< Pickup sound */
static char pickupfailsound[256];                          /*!< Pickup failure sound */

/*!
 * \brief Context for parking dialback to parker.
 * \note The need for the context is a KLUDGE.
 *
 * \todo Might be able to eliminate the parking_con_dial context
 * kludge by running app_dial directly in its own thread to
 * simulate a PBX.
 */
static char parking_con_dial[] = "park-dial";

/*! Ensure that features.conf reloads on one thread at a time. */
AST_MUTEX_DEFINE_STATIC(features_reload_lock);

static int adsipark;

static int transferdigittimeout;
static int featuredigittimeout;
static int comebacktoorigin = 1;

static int atxfernoanswertimeout;
static unsigned int atxferdropcall;
static unsigned int atxferloopdelay;
static unsigned int atxfercallbackretries;

static char *registrar = "features";		   /*!< Registrar for operations */

/*! PARK_APP_NAME application arguments */
AST_DEFINE_APP_ARGS_TYPE(park_app_args,
	AST_APP_ARG(timeout);		/*!< Time in ms to remain in the parking lot. */
	AST_APP_ARG(return_con);	/*!< Context to return parked call if timeout. */
	AST_APP_ARG(return_ext);	/*!< Exten to return parked call if timeout. */
	AST_APP_ARG(return_pri);	/*!< Priority to return parked call if timeout. */
	AST_APP_ARG(options);		/*!< Parking option flags. */
	AST_APP_ARG(pl_name);		/*!< Parking lot name to use if present. */
	AST_APP_ARG(dummy);			/*!< Place to put any remaining args string. */
	);

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
/*!
 * \internal
 * \brief Set the channel technology to the kill technology.
 *
 * \param chan Channel to change technology.
 *
 * \return Nothing
 */
static void set_kill_chan_tech(struct ast_channel *chan)
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

	/* Install the kill technology and wake up anyone waiting on it. */
	chan->tech = &ast_kill_tech;
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
static struct ast_parkinglot *find_parkinglot(const char *name);
static struct ast_parkinglot *create_parkinglot(const char *name);
static struct ast_parkinglot *copy_parkinglot(const char *name, const struct ast_parkinglot *parkinglot);
static int parkinglot_activate(struct ast_parkinglot *parkinglot);
static int play_message_on_chan(struct ast_channel *play_to, struct ast_channel *other, const char *msg, const char *audiofile);

/*!
 * \internal
 * \brief Get the parking extension if it exists.
 *
 * \param exten_str Parking extension to see if exists.
 * \param chan Channel to autoservice while looking for exten.  (Could be NULL)
 * \param context Parking context to look in for exten.
 *
 * \retval exten on success.
 * \retval NULL on error or exten does not exist.
 */
static struct ast_exten *get_parking_exten(const char *exten_str, struct ast_channel *chan, const char *context)
{
	struct ast_exten *exten;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	const char *app_at_exten;

	exten = pbx_find_extension(chan, NULL, &q, context, exten_str, 1, NULL, NULL,
		E_MATCH);
	if (!exten) {
		return NULL;
	}

	app_at_exten = ast_get_extension_app(exten);
	if (!app_at_exten || strcasecmp(PARK_APP_NAME, app_at_exten)) {
		return NULL;
	}

	return exten;
}

int ast_parking_ext_valid(const char *exten_str, struct ast_channel *chan, const char *context)
{
	return get_parking_exten(exten_str, chan, context) ? 1 : 0;
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
	struct ast_parkinglot *parkinglot = obj;
	struct ast_parkinglot *parkinglot2 = arg;

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
	struct ast_channel *transferee, const char *type, format_t format, void *data,
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

/*!
 * \brief Find parking lot name from channel
 * \note Channel needs to be locked while the returned string is in use.
 */
static const char *findparkinglotname(struct ast_channel *chan)
{
	const char *name;

	/* The channel variable overrides everything */
	name = pbx_builtin_getvar_helper(chan, "PARKINGLOT");
	if (!name && !ast_strlen_zero(chan->parkinglot)) {
		/* Use the channel's parking lot. */
		name = chan->parkinglot;
	}
	return name;
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

/*! Optional additional parking options when parking a call. */
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
	/*! \brief Parkinglot to be parked in */
	struct ast_parkinglot *parkinglot;
};

/*!
 * \internal
 * \brief Create a dynamic parking lot.
 *
 * \param name Dynamic parking lot name to create.
 * \param chan Channel to get dynamic parking lot parameters.
 *
 * \retval parkinglot on success.
 * \retval NULL on error.
 */
static struct ast_parkinglot *create_dynamic_parkinglot(const char *name, struct ast_channel *chan)
{
	const char *dyn_context;
	const char *dyn_exten;
	const char *dyn_range;
	const char *template_name;
	struct ast_parkinglot *template_parkinglot = NULL;
	struct ast_parkinglot *parkinglot;
	int dyn_start;
	int dyn_end;

	ast_channel_lock(chan);
	template_name = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGDYNAMIC"), ""));
	dyn_context = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGDYNCONTEXT"), ""));
	dyn_exten = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGDYNEXTEN"), ""));
	dyn_range = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGDYNPOS"), ""));
	ast_channel_unlock(chan);

	if (!ast_strlen_zero(template_name)) {
		template_parkinglot = find_parkinglot(template_name);
		if (!template_parkinglot) {
			ast_debug(1, "PARKINGDYNAMIC lot %s does not exist.\n",
				template_name);
		} else if (template_parkinglot->cfg.is_invalid) {
			ast_debug(1, "PARKINGDYNAMIC lot %s has invalid config.\n",
				template_name);
			parkinglot_unref(template_parkinglot);
			template_parkinglot = NULL;
		}
	}
	if (!template_parkinglot) {
		template_parkinglot = parkinglot_addref(default_parkinglot);
		ast_debug(1, "Using default parking lot for template\n");
	}

	parkinglot = copy_parkinglot(name, template_parkinglot);
	if (!parkinglot) {
		ast_log(LOG_ERROR, "Could not build dynamic parking lot!\n");
	} else {
		/* Configure the dynamic parking lot. */
		if (!ast_strlen_zero(dyn_context)) {
			ast_copy_string(parkinglot->cfg.parking_con, dyn_context,
				sizeof(parkinglot->cfg.parking_con));
		}
		if (!ast_strlen_zero(dyn_exten)) {
			ast_copy_string(parkinglot->cfg.parkext, dyn_exten,
				sizeof(parkinglot->cfg.parkext));
		}
		if (!ast_strlen_zero(dyn_range)) {
			if (sscanf(dyn_range, "%30d-%30d", &dyn_start, &dyn_end) != 2) {
				ast_log(LOG_WARNING,
					"Format for parking positions is a-b, where a and b are numbers\n");
			} else if (dyn_end < dyn_start || dyn_start <= 0 || dyn_end <= 0) {
				ast_log(LOG_WARNING,
					"Format for parking positions is a-b, where a <= b\n");
			} else {
				parkinglot->cfg.parking_start = dyn_start;
				parkinglot->cfg.parking_stop = dyn_end;
			}
		}

		/*
		 * Sanity check for dynamic parking lot configuration.
		 *
		 * XXX It may be desirable to instead check if the dynamic
		 * parking lot overlaps any existing lots like what is done for
		 * a reload.
		 */
		if (!strcmp(parkinglot->cfg.parking_con, template_parkinglot->cfg.parking_con)) {
			if (!strcmp(parkinglot->cfg.parkext, template_parkinglot->cfg.parkext)
				&& parkinglot->cfg.parkext_exclusive) {
				ast_log(LOG_WARNING,
					"Parking lot '%s' conflicts with template parking lot '%s'!\n"
					"Change either PARKINGDYNCONTEXT or PARKINGDYNEXTEN.\n",
					parkinglot->name, template_parkinglot->name);
			}
			if ((template_parkinglot->cfg.parking_start <= parkinglot->cfg.parking_start
					&& parkinglot->cfg.parking_start <= template_parkinglot->cfg.parking_stop)
				|| (template_parkinglot->cfg.parking_start <= parkinglot->cfg.parking_stop
					&& parkinglot->cfg.parking_stop <= template_parkinglot->cfg.parking_stop)
				|| (parkinglot->cfg.parking_start < template_parkinglot->cfg.parking_start
					&& template_parkinglot->cfg.parking_stop < parkinglot->cfg.parking_stop)) {
				ast_log(LOG_WARNING,
					"Parking lot '%s' parking spaces overlap template parking lot '%s'!\n"
					"Change PARKINGDYNPOS.\n",
					parkinglot->name, template_parkinglot->name);
			}
		}

		parkinglot_activate(parkinglot);
		ao2_link(parkinglots, parkinglot);
	}
	parkinglot_unref(template_parkinglot);

	return parkinglot;
}

/*!
 * \internal
 * \brief Reserve a parking space in a parking lot for a call being parked.
 *
 * \param park_me Channel being parked.
 * \param parker Channel parking the call.
 * \param args Optional additional parking options when parking a call.
 *
 * \return Parked call descriptor or NULL if failed.
 * \note The parking lot list is locked if successful.
 */
static struct parkeduser *park_space_reserve(struct ast_channel *park_me, struct ast_channel *parker, struct ast_park_call_args *args)
{
	struct parkeduser *pu;
	int i;
	int parking_space = -1;
	const char *parkinglotname;
	const char *parkingexten;
	struct parkeduser *cur;
	struct ast_parkinglot *parkinglot = NULL;

	if (args->parkinglot) {
		parkinglot = parkinglot_addref(args->parkinglot);
		parkinglotname = parkinglot->name;
	} else {
		if (parker) {
			parkinglotname = findparkinglotname(parker);
		} else { /* parker was NULL, check park_me (ParkAndAnnounce / res_agi) */
			parkinglotname = findparkinglotname(park_me);
		}
		if (!ast_strlen_zero(parkinglotname)) {
			parkinglot = find_parkinglot(parkinglotname);
		} else {
			/* Parking lot is not specified, so use the default parking lot. */
			ast_debug(4, "This could be an indication channel driver needs updating, using default lot.\n");
			parkinglot = parkinglot_addref(default_parkinglot);
		}
	}

	/* Dynamically create parkinglot */
	if (!parkinglot && parkeddynamic && !ast_strlen_zero(parkinglotname)) {
		parkinglot = create_dynamic_parkinglot(parkinglotname, park_me);
	}

	if (!parkinglot) {
		ast_log(LOG_WARNING, "Parking lot not available to park %s.\n", park_me->name);
		return NULL;
	}

	ast_debug(1, "Parking lot: %s\n", parkinglot->name);
	if (parkinglot->disabled || parkinglot->cfg.is_invalid) {
		ast_log(LOG_WARNING, "Parking lot %s is not in a useable state.\n",
			parkinglot->name);
		parkinglot_unref(parkinglot);
		return NULL;
	}

	/* Allocate memory for parking data */
	if (!(pu = ast_calloc(1, sizeof(*pu)))) {
		parkinglot_unref(parkinglot);
		return NULL;
	}

	/* Lock parking list */
	AST_LIST_LOCK(&parkinglot->parkings);

	/* Check for channel variable PARKINGEXTEN */
	parkingexten = ast_strdupa(S_OR(pbx_builtin_getvar_helper(park_me, "PARKINGEXTEN"), ""));
	if (!ast_strlen_zero(parkingexten)) {
		/*!
		 * \note The API forces us to specify a numeric parking slot, even
		 * though the architecture would tend to support non-numeric extensions
		 * (as are possible with SIP, for example).  Hence, we enforce that
		 * limitation here.  If extout was not numeric, we could permit
		 * arbitrary non-numeric extensions.
		 */
		if (sscanf(parkingexten, "%30d", &parking_space) != 1 || parking_space <= 0) {
			ast_log(LOG_WARNING, "PARKINGEXTEN='%s' is not a valid parking space.\n",
				parkingexten);
			AST_LIST_UNLOCK(&parkinglot->parkings);
			parkinglot_unref(parkinglot);
			ast_free(pu);
			return NULL;
		}

		if (parking_space < parkinglot->cfg.parking_start
			|| parkinglot->cfg.parking_stop < parking_space) {
			/*
			 * Cannot allow park because parking lots are not setup for
			 * spaces outside of the lot.  (Things like dialplan hints don't
			 * exist for outside lot space.)
			 */
			ast_log(LOG_WARNING, "PARKINGEXTEN=%d is not in %s (%d-%d).\n",
				parking_space, parkinglot->name, parkinglot->cfg.parking_start,
				parkinglot->cfg.parking_stop);
			AST_LIST_UNLOCK(&parkinglot->parkings);
			parkinglot_unref(parkinglot);
			ast_free(pu);
			return NULL;
		}

		/* Check if requested parking space is in use. */
		AST_LIST_TRAVERSE(&parkinglot->parkings, cur, list) {
			if (cur->parkingnum == parking_space) {
				ast_log(LOG_WARNING, "PARKINGEXTEN=%d is already in use in %s\n",
					parking_space, parkinglot->name);
				AST_LIST_UNLOCK(&parkinglot->parkings);
				parkinglot_unref(parkinglot);
				ast_free(pu);
				return NULL;
			}
		}
	} else {
		/* PARKINGEXTEN is empty, so find a usable extension in the lot to park the call */
		int start; /* The first slot we look in the parkinglot. It can be randomized. */
		int start_checked = 0; /* flag raised once the first slot is checked */

		/* If using randomize mode, set start to random position on parking range */
		if (ast_test_flag(args, AST_PARK_OPT_RANDOMIZE)) {
			start = ast_random() % (parkinglot->cfg.parking_stop - parkinglot->cfg.parking_start + 1);
			start += parkinglot->cfg.parking_start;
		} else if (parkinglot->cfg.parkfindnext
			&& parkinglot->cfg.parking_start <= parkinglot->next_parking_space
			&& parkinglot->next_parking_space <= parkinglot->cfg.parking_stop) {
			/* Start looking with the next parking space in the lot. */
			start = parkinglot->next_parking_space;
		} else {
			/* Otherwise, just set it to the start position. */
			start = parkinglot->cfg.parking_start;
		}

		/* free parking extension linear search: O(n^2) */
		for (i = start; ; i++) {
			/* If we are past the end, wrap around to the first parking slot*/
			if (i == parkinglot->cfg.parking_stop + 1) {
				i = parkinglot->cfg.parking_start;
			}

			if (i == start) {
				/* At this point, if start_checked, we've exhausted all the possible slots. */
				if (start_checked) {
					break;
				} else {
					start_checked = 1;
				}
			}

			/* Search the list of parked calls already in use for i. If we find it, it's in use. */
			AST_LIST_TRAVERSE(&parkinglot->parkings, cur, list) {
				if (cur->parkingnum == i) {
					break;
				}
			}
			if (!cur) {
				/* We found a parking space. */
				parking_space = i;
				break;
			}
		}
		if (parking_space == -1) {
			/* We did not find a parking space.  Lot is full. */
			ast_log(LOG_WARNING, "No more parking spaces in %s\n", parkinglot->name);
			AST_LIST_UNLOCK(&parkinglot->parkings);
			parkinglot_unref(parkinglot);
			ast_free(pu);
			return NULL;
		}
	}

	/* Prepare for next parking space search. */
	parkinglot->next_parking_space = parking_space + 1;

	snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", parking_space);
	pu->notquiteyet = 1;
	pu->parkingnum = parking_space;
	pu->parkinglot = parkinglot;
	AST_LIST_INSERT_TAIL(&parkinglot->parkings, pu, list);

	return pu;
}

/* Park a call */
static int park_call_full(struct ast_channel *chan, struct ast_channel *peer, struct ast_park_call_args *args)
{
	struct parkeduser *pu = args->pu;
	const char *event_from;
	char app_data[AST_MAX_EXTENSION + AST_MAX_CONTEXT];

	if (pu == NULL) {
		args->pu = pu = park_space_reserve(chan, peer, args);
		if (pu == NULL) {
			return -1;
		}
	}

	chan->appl = "Parked Call";
	chan->data = NULL;

	pu->chan = chan;

	/* Put the parked channel on hold if we have two different channels */
	if (chan != peer) {
		if (ast_test_flag(args, AST_PARK_OPT_RINGING)) {
			pu->hold_method = AST_CONTROL_RINGING;
			ast_indicate(pu->chan, AST_CONTROL_RINGING);
		} else {
			pu->hold_method = AST_CONTROL_HOLD;
			ast_indicate_data(pu->chan, AST_CONTROL_HOLD, 
				S_OR(pu->parkinglot->cfg.mohclass, NULL),
				!ast_strlen_zero(pu->parkinglot->cfg.mohclass) ? strlen(pu->parkinglot->cfg.mohclass) + 1 : 0);
		}
	}
	
	pu->start = ast_tvnow();
	pu->parkingtime = (args->timeout > 0) ? args->timeout : pu->parkinglot->cfg.parkingtime;
	if (args->extout)
		*(args->extout) = pu->parkingnum;

	if (peer) { 
		/*
		 * This is so ugly that it hurts, but implementing
		 * get_base_channel() on local channels could have ugly side
		 * effects.  We could have
		 * transferer<->local,1<->local,2<->parking and we need the
		 * callback name to be that of transferer.  Since local,1/2 have
		 * the same name we can be tricky and just grab the bridged
		 * channel from the other side of the local.
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

	/*
	 * Remember what had been dialed, so that if the parking
	 * expires, we try to come back to the same place
	 */
	pu->options_specified = (!ast_strlen_zero(args->return_con) || !ast_strlen_zero(args->return_ext) || args->return_pri);

	/*
	 * If extension has options specified, they override all other
	 * possibilities such as the returntoorigin flag and transferred
	 * context.  Information on extension options is lost here, so
	 * we set a flag
	 */
	ast_copy_string(pu->context, 
		S_OR(args->return_con, S_OR(chan->macrocontext, chan->context)), 
		sizeof(pu->context));
	ast_copy_string(pu->exten, 
		S_OR(args->return_ext, S_OR(chan->macroexten, chan->exten)), 
		sizeof(pu->exten));
	pu->priority = args->return_pri ? args->return_pri : 
		(chan->macropriority ? chan->macropriority : chan->priority);

	/*
	 * If parking a channel directly, don't quite yet get parking
	 * running on it.  All parking lot entries are put into the
	 * parking lot with notquiteyet on.
	 */
	if (peer != chan) {
		pu->notquiteyet = 0;
	}

	/* Wake up the (presumably select()ing) thread */
	pthread_kill(parking_thread, SIGURG);
	ast_verb(2, "Parked %s on %d (lot %s). Will timeout back to extension [%s] %s, %d in %d seconds\n",
		pu->chan->name, pu->parkingnum, pu->parkinglot->name,
		pu->context, pu->exten, pu->priority, (pu->parkingtime / 1000));

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
		"ConnectedLineNum: %s\r\n"
		"ConnectedLineName: %s\r\n"
		"Uniqueid: %s\r\n",
		pu->parkingexten, pu->chan->name, pu->parkinglot->name, event_from ? event_from : "",
		(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL),
		S_COR(pu->chan->caller.id.number.valid, pu->chan->caller.id.number.str, "<unknown>"),
		S_COR(pu->chan->caller.id.name.valid, pu->chan->caller.id.name.str, "<unknown>"),
		S_COR(pu->chan->connected.id.number.valid, pu->chan->connected.id.number.str, "<unknown>"),
		S_COR(pu->chan->connected.id.name.valid, pu->chan->connected.id.name.str, "<unknown>"),
		pu->chan->uniqueid
		);

	if (peer && adsipark && ast_adsi_available(peer)) {
		adsi_announce_park(peer, pu->parkingexten);	/* Only supports parking numbers */
		ast_adsi_unload_session(peer);
	}

	snprintf(app_data, sizeof(app_data), "%s,%s", pu->parkingexten,
		pu->parkinglot->name);
	if (ast_add_extension(pu->parkinglot->cfg.parking_con, 1, pu->parkingexten, 1,
		NULL, NULL, parkedcall, ast_strdup(app_data), ast_free_ptr, registrar)) {
		ast_log(LOG_ERROR, "Could not create parked call exten: %s@%s\n",
			pu->parkingexten, pu->parkinglot->cfg.parking_con);
	} else {
		notify_metermaids(pu->parkingexten, pu->parkinglot->cfg.parking_con, AST_DEVICE_INUSE);
	}

	AST_LIST_UNLOCK(&pu->parkinglot->parkings);

	/* Only say number if it's a number and the channel hasn't been masqueraded away */
	if (peer && !ast_test_flag(args, AST_PARK_OPT_SILENCE)
		&& (ast_strlen_zero(args->orig_chan_name) || !strcasecmp(peer->name, args->orig_chan_name))) {
		/*
		 * If a channel is masqueraded into peer while playing back the
		 * parking space number do not continue playing it back.  This
		 * is the case if an attended transfer occurs.
		 */
		ast_set_flag(peer, AST_FLAG_MASQ_NOSTREAM);
		/* Tell the peer channel the number of the parking space */
		ast_say_digits(peer, pu->parkingnum, "", peer->language);
		ast_clear_flag(peer, AST_FLAG_MASQ_NOSTREAM);
	}
	if (peer == chan) { /* pu->notquiteyet = 1 */
		/* Wake up parking thread if we're really done */
		pu->hold_method = AST_CONTROL_HOLD;
		ast_indicate_data(pu->chan, AST_CONTROL_HOLD, 
			S_OR(pu->parkinglot->cfg.mohclass, NULL),
			!ast_strlen_zero(pu->parkinglot->cfg.mohclass) ? strlen(pu->parkinglot->cfg.mohclass) + 1 : 0);
		pu->notquiteyet = 0;
		pthread_kill(parking_thread, SIGURG);
	}
	return 0;
}

/*! \brief Park a call */
int ast_park_call(struct ast_channel *chan, struct ast_channel *peer, int timeout, const char *parkexten, int *extout)
{
	struct ast_park_call_args args = {
		.timeout = timeout,
		.extout = extout,
	};

	return park_call_full(chan, peer, &args);
}

/*!
 * \param rchan the real channel to be parked
 * \param peer the channel to have the parking read to.
 * \param timeout is a timeout in milliseconds
 * \param extout is a parameter to an int that will hold the parked location, or NULL if you want.
 * \param play_announcement TRUE if to play which parking space call parked in to peer.
 * \param args Optional additional parking options when parking a call.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout, int play_announcement, struct ast_park_call_args *args)
{
	struct ast_channel *chan;
	struct ast_park_call_args park_args = {0,};

	if (!args) {
		args = &park_args;
		args->timeout = timeout;
		args->extout = extout;
	}

	/* Make a new, channel that we'll use to masquerade in the real one */
	chan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, rchan->accountcode, rchan->exten,
		rchan->context, rchan->linkedid, rchan->amaflags, "Parked/%s", rchan->name);
	if (!chan) {
		ast_log(LOG_WARNING, "Unable to create parked channel\n");
		if (peer == rchan) {
			/* Only have one channel to worry about. */
			ast_stream_and_wait(peer, "pbx-parkingfailed", "");
		} else if (peer) {
			/* Have two different channels to worry about. */
			play_message_on_chan(peer, rchan, "failure message", "pbx-parkingfailed");
		}
		return -1;
	}

	args->pu = park_space_reserve(rchan, peer, args);
	if (!args->pu) {
		chan->hangupcause = AST_CAUSE_SWITCH_CONGESTION;
		ast_hangup(chan);
		if (peer == rchan) {
			/* Only have one channel to worry about. */
			ast_stream_and_wait(peer, "pbx-parkingfailed", "");
		} else if (peer) {
			/* Have two different channels to worry about. */
			play_message_on_chan(peer, rchan, "failure message", "pbx-parkingfailed");
		}
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

	/* Manually do the masquerade to make sure it is complete. */
	ast_do_masquerade(chan);

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

int ast_masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout)
{
	return masq_park_call(rchan, peer, timeout, extout, 0, NULL);
}

/*!
 * \brief Park call via masqueraded channel and announce parking spot on peer channel.
 *
 * \param rchan the real channel to be parked
 * \param peer the channel to have the parking read to.
 * \param args Optional additional parking options when parking a call.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int masq_park_call_announce(struct ast_channel *rchan, struct ast_channel *peer, struct ast_park_call_args *args)
{
	return masq_park_call(rchan, peer, 0, NULL, 1, args);
}

static int finishup(struct ast_channel *chan)
{
	ast_indicate(chan, AST_CONTROL_UNHOLD);

	return ast_autoservice_stop(chan);
}

/*!
 * \internal
 * \brief Builtin transfer park call helper.
 *
 * \param park_me Channel to be parked.
 * \param parker Channel parking the call.
 * \param park_exten Parking lot dialplan access ramp extension.
 *
 * \note Assumes park_me is on hold and in autoservice.
 *
 * \retval -1 on successful park.
 * \retval -1 on park_me hangup.
 * \retval AST_FEATURE_RETURN_SUCCESS on error to keep the bridge connected.
 */
static int xfer_park_call_helper(struct ast_channel *park_me, struct ast_channel *parker, struct ast_exten *park_exten)
{
	char *parse;
	const char *app_data;
	const char *pl_name;
	struct ast_park_call_args args = { 0, };
	struct park_app_args app_args;
	int res;

	app_data = ast_get_extension_app_data(park_exten);
	if (!app_data) {
		app_data = "";
	}
	parse = ast_strdupa(app_data);
	AST_STANDARD_APP_ARGS(app_args, parse);

	/* Find the parking lot */
	if (!ast_strlen_zero(app_args.pl_name)) {
		pl_name = app_args.pl_name;
	} else {
		pl_name = findparkinglotname(parker);
	}
	if (ast_strlen_zero(pl_name)) {
		/* Parking lot is not specified, so use the default parking lot. */
		args.parkinglot = parkinglot_addref(default_parkinglot);
	} else {
		args.parkinglot = find_parkinglot(pl_name);
		if (!args.parkinglot && parkeddynamic) {
			args.parkinglot = create_dynamic_parkinglot(pl_name, park_me);
		}
	}

	if (args.parkinglot) {
		/* Park the call */
		res = finishup(park_me);
		if (res) {
			/* park_me hungup on us. */
			parkinglot_unref(args.parkinglot);
			return -1;
		}
		res = masq_park_call_announce(park_me, parker, &args);
		parkinglot_unref(args.parkinglot);
	} else {
		/* Parking failed because parking lot does not exist. */
		ast_stream_and_wait(parker, "pbx-parkingfailed", "");
		finishup(park_me);
		res = -1;
	}

	return res ? AST_FEATURE_RETURN_SUCCESS : -1;
}

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

/*!
 * \brief support routing for one touch call parking
 * \param chan channel parking call
 * \param peer channel to be parked
 * \param config unsed
 * \param code unused
 * \param sense feature options
 * \param data unused
 *
 * \retval -1 on successful park.
 * \retval -1 on chan hangup.
 * \retval AST_FEATURE_RETURN_SUCCESS on error to keep the bridge connected.
 */
static int builtin_parkcall(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	struct ast_channel *parker;
	struct ast_channel *parkee;

	/*
	 * We used to set chan's exten and priority to "s" and 1 here,
	 * but this generates (in some cases) an invalid extension, and
	 * if "s" exists, could errantly cause execution of extensions
	 * you don't expect.  It makes more sense to let nature take its
	 * course when chan finishes, and let the pbx do its thing and
	 * hang up when the park is over.
	 */

	/* Answer if call is not up */
	if (chan->_state != AST_STATE_UP) {
		/*
		 * XXX Why are we doing this?  Both of the channels should be up
		 * since you cannot do DTMF features unless you are bridged.
		 */
		if (ast_answer(chan)) {
			return -1;
		}

		/* Sleep to allow VoIP streams to settle down */
		if (ast_safe_sleep(chan, 1000)) {
			return -1;
		}
	}

	/* one direction used to call park_call.... */
	set_peers(&parker, &parkee, peer, chan, sense);
	return masq_park_call_announce(parkee, parker, NULL)
		? AST_FEATURE_RETURN_SUCCESS : -1;
}

/*!
 * \internal
 * \brief Play file to specified channel.
 *
 * \param play_to Channel to play audiofile to.
 * \param other Channel to put in autoservice while playing file.
 * \param msg Descriptive name of message type being played.
 * \param audiofile Audio file to play.
 *
 * \retval 0 on success.
 * \retval -1 on error. (Couldn't play file, a channel hung up,...)
 */
static int play_message_on_chan(struct ast_channel *play_to, struct ast_channel *other, const char *msg, const char *audiofile)
{
	/* Put other channel in autoservice. */
	if (ast_autoservice_start(other)) {
		return -1;
	}
	ast_autoservice_ignore(other, AST_FRAME_DTMF_BEGIN);
	ast_autoservice_ignore(other, AST_FRAME_DTMF_END);
	if (ast_stream_and_wait(play_to, audiofile, "")) {
		ast_log(LOG_WARNING, "Failed to play %s '%s'!\n", msg, audiofile);
		ast_autoservice_stop(other);
		return -1;
	}
	if (ast_autoservice_stop(other)) {
		return -1;
	}
	return 0;
}

/*!
 * \internal
 * \brief Play file to specified channels.
 *
 * \param left Channel on left to play file.
 * \param right Channel on right to play file.
 * \param which Play file on indicated channels: which < 0 play left, which == 0 play both, which > 0 play right
 * \param msg Descriptive name of message type being played.
 * \param audiofile Audio file to play to channels.
 *
 * \note Plays file to the indicated channels in turn so please
 * don't use this for very long messages.
 *
 * \retval 0 on success.
 * \retval -1 on error. (Couldn't play file, channel hung up,...)
 */
static int play_message_to_chans(struct ast_channel *left, struct ast_channel *right, int which, const char *msg, const char *audiofile)
{
	/* First play the file to the left channel if requested. */
	if (which <= 0 && play_message_on_chan(left, right, msg, audiofile)) {
		return -1;
	}

	/* Then play the file to the right channel if requested. */
	if (which >= 0 && play_message_on_chan(right, left, msg, audiofile)) {
		return -1;
	}

	return 0;
}

/*!
 * \brief Play message to both caller and callee in bridged call, plays synchronously, autoservicing the
 * other channel during the message, so please don't use this for very long messages
 */
static int play_message_in_bridged_call(struct ast_channel *caller_chan, struct ast_channel *callee_chan, const char *audiofile)
{
	return play_message_to_chans(caller_chan, callee_chan, 0, "automon message",
		audiofile);
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
	struct ast_exten *park_exten;
	const char *transferer_real_context;
	char xferto[256] = "";
	int res;

	set_peers(&transferer, &transferee, peer, chan, sense);
	transferer_real_context = real_ctx(transferer, transferee);

	/* Start autoservice on transferee while we talk to the transferer */
	ast_autoservice_start(transferee);
	ast_indicate(transferee, AST_CONTROL_HOLD);

	/* Transfer */
	res = ast_stream_and_wait(transferer, "pbx-transfer", AST_DIGIT_ANY);
	if (res < 0) {
		finishup(transferee);
		return -1; /* error ? */
	}
	if (res > 0) { /* If they've typed a digit already, handle it */
		xferto[0] = (char) res;
	}

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

	park_exten = get_parking_exten(xferto, transferer, transferer_real_context);
	if (park_exten) {
		/* We are transfering the transferee to a parking lot. */
		return xfer_park_call_helper(transferee, transferer, park_exten);
	}

	/* Do blind transfer. */
	ast_cel_report_event(transferer, AST_CEL_BLINDTRANSFER, NULL, xferto, transferee);
	pbx_builtin_setvar_helper(transferer, "BLINDTRANSFER", transferee->name);
	pbx_builtin_setvar_helper(transferee, "BLINDTRANSFER", transferer->name);
	res = finishup(transferee);
	if (!transferer->cdr) { /* this code should never get called (in a perfect world) */
		transferer->cdr = ast_cdr_alloc();
		if (transferer->cdr) {
			ast_cdr_init(transferer->cdr, transferer); /* initialize our channel's cdr */
			ast_cdr_start(transferer->cdr);
		}
	}
	if (transferer->cdr) {
		struct ast_cdr *swap = transferer->cdr;

		ast_log(LOG_DEBUG,
			"transferer=%s; transferee=%s; lastapp=%s; lastdata=%s; chan=%s; dstchan=%s\n",
			transferer->name, transferee->name, transferer->cdr->lastapp,
			transferer->cdr->lastdata, transferer->cdr->channel,
			transferer->cdr->dstchannel);
		ast_log(LOG_DEBUG, "TRANSFEREE; lastapp=%s; lastdata=%s, chan=%s; dstchan=%s\n",
			transferee->cdr->lastapp, transferee->cdr->lastdata, transferee->cdr->channel,
			transferee->cdr->dstchannel);
		ast_log(LOG_DEBUG, "transferer_real_context=%s; xferto=%s\n",
			transferer_real_context, xferto);
		/* swap cdrs-- it will save us some time & work */
		transferer->cdr = transferee->cdr;
		transferee->cdr = swap;
	}
	if (!transferee->pbx) {
		/* Doh!  Use our handy async_goto functions */
		ast_verb(3, "Transferring %s to '%s' (context %s) priority 1\n",
			transferee->name, xferto, transferer_real_context);
		if (ast_async_goto(transferee, transferer_real_context, xferto, 1)) {
			ast_log(LOG_WARNING, "Async goto failed :-(\n");
		}
	} else {
		/* Set the channel's new extension, since it exists, using transferer context */
		ast_set_flag(transferee, AST_FLAG_BRIDGE_HANGUP_DONT); /* don't let the after-bridge code run the h-exten */
		ast_log(LOG_DEBUG,
			"ABOUT TO AST_ASYNC_GOTO, have a pbx... set HANGUP_DONT on chan=%s\n",
			transferee->name);
		if (ast_channel_connected_line_macro(transferee, transferer, &transferer->connected, 1, 0)) {
			ast_channel_update_connected_line(transferer, &transferer->connected, NULL);
		}
		set_c_e_p(transferee, transferer_real_context, xferto, 0);
	}
	check_goto_on_transfer(transferer);
	return res;
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
	struct ast_exten *park_exten;
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
	if (res > 0) { /* If they've typed a digit already, handle it */
		xferto[0] = (char) res;
	}

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

	park_exten = get_parking_exten(xferto, transferer, transferer_real_context);
	if (park_exten) {
		/* We are transfering the transferee to a parking lot. */
		return xfer_park_call_helper(transferee, transferer, park_exten);
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
		transferee, "Local", ast_best_codec(transferer->nativeformats), xferto,
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
					ast_best_codec(transferee->nativeformats), transferer_name,
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
					ast_best_codec(transferee->nativeformats), xferto,
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
	struct ast_flags *features, feature_interpret_op operation, struct ast_call_feature *feature)
{
	int x;
	struct feature_group *fg = NULL;
	struct feature_group_exten *fge;
	struct ast_call_feature *tmpfeature;
	char *tmp, *tok;
	int res = AST_FEATURE_RETURN_PASSDIGITS;
	int feature_detected = 0;

	if (!(peer && chan && config) && operation == FEATURE_INTERPRET_DO) {
		return -1; /* can not run feature operation */
	}

	ast_rwlock_rdlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if ((ast_test_flag(features, builtin_features[x].feature_mask)) &&
		    !ast_strlen_zero(builtin_features[x].exten)) {
			/* Feature is up for consideration */
			if (!strcmp(builtin_features[x].exten, code)) {
				ast_debug(3, "Feature detected: fname=%s sname=%s exten=%s\n", builtin_features[x].fname, builtin_features[x].sname, builtin_features[x].exten);
				if (operation == FEATURE_INTERPRET_CHECK) {
					res = AST_FEATURE_RETURN_SUCCESS; /* We found something */
				} else if (operation == FEATURE_INTERPRET_DO) {
					res = builtin_features[x].operation(chan, peer, config, code, sense, NULL);
				}
				if (feature) {
					memcpy(feature, &builtin_features[x], sizeof(feature));
				}
				feature_detected = 1;
				break;
			} else if (!strncmp(builtin_features[x].exten, code, strlen(code))) {
				if (res == AST_FEATURE_RETURN_PASSDIGITS) {
					res = AST_FEATURE_RETURN_STOREDIGITS;
				}
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
			if (operation == FEATURE_INTERPRET_CHECK) {
				res = AST_FEATURE_RETURN_SUCCESS; /* We found something */
			} else if (operation == FEATURE_INTERPRET_DO) {
				res = tmpfeature->operation(chan, peer, config, code, sense, tmpfeature);
			}
			if (feature) {
				memcpy(feature, tmpfeature, sizeof(feature));
			}
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

	return feature_interpret_helper(chan, peer, config, code, sense, dynamic_features_buf, &features, FEATURE_INTERPRET_DO, &feature);
}


int ast_feature_detect(struct ast_channel *chan, struct ast_flags *features, const char *code, struct ast_call_feature *feature) {

	return feature_interpret_helper(chan, NULL, NULL, code, 0, NULL, features, FEATURE_INTERPRET_DETECT, feature);
}

/*! \brief Check if a feature exists */
static int feature_check(struct ast_channel *chan, struct ast_flags *features, char *code) {
	char *chan_dynamic_features;
	ast_channel_lock(chan);
	chan_dynamic_features = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "DYNAMIC_FEATURES"),""));
	ast_channel_unlock(chan);

	return feature_interpret_helper(chan, NULL, NULL, code, 0, chan_dynamic_features, features, FEATURE_INTERPRET_CHECK, NULL);
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
	struct ast_channel *transferee, const char *type, format_t format, void *data,
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
	struct ast_frame *f;
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_frames;

	caller_hungup = ast_check_hangup(caller);

	if (!(chan = ast_request(type, format, requestor, data, &cause))) {
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
				set_kill_chan_tech(caller);
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
				state = 0;
				chan = ast_call_forward(caller, chan, NULL, format, NULL, &state);
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

static void clear_dialed_interfaces(struct ast_channel *chan)
{
	struct ast_datastore *di_datastore;

	ast_channel_lock(chan);
	if ((di_datastore = ast_channel_datastore_find(chan, &dialed_interface_info, NULL))) {
		if (option_debug) {
			ast_log(LOG_DEBUG, "Removing dialed interfaces datastore on %s since we're bridging\n", chan->name);
		}
		if (!ast_channel_datastore_remove(chan, di_datastore)) {
			ast_datastore_free(di_datastore);
		}
	}
	ast_channel_unlock(chan);
}

/*!
 * \brief bridge the call and set CDR
 *
 * \param chan The bridge considers this channel the caller.
 * \param peer The bridge considers this channel the callee.
 * \param config Configuration for this bridge.
 *
 * Set start time, check for two channels,check if monitor on
 * check for feature activation, create new CDR
 * \retval res on success.
 * \retval -1 on failure to bridge.
 */
int ast_bridge_call(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_frame *f;
	struct ast_channel *who;
	char chan_featurecode[FEATURE_MAX_LEN + 1]="";
	char peer_featurecode[FEATURE_MAX_LEN + 1]="";
	char orig_channame[AST_CHANNEL_NAME];
	char orig_peername[AST_CHANNEL_NAME];
	int res;
	int diff;
	int hasfeatures=0;
	int hadfeatures=0;
	int autoloopflag;
	int sendingdtmfdigit = 0;
	int we_disabled_peer_cdr = 0;
	struct ast_option_header *aoh;
	struct ast_cdr *bridge_cdr = NULL;
	struct ast_cdr *chan_cdr = chan->cdr; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *peer_cdr = peer->cdr; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *new_chan_cdr = NULL; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *new_peer_cdr = NULL; /* the proper chan cdr, if there are forked cdrs */
	struct ast_silence_generator *silgen = NULL;
	const char *h_context;

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
		ast_debug(4,"bridge answer set, chan answer set\n");
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

	/* If we are bridging a call, stop worrying about forwarding loops. We presume that if
	 * a call is being bridged, that the humans in charge know what they're doing. If they
	 * don't, well, what can we do about that? */
	clear_dialed_interfaces(chan);
	clear_dialed_interfaces(peer);

	for (;;) {
		struct ast_channel *other;	/* used later */
	
		res = ast_channel_bridge(chan, peer, config, &f, &who);

		if (ast_test_flag(chan, AST_FLAG_ZOMBIE)
			|| ast_test_flag(peer, AST_FLAG_ZOMBIE)) {
			/* Zombies are present time to leave! */
			res = -1;
			if (f) {
				ast_frfree(f);
			}
			goto before_you_go;
		}

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
				/* Forward option Requests, but only ones we know are safe
				 * These are ONLY sent by chan_iax2 and I'm not convinced that
				 * they are useful. I haven't deleted them entirely because I
				 * just am not sure of the ramifications of removing them. */
				if (aoh && aoh->flag == AST_OPTION_FLAG_REQUEST) {
				   	switch (ntohs(aoh->option)) {
					case AST_OPTION_TONE_VERIFY:
					case AST_OPTION_TDD:
					case AST_OPTION_RELAXDTMF:
					case AST_OPTION_AUDIO_MODE:
					case AST_OPTION_DIGIT_DETECT:
					case AST_OPTION_FAX_DETECT:
						ast_channel_setoption(other, ntohs(aoh->option), aoh->data, 
							f->datalen - sizeof(struct ast_option_header), 0);
					}
				}
				break;
			}
		} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
			struct ast_flags *cfg;
			char dtmfcode[2] = { f->subclass.integer, };
			size_t featurelen;

			if (who == chan) {
				featurelen = strlen(chan_featurecode);
				cfg = &(config->features_caller);
			} else {
				featurelen = strlen(peer_featurecode);
				cfg = &(config->features_callee);
			}
			/* Take a peek if this (possibly) matches a feature. If not, just pass this
			 * DTMF along untouched. If this is not the first digit of a multi-digit code
			 * then we need to fall through and stream the characters if it matches */
			if (featurelen == 0
				&& feature_check(chan, cfg, &dtmfcode[0]) == AST_FEATURE_RETURN_PASSDIGITS) {
				if (option_debug > 3) {
					ast_log(LOG_DEBUG, "Passing DTMF through, since it is not a feature code\n");
				}
				ast_write(other, f);
				sendingdtmfdigit = 1;
			} else {
				/* If ast_opt_transmit_silence is set, then we need to make sure we are
				 * transmitting something while we hold on to the DTMF waiting for a
				 * feature. */
				if (!silgen && ast_opt_transmit_silence) {
					silgen = ast_channel_start_silence_generator(other);
				}
				if (option_debug > 3) {
					ast_log(LOG_DEBUG, "Not passing DTMF through, since it may be a feature code\n");
				}
			}
		} else if (f->frametype == AST_FRAME_DTMF_END) {
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

			if (sendingdtmfdigit == 1) {
				/* We let the BEGIN go through happily, so let's not bother with the END,
				 * since we already know it's not something we bother with */
				ast_write(other, f);
				sendingdtmfdigit = 0;
			} else {
				/*! append the event to featurecode. we rely on the string being zero-filled, and
				 * not overflowing it. 
				 * \todo XXX how do we guarantee the latter ?
				 */
				featurecode[strlen(featurecode)] = f->subclass.integer;
				/* Get rid of the frame before we start doing "stuff" with the channels */
				ast_frfree(f);
				f = NULL;
				if (silgen) {
					ast_channel_stop_silence_generator(other, silgen);
					silgen = NULL;
				}
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
						/* No warning next time - we are waiting for feature code */
						ast_set_flag(config, AST_FEATURE_WARNING_ACTIVE);
					}
					config->feature_start_time = ast_tvnow();
					config->feature_timer = featuredigittimeout;
					ast_debug(1, "Set feature timer to %ld ms\n", config->feature_timer);
				}
			}
		}
		if (f)
			ast_frfree(f);
	}
	ast_cel_report_event(chan, AST_CEL_BRIDGE_END, NULL, NULL, NULL);

before_you_go:
	/* Just in case something weird happened and we didn't clean up the silence generator... */
	if (silgen) {
		ast_channel_stop_silence_generator(who == chan ? peer : chan, silgen);
		silgen = NULL;
	}

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
	if (ast_test_flag(&config->features_caller, AST_FEATURE_NO_H_EXTEN)) {
		h_context = NULL;
	} else if (ast_exists_extension(chan, chan->context, "h", 1,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))) {
		h_context = chan->context;
	} else if (!ast_strlen_zero(chan->macrocontext)
		&& ast_exists_extension(chan, chan->macrocontext, "h", 1,
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))) {
		h_context = chan->macrocontext;
	} else {
		h_context = NULL;
	}
	if (h_context) {
		struct ast_cdr *swapper = NULL;
		char savelastapp[AST_MAX_EXTENSION];
		char savelastdata[AST_MAX_EXTENSION];
		char save_context[AST_MAX_CONTEXT];
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
		ast_copy_string(save_context, chan->context, sizeof(save_context));
		ast_copy_string(save_exten, chan->exten, sizeof(save_exten));
		save_prio = chan->priority;
		if (h_context != chan->context) {
			ast_copy_string(chan->context, h_context, sizeof(chan->context));
		}
		ast_copy_string(chan->exten, "h", sizeof(chan->exten));
		chan->priority = 1;
		ast_channel_unlock(chan);

		while ((spawn_error = ast_spawn_extension(chan, chan->context, chan->exten,
			chan->priority,
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL),
			&found, 1)) == 0) {
			chan->priority++;
		}
		if (found && spawn_error) {
			/* Something bad happened, or a hangup has been requested. */
			ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
			ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
		}

		/* swap it back */
		ast_channel_lock(chan);
		ast_copy_string(chan->context, save_context, sizeof(chan->context));
		ast_copy_string(chan->exten, save_exten, sizeof(chan->exten));
		chan->priority = save_prio;
		if (bridge_cdr) {
			if (chan->cdr == bridge_cdr) {
				chan->cdr = swapper;
			} else {
				bridge_cdr = NULL;
			}
		}
		/* An "h" exten has been run, so indicate that one has been run. */
		ast_set_flag(chan, AST_FLAG_BRIDGE_HANGUP_RUN);
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
		"ConnectedLineNum: %s\r\n"
		"ConnectedLineName: %s\r\n"
		"UniqueID: %s\r\n",
		pu->parkingexten, 
		pu->chan->name,
		pu->parkinglot->name,
		S_COR(pu->chan->caller.id.number.valid, pu->chan->caller.id.number.str, "<unknown>"),
		S_COR(pu->chan->caller.id.name.valid, pu->chan->caller.id.name.str, "<unknown>"),
		S_COR(pu->chan->connected.id.number.valid, pu->chan->connected.id.number.str, "<unknown>"),
		S_COR(pu->chan->connected.id.name.valid, pu->chan->connected.id.name.str, "<unknown>"),
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

/*!
 * \internal
 * \brief Run management on a parked call.
 *
 * \note The parkinglot parkings list is locked on entry.
 *
 * \retval TRUE if the parking completed.
 */
static int manage_parked_call(struct parkeduser *pu, const struct pollfd *pfds, int nfds, struct pollfd **new_pfds, int *new_nfds, int *ms)
{
	struct ast_channel *chan = pu->chan;	/* shorthand */
	int tms;        /* timeout for this item */
	int x;          /* fd index in channel */
	int parking_complete = 0;

	tms = ast_tvdiff_ms(ast_tvnow(), pu->start);
	if (tms > pu->parkingtime) {
		/*
		 * Call has been parked too long.
		 * Stop entertaining the caller.
		 */
		switch (pu->hold_method) {
		case AST_CONTROL_HOLD:
			ast_indicate(pu->chan, AST_CONTROL_UNHOLD);
			break;
		case AST_CONTROL_RINGING:
			ast_indicate(pu->chan, -1);
			break;
		default:
			break;
		}
		pu->hold_method = 0;

		/* Get chan, exten from derived kludge */
		if (pu->peername[0]) {
			char *peername;
			char *dash;
			char *peername_flat; /* using something like DAHDI/52 for an extension name is NOT a good idea */
			int i;

			peername = ast_strdupa(pu->peername);
			dash = strrchr(peername, '-');
			if (dash) {
				*dash = '\0';
			}

			peername_flat = ast_strdupa(peername);
			for (i = 0; peername_flat[i]; i++) {
				if (peername_flat[i] == '/') {
					peername_flat[i] = '_';
				}
			}

			if (!ast_context_find_or_create(NULL, NULL, parking_con_dial, registrar)) {
				ast_log(LOG_ERROR,
					"Parking dial context '%s' does not exist and unable to create\n",
					parking_con_dial);
			} else {
				char returnexten[AST_MAX_EXTENSION];
				struct ast_datastore *features_datastore;
				struct ast_dial_features *dialfeatures;

				if (!strncmp(peername, "Parked/", 7)) {
					peername += 7;
				}

				ast_channel_lock(chan);
				features_datastore = ast_channel_datastore_find(chan, &dial_features_info,
					NULL);
				if (features_datastore && (dialfeatures = features_datastore->data)) {
					char buf[MAX_DIAL_FEATURE_OPTIONS] = {0,};

					snprintf(returnexten, sizeof(returnexten), "%s,30,%s", peername,
						callback_dialoptions(&(dialfeatures->features_callee),
							&(dialfeatures->features_caller), buf, sizeof(buf)));
				} else { /* Existing default */
					ast_log(LOG_NOTICE, "Dial features not found on %s, using default!\n",
						chan->name);
					snprintf(returnexten, sizeof(returnexten), "%s,30,t", peername);
				}
				ast_channel_unlock(chan);

				if (ast_add_extension(parking_con_dial, 1, peername_flat, 1, NULL, NULL,
					"Dial", ast_strdup(returnexten), ast_free_ptr, registrar)) {
					ast_log(LOG_ERROR,
						"Could not create parking return dial exten: %s@%s\n",
						peername_flat, parking_con_dial);
				}
			}
			if (pu->options_specified) {
				/*
				 * Park() was called with overriding return arguments, respect
				 * those arguments.
				 */
				set_c_e_p(chan, pu->context, pu->exten, pu->priority);
			} else if (comebacktoorigin) {
				set_c_e_p(chan, parking_con_dial, peername_flat, 1);
			} else {
				char parkingslot[AST_MAX_EXTENSION];

				snprintf(parkingslot, sizeof(parkingslot), "%d", pu->parkingnum);
				pbx_builtin_setvar_helper(chan, "PARKINGSLOT", parkingslot);
				set_c_e_p(chan, "parkedcallstimeout", peername_flat, 1);
			}
		} else {
			/*
			 * They've been waiting too long, send them back to where they
			 * came.  Theoretically they should have their original
			 * extensions and such, but we copy to be on the safe side.
			 */
			set_c_e_p(chan, pu->context, pu->exten, pu->priority);
		}
		post_manager_event("ParkedCallTimeOut", pu);
		ast_cel_report_event(pu->chan, AST_CEL_PARK_END, NULL, "ParkedCallTimeOut", NULL);

		ast_verb(2, "Timeout for %s parked on %d (%s). Returning to %s,%s,%d\n",
			pu->chan->name, pu->parkingnum, pu->parkinglot->name, pu->chan->context,
			pu->chan->exten, pu->chan->priority);

		/* Start up the PBX, or hang them up */
		if (ast_pbx_start(chan))  {
			ast_log(LOG_WARNING,
				"Unable to restart the PBX for user on '%s', hanging them up...\n",
				pu->chan->name);
			ast_hangup(chan);
		}

		/* And take them out of the parking lot */
		parking_complete = 1;
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
			if (!f || (f->frametype == AST_FRAME_CONTROL
				&& f->subclass.integer == AST_CONTROL_HANGUP)) {
				if (f) {
					ast_frfree(f);
				}
				post_manager_event("ParkedCallGiveUp", pu);
				ast_cel_report_event(pu->chan, AST_CEL_PARK_END, NULL, "ParkedCallGiveUp",
					NULL);

				/* There's a problem, hang them up */
				ast_verb(2, "%s got tired of being parked\n", chan->name);
				ast_hangup(chan);

				/* And take them out of the parking lot */
				parking_complete = 1;
				break;
			} else {
				/* XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
				ast_frfree(f);
				if (pu->hold_method == AST_CONTROL_HOLD
					&& pu->moh_trys < 3
					&& !chan->generatordata) {
					ast_debug(1,
						"MOH on parked call stopped by outside source.  Restarting on channel %s.\n",
						chan->name);
					ast_indicate_data(chan, AST_CONTROL_HOLD,
						S_OR(pu->parkinglot->cfg.mohclass, NULL),
						(!ast_strlen_zero(pu->parkinglot->cfg.mohclass)
							? strlen(pu->parkinglot->cfg.mohclass) + 1 : 0));
					pu->moh_trys++;
				}
				goto std;	/* XXX Ick: jumping into an else statement??? XXX */
			}
		} /* End for */
		if (x >= AST_MAX_FDS) {
std:		for (x = 0; x < AST_MAX_FDS; x++) {	/* mark fds for next round */
				if (chan->fds[x] > -1) {
					void *tmp = ast_realloc(*new_pfds,
						(*new_nfds + 1) * sizeof(struct pollfd));

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

	return parking_complete;
}

/*! \brief Run management on parkinglots, called once per parkinglot */
static void manage_parkinglot(struct ast_parkinglot *curlot, const struct pollfd *pfds, int nfds, struct pollfd **new_pfds, int *new_nfds, int *ms)
{
	struct parkeduser *pu;
	struct ast_context *con;

	/* Lock parkings list */
	AST_LIST_LOCK(&curlot->parkings);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&curlot->parkings, pu, list) {
		if (pu->notquiteyet) { /* Pretend this one isn't here yet */
			continue;
		}
		if (manage_parked_call(pu, pfds, nfds, new_pfds, new_nfds, ms)) {
			/* Parking is complete for this call so remove it from the parking lot. */
			con = ast_context_find(pu->parkinglot->cfg.parking_con);
			if (con) {
				if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0)) {
					ast_log(LOG_WARNING,
						"Whoa, failed to remove the parking extension %s@%s!\n",
						pu->parkingexten, pu->parkinglot->cfg.parking_con);
				}
				notify_metermaids(pu->parkingexten, pu->parkinglot->cfg.parking_con,
					AST_DEVICE_NOT_INUSE);
			} else {
				ast_log(LOG_WARNING,
					"Whoa, parking lot '%s' context '%s' does not exist.\n",
					pu->parkinglot->name, pu->parkinglot->cfg.parking_con);
			}
			AST_LIST_REMOVE_CURRENT(list);
			parkinglot_unref(pu->parkinglot);
			ast_free(pu);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&curlot->parkings);
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
static struct ast_parkinglot *find_parkinglot(const char *name)
{
	struct ast_parkinglot *parkinglot;

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	parkinglot = ao2_find(parkinglots, (void *) name, 0);
	if (parkinglot) {
		ast_debug(1, "Found Parking lot: %s\n", parkinglot->name);
	}

	return parkinglot;
}

/*! \brief Copy parkinglot and store it with new name */
static struct ast_parkinglot *copy_parkinglot(const char *name, const struct ast_parkinglot *parkinglot)
{
	struct ast_parkinglot *copylot;

	if ((copylot = find_parkinglot(name))) { /* Parkinglot with that name already exists */
		ao2_ref(copylot, -1);
		return NULL;
	}

	copylot = create_parkinglot(name);
	if (!copylot) {
		return NULL;
	}

	ast_debug(1, "Building parking lot %s\n", name);

	/* Copy the source parking lot configuration. */
	copylot->cfg = parkinglot->cfg;

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
	struct ast_park_call_args args = {
		.orig_chan_name = orig_chan_name,
	};
	struct ast_flags flags = { 0 };
	char orig_exten[AST_MAX_EXTENSION];
	int orig_priority;
	int res;
	const char *pl_name;
	char *parse;
	struct park_app_args app_args;

	/* Answer if call is not up */
	if (chan->_state != AST_STATE_UP) {
		if (ast_answer(chan)) {
			return -1;
		}

		/* Sleep to allow VoIP streams to settle down */
		if (ast_safe_sleep(chan, 1000)) {
			return -1;
		}
	}

	/* Process the dialplan application options. */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(app_args, parse);

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

	ast_app_parse_options(park_call_options, &flags, NULL, app_args.options);
	args.flags = flags.flags;

	/*
	 * Setup the exten/priority to be s/1 since we don't know where
	 * this call should return.
	 */
	ast_copy_string(orig_exten, chan->exten, sizeof(orig_exten));
	orig_priority = chan->priority;
	strcpy(chan->exten, "s");
	chan->priority = 1;

	/* Park the call */
	if (!ast_strlen_zero(app_args.pl_name)) {
		pl_name = app_args.pl_name;
	} else {
		pl_name = findparkinglotname(chan);
	}
	if (ast_strlen_zero(pl_name)) {
		/* Parking lot is not specified, so use the default parking lot. */
		args.parkinglot = parkinglot_addref(default_parkinglot);
	} else {
		args.parkinglot = find_parkinglot(pl_name);
		if (!args.parkinglot && parkeddynamic) {
			args.parkinglot = create_dynamic_parkinglot(pl_name, chan);
		}
	}
	if (args.parkinglot) {
		res = masq_park_call_announce(chan, chan, &args);
		parkinglot_unref(args.parkinglot);
	} else {
		/* Parking failed because the parking lot does not exist. */
		ast_stream_and_wait(chan, "pbx-parkingfailed", "");
		res = -1;
	}
	if (res) {
		/* Park failed, try to continue in the dialplan. */
		ast_copy_string(chan->exten, orig_exten, sizeof(chan->exten));
		chan->priority = orig_priority;
		res = 0;
	} else {
		/* Park succeeded. */
		res = 1;
	}

	return res;
}

/*! \brief Pickup parked call */
static int parked_call_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_channel *peer = NULL;
	struct parkeduser *pu;
	struct ast_context *con;
	char *parse;
	const char *pl_name;
	int park = 0;
	struct ast_bridge_config config;
	struct ast_parkinglot *parkinglot;
	AST_DECLARE_APP_ARGS(app_args,
		AST_APP_ARG(pl_space);	/*!< Parking lot space to retrieve if present. */
		AST_APP_ARG(pl_name);	/*!< Parking lot name to use if present. */
		AST_APP_ARG(dummy);		/*!< Place to put any remaining args string. */
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(app_args, parse);

	if (!ast_strlen_zero(app_args.pl_space)) {
		if (sscanf(app_args.pl_space, "%30u", &park) != 1) {
			ast_log(LOG_WARNING, "Specified parking extension not a number: %s\n",
				app_args.pl_space);
			park = -1;
		}
	}

	if (!ast_strlen_zero(app_args.pl_name)) {
		pl_name = app_args.pl_name;
	} else {
		pl_name = findparkinglotname(chan);
	}
	if (ast_strlen_zero(pl_name)) {
		/* Parking lot is not specified, so use the default parking lot. */
		parkinglot = parkinglot_addref(default_parkinglot);
	} else {
		parkinglot = find_parkinglot(pl_name);
		if (!parkinglot) {
			/* It helps to answer the channel if not already up. :) */
			if (chan->_state != AST_STATE_UP) {
				ast_answer(chan);
			}
			if (ast_stream_and_wait(chan, "pbx-invalidpark", "")) {
				ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n",
					"pbx-invalidpark", chan->name);
			}
			ast_log(LOG_WARNING,
				"Channel %s tried to retrieve parked call from unknown parking lot '%s'\n",
				chan->name, pl_name);
			return -1;
		}
	}

	AST_LIST_LOCK(&parkinglot->parkings);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&parkinglot->parkings, pu, list) {
		if ((ast_strlen_zero(app_args.pl_space) || pu->parkingnum == park)
			&& !pu->notquiteyet && !pu->chan->pbx) {
			/* The parking space has a call and can be picked up now. */
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	if (pu) {
		/* Found a parked call to pickup. */
		peer = pu->chan;
		con = ast_context_find(parkinglot->cfg.parking_con);
		if (con) {
			if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0)) {
				ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
			} else {
				notify_metermaids(pu->parkingexten, parkinglot->cfg.parking_con, AST_DEVICE_NOT_INUSE);
			}
		} else {
			ast_log(LOG_WARNING, "Whoa, no parking context?\n");
		}

		ast_cel_report_event(pu->chan, AST_CEL_PARK_END, NULL, "UnParkedCall", chan);
		ast_manager_event(pu->chan, EVENT_FLAG_CALL, "UnParkedCall",
			"Exten: %s\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n"
			"ConnectedLineNum: %s\r\n"
			"ConnectedLineName: %s\r\n",
			pu->parkingexten, pu->chan->name, chan->name,
			S_COR(pu->chan->caller.id.number.valid, pu->chan->caller.id.number.str, "<unknown>"),
			S_COR(pu->chan->caller.id.name.valid, pu->chan->caller.id.name.str, "<unknown>"),
			S_COR(pu->chan->connected.id.number.valid, pu->chan->connected.id.number.str, "<unknown>"),
			S_COR(pu->chan->connected.id.name.valid, pu->chan->connected.id.name.str, "<unknown>")
			);

		/* Stop entertaining the caller. */
		switch (pu->hold_method) {
		case AST_CONTROL_HOLD:
			ast_indicate(pu->chan, AST_CONTROL_UNHOLD);
			break;
		case AST_CONTROL_RINGING:
			ast_indicate(pu->chan, -1);
			break;
		default:
			break;
		}
		pu->hold_method = 0;

		parkinglot_unref(pu->parkinglot);
		ast_free(pu);
	}
	AST_LIST_UNLOCK(&parkinglot->parkings);

	if (peer) {
		/* Update connected line between retrieving call and parked call. */
		struct ast_party_connected_line connected;

		ast_party_connected_line_init(&connected);

		/* Send our caller-id to peer. */
		ast_channel_lock(chan);
		ast_connected_line_copy_from_caller(&connected, &chan->caller);
		ast_channel_unlock(chan);
		connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
		if (ast_channel_connected_line_macro(chan, peer, &connected, 0, 0)) {
			ast_channel_update_connected_line(peer, &connected, NULL);
		}

		/*
		 * Get caller-id from peer.
		 *
		 * Update the retrieving call before it is answered if possible
		 * for best results.  Some phones do not support updating the
		 * connected line information after connection.
		 */
		ast_channel_lock(peer);
		ast_connected_line_copy_from_caller(&connected, &peer->caller);
		ast_channel_unlock(peer);
		connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
		if (ast_channel_connected_line_macro(peer, chan, &connected, 1, 0)) {
			ast_channel_update_connected_line(chan, &connected, NULL);
		}

		ast_party_connected_line_free(&connected);
	}

	/* JK02: it helps to answer the channel if not already up */
	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (peer) {
		struct ast_datastore *features_datastore;
		struct ast_dial_features *dialfeatures = NULL;

		/* Play a courtesy to the source(s) configured to prefix the bridge connecting */
		if (!ast_strlen_zero(courtesytone)) {
			static const char msg[] = "courtesy tone";

			switch (parkedplay) {
			case 0:/* Courtesy tone to pickup chan */
				res = play_message_to_chans(chan, peer, -1, msg, courtesytone);
				break;
			case 1:/* Courtesy tone to parked chan */
				res = play_message_to_chans(chan, peer, 1, msg, courtesytone);
				break;
			case 2:/* Courtesy tone to both chans */
				res = play_message_to_chans(chan, peer, 0, msg, courtesytone);
				break;
			default:
				res = 0;
				break;
			}
			if (res) {
				ast_hangup(peer);
				parkinglot_unref(parkinglot);
				return -1;
			}
		}

		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			ast_hangup(peer);
			parkinglot_unref(parkinglot);
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

		/*
		 * When the datastores for both caller and callee are created,
		 * both the callee and caller channels use the features_caller
		 * flag variable to represent themselves.  With that said, the
		 * config.features_callee flags should be copied from the
		 * datastore's caller feature flags regardless if peer was a
		 * callee or caller.
		 */
		if (dialfeatures) {
			ast_copy_flags(&(config.features_callee), &(dialfeatures->features_caller), AST_FLAGS_ALL);
		}
		ast_channel_unlock(peer);

		if ((parkinglot->cfg.parkedcalltransfers == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->cfg.parkedcalltransfers == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
		}
		if ((parkinglot->cfg.parkedcalltransfers == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->cfg.parkedcalltransfers == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
		}
		if ((parkinglot->cfg.parkedcallreparking == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->cfg.parkedcallreparking == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_PARKCALL);
		}
		if ((parkinglot->cfg.parkedcallreparking == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->cfg.parkedcallreparking == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_PARKCALL);
		}
		if ((parkinglot->cfg.parkedcallhangup == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->cfg.parkedcallhangup == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_DISCONNECT);
		}
		if ((parkinglot->cfg.parkedcallhangup == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->cfg.parkedcallhangup == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_DISCONNECT);
		}
		if ((parkinglot->cfg.parkedcallrecording == AST_FEATURE_FLAG_BYCALLEE) || (parkinglot->cfg.parkedcallrecording == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
		}
		if ((parkinglot->cfg.parkedcallrecording == AST_FEATURE_FLAG_BYCALLER) || (parkinglot->cfg.parkedcallrecording == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);
		}

		res = ast_bridge_call(chan, peer, &config);

		pbx_builtin_setvar_helper(chan, "PARKEDCHANNEL", peer->name);
		ast_cdr_setdestchan(chan->cdr, peer->name);

		/* Simulate the PBX hanging up */
		ast_hangup(peer);
	} else {
		if (ast_stream_and_wait(chan, "pbx-invalidpark", "")) {
			ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", "pbx-invalidpark",
				chan->name);
		}
		ast_verb(3, "Channel %s tried to retrieve nonexistent parked call %d\n",
			chan->name, park);
	}

	parkinglot_unref(parkinglot);
	return -1;
}

/*!
 * \brief Unreference parkinglot object.
 */
static void parkinglot_unref(struct ast_parkinglot *parkinglot) 
{
	ast_debug(3, "Multiparking: %s refcount now %d\n", parkinglot->name,
		ao2_ref(parkinglot, 0) - 1);
	ao2_ref(parkinglot, -1);
}

static struct ast_parkinglot *parkinglot_addref(struct ast_parkinglot *parkinglot)
{
	int refcount;

	refcount = ao2_ref(parkinglot, +1);
	ast_debug(3, "Multiparking: %s refcount now %d\n", parkinglot->name, refcount + 1);
	return parkinglot;
}

/*! \brief Destroy a parking lot */
static void parkinglot_destroy(void *obj)
{
	struct ast_parkinglot *doomed = obj;

	/*
	 * No need to destroy parked calls here because any parked call
	 * holds a parking lot reference.  Therefore the parkings list
	 * must be empty.
	 */
	ast_assert(AST_LIST_EMPTY(&doomed->parkings));
	AST_LIST_HEAD_DESTROY(&doomed->parkings);
}

/*! \brief Allocate parking lot structure */
static struct ast_parkinglot *create_parkinglot(const char *name)
{
	struct ast_parkinglot *newlot;

	if (ast_strlen_zero(name)) { /* No name specified */
		return NULL;
	}

	newlot = ao2_alloc(sizeof(*newlot), parkinglot_destroy);
	if (!newlot)
		return NULL;
	
	ast_copy_string(newlot->name, name, sizeof(newlot->name));
	newlot->cfg.is_invalid = 1;/* No config is set yet. */
	AST_LIST_HEAD_INIT(&newlot->parkings);

	return newlot;
}

/*! 
 * \brief Add parking hints for all defined parking spaces.
 * \param context Dialplan context to add the hints.
 * \param start Starting space in parkinglot.
 * \param stop Ending space in parkinglot.
 */
static void park_add_hints(const char *context, int start, int stop)
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

/*! Default configuration for default parking lot. */
static const struct parkinglot_cfg parkinglot_cfg_default_default = {
	.mohclass = "default",
	.parkext = DEFAULT_PARK_EXTENSION,
	.parking_con = "parkedcalls",
	.parking_start = 701,
	.parking_stop = 750,
	.parkingtime = DEFAULT_PARK_TIME,
};

/*! Default configuration for normal parking lots. */
static const struct parkinglot_cfg parkinglot_cfg_default = {
	.parkext = DEFAULT_PARK_EXTENSION,
	.parkingtime = DEFAULT_PARK_TIME,
};

/*!
 * \internal
 * \brief Set parking lot feature flag configuration value.
 *
 * \param pl_name Parking lot name for diagnostic messages.
 * \param param Parameter value to set.
 * \param var Current configuration variable item.
 *
 * \return Nothing
 */
static void parkinglot_feature_flag_cfg(const char *pl_name, int *param, struct ast_variable *var)
{
	ast_debug(1, "Setting parking lot %s %s to %s\n", pl_name, var->name, var->value);
	if (!strcasecmp(var->value, "both")) {
		*param = AST_FEATURE_FLAG_BYBOTH;
	} else if (!strcasecmp(var->value, "caller")) {
		*param = AST_FEATURE_FLAG_BYCALLER;
	} else if (!strcasecmp(var->value, "callee")) {
		*param = AST_FEATURE_FLAG_BYCALLEE;
	}
}

/*!
 * \internal
 * \brief Read parking lot configuration.
 *
 * \param pl_name Parking lot name for diagnostic messages.
 * \param cfg Parking lot config to update that is already initialized with defaults.
 * \param var Config variable list.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int parkinglot_config_read(const char *pl_name, struct parkinglot_cfg *cfg, struct ast_variable *var)
{
	int error = 0;

	while (var) {
		if (!strcasecmp(var->name, "context")) {
			ast_copy_string(cfg->parking_con, var->value, sizeof(cfg->parking_con));
		} else if (!strcasecmp(var->name, "parkext")) {
			ast_copy_string(cfg->parkext, var->value, sizeof(cfg->parkext));
		} else if (!strcasecmp(var->name, "parkext_exclusive")) {
			cfg->parkext_exclusive = ast_true(var->value);
		} else if (!strcasecmp(var->name, "parkinghints")) {
			cfg->parkaddhints = ast_true(var->value);
		} else if (!strcasecmp(var->name, "parkedmusicclass")) {
			ast_copy_string(cfg->mohclass, var->value, sizeof(cfg->mohclass));
		} else if (!strcasecmp(var->name, "parkingtime")) {
			int parkingtime = 0;

			if ((sscanf(var->value, "%30d", &parkingtime) != 1) || parkingtime < 1) {
				ast_log(LOG_WARNING, "%s is not a valid parkingtime\n", var->value);
				error = -1;
			} else {
				cfg->parkingtime = parkingtime * 1000;
			}
		} else if (!strcasecmp(var->name, "parkpos")) {
			int start = 0;
			int end = 0;

			if (sscanf(var->value, "%30d-%30d", &start, &end) != 2) {
				ast_log(LOG_WARNING,
					"Format for parking positions is a-b, where a and b are numbers at line %d of %s\n",
					var->lineno, var->file);
				error = -1;
			} else if (end < start || start <= 0 || end <= 0) {
				ast_log(LOG_WARNING, "Parking range is invalid. Must be a <= b, at line %d of %s\n",
					var->lineno, var->file);
				error = -1;
			} else {
				cfg->parking_start = start;
				cfg->parking_stop = end;
			}
		} else if (!strcasecmp(var->name, "findslot")) {
			cfg->parkfindnext = (!strcasecmp(var->value, "next"));
		} else if (!strcasecmp(var->name, "parkedcalltransfers")) {
			parkinglot_feature_flag_cfg(pl_name, &cfg->parkedcalltransfers, var);
		} else if (!strcasecmp(var->name, "parkedcallreparking")) {
			parkinglot_feature_flag_cfg(pl_name, &cfg->parkedcallreparking, var);
		} else if (!strcasecmp(var->name, "parkedcallhangup")) {
			parkinglot_feature_flag_cfg(pl_name, &cfg->parkedcallhangup, var);
		} else if (!strcasecmp(var->name, "parkedcallrecording")) {
			parkinglot_feature_flag_cfg(pl_name, &cfg->parkedcallrecording, var);
		}
		var = var->next;
	}

	/* Check for configuration errors */
	if (ast_strlen_zero(cfg->parking_con)) {
		ast_log(LOG_WARNING, "Parking lot %s needs context\n", pl_name);
		error = -1;
	}
	if (ast_strlen_zero(cfg->parkext)) {
		ast_log(LOG_WARNING, "Parking lot %s needs parkext\n", pl_name);
		error = -1;
	}
	if (!cfg->parking_start) {
		ast_log(LOG_WARNING, "Parking lot %s needs parkpos\n", pl_name);
		error = -1;
	}
	if (error) {
		cfg->is_invalid = 1;
	}

	return error;
}

/*!
 * \internal
 * \brief Activate the given parkinglot.
 *
 * \param parkinglot Parking lot to activate.
 *
 * \details
 * Insert into the dialplan the context, parking lot access
 * extension, and optional dialplan hints.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int parkinglot_activate(struct ast_parkinglot *parkinglot)
{
	int disabled = 0;
	char app_data[5 + AST_MAX_CONTEXT];

	/* Create Park option list.  Must match with struct park_app_args options. */
	if (parkinglot->cfg.parkext_exclusive) {
		/* Specify the parking lot this parking extension parks calls. */
		snprintf(app_data, sizeof(app_data), ",,,,,%s", parkinglot->name);
	} else {
		/* The dialplan must specify which parking lot to use. */
		app_data[0] = '\0';
	}

	/* Create context */
	if (!ast_context_find_or_create(NULL, NULL, parkinglot->cfg.parking_con, registrar)) {
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n",
			parkinglot->cfg.parking_con);
		disabled = 1;

	/* Add a parking extension into the context */
	} else if (ast_add_extension(parkinglot->cfg.parking_con, 1, parkinglot->cfg.parkext,
		1, NULL, NULL, parkcall, ast_strdup(app_data), ast_free_ptr, registrar)) {
		ast_log(LOG_ERROR, "Could not create parking lot %s access exten %s@%s\n",
			parkinglot->name, parkinglot->cfg.parkext, parkinglot->cfg.parking_con);
		disabled = 1;
	} else {
		/* Add parking hints */
		if (parkinglot->cfg.parkaddhints) {
			park_add_hints(parkinglot->cfg.parking_con, parkinglot->cfg.parking_start,
				parkinglot->cfg.parking_stop);
		}

		/*
		 * XXX Not sure why we should need to notify the metermaids for
		 * this exten.  It was originally done for the default parking
		 * lot entry exten only but should be done for all entry extens
		 * if we do it for one.
		 */
		/* Notify metermaids about parking lot entry exten state. */
		notify_metermaids(parkinglot->cfg.parkext, parkinglot->cfg.parking_con,
			AST_DEVICE_INUSE);
	}

	parkinglot->disabled = disabled;
	return disabled ? -1 : 0;
}

/*! \brief Build parkinglot from configuration and chain it in if it doesn't already exist */
static struct ast_parkinglot *build_parkinglot(const char *pl_name, struct ast_variable *var)
{
	struct ast_parkinglot *parkinglot;
	const struct parkinglot_cfg *cfg_defaults;
	struct parkinglot_cfg new_cfg;
	int cfg_error;
	int oldparkinglot = 0;

	parkinglot = find_parkinglot(pl_name);
	if (parkinglot) {
		oldparkinglot = 1;
	} else {
		parkinglot = create_parkinglot(pl_name);
		if (!parkinglot) {
			return NULL;
		}
	}
	if (!strcmp(parkinglot->name, DEFAULT_PARKINGLOT)) {
		cfg_defaults = &parkinglot_cfg_default_default;
	} else {
		cfg_defaults = &parkinglot_cfg_default;
	}
	new_cfg = *cfg_defaults;

	ast_debug(1, "Building parking lot %s\n", parkinglot->name);

	ao2_lock(parkinglot);

	/* Do some config stuff */
	cfg_error = parkinglot_config_read(parkinglot->name, &new_cfg, var);
	if (oldparkinglot) {
		if (cfg_error) {
			/* Bad configuration read.  Keep using the original config. */
			ast_log(LOG_WARNING, "Changes to parking lot %s are discarded.\n",
				parkinglot->name);
			cfg_error = 0;
		} else if (!AST_LIST_EMPTY(&parkinglot->parkings)
			&& memcmp(&new_cfg, &parkinglot->cfg, sizeof(parkinglot->cfg))) {
			/* Try reloading later when parking lot is empty. */
			ast_log(LOG_WARNING,
				"Parking lot %s has parked calls.  Parking lot changes discarded.\n",
				parkinglot->name);
			force_reload_load = 1;
		} else {
			/* Accept the new config */
			parkinglot->cfg = new_cfg;
		}
	} else {
		/* Load the initial parking lot config. */
		parkinglot->cfg = new_cfg;
	}
	parkinglot->the_mark = 0;

	ao2_unlock(parkinglot);

	if (cfg_error) {
		/* Only new parking lots could have config errors here. */
		ast_log(LOG_WARNING, "New parking lot %s is discarded.\n", parkinglot->name);
		parkinglot_unref(parkinglot);
		return NULL;
	}

	/* Move it into the list, if it wasn't already there */
	if (!oldparkinglot) {
		ao2_link(parkinglots, parkinglot);
	}
	parkinglot_unref(parkinglot);

	return parkinglot;
}

/*!
 * \internal
 * \brief Process an applicationmap section config line.
 *
 * \param var Config variable line.
 *
 * \return Nothing
 */
static void process_applicationmap_line(struct ast_variable *var)
{
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
	if (ast_strlen_zero(args.app)
		|| ast_strlen_zero(args.exten)
		|| ast_strlen_zero(activateon)
		|| ast_strlen_zero(var->name)) {
		ast_log(LOG_NOTICE,
			"Please check the feature Mapping Syntax, either extension, name, or app aren't provided %s %s %s %s\n",
			args.app, args.exten, activateon, var->name);
		return;
	}

	AST_RWLIST_RDLOCK(&feature_list);
	if (find_dynamic_feature(var->name)) {
		AST_RWLIST_UNLOCK(&feature_list);
		ast_log(LOG_WARNING, "Dynamic Feature '%s' specified more than once!\n",
			var->name);
		return;
	}
	AST_RWLIST_UNLOCK(&feature_list);

	if (!(feature = ast_calloc(1, sizeof(*feature)))) {
		return;
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

	/* Allow caller and callee to be specified for backwards compatability */
	if (!strcasecmp(activateon, "self") || !strcasecmp(activateon, "caller")) {
		ast_set_flag(feature, AST_FEATURE_FLAG_ONSELF);
	} else if (!strcasecmp(activateon, "peer") || !strcasecmp(activateon, "callee")) {
		ast_set_flag(feature, AST_FEATURE_FLAG_ONPEER);
	} else {
		ast_log(LOG_NOTICE, "Invalid 'ActivateOn' specification for feature '%s',"
			" must be 'self', or 'peer'\n", var->name);
		return;
	}

	if (ast_strlen_zero(args.activatedby)) {
		ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
	} else if (!strcasecmp(args.activatedby, "caller")) {
		ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLER);
	} else if (!strcasecmp(args.activatedby, "callee")) {
		ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLEE);
	} else if (!strcasecmp(args.activatedby, "both")) {
		ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
	} else {
		ast_log(LOG_NOTICE, "Invalid 'ActivatedBy' specification for feature '%s',"
			" must be 'caller', or 'callee', or 'both'\n", var->name);
		return;
	}

	ast_register_feature(feature);

	ast_verb(2, "Mapping Feature '%s' to app '%s(%s)' with code '%s'\n",
		var->name, args.app, args.app_args, args.exten);
}

static int process_config(struct ast_config *cfg)
{
	int i;
	struct ast_variable *var = NULL;
	struct feature_group *fg = NULL;
	char *ctg; 
	static const char * const categories[] = { 
		/* Categories in features.conf that are not
		 * to be parsed as group categories
		 */
		"general",
		"featuremap",
		"applicationmap"
	};

	/* Set general features global defaults. */
	featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;

	/* Set global call pickup defaults. */
	strcpy(pickup_ext, "*8");
	pickupsound[0] = '\0';
	pickupfailsound[0] = '\0';

	/* Set global call transfer defaults. */
	strcpy(xfersound, "beep");
	strcpy(xferfailsound, "beeperr");
	transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
	atxfernoanswertimeout = DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER;
	atxferloopdelay = DEFAULT_ATXFER_LOOP_DELAY;
	atxferdropcall = DEFAULT_ATXFER_DROP_CALL;
	atxfercallbackretries = DEFAULT_ATXFER_CALLBACK_RETRIES;

	/* Set global call parking defaults. */
	comebacktoorigin = 1;
	courtesytone[0] = '\0';
	parkedplay = 0;
	adsipark = 0;
	parkeddynamic = 0;

	var = ast_variable_browse(cfg, "general");
	build_parkinglot(DEFAULT_PARKINGLOT, var);
	for (; var; var = var->next) {
		if (!strcasecmp(var->name, "parkeddynamic")) {
			parkeddynamic = ast_true(var->value);
		} else if (!strcasecmp(var->name, "adsipark")) {
			adsipark = ast_true(var->value);
		} else if (!strcasecmp(var->name, "transferdigittimeout")) {
			if ((sscanf(var->value, "%30d", &transferdigittimeout) != 1) || (transferdigittimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid transferdigittimeout\n", var->value);
				transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
			} else {
				transferdigittimeout = transferdigittimeout * 1000;
			}
		} else if (!strcasecmp(var->name, "featuredigittimeout")) {
			if ((sscanf(var->value, "%30d", &featuredigittimeout) != 1) || (featuredigittimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid featuredigittimeout\n", var->value);
				featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
			}
		} else if (!strcasecmp(var->name, "atxfernoanswertimeout")) {
			if ((sscanf(var->value, "%30d", &atxfernoanswertimeout) != 1) || (atxfernoanswertimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxfernoanswertimeout\n", var->value);
				atxfernoanswertimeout = DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER;
			} else {
				atxfernoanswertimeout = atxfernoanswertimeout * 1000;
			}
		} else if (!strcasecmp(var->name, "atxferloopdelay")) {
			if ((sscanf(var->value, "%30u", &atxferloopdelay) != 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxferloopdelay\n", var->value);
				atxferloopdelay = DEFAULT_ATXFER_LOOP_DELAY;
			} else {
				atxferloopdelay *= 1000;
			}
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
			if (!strcasecmp(var->value, "both")) {
				parkedplay = 2;
			} else if (!strcasecmp(var->value, "parked")) {
				parkedplay = 1;
			} else {
				parkedplay = 0;
			}
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
		}
	}

	unmap_features();
	for (var = ast_variable_browse(cfg, "featuremap"); var; var = var->next) {
		if (remap_feature(var->name, var->value)) {
			ast_log(LOG_NOTICE, "Unknown feature '%s'\n", var->name);
		}
	}

	/* Map a key combination to an application */
	ast_unregister_features();
	for (var = ast_variable_browse(cfg, "applicationmap"); var; var = var->next) {
		process_applicationmap_line(var);
	}

	ast_unregister_groups();
	AST_RWLIST_WRLOCK(&feature_groups);

	ctg = NULL;
	while ((ctg = ast_category_browse(cfg, ctg))) {
		/* Is this a parkinglot definition ? */
		if (!strncasecmp(ctg, "parkinglot_", strlen("parkinglot_"))) {
			ast_debug(2, "Found configuration section %s, assume parking context\n", ctg);
			if (!build_parkinglot(ctg, ast_variable_browse(cfg, ctg))) {
				ast_log(LOG_ERROR, "Could not build parking lot %s. Configuration error.\n", ctg);
			} else {
				ast_debug(1, "Configured parking context %s\n", ctg);
			}
			continue;	
		}

		/* No, check if it's a group */
		for (i = 0; i < ARRAY_LEN(categories); i++) {
			if (!strcasecmp(categories[i], ctg)) {
				break;
			}
		}
		if (i < ARRAY_LEN(categories)) {
			continue;
		}

		if (!(fg = register_group(ctg))) {
			continue;
		}

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

	return 0;
}

/*!
 * \internal
 * \brief Destroy the given dialplan usage context.
 *
 * \param doomed Parking lot usage context to destroy.
 *
 * \return Nothing
 */
static void destroy_dialplan_usage_context(struct parking_dp_context *doomed)
{
	struct parking_dp_ramp *ramp;
	struct parking_dp_spaces *spaces;

	while ((ramp = AST_LIST_REMOVE_HEAD(&doomed->access_extens, node))) {
		ast_free(ramp);
	}
	while ((spaces = AST_LIST_REMOVE_HEAD(&doomed->spaces, node))) {
		ast_free(spaces);
	}
	while ((spaces = AST_LIST_REMOVE_HEAD(&doomed->hints, node))) {
		ast_free(spaces);
	}
	ast_free(doomed);
}

/*!
 * \internal
 * \brief Destroy the given dialplan usage map.
 *
 * \param doomed Parking lot usage map to destroy.
 *
 * \return Nothing
 */
static void destroy_dialplan_usage_map(struct parking_dp_map *doomed)
{
	struct parking_dp_context *item;

	while ((item = AST_LIST_REMOVE_HEAD(doomed, node))) {
		destroy_dialplan_usage_context(item);
	}
}

/*!
 * \internal
 * \brief Create a new parking lot ramp dialplan usage node.
 *
 * \param exten Parking lot access ramp extension.
 * \param exclusive TRUE if the parking lot access ramp extension is exclusive.
 *
 * \retval New usage ramp node on success.
 * \retval NULL on error.
 */
static struct parking_dp_ramp *build_dialplan_useage_ramp(const char *exten, int exclusive)
{
	struct parking_dp_ramp *ramp_node;

	ramp_node = ast_calloc(1, sizeof(*ramp_node) + strlen(exten));
	if (!ramp_node) {
		return NULL;
	}
	ramp_node->exclusive = exclusive;
	strcpy(ramp_node->exten, exten);
	return ramp_node;
}

/*!
 * \internal
 * \brief Add parking lot access ramp to the context ramp usage map.
 *
 * \param ramp_map Current parking lot context ramp usage map.
 * \param exten Parking lot access ramp extension to add.
 * \param exclusive TRUE if the parking lot access ramp extension is exclusive.
 * \param lot Parking lot supplying reference data.
 * \param complain TRUE if to complain of parking lot ramp conflicts.
 *
 * \retval 0 on success.  The ramp_map is updated.
 * \retval -1 on failure.
 */
static int usage_context_add_ramp(struct parking_dp_ramp_map *ramp_map, const char *exten, int exclusive, struct ast_parkinglot *lot, int complain)
{
	struct parking_dp_ramp *cur_ramp;
	struct parking_dp_ramp *new_ramp;
	int cmp;

	/* Make sure that exclusive is only 0 or 1 */
	if (exclusive) {
		exclusive = 1;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(ramp_map, cur_ramp, node) {
		cmp = strcmp(exten, cur_ramp->exten);
		if (cmp > 0) {
			/* The parking lot ramp goes after this node. */
			continue;
		}
		if (cmp == 0) {
			/* The ramp is already in the map. */
			if (complain && (cur_ramp->exclusive || exclusive)) {
				ast_log(LOG_WARNING,
					"Parking lot '%s' parkext %s@%s used by another parking lot.\n",
					lot->name, exten, lot->cfg.parking_con);
			}
			return 0;
		}
		/* The new parking lot ramp goes before this node. */
		new_ramp = build_dialplan_useage_ramp(exten, exclusive);
		if (!new_ramp) {
			return -1;
		}
		AST_LIST_INSERT_BEFORE_CURRENT(new_ramp, node);
		return 0;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* New parking lot access ramp goes on the end. */
	new_ramp = build_dialplan_useage_ramp(exten, exclusive);
	if (!new_ramp) {
		return -1;
	}
	AST_LIST_INSERT_TAIL(ramp_map, new_ramp, node);
	return 0;
}

/*!
 * \internal
 * \brief Create a new parking lot spaces dialplan usage node.
 *
 * \param start First parking lot space to add.
 * \param stop Last parking lot space to add.
 *
 * \retval New usage ramp node on success.
 * \retval NULL on error.
 */
static struct parking_dp_spaces *build_dialplan_useage_spaces(int start, int stop)
{
	struct parking_dp_spaces *spaces_node;

	spaces_node = ast_calloc(1, sizeof(*spaces_node));
	if (!spaces_node) {
		return NULL;
	}
	spaces_node->start = start;
	spaces_node->stop = stop;
	return spaces_node;
}

/*!
 * \internal
 * \brief Add parking lot spaces to the context space usage map.
 *
 * \param space_map Current parking lot context space usage map.
 * \param start First parking lot space to add.
 * \param stop Last parking lot space to add.
 * \param lot Parking lot supplying reference data.
 * \param complain TRUE if to complain of parking lot spaces conflicts.
 *
 * \retval 0 on success.  The space_map is updated.
 * \retval -1 on failure.
 */
static int usage_context_add_spaces(struct parking_dp_space_map *space_map, int start, int stop, struct ast_parkinglot *lot, int complain)
{
	struct parking_dp_spaces *cur_node;
	struct parking_dp_spaces *expand_node;
	struct parking_dp_spaces *new_node;

	expand_node = NULL;
	AST_LIST_TRAVERSE_SAFE_BEGIN(space_map, cur_node, node) {
		/* NOTE: stop + 1 to combine immediately adjacent nodes into one. */
		if (expand_node) {
			/* The previous node is expanding to possibly eat following nodes. */
			if (expand_node->stop + 1 < cur_node->start) {
				/* Current node is completely after expanding node. */
				return 0;
			}

			if (complain
				&& ((cur_node->start <= start && start <= cur_node->stop)
					|| (cur_node->start <= stop && stop <= cur_node->stop)
					|| (start < cur_node->start && cur_node->stop < stop))) {
				/* Only complain once per range add. */
				complain = 0;
				ast_log(LOG_WARNING,
					"Parking lot '%s' parkpos %d-%d@%s overlaps another parking lot.\n",
					lot->name, start, stop, lot->cfg.parking_con);
			}

			/* Current node is eaten by the expanding node. */
			if (expand_node->stop < cur_node->stop) {
				expand_node->stop = cur_node->stop;
			}
			AST_LIST_REMOVE_CURRENT(node);
			ast_free(cur_node);
			continue;
		}

		if (cur_node->stop + 1 < start) {
			/* New range is completely after current node. */
			continue;
		}
		if (stop + 1 < cur_node->start) {
			/* New range is completely before current node. */
			new_node = build_dialplan_useage_spaces(start, stop);
			if (!new_node) {
				return -1;
			}
			AST_LIST_INSERT_BEFORE_CURRENT(new_node, node);
			return 0;
		}

		if (complain
			&& ((cur_node->start <= start && start <= cur_node->stop)
				|| (cur_node->start <= stop && stop <= cur_node->stop)
				|| (start < cur_node->start && cur_node->stop < stop))) {
			/* Only complain once per range add. */
			complain = 0;
			ast_log(LOG_WARNING,
				"Parking lot '%s' parkpos %d-%d@%s overlaps another parking lot.\n",
				lot->name, start, stop, lot->cfg.parking_con);
		}

		/* Current node range overlaps or is immediately adjacent to new range. */
		if (start < cur_node->start) {
			/* Expand the current node in the front. */
			cur_node->start = start;
		}
		if (stop <= cur_node->stop) {
			/* Current node is not expanding in the rear. */
			return 0;
		}
		cur_node->stop = stop;
		expand_node = cur_node;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (expand_node) {
		/*
		 * The previous node expanded and either ate all following nodes
		 * or it was the last node.
		 */
		return 0;
	}

	/* New range goes on the end. */
	new_node = build_dialplan_useage_spaces(start, stop);
	if (!new_node) {
		return -1;
	}
	AST_LIST_INSERT_TAIL(space_map, new_node, node);
	return 0;
}

/*!
 * \internal
 * \brief Add parking lot spaces to the context dialplan usage node.
 *
 * \param ctx_node Usage node to add parking lot spaces.
 * \param lot Parking lot to add data to ctx_node.
 * \param complain TRUE if to complain of parking lot ramp and spaces conflicts.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int dialplan_usage_add_parkinglot_data(struct parking_dp_context *ctx_node, struct ast_parkinglot *lot, int complain)
{
	if (usage_context_add_ramp(&ctx_node->access_extens, lot->cfg.parkext,
		lot->cfg.parkext_exclusive, lot, complain)) {
		return -1;
	}
	if (usage_context_add_spaces(&ctx_node->spaces, lot->cfg.parking_start,
		lot->cfg.parking_stop, lot, complain)) {
		return -1;
	}
	if (lot->cfg.parkaddhints
		&& usage_context_add_spaces(&ctx_node->hints, lot->cfg.parking_start,
			lot->cfg.parking_stop, lot, 0)) {
		return -1;
	}
	return 0;
}

/*!
 * \internal
 * \brief Create a new parking lot context dialplan usage node.
 *
 * \param lot Parking lot to create a new dialplan usage from.
 *
 * \retval New usage context node on success.
 * \retval NULL on error.
 */
static struct parking_dp_context *build_dialplan_useage_context(struct ast_parkinglot *lot)
{
	struct parking_dp_context *ctx_node;

	ctx_node = ast_calloc(1, sizeof(*ctx_node) + strlen(lot->cfg.parking_con));
	if (!ctx_node) {
		return NULL;
	}
	if (dialplan_usage_add_parkinglot_data(ctx_node, lot, 0)) {
		destroy_dialplan_usage_context(ctx_node);
		return NULL;
	}
	strcpy(ctx_node->context, lot->cfg.parking_con);
	return ctx_node;
}

/*!
 * \internal
 * \brief Add the given parking lot dialplan usage to the dialplan usage map.
 *
 * \param usage_map Parking lot usage map to add the given parking lot.
 * \param lot Parking lot to add dialplan usage.
 * \param complain TRUE if to complain of parking lot ramp and spaces conflicts.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int dialplan_usage_add_parkinglot(struct parking_dp_map *usage_map, struct ast_parkinglot *lot, int complain)
{
	struct parking_dp_context *cur_ctx;
	struct parking_dp_context *new_ctx;
	int cmp;

	AST_LIST_TRAVERSE_SAFE_BEGIN(usage_map, cur_ctx, node) {
		cmp = strcmp(lot->cfg.parking_con, cur_ctx->context);
		if (cmp > 0) {
			/* The parking lot context goes after this node. */
			continue;
		}
		if (cmp == 0) {
			/* This is the node we will add parking lot spaces to the map. */
			return dialplan_usage_add_parkinglot_data(cur_ctx, lot, complain);
		}
		/* The new parking lot context goes before this node. */
		new_ctx = build_dialplan_useage_context(lot);
		if (!new_ctx) {
			return -1;
		}
		AST_LIST_INSERT_BEFORE_CURRENT(new_ctx, node);
		return 0;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* New parking lot context goes on the end. */
	new_ctx = build_dialplan_useage_context(lot);
	if (!new_ctx) {
		return -1;
	}
	AST_LIST_INSERT_TAIL(usage_map, new_ctx, node);
	return 0;
}

/*!
 * \internal
 * \brief Build the dialplan usage map of the current parking lot container.
 *
 * \param usage_map Parking lot usage map.  Must already be initialized.
 * \param complain TRUE if to complain of parking lot ramp and spaces conflicts.
 *
 * \retval 0 on success.  The usage_map is filled in.
 * \retval -1 on failure.  Built usage_map is incomplete.
 */
static int build_dialplan_useage_map(struct parking_dp_map *usage_map, int complain)
{
	int status = 0;
	struct ao2_iterator iter;
	struct ast_parkinglot *curlot;

	/* For all parking lots */
	iter = ao2_iterator_init(parkinglots, 0);
	for (; (curlot = ao2_iterator_next(&iter)); ao2_ref(curlot, -1)) {
		/* Add the parking lot to the map. */
		if (dialplan_usage_add_parkinglot(usage_map, curlot, complain)) {
			ao2_ref(curlot, -1);
			status = -1;
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	return status;
}

/*!
 * \internal
 * \brief Remove the given extension if it exists.
 *
 * \param context Dialplan database context name.
 * \param exten Extension to remove.
 * \param priority Extension priority to remove.
 *
 * \return Nothing
 */
static void remove_exten_if_exist(const char *context, const char *exten, int priority)
{
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */

	if (pbx_find_extension(NULL, NULL, &q, context, exten, priority, NULL, NULL,
		E_MATCH)) {
		ast_debug(1, "Removing unneeded parking lot exten: %s@%s priority:%d\n",
			context, exten, priority);
		ast_context_remove_extension(context, exten, priority, registrar);
	}
}

/*!
 * \internal
 * \brief Remove unused parking lot access ramp items.
 *
 * \param context Dialplan database context name.
 * \param old_ramps Before configuration reload access ramp usage map.
 * \param new_ramps After configuration reload access ramp usage map.
 *
 * \details
 * Remove access ramp items that were in the old context but not in the
 * new context.
 *
 * \return Nothing
 */
static void remove_dead_ramp_usage(const char *context, struct parking_dp_ramp_map *old_ramps, struct parking_dp_ramp_map *new_ramps)
{
	struct parking_dp_ramp *old_ramp;
	struct parking_dp_ramp *new_ramp;
	int cmp;

	old_ramp = AST_LIST_FIRST(old_ramps);
	new_ramp = AST_LIST_FIRST(new_ramps);

	while (new_ramp) {
		if (!old_ramp) {
			/* No old ramps left, so no dead ramps can remain. */
			return;
		}
		cmp = strcmp(old_ramp->exten, new_ramp->exten);
		if (cmp < 0) {
			/* New map does not have old ramp. */
			remove_exten_if_exist(context, old_ramp->exten, 1);
			old_ramp = AST_LIST_NEXT(old_ramp, node);
			continue;
		}
		if (cmp == 0) {
			/* Old and new map have this ramp. */
			old_ramp = AST_LIST_NEXT(old_ramp, node);
		} else {
			/* Old map does not have new ramp. */
		}
		new_ramp = AST_LIST_NEXT(new_ramp, node);
	}

	/* Any old ramps left must be dead. */
	for (; old_ramp; old_ramp = AST_LIST_NEXT(old_ramp, node)) {
		remove_exten_if_exist(context, old_ramp->exten, 1);
	}
}

/*!
 * \internal
 * \brief Destroy the given parking space.
 *
 * \param context Dialplan database context name.
 * \param space Parking space.
 *
 * \return Nothing
 */
static void destroy_space(const char *context, int space)
{
	char exten[AST_MAX_EXTENSION];

	/* Destroy priorities of the parking space that we registered. */
	snprintf(exten, sizeof(exten), "%d", space);
	remove_exten_if_exist(context, exten, PRIORITY_HINT);
	remove_exten_if_exist(context, exten, 1);
}

/*!
 * \internal
 * \brief Remove unused parking lot space items.
 *
 * \param context Dialplan database context name.
 * \param old_spaces Before configuration reload parking space usage map.
 * \param new_spaces After configuration reload parking space usage map.
 * \param destroy_space Function to destroy parking space item.
 *
 * \details
 * Remove parking space items that were in the old context but
 * not in the new context.
 *
 * \return Nothing
 */
static void remove_dead_spaces_usage(const char *context,
	struct parking_dp_space_map *old_spaces, struct parking_dp_space_map *new_spaces,
	void (*destroy_space)(const char *context, int space))
{
	struct parking_dp_spaces *old_range;
	struct parking_dp_spaces *new_range;
	int space;/*!< Current position in the current old range. */
	int stop;

	old_range = AST_LIST_FIRST(old_spaces);
	new_range = AST_LIST_FIRST(new_spaces);
	space = -1;

	while (old_range) {
		if (space < old_range->start) {
			space = old_range->start;
		}
		if (new_range) {
			if (space < new_range->start) {
				/* Current position in old range starts before new range. */
				if (old_range->stop < new_range->start) {
					/* Old range ends before new range. */
					stop = old_range->stop;
					old_range = AST_LIST_NEXT(old_range, node);
				} else {
					/* Tail of old range overlaps new range. */
					stop = new_range->start - 1;
				}
			} else if (/* new_range->start <= space && */ space <= new_range->stop) {
				/* Current position in old range overlaps new range. */
				if (old_range->stop <= new_range->stop) {
					/* Old range ends at or before new range. */
					old_range = AST_LIST_NEXT(old_range, node);
				} else {
					/* Old range extends beyond end of new range. */
					space = new_range->stop + 1;
					new_range = AST_LIST_NEXT(new_range, node);
				}
				continue;
			} else /* if (new_range->stop < space) */ {
				/* Current position in old range starts after new range. */
				new_range = AST_LIST_NEXT(new_range, node);
				continue;
			}
		} else {
			/* No more new ranges.  All remaining old spaces are dead. */
			stop = old_range->stop;
			old_range = AST_LIST_NEXT(old_range, node);
		}

		/* Destroy dead parking spaces. */
		for (; space <= stop; ++space) {
			destroy_space(context, space);
		}
	}
}

/*!
 * \internal
 * \brief Remove unused parking lot context items.
 *
 * \param context Dialplan database context name.
 * \param old_ctx Before configuration reload context usage map.
 * \param new_ctx After configuration reload context usage map.
 *
 * \details
 * Remove context usage items that were in the old context but not in the
 * new context.
 *
 * \return Nothing
 */
static void remove_dead_context_usage(const char *context, struct parking_dp_context *old_ctx, struct parking_dp_context *new_ctx)
{
	remove_dead_ramp_usage(context, &old_ctx->access_extens, &new_ctx->access_extens);
	remove_dead_spaces_usage(context, &old_ctx->spaces, &new_ctx->spaces, destroy_space);
#if 0
	/* I don't think we should destroy hints if the parking space still exists. */
	remove_dead_spaces_usage(context, &old_ctx->hints, &new_ctx->hints, destroy_space_hint);
#endif
}

/*!
 * \internal
 * \brief Remove unused parking lot dialplan items.
 *
 * \param old_map Before configuration reload dialplan usage map.
 * \param new_map After configuration reload dialplan usage map.
 *
 * \details
 * Remove dialplan items that were in the old map but not in the
 * new map.
 *
 * \return Nothing
 */
static void remove_dead_dialplan_useage(struct parking_dp_map *old_map, struct parking_dp_map *new_map)
{
	struct parking_dp_context *old_ctx;
	struct parking_dp_context *new_ctx;
	struct ast_context *con;
	int cmp;

	old_ctx = AST_LIST_FIRST(old_map);
	new_ctx = AST_LIST_FIRST(new_map);

	while (new_ctx) {
		if (!old_ctx) {
			/* No old contexts left, so no dead stuff can remain. */
			return;
		}
		cmp = strcmp(old_ctx->context, new_ctx->context);
		if (cmp < 0) {
			/* New map does not have old map context. */
			con = ast_context_find(old_ctx->context);
			if (con) {
				ast_context_destroy(con, registrar);
			}
			old_ctx = AST_LIST_NEXT(old_ctx, node);
			continue;
		}
		if (cmp == 0) {
			/* Old and new map have this context. */
			remove_dead_context_usage(old_ctx->context, old_ctx, new_ctx);
			old_ctx = AST_LIST_NEXT(old_ctx, node);
		} else {
			/* Old map does not have new map context. */
		}
		new_ctx = AST_LIST_NEXT(new_ctx, node);
	}

	/* Any old contexts left must be dead. */
	for (; old_ctx; old_ctx = AST_LIST_NEXT(old_ctx, node)) {
		con = ast_context_find(old_ctx->context);
		if (con) {
			ast_context_destroy(con, registrar);
		}
	}
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

	if (parkinglot->the_mark) {
		if (AST_LIST_EMPTY(&parkinglot->parkings)) {
			/* This parking lot can actually be deleted. */
			return CMP_MATCH;
		}
		/* Try reloading later when parking lot is empty. */
		ast_log(LOG_WARNING,
			"Parking lot %s has parked calls.  Could not remove.\n",
			parkinglot->name);
		parkinglot->disabled = 1;
		force_reload_load = 1;
	}

	return 0;
}

static int parkinglot_activate_cb(void *obj, void *arg, int flags)
{
	struct ast_parkinglot *parkinglot = obj;

	if (parkinglot->the_mark) {
		/*
		 * Don't activate a parking lot that still bears the_mark since
		 * it is effectively deleted.
		 */
		return 0;
	}

	if (parkinglot_activate(parkinglot)) {
		/*
		 * The parking lot failed to activate.  Allow reloading later to
		 * see if that fixes it.
		 */
		force_reload_load = 1;
		ast_log(LOG_WARNING, "Parking lot %s not open for business.\n", parkinglot->name);
	} else {
		ast_debug(1, "Parking lot %s now open for business. (parkpos %d-%d)\n",
			parkinglot->name, parkinglot->cfg.parking_start,
			parkinglot->cfg.parking_stop);
	}

	return 0;
}

static int load_config(int reload)
{
	struct ast_flags config_flags = {
		reload && !force_reload_load ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_config *cfg;
	struct parking_dp_map old_usage_map = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct parking_dp_map new_usage_map = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

	/* We are reloading now and have already determined if we will force the reload. */
	force_reload_load = 0;

	if (!default_parkinglot) {
		/* Must create the default default parking lot */
		default_parkinglot = build_parkinglot(DEFAULT_PARKINGLOT, NULL);
		if (!default_parkinglot) {
			ast_log(LOG_ERROR, "Configuration of default default parking lot failed.\n");
			return -1;
		}
		ast_debug(1, "Configuration of default default parking lot done.\n");
		parkinglot_addref(default_parkinglot);
	}

	cfg = ast_config_load2("features.conf", "features", config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		/* No sense in asking for reload trouble if nothing changed. */
		ast_debug(1, "features.conf did not change.\n");
		return 0;
	}
	if (cfg == CONFIG_STATUS_FILEMISSING
		|| cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Could not load features.conf\n");
		return 0;
	}

	/* Save current parking lot dialplan needs. */
	if (build_dialplan_useage_map(&old_usage_map, 0)) {
		destroy_dialplan_usage_map(&old_usage_map);

		/* Allow reloading later to see if conditions have improved. */
		force_reload_load = 1;
		return -1;
	}

	ao2_t_callback(parkinglots, OBJ_NODATA, parkinglot_markall_cb, NULL,
		"callback to mark all parking lots");
	process_config(cfg);
	ast_config_destroy(cfg);
	ao2_t_callback(parkinglots, OBJ_NODATA | OBJ_UNLINK, parkinglot_is_marked_cb, NULL,
		"callback to remove marked parking lots");

	/* Save updated parking lot dialplan needs. */
	if (build_dialplan_useage_map(&new_usage_map, 1)) {
		/*
		 * Yuck, if this failure caused any parking lot dialplan items
		 * to be lost, they will likely remain lost until Asterisk is
		 * restarted.
		 */
		destroy_dialplan_usage_map(&old_usage_map);
		destroy_dialplan_usage_map(&new_usage_map);
		return -1;
	}

	/* Remove no longer needed parking lot dialplan usage. */
	remove_dead_dialplan_useage(&old_usage_map, &new_usage_map);

	destroy_dialplan_usage_map(&old_usage_map);
	destroy_dialplan_usage_map(&new_usage_map);

	ao2_t_callback(parkinglots, OBJ_NODATA, parkinglot_activate_cb, NULL,
		"callback to activate all parking lots");

	return 0;
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
		ast_cli(a->fd,"%-22s:      %s\n", "Parking extension", curlot->cfg.parkext);
		ast_cli(a->fd,"%-22s:      %s\n", "Parking context", curlot->cfg.parking_con);
		ast_cli(a->fd,"%-22s:      %d-%d\n", "Parked call extensions",
			curlot->cfg.parking_start, curlot->cfg.parking_stop);
		ast_cli(a->fd,"%-22s:      %d ms\n", "Parkingtime", curlot->cfg.parkingtime);
		ast_cli(a->fd,"%-22s:      %s\n", "MusicOnHold class", curlot->cfg.mohclass);
		ast_cli(a->fd,"%-22s:      %s\n", "Enabled", AST_CLI_YESNO(!curlot->disabled));
		ast_cli(a->fd,"\n");
		ao2_ref(curlot, -1);
	}
	ao2_iterator_destroy(&iter);

	return CLI_SUCCESS;
}

int ast_features_reload(void)
{
	struct ast_context *con;
	int res;

	ast_mutex_lock(&features_reload_lock);/* Searialize reloading features.conf */

	/*
	 * Always destroy the parking_con_dial context to remove buildup
	 * of recalled extensions in the context.  At worst, the parked
	 * call gets hungup attempting to run an invalid extension when
	 * we are trying to callback the parker or the preset return
	 * extension.  This is a small window of opportunity on an
	 * execution chain that is not expected to happen very often.
	 */
	con = ast_context_find(parking_con_dial);
	if (con) {
		ast_context_destroy(con, registrar);
	}

	res = load_config(1);
	ast_mutex_unlock(&features_reload_lock);

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
	ast_channel_unlock(chan);
	ast_channel_unlock(tmpchan);

	ast_channel_masquerade(tmpchan, chan);

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

	ast_cli(a->fd, "%-10s %-25s (%-15s %-12s %4s) %s\n", "Num", "Channel",
		"Context", "Extension", "Pri", "Timeout");

	iter = ao2_iterator_init(parkinglots, 0);
	while ((curlot = ao2_iterator_next(&iter))) {
		int lotparked = 0;

		/* subtract ref for iterator and for configured parking lot */
		ast_cli(a->fd, "*** Parking lot: %s (%d)\n", curlot->name,
			ao2_ref(curlot, 0) - 2 - (curlot == default_parkinglot));

		AST_LIST_LOCK(&curlot->parkings);
		AST_LIST_TRAVERSE(&curlot->parkings, cur, list) {
			ast_cli(a->fd, "%-10.10s %-25s (%-15s %-12s %4d) %6lds\n",
				cur->parkingexten, cur->chan->name, cur->context, cur->exten,
				cur->priority,
				(long) (cur->start.tv_sec + (cur->parkingtime / 1000) - time(NULL)));
			++lotparked;
		}
		AST_LIST_UNLOCK(&curlot->parkings);
		if (lotparked) {
			numparked += lotparked;
			ast_cli(a->fd, "   %d parked call%s in parking lot %s\n", lotparked,
				ESS(lotparked), curlot->name);
		}

		ao2_ref(curlot, -1);
	}
	ao2_iterator_destroy(&iter);

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
	int numparked = 0;

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	astman_send_ack(s, m, "Parked calls will follow");

	iter = ao2_iterator_init(parkinglots, 0);
	while ((curlot = ao2_iterator_next(&iter))) {
		AST_LIST_LOCK(&curlot->parkings);
		AST_LIST_TRAVERSE(&curlot->parkings, cur, list) {
			astman_append(s, "Event: ParkedCall\r\n"
				"Parkinglot: %s\r\n"
				"Exten: %d\r\n"
				"Channel: %s\r\n"
				"From: %s\r\n"
				"Timeout: %ld\r\n"
				"CallerIDNum: %s\r\n"
				"CallerIDName: %s\r\n"
				"ConnectedLineNum: %s\r\n"
				"ConnectedLineName: %s\r\n"
				"%s"
				"\r\n",
				curlot->name,
				cur->parkingnum, cur->chan->name, cur->peername,
				(long) cur->start.tv_sec + (long) (cur->parkingtime / 1000) - (long) time(NULL),
				S_COR(cur->chan->caller.id.number.valid, cur->chan->caller.id.number.str, ""),	/* XXX in other places it is <unknown> */
				S_COR(cur->chan->caller.id.name.valid, cur->chan->caller.id.name.str, ""),
				S_COR(cur->chan->connected.id.number.valid, cur->chan->connected.id.number.str, ""),	/* XXX in other places it is <unknown> */
				S_COR(cur->chan->connected.id.name.valid, cur->chan->connected.id.name.str, ""),
				idText);
			++numparked;
		}
		AST_LIST_UNLOCK(&curlot->parkings);
		ao2_ref(curlot, -1);
	}
	ao2_iterator_destroy(&iter);

	astman_append(s,
		"Event: ParkedCallsComplete\r\n"
		"Total: %d\r\n"
		"%s"
		"\r\n",
		numparked, idText);

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

	if (!ast_strlen_zero(timeout)) {
		if (sscanf(timeout, "%30d", &args.timeout) != 1) {
			astman_send_error(s, m, "Invalid timeout value.");
			return 0;
		}
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

	if (!ast_strlen_zero(parkinglotname)) {
		args.parkinglot = find_parkinglot(parkinglotname);
	}

	res = masq_park_call(ch1, ch2, 0, NULL, 0, &args);
	if (!res) {
		ast_softhangup(ch2, AST_SOFTHANGUP_EXPLICIT);
		astman_send_ack(s, m, "Park successful");
	} else {
		astman_send_error(s, m, "Park failure");
	}

	if (args.parkinglot) {
		parkinglot_unref(args.parkinglot);
	}
	ch1 = ast_channel_unref(ch1);
	ch2 = ast_channel_unref(ch2);

	return 0;
}

/*!
 * The presence of this datastore on the channel indicates that
 * someone is attemting to pickup or has picked up the channel.
 * The purpose is to prevent a race between two channels
 * attempting to pickup the same channel.
 */
static const struct ast_datastore_info pickup_active = {
 	.type = "pickup-active",
};

int ast_can_pickup(struct ast_channel *chan)
{
	if (!chan->pbx && !chan->masq && !ast_test_flag(chan, AST_FLAG_ZOMBIE)
		&& (chan->_state == AST_STATE_RINGING
			|| chan->_state == AST_STATE_RING
			/*
			 * Check the down state as well because some SIP devices do not
			 * give 180 ringing when they can just give 183 session progress
			 * instead.  Issue 14005.  (Some ISDN switches as well for that
			 * matter.)
			 */
			|| chan->_state == AST_STATE_DOWN)
		&& !ast_channel_datastore_find(chan, &pickup_active, NULL)) {
		return 1;
	}
	return 0;
}

static int find_channel_by_group(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	struct ast_channel *chan = data;/*!< Channel wanting to pickup call */

	ast_channel_lock(target);
	if (chan != target && (chan->pickupgroup & target->callgroup)
		&& ast_can_pickup(target)) {
		/* Return with the channel still locked on purpose */
		return CMP_MATCH | CMP_STOP;
	}
	ast_channel_unlock(target);

	return 0;
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
	struct ast_channel *target;/*!< Potential pickup target */
	int res = -1;
	ast_debug(1, "pickup attempt by %s\n", chan->name);

	/* The found channel is already locked. */
	target = ast_channel_callback(find_channel_by_group, NULL, chan, 0);
	if (target) {
		ast_log(LOG_NOTICE, "pickup %s attempt by %s\n", target->name, chan->name);

		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		if (!res) {
			if (!ast_strlen_zero(pickupsound)) {
				pbx_builtin_setvar_helper(target, "BRIDGE_PLAY_SOUND", pickupsound);
			}
		} else {
			ast_log(LOG_WARNING, "pickup %s failed by %s\n", target->name, chan->name);
		}
		target = ast_channel_unref(target);
	}

	if (res < 0) {
		ast_debug(1, "No call pickup possible... for %s\n", chan->name);
		if (!ast_strlen_zero(pickupfailsound)) {
			ast_answer(chan);
			ast_stream_and_wait(chan, pickupfailsound, "");
		}
	}

	return res;
}

int ast_do_pickup(struct ast_channel *chan, struct ast_channel *target)
{
	struct ast_party_connected_line connected_caller;
	struct ast_channel *chans[2] = { chan, target };
	struct ast_datastore *ds_pickup;
	const char *chan_name;/*!< A masquerade changes channel names. */
	const char *target_name;/*!< A masquerade changes channel names. */
	int res = -1;

	target_name = ast_strdupa(target->name);
	ast_debug(1, "Call pickup on '%s' by '%s'\n", target_name, chan->name);

	/* Mark the target to block any call pickup race. */
	ds_pickup = ast_datastore_alloc(&pickup_active, NULL);
	if (!ds_pickup) {
		ast_log(LOG_WARNING,
			"Unable to create channel datastore on '%s' for call pickup\n", target_name);
		return -1;
	}
	ast_channel_datastore_add(target, ds_pickup);

	ast_party_connected_line_init(&connected_caller);
	ast_party_connected_line_copy(&connected_caller, &target->connected);
	ast_channel_unlock(target);/* The pickup race is avoided so we do not need the lock anymore. */
	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
	if (ast_channel_connected_line_macro(NULL, chan, &connected_caller, 0, 0)) {
		ast_channel_update_connected_line(chan, &connected_caller, NULL);
	}
	ast_party_connected_line_free(&connected_caller);

	ast_channel_lock(chan);
	chan_name = ast_strdupa(chan->name);
	ast_connected_line_copy_from_caller(&connected_caller, &chan->caller);
	ast_channel_unlock(chan);
	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
	ast_channel_queue_connected_line_update(chan, &connected_caller, NULL);
	ast_party_connected_line_free(&connected_caller);

	ast_cel_report_event(target, AST_CEL_PICKUP, NULL, NULL, chan);

	if (ast_answer(chan)) {
		ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan_name);
		goto pickup_failed;
	}

	if (ast_queue_control(chan, AST_CONTROL_ANSWER)) {
		ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan_name);
		goto pickup_failed;
	}

	if (ast_channel_masquerade(target, chan)) {
		ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan_name,
			target_name);
		goto pickup_failed;
	}

	/* If you want UniqueIDs, set channelvars in manager.conf to CHANNEL(uniqueid) */
	ast_manager_event_multichan(EVENT_FLAG_CALL, "Pickup", 2, chans,
		"Channel: %s\r\n"
		"TargetChannel: %s\r\n",
		chan_name, target_name);

	/* Do the masquerade manually to make sure that it is completed. */
	ast_do_masquerade(target);
	res = 0;

pickup_failed:
	ast_channel_lock(target);
	if (!ast_channel_datastore_remove(target, ds_pickup)) {
		ast_datastore_free(ds_pickup);
	}

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

		/*
		 * If the first warning is requested _after_ the entire call
		 * would end, and no warning frequency is requested, then turn
		 * off the warning. If a warning frequency is requested, reduce
		 * the 'first warning' time by that frequency until it falls
		 * within the call's total time limit.
		 *
		 * Graphically:
		 *                timelim->|    delta        |<-playwarning
		 *      0__________________|_________________|
		 *                       | w  |    |    |    |
		 *
		 * so the number of intervals to cut is 1+(delta-1)/w
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

#if defined(TEST_FRAMEWORK)
/*!
 * \internal
 * \brief Convert parking spaces map list to a comma separated string.
 *
 * \param str String buffer to fill.
 * \param spaces Parking spaces map list to convert.
 *
 * \return Nothing
 */
static void create_spaces_str(struct ast_str **str, struct parking_dp_space_map *spaces)
{
	const char *comma;
	struct parking_dp_spaces *cur;

	ast_str_reset(*str);
	comma = "";
	AST_LIST_TRAVERSE(spaces, cur, node) {
		if (cur->start == cur->stop) {
			ast_str_append(str, 0, "%s%d", comma, cur->start);
		} else {
			ast_str_append(str, 0, "%s%d-%d", comma, cur->start, cur->stop);
		}
		comma = ",";
	}
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
/*!
 * \internal
 * \brief Compare parking spaces map to what is expected.
 *
 * \param test Unit test context.
 * \param spaces Parking spaces map list to check.
 * \param expected String to compare with.
 * \param what What is being compared.
 *
 * \retval 0 successful compare.
 * \retval nonzero if failed to compare.
 */
static int check_spaces(struct ast_test *test, struct parking_dp_space_map *spaces, const char *expected, const char *what)
{
	int cmp;
	struct ast_str *str = ast_str_alloca(1024);

	create_spaces_str(&str, spaces);
	cmp = strcmp(expected, ast_str_buffer(str));
	if (cmp) {
		ast_test_status_update(test,
			"Unexpected parking space map for %s. Expect:'%s' Got:'%s'\n",
			what, expected, ast_str_buffer(str));
	}
	return cmp;
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
/*!
 * \internal
 * \brief Add a dead space to the dead spaces list.
 *
 * \param context Dead spaces list ptr pretending to be a context name ptr.
 * \param space Dead space to add to the list.
 *
 * \return Nothing
 */
static void test_add_dead_space(const char *context, int space)
{
	struct parking_dp_space_map *dead_spaces = (struct parking_dp_space_map *) context;

	usage_context_add_spaces(dead_spaces, space, space, NULL, 0);
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
struct test_map {
	const char *ramp;
	int start;
	int stop;
	const char *expect;
};

/*!
 * \internal
 * \brief Build a parking lot dialplan usage test map from a table.
 *
 * \param test Unit test context.
 * \param lot Parking lot to use to build test usage map.
 * \param table_name Name of passed in table.
 * \param table Usage information to put in the usage map.
 * \param num_entries Number of entries in the table.
 *
 * \retval Created context node on success.
 * \retval NULL on error.
 */
static struct parking_dp_context *test_build_maps(struct ast_test *test,
	struct ast_parkinglot *lot, const char *table_name, const struct test_map *table,
	size_t num_entries)
{
	struct parking_dp_context *ctx_node;
	int cur_index = 0;
	char what[40];

	snprintf(what, sizeof(what), "%s[%d]", table_name, cur_index);
	ast_copy_string(lot->cfg.parkext, table->ramp, sizeof(lot->cfg.parkext));
	lot->cfg.parking_start = table->start;
	lot->cfg.parking_stop = table->stop;
	ctx_node = build_dialplan_useage_context(lot);
	if (!ctx_node) {
		ast_test_status_update(test, "Failed to create parking lot context map for %s\n",
			what);
		return NULL;
	}
	if (check_spaces(test, &ctx_node->spaces, table->expect, what)) {
		destroy_dialplan_usage_context(ctx_node);
		return NULL;
	}
	while (--num_entries) {
		++cur_index;
		++table;
		snprintf(what, sizeof(what), "%s[%d]", table_name, cur_index);
		ast_copy_string(lot->cfg.parkext, table->ramp, sizeof(lot->cfg.parkext));
		lot->cfg.parking_start = table->start;
		lot->cfg.parking_stop = table->stop;
		if (dialplan_usage_add_parkinglot_data(ctx_node, lot, 1)) {
			ast_test_status_update(test, "Failed to add parking lot data for %s\n", what);
			destroy_dialplan_usage_context(ctx_node);
			return NULL;
		}
		if (check_spaces(test, &ctx_node->spaces, table->expect, what)) {
			destroy_dialplan_usage_context(ctx_node);
			return NULL;
		}
	}
	return ctx_node;
}

static const struct test_map test_old_ctx[] = {
	/* The following order of building ctx is important to test adding items to the lists. */
	{ "702", 14, 15, "14-15" },
	{ "700", 10, 11, "10-11,14-15" },
	{ "701", 18, 19, "10-11,14-15,18-19" },
	{ "703", 12, 13, "10-15,18-19" },
	{ "704", 16, 17, "10-19" },

	/* Parking ramp and space conflicts are intended with these lines. */
	{ "704", 9, 19, "9-19" },
	{ "704", 9, 20, "9-20" },
	{ "704", 8, 21, "8-21" },

	/* Add more spaces to ctx to test removing dead parking spaces. */
	{ "705", 23, 25, "8-21,23-25" },
	{ "706", 28, 31, "8-21,23-25,28-31" },
	{ "707", 33, 34, "8-21,23-25,28-31,33-34" },
	{ "708", 38, 40, "8-21,23-25,28-31,33-34,38-40" },
	{ "709", 42, 43, "8-21,23-25,28-31,33-34,38-40,42-43" },
};

static const struct test_map test_new_ctx[] = {
	{ "702", 4, 5, "4-5" },
	{ "704", 24, 26, "4-5,24-26" },
	{ "709", 29, 30, "4-5,24-26,29-30" },
	{ "710", 32, 35, "4-5,24-26,29-30,32-35" },
	{ "711", 37, 39, "4-5,24-26,29-30,32-35,37-39" },
};
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
/*!
 * \internal
 * \brief Test parking dialplan usage map code.
 *
 * \param test Unit test context.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int test_dialplan_usage_map(struct ast_test *test)
{
	struct parking_dp_context *old_ctx;
	struct parking_dp_context *new_ctx;
	struct ast_parkinglot *lot;
	struct parking_dp_spaces *spaces;
	struct parking_dp_space_map dead_spaces = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	int res;

	ast_test_status_update(test, "Test parking dialplan usage map code\n");

	lot = create_parkinglot("test_lot");
	if (!lot) {
		return -1;
	}
	ast_copy_string(lot->cfg.parking_con, "test-ctx", sizeof(lot->cfg.parking_con));
	lot->cfg.parkext_exclusive = 1;

	ast_test_status_update(test,
		"Build old_ctx map\n");
	ast_log(LOG_NOTICE, "6 Ramp and space conflict warnings are expected.\n");
	old_ctx = test_build_maps(test, lot, "test_old_ctx", test_old_ctx,
		ARRAY_LEN(test_old_ctx));
	if (!old_ctx) {
		ao2_ref(lot, -1);
		return -1;
	}

	ast_test_status_update(test, "Build new_ctx map\n");
	new_ctx = test_build_maps(test, lot, "test_new_ctx", test_new_ctx,
		ARRAY_LEN(test_new_ctx));
	if (!new_ctx) {
		res = -1;
		goto fail_old_ctx;
	}

	ast_test_status_update(test, "Test removing dead parking spaces\n");
	remove_dead_spaces_usage((void *) &dead_spaces, &old_ctx->spaces,
		&new_ctx->spaces, test_add_dead_space);
	if (check_spaces(test, &dead_spaces, "8-21,23,28,31,40,42-43", "dead_spaces")) {
		res = -1;
		goto fail_dead_spaces;
	}

	res = 0;

fail_dead_spaces:
	while ((spaces = AST_LIST_REMOVE_HEAD(&dead_spaces, node))) {
		ast_free(spaces);
	}
	destroy_dialplan_usage_context(new_ctx);

fail_old_ctx:
	destroy_dialplan_usage_context(old_ctx);
	ao2_ref(lot, -1);
	return res;
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
static int fake_fixup(struct ast_channel *clonechan, struct ast_channel *original)
{
	return 0;
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
static struct ast_channel *create_test_channel(const struct ast_channel_tech *fake_tech)
{
	struct ast_channel *test_channel1;

	if (!(test_channel1 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
		NULL, NULL, 0, 0, "TestChannel1"))) {
		ast_log(LOG_WARNING, "Whoa, test channel creation failed.\n");
		return NULL;
	}

	/* normally this is done in the channel driver */
	test_channel1->nativeformats = AST_FORMAT_GSM;
	test_channel1->writeformat = AST_FORMAT_GSM;
	test_channel1->rawwriteformat = AST_FORMAT_GSM;
	test_channel1->readformat = AST_FORMAT_GSM;
	test_channel1->rawreadformat = AST_FORMAT_GSM;
	test_channel1->tech = fake_tech;

	return test_channel1;
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
static int unpark_test_channel(struct ast_channel *toremove, struct ast_park_call_args *args)
{
	struct ast_context *con;
	struct parkeduser *pu_toremove;
	int res = 0;

	args->pu->notquiteyet = 1; /* go ahead and stop processing the test parking */

	AST_LIST_LOCK(&args->pu->parkinglot->parkings);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&args->pu->parkinglot->parkings, pu_toremove, list) {
		if (pu_toremove == args->pu) {
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&args->pu->parkinglot->parkings);

	if (!pu_toremove) {
		ast_log(LOG_WARNING, "Whoa, could not find parking test call!\n");
		return -1;
	}

	con = ast_context_find(args->pu->parkinglot->cfg.parking_con);
	if (con) {
		if (ast_context_remove_extension2(con, args->pu->parkingexten, 1, NULL, 0)) {
			ast_log(LOG_WARNING, "Whoa, failed to remove the parking extension!\n");
			res = -1;
		} else {
			notify_metermaids(args->pu->parkingexten,
				pu_toremove->parkinglot->cfg.parking_con, AST_DEVICE_NOT_INUSE);
		}
	} else {
		ast_log(LOG_WARNING, "Whoa, no parking context?\n");
		res = -1;
	}

	parkinglot_unref(pu_toremove->parkinglot);
	ast_free(pu_toremove);
	args->pu = NULL;

	if (!res && toremove) {
		ast_hangup(toremove);
	}
	return res;
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
AST_TEST_DEFINE(features_test)
{
	struct ast_channel *test_channel1 = NULL;
	struct ast_channel *parked_chan = NULL;
	struct ast_parkinglot *dynlot;
	struct ast_park_call_args args = {
		.timeout = DEFAULT_PARK_TIME,
	};

	int res = 0;

	static const struct ast_channel_tech fake_tech = {
		.fixup = fake_fixup, /* silence warning from masquerade */
	};

	static const char unique_lot_1[] = "myuniquetestparkinglot314";
	static const char unique_lot_2[] = "myuniquetestparkinglot3141592654";
	static const char unique_context_1[] = "myuniquetestcontext314";
	static const char unique_context_2[] = "myuniquetestcontext3141592654";
	static const char parkinglot_parkext[] = "750";
	static const char parkinglot_range[] = "751-760";

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

	if (test_dialplan_usage_map(test)) {
		res = -1;
		goto exit_features_test;
	}

	/* changing a config option is a bad practice, but must be done in this case */
	parkeddynamic = 1;

	ast_test_status_update(test, "Test parking functionality with defaults\n");
	if (!(test_channel1 = create_test_channel(&fake_tech))) {
		res = -1;
		goto exit_features_test;
	}
	if (park_call_full(test_channel1, NULL, &args)) {
		res = -1;
		goto exit_features_test;
	}
	if (unpark_test_channel(test_channel1, &args)) {
		res = -1;
		goto exit_features_test;
	}


	ast_test_status_update(test, "Check that certain parking options are respected\n");
	if (!(test_channel1 = create_test_channel(&fake_tech))) {
		res = -1;
		goto exit_features_test;
	}
	pbx_builtin_setvar_helper(test_channel1, "PARKINGLOT", unique_lot_1);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNCONTEXT", unique_context_1);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNEXTEN", parkinglot_parkext);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNPOS", parkinglot_range);
	if (park_call_full(test_channel1, NULL, &args)) {
		res = -1;
		goto exit_features_test;
	}
	/* grab newly created parking lot for destruction in the end */
	dynlot = args.pu->parkinglot;
	if (args.pu->parkingnum != 751
		|| strcmp(dynlot->name, unique_lot_1)
		|| strcmp(dynlot->cfg.parking_con, unique_context_1)
		|| strcmp(dynlot->cfg.parkext, parkinglot_parkext)
		|| dynlot->cfg.parking_start != 751
		|| dynlot->cfg.parking_stop != 760) {
		ast_test_status_update(test, "Parking settings were not respected\n");
		ast_test_status_update(test, "Dyn-name:%s\n", dynlot->name);
		ast_test_status_update(test, "Dyn-context:%s\n", dynlot->cfg.parking_con);
		ast_test_status_update(test, "Dyn-parkext:%s\n", dynlot->cfg.parkext);
		ast_test_status_update(test, "Dyn-parkpos:%d-%d\n", dynlot->cfg.parking_start,
			dynlot->cfg.parking_stop);
		ast_test_status_update(test, "Parked in space:%d\n", args.pu->parkingnum);
		if (!unpark_test_channel(test_channel1, &args)) {
			test_channel1 = NULL;
		}
		res = -1;
		goto exit_features_test;
	} else {
		ast_test_status_update(test, "Parking settings for non-masquerading park verified\n");
	}
	if (unpark_test_channel(test_channel1, &args)) {
		res = -1;
		goto exit_features_test;
	}


	ast_test_status_update(test, "Check #2 that certain parking options are respected\n");
	if (!(test_channel1 = create_test_channel(&fake_tech))) {
		res = -1;
		goto exit_features_test;
	}
	pbx_builtin_setvar_helper(test_channel1, "PARKINGLOT", unique_lot_2);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNCONTEXT", unique_context_2);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNEXTEN", parkinglot_parkext);
	pbx_builtin_setvar_helper(test_channel1, "PARKINGDYNPOS", parkinglot_range);
	if (masq_park_call(test_channel1, NULL, 0, NULL, 0, &args)) {
		res = -1;
		goto exit_features_test;
	}
	/* hangup zombie channel */
	ast_hangup(test_channel1);
	test_channel1 = NULL;

	dynlot = args.pu->parkinglot;
	if (args.pu->parkingnum != 751
		|| strcmp(dynlot->name, unique_lot_2)
		|| strcmp(dynlot->cfg.parking_con, unique_context_2)
		|| strcmp(dynlot->cfg.parkext, parkinglot_parkext)
		|| dynlot->cfg.parking_start != 751
		|| dynlot->cfg.parking_stop != 760) {
		ast_test_status_update(test, "Parking settings were not respected\n");
		ast_test_status_update(test, "Dyn-name:%s\n", dynlot->name);
		ast_test_status_update(test, "Dyn-context:%s\n", dynlot->cfg.parking_con);
		ast_test_status_update(test, "Dyn-parkext:%s\n", dynlot->cfg.parkext);
		ast_test_status_update(test, "Dyn-parkpos:%d-%d\n", dynlot->cfg.parking_start,
			dynlot->cfg.parking_stop);
		ast_test_status_update(test, "Parked in space:%d\n", args.pu->parkingnum);
		res = -1;
	} else {
		ast_test_status_update(test, "Parking settings for masquerading park verified\n");
	}

	/* find the real channel */
	parked_chan = ast_channel_get_by_name("TestChannel1");
	if (unpark_test_channel(parked_chan, &args)) {
		if (parked_chan) {
			ast_hangup(parked_chan);
		}
		res = -1;
	}


exit_features_test:

	if (test_channel1) {
		ast_hangup(test_channel1);
	}

	force_reload_load = 1;
	ast_features_reload();
	return res ? AST_TEST_FAIL : AST_TEST_PASS;
}
#endif	/* defined(TEST_FRAMEWORK) */

int ast_features_init(void)
{
	int res;

	parkinglots = ao2_container_alloc(7, parkinglot_hash_cb, parkinglot_cmp_cb);
	if (!parkinglots) {
		return -1;
	}

	res = load_config(0);
	if (res) {
		return res;
	}
	ast_cli_register_multiple(cli_features, ARRAY_LEN(cli_features));
	ast_pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	ast_register_application2(app_bridge, bridge_exec, NULL, NULL, NULL);
	res = ast_register_application2(parkedcall, parked_call_exec, NULL, NULL, NULL);
	if (!res)
		res = ast_register_application2(parkcall, park_call_exec, NULL, NULL, NULL);
	if (!res) {
		ast_manager_register_xml("ParkedCalls", 0, manager_parking_status);
		ast_manager_register_xml("Park", EVENT_FLAG_CALL, manager_park);
		ast_manager_register_xml("Bridge", EVENT_FLAG_CALL, action_bridge);
	}

	res |= ast_devstate_prov_add("Park", metermaidstate);
#if defined(TEST_FRAMEWORK)
	res |= AST_TEST_REGISTER(features_test);
#endif	/* defined(TEST_FRAMEWORK) */

	return res;
}
