/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
 * Copyright (C) 2012, Russell Bryant
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

/*! \li \ref features.c uses the configuration file \ref features.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page features.conf features.conf
 * \verbinclude features.conf.sample
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
#include "asterisk/bridging.h"
#include "asterisk/bridging_basic.h"
#include "asterisk/features_config.h"

/* BUGBUG TEST_FRAMEWORK is disabled because parking tests no longer work. */
#undef TEST_FRAMEWORK

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
					<option name="F" argsep="^">
						<argument name="context" required="false" />
						<argument name="exten" required="false" />
						<argument name="priority" required="true" />
						<para>When the bridger hangs up, transfer the <emphasis>bridged</emphasis> party
						to the specified destination and <emphasis>start</emphasis> execution at that location.</para>
						<note>
							<para>Any channel variables you want the called channel to inherit from the caller channel must be
							prefixed with one or two underbars ('_').</para>
						</note>
						<note>
							<para>This option will override the 'x' option</para>
						</note>
					</option>
					<option name="F">
						<para>When the bridger hangs up, transfer the <emphasis>bridged</emphasis> party
						to the next priority of	the current extension and <emphasis>start</emphasis> execution
						at that location.</para>
						<note>
							<para>Any channel variables you want the called channel to inherit from the caller channel must be
							prefixed with one or two underbars ('_').</para>
						</note>
						<note>
							<para>Using this option from a Macro() or GoSub() might not make sense as there would be no return points.</para>
						</note>
						<note>
							<para>This option will override the 'x' option</para>
						</note>
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
				<para>Channel to return to if timeout.</para>
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
					<enum name="no" />
					<enum name="Channel1" />
					<enum name="Channel2" />
					<enum name="Both" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Bridge together two channels already in the PBX.</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="ParkedCallTimeOut">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a parked call times out.</synopsis>
			<syntax>
				<parameter name="Exten">
					<para>The parking lot extension.</para>
				</parameter>
				<parameter name="Channel"/>
				<parameter name="Parkinglot">
					<para>The name of the parking lot.</para>
				</parameter>
				<parameter name="CallerIDNum"/>
				<parameter name="CallerIDName"/>
				<parameter name="ConnectedLineNum"/>
				<parameter name="ConnectedLineName"/>
				<parameter name="UniqueID"/>
			</syntax>
			<see-also>
				<ref type="managerEvent">ParkedCall</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ParkedCallGiveUp">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a parked call hangs up while in the parking lot.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCallTimeOut']/managerEventInstance/syntax/parameter[@name='Exten'])" />
				<parameter name="Channel"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCallTimeOut']/managerEventInstance/syntax/parameter[@name='Parkinglot'])" />
				<parameter name="CallerIDNum"/>
				<parameter name="CallerIDName"/>
				<parameter name="ConnectedLineNum"/>
				<parameter name="ConnectedLineName"/>
				<parameter name="UniqueID"/>
			</syntax>
			<see-also>
				<ref type="managerEvent">ParkedCall</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
 ***/

#define DEFAULT_PARK_TIME							45000	/*!< ms */
#define DEFAULT_PARK_EXTENSION						"700"
#define DEFAULT_COMEBACK_CONTEXT					"parkedcallstimeout"
#define DEFAULT_COMEBACK_TO_ORIGIN					1
#define DEFAULT_COMEBACK_DIAL_TIME					30

#define AST_MAX_WATCHERS 256
#define MAX_DIAL_FEATURE_OPTIONS 30

/* TODO Scrape all of the parking stuff out of features.c */

typedef enum {
	FEATURE_INTERPRET_DETECT, /* Used by ast_feature_detect */
	FEATURE_INTERPRET_DO,     /* Used by feature_interpret */
	FEATURE_INTERPRET_CHECK,  /* Used by feature_check */
} feature_interpret_op;

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
	unsigned int parkingtime;                   /*!< Maximum length in parking lot before return */
	/*! Method to entertain the caller when parked: AST_CONTROL_RINGING, AST_CONTROL_HOLD, or 0(none) */
	enum ast_control_frame_type hold_method;
	unsigned int notquiteyet:1;
	unsigned int options_specified:1;
	char peername[AST_CHANNEL_NAME];
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
	char parking_con[AST_MAX_CONTEXT];
	/*! Context that timed-out parked calls are called back on when comebacktoorigin=no */
	char comebackcontext[AST_MAX_CONTEXT];
	/*! First available extension for parking */
	int parking_start;
	/*! Last available extension for parking */
	int parking_stop;
	/*! Default parking time in ms. */
	unsigned int parkingtime;
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

	/*! Time in seconds to dial the device that parked a timedout parked call */
	unsigned int comebackdialtime;
	/*! TRUE if findslot is set to next */
	unsigned int parkfindnext:1;
	/*! TRUE if the parking lot is exclusively accessed by parkext */
	unsigned int parkext_exclusive:1;
	/*! Add parking hints automatically */
	unsigned int parkaddhints:1;
	/*! TRUE if configuration is invalid and the parking lot should not be used. */
	unsigned int is_invalid:1;
	/*! TRUE if a timed out parked call goes back to the parker */
	unsigned int comebacktoorigin:1;
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
#ifdef TEST_FRAMEWORK
static int force_reload_load;
#endif

static int parkeddynamic = 0;                              /*!< Enable creation of parkinglots dynamically */

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
static const char *parkcall = "Park";

static pthread_t parking_thread;
struct ast_dial_features {
	/*! Channel's feature flags. */
	struct ast_flags my_features;
	/*! Bridge peer's feature flags. */
	struct ast_flags peer_features;
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
	if (ast_channel_tech(chan)->hangup) {
		ast_channel_tech(chan)->hangup(chan);
	}
	if (ast_channel_tech_pvt(chan)) {
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n",
			ast_channel_name(chan));
		ast_free(ast_channel_tech_pvt(chan));
		ast_channel_tech_pvt_set(chan, NULL);
	}

	/* Install the kill technology and wake up anyone waiting on it. */
	ast_channel_tech_set(chan, &ast_kill_tech);
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
	len = snprintf(dummy, sizeof(dummy), "%s<XFER_%x>", ast_channel_name(chan), seq_num) + 1;
	chan_name = ast_alloca(len);
	snprintf(chan_name, len, "%s<XFER_%x>", ast_channel_name(chan), seq_num);
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

static const struct ast_datastore_info dial_features_info = {
	.type = "dial-features",
	.destroy = ast_free_ptr,
	.duplicate = dial_features_duplicate,
};

/*!
 * \internal
 * \brief Set the features datastore if it doesn't exist.
 *
 * \param chan Channel to add features datastore
 * \param my_features The channel's feature flags
 * \param peer_features The channel's bridge peer feature flags
 *
 * \retval TRUE if features datastore already existed.
 */
static int add_features_datastore(struct ast_channel *chan, const struct ast_flags *my_features, const struct ast_flags *peer_features)
{
	struct ast_datastore *datastore;
	struct ast_dial_features *dialfeatures;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &dial_features_info, NULL);
	ast_channel_unlock(chan);
	if (datastore) {
		/* Already exists. */
		return 1;
	}

	/* Create a new datastore with specified feature flags. */
	datastore = ast_datastore_alloc(&dial_features_info, NULL);
	if (!datastore) {
		ast_log(LOG_WARNING, "Unable to create channel features datastore.\n");
		return 0;
	}
	dialfeatures = ast_calloc(1, sizeof(*dialfeatures));
	if (!dialfeatures) {
		ast_log(LOG_WARNING, "Unable to allocate memory for feature flags.\n");
		ast_datastore_free(datastore);
		return 0;
	}
	ast_copy_flags(&dialfeatures->my_features, my_features, AST_FLAGS_ALL);
	ast_copy_flags(&dialfeatures->peer_features, peer_features, AST_FLAGS_ALL);
	datastore->inheritance = DATASTORE_INHERIT_FOREVER;
	datastore->data = dialfeatures;
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
	return 0;
}

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

	ast_debug(4, "Checking if %s@%s is a parking exten\n", exten_str, context);
	exten = pbx_find_extension(chan, NULL, &q, context, exten_str, 1, NULL, NULL,
		E_MATCH);
	if (!exten) {
		return NULL;
	}

	app_at_exten = ast_get_extension_app(exten);
	if (!app_at_exten || strcasecmp(parkcall, app_at_exten)) {
		return NULL;
	}

	return exten;
}

int ast_parking_ext_valid(const char *exten_str, struct ast_channel *chan, const char *context)
{
	return get_parking_exten(exten_str, chan, context) ? 1 : 0;
}

struct ast_bridge_thread_obj
{
	struct ast_bridge_config bconfig;
	struct ast_channel *chan;
	struct ast_channel *peer;
	struct ast_callid *callid;                             /*<! callid pointer (Only used to bind thread) */
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
	ast_channel_context_set(chan, context);
	ast_channel_exten_set(chan, ext);
	ast_channel_priority_set(chan, pri);
}

#if 0
static struct ast_channel *feature_request_and_dial(struct ast_channel *caller,
	const char *caller_name, struct ast_channel *requestor,
	struct ast_channel *transferee, const char *type, struct ast_format_cap *cap, const char *addr,
	int timeout, int *outstate, const char *language);
#endif

static const struct ast_datastore_info channel_app_data_datastore = {
	.type = "Channel appdata datastore",
	.destroy = ast_free_ptr,
};

#if 0
static int set_chan_app_data(struct ast_channel *chan, const char *src_app_data)
{
	struct ast_datastore *datastore;
	char *dst_app_data;

	datastore = ast_datastore_alloc(&channel_app_data_datastore, NULL);
	if (!datastore) {
		return -1;
	}

	dst_app_data = ast_malloc(strlen(src_app_data) + 1);
	if (!dst_app_data) {
		ast_datastore_free(datastore);
		return -1;
	}

	ast_channel_data_set(chan, strcpy(dst_app_data, src_app_data));
	datastore->data = dst_app_data;
	ast_channel_datastore_add(chan, datastore);
	return 0;
}
#endif

#if 0
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

	if (tobj->callid) {
		ast_callid_threadassoc_add(tobj->callid);
		/* Need to deref and set to null since ast_bridge_thread_obj has no common destructor */
		tobj->callid = ast_callid_unref(tobj->callid);
	}

	ast_channel_appl_set(tobj->chan, !tobj->return_to_pbx ? "Transferred Call" : "ManagerBridge");
	if (set_chan_app_data(tobj->chan, ast_channel_name(tobj->peer))) {
		ast_channel_data_set(tobj->chan, "(Empty)");
	}
	ast_channel_appl_set(tobj->peer, !tobj->return_to_pbx ? "Transferred Call" : "ManagerBridge");
	if (set_chan_app_data(tobj->peer, ast_channel_name(tobj->chan))) {
		ast_channel_data_set(tobj->peer, "(Empty)");
	}

	if (tobj->return_to_pbx) {
		ast_after_bridge_set_goto(tobj->chan, ast_channel_context(tobj->chan),
			ast_channel_exten(tobj->chan), ast_channel_priority(tobj->chan));
		ast_after_bridge_set_goto(tobj->peer, ast_channel_context(tobj->peer),
			ast_channel_exten(tobj->peer), ast_channel_priority(tobj->peer));
	}

	ast_bridge_call(tobj->chan, tobj->peer, &tobj->bconfig);

	ast_after_bridge_goto_run(tobj->chan);

	ast_free(tobj);

	return NULL;
}
#endif

#if 0
/*!
 * \brief create thread for the bridging call
 * \param tobj
 */
static void bridge_call_thread_launch(struct ast_bridge_thread_obj *tobj)
{
	pthread_t thread;

	/* This needs to be unreffed once it has been associated with the new thread. */
	tobj->callid = ast_read_threadstorage_callid();

	if (ast_pthread_create_detached(&thread, NULL, bridge_call_thread, tobj)) {
		ast_log(LOG_ERROR, "Failed to create bridge_call_thread.\n");
		ast_callid_unref(tobj->callid);
		ast_hangup(tobj->chan);
		ast_hangup(tobj->peer);
		ast_free(tobj);
	}
}
#endif

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
	if (!name && !ast_strlen_zero(ast_channel_parkinglot(chan))) {
		/* Use the channel's parking lot. */
		name = ast_channel_parkinglot(chan);
	}
	return name;
}

/*! \brief Notify metermaids that we've changed an extension */
static void notify_metermaids(const char *exten, char *context, enum ast_device_state state)
{
	ast_debug(4, "Notification of state change to metermaids %s@%s\n to state '%s'",
		exten, context, ast_devstate2str(state));

	ast_devstate_changed(state, AST_DEVSTATE_CACHABLE, "park:%s@%s", exten, context);
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
 * \brief Abort parking a call that has not completed parking yet.
 *
 * \param pu Parked user item to clean up.
 *
 * \note The parking lot parkings list is locked on entry.
 *
 * \return Nothing
 */
static void park_space_abort(struct parkeduser *pu)
{
	struct ast_parkinglot *parkinglot;

	parkinglot = pu->parkinglot;

	/* Put back the parking space just allocated. */
	--parkinglot->next_parking_space;

	AST_LIST_REMOVE(&parkinglot->parkings, pu, list);

	AST_LIST_UNLOCK(&parkinglot->parkings);
	parkinglot_unref(parkinglot);
	ast_free(pu);
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
		ast_log(LOG_WARNING, "Parking lot not available to park %s.\n", ast_channel_name(park_me));
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
	const char *event_from;		/*!< Channel name that is parking the call. */
	char app_data[AST_MAX_EXTENSION + AST_MAX_CONTEXT];

	if (pu == NULL) {
		args->pu = pu = park_space_reserve(chan, peer, args);
		if (pu == NULL) {
			return -1;
		}
	}

	ast_channel_appl_set(chan, "Parked Call");
	ast_channel_data_set(chan, NULL);

	pu->chan = chan;

	/* Put the parked channel on hold if we have two different channels */
	if (chan != peer) {
		if (ast_test_flag(args, AST_PARK_OPT_RINGING)) {
			pu->hold_method = AST_CONTROL_RINGING;
			ast_indicate(chan, AST_CONTROL_RINGING);
		} else {
			pu->hold_method = AST_CONTROL_HOLD;
			ast_indicate_data(chan, AST_CONTROL_HOLD,
				S_OR(pu->parkinglot->cfg.mohclass, NULL),
				!ast_strlen_zero(pu->parkinglot->cfg.mohclass) ? strlen(pu->parkinglot->cfg.mohclass) + 1 : 0);
		}
	}

	pu->start = ast_tvnow();
	/* XXX This line was changed to not use get_parkingtime. This is just a placeholder message, because
	 * likely this entire function is going away.
	 */
	pu->parkingtime = args->timeout;
	if (args->extout)
		*(args->extout) = pu->parkingnum;

	if (peer) {
		event_from = S_OR(args->orig_chan_name, ast_channel_name(peer));

		/*
		 * This is so ugly that it hurts, but implementing
		 * get_base_channel() on local channels could have ugly side
		 * effects.  We could have
		 * transferer<->local;1<->local;2<->parking and we need the
		 * callback name to be that of transferer.  Since local;1/2 have
		 * the same name we can be tricky and just grab the bridged
		 * channel from the other side of the local.
		 */
		if (!strcasecmp(ast_channel_tech(peer)->type, "Local")) {
			struct ast_channel *tmpchan, *base_peer;
			char other_side[AST_CHANNEL_NAME];
			char *c;

			ast_copy_string(other_side, event_from, sizeof(other_side));
			if ((c = strrchr(other_side, ';'))) {
				*++c = '1';
			}
			if ((tmpchan = ast_channel_get_by_name(other_side))) {
				ast_channel_lock(tmpchan);
				if ((base_peer = ast_bridged_channel(tmpchan))) {
					ast_copy_string(pu->peername, ast_channel_name(base_peer), sizeof(pu->peername));
				}
				ast_channel_unlock(tmpchan);
				tmpchan = ast_channel_unref(tmpchan);
			}
		} else {
			ast_copy_string(pu->peername, event_from, sizeof(pu->peername));
		}
	} else {
		event_from = S_OR(pbx_builtin_getvar_helper(chan, "BLINDTRANSFER"),
			ast_channel_name(chan));
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
		S_OR(args->return_con, S_OR(ast_channel_macrocontext(chan), ast_channel_context(chan))),
		sizeof(pu->context));
	ast_copy_string(pu->exten,
		S_OR(args->return_ext, S_OR(ast_channel_macroexten(chan), ast_channel_exten(chan))),
		sizeof(pu->exten));
	pu->priority = args->return_pri ? args->return_pri :
		(ast_channel_macropriority(chan) ? ast_channel_macropriority(chan) : ast_channel_priority(chan));

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
	ast_verb(2, "Parked %s on %d (lot %s). Will timeout back to extension [%s] %s, %d in %u seconds\n",
		ast_channel_name(chan), pu->parkingnum, pu->parkinglot->name,
		pu->context, pu->exten, pu->priority, (pu->parkingtime / 1000));

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a call has been parked.</synopsis>
			<syntax>
				<parameter name="Exten">
					<para>The parking lot extension.</para>
				</parameter>
				<parameter name="Parkinglot">
					<para>The name of the parking lot.</para>
				</parameter>
				<parameter name="From">
					<para>The name of the channel that parked the call.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">Park</ref>
				<ref type="manager">Park</ref>
				<ref type="managerEvent">ParkedCallTimeOut</ref>
				<ref type="managerEvent">ParkedCallGiveUp</ref>
			</see-also>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "ParkedCall",
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
		pu->parkingexten, ast_channel_name(chan), pu->parkinglot->name, event_from,
		(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL),
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, "<unknown>"),
		S_COR(ast_channel_connected(chan)->id.number.valid, ast_channel_connected(chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_connected(chan)->id.name.valid, ast_channel_connected(chan)->id.name.str, "<unknown>"),
		ast_channel_uniqueid(chan)
		);
	ast_debug(4, "peer: %s\n", peer ? ast_channel_name(peer) : "-No peer-");
	ast_debug(4, "args->orig_chan_name: %s\n", args->orig_chan_name ? args->orig_chan_name : "-none-");
	ast_debug(4, "pu->peername: %s\n", pu->peername);
	ast_debug(4, "AMI ParkedCall Channel: %s\n", ast_channel_name(chan));
	ast_debug(4, "AMI ParkedCall From: %s\n", event_from);

	if (peer && adsipark && ast_adsi_available(peer)) {
		adsi_announce_park(peer, pu->parkingexten);	/* Only supports parking numbers */
		ast_adsi_unload_session(peer);
	}

	snprintf(app_data, sizeof(app_data), "%s,%s", pu->parkingexten,
		pu->parkinglot->name);

	AST_LIST_UNLOCK(&pu->parkinglot->parkings);

	/* Only say number if it's a number and the channel hasn't been masqueraded away */
	if (peer && !ast_test_flag(args, AST_PARK_OPT_SILENCE)
		&& (ast_strlen_zero(args->orig_chan_name) || !strcasecmp(ast_channel_name(peer), args->orig_chan_name))) {
		/*
		 * If a channel is masqueraded into peer while playing back the
		 * parking space number do not continue playing it back.  This
		 * is the case if an attended transfer occurs.
		 */
		ast_set_flag(ast_channel_flags(peer), AST_FLAG_MASQ_NOSTREAM);
		/* Tell the peer channel the number of the parking space */
		ast_say_digits(peer, pu->parkingnum, "", ast_channel_language(peer));
		ast_clear_flag(ast_channel_flags(peer), AST_FLAG_MASQ_NOSTREAM);
	}
	if (peer == chan) { /* pu->notquiteyet = 1 */
		/* Wake up parking thread if we're really done */
		if (ast_test_flag(args, AST_PARK_OPT_RINGING)) {
			pu->hold_method = AST_CONTROL_RINGING;
			ast_indicate(chan, AST_CONTROL_RINGING);
		} else {
			pu->hold_method = AST_CONTROL_HOLD;
			ast_indicate_data(chan, AST_CONTROL_HOLD,
				S_OR(pu->parkinglot->cfg.mohclass, NULL),
				!ast_strlen_zero(pu->parkinglot->cfg.mohclass) ? strlen(pu->parkinglot->cfg.mohclass) + 1 : 0);
		}
		pu->notquiteyet = 0;
		pthread_kill(parking_thread, SIGURG);
	}
	return 0;
}

int ast_park_call_exten(struct ast_channel *park_me, struct ast_channel *parker, const char *park_exten, const char *park_context, int timeout, int *extout)
{
	int res;
	char *parse;
	const char *app_data;
	struct ast_exten *exten;
	struct park_app_args app_args;
	struct ast_park_call_args args = {
		.timeout = timeout,
		.extout = extout,
	};

	if (!park_exten || !park_context) {
		return park_call_full(park_me, parker, &args);
	}

	/*
	 * Determiine if the specified park extension has an exclusive
	 * parking lot to use.
	 */
	if (parker && parker != park_me) {
		ast_autoservice_start(park_me);
	}
	exten = get_parking_exten(park_exten, parker, park_context);
	if (exten) {
		app_data = ast_get_extension_app_data(exten);
		if (!app_data) {
			app_data = "";
		}
		parse = ast_strdupa(app_data);
		AST_STANDARD_APP_ARGS(app_args, parse);

		if (!ast_strlen_zero(app_args.pl_name)) {
			/* Find the specified exclusive parking lot */
			args.parkinglot = find_parkinglot(app_args.pl_name);
			if (!args.parkinglot && parkeddynamic) {
				args.parkinglot = create_dynamic_parkinglot(app_args.pl_name, park_me);
			}
		}
	}
	if (parker && parker != park_me) {
		ast_autoservice_stop(park_me);
	}

	res = park_call_full(park_me, parker, &args);
	if (args.parkinglot) {
		parkinglot_unref(args.parkinglot);
	}
	return res;
}

int ast_park_call(struct ast_channel *park_me, struct ast_channel *parker, int timeout, const char *park_exten, int *extout)
{
	struct ast_park_call_args args = {
		.timeout = timeout,
		.extout = extout,
	};

	return park_call_full(park_me, parker, &args);
}

/*!
 * \brief Park call via masqueraded channel and announce parking spot on peer channel.
 *
 * \param rchan the real channel to be parked
 * \param peer the channel to have the parking read to.
 * \param args Additional parking options when parking a call.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, struct ast_park_call_args *args)
{
	struct ast_channel *chan;

	/* Make a new, channel that we'll use to masquerade in the real one */
	chan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, ast_channel_accountcode(rchan), ast_channel_exten(rchan),
		ast_channel_context(rchan), ast_channel_linkedid(rchan), ast_channel_amaflags(rchan), "Parked/%s", ast_channel_name(rchan));
	if (!chan) {
		ast_log(LOG_WARNING, "Unable to create parked channel\n");
		if (!ast_test_flag(args, AST_PARK_OPT_SILENCE)) {
			if (peer == rchan) {
				/* Only have one channel to worry about. */
				ast_stream_and_wait(peer, "pbx-parkingfailed", "");
			} else if (peer) {
				/* Have two different channels to worry about. */
				play_message_on_chan(peer, rchan, "failure message", "pbx-parkingfailed");
			}
		}
		return -1;
	}

	args->pu = park_space_reserve(rchan, peer, args);
	if (!args->pu) {
		ast_hangup(chan);
		if (!ast_test_flag(args, AST_PARK_OPT_SILENCE)) {
			if (peer == rchan) {
				/* Only have one channel to worry about. */
				ast_stream_and_wait(peer, "pbx-parkingfailed", "");
			} else if (peer) {
				/* Have two different channels to worry about. */
				play_message_on_chan(peer, rchan, "failure message", "pbx-parkingfailed");
			}
		}
		return -1;
	}

	/* Make formats okay */
	ast_format_copy(ast_channel_readformat(chan), ast_channel_readformat(rchan));
	ast_format_copy(ast_channel_writeformat(chan), ast_channel_writeformat(rchan));

	if (ast_channel_masquerade(chan, rchan)) {
		park_space_abort(args->pu);
		args->pu = NULL;
		ast_hangup(chan);
		if (!ast_test_flag(args, AST_PARK_OPT_SILENCE)) {
			if (peer == rchan) {
				/* Only have one channel to worry about. */
				ast_stream_and_wait(peer, "pbx-parkingfailed", "");
			} else if (peer) {
				/* Have two different channels to worry about. */
				play_message_on_chan(peer, rchan, "failure message", "pbx-parkingfailed");
			}
		}
		return -1;
	}

	/* Setup the extensions and such */
	set_c_e_p(chan, ast_channel_context(rchan), ast_channel_exten(rchan), ast_channel_priority(rchan));

	/* Setup the macro extension and such */
	ast_channel_macrocontext_set(chan, ast_channel_macrocontext(rchan));
	ast_channel_macroexten_set(chan, ast_channel_macroexten(rchan));
	ast_channel_macropriority_set(chan, ast_channel_macropriority(rchan));

	/* Manually do the masquerade to make sure it is complete. */
	ast_do_masquerade(chan);

	if (peer == rchan) {
		peer = chan;
	}

	/* parking space reserved, return code check unnecessary */
	park_call_full(chan, peer, args);

	return 0;
}

int ast_masq_park_call_exten(struct ast_channel *park_me, struct ast_channel *parker, const char *park_exten, const char *park_context, int timeout, int *extout)
{
	int res;
	char *parse;
	const char *app_data;
	struct ast_exten *exten;
	struct park_app_args app_args;
	struct ast_park_call_args args = {
		.timeout = timeout,
		.extout = extout,
	};

	if (parker) {
		args.orig_chan_name = ast_strdupa(ast_channel_name(parker));
	}
	if (!park_exten || !park_context) {
		return masq_park_call(park_me, parker, &args);
	}

	/*
	 * Determiine if the specified park extension has an exclusive
	 * parking lot to use.
	 */
	if (parker && parker != park_me) {
		ast_autoservice_start(park_me);
	}
	exten = get_parking_exten(park_exten, parker, park_context);
	if (exten) {
		app_data = ast_get_extension_app_data(exten);
		if (!app_data) {
			app_data = "";
		}
		parse = ast_strdupa(app_data);
		AST_STANDARD_APP_ARGS(app_args, parse);

		if (!ast_strlen_zero(app_args.pl_name)) {
			/* Find the specified exclusive parking lot */
			args.parkinglot = find_parkinglot(app_args.pl_name);
			if (!args.parkinglot && parkeddynamic) {
				args.parkinglot = create_dynamic_parkinglot(app_args.pl_name, park_me);
			}
		}
	}
	if (parker && parker != park_me) {
		ast_autoservice_stop(park_me);
	}

	res = masq_park_call(park_me, parker, &args);
	if (args.parkinglot) {
		parkinglot_unref(args.parkinglot);
	}
	return res;
}

int ast_masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout)
{
	struct ast_park_call_args args = {
		.timeout = timeout,
		.extout = extout,
	};

	if (peer) {
		args.orig_chan_name = ast_strdupa(ast_channel_name(peer));
	}
	return masq_park_call(rchan, peer, &args);
}

#if 0
static int finishup(struct ast_channel *chan)
{
	ast_indicate(chan, AST_CONTROL_UNHOLD);

	return ast_autoservice_stop(chan);
}
#endif

#if 0
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
		res = masq_park_call(park_me, parker, &args);
		parkinglot_unref(args.parkinglot);
	} else {
		/* Parking failed because parking lot does not exist. */
		if (!ast_test_flag(&args, AST_PARK_OPT_SILENCE)) {
			ast_stream_and_wait(parker, "pbx-parkingfailed", "");
		}
		finishup(park_me);
		res = -1;
	}

	return res ? AST_FEATURE_RETURN_SUCCESS : -1;
}
#endif

#if 0
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
#endif

#if 0
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
	struct ast_park_call_args args = { 0, };

	/*
	 * We used to set chan's exten and priority to "s" and 1 here,
	 * but this generates (in some cases) an invalid extension, and
	 * if "s" exists, could errantly cause execution of extensions
	 * you don't expect.  It makes more sense to let nature take its
	 * course when chan finishes, and let the pbx do its thing and
	 * hang up when the park is over.
	 */

	/* Answer if call is not up */
	if (ast_channel_state(chan) != AST_STATE_UP) {
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
	return masq_park_call(parkee, parker, &args) ? AST_FEATURE_RETURN_SUCCESS : -1;
}
#endif

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

#if 0
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
#endif

#if 0
/*!
 * \brief Play message to both caller and callee in bridged call, plays synchronously, autoservicing the
 * other channel during the message, so please don't use this for very long messages
 */
static int play_message_in_bridged_call(struct ast_channel *caller_chan, struct ast_channel *callee_chan, const char *audiofile)
{
	return play_message_to_chans(caller_chan, callee_chan, 0, "automon message",
		audiofile);
}
#endif

#if 0
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
	const char *touch_format = NULL;
	const char *touch_monitor = NULL;
	const char *touch_monitor_prefix = NULL;
	struct ast_app *monitor_app;

	monitor_app = pbx_findapp("Monitor");
	if (!monitor_app) {
		ast_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
		return -1;
	}

	set_peers(&caller_chan, &callee_chan, peer, chan, sense);

	/* Find extra messages */
	automon_message_start = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_MESSAGE_START");
	automon_message_stop = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_MESSAGE_STOP");

	if (!ast_strlen_zero(courtesytone)) {	/* Play courtesy tone if configured */
		if(play_message_in_bridged_call(caller_chan, callee_chan, courtesytone) == -1) {
			return -1;
		}
	}

	if (ast_channel_monitor(callee_chan)) {
		ast_verb(4, "User hit '%s' to stop recording call.\n", code);
		if (!ast_strlen_zero(automon_message_stop)) {
			play_message_in_bridged_call(caller_chan, callee_chan, automon_message_stop);
		}
		ast_channel_monitor(callee_chan)->stop(callee_chan, 1);
		return AST_FEATURE_RETURN_SUCCESS;
	}

	touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_FORMAT");
	touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR");
	touch_monitor_prefix = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_PREFIX");

	if (!touch_format)
		touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_FORMAT");

	if (!touch_monitor)
		touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR");

	if (!touch_monitor_prefix)
		touch_monitor_prefix = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_PREFIX");

	if (touch_monitor) {
		len = strlen(touch_monitor) + 50;
		args = ast_alloca(len);
		touch_filename = ast_alloca(len);
		snprintf(touch_filename, len, "%s-%ld-%s", S_OR(touch_monitor_prefix, "auto"), (long)time(NULL), touch_monitor);
		snprintf(args, len, "%s,%s,m", S_OR(touch_format, "wav"), touch_filename);
	} else {
		caller_chan_id = ast_strdupa(S_COR(ast_channel_caller(caller_chan)->id.number.valid,
			ast_channel_caller(caller_chan)->id.number.str, ast_channel_name(caller_chan)));
		callee_chan_id = ast_strdupa(S_COR(ast_channel_caller(callee_chan)->id.number.valid,
			ast_channel_caller(callee_chan)->id.number.str, ast_channel_name(callee_chan)));
		len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
		args = ast_alloca(len);
		touch_filename = ast_alloca(len);
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
#endif

#if 0
static int builtin_automixmonitor(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	char *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL, *touch_filename = NULL;
	int x = 0;
	size_t len;
	struct ast_channel *caller_chan, *callee_chan;
	const char *mixmonitor_spy_type = "MixMonitor";
	const char *touch_format;
	const char *touch_monitor;
	struct ast_app *mixmonitor_app;
	int count = 0;

	mixmonitor_app = pbx_findapp("MixMonitor");
	if (!mixmonitor_app) {
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
			struct ast_app *stopmixmonitor_app;

			stopmixmonitor_app = pbx_findapp("StopMixMonitor");
			if (!stopmixmonitor_app) {
				ast_log(LOG_ERROR,"Cannot stop recording the call. The stopmixmonitor application is disabled.\n");
				return -1;
			}
			pbx_exec(callee_chan, stopmixmonitor_app, "");
			return AST_FEATURE_RETURN_SUCCESS;
		}

		ast_log(LOG_WARNING,"Stopped MixMonitors are attached to the channel.\n");
	}

	touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MIXMONITOR_FORMAT");
	touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MIXMONITOR");

	if (!touch_format)
		touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MIXMONITOR_FORMAT");

	if (!touch_monitor)
		touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MIXMONITOR");

	if (touch_monitor) {
		len = strlen(touch_monitor) + 50;
		args = ast_alloca(len);
		touch_filename = ast_alloca(len);
		snprintf(touch_filename, len, "auto-%ld-%s", (long)time(NULL), touch_monitor);
		snprintf(args, len, "%s.%s,b", touch_filename, (touch_format) ? touch_format : "wav");
	} else {
		caller_chan_id = ast_strdupa(S_COR(ast_channel_caller(caller_chan)->id.number.valid,
			ast_channel_caller(caller_chan)->id.number.str, ast_channel_name(caller_chan)));
		callee_chan_id = ast_strdupa(S_COR(ast_channel_caller(callee_chan)->id.number.valid,
			ast_channel_caller(callee_chan)->id.number.str, ast_channel_name(callee_chan)));
		len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
		args = ast_alloca(len);
		touch_filename = ast_alloca(len);
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
#endif

#if 0
static int builtin_disconnect(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data)
{
	ast_verb(4, "User hit '%s' to disconnect call.\n", code);
	return AST_FEATURE_RETURN_HANGUP;
}
#endif

#if 0
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
		s = ast_channel_macrocontext(transferer);
	}
	if (ast_strlen_zero(s)) {
		s = ast_channel_context(transferer);
	}
	return s;
}
#endif

#if 0
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
			ast_channel_name(c), ast_channel_name(newchan));
		ast_autoservice_chan_hangup_peer(c, newchan);
		return -1;
	}
	return 0;
}
#endif

#if 0
/*!
 * \internal
 * \brief Builtin attended transfer failed cleanup.
 * \since 10.0
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
	if (ast_channel_connected_line_sub(transferee, transferer, connected_line, 0) &&
		ast_channel_connected_line_macro(transferee, transferer, connected_line, 1, 0)) {
		ast_channel_update_connected_line(transferer, connected_line, NULL);
	}
	ast_party_connected_line_free(connected_line);
}
#endif

#if 0
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
	const char *chan1_attended_sound;
	const char *chan2_attended_sound;
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
	struct ast_dial_features *dialfeatures;
	char *transferer_tech;
	char *transferer_name;
	char *transferer_name_orig;
	char *dash;
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg, NULL, ao2_cleanup);

	ast_debug(1, "Executing Attended Transfer %s, %s (sense=%d) \n", ast_channel_name(chan), ast_channel_name(peer), sense);
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

	ast_channel_lock(transferer);
	xfer_cfg = ast_get_chan_features_xfer_config(transferer);
	ast_channel_unlock(transferer);

	/* XXX All accesses to the xfer_cfg structure after this point are not thread-safe,
	 * but I don't care because this is dead code.
	 */

	/* this is specific of atxfer */
	res = ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, xfer_cfg->transferdigittimeout);
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

	/*
	 * Append context to dialed transfer number.
	 *
	 * NOTE: The local channel needs the /n flag so party C will use
	 * the feature flags set by the dialplan when calling that
	 * party.
	 */
	snprintf(xferto + l, sizeof(xferto) - l, "@%s/n", transferer_real_context);

	/* If we are performing an attended transfer and we have two channels involved then
	   copy sound file information to play upon attended transfer completion */
	chan1_attended_sound = pbx_builtin_getvar_helper(transferer, "ATTENDED_TRANSFER_COMPLETE_SOUND");
	chan2_attended_sound = pbx_builtin_getvar_helper(transferee, "ATTENDED_TRANSFER_COMPLETE_SOUND");
	if (!ast_strlen_zero(chan1_attended_sound)) {
		pbx_builtin_setvar_helper(transferer, "BRIDGE_PLAY_SOUND", chan1_attended_sound);
	}
	if (!ast_strlen_zero(chan2_attended_sound)) {
		pbx_builtin_setvar_helper(transferee, "BRIDGE_PLAY_SOUND", chan2_attended_sound);
	}

	/* Extract redial transferer information from the channel name. */
	transferer_name_orig = ast_strdupa(ast_channel_name(transferer));
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
	ast_party_connected_line_copy(&connected_line, ast_channel_connected(transferer));
	ast_channel_unlock(transferer);
	connected_line.source = AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER;

	/* Dial party C */
	newchan = feature_request_and_dial(transferer, transferer_name_orig, transferer,
		transferee, "Local", ast_channel_nativeformats(transferer), xferto,
		xfer_cfg->atxfernoanswertimeout, &outstate, ast_channel_language(transferer));
	ast_debug(2, "Dial party C result: newchan:%d, outstate:%d\n", !!newchan, outstate);

	if (!ast_check_hangup(transferer)) {
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
				if (ast_stream_and_wait(transferer, xfer_cfg->xfersound, "")) {
					ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
				}
				break;
			default:
				if (ast_stream_and_wait(transferer, xfer_cfg->xferfailsound, "")) {
					ast_log(LOG_WARNING, "Failed to play transfer failed sound!\n");
				}
				break;
			}
			atxfer_fail_cleanup(transferee, transferer, &connected_line);
			return AST_FEATURE_RETURN_SUCCESS;
		}

		if (check_compat(transferer, newchan)) {
			if (ast_stream_and_wait(transferer, xfer_cfg->xferfailsound, "")) {
				ast_log(LOG_WARNING, "Failed to play transfer failed sound!\n");
			}
			atxfer_fail_cleanup(transferee, transferer, &connected_line);
			return AST_FEATURE_RETURN_SUCCESS;
		}
		memset(&bconfig,0,sizeof(struct ast_bridge_config));
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_DISCONNECT);
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_DISCONNECT);

		/*
		 * Let party B and C talk as long as they want while party A
		 * languishes in autoservice listening to MOH.
		 */
		ast_bridge_call(transferer, newchan, &bconfig);

		if (ast_check_hangup(newchan) || !ast_check_hangup(transferer)) {
			ast_autoservice_chan_hangup_peer(transferer, newchan);
			if (ast_stream_and_wait(transferer, xfer_cfg->xfersound, "")) {
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

		if (!newchan && !xfer_cfg->atxferdropcall) {
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
					ast_channel_nativeformats(transferee), transferer_name,
					xfer_cfg->atxfernoanswertimeout, &outstate, ast_channel_language(transferer));
				ast_debug(2, "Dial party B result: newchan:%d, outstate:%d\n",
					!!newchan, outstate);
				if (newchan) {
					/*
					 * We have recalled party B (newchan).  We need to give this
					 * call leg the same feature flags as the original party B call
					 * leg.
					 */
					ast_channel_lock(transferer);
					features_datastore = ast_channel_datastore_find(transferer,
						&dial_features_info, NULL);
					if (features_datastore && (dialfeatures = features_datastore->data)) {
						struct ast_flags my_features = { 0 };
						struct ast_flags peer_features = { 0 };

						ast_copy_flags(&my_features, &dialfeatures->my_features,
							AST_FLAGS_ALL);
						ast_copy_flags(&peer_features, &dialfeatures->peer_features,
							AST_FLAGS_ALL);
						ast_channel_unlock(transferer);
						add_features_datastore(newchan, &my_features, &peer_features);
					} else {
						ast_channel_unlock(transferer);
					}
					break;
				}
				if (ast_check_hangup(transferee)) {
					break;
				}

				++tries;
				if (xfer_cfg->atxfercallbackretries <= tries) {
					/* No more callback tries remaining. */
					break;
				}

				if (xfer_cfg->atxferloopdelay) {
					/* Transfer failed, sleeping */
					ast_debug(1, "Sleeping for %d ms before retrying atxfer.\n",
						xfer_cfg->atxferloopdelay);
					ast_safe_sleep(transferee, xfer_cfg->atxferloopdelay);
					if (ast_check_hangup(transferee)) {
						ast_party_connected_line_free(&connected_line);
						return -1;
					}
				}

				/* Retry dialing party C. */
				ast_debug(1, "We're retrying to call %s/%s\n", "Local", xferto);
				newchan = feature_request_and_dial(transferer, transferer_name_orig,
					transferer, transferee, "Local",
					ast_channel_nativeformats(transferee), xferto,
					xfer_cfg->atxfernoanswertimeout, &outstate, ast_channel_language(transferer));
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
			ast_autoservice_chan_hangup_peer(transferee, newchan);
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

	xferchan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", ast_channel_linkedid(transferee), 0, "Transfered/%s", ast_channel_name(transferee));
	if (!xferchan) {
		ast_autoservice_chan_hangup_peer(transferee, newchan);
		ast_party_connected_line_free(&connected_line);
		return -1;
	}

	/* Give party A a momentary ringback tone during transfer. */
	ast_channel_visible_indication_set(xferchan, AST_CONTROL_RINGING);

	/* Make formats okay */
	ast_format_copy(ast_channel_readformat(xferchan), ast_channel_readformat(transferee));
	ast_format_copy(ast_channel_writeformat(xferchan), ast_channel_writeformat(transferee));

	if (ast_channel_masquerade(xferchan, transferee)) {
		ast_hangup(xferchan);
		ast_autoservice_chan_hangup_peer(transferee, newchan);
		ast_party_connected_line_free(&connected_line);
		return -1;
	}

	dash = strrchr(xferto, '@');
	if (dash) {
		/* Trim off the context. */
		*dash = '\0';
	}
	ast_explicit_goto(xferchan, transferer_real_context, xferto, 1);
	ast_channel_state_set(xferchan, AST_STATE_UP);
	ast_clear_flag(ast_channel_flags(xferchan), AST_FLAGS_ALL);

	/* Do the masquerade manually to make sure that is is completed. */
	ast_do_masquerade(xferchan);

	ast_channel_state_set(newchan, AST_STATE_UP);
	ast_clear_flag(ast_channel_flags(newchan), AST_FLAGS_ALL);
	tobj = ast_calloc(1, sizeof(*tobj));
	if (!tobj) {
		ast_hangup(xferchan);
		ast_hangup(newchan);
		ast_party_connected_line_free(&connected_line);
		return -1;
	}

	tobj->chan = newchan;
	tobj->peer = xferchan;
	tobj->bconfig = *config;

	ast_channel_lock(newchan);
	features_datastore = ast_channel_datastore_find(newchan, &dial_features_info, NULL);
	if (features_datastore && (dialfeatures = features_datastore->data)) {
		ast_copy_flags(&tobj->bconfig.features_callee, &dialfeatures->my_features,
			AST_FLAGS_ALL);
	}
	ast_channel_unlock(newchan);

	ast_channel_lock(xferchan);
	features_datastore = ast_channel_datastore_find(xferchan, &dial_features_info, NULL);
	if (features_datastore && (dialfeatures = features_datastore->data)) {
		ast_copy_flags(&tobj->bconfig.features_caller, &dialfeatures->my_features,
			AST_FLAGS_ALL);
	}
	ast_channel_unlock(xferchan);

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
	ast_party_connected_line_copy(&connected_line, ast_channel_connected(transferer));
	ast_channel_unlock(transferer);
	connected_line.source = AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER;
	if (ast_channel_connected_line_sub(newchan, xferchan, &connected_line, 0) &&
		ast_channel_connected_line_macro(newchan, xferchan, &connected_line, 1, 0)) {
		ast_channel_update_connected_line(xferchan, &connected_line, NULL);
	}

	/* Transfer party A connected line to party C */
	ast_channel_lock(xferchan);
	ast_connected_line_copy_from_caller(&connected_line, ast_channel_caller(xferchan));
	ast_channel_unlock(xferchan);
	connected_line.source = AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER;
	if (ast_channel_connected_line_sub(xferchan, newchan, &connected_line, 0) &&
		ast_channel_connected_line_macro(xferchan, newchan, &connected_line, 0, 0)) {
		ast_channel_update_connected_line(newchan, &connected_line, NULL);
	}

	if (ast_stream_and_wait(newchan, xfer_cfg->xfersound, ""))
		ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
	bridge_call_thread_launch(tobj);

	ast_party_connected_line_free(&connected_line);
	return -1;/* The transferee is masqueraded and the original bridged channels can be hungup. */
}
#endif

/*!
 * \internal
 * \brief Get the extension for a given builtin feature
 *
 * \pre expects features_lock to be readlocked
 *
 * \retval 0 success
 * \retval non-zero failiure
 */
static int builtin_feature_get_exten(struct ast_channel *chan, const char *feature_name,
		char *buf, size_t len)
{
	SCOPED_CHANNELLOCK(lock, chan);

	return ast_get_builtin_feature(chan, feature_name, buf, len);
}

static void set_config_flags(struct ast_channel *chan, struct ast_bridge_config *config)
{
/* BUGBUG there is code that checks AST_BRIDGE_IGNORE_SIGS but no code to set it. */
/* BUGBUG there is code that checks AST_BRIDGE_REC_CHANNEL_0 but no code to set it. */
/* BUGBUG there is code that checks AST_BRIDGE_REC_CHANNEL_1 but no code to set it. */
	ast_clear_flag(config, AST_FLAGS_ALL);

	if (ast_test_flag(&config->features_caller, AST_FEATURE_DTMF_MASK)) {
		ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
	}
	if (ast_test_flag(&config->features_callee, AST_FEATURE_DTMF_MASK)) {
		ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
	}

	if (!(ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_0) && ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_1))) {
		RAII_VAR(struct ao2_container *, applicationmap, NULL, ao2_cleanup);

		ast_channel_lock(chan);
		applicationmap = ast_get_chan_applicationmap(chan);
		ast_channel_unlock(chan);

		if (!applicationmap) {
			return;
		}

		/* If an applicationmap exists for this channel at all, then the channel needs the DTMF flag set */
		ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
	}
}

#if 0
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
 * \param addr destination of the call
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
	struct ast_channel *transferee, const char *type, struct ast_format_cap *cap, const char *addr,
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
	char disconnect_code[AST_FEATURE_MAX_LEN];
	char *dialed_code = NULL;
	struct ast_format_cap *tmp_cap;
	struct ast_format best_audio_fmt;
	struct ast_frame *f;
	int disconnect_res;
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_frames;

	tmp_cap = ast_format_cap_alloc_nolock();
	if (!tmp_cap) {
		if (outstate) {
			*outstate = 0;
		}
		return NULL;
	}
	ast_best_codec(cap, &best_audio_fmt);
	ast_format_cap_add(tmp_cap, &best_audio_fmt);

	caller_hungup = ast_check_hangup(caller);

	if (!(chan = ast_request(type, tmp_cap, requestor, addr, &cause))) {
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, addr);
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

	ast_channel_language_set(chan, language);
	ast_channel_inherit_variables(caller, chan);
	pbx_builtin_setvar_helper(chan, "TRANSFERERNAME", caller_name);

	ast_channel_lock(chan);
	ast_connected_line_copy_from_caller(ast_channel_connected(chan), ast_channel_caller(requestor));
	ast_channel_unlock(chan);

	if (ast_call(chan, addr, timeout)) {
		ast_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, addr);
		switch (ast_channel_hangupcause(chan)) {
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
	ast_channel_lock(chan);
	disconnect_res = ast_get_builtin_feature(chan, "disconnect",
			disconnect_code, sizeof(disconnect_code));
	ast_channel_unlock(chan);

	if (!disconnect_res) {
		len = strlen(disconnect_code) + 1;
		dialed_code = ast_alloca(len);
		memset(dialed_code, 0, len);
	}

	x = 0;
	started = ast_tvnow();
	to = timeout;
	AST_LIST_HEAD_INIT_NOLOCK(&deferred_frames);

	ast_poll_channel_add(caller, chan);

	transferee_hungup = 0;
	while (!ast_check_hangup(transferee) && (ast_channel_state(chan) != AST_STATE_UP)) {
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
			ast_log(LOG_NOTICE, "We exceeded our AT-timeout for %s\n", ast_channel_name(chan));
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
			if (!ast_strlen_zero(ast_channel_call_forward(chan))) {
				state = 0;
				ast_autoservice_start(transferee);
				chan = ast_call_forward(caller, chan, NULL, tmp_cap, NULL, &state);
				ast_autoservice_stop(transferee);
				if (!chan) {
					break;
				}
				continue;
			}
			f = ast_read(chan);
			if (f == NULL) { /*doh! where'd he go?*/
				switch (ast_channel_hangupcause(chan)) {
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
					ast_verb(3, "%s is ringing\n", ast_channel_name(chan));
					ast_indicate(caller, AST_CONTROL_RINGING);
				} else if (f->subclass.integer == AST_CONTROL_BUSY) {
					state = f->subclass.integer;
					ast_verb(3, "%s is busy\n", ast_channel_name(chan));
					ast_indicate(caller, AST_CONTROL_BUSY);
					ast_frfree(f);
					break;
				} else if (f->subclass.integer == AST_CONTROL_INCOMPLETE) {
					ast_verb(3, "%s dialed incomplete extension %s; ignoring\n", ast_channel_name(chan), ast_channel_exten(chan));
				} else if (f->subclass.integer == AST_CONTROL_CONGESTION) {
					state = f->subclass.integer;
					ast_verb(3, "%s is congested\n", ast_channel_name(chan));
					ast_indicate(caller, AST_CONTROL_CONGESTION);
					ast_frfree(f);
					break;
				} else if (f->subclass.integer == AST_CONTROL_ANSWER) {
					/* This is what we are hoping for */
					state = f->subclass.integer;
					ast_frfree(f);
					ready=1;
					break;
				} else if (f->subclass.integer == AST_CONTROL_PVT_CAUSE_CODE) {
					ast_indicate_data(caller, AST_CONTROL_PVT_CAUSE_CODE, f->data.ptr, f->datalen);
				} else if (f->subclass.integer == AST_CONTROL_CONNECTED_LINE) {
					if (caller_hungup) {
						struct ast_party_connected_line connected;

						/* Just save it for the transfer. */
						ast_party_connected_line_set_init(&connected, ast_channel_connected(caller));
						res = ast_connected_line_parse_data(f->data.ptr, f->datalen,
							&connected);
						if (!res) {
							ast_channel_set_connected_line(caller, &connected, NULL);
						}
						ast_party_connected_line_free(&connected);
					} else {
						ast_autoservice_start(transferee);
						if (ast_channel_connected_line_sub(chan, caller, f, 1) &&
							ast_channel_connected_line_macro(chan, caller, f, 1, 1)) {
							ast_indicate_data(caller, AST_CONTROL_CONNECTED_LINE,
								f->data.ptr, f->datalen);
						}
						ast_autoservice_stop(transferee);
					}
				} else if (f->subclass.integer == AST_CONTROL_REDIRECTING) {
					if (!caller_hungup) {
						ast_autoservice_start(transferee);
						if (ast_channel_redirecting_sub(chan, caller, f, 1) &&
							ast_channel_redirecting_macro(chan, caller, f, 1, 1)) {
							ast_indicate_data(caller, AST_CONTROL_REDIRECTING,
								f->data.ptr, f->datalen);
						}
						ast_autoservice_stop(transferee);
					}
				} else if (f->subclass.integer != -1
					&& f->subclass.integer != AST_CONTROL_PROGRESS
					&& f->subclass.integer != AST_CONTROL_PROCEEDING) {
					ast_log(LOG_NOTICE, "Don't know what to do about control frame: %d\n", f->subclass.integer);
				}
				/* else who cares */
			} else if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO) {
				ast_write(caller, f);
			}
		} else if (caller == active_channel) {
			f = ast_read(caller);
			if (f) {
				if (f->frametype == AST_FRAME_DTMF && dialed_code) {
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
	if (chan && (ready || ast_channel_state(chan) == AST_STATE_UP)) {
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
#endif

void ast_channel_log(char *title, struct ast_channel *chan);

void ast_channel_log(char *title, struct ast_channel *chan) /* for debug, this is handy enough to justify keeping it in the source */
{
	ast_log(LOG_NOTICE, "______ %s (%lx)______\n", title, (unsigned long) chan);
	ast_log(LOG_NOTICE, "CHAN: name: %s;  appl: %s; data: %s; contxt: %s;  exten: %s; pri: %d;\n",
		ast_channel_name(chan), ast_channel_appl(chan), ast_channel_data(chan),
		ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan));
	ast_log(LOG_NOTICE, "CHAN: acctcode: %s;  dialcontext: %s; amaflags: %x; maccontxt: %s;  macexten: %s; macpri: %d;\n",
		ast_channel_accountcode(chan), ast_channel_dialcontext(chan), ast_channel_amaflags(chan),
		ast_channel_macrocontext(chan), ast_channel_macroexten(chan), ast_channel_macropriority(chan));
	ast_log(LOG_NOTICE, "CHAN: masq: %p;  masqr: %p; uniqueID: %s; linkedID:%s\n",
		ast_channel_masq(chan), ast_channel_masqr(chan),
		ast_channel_uniqueid(chan), ast_channel_linkedid(chan));
	if (ast_channel_masqr(chan)) {
		ast_log(LOG_NOTICE, "CHAN: masquerading as: %s;  cdr: %p;\n",
			ast_channel_name(ast_channel_masqr(chan)), ast_channel_cdr(ast_channel_masqr(chan)));
	}

	ast_log(LOG_NOTICE, "===== done ====\n");
}

static void set_bridge_features_on_config(struct ast_bridge_config *config, const char *features)
{
	const char *feature;

	if (ast_strlen_zero(features)) {
		return;
	}

	for (feature = features; *feature; feature++) {
		struct ast_flags *party;

		if (isupper(*feature)) {
			party = &config->features_caller;
		} else {
			party = &config->features_callee;
		}

		switch (tolower(*feature)) {
		case 't' :
			ast_set_flag(party, AST_FEATURE_REDIRECT);
			break;
		case 'k' :
			ast_set_flag(party, AST_FEATURE_PARKCALL);
			break;
		case 'h' :
			ast_set_flag(party, AST_FEATURE_DISCONNECT);
			break;
		case 'w' :
			ast_set_flag(party, AST_FEATURE_AUTOMON);
			break;
		case 'x' :
			ast_set_flag(party, AST_FEATURE_AUTOMIXMON);
			break;
		default :
			ast_log(LOG_WARNING, "Skipping unknown feature code '%c'\n", *feature);
			break;
		}
	}
}

static void add_features_datastores(struct ast_channel *caller, struct ast_channel *callee, struct ast_bridge_config *config)
{
	if (add_features_datastore(caller, &config->features_caller, &config->features_callee)) {
		/*
		 * If we don't return here, then when we do a builtin_atxfer we
		 * will copy the disconnect flags over from the atxfer to the
		 * callee (Party C).
		 */
		return;
	}

	add_features_datastore(callee, &config->features_callee, &config->features_caller);
}

static void clear_dialed_interfaces(struct ast_channel *chan)
{
	struct ast_datastore *di_datastore;

	ast_channel_lock(chan);
	if ((di_datastore = ast_channel_datastore_find(chan, &dialed_interface_info, NULL))) {
		if (option_debug) {
			ast_log(LOG_DEBUG, "Removing dialed interfaces datastore on %s since we're bridging\n", ast_channel_name(chan));
		}
		if (!ast_channel_datastore_remove(chan, di_datastore)) {
			ast_datastore_free(di_datastore);
		}
	}
	ast_channel_unlock(chan);
}

void ast_bridge_end_dtmf(struct ast_channel *chan, char digit, struct timeval start, const char *why)
{
	int dead;
	long duration;

	ast_channel_lock(chan);
	dead = ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE)
		|| (ast_channel_softhangup_internal_flag(chan)
			& ~(AST_SOFTHANGUP_ASYNCGOTO | AST_SOFTHANGUP_UNBRIDGE));
	ast_channel_unlock(chan);
	if (dead) {
		/* Channel is a zombie or a real hangup. */
		return;
	}

	duration = ast_tvdiff_ms(ast_tvnow(), start);
	ast_senddigit_end(chan, digit, duration);
	ast_log(LOG_DTMF, "DTMF end '%c' simulated on %s due to %s, duration %ld ms\n",
		digit, ast_channel_name(chan), why, duration);
}

/*!
 * \internal
 * \brief Setup bridge builtin features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_bridge_features_builtin(struct ast_bridge_features *features, struct ast_channel *chan)
{
	struct ast_flags *flags;
	char dtmf[AST_FEATURE_MAX_LEN];
	int res;

	ast_channel_lock(chan);
	flags = ast_bridge_features_ds_get(chan);
	ast_channel_unlock(chan);
	if (!flags) {
		return 0;
	}

	res = 0;
	if (ast_test_flag(flags, AST_FEATURE_REDIRECT)) {
		/* Add atxfer and blind transfer. */
		if (!builtin_feature_get_exten(chan, "blindxfer", dtmf, sizeof(dtmf))
				&& !ast_strlen_zero(dtmf)) {
/* BUGBUG need to supply a blind transfer structure and destructor to use other than defaults */
			res |= ast_bridge_features_enable(features, AST_BRIDGE_BUILTIN_BLINDTRANSFER, dtmf,
					NULL, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
		}
		if (!builtin_feature_get_exten(chan, "atxfer", dtmf, sizeof(dtmf)) &&
				!ast_strlen_zero(dtmf)) {
/* BUGBUG need to supply an attended transfer structure and destructor to use other than defaults */
			res |= ast_bridge_features_enable(features, AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER, dtmf,
					NULL, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
		}
	}
	if (ast_test_flag(flags, AST_FEATURE_DISCONNECT) &&
			!builtin_feature_get_exten(chan, "disconnect", dtmf, sizeof(dtmf)) &&
			!ast_strlen_zero(dtmf)) {
		res |= ast_bridge_features_enable(features, AST_BRIDGE_BUILTIN_HANGUP, dtmf,
				NULL, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
	}
	if (ast_test_flag(flags, AST_FEATURE_PARKCALL) &&
			!builtin_feature_get_exten(chan, "parkcall", dtmf, sizeof(dtmf)) &&
			!ast_strlen_zero(dtmf)) {
		res |= ast_bridge_features_enable(features, AST_BRIDGE_BUILTIN_PARKCALL, dtmf,
				NULL, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
	}
	if (ast_test_flag(flags, AST_FEATURE_AUTOMON) &&
			!builtin_feature_get_exten(chan, "automon", dtmf, sizeof(dtmf)) &&
			!ast_strlen_zero(dtmf)) {
		res |= ast_bridge_features_enable(features, AST_BRIDGE_BUILTIN_AUTOMON, dtmf,
				NULL, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
	}
	if (ast_test_flag(flags, AST_FEATURE_AUTOMIXMON) &&
			!builtin_feature_get_exten(chan, "automixmon", dtmf, sizeof(dtmf)) &&
			!ast_strlen_zero(dtmf)) {
		res |= ast_bridge_features_enable(features, AST_BRIDGE_BUILTIN_AUTOMIXMON, dtmf,
				NULL, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
	}

#if 0	/* BUGBUG don't report errors untill all of the builtin features are supported. */
	return res ? -1 : 0;
#else
	return 0;
#endif
}

struct dynamic_dtmf_hook_run {
	/*! Offset into app_name[] where the channel name that activated the hook starts. */
	int activated_offset;
	/*! Offset into app_name[] where the dynamic feature name starts. */
	int feature_offset;
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

static void dynamic_dtmf_hook_callback(struct ast_bridge_channel *bridge_channel,
	const void *payload, size_t payload_size)
{
	struct ast_channel *chan = bridge_channel->chan;
	const struct dynamic_dtmf_hook_run *run_data = payload;

	pbx_builtin_setvar_helper(chan, "DYNAMIC_FEATURENAME",
		&run_data->app_name[run_data->feature_offset]);
	pbx_builtin_setvar_helper(chan, "DYNAMIC_WHO_ACTIVATED",
		&run_data->app_name[run_data->activated_offset]);

	ast_bridge_channel_run_app(bridge_channel, run_data->app_name,
		run_data->app_args_offset ? &run_data->app_name[run_data->app_args_offset] : NULL,
		run_data->moh_offset ? &run_data->app_name[run_data->moh_offset] : NULL);
}

static void dynamic_dtmf_hook_run_callback(struct ast_bridge_channel *bridge_channel,
	ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	callback(bridge_channel, payload, payload_size);
}

struct dynamic_dtmf_hook_data {
	/*! Which side of bridge to run app (AST_FEATURE_FLAG_ONSELF/AST_FEATURE_FLAG_ONPEER) */
	unsigned int flags;
	/*! Offset into app_name[] where the dynamic feature name starts. */
	int feature_offset;
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

/*!
 * \internal
 * \brief Activated dynamic DTMF feature hook.
 * \since 12.0.0
 *
 * \param bridge The bridge that the channel is part of
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
static int dynamic_dtmf_hook_trip(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct dynamic_dtmf_hook_data *pvt = hook_pvt;
	void (*run_it)(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size);
	struct dynamic_dtmf_hook_run *run_data;
	const char *activated_name;
	size_t len_name;
	size_t len_args;
	size_t len_moh;
	size_t len_feature;
	size_t len_activated;
	size_t len_data;

	/* Determine lengths of things. */
	len_name = strlen(pvt->app_name) + 1;
	len_args = pvt->app_args_offset ? strlen(&pvt->app_name[pvt->app_args_offset]) + 1 : 0;
	len_moh = pvt->moh_offset ? strlen(&pvt->app_name[pvt->moh_offset]) + 1 : 0;
	len_feature = strlen(&pvt->app_name[pvt->feature_offset]) + 1;
	ast_channel_lock(bridge_channel->chan);
	activated_name = ast_strdupa(ast_channel_name(bridge_channel->chan));
	ast_channel_unlock(bridge_channel->chan);
	len_activated = strlen(activated_name) + 1;
	len_data = sizeof(*run_data) + len_name + len_args + len_moh + len_feature + len_activated;

	/* Fill in dynamic feature run hook data. */
	run_data = ast_alloca(len_data);
	run_data->app_args_offset = len_args ? len_name : 0;
	run_data->moh_offset = len_moh ? len_name + len_args : 0;
	run_data->feature_offset = len_name + len_args + len_moh;
	run_data->activated_offset = len_name + len_args + len_moh + len_feature;
	strcpy(run_data->app_name, pvt->app_name);/* Safe */
	if (len_args) {
		strcpy(&run_data->app_name[run_data->app_args_offset],
			&pvt->app_name[pvt->app_args_offset]);/* Safe */
	}
	if (len_moh) {
		strcpy(&run_data->app_name[run_data->moh_offset],
			&pvt->app_name[pvt->moh_offset]);/* Safe */
	}
	strcpy(&run_data->app_name[run_data->feature_offset],
		&pvt->app_name[pvt->feature_offset]);/* Safe */
	strcpy(&run_data->app_name[run_data->activated_offset], activated_name);/* Safe */

	if (ast_test_flag(pvt, AST_FEATURE_FLAG_ONPEER)) {
		run_it = ast_bridge_channel_write_callback;
	} else {
		run_it = dynamic_dtmf_hook_run_callback;
	}
	run_it(bridge_channel, dynamic_dtmf_hook_callback, run_data, len_data);
	return 0;
}

/*!
 * \internal
 * \brief Add a dynamic DTMF feature hook to the bridge features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param flags Which side of bridge to run app (AST_FEATURE_FLAG_ONSELF/AST_FEATURE_FLAG_ONPEER).
 * \param dtmf DTMF trigger sequence.
 * \param feature_name Name of the dynamic feature.
 * \param app_name Dialplan application name to run.
 * \param app_args Dialplan application arguments. (Empty or NULL if no arguments)
 * \param moh_class MOH class to play to peer. (Empty or NULL if no MOH played)
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int dynamic_dtmf_hook_add(struct ast_bridge_features *features, unsigned int flags, const char *dtmf, const char *feature_name, const char *app_name, const char *app_args, const char *moh_class)
{
	struct dynamic_dtmf_hook_data *hook_data;
	size_t len_name = strlen(app_name) + 1;
	size_t len_args = ast_strlen_zero(app_args) ? 0 : strlen(app_args) + 1;
	size_t len_moh = ast_strlen_zero(moh_class) ? 0 : strlen(moh_class) + 1;
	size_t len_feature = strlen(feature_name) + 1;
	size_t len_data = sizeof(*hook_data) + len_name + len_args + len_moh + len_feature;

	/* Fill in application run hook data. */
	hook_data = ast_malloc(len_data);
	if (!hook_data) {
		return -1;
	}
	hook_data->flags = flags;
	hook_data->app_args_offset = len_args ? len_name : 0;
	hook_data->moh_offset = len_moh ? len_name + len_args : 0;
	hook_data->feature_offset = len_name + len_args + len_moh;
	strcpy(hook_data->app_name, app_name);/* Safe */
	if (len_args) {
		strcpy(&hook_data->app_name[hook_data->app_args_offset], app_args);/* Safe */
	}
	if (len_moh) {
		strcpy(&hook_data->app_name[hook_data->moh_offset], moh_class);/* Safe */
	}
	strcpy(&hook_data->app_name[hook_data->feature_offset], feature_name);/* Safe */

	return ast_bridge_dtmf_hook(features, dtmf, dynamic_dtmf_hook_trip, hook_data,
		ast_free_ptr, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
}

static int setup_dynamic_feature(void *obj, void *arg, void *data, int flags)
{
	struct ast_applicationmap_item *item = obj;
	struct ast_bridge_features *features = arg;
	int *res = data;

	*res |= dynamic_dtmf_hook_add(features,
		item->activate_on_self ? AST_FEATURE_FLAG_ONSELF : AST_FEATURE_FLAG_ONPEER,
		item->dtmf, item->name, item->app, item->app_data, item->moh_class);

	return 0;
}

/*!
 * \internal
 * \brief Setup bridge dynamic features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_bridge_features_dynamic(struct ast_bridge_features *features, struct ast_channel *chan)
{
	RAII_VAR(struct ao2_container *, applicationmap, NULL, ao2_cleanup);
	int res = 0;

	ast_channel_lock(chan);
	applicationmap = ast_get_chan_applicationmap(chan);
	ast_channel_unlock(chan);
	if (!applicationmap) {
		return 0;
	}

	ao2_callback_data(applicationmap, 0, setup_dynamic_feature, features, &res);

	return res;
}

/* BUGBUG this really should be made a private function of bridging_basic.c after struct ast_call_feature is made an ao2 object. */
int ast_bridge_channel_setup_features(struct ast_bridge_channel *bridge_channel)
{
	int res = 0;

	/* Always pass through any DTMF digits. */
	bridge_channel->features->dtmf_passthrough = 1;

	res |= setup_bridge_features_builtin(bridge_channel->features, bridge_channel->chan);
	res |= setup_bridge_features_dynamic(bridge_channel->features, bridge_channel->chan);

	return res;
}

static void bridge_config_set_limits_warning_values(struct ast_bridge_config *config, struct ast_bridge_features_limits *limits)
{
	if (config->end_sound) {
		ast_string_field_set(limits, duration_sound, config->end_sound);
	}

	if (config->warning_sound) {
		ast_string_field_set(limits, warning_sound, config->warning_sound);
	}

	if (config->start_sound) {
		ast_string_field_set(limits, connect_sound, config->start_sound);
	}

	limits->frequency = config->warning_freq;
	limits->warning = config->play_warning;
}

/*!
 * \internal brief Setup limit hook structures on calls that need limits
 *
 * \param config ast_bridge_config which provides the limit data
 * \param caller_limits pointer to an ast_bridge_features_limits struct which will store the caller side limits
 * \param callee_limits pointer to an ast_bridge_features_limits struct which will store the callee side limits
 */
static void bridge_config_set_limits(struct ast_bridge_config *config, struct ast_bridge_features_limits *caller_limits, struct ast_bridge_features_limits *callee_limits)
{
	if (ast_test_flag(&config->features_caller, AST_FEATURE_PLAY_WARNING)) {
		bridge_config_set_limits_warning_values(config, caller_limits);
	}

	if (ast_test_flag(&config->features_callee, AST_FEATURE_PLAY_WARNING)) {
		bridge_config_set_limits_warning_values(config, callee_limits);
	}

	caller_limits->duration = config->timelimit;
	callee_limits->duration = config->timelimit;
}

/*!
 * \internal
 * \brief Check if Monitor needs to be started on a channel.
 * \since 12.0.0
 *
 * \param chan The bridge considers this channel the caller.
 * \param peer The bridge considers this channel the callee.
 *
 * \return Nothing
 */
static void bridge_check_monitor(struct ast_channel *chan, struct ast_channel *peer)
{
	const char *value;
	const char *monitor_args = NULL;
	struct ast_channel *monitor_chan = NULL;

	ast_channel_lock(chan);
	value = pbx_builtin_getvar_helper(chan, "AUTO_MONITOR");
	if (!ast_strlen_zero(value)) {
		monitor_args = ast_strdupa(value);
		monitor_chan = chan;
	}
	ast_channel_unlock(chan);
	if (!monitor_chan) {
		ast_channel_lock(peer);
		value = pbx_builtin_getvar_helper(peer, "AUTO_MONITOR");
		if (!ast_strlen_zero(value)) {
			monitor_args = ast_strdupa(value);
			monitor_chan = peer;
		}
		ast_channel_unlock(peer);
	}
	if (monitor_chan) {
		struct ast_app *monitor_app;

		monitor_app = pbx_findapp("Monitor");
		if (monitor_app) {
			pbx_exec(monitor_chan, monitor_app, monitor_args);
		}
	}
}

/*!
 * \internal
 * \brief Send the peer channel on its way on bridge start failure.
 * \since 12.0.0
 *
 * \param chan Chan to put into autoservice.
 * \param peer Chan to send to after bridge goto or run hangup handlers and hangup.
 *
 * \return Nothing
 */
static void bridge_failed_peer_goto(struct ast_channel *chan, struct ast_channel *peer)
{
	if (ast_after_bridge_goto_setup(peer)
		|| ast_pbx_start(peer)) {
		ast_autoservice_chan_hangup_peer(chan, peer);
	}
}

static int pre_bridge_setup(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config,
		struct ast_bridge_features *chan_features, struct ast_bridge_features *peer_features)
{
	int res;

	set_bridge_features_on_config(config, pbx_builtin_getvar_helper(chan, "BRIDGE_FEATURES"));
	add_features_datastores(chan, peer, config);

	/*
	 * This is an interesting case.  One example is if a ringing
	 * channel gets redirected to an extension that picks up a
	 * parked call.  This will make sure that the call taken out of
	 * parking gets told that the channel it just got bridged to is
	 * still ringing.
	 */
	if (ast_channel_state(chan) == AST_STATE_RINGING
		&& ast_channel_visible_indication(peer) != AST_CONTROL_RINGING) {
		ast_indicate(peer, AST_CONTROL_RINGING);
	}

	bridge_check_monitor(chan, peer);

	set_config_flags(chan, config);

	/* Answer if need be */
	if (ast_channel_state(chan) != AST_STATE_UP) {
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
	ast_channel_set_linkgroup(chan, peer);

	/*
	 * If we are bridging a call, stop worrying about forwarding
	 * loops.  We presume that if a call is being bridged, that the
	 * humans in charge know what they're doing.  If they don't,
	 * well, what can we do about that?
	 */
	clear_dialed_interfaces(chan);
	clear_dialed_interfaces(peer);

	res = 0;
	ast_channel_lock(chan);
	res |= ast_bridge_features_ds_set(chan, &config->features_caller);
	ast_channel_unlock(chan);
	ast_channel_lock(peer);
	res |= ast_bridge_features_ds_set(peer, &config->features_callee);
	ast_channel_unlock(peer);

	if (res) {
		return -1;
	}

	if (config->timelimit) {
		struct ast_bridge_features_limits call_duration_limits_chan;
		struct ast_bridge_features_limits call_duration_limits_peer;
		int abandon_call = 0; /* TRUE if set limits fails so we can abandon the call. */

		if (ast_bridge_features_limits_construct(&call_duration_limits_chan)) {
			ast_log(LOG_ERROR, "Could not construct caller duration limits. Bridge canceled.\n");

			return -1;
		}

		if (ast_bridge_features_limits_construct(&call_duration_limits_peer)) {
			ast_log(LOG_ERROR, "Could not construct callee duration limits. Bridge canceled.\n");
			ast_bridge_features_limits_destroy(&call_duration_limits_chan);

			return -1;
		}

		bridge_config_set_limits(config, &call_duration_limits_chan, &call_duration_limits_peer);

		if (ast_bridge_features_set_limits(chan_features, &call_duration_limits_chan, 0)) {
			abandon_call = 1;
		}
		if (ast_bridge_features_set_limits(peer_features, &call_duration_limits_peer, 0)) {
			abandon_call = 1;
		}

		/* At this point we are done with the limits structs since they have been copied to the individual feature sets. */
		ast_bridge_features_limits_destroy(&call_duration_limits_chan);
		ast_bridge_features_limits_destroy(&call_duration_limits_peer);

		if (abandon_call) {
			ast_log(LOG_ERROR, "Could not set duration limits on one or more sides of the call. Bridge canceled.\n");
			return -1;
		}
	}

	return 0;
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
	int res;
	struct ast_bridge *bridge;
	struct ast_bridge_features chan_features;
	struct ast_bridge_features *peer_features;

	/* Setup features. */
	res = ast_bridge_features_init(&chan_features);
	peer_features = ast_bridge_features_new();
	if (res || !peer_features) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	if (pre_bridge_setup(chan, peer, config, &chan_features, peer_features)) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	/* Create bridge */
	bridge = ast_bridge_basic_new();
	if (!bridge) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	/* Put peer into the bridge */
	if (ast_bridge_impart(bridge, peer, NULL, peer_features, 1)) {
		ast_bridge_destroy(bridge);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	/* Join bridge */
	ast_bridge_join(bridge, chan, NULL, &chan_features, NULL, 1);

	/*
	 * If the bridge was broken for a hangup that isn't real, then
	 * don't run the h extension, because the channel isn't really
	 * hung up.  This should really only happen with
	 * AST_SOFTHANGUP_ASYNCGOTO.
	 */
	res = -1;
	ast_channel_lock(chan);
	if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
		res = 0;
	}
	ast_channel_unlock(chan);

	ast_bridge_features_cleanup(&chan_features);

/* BUGBUG this is used by Dial and FollowMe for CDR information.  By Queue for Queue stats like CDRs. */
	if (res && config->end_bridge_callback) {
		config->end_bridge_callback(config->end_bridge_callback_data);
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
		ast_channel_name(pu->chan),
		pu->parkinglot->name,
		S_COR(ast_channel_caller(pu->chan)->id.number.valid, ast_channel_caller(pu->chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(pu->chan)->id.name.valid, ast_channel_caller(pu->chan)->id.name.str, "<unknown>"),
		S_COR(ast_channel_connected(pu->chan)->id.number.valid, ast_channel_connected(pu->chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_connected(pu->chan)->id.name.valid, ast_channel_connected(pu->chan)->id.name.str, "<unknown>"),
		ast_channel_uniqueid(pu->chan)
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
			char parkingslot[AST_MAX_EXTENSION]; /* buffer for parkinglot slot number */
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
				char comebackdialtime[AST_MAX_EXTENSION];
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

					snprintf(returnexten, sizeof(returnexten), "%s,%u,%s", peername,
						pu->parkinglot->cfg.comebackdialtime,
						callback_dialoptions(&dialfeatures->peer_features,
							&dialfeatures->my_features, buf, sizeof(buf)));
				} else { /* Existing default */
					ast_log(LOG_NOTICE, "Dial features not found on %s, using default!\n",
						ast_channel_name(chan));
					snprintf(returnexten, sizeof(returnexten), "%s,%u,t", peername,
						pu->parkinglot->cfg.comebackdialtime);
				}
				ast_channel_unlock(chan);

				snprintf(comebackdialtime, sizeof(comebackdialtime), "%u",
						pu->parkinglot->cfg.comebackdialtime);
				pbx_builtin_setvar_helper(chan, "COMEBACKDIALTIME", comebackdialtime);

				pbx_builtin_setvar_helper(chan, "PARKER", peername);

			}

			snprintf(parkingslot, sizeof(parkingslot), "%d", pu->parkingnum);
			pbx_builtin_setvar_helper(chan, "PARKINGSLOT", parkingslot);
			pbx_builtin_setvar_helper(chan, "PARKEDLOT", pu->parkinglot->name);

			if (pu->options_specified) {
				/*
				 * Park() was called with overriding return arguments, respect
				 * those arguments.
				 */
				set_c_e_p(chan, pu->context, pu->exten, pu->priority);
			} else if (pu->parkinglot->cfg.comebacktoorigin) {
				set_c_e_p(chan, parking_con_dial, peername_flat, 1);
			} else {
				/* Handle fallback when extensions don't exist here since that logic was removed from pbx */
				if (ast_exists_extension(chan, pu->parkinglot->cfg.comebackcontext, peername_flat, 1, NULL)) {
					set_c_e_p(chan, pu->parkinglot->cfg.comebackcontext, peername_flat, 1);
				} else if (ast_exists_extension(chan, pu->parkinglot->cfg.comebackcontext, "s", 1, NULL)) {
					ast_verb(2, "Can not start %s at %s,%s,1. Using 's@%s' instead.\n", ast_channel_name(chan),
						pu->parkinglot->cfg.comebackcontext, peername_flat, pu->parkinglot->cfg.comebackcontext);
					set_c_e_p(chan, pu->parkinglot->cfg.comebackcontext, "s", 1);
				} else {
					ast_verb(2, "Can not start %s at %s,%s,1 and exten 's@%s' does not exist. Using 's@default'\n",
						ast_channel_name(chan),
						pu->parkinglot->cfg.comebackcontext, peername_flat,
						pu->parkinglot->cfg.comebackcontext);
					set_c_e_p(chan, "default", "s", 1);
				}
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

		ast_verb(2, "Timeout for %s parked on %d (%s). Returning to %s,%s,%d\n",
			ast_channel_name(pu->chan), pu->parkingnum, pu->parkinglot->name, ast_channel_context(pu->chan),
			ast_channel_exten(pu->chan), ast_channel_priority(pu->chan));

		/* Start up the PBX, or hang them up */
		if (ast_pbx_start(chan))  {
			ast_log(LOG_WARNING,
				"Unable to restart the PBX for user on '%s', hanging them up...\n",
				ast_channel_name(pu->chan));
			ast_hangup(chan);
		}

		/* And take them out of the parking lot */
		return 1;
	}

	/* still within parking time, process descriptors */
	if (pfds) {
		for (x = 0; x < AST_MAX_FDS; x++) {
			struct ast_frame *f;
			int y;

			if (!ast_channel_fd_isset(chan, x)) {
				continue;	/* nothing on this descriptor */
			}

			for (y = 0; y < nfds; y++) {
				if (pfds[y].fd == ast_channel_fd(chan, x)) {
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
				ast_set_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION);
			} else {
				ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION);
			}
			ast_channel_fdno_set(chan, x);

			/* See if they need servicing */
			f = ast_read(pu->chan);
			/* Hangup? */
			if (!f || (f->frametype == AST_FRAME_CONTROL
				&& f->subclass.integer == AST_CONTROL_HANGUP)) {
				if (f) {
					ast_frfree(f);
				}
				post_manager_event("ParkedCallGiveUp", pu);

				/* There's a problem, hang them up */
				ast_verb(2, "%s got tired of being parked\n", ast_channel_name(chan));
				ast_hangup(chan);

				/* And take them out of the parking lot */
				return 1;
			} else {
				/* XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
				ast_frfree(f);
				if (pu->hold_method == AST_CONTROL_HOLD
					&& pu->moh_trys < 3
					&& !ast_channel_generatordata(chan)) {
					ast_debug(1,
						"MOH on parked call stopped by outside source.  Restarting on channel %s.\n",
						ast_channel_name(chan));
					ast_indicate_data(chan, AST_CONTROL_HOLD,
						S_OR(pu->parkinglot->cfg.mohclass, NULL),
						(!ast_strlen_zero(pu->parkinglot->cfg.mohclass)
							? strlen(pu->parkinglot->cfg.mohclass) + 1 : 0));
					pu->moh_trys++;
				}
				break;
			}
		} /* End for */
	}

	/* mark fds for next round */
	for (x = 0; x < AST_MAX_FDS; x++) {
		if (ast_channel_fd_isset(chan, x)) {
			void *tmp = ast_realloc(*new_pfds,
				(*new_nfds + 1) * sizeof(struct pollfd));

			if (!tmp) {
				continue;
			}
			*new_pfds = tmp;
			(*new_pfds)[*new_nfds].fd = ast_channel_fd(chan, x);
			(*new_pfds)[*new_nfds].events = POLLIN | POLLERR | POLLPRI;
			(*new_pfds)[*new_nfds].revents = 0;
			(*new_nfds)++;
		}
	}
	/* Keep track of our shortest wait */
	if (tms < *ms || *ms < 0) {
		*ms = tms;
	}

	/* Stay in the parking lot. */
	return 0;
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

/*! Default configuration for default parking lot. */
static const struct parkinglot_cfg parkinglot_cfg_default_default = {
	.mohclass = "default",
	.parkext = DEFAULT_PARK_EXTENSION,
	.parking_con = "parkedcalls",
	.parking_start = 701,
	.parking_stop = 750,
	.parkingtime = DEFAULT_PARK_TIME,
	.comebackdialtime = DEFAULT_COMEBACK_DIAL_TIME,
	.comebackcontext = DEFAULT_COMEBACK_CONTEXT,
	.comebacktoorigin = DEFAULT_COMEBACK_TO_ORIGIN,
};

/*! Default configuration for normal parking lots. */
static const struct parkinglot_cfg parkinglot_cfg_default = {
	.parkext = DEFAULT_PARK_EXTENSION,
	.parkingtime = DEFAULT_PARK_TIME,
	.comebackdialtime = DEFAULT_COMEBACK_DIAL_TIME,
	.comebackcontext = DEFAULT_COMEBACK_CONTEXT,
	.comebacktoorigin = DEFAULT_COMEBACK_TO_ORIGIN,
};

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
	/* XXX All parking stuff is being replaced by res_parking */
	parkinglot->disabled = 1;
	return -1;
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

	res = ast_features_config_reload();
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
 * \internal
 * \brief Add an arbitrary channel to a bridge
 *
 * The channel that is being added to the bridge can be in any state: unbridged,
 * bridged, answered, unanswered, etc. The channel will be added asynchronously,
 * meaning that when this function returns once the channel has been added to
 * the bridge, not once the channel has been removed from the bridge.
 *
 * In addition, a tone can optionally be played to the channel once the
 * channel is placed into the bridge.
 *
 * \note When this function returns, there is no guarantee that the channel that
 * was passed in is valid any longer. Do not attempt to operate on the channel
 * after this function returns.
 *
 * \param bridge Bridge to which the channel should be added
 * \param chan The channel to add to the bridge
 * \param features Features for this channel in the bridge
 * \param play_tone Indicates if a tone should be played to the channel
 * \retval 0 Success
 * \retval -1 Failure
 */
static int add_to_bridge(struct ast_bridge *bridge, struct ast_channel *chan,
		struct ast_bridge_features *features, int play_tone)
{
	RAII_VAR(struct ast_bridge *, chan_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg, NULL, ao2_cleanup);
	struct ast_channel *bridge_chan = NULL;
	const char *tone = NULL;

	ast_channel_lock(chan);
	chan_bridge = ast_channel_get_bridge(chan);
	xfer_cfg = ast_get_chan_features_xfer_config(chan);
	if (!xfer_cfg) {
		ast_log(LOG_ERROR, "Unable to determine what tone to play to channel.\n");
	} else {
		tone = ast_strdupa(xfer_cfg->xfersound);
	}
	ast_channel_unlock(chan);

	if (chan_bridge) {
		if (ast_bridge_move(bridge, chan_bridge, chan, NULL, 1)) {
			return -1;
		}
	} else {
		/* Slightly less easy case. We need to yank channel A from
		 * where he currently is and impart him into our bridge.
		 */
		bridge_chan = ast_channel_yank(chan);
		if (!bridge_chan) {
			ast_log(LOG_WARNING, "Could not gain control of channel %s\n", ast_channel_name(chan));
			return -1;
		}
		if (ast_channel_state(bridge_chan) != AST_STATE_UP) {
			ast_answer(bridge_chan);
		}
		if (ast_bridge_impart(bridge, bridge_chan, NULL, features, 1)) {
			ast_log(LOG_WARNING, "Could not add %s to the bridge\n", ast_channel_name(chan));
			return -1;
		}
	}

	if (play_tone && !ast_strlen_zero(tone)) {
		struct ast_channel *play_chan = bridge_chan ?: chan;
		RAII_VAR(struct ast_bridge_channel *, play_bridge_channel, NULL, ao2_cleanup);

		ast_channel_lock(play_chan);
		play_bridge_channel = ast_channel_get_bridge_channel(play_chan);
		ast_channel_unlock(play_chan);

		if (!play_bridge_channel) {
			ast_log(LOG_WARNING, "Unable to play tone for channel %s. Unable to get bridge channel\n",
					ast_channel_name(play_chan));
		} else {
			ast_bridge_channel_queue_playfile(play_bridge_channel, NULL, tone, NULL);
		}
	}
	return 0;
}

enum play_tone_action {
	PLAYTONE_NONE = 0,
	PLAYTONE_CHANNEL1 = (1 << 0),
	PLAYTONE_CHANNEL2 = (1 << 1),
	PLAYTONE_BOTH = PLAYTONE_CHANNEL1 | PLAYTONE_CHANNEL2,
};

static enum play_tone_action parse_playtone(const char *playtone_val)
{
	if (ast_strlen_zero(playtone_val) || ast_false(playtone_val)) {
		return PLAYTONE_NONE;
	} if (!strcasecmp(playtone_val, "channel1")) {
		return PLAYTONE_CHANNEL1;
	} else if (!strcasecmp(playtone_val, "channel2") || ast_true(playtone_val)) {
		return PLAYTONE_CHANNEL2;
	} else if (!strcasecmp(playtone_val, "both")) {
		return PLAYTONE_BOTH;
	} else {
		/* Invalid input. Assume none */
		return PLAYTONE_NONE;
	}
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
 * \retval 0
 */
static int action_bridge(struct mansession *s, const struct message *m)
{
	const char *channela = astman_get_header(m, "Channel1");
	const char *channelb = astman_get_header(m, "Channel2");
	enum play_tone_action playtone = parse_playtone(astman_get_header(m, "Tone"));
	RAII_VAR(struct ast_channel *, chana, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chanb, NULL, ao2_cleanup);
	const char *chana_name;
	const char *chana_exten;
	const char *chana_context;
	int chana_priority;
	const char *chanb_name;
	const char *chanb_exten;
	const char *chanb_context;
	int chanb_priority;
	struct ast_bridge *bridge;
	char buf[256];

	/* make sure valid channels were specified */
	if (ast_strlen_zero(channela) || ast_strlen_zero(channelb)) {
		astman_send_error(s, m, "Missing channel parameter in request");
		return 0;
	}

	/* Start with chana */
	chana = ast_channel_get_by_name_prefix(channela, strlen(channela));
	if (!chana) {
		snprintf(buf, sizeof(buf), "Channel1 does not exist: %s", channela);
		astman_send_error(s, m, buf);
		return 0;
	}
	ast_channel_lock(chana);
	chana_name = ast_strdupa(ast_channel_name(chana));
	chana_exten = ast_strdupa(ast_channel_exten(chana));
	chana_context = ast_strdupa(ast_channel_context(chana));
	chana_priority = ast_channel_priority(chana);
	if (!ast_test_flag(ast_channel_flags(chana), AST_FLAG_IN_AUTOLOOP)) {
		chana_priority++;
	}
	ast_channel_unlock(chana);

	chanb = ast_channel_get_by_name_prefix(channelb, strlen(channelb));
	if (!chanb) {
		snprintf(buf, sizeof(buf), "Channel2 does not exist: %s", channelb);
		astman_send_error(s, m, buf);
		return 0;
	}
	ast_channel_lock(chanb);
	chanb_name = ast_strdupa(ast_channel_name(chanb));
	chanb_exten = ast_strdupa(ast_channel_exten(chanb));
	chanb_context = ast_strdupa(ast_channel_context(chanb));
	chanb_priority = ast_channel_priority(chanb);
	if (!ast_test_flag(ast_channel_flags(chanb), AST_FLAG_IN_AUTOLOOP)) {
		chanb_priority++;
	}
	ast_channel_unlock(chanb);

	bridge = ast_bridge_basic_new();
	if (!bridge) {
		astman_send_error(s, m, "Unable to create bridge\n");
		return 0;
	}

	ast_after_bridge_set_go_on(chana, chana_context, chana_exten, chana_priority, NULL);
	if (add_to_bridge(bridge, chana, NULL, playtone & PLAYTONE_CHANNEL1)) {
		snprintf(buf, sizeof(buf), "Unable to add Channel1 to bridge: %s", ast_channel_name(chana));
		astman_send_error(s, m, buf);
		ast_bridge_destroy(bridge);
		return 0;
	}

	ast_after_bridge_set_go_on(chanb, chanb_context, chanb_exten, chanb_priority, NULL);
	if (add_to_bridge(bridge, chanb, NULL, playtone & PLAYTONE_CHANNEL2)) {
		snprintf(buf, sizeof(buf), "Unable to add Channel2 to bridge: %s", ast_channel_name(chanb));
		astman_send_error(s, m, buf);
		ast_bridge_destroy(bridge);
		return 0;
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a bridge is successfully created due to a manager action.</synopsis>
			<syntax>
				<parameter name="Response">
					<enumlist>
						<enum name="Success"/>
						<enum name="Failed"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="manager">Bridge</ref>
			</see-also>
		</managerEventInstance>
	***/
/* BUGBUG This event used to use ast_manager_event_multichan. Now channel variables are not included in the event */
	manager_event(EVENT_FLAG_CALL, "BridgeAction",
				"Response: Success\r\n"
				"Channel1: %s\r\n"
				"Channel2: %s\r\n", chana_name, chanb_name);

	astman_send_ack(s, m, "Channels have been bridged");

	return 0;
}

static struct ast_cli_entry cli_features[] = {
	AST_CLI_DEFINE(handle_features_reload, "Reloads configured features"),
};

/*!
 * \brief Create manager event for parked calls
 * \param s
 * \param m
 *
 * Get channels involved in park, create event.
 * \return Always 0
 *
 * \note ADSI is not compatible with this AMI action for the
 * same reason ch2 can no longer announce the parking space.
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
	struct ast_park_call_args args = {
			/*
			 * Don't say anything to ch2 since AMI is a third party parking
			 * a call and we will likely crash if we do.
			 *
			 * XXX When the AMI action was originally implemented, the
			 * parking space was announced to ch2.  Unfortunately, grabbing
			 * the ch2 lock and holding it while the announcement is played
			 * was not really a good thing to do to begin with since it
			 * could hold up the system.  Also holding the lock is no longer
			 * possible with a masquerade.
			 *
			 * Restoring the announcement to ch2 is not easily doable for
			 * the following reasons:
			 *
			 * 1) The AMI manager is not the thread processing ch2.
			 *
			 * 2) ch2 could be the same as ch1, bridged to ch1, or some
			 * random uninvolved channel.
			 */
			.flags = AST_PARK_OPT_SILENCE,
		};

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

	res = masq_park_call(ch1, ch2, &args);
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
	if (!ast_channel_pbx(chan) && !ast_channel_masq(chan) && !ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE)
		&& (ast_channel_state(chan) == AST_STATE_RINGING
			|| ast_channel_state(chan) == AST_STATE_RING
			/*
			 * Check the down state as well because some SIP devices do not
			 * give 180 ringing when they can just give 183 session progress
			 * instead.  Issue 14005.  (Some ISDN switches as well for that
			 * matter.)
			 */
			|| ast_channel_state(chan) == AST_STATE_DOWN)
		&& !ast_channel_datastore_find(chan, &pickup_active, NULL)) {
		return 1;
	}
	return 0;
}

static int find_channel_by_group(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	struct ast_channel *chan = arg;/*!< Channel wanting to pickup call */

	if (chan == target) {
		return 0;
	}

	ast_channel_lock(target);
	if (ast_can_pickup(target)) {
		/* Lock both channels. */
		while (ast_channel_trylock(chan)) {
			ast_channel_unlock(target);
			sched_yield();
			ast_channel_lock(target);
		}

		/*
		 * Both callgroup and namedcallgroup pickup variants are
		 * matched independently.  Checking for named group match is
		 * done last since it's a more expensive operation.
		 */
		if ((ast_channel_pickupgroup(chan) & ast_channel_callgroup(target))
			|| (ast_namedgroups_intersect(ast_channel_named_pickupgroups(chan),
				ast_channel_named_callgroups(target)))) {
			struct ao2_container *candidates = data;/*!< Candidate channels found. */

			/* This is a candidate to pickup */
			ao2_link(candidates, target);
		}
		ast_channel_unlock(chan);
	}
	ast_channel_unlock(target);

	return 0;
}

struct ast_channel *ast_pickup_find_by_group(struct ast_channel *chan)
{
	struct ao2_container *candidates;/*!< Candidate channels found to pickup. */
	struct ast_channel *target;/*!< Potential pickup target */

	candidates = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 1, NULL, NULL);
	if (!candidates) {
		return NULL;
	}

	/* Find all candidate targets by group. */
	ast_channel_callback(find_channel_by_group, chan, candidates, 0);

	/* Find the oldest pickup target candidate */
	target = NULL;
	for (;;) {
		struct ast_channel *candidate;/*!< Potential new older target */
		struct ao2_iterator iter;

		iter = ao2_iterator_init(candidates, 0);
		while ((candidate = ao2_iterator_next(&iter))) {
			if (!target) {
				/* First target. */
				target = candidate;
				continue;
			}
			if (ast_tvcmp(ast_channel_creationtime(candidate), ast_channel_creationtime(target)) < 0) {
				/* We have a new target. */
				ast_channel_unref(target);
				target = candidate;
				continue;
			}
			ast_channel_unref(candidate);
		}
		ao2_iterator_destroy(&iter);
		if (!target) {
			/* No candidates found. */
			break;
		}

		/* The found channel must be locked and ref'd. */
		ast_channel_lock(target);

		/* Recheck pickup ability */
		if (ast_can_pickup(target)) {
			/* This is the channel to pickup. */
			break;
		}

		/* Someone else picked it up or the call went away. */
		ast_channel_unlock(target);
		ao2_unlink(candidates, target);
		target = ast_channel_unref(target);
	}
	ao2_ref(candidates, -1);

	return target;
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
	RAII_VAR(struct ast_features_pickup_config *, pickup_cfg, NULL, ao2_cleanup);
	const char *pickup_sound;
	const char *fail_sound;

	ast_debug(1, "pickup attempt by %s\n", ast_channel_name(chan));
	ast_channel_lock(chan);
	pickup_cfg = ast_get_chan_features_pickup_config(chan);
	if (!pickup_cfg) {
		ast_log(LOG_ERROR, "Unable to retrieve pickup configuration. Unable to play pickup sounds\n");
	}
	pickup_sound = ast_strdupa(pickup_cfg ? pickup_cfg->pickupsound : "");
	fail_sound = ast_strdupa(pickup_cfg ? pickup_cfg->pickupfailsound : "");
	ast_channel_unlock(chan);

	/* The found channel is already locked. */
	target = ast_pickup_find_by_group(chan);
	if (target) {
		ast_log(LOG_NOTICE, "pickup %s attempt by %s\n", ast_channel_name(target), ast_channel_name(chan));

		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		if (!res) {
			if (!ast_strlen_zero(pickup_sound)) {
				pbx_builtin_setvar_helper(target, "BRIDGE_PLAY_SOUND", pickup_sound);
			}
		} else {
			ast_log(LOG_WARNING, "pickup %s failed by %s\n", ast_channel_name(target), ast_channel_name(chan));
		}
		target = ast_channel_unref(target);
	}

	if (res < 0) {
		ast_debug(1, "No call pickup possible... for %s\n", ast_channel_name(chan));
		if (!ast_strlen_zero(fail_sound)) {
			ast_answer(chan);
			ast_stream_and_wait(chan, fail_sound, "");
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

	target_name = ast_strdupa(ast_channel_name(target));
	ast_debug(1, "Call pickup on '%s' by '%s'\n", target_name, ast_channel_name(chan));

	/* Mark the target to block any call pickup race. */
	ds_pickup = ast_datastore_alloc(&pickup_active, NULL);
	if (!ds_pickup) {
		ast_log(LOG_WARNING,
			"Unable to create channel datastore on '%s' for call pickup\n", target_name);
		return -1;
	}
	ast_channel_datastore_add(target, ds_pickup);

	ast_party_connected_line_init(&connected_caller);
	ast_party_connected_line_copy(&connected_caller, ast_channel_connected(target));
	ast_channel_unlock(target);/* The pickup race is avoided so we do not need the lock anymore. */
	/* Reset any earlier private connected id representation */
	ast_party_id_reset(&connected_caller.priv);

	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
	if (ast_channel_connected_line_sub(NULL, chan, &connected_caller, 0) &&
		ast_channel_connected_line_macro(NULL, chan, &connected_caller, 0, 0)) {
		ast_channel_update_connected_line(chan, &connected_caller, NULL);
	}
	ast_party_connected_line_free(&connected_caller);

	ast_channel_lock(chan);
	chan_name = ast_strdupa(ast_channel_name(chan));
	ast_connected_line_copy_from_caller(&connected_caller, ast_channel_caller(chan));
	ast_channel_unlock(chan);
	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;

	ast_cel_report_event(target, AST_CEL_PICKUP, NULL, NULL, chan);

	if (ast_answer(chan)) {
		ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan_name);
		goto pickup_failed;
	}

	if (ast_queue_control(chan, AST_CONTROL_ANSWER)) {
		ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan_name);
		goto pickup_failed;
	}

	ast_channel_queue_connected_line_update(chan, &connected_caller, NULL);

	/* setting the HANGUPCAUSE so the ringing channel knows this call was not a missed call */
	ast_channel_hangupcause_set(chan, AST_CAUSE_ANSWERED_ELSEWHERE);

	if (ast_channel_move(target, chan)) {
		ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan_name,
			target_name);
		goto pickup_failed;
	}

	/* If you want UniqueIDs, set channelvars in manager.conf to CHANNEL(uniqueid) */
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a call pickup occurs.</synopsis>
			<syntax>
				<parameter name="Channel"><para>The name of the channel that initiated the pickup.</para></parameter>
				<parameter name="TargetChannel"><para>The name of the channel that is being picked up.</para></parameter>
			</syntax>
		</managerEventInstance>
	***/
	ast_manager_event_multichan(EVENT_FLAG_CALL, "Pickup", 2, chans,
		"Channel: %s\r\n"
		"TargetChannel: %s\r\n",
		chan_name, target_name);

	res = 0;

pickup_failed:
	ast_channel_lock(target);
	if (!ast_channel_datastore_remove(target, ds_pickup)) {
		ast_datastore_free(ds_pickup);
	}
	ast_party_connected_line_free(&connected_caller);

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
	OPT_CALLEE_GO_ON = (1 << 12),
};

enum {
	OPT_ARG_DURATION_LIMIT = 0,
	OPT_ARG_DURATION_STOP,
	OPT_ARG_CALLEE_GO_ON,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(bridge_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION('p', BRIDGE_OPT_PLAYTONE),
	AST_APP_OPTION_ARG('F', OPT_CALLEE_GO_ON, OPT_ARG_CALLEE_GO_ON),
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
		play_to_caller = 0;
		play_to_callee = 0;
		config->timelimit = 0;
		config->play_warning = 0;
		config->warning_freq = 0;
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
	RAII_VAR(struct ast_channel *, current_dest_chan, NULL, ao2_cleanup);
	struct ast_channel *chans[2];
	char *tmp_data  = NULL;
	struct ast_flags opts = { 0, };
	struct ast_bridge_config bconfig = { { 0, }, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct timeval calldurationlimit = { 0, };
	const char *context;
	const char *extension;
	int priority;
	struct ast_bridge_features chan_features;
	struct ast_bridge_features *peer_features;
	struct ast_bridge *bridge;

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

	/* make sure we have a valid end point */
	if (!(current_dest_chan = ast_channel_get_by_name_prefix(args.dest_chan,
			strlen(args.dest_chan)))) {
		ast_log(LOG_WARNING, "Bridge failed because channel %s does not exist\n",
			args.dest_chan);
		ast_manager_event(chan, EVENT_FLAG_CALL, "BridgeExec",
			"Response: Failed\r\n"
			"Reason: Channel2 does not exist\r\n"
			"Channel1: %s\r\n"
			"Channel2: %s\r\n", ast_channel_name(chan), args.dest_chan);
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "NONEXISTENT");
		return 0;
	}

	/* avoid bridge with ourselves */
	if (chan == current_dest_chan) {
		ast_log(LOG_WARNING, "Unable to bridge channel %s with itself\n", ast_channel_name(chan));
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when an error occurs during bridge creation.</synopsis>
				<see-also>
					<ref type="application">Bridge</ref>
				</see-also>
			</managerEventInstance>
		***/
		ast_manager_event(chan, EVENT_FLAG_CALL, "BridgeExec",
			"Response: Failed\r\n"
			"Reason: Unable to bridge channel to itself\r\n"
			"Channel1: %s\r\n"
			"Channel2: %s\r\n",
			ast_channel_name(chan), args.dest_chan);
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "LOOP");
		return 0;
	}

	if (ast_test_flag(&opts, OPT_DURATION_LIMIT)
		&& !ast_strlen_zero(opt_args[OPT_ARG_DURATION_LIMIT])
		&& ast_bridge_timelimit(chan, &bconfig, opt_args[OPT_ARG_DURATION_LIMIT], &calldurationlimit)) {
		ast_manager_event(chan, EVENT_FLAG_CALL, "BridgeExec",
			"Response: Failed\r\n"
			"Reason: Cannot setup bridge time limit\r\n"
			"Channel1: %s\r\n"
			"Channel2: %s\r\n", ast_channel_name(chan), args.dest_chan);
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "FAILURE");
		goto done;
	}

	chans[0] = chan;
	chans[1] = current_dest_chan;

	/* Report that the bridge will be successfull */
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when the bridge is created successfully.</synopsis>
			<see-also>
				<ref type="application">Bridge</ref>
			</see-also>
		</managerEventInstance>
	***/
	ast_manager_event_multichan(EVENT_FLAG_CALL, "BridgeExec", 2, chans,
		"Response: Success\r\n"
		"Channel1: %s\r\n"
		"Channel2: %s\r\n", ast_channel_name(chan), ast_channel_name(current_dest_chan));

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

	/* Setup after bridge goto location. */
	if (ast_test_flag(&opts, OPT_CALLEE_GO_ON)) {
		ast_channel_lock(chan);
		context = ast_strdupa(ast_channel_context(chan));
		extension = ast_strdupa(ast_channel_exten(chan));
		priority = ast_channel_priority(chan);
		ast_channel_unlock(chan);
		ast_after_bridge_set_go_on(current_dest_chan, context, extension, priority,
			opt_args[OPT_ARG_CALLEE_GO_ON]);
	} else if (!ast_test_flag(&opts, OPT_CALLEE_KILL)) {
		ast_channel_lock(current_dest_chan);
		context = ast_strdupa(ast_channel_context(current_dest_chan));
		extension = ast_strdupa(ast_channel_exten(current_dest_chan));
		priority = ast_channel_priority(current_dest_chan);
		ast_channel_unlock(current_dest_chan);
		ast_after_bridge_set_goto(current_dest_chan, context, extension, priority);
	}

	if (ast_bridge_features_init(&chan_features)) {
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	peer_features = ast_bridge_features_new();
	if (!peer_features) {
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	if (pre_bridge_setup(chan, current_dest_chan, &bconfig, &chan_features, peer_features)) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	bridge = ast_bridge_basic_new();
	if (!bridge) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	if (add_to_bridge(bridge, current_dest_chan, peer_features, ast_test_flag(&opts, BRIDGE_OPT_PLAYTONE))) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		ast_bridge_destroy(bridge);
		goto done;
	}

	ast_bridge_join(bridge, chan, NULL, &chan_features, NULL, 1);

	ast_bridge_features_cleanup(&chan_features);

	/* The bridge has ended, set BRIDGERESULT to SUCCESS. */
	pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "SUCCESS");
done:
	ast_free((char *) bconfig.warning_sound);
	ast_free((char *) bconfig.end_sound);
	ast_free((char *) bconfig.start_sound);

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
	struct ast_format tmp_fmt;

	if (!(test_channel1 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
		NULL, NULL, 0, 0, "TestChannel1"))) {
		ast_log(LOG_WARNING, "Whoa, test channel creation failed.\n");
		return NULL;
	}

	/* normally this is done in the channel driver */
	ast_format_cap_add(ast_channel_nativeformats(test_channel1), ast_format_set(&tmp_fmt, AST_FORMAT_GSM, 0));

	ast_format_set(ast_channel_writeformat(test_channel1), AST_FORMAT_GSM, 0);
	ast_format_set(ast_channel_rawwriteformat(test_channel1), AST_FORMAT_GSM, 0);
	ast_format_set(ast_channel_readformat(test_channel1), AST_FORMAT_GSM, 0);
	ast_format_set(ast_channel_rawreadformat(test_channel1), AST_FORMAT_GSM, 0);

	ast_channel_tech_set(test_channel1, fake_tech);

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
	if (masq_park_call(test_channel1, NULL, &args)) {
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

/*! \internal \brief Clean up resources on Asterisk shutdown */
static void features_shutdown(void)
{
	ast_features_config_shutdown();

	ast_cli_unregister_multiple(cli_features, ARRAY_LEN(cli_features));
	ast_devstate_prov_del("Park");
	ast_manager_unregister("Bridge");
	ast_manager_unregister("Park");

	ast_unregister_application(app_bridge);

	pthread_cancel(parking_thread);
	pthread_kill(parking_thread, SIGURG);
	pthread_join(parking_thread, NULL);
	ast_context_destroy(NULL, registrar);
	ao2_ref(parkinglots, -1);
}

int ast_features_init(void)
{
	int res;

	parkinglots = ao2_container_alloc(7, parkinglot_hash_cb, parkinglot_cmp_cb);
	if (!parkinglots) {
		return -1;
	}

	res = ast_features_config_init();
	if (res) {
		return res;
	}
	ast_cli_register_multiple(cli_features, ARRAY_LEN(cli_features));
	if (ast_pthread_create(&parking_thread, NULL, do_parking_thread, NULL)) {
		ast_features_config_shutdown();
		ast_cli_unregister_multiple(cli_features, ARRAY_LEN(cli_features));
		return -1;
	}
	res |= ast_register_application2(app_bridge, bridge_exec, NULL, NULL, NULL);
	res |= ast_manager_register_xml_core("Park", EVENT_FLAG_CALL, manager_park);
	res |= ast_manager_register_xml_core("Bridge", EVENT_FLAG_CALL, action_bridge);

	res |= ast_devstate_prov_add("Park", metermaidstate);
#if defined(TEST_FRAMEWORK)
	res |= AST_TEST_REGISTER(features_test);
#endif	/* defined(TEST_FRAMEWORK) */

	if (res) {
		features_shutdown();
	} else {
		ast_register_atexit(features_shutdown);
	}

	return res;
}
