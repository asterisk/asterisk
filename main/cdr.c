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
 * \brief Call Detail Record API
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \note Includes code and algorithms from the Zapata library.
 *
 * \note We do a lot of checking here in the CDR code to try to be sure we don't ever let a CDR slip
 * through our fingers somehow.  If someone allocates a CDR, it must be completely handled normally
 * or a WARNING shall be logged, so that we can best keep track of any escape condition where the CDR
 * isn't properly generated and posted.
 */

/*! \li \ref cdr.c uses the configuration file \ref cdr.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cdr.conf cdr.conf
 * \verbinclude cdr.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <signal.h>
#include <inttypes.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/callerid.h"
#include "asterisk/manager.h"
#include "asterisk/causes.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/sched.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/stringfields.h"
#include "asterisk/data.h"
#include "asterisk/config_options.h"
#include "asterisk/json.h"
#include "asterisk/parking.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/astobj2.h"

/*** DOCUMENTATION
	<configInfo name="cdr" language="en_US">
		<synopsis>Call Detail Record configuration</synopsis>
		<description>
			<para>CDR is Call Detail Record, which provides logging services via a variety of
			pluggable backend modules. Detailed call information can be recorded to
			databases, files, etc. Useful for billing, fraud prevention, compliance with
			Sarbanes-Oxley aka The Enron Act, QOS evaluations, and more.</para>
		</description>
		<configFile name="cdr.conf">
			<configObject name="general">
				<synopsis>Global settings applied to the CDR engine.</synopsis>
				<configOption name="debug">
					<synopsis>Enable/disable verbose CDR debugging.</synopsis>
					<description><para>When set to <literal>True</literal>, verbose updates
					of changes in CDR information will be logged. Note that this is only
					of use when debugging CDR behavior.</para>
					</description>
				</configOption>
				<configOption name="enable">
					<synopsis>Enable/disable CDR logging.</synopsis>
					<description><para>Define whether or not to use CDR logging. Setting this to "no" will override
					any loading of backend CDR modules.  Default is "yes".</para>
					</description>
				</configOption>
				<configOption name="unanswered">
					<synopsis>Log calls that are never answered and don't set an outgoing party.</synopsis>
					<description><para>
					Define whether or not to log unanswered calls that don't involve an outgoing party. Setting
					this to "yes" will make calls to extensions that don't answer and don't set a side B channel
					(such as by using the Dial application) receive CDR log entries. If this option is set to
					"no", then those log entries will not be created. Unanswered calls which get offered to an
					outgoing line will always receive log entries regardless of this option, and that is the
					intended behavior.
					</para>
					</description>
				</configOption>
				<configOption name="congestion">
					<synopsis>Log congested calls.</synopsis>
					<description><para>Define whether or not to log congested calls. Setting this to "yes" will
					report each call that fails to complete due to congestion conditions.</para>
					</description>
				</configOption>
				<configOption name="endbeforehexten">
					<synopsis>Don't produce CDRs while executing hangup logic</synopsis>
					<description>
						<para>As each CDR for a channel is finished, its end time is updated
						and the CDR is finalized. When a channel is hung up and hangup
						logic is present (in the form of a hangup handler or the
						<literal>h</literal> extension), a new CDR is generated for the
						channel. Any statistics are gathered from this new CDR. By enabling
						this option, no new CDR is created for the dialplan logic that is
						executed in <literal>h</literal> extensions or attached hangup handler
						subroutines. The default value is <literal>yes</literal>, indicating
						that a CDR will be generated during hangup logic.</para>
					</description>
				</configOption>
				<configOption name="initiatedseconds">
					<synopsis>Count microseconds for billsec purposes</synopsis>
					<description><para>Normally, the <literal>billsec</literal> field logged to the CDR backends
					is simply the end time (hangup time) minus the answer time in seconds. Internally,
					asterisk stores the time in terms of microseconds and seconds. By setting
					initiatedseconds to <literal>yes</literal>, you can force asterisk to report any seconds
					that were initiated (a sort of round up method). Technically, this is
					when the microsecond part of the end time is greater than the microsecond
					part of the answer time, then the billsec time is incremented one second.</para>
					</description>
				</configOption>
				<configOption name="batch">
					<synopsis>Submit CDRs to the backends for processing in batches</synopsis>
					<description><para>Define the CDR batch mode, where instead of posting the CDR at the end of
					every call, the data will be stored in a buffer to help alleviate load on the
					asterisk server.</para>
					<warning><para>Use of batch mode may result in data loss after unsafe asterisk termination,
					i.e., software crash, power failure, kill -9, etc.</para>
					</warning>
					</description>
				</configOption>
				<configOption name="size">
					<synopsis>The maximum number of CDRs to accumulate before triggering a batch</synopsis>
					<description><para>Define the maximum number of CDRs to accumulate in the buffer before posting
					them to the backend engines. batch must be set to <literal>yes</literal>.</para>
					</description>
				</configOption>
				<configOption name="time">
					<synopsis>The maximum time to accumulate CDRs before triggering a batch</synopsis>
					<description><para>Define the maximum time to accumulate CDRs before posting them in a batch to the
					backend engines. If this time limit is reached, then it will post the records, regardless of the value
					defined for size. batch must be set to <literal>yes</literal>.</para>
					<note><para>Time is expressed in seconds.</para></note>
					</description>
				</configOption>
				<configOption name="scheduleronly">
					<synopsis>Post batched CDRs on their own thread instead of the scheduler</synopsis>
					<description><para>The CDR engine uses the internal asterisk scheduler to determine when to post
					records.  Posting can either occur inside the scheduler thread, or a new
					thread can be spawned for the submission of every batch.  For small batches,
					it might be acceptable to just use the scheduler thread, so set this to <literal>yes</literal>.
					For large batches, say anything over size=10, a new thread is recommended, so
					set this to <literal>no</literal>.</para>
					</description>
				</configOption>
				<configOption name="safeshutdown">
					<synopsis>Block shutdown of Asterisk until CDRs are submitted</synopsis>
					<description><para>When shutting down asterisk, you can block until the CDRs are submitted.  If
					you don't, then data will likely be lost.  You can always check the size of
					the CDR batch buffer with the CLI <astcli>cdr status</astcli> command.  To enable blocking on
					submission of CDR data during asterisk shutdown, set this to <literal>yes</literal>.</para>
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/


/* The prime here should be similar in size to the channel container. */
#ifdef LOW_MEMORY
#define NUM_CDR_BUCKETS 61
#else
#define NUM_CDR_BUCKETS 769
#endif

#define DEFAULT_ENABLED "1"
#define DEFAULT_BATCHMODE "0"
#define DEFAULT_UNANSWERED "0"
#define DEFAULT_CONGESTION "0"
#define DEFAULT_END_BEFORE_H_EXTEN "1"
#define DEFAULT_INITIATED_SECONDS "0"

#define DEFAULT_BATCH_SIZE "100"
#define MAX_BATCH_SIZE 1000
#define DEFAULT_BATCH_TIME "300"
#define MAX_BATCH_TIME 86400
#define DEFAULT_BATCH_SCHEDULER_ONLY "0"
#define DEFAULT_BATCH_SAFE_SHUTDOWN "1"

#define CDR_DEBUG(mod_cfg, fmt, ...) \
	do { \
		if (ast_test_flag(&(mod_cfg)->general->settings, CDR_DEBUG)) { \
			ast_verbose((fmt), ##__VA_ARGS__); \
		} \
	} while (0)

static void cdr_detach(struct ast_cdr *cdr);
static void cdr_submit_batch(int shutdown);
static int cdr_toggle_runtime_options(void);

/*! \brief The configuration settings for this module */
struct module_config {
	struct ast_cdr_config *general;		/*< CDR global settings */
};

/*! \brief The container for the module configuration */
static AO2_GLOBAL_OBJ_STATIC(module_configs);

/*! \brief The type definition for general options */
static struct aco_type general_option = {
	.type = ACO_GLOBAL,
	.name = "general",
	.item_offset = offsetof(struct module_config, general),
	.category = "^general$",
	.category_match = ACO_WHITELIST,
};

static void *module_config_alloc(void);
static void module_config_destructor(void *obj);

/*! \brief The file definition */
static struct aco_file module_file_conf = {
	.filename = "cdr.conf",
	.skip_category = "(^csv$|^custom$|^manager$|^odbc$|^pgsql$|^radius$|^sqlite$|^tds$|^mysql$)",
	.types = ACO_TYPES(&general_option),
};

CONFIG_INFO_CORE("cdr", cfg_info, module_configs, module_config_alloc,
	.files = ACO_FILES(&module_file_conf),
);

static struct aco_type *general_options[] = ACO_TYPES(&general_option);

/*! \brief Dispose of a module config object */
static void module_config_destructor(void *obj)
{
	struct module_config *cfg = obj;

	if (!cfg) {
		return;
	}
	ao2_ref(cfg->general, -1);
}

/*! \brief Create a new module config object */
static void *module_config_alloc(void)
{
	struct module_config *mod_cfg;
	struct ast_cdr_config *cdr_config;

	mod_cfg = ao2_alloc(sizeof(*mod_cfg), module_config_destructor);
	if (!mod_cfg) {
		return NULL;
	}

	cdr_config = ao2_alloc(sizeof(*cdr_config), NULL);
	if (!cdr_config) {
		ao2_ref(cdr_config, -1);
		return NULL;
	}
	mod_cfg->general = cdr_config;

	return mod_cfg;
}

/*! \brief Registration object for CDR backends */
struct cdr_beitem {
	char name[20];
	char desc[80];
	ast_cdrbe be;
	AST_RWLIST_ENTRY(cdr_beitem) list;
	int suspended:1;
};

/*! \brief List of registered backends */
static AST_RWLIST_HEAD_STATIC(be_list, cdr_beitem);

/*! \brief Queued CDR waiting to be batched */
struct cdr_batch_item {
	struct ast_cdr *cdr;
	struct cdr_batch_item *next;
};

/*! \brief The actual batch queue */
static struct cdr_batch {
	int size;
	struct cdr_batch_item *head;
	struct cdr_batch_item *tail;
} *batch = NULL;

/*! \brief The global sequence counter used for CDRs */
static int global_cdr_sequence =  0;

/*! \brief Scheduler items */
static struct ast_sched_context *sched;
static int cdr_sched = -1;
AST_MUTEX_DEFINE_STATIC(cdr_sched_lock);
static pthread_t cdr_thread = AST_PTHREADT_NULL;

/*! \brief Lock protecting modifications to the batch queue */
AST_MUTEX_DEFINE_STATIC(cdr_batch_lock);

/*! \brief These are used to wake up the CDR thread when there's work to do */
AST_MUTEX_DEFINE_STATIC(cdr_pending_lock);
static ast_cond_t cdr_pending_cond;

/*! \brief A container of the active CDRs indexed by Party A channel id */
static struct ao2_container *active_cdrs_by_channel;

/*! \brief Message router for stasis messages regarding channel state */
static struct stasis_message_router *stasis_router;

/*! \brief Our subscription for bridges */
static struct stasis_forward *bridge_subscription;

/*! \brief Our subscription for channels */
static struct stasis_forward *channel_subscription;

/*! \brief Our subscription for parking */
static struct stasis_forward *parking_subscription;

/*! \brief The parent topic for all topics we want to aggregate for CDRs */
static struct stasis_topic *cdr_topic;

/*! \brief A message type used to synchronize with the CDR topic */
STASIS_MESSAGE_TYPE_DEFN_LOCAL(cdr_sync_message_type);

struct cdr_object;

/*! \brief Return types for \ref process_bridge_enter functions */
enum process_bridge_enter_results {
	/*!
	 * The CDR was the only party in the bridge.
	 */
	BRIDGE_ENTER_ONLY_PARTY,
	/*!
	 * The CDR was able to obtain a Party B from some other party already in the bridge
	 */
	BRIDGE_ENTER_OBTAINED_PARTY_B,
	/*!
	 * The CDR was not able to obtain a Party B
	 */
	BRIDGE_ENTER_NO_PARTY_B,
	/*!
	 * This CDR can't handle a bridge enter message and a new CDR needs to be created
	 */
	BRIDGE_ENTER_NEED_CDR,
};

/*!
 * \brief A virtual table used for \ref cdr_object.
 *
 * Note that all functions are optional - if a subclass does not need an
 * implementation, it is safe to leave it NULL.
 */
struct cdr_object_fn_table {
	/*! \brief Name of the subclass */
	const char *name;

	/*!
	 * \brief An initialization function. This will be called automatically
	 * when a \ref cdr_object is switched to this type in
	 * \ref cdr_object_transition_state
	 *
	 * \param cdr The \ref cdr_object that was just transitioned
	 */
	void (* const init_function)(struct cdr_object *cdr);

	/*!
	 * \brief Process a Party A update for the \ref cdr_object
	 *
	 * \param cdr The \ref cdr_object to process the update
	 * \param snapshot The snapshot for the CDR's Party A
	 * \retval 0 the CDR handled the update or ignored it
	 * \retval 1 the CDR is finalized and a new one should be made to handle it
	 */
	int (* const process_party_a)(struct cdr_object *cdr,
			struct ast_channel_snapshot *snapshot);

	/*!
	 * \brief Process a Party B update for the \ref cdr_object
	 *
	 * \param cdr The \ref cdr_object to process the update
	 * \param snapshot The snapshot for the CDR's Party B
	 */
	void (* const process_party_b)(struct cdr_object *cdr,
			struct ast_channel_snapshot *snapshot);

	/*!
	 * \brief Process the beginning of a dial. A dial message implies one of two
	 * things:
	 * The \ref cdr_object's Party A has been originated
	 * The \ref cdr_object's Party A is dialing its Party B
	 *
	 * \param cdr The \ref cdr_object
	 * \param caller The originator of the dial attempt
	 * \param peer The destination of the dial attempt
	 *
	 * \retval 0 if the parties in the dial were handled by this CDR
	 * \retval 1 if the parties could not be handled by this CDR
	 */
	int (* const process_dial_begin)(struct cdr_object *cdr,
			struct ast_channel_snapshot *caller,
			struct ast_channel_snapshot *peer);

	/*!
	 * \brief Process the end of a dial. At the end of a dial, a CDR can be
	 * transitioned into one of two states - DialedPending
	 * (\ref dialed_pending_state_fn_table) or Finalized
	 * (\ref finalized_state_fn_table).
	 *
	 * \param cdr The \ref cdr_object
	 * \param caller The originator of the dial attempt
	 * \param peer the Destination of the dial attempt
	 * \param dial_status What happened
	 *
	 * \retval 0 if the parties in the dial were handled by this CDR
	 * \retval 1 if the parties could not be handled by this CDR
	 */
	int (* const process_dial_end)(struct cdr_object *cdr,
			struct ast_channel_snapshot *caller,
			struct ast_channel_snapshot *peer,
			const char *dial_status);

	/*!
	 * \brief Process the entering of a bridge by this CDR. The purpose of this
	 * callback is to have the CDR prepare itself for the bridge and attempt to
	 * find a valid Party B. The act of creating new CDRs based on the entering
	 * of this channel into the bridge is handled by the higher level message
	 * handler.
	 *
	 * Note that this handler is for when a channel enters into a "normal"
	 * bridge, where people actually talk to each other. Parking is its own
	 * thing.
	 *
	 * \param cdr The \ref cdr_object
	 * \param bridge The bridge that the Party A just entered into
	 * \param channel The \ref ast_channel_snapshot for this CDR's Party A
	 *
	 * \retval process_bridge_enter_results Defines whether or not this CDR was able
	 * to fully handle the bridge enter message.
	 */
	enum process_bridge_enter_results (* const process_bridge_enter)(
			struct cdr_object *cdr,
			struct ast_bridge_snapshot *bridge,
			struct ast_channel_snapshot *channel);

	/*!
	 * \brief Process entering into a parking bridge.
	 *
	 * \param cdr The \ref cdr_object
	 * \param bridge The parking bridge that Party A just entered into
	 * \param channel The \ref ast_channel_snapshot for this CDR's Party A
	 *
	 * \retval 0 This CDR successfully transitioned itself into the parked state
	 * \retval 1 This CDR couldn't handle the parking transition and we need a
	 *  new CDR.
	 */
	int (* const process_parking_bridge_enter)(struct cdr_object *cdr,
			struct ast_bridge_snapshot *bridge,
			struct ast_channel_snapshot *channel);

	/*!
	 * \brief Process the leaving of a bridge by this CDR.
	 *
	 * \param cdr The \ref cdr_object
	 * \param bridge The bridge that the Party A just left
	 * \param channel The \ref ast_channel_snapshot for this CDR's Party A
	 *
	 * \retval 0 This CDR left successfully
	 * \retval 1 Error
	 */
	int (* const process_bridge_leave)(struct cdr_object *cdr,
			struct ast_bridge_snapshot *bridge,
			struct ast_channel_snapshot *channel);

	/*!
	 * \brief Process an update informing us that the channel got itself parked
	 *
	 * \param cdr The \ref cdr_object
	 * \param channel The parking information for this CDR's party A
	 *
	 * \retval 0 This CDR successfully parked itself
	 * \retval 1 This CDR couldn't handle the park
	 */
	int (* const process_parked_channel)(struct cdr_object *cdr,
			struct ast_parked_call_payload *parking_info);
};

static int base_process_party_a(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot);
static enum process_bridge_enter_results base_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);
static int base_process_bridge_leave(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);
static int base_process_dial_end(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer, const char *dial_status);
static int base_process_parked_channel(struct cdr_object *cdr, struct ast_parked_call_payload *parking_info);

static void single_state_init_function(struct cdr_object *cdr);
static void single_state_process_party_b(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot);
static int single_state_process_dial_begin(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer);
static enum process_bridge_enter_results single_state_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);
static int single_state_process_parking_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);

/*!
 * \brief The virtual table for the Single state.
 *
 * A \ref cdr_object starts off in this state. This represents a channel that
 * has no Party B information itself.
 *
 * A \ref cdr_object from this state can go into any of the following states:
 * * \ref dial_state_fn_table
 * * \ref bridge_state_fn_table
 * * \ref finalized_state_fn_table
 */
struct cdr_object_fn_table single_state_fn_table = {
	.name = "Single",
	.init_function = single_state_init_function,
	.process_party_a = base_process_party_a,
	.process_party_b = single_state_process_party_b,
	.process_dial_begin = single_state_process_dial_begin,
	.process_dial_end = base_process_dial_end,
	.process_bridge_enter = single_state_process_bridge_enter,
	.process_parking_bridge_enter = single_state_process_parking_bridge_enter,
	.process_bridge_leave = base_process_bridge_leave,
	.process_parked_channel = base_process_parked_channel,
};

static void dial_state_process_party_b(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot);
static int dial_state_process_dial_begin(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer);
static int dial_state_process_dial_end(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer, const char *dial_status);
static enum process_bridge_enter_results dial_state_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);

/*!
 * \brief The virtual table for the Dial state.
 *
 * A \ref cdr_object that has begun a dial operation. This state is entered when
 * the Party A for a CDR is determined to be dialing out to a Party B or when
 * a CDR is for an originated channel (in which case the Party A information is
 * the originated channel, and there is no Party B).
 *
 * A \ref cdr_object from this state can go in any of the following states:
 * * \ref dialed_pending_state_fn_table
 * * \ref bridge_state_fn_table
 * * \ref finalized_state_fn_table
 */
struct cdr_object_fn_table dial_state_fn_table = {
	.name = "Dial",
	.process_party_a = base_process_party_a,
	.process_party_b = dial_state_process_party_b,
	.process_dial_begin = dial_state_process_dial_begin,
	.process_dial_end = dial_state_process_dial_end,
	.process_bridge_enter = dial_state_process_bridge_enter,
	.process_bridge_leave = base_process_bridge_leave,
};

static int dialed_pending_state_process_party_a(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot);
static int dialed_pending_state_process_dial_begin(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer);
static enum process_bridge_enter_results dialed_pending_state_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);
static int dialed_pending_state_process_parking_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);

/*!
 * \brief The virtual table for the Dialed Pending state.
 *
 * A \ref cdr_object that has successfully finished a dial operation, but we
 * don't know what they're going to do yet. It's theoretically possible to dial
 * a party and then have that party not be bridged with the caller; likewise,
 * an origination can complete and the channel go off and execute dialplan. The
 * pending state acts as a bridge between either:
 * * Entering a bridge
 * * Getting a new CDR for new dialplan execution
 * * Switching from being originated to executing dialplan
 *
 * A \ref cdr_object from this state can go in any of the following states:
 * * \ref single_state_fn_table
 * * \ref dialed_pending_state_fn_table
 * * \ref bridge_state_fn_table
 * * \ref finalized_state_fn_table
 */
struct cdr_object_fn_table dialed_pending_state_fn_table = {
	.name = "DialedPending",
	.process_party_a = dialed_pending_state_process_party_a,
	.process_dial_begin = dialed_pending_state_process_dial_begin,
	.process_bridge_enter = dialed_pending_state_process_bridge_enter,
	.process_parking_bridge_enter = dialed_pending_state_process_parking_bridge_enter,
	.process_bridge_leave = base_process_bridge_leave,
	.process_parked_channel = base_process_parked_channel,
};

static void bridge_state_process_party_b(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot);
static int bridge_state_process_bridge_leave(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);

/*!
 * \brief The virtual table for the Bridged state
 *
 * A \ref cdr_object enters this state when it receives notification that the
 * channel has entered a bridge.
 *
 * A \ref cdr_object from this state can go to:
 * * \ref finalized_state_fn_table
 */
struct cdr_object_fn_table bridge_state_fn_table = {
	.name = "Bridged",
	.process_party_a = base_process_party_a,
	.process_party_b = bridge_state_process_party_b,
	.process_bridge_leave = bridge_state_process_bridge_leave,
	.process_parked_channel = base_process_parked_channel,
};

static int parked_state_process_bridge_leave(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel);

/*!
 * \brief The virtual table for the Parked state
 *
 * Parking is weird. Unlike typical bridges, it has to be treated somewhat
 * uniquely - a channel in a parking bridge (which is a subclass of a holding
 * bridge) has to be handled as if the channel went into an application.
 * However, when the channel comes out, we need a new CDR - unlike the Single
 * state.
 */
struct cdr_object_fn_table parked_state_fn_table = {
	.name = "Parked",
	.process_party_a = base_process_party_a,
	.process_bridge_leave = parked_state_process_bridge_leave,
	.process_parked_channel = base_process_parked_channel,
};

static void finalized_state_init_function(struct cdr_object *cdr);
static int finalized_state_process_party_a(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot);

/*!
 * \brief The virtual table for the finalized state.
 *
 * Once in the finalized state, the CDR is done. No modifications can be made
 * to the CDR.
 */
struct cdr_object_fn_table finalized_state_fn_table = {
	.name = "Finalized",
	.init_function = finalized_state_init_function,
	.process_party_a = finalized_state_process_party_a,
	.process_bridge_enter = base_process_bridge_enter,
};

/*! \brief A wrapper object around a snapshot.
 * Fields that are mutable by the CDR engine are replicated here.
 */
struct cdr_object_snapshot {
	struct ast_channel_snapshot *snapshot;  /*!< The channel snapshot */
	char userfield[AST_MAX_USER_FIELD];     /*!< Userfield for the channel */
	unsigned int flags;                     /*!< Specific flags for this party */
	struct varshead variables;              /*!< CDR variables for the channel */
};

/*! \brief An in-memory representation of an active CDR */
struct cdr_object {
	struct cdr_object_snapshot party_a;     /*!< The Party A information */
	struct cdr_object_snapshot party_b;     /*!< The Party B information */
	struct cdr_object_fn_table *fn_table;   /*!< The current virtual table */

	enum ast_cdr_disposition disposition;   /*!< The disposition of the CDR */
	struct timeval start;                   /*!< When this CDR was created */
	struct timeval answer;                  /*!< Either when the channel was answered, or when the path between channels was established */
	struct timeval end;                     /*!< When this CDR was finalized */
	unsigned int sequence;                  /*!< A monotonically increasing number for each CDR */
	struct ast_flags flags;                 /*!< Flags on the CDR */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(linkedid);         /*!< Linked ID. Cached here as it may change out from party A, which must be immutable */
		AST_STRING_FIELD(uniqueid);			/*!< Unique id of party A. Cached here as it is the primary key of this CDR */
		AST_STRING_FIELD(name);             /*!< Channel name of party A. Cached here as the party A address may change */
		AST_STRING_FIELD(bridge);           /*!< The bridge the party A happens to be in. */
		AST_STRING_FIELD(appl);             /*!< The last accepted application party A was in */
		AST_STRING_FIELD(data);             /*!< The data for the last accepted application party A was in */
		AST_STRING_FIELD(context);          /*!< The accepted context for Party A */
		AST_STRING_FIELD(exten);            /*!< The accepted extension for Party A */
	);
	struct cdr_object *next;                /*!< The next CDR object in the chain */
	struct cdr_object *last;                /*!< The last CDR object in the chain */
};

/*!
 * \brief Copy variables from one list to another
 * \param to_list destination
 * \param from_list source
 * \retval The number of copied variables
 */
static int copy_variables(struct varshead *to_list, struct varshead *from_list)
{
	struct ast_var_t *variables;
	struct ast_var_t *newvariable;
	const char *var;
	const char *val;
	int x = 0;

	AST_LIST_TRAVERSE(from_list, variables, entries) {
		var = ast_var_name(variables);
		if (ast_strlen_zero(var)) {
			continue;
		}
		val = ast_var_value(variables);
		if (ast_strlen_zero(val)) {
			continue;
		}
		newvariable = ast_var_assign(var, val);
		if (newvariable) {
			AST_LIST_INSERT_HEAD(to_list, newvariable, entries);
			++x;
		}
	}

	return x;
}

/*!
 * \brief Delete all variables from a variable list
 * \param headp The head pointer to the variable list to delete
 */
static void free_variables(struct varshead *headp)
{
	struct ast_var_t *vardata;

	while ((vardata = AST_LIST_REMOVE_HEAD(headp, entries))) {
		ast_var_delete(vardata);
	}
}

/*!
 * \brief Copy a snapshot and its details
 * \param dst The destination
 * \param src The source
 */
static void cdr_object_snapshot_copy(struct cdr_object_snapshot *dst, struct cdr_object_snapshot *src)
{
	if (dst->snapshot) {
		ao2_t_ref(dst->snapshot, -1, "release old snapshot during copy");
	}
	dst->snapshot = src->snapshot;
	ao2_t_ref(dst->snapshot, +1, "bump new snapshot during copy");
	strcpy(dst->userfield, src->userfield);
	dst->flags = src->flags;
	copy_variables(&dst->variables, &src->variables);
}

/*!
 * \brief Transition a \ref cdr_object to a new state
 * \param cdr The \ref cdr_object to transition
 * \param fn_table The \ref cdr_object_fn_table state to go to
 */
static void cdr_object_transition_state(struct cdr_object *cdr, struct cdr_object_fn_table *fn_table)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	CDR_DEBUG(mod_cfg, "%p - Transitioning CDR for %s from state %s to %s\n",
		cdr, cdr->party_a.snapshot->name,
		cdr->fn_table ? cdr->fn_table->name : "NONE", fn_table->name);
	cdr->fn_table = fn_table;
	if (cdr->fn_table->init_function) {
		cdr->fn_table->init_function(cdr);
	}
}
/*! \internal
 * \brief Hash function for containers of CDRs indexing by Party A uniqueid */
static int cdr_object_channel_hash_fn(const void *obj, const int flags)
{
	const struct cdr_object *cdr;
	const char *key;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key = obj;
		break;
	case OBJ_POINTER:
		cdr = obj;
		key = cdr->uniqueid;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

/*! \internal
 * \brief Comparison function for containers of CDRs indexing by Party A uniqueid
 */
static int cdr_object_channel_cmp_fn(void *obj, void *arg, int flags)
{
    struct cdr_object *left = obj;
    struct cdr_object *right = arg;
    const char *right_key = arg;
    int cmp;

    switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
    case OBJ_POINTER:
        right_key = right->uniqueid;
        /* Fall through */
    case OBJ_KEY:
        cmp = strcmp(left->uniqueid, right_key);
        break;
    case OBJ_PARTIAL_KEY:
        /*
         * We could also use a partial key struct containing a length
         * so strlen() does not get called for every comparison instead.
         */
        cmp = strncmp(left->uniqueid, right_key, strlen(right_key));
        break;
    default:
        /* Sort can only work on something with a full or partial key. */
        ast_assert(0);
        cmp = 0;
        break;
    }
    return cmp ? 0 : CMP_MATCH;
}

/*!
 * \brief \ref cdr_object Destructor
 */
static void cdr_object_dtor(void *obj)
{
	struct cdr_object *cdr = obj;
	struct ast_var_t *it_var;

	ao2_cleanup(cdr->party_a.snapshot);
	ao2_cleanup(cdr->party_b.snapshot);
	while ((it_var = AST_LIST_REMOVE_HEAD(&cdr->party_a.variables, entries))) {
		ast_var_delete(it_var);
	}
	while ((it_var = AST_LIST_REMOVE_HEAD(&cdr->party_b.variables, entries))) {
		ast_var_delete(it_var);
	}
	ast_string_field_free_memory(cdr);

	ao2_cleanup(cdr->next);
}

/*!
 * \brief \ref cdr_object constructor
 * \param chan The \ref ast_channel_snapshot that is the CDR's Party A
 *
 * This implicitly sets the state of the newly created CDR to the Single state
 * (\ref single_state_fn_table)
 */
static struct cdr_object *cdr_object_alloc(struct ast_channel_snapshot *chan)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct cdr_object *cdr;

	ast_assert(chan != NULL);

	cdr = ao2_alloc(sizeof(*cdr), cdr_object_dtor);
	if (!cdr) {
		return NULL;
	}
	cdr->last = cdr;
	if (ast_string_field_init(cdr, 64)) {
		ao2_cleanup(cdr);
		return NULL;
	}
	ast_string_field_set(cdr, uniqueid, chan->uniqueid);
	ast_string_field_set(cdr, name, chan->name);
	ast_string_field_set(cdr, linkedid, chan->linkedid);
	cdr->disposition = AST_CDR_NULL;
	cdr->sequence = ast_atomic_fetchadd_int(&global_cdr_sequence, +1);

	cdr->party_a.snapshot = chan;
	ao2_t_ref(cdr->party_a.snapshot, +1, "bump snapshot during CDR creation");

	CDR_DEBUG(mod_cfg, "%p - Created CDR for channel %s\n", cdr, chan->name);

	cdr_object_transition_state(cdr, &single_state_fn_table);

	return cdr;
}

/*!
 * \brief Create a new \ref cdr_object and append it to an existing chain
 * \param cdr The \ref cdr_object to append to
 */
static struct cdr_object *cdr_object_create_and_append(struct cdr_object *cdr)
{
	struct cdr_object *new_cdr;
	struct cdr_object *it_cdr;
	struct cdr_object *cdr_last;

	cdr_last = cdr->last;
	new_cdr = cdr_object_alloc(cdr_last->party_a.snapshot);
	if (!new_cdr) {
		return NULL;
	}
	new_cdr->disposition = AST_CDR_NULL;

	/* Copy over the linkedid, as it may have changed */
	ast_string_field_set(new_cdr, linkedid, cdr_last->linkedid);
	ast_string_field_set(new_cdr, appl, cdr_last->appl);
	ast_string_field_set(new_cdr, data, cdr_last->data);
	ast_string_field_set(new_cdr, context, cdr_last->context);
	ast_string_field_set(new_cdr, exten, cdr_last->exten);

	/*
	 * If the current CDR says to disable all future ones,
	 * keep the disable chain going
	 */
	if (ast_test_flag(&cdr_last->flags, AST_CDR_FLAG_DISABLE_ALL)) {
		ast_set_flag(&new_cdr->flags, AST_CDR_FLAG_DISABLE_ALL);
	}

	/* Copy over other Party A information */
	cdr_object_snapshot_copy(&new_cdr->party_a, &cdr_last->party_a);

	/* Append the CDR to the end of the list */
	for (it_cdr = cdr; it_cdr->next; it_cdr = it_cdr->next) {
		it_cdr->last = new_cdr;
	}
	it_cdr->last = new_cdr;
	it_cdr->next = new_cdr;

	return new_cdr;
}

/*!
 * \brief Return whether or not a channel has changed its state in the dialplan, subject
 * to endbeforehexten logic
 *
 * \param old_snapshot The previous state
 * \param new_snapshot The new state
 *
 * \retval 0 if the state has not changed
 * \retval 1 if the state changed
 */
static int snapshot_cep_changed(struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	RAII_VAR(struct module_config *, mod_cfg,
		ao2_global_obj_ref(module_configs), ao2_cleanup);

	/* If we ignore hangup logic, don't indicate that we're executing anything new */
	if (ast_test_flag(&mod_cfg->general->settings, CDR_END_BEFORE_H_EXTEN)
		&& ast_test_flag(&new_snapshot->softhangup_flags, AST_SOFTHANGUP_HANGUP_EXEC)) {
		return 0;
	}

	/* When Party A is originated to an application and the application exits, the stack
	 * will attempt to clear the application and restore the dummy originate application
	 * of "AppDialX". Ignore application changes to AppDialX as a result.
	 */
	if (strcmp(new_snapshot->appl, old_snapshot->appl) && strncasecmp(new_snapshot->appl, "appdial", 7)
		&& (strcmp(new_snapshot->context, old_snapshot->context)
		|| strcmp(new_snapshot->exten, old_snapshot->exten)
		|| new_snapshot->priority != old_snapshot->priority)) {
		return 1;
	}

	return 0;
}

/*!
 * \brief Return whether or not a \ref ast_channel_snapshot is for a channel
 * that was created as the result of a dial operation
 *
 * \retval 0 the channel was not created as the result of a dial
 * \retval 1 the channel was created as the result of a dial
 */
static int snapshot_is_dialed(struct ast_channel_snapshot *snapshot)
{
	return (ast_test_flag(&snapshot->flags, AST_FLAG_OUTGOING)
			&& !(ast_test_flag(&snapshot->flags, AST_FLAG_ORIGINATED)));
}

/*!
 * \brief Given two CDR snapshots, figure out who should be Party A for the
 * resulting CDR
 * \param left One of the snapshots
 * \param right The other snapshot
 * \retval The snapshot that won
 */
static struct cdr_object_snapshot *cdr_object_pick_party_a(struct cdr_object_snapshot *left, struct cdr_object_snapshot *right)
{
	/* Check whether or not the party is dialed. A dialed party is never the
	 * Party A with a party that was not dialed.
	 */
	if (!snapshot_is_dialed(left->snapshot) && snapshot_is_dialed(right->snapshot)) {
		return left;
	} else if (snapshot_is_dialed(left->snapshot) && !snapshot_is_dialed(right->snapshot)) {
		return right;
	}

	/* Try the Party A flag */
	if (ast_test_flag(left, AST_CDR_FLAG_PARTY_A) && !ast_test_flag(right, AST_CDR_FLAG_PARTY_A)) {
		return left;
	} else if (!ast_test_flag(right, AST_CDR_FLAG_PARTY_A) && ast_test_flag(right, AST_CDR_FLAG_PARTY_A)) {
		return right;
	}

	/* Neither party is dialed and neither has the Party A flag - defer to
	 * creation time */
	if (left->snapshot->creationtime.tv_sec < right->snapshot->creationtime.tv_sec) {
		return left;
	} else if (left->snapshot->creationtime.tv_sec > right->snapshot->creationtime.tv_sec) {
		return right;
	} else if (left->snapshot->creationtime.tv_usec > right->snapshot->creationtime.tv_usec) {
		return right;
	} else {
		/* Okay, fine, take the left one */
		return left;
	}
}

/*!
 * Compute the duration for a \ref cdr_object
 */
static long cdr_object_get_duration(struct cdr_object *cdr)
{
	return (long)(ast_tvdiff_ms(ast_tvzero(cdr->end) ? ast_tvnow() : cdr->end, cdr->start) / 1000);
}

/*!
 * \brief Compute the billsec for a \ref cdr_object
 */
static long cdr_object_get_billsec(struct cdr_object *cdr)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	long int ms;

	if (ast_tvzero(cdr->answer)) {
		return 0;
	}
	ms = ast_tvdiff_ms(ast_tvzero(cdr->end) ? ast_tvnow() : cdr->end, cdr->answer);
	if (ast_test_flag(&mod_cfg->general->settings, CDR_INITIATED_SECONDS)
		&& (ms % 1000 >= 500)) {
		ms = (ms / 1000) + 1;
	} else {
		ms = ms / 1000;
	}

	return ms;
}

/*!
 * \internal
 * \brief Set a variable on a CDR object
 *
 * \param headp The header pointer to the variable to set
 * \param name The name of the variable
 * \param value The value of the variable
 */
static void set_variable(struct varshead *headp, const char *name, const char *value)
{
	struct ast_var_t *newvariable;

	AST_LIST_TRAVERSE_SAFE_BEGIN(headp, newvariable, entries) {
		if (!strcasecmp(ast_var_name(newvariable), name)) {
			AST_LIST_REMOVE_CURRENT(entries);
			ast_var_delete(newvariable);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (value && (newvariable = ast_var_assign(name, value))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}
}

/*!
 * \brief Create a chain of \ref ast_cdr objects from a chain of \ref cdr_object
 * suitable for consumption by the registered CDR backends
 * \param cdr The \ref cdr_object to convert to a public record
 * \retval A chain of \ref ast_cdr objects on success
 * \retval NULL on failure
 */
static struct ast_cdr *cdr_object_create_public_records(struct cdr_object *cdr)
{
	struct ast_cdr *pub_cdr = NULL, *cdr_prev = NULL;
	struct cdr_object *it_cdr;
	struct ast_var_t *it_var, *it_copy_var;
	struct ast_channel_snapshot *party_a;
	struct ast_channel_snapshot *party_b;

	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		struct ast_cdr *cdr_copy;

		/* Don't create records for CDRs where the party A was a dialed channel */
		if (snapshot_is_dialed(it_cdr->party_a.snapshot) && !it_cdr->party_b.snapshot) {
			ast_debug(1, "CDR for %s is dialed and has no Party B; discarding\n",
				it_cdr->party_a.snapshot->name);
			continue;
		}

		cdr_copy = ast_calloc(1, sizeof(*cdr_copy));
		if (!cdr_copy) {
			ast_free(pub_cdr);
			return NULL;
		}

		party_a = it_cdr->party_a.snapshot;
		party_b = it_cdr->party_b.snapshot;

		/* Party A */
		ast_assert(party_a != NULL);
		ast_copy_string(cdr_copy->accountcode, party_a->accountcode, sizeof(cdr_copy->accountcode));
		cdr_copy->amaflags = party_a->amaflags;
		ast_copy_string(cdr_copy->channel, party_a->name, sizeof(cdr_copy->channel));
		ast_callerid_merge(cdr_copy->clid, sizeof(cdr_copy->clid), party_a->caller_name, party_a->caller_number, "");
		ast_copy_string(cdr_copy->src, party_a->caller_number, sizeof(cdr_copy->src));
		ast_copy_string(cdr_copy->uniqueid, party_a->uniqueid, sizeof(cdr_copy->uniqueid));
		ast_copy_string(cdr_copy->lastapp, it_cdr->appl, sizeof(cdr_copy->lastapp));
		ast_copy_string(cdr_copy->lastdata, it_cdr->data, sizeof(cdr_copy->lastdata));
		ast_copy_string(cdr_copy->dst, it_cdr->exten, sizeof(cdr_copy->dst));
		ast_copy_string(cdr_copy->dcontext, it_cdr->context, sizeof(cdr_copy->dcontext));

		/* Party B */
		if (party_b) {
			ast_copy_string(cdr_copy->dstchannel, party_b->name, sizeof(cdr_copy->dstchannel));
			ast_copy_string(cdr_copy->peeraccount, party_b->accountcode, sizeof(cdr_copy->peeraccount));
			if (!ast_strlen_zero(it_cdr->party_b.userfield)) {
				snprintf(cdr_copy->userfield, sizeof(cdr_copy->userfield), "%s;%s", it_cdr->party_a.userfield, it_cdr->party_b.userfield);
			}
		}
		if (ast_strlen_zero(cdr_copy->userfield) && !ast_strlen_zero(it_cdr->party_a.userfield)) {
			ast_copy_string(cdr_copy->userfield, it_cdr->party_a.userfield, sizeof(cdr_copy->userfield));
		}

		/* Timestamps/durations */
		cdr_copy->start = it_cdr->start;
		cdr_copy->answer = it_cdr->answer;
		cdr_copy->end = it_cdr->end;
		cdr_copy->billsec = cdr_object_get_billsec(it_cdr);
		cdr_copy->duration = cdr_object_get_duration(it_cdr);

		/* Flags and IDs */
		ast_copy_flags(cdr_copy, &it_cdr->flags, AST_FLAGS_ALL);
		ast_copy_string(cdr_copy->linkedid, it_cdr->linkedid, sizeof(cdr_copy->linkedid));
		cdr_copy->disposition = it_cdr->disposition;
		cdr_copy->sequence = it_cdr->sequence;

		/* Variables */
		copy_variables(&cdr_copy->varshead, &it_cdr->party_a.variables);
		AST_LIST_TRAVERSE(&it_cdr->party_b.variables, it_var, entries) {
			int found = 0;
			struct ast_var_t *newvariable;
			AST_LIST_TRAVERSE(&cdr_copy->varshead, it_copy_var, entries) {
				if (!strcasecmp(ast_var_name(it_var), ast_var_name(it_copy_var))) {
					found = 1;
					break;
				}
			}
			if (!found && (newvariable = ast_var_assign(ast_var_name(it_var), ast_var_value(it_var)))) {
				AST_LIST_INSERT_TAIL(&cdr_copy->varshead, newvariable, entries);
			}
		}

		if (!pub_cdr) {
			pub_cdr = cdr_copy;
			cdr_prev = pub_cdr;
		} else {
			cdr_prev->next = cdr_copy;
			cdr_prev = cdr_copy;
		}
	}

	return pub_cdr;
}

/*!
 * \brief Dispatch a CDR.
 * \param cdr The \ref cdr_object to dispatch
 *
 * This will create a \ref ast_cdr object and publish it to the various backends
 */
static void cdr_object_dispatch(struct cdr_object *cdr)
{
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct ast_cdr *pub_cdr;

	CDR_DEBUG(mod_cfg, "%p - Dispatching CDR for Party A %s, Party B %s\n", cdr,
			cdr->party_a.snapshot->name,
			cdr->party_b.snapshot ? cdr->party_b.snapshot->name : "<none>");
	pub_cdr = cdr_object_create_public_records(cdr);
	cdr_detach(pub_cdr);
}

/*!
 * \brief Set the disposition on a \ref cdr_object based on a hangupcause code
 * \param cdr The \ref cdr_object
 * \param hangupcause The Asterisk hangup cause code
 */
static void cdr_object_set_disposition(struct cdr_object *cdr, int hangupcause)
{
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);

	/* Change the disposition based on the hang up cause */
	switch (hangupcause) {
	case AST_CAUSE_BUSY:
		cdr->disposition = AST_CDR_BUSY;
		break;
	case AST_CAUSE_CONGESTION:
		if (!ast_test_flag(&mod_cfg->general->settings, CDR_CONGESTION)) {
			cdr->disposition = AST_CDR_FAILED;
		} else {
			cdr->disposition = AST_CDR_CONGESTION;
		}
		break;
	case AST_CAUSE_NO_ROUTE_DESTINATION:
	case AST_CAUSE_UNREGISTERED:
		cdr->disposition = AST_CDR_FAILED;
		break;
	case AST_CAUSE_NORMAL_CLEARING:
	case AST_CAUSE_NO_ANSWER:
		cdr->disposition = AST_CDR_NOANSWER;
		break;
	default:
		break;
	}
}

/*!
 * \brief Finalize a CDR.
 *
 * This function is safe to call multiple times. Note that you can call this
 * explicitly before going to the finalized state if there's a chance the CDR
 * will be re-activated, in which case the \ref cdr_object's end time should be
 * cleared. This function is implicitly called when a CDR transitions to the
 * finalized state and right before it is dispatched
 *
 * \param cdr_object The CDR to finalize
 */
static void cdr_object_finalize(struct cdr_object *cdr)
{
	if (!ast_tvzero(cdr->end)) {
		return;
	}
	cdr->end = ast_tvnow();

	if (cdr->disposition == AST_CDR_NULL) {
		if (!ast_tvzero(cdr->answer)) {
			cdr->disposition = AST_CDR_ANSWERED;
		} else if (cdr->party_a.snapshot->hangupcause) {
			cdr_object_set_disposition(cdr, cdr->party_a.snapshot->hangupcause);
		} else if (cdr->party_b.snapshot && cdr->party_b.snapshot->hangupcause) {
			cdr_object_set_disposition(cdr, cdr->party_b.snapshot->hangupcause);
		} else {
			cdr->disposition = AST_CDR_FAILED;
		}
	}

	/* tv_usec is suseconds_t, which could be int or long */
	ast_debug(1, "Finalized CDR for %s - start %ld.%06ld answer %ld.%06ld end %ld.%06ld dispo %s\n",
			cdr->party_a.snapshot->name,
			(long)cdr->start.tv_sec,
			(long)cdr->start.tv_usec,
			(long)cdr->answer.tv_sec,
			(long)cdr->answer.tv_usec,
			(long)cdr->end.tv_sec,
			(long)cdr->end.tv_usec,
			ast_cdr_disp2str(cdr->disposition));
}

/*!
 * \brief Check to see if a CDR needs to move to the finalized state because
 * its Party A hungup.
 */
static void cdr_object_check_party_a_hangup(struct cdr_object *cdr)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	if (ast_test_flag(&mod_cfg->general->settings, CDR_END_BEFORE_H_EXTEN)
		&& ast_test_flag(&cdr->party_a.snapshot->softhangup_flags, AST_SOFTHANGUP_HANGUP_EXEC)) {
		cdr_object_finalize(cdr);
	}

	if (ast_test_flag(&cdr->party_a.snapshot->flags, AST_FLAG_DEAD)
		&& cdr->fn_table != &finalized_state_fn_table) {
		cdr_object_transition_state(cdr, &finalized_state_fn_table);
	}
}

/*!
 * \brief Check to see if a CDR needs to be answered based on its Party A.
 * Note that this is safe to call as much as you want - we won't answer twice
 */
static void cdr_object_check_party_a_answer(struct cdr_object *cdr) {
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	if (cdr->party_a.snapshot->state == AST_STATE_UP && ast_tvzero(cdr->answer)) {
		cdr->answer = ast_tvnow();
		/* tv_usec is suseconds_t, which could be int or long */
		CDR_DEBUG(mod_cfg, "%p - Set answered time to %ld.%06ld\n", cdr,
			(long)cdr->answer.tv_sec,
			(long)cdr->answer.tv_usec);
	}
}

/* \brief Set Caller ID information on a CDR */
static void cdr_object_update_cid(struct cdr_object_snapshot *old_snapshot, struct ast_channel_snapshot *new_snapshot)
{
	if (!old_snapshot->snapshot) {
		set_variable(&old_snapshot->variables, "dnid", new_snapshot->caller_dnid);
		set_variable(&old_snapshot->variables, "callingsubaddr", new_snapshot->caller_subaddr);
		set_variable(&old_snapshot->variables, "calledsubaddr", new_snapshot->dialed_subaddr);
		return;
	}
	if (!strcmp(old_snapshot->snapshot->caller_dnid, new_snapshot->caller_dnid)) {
		set_variable(&old_snapshot->variables, "dnid", new_snapshot->caller_dnid);
	}
	if (!strcmp(old_snapshot->snapshot->caller_subaddr, new_snapshot->caller_subaddr)) {
		set_variable(&old_snapshot->variables, "callingsubaddr", new_snapshot->caller_subaddr);
	}
	if (!strcmp(old_snapshot->snapshot->dialed_subaddr, new_snapshot->dialed_subaddr)) {
		set_variable(&old_snapshot->variables, "calledsubaddr", new_snapshot->dialed_subaddr);
	}
}

/*!
 * \brief Swap an old \ref cdr_object_snapshot's \ref ast_channel_snapshot for
 * a new \ref ast_channel_snapshot
 * \param old_snapshot The old \ref cdr_object_snapshot
 * \param new_snapshot The new \ref ast_channel_snapshot for old_snapshot
 */
static void cdr_object_swap_snapshot(struct cdr_object_snapshot *old_snapshot,
		struct ast_channel_snapshot *new_snapshot)
{
	cdr_object_update_cid(old_snapshot, new_snapshot);
	if (old_snapshot->snapshot) {
		ao2_t_ref(old_snapshot->snapshot, -1, "Drop ref for swap");
	}
	ao2_t_ref(new_snapshot, +1, "Bump ref for swap");
	old_snapshot->snapshot = new_snapshot;
}

/* BASE METHOD IMPLEMENTATIONS */

static int base_process_party_a(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	ast_assert(strcasecmp(snapshot->name, cdr->party_a.snapshot->name) == 0);

	/* Ignore any snapshots from a dead or dying channel */
	if (ast_test_flag(&snapshot->softhangup_flags, AST_SOFTHANGUP_HANGUP_EXEC)
			&& ast_test_flag(&mod_cfg->general->settings, CDR_END_BEFORE_H_EXTEN)) {
		cdr_object_check_party_a_hangup(cdr);
		return 0;
	}

	/*
	 * Only record the context and extension if we aren't in a subroutine, or if
	 * we are executing hangup logic.
	 */
	if (!ast_test_flag(&snapshot->flags, AST_FLAG_SUBROUTINE_EXEC)
		|| ast_test_flag(&snapshot->softhangup_flags, AST_SOFTHANGUP_HANGUP_EXEC)) {
		ast_string_field_set(cdr, context, snapshot->context);
		ast_string_field_set(cdr, exten, snapshot->exten);
	}

	cdr_object_swap_snapshot(&cdr->party_a, snapshot);

	/* When Party A is originated to an application and the application exits, the stack
	 * will attempt to clear the application and restore the dummy originate application
	 * of "AppDialX". Prevent that, and any other application changes we might not want
	 * here.
	 */
	if (!ast_strlen_zero(snapshot->appl)
			&& (strncasecmp(snapshot->appl, "appdial", 7) || ast_strlen_zero(cdr->appl))
			&& !ast_test_flag(&cdr->flags, AST_CDR_LOCK_APP)) {
		ast_string_field_set(cdr, appl, snapshot->appl);
		ast_string_field_set(cdr, data, snapshot->data);

		/* Dial (app_dial) is a special case. Because pre-dial handlers, which
		 * execute before the dial begins, will alter the application/data to
		 * something people typically don't want to see, if we see a channel enter
		 * into Dial here, we set the appl/data accordingly and lock it.
		 */
		if (!strcmp(snapshot->appl, "Dial")) {
			ast_set_flag(&cdr->flags, AST_CDR_LOCK_APP);
		}
	}

	ast_string_field_set(cdr, linkedid, snapshot->linkedid);
	cdr_object_check_party_a_answer(cdr);
	cdr_object_check_party_a_hangup(cdr);

	return 0;
}

static int base_process_bridge_leave(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	/* In general, most things shouldn't get a bridge leave */
	ast_assert(0);
	return 1;
}

static int base_process_dial_end(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer, const char *dial_status)
{
	/* In general, most things shouldn't get a dial end. */
	ast_assert(0);
	return 0;
}

static enum process_bridge_enter_results base_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	/* Base process bridge enter simply indicates that we can't handle it */
	return BRIDGE_ENTER_NEED_CDR;
}

static int base_process_parked_channel(struct cdr_object *cdr, struct ast_parked_call_payload *parking_info)
{
	char park_info[128];

	ast_assert(!strcasecmp(parking_info->parkee->name, cdr->party_a.snapshot->name));

	/* Update Party A information regardless */
	cdr->fn_table->process_party_a(cdr, parking_info->parkee);

	/* Fake out where we're parked */
	ast_string_field_set(cdr, appl, "Park");
	snprintf(park_info, sizeof(park_info), "%s:%u", parking_info->parkinglot, parking_info->parkingspace);
	ast_string_field_set(cdr, data, park_info);

	/* Prevent any further changes to the App/Data fields for this record */
	ast_set_flag(&cdr->flags, AST_CDR_LOCK_APP);

	return 0;
}

/* SINGLE STATE */

static void single_state_init_function(struct cdr_object *cdr) {
	cdr->start = ast_tvnow();
	cdr_object_check_party_a_answer(cdr);
}

static void single_state_process_party_b(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot)
{
	/* This should never happen! */
	ast_assert(cdr->party_b.snapshot == NULL);
	ast_assert(0);
	return;
}

static int single_state_process_dial_begin(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	if (caller && !strcasecmp(cdr->party_a.snapshot->name, caller->name)) {
		base_process_party_a(cdr, caller);
		CDR_DEBUG(mod_cfg, "%p - Updated Party A %s snapshot\n", cdr,
				cdr->party_a.snapshot->name);
		cdr_object_swap_snapshot(&cdr->party_b, peer);
		CDR_DEBUG(mod_cfg, "%p - Updated Party B %s snapshot\n", cdr,
				cdr->party_b.snapshot->name);

		/* If we have two parties, lock the application that caused the
		 * two parties to be associated. This prevents mid-call event
		 * macros/gosubs from perturbing the CDR application/data
		 */
		ast_set_flag(&cdr->flags, AST_CDR_LOCK_APP);
	} else if (!strcasecmp(cdr->party_a.snapshot->name, peer->name)) {
		/* We're the entity being dialed, i.e., outbound origination */
		base_process_party_a(cdr, peer);
		CDR_DEBUG(mod_cfg, "%p - Updated Party A %s snapshot\n", cdr,
				cdr->party_a.snapshot->name);
	}

	cdr_object_transition_state(cdr, &dial_state_fn_table);
	return 0;
}

/*!
 * \brief Handle a comparison between our \ref cdr_object and a \ref cdr_object
 * already in the bridge while in the Single state. The goal of this is to find
 * a Party B for our CDR.
 *
 * \param cdr Our \ref cdr_object in the Single state
 * \param cand_cdr The \ref cdr_object already in the Bridge state
 *
 * \retval 0 The cand_cdr had a Party A or Party B that we could use as our
 * Party B
 * \retval 1 No party in the cand_cdr could be used as our Party B
 */
static int single_state_bridge_enter_comparison(struct cdr_object *cdr,
		struct cdr_object *cand_cdr)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct cdr_object_snapshot *party_a;

	/* Don't match on ourselves */
	if (!strcasecmp(cdr->party_a.snapshot->name, cand_cdr->party_a.snapshot->name)) {
		return 1;
	}

	/* Try the candidate CDR's Party A first */
	party_a = cdr_object_pick_party_a(&cdr->party_a, &cand_cdr->party_a);
	if (!strcasecmp(party_a->snapshot->name, cdr->party_a.snapshot->name)) {
		CDR_DEBUG(mod_cfg, "%p - Party A %s has new Party B %s\n",
			cdr, cdr->party_a.snapshot->name, cand_cdr->party_a.snapshot->name);
		cdr_object_snapshot_copy(&cdr->party_b, &cand_cdr->party_a);
		if (!cand_cdr->party_b.snapshot) {
			/* We just stole them - finalize their CDR. Note that this won't
			 * transition their state, it just sets the end time and the
			 * disposition - if we need to re-activate them later, we can.
			 */
			cdr_object_finalize(cand_cdr);
		}
		return 0;
	}

	/* Try their Party B, unless it's us */
	if (!cand_cdr->party_b.snapshot
		|| !strcasecmp(cdr->party_a.snapshot->name, cand_cdr->party_b.snapshot->name)) {
		return 1;
	}
	party_a = cdr_object_pick_party_a(&cdr->party_a, &cand_cdr->party_b);
	if (!strcasecmp(party_a->snapshot->name, cdr->party_a.snapshot->name)) {
		CDR_DEBUG(mod_cfg, "%p - Party A %s has new Party B %s\n",
			cdr, cdr->party_a.snapshot->name, cand_cdr->party_b.snapshot->name);
		cdr_object_snapshot_copy(&cdr->party_b, &cand_cdr->party_b);
		return 0;
	}

	return 1;
}

static enum process_bridge_enter_results single_state_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	struct ao2_iterator it_cdrs;
	char *channel_id;
	int success = 0;

	ast_string_field_set(cdr, bridge, bridge->uniqueid);

	if (ao2_container_count(bridge->channels) == 1) {
		/* No one in the bridge yet but us! */
		cdr_object_transition_state(cdr, &bridge_state_fn_table);
		return BRIDGE_ENTER_ONLY_PARTY;
	}

	for (it_cdrs = ao2_iterator_init(bridge->channels, 0);
		!success && (channel_id = ao2_iterator_next(&it_cdrs));
		ao2_ref(channel_id, -1)) {
		RAII_VAR(struct cdr_object *, cand_cdr_master,
			ao2_find(active_cdrs_by_channel, channel_id, OBJ_KEY),
			ao2_cleanup);
		struct cdr_object *cand_cdr;

		if (!cand_cdr_master) {
			continue;
		}

		ao2_lock(cand_cdr_master);
		for (cand_cdr = cand_cdr_master; cand_cdr; cand_cdr = cand_cdr->next) {
			/* Skip any records that are not in a bridge or in this bridge.
			 * I'm not sure how that would happen, but it pays to be careful. */
			if (cand_cdr->fn_table != &bridge_state_fn_table ||
					strcmp(cdr->bridge, cand_cdr->bridge)) {
				continue;
			}

			if (single_state_bridge_enter_comparison(cdr, cand_cdr)) {
				continue;
			}
			/* We successfully got a party B - break out */
			success = 1;
			break;
		}
		ao2_unlock(cand_cdr_master);
	}
	ao2_iterator_destroy(&it_cdrs);

	/* We always transition state, even if we didn't get a peer */
	cdr_object_transition_state(cdr, &bridge_state_fn_table);

	/* Success implies that we have a Party B */
	if (success) {
		return BRIDGE_ENTER_OBTAINED_PARTY_B;
	}

	return BRIDGE_ENTER_NO_PARTY_B;
}

static int single_state_process_parking_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	cdr_object_transition_state(cdr, &parked_state_fn_table);
	return 0;
}


/* DIAL STATE */

static void dial_state_process_party_b(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot)
{
	ast_assert(snapshot != NULL);

	if (!cdr->party_b.snapshot
		|| strcasecmp(cdr->party_b.snapshot->name, snapshot->name)) {
		return;
	}
	cdr_object_swap_snapshot(&cdr->party_b, snapshot);

	/* If party B hangs up, finalize this CDR */
	if (ast_test_flag(&cdr->party_b.snapshot->flags, AST_FLAG_DEAD)) {
		cdr_object_transition_state(cdr, &finalized_state_fn_table);
	}
}

static int dial_state_process_dial_begin(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer)
{
	/* Don't process a begin dial here. A party A already in the dial state will
	 * who receives a dial begin for something else will be handled by the
	 * message router callback and will add a new CDR for the party A */
	return 1;
}

/*!
 * \internal
 * \brief Convert a dial status to a CDR disposition
 */
static enum ast_cdr_disposition dial_status_to_disposition(const char *dial_status)
{
	RAII_VAR(struct module_config *, mod_cfg,
		ao2_global_obj_ref(module_configs), ao2_cleanup);

	if (!strcmp(dial_status, "ANSWER")) {
		return AST_CDR_ANSWERED;
	} else if (!strcmp(dial_status, "BUSY")) {
		return AST_CDR_BUSY;
	} else if (!strcmp(dial_status, "CANCEL") || !strcmp(dial_status, "NOANSWER")) {
		return AST_CDR_NOANSWER;
	} else if (!strcmp(dial_status, "CONGESTION")) {
		if (!ast_test_flag(&mod_cfg->general->settings, CDR_CONGESTION)) {
			return AST_CDR_FAILED;
		} else {
			return AST_CDR_CONGESTION;
		}
	} else if (!strcmp(dial_status, "FAILED")) {
		return AST_CDR_FAILED;
	}
	return AST_CDR_FAILED;
}

static int dial_state_process_dial_end(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer, const char *dial_status)
{
	struct ast_channel_snapshot *party_a;

	if (caller) {
		party_a = caller;
	} else {
		party_a = peer;
	}
	ast_assert(!strcasecmp(cdr->party_a.snapshot->name, party_a->name));
	cdr_object_swap_snapshot(&cdr->party_a, party_a);

	if (cdr->party_b.snapshot) {
		if (strcasecmp(cdr->party_b.snapshot->name, peer->name)) {
			/* Not the status for this CDR - defer back to the message router */
			return 1;
		}
		cdr_object_swap_snapshot(&cdr->party_b, peer);
	}

	/* Set the disposition based on the dial string. */
	cdr->disposition = dial_status_to_disposition(dial_status);
	if (cdr->disposition == AST_CDR_ANSWERED) {
		/* Switch to dial pending to wait and see what the caller does */
		cdr_object_transition_state(cdr, &dialed_pending_state_fn_table);
	} else {
		cdr_object_transition_state(cdr, &finalized_state_fn_table);
	}

	return 0;
}

static enum process_bridge_enter_results dial_state_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	struct ao2_iterator it_cdrs;
	char *channel_id;
	int success = 0;

	ast_string_field_set(cdr, bridge, bridge->uniqueid);

	/* Get parties in the bridge */
	if (ao2_container_count(bridge->channels) == 1) {
		/* No one in the bridge yet but us! */
		cdr_object_transition_state(cdr, &bridge_state_fn_table);
		return BRIDGE_ENTER_ONLY_PARTY;
	}

	for (it_cdrs = ao2_iterator_init(bridge->channels, 0);
		!success && (channel_id = ao2_iterator_next(&it_cdrs));
		ao2_ref(channel_id, -1)) {
		RAII_VAR(struct cdr_object *, cand_cdr_master,
			ao2_find(active_cdrs_by_channel, channel_id, OBJ_KEY),
			ao2_cleanup);
		struct cdr_object *cand_cdr;

		if (!cand_cdr_master) {
			continue;
		}

		ao2_lock(cand_cdr_master);
		for (cand_cdr = cand_cdr_master; cand_cdr; cand_cdr = cand_cdr->next) {
			/* Skip any records that are not in a bridge or in this bridge.
			 * I'm not sure how that would happen, but it pays to be careful. */
			if (cand_cdr->fn_table != &bridge_state_fn_table ||
					strcmp(cdr->bridge, cand_cdr->bridge)) {
				continue;
			}

			/* If we don't have a Party B (originated channel), skip it */
			if (!cdr->party_b.snapshot) {
				continue;
			}

			/* Skip any records that aren't our Party B */
			if (strcasecmp(cdr->party_b.snapshot->name, cand_cdr->party_a.snapshot->name)) {
				continue;
			}
			cdr_object_snapshot_copy(&cdr->party_b, &cand_cdr->party_a);
			/* If they have a Party B, they joined up with someone else as their
			 * Party A. Don't finalize them as they're active. Otherwise, we
			 * have stolen them so they need to be finalized.
			 */
			if (!cand_cdr->party_b.snapshot) {
				cdr_object_finalize(cand_cdr);
			}
			success = 1;
			break;
		}
		ao2_unlock(cand_cdr_master);
	}
	ao2_iterator_destroy(&it_cdrs);

	/* We always transition state, even if we didn't get a peer */
	cdr_object_transition_state(cdr, &bridge_state_fn_table);

	/* Success implies that we have a Party B */
	if (success) {
		return BRIDGE_ENTER_OBTAINED_PARTY_B;
	}
	return BRIDGE_ENTER_NO_PARTY_B;
}

/* DIALED PENDING STATE */

static int dialed_pending_state_process_party_a(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot)
{
	/* If we get a CEP change, we're executing dialplan. If we have a Party B
	 * that means we need a new CDR; otherwise, switch us over to single.
	 */
	if (snapshot_cep_changed(cdr->party_a.snapshot, snapshot)) {
		if (cdr->party_b.snapshot) {
			cdr_object_transition_state(cdr, &finalized_state_fn_table);
			cdr->fn_table->process_party_a(cdr, snapshot);
			return 1;
		} else {
			cdr_object_transition_state(cdr, &single_state_fn_table);
			cdr->fn_table->process_party_a(cdr, snapshot);
			return 0;
		}
	}
	base_process_party_a(cdr, snapshot);
	return 0;
}

static enum process_bridge_enter_results dialed_pending_state_process_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	cdr_object_transition_state(cdr, &dial_state_fn_table);
	return cdr->fn_table->process_bridge_enter(cdr, bridge, channel);
}

static int dialed_pending_state_process_parking_bridge_enter(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	if (cdr->party_b.snapshot) {
		/* We can't handle this as we have a Party B - ask for a new one */
		return 1;
	}
	cdr_object_transition_state(cdr, &parked_state_fn_table);
	return 0;
}

static int dialed_pending_state_process_dial_begin(struct cdr_object *cdr, struct ast_channel_snapshot *caller, struct ast_channel_snapshot *peer)
{
	cdr_object_transition_state(cdr, &finalized_state_fn_table);

	/* Ask for a new CDR */
	return 1;
}

/* BRIDGE STATE */

static void bridge_state_process_party_b(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot)
{
	if (!cdr->party_b.snapshot
		|| strcasecmp(cdr->party_b.snapshot->name, snapshot->name)) {
		return;
	}
	cdr_object_swap_snapshot(&cdr->party_b, snapshot);

	/* If party B hangs up, finalize this CDR */
	if (ast_test_flag(&cdr->party_b.snapshot->flags, AST_FLAG_DEAD)) {
		cdr_object_transition_state(cdr, &finalized_state_fn_table);
	}
}

static int bridge_state_process_bridge_leave(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	if (strcmp(cdr->bridge, bridge->uniqueid)) {
		return 1;
	}
	if (strcasecmp(cdr->party_a.snapshot->name, channel->name)
		&& cdr->party_b.snapshot
		&& strcasecmp(cdr->party_b.snapshot->name, channel->name)) {
		return 1;
	}
	cdr_object_transition_state(cdr, &finalized_state_fn_table);

	return 0;
}

/* PARKED STATE */

static int parked_state_process_bridge_leave(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge, struct ast_channel_snapshot *channel)
{
	if (strcasecmp(cdr->party_a.snapshot->name, channel->name)) {
		return 1;
	}
	cdr_object_transition_state(cdr, &finalized_state_fn_table);

	return 0;
}

/* FINALIZED STATE */

static void finalized_state_init_function(struct cdr_object *cdr)
{
	cdr_object_finalize(cdr);
}

static int finalized_state_process_party_a(struct cdr_object *cdr, struct ast_channel_snapshot *snapshot)
{
	RAII_VAR(struct module_config *, mod_cfg,
		ao2_global_obj_ref(module_configs), ao2_cleanup);

	if (ast_test_flag(&snapshot->softhangup_flags, AST_SOFTHANGUP_HANGUP_EXEC)
			&& ast_test_flag(&mod_cfg->general->settings, CDR_END_BEFORE_H_EXTEN)) {
		return 0;
	}

	/* Indicate that, if possible, we should get a new CDR */
	return 1;
}

/*!
 * \internal
 * \brief Filter channel snapshots by technology
 */
static int filter_channel_snapshot(struct ast_channel_snapshot *snapshot)
{
	return snapshot->tech_properties & AST_CHAN_TP_INTERNAL;
}

/*!
 * \internal
 * \brief Filter a channel cache update
 */
static int filter_channel_cache_message(struct ast_channel_snapshot *old_snapshot,
		struct ast_channel_snapshot *new_snapshot)
{
	int ret = 0;

	/* Drop cache updates from certain channel technologies */
	if (old_snapshot) {
		ret |= filter_channel_snapshot(old_snapshot);
	}
	if (new_snapshot) {
		ret |= filter_channel_snapshot(new_snapshot);
	}

	return ret;
}

/* TOPIC ROUTER CALLBACKS */

/*!
 * \brief Handler for Stasis-Core dial messages
 * \param data Passed on
 * \param sub The stasis subscription for this message callback
 * \param topic The topic this message was published for
 * \param message The message
 */
static void handle_dial_message(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	RAII_VAR(struct cdr_object *, cdr, NULL, ao2_cleanup);
	struct ast_multi_channel_blob *payload = stasis_message_data(message);
	struct ast_channel_snapshot *caller;
	struct ast_channel_snapshot *peer;
	struct cdr_object *it_cdr;
	struct ast_json *dial_status_blob;
	const char *dial_status = NULL;
	int res = 1;

	caller = ast_multi_channel_blob_get_channel(payload, "caller");
	peer = ast_multi_channel_blob_get_channel(payload, "peer");
	if (!peer && !caller) {
		return;
	}
	dial_status_blob = ast_json_object_get(ast_multi_channel_blob_get_json(payload), "dialstatus");
	if (dial_status_blob) {
		dial_status = ast_json_string_get(dial_status_blob);
	}

	CDR_DEBUG(mod_cfg, "Dial %s message for %s, %s: %u.%08u\n",
			ast_strlen_zero(dial_status) ? "Begin" : "End",
			caller ? caller->name : "(none)",
			peer ? peer->name : "(none)",
			(unsigned int)stasis_message_timestamp(message)->tv_sec,
			(unsigned int)stasis_message_timestamp(message)->tv_usec);

	if (filter_channel_snapshot(peer) || (caller && filter_channel_snapshot(caller))) {
		return;
	}

	/* Figure out who is running this show */
	if (caller) {
		cdr = ao2_find(active_cdrs_by_channel, caller->uniqueid, OBJ_KEY);
	} else {
		cdr = ao2_find(active_cdrs_by_channel, peer->uniqueid, OBJ_KEY);
	}

	if (!cdr) {
		ast_log(AST_LOG_WARNING, "No CDR for channel %s\n", caller ? caller->name : peer->name);
		ast_assert(0);
		return;
	}

	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (ast_strlen_zero(dial_status)) {
			if (!it_cdr->fn_table->process_dial_begin) {
				continue;
			}
			CDR_DEBUG(mod_cfg, "%p - Processing Dial Begin message for channel %s, peer %s\n",
					it_cdr,
					caller ? caller->name : "(none)",
					peer ? peer->name : "(none)");
			res &= it_cdr->fn_table->process_dial_begin(it_cdr,
					caller,
					peer);
		} else {
			if (!it_cdr->fn_table->process_dial_end) {
				continue;
			}
			CDR_DEBUG(mod_cfg, "%p - Processing Dial End message for channel %s, peer %s\n",
					it_cdr,
					caller ? caller->name : "(none)",
					peer ? peer->name : "(none)");
			it_cdr->fn_table->process_dial_end(it_cdr,
					caller,
					peer,
					dial_status);
		}
	}

	/* If no CDR handled a dial begin message, make a new one */
	if (res && ast_strlen_zero(dial_status)) {
		struct cdr_object *new_cdr;

		new_cdr = cdr_object_create_and_append(cdr);
		if (!new_cdr) {
			ao2_unlock(cdr);
			return;
		}
		new_cdr->fn_table->process_dial_begin(new_cdr,
				caller,
				peer);
	}
	ao2_unlock(cdr);
}

static int cdr_object_finalize_party_b(void *obj, void *arg, int flags)
{
	struct cdr_object *cdr = obj;
	struct ast_channel_snapshot *party_b = arg;
	struct cdr_object *it_cdr;
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->party_b.snapshot
			&& !strcasecmp(it_cdr->party_b.snapshot->name, party_b->name)) {
			/* Don't transition to the finalized state - let the Party A do
			 * that when its ready
			 */
			cdr_object_finalize(it_cdr);
		}
	}
	return 0;
}

static int cdr_object_update_party_b(void *obj, void *arg, int flags)
{
	struct cdr_object *cdr = obj;
	struct ast_channel_snapshot *party_b = arg;
	struct cdr_object *it_cdr;
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (!it_cdr->fn_table->process_party_b) {
			continue;
		}
		if (it_cdr->party_b.snapshot
			&& !strcasecmp(it_cdr->party_b.snapshot->name, party_b->name)) {
			it_cdr->fn_table->process_party_b(it_cdr, party_b);
		}
	}
	return 0;
}

/*! \brief Determine if we need to add a new CDR based on snapshots */
static int check_new_cdr_needed(struct ast_channel_snapshot *old_snapshot,
		struct ast_channel_snapshot *new_snapshot)
{
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);

	/* If we're dead, we don't need a new CDR */
	if (!new_snapshot
		|| (ast_test_flag(&new_snapshot->softhangup_flags, AST_SOFTHANGUP_HANGUP_EXEC)
			&& ast_test_flag(&mod_cfg->general->settings, CDR_END_BEFORE_H_EXTEN))) {
		return 0;
	}

	/* Auto-fall through will increment the priority but have no application */
	if (ast_strlen_zero(new_snapshot->appl)) {
		return 0;
	}

	if (old_snapshot && !snapshot_cep_changed(old_snapshot, new_snapshot)) {
		return 0;
	}

	return 1;
}

/*!
 * \brief Handler for Stasis-Core channel cache update messages
 * \param data Passed on
 * \param sub The stasis subscription for this message callback
 * \param topic The topic this message was published for
 * \param message The message
 */
static void handle_channel_cache_message(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	RAII_VAR(struct cdr_object *, cdr, NULL, ao2_cleanup);
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct stasis_cache_update *update = stasis_message_data(message);
	struct ast_channel_snapshot *old_snapshot;
	struct ast_channel_snapshot *new_snapshot;
	const char *uniqueid;
	const char *name;
	struct cdr_object *it_cdr;

	ast_assert(update != NULL);
	ast_assert(ast_channel_snapshot_type() == update->type);

	old_snapshot = stasis_message_data(update->old_snapshot);
	new_snapshot = stasis_message_data(update->new_snapshot);
	uniqueid = new_snapshot ? new_snapshot->uniqueid : old_snapshot->uniqueid;
	name = new_snapshot ? new_snapshot->name : old_snapshot->name;

	if (filter_channel_cache_message(old_snapshot, new_snapshot)) {
		return;
	}

	if (new_snapshot && !old_snapshot) {
		cdr = cdr_object_alloc(new_snapshot);
		if (!cdr) {
			return;
		}
		ao2_link(active_cdrs_by_channel, cdr);
	}

	/* Handle Party A */
	if (!cdr) {
		cdr = ao2_find(active_cdrs_by_channel, uniqueid, OBJ_KEY);
	}
	if (!cdr) {
		ast_log(AST_LOG_WARNING, "No CDR for channel %s\n", name);
		ast_assert(0);
	} else {
		ao2_lock(cdr);
		if (new_snapshot) {
			int all_reject = 1;
			for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
				if (!it_cdr->fn_table->process_party_a) {
					continue;
				}
				all_reject &= it_cdr->fn_table->process_party_a(it_cdr, new_snapshot);
			}
			if (all_reject && check_new_cdr_needed(old_snapshot, new_snapshot)) {
				/* We're not hung up and we have a new snapshot - we need a new CDR */
				struct cdr_object *new_cdr;
				new_cdr = cdr_object_create_and_append(cdr);
				if (new_cdr) {
					new_cdr->fn_table->process_party_a(new_cdr, new_snapshot);
				}
			}
		} else {
			CDR_DEBUG(mod_cfg, "%p - Beginning finalize/dispatch for %s\n", cdr, old_snapshot->name);
			for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
				cdr_object_finalize(it_cdr);
			}
			cdr_object_dispatch(cdr);
			ao2_unlink(active_cdrs_by_channel, cdr);
		}
		ao2_unlock(cdr);
	}

	/* Handle Party B */
	if (new_snapshot) {
		ao2_callback(active_cdrs_by_channel, OBJ_NODATA, cdr_object_update_party_b,
			new_snapshot);
	} else {
		ao2_callback(active_cdrs_by_channel, OBJ_NODATA, cdr_object_finalize_party_b,
			old_snapshot);
	}

}

struct bridge_leave_data {
	struct ast_bridge_snapshot *bridge;
	struct ast_channel_snapshot *channel;
};

/*! \brief Callback used to notify CDRs of a Party B leaving the bridge */
static int cdr_object_party_b_left_bridge_cb(void *obj, void *arg, int flags)
{
	struct cdr_object *cdr = obj;
	struct bridge_leave_data *leave_data = arg;
	struct cdr_object *it_cdr;

	if (strcmp(cdr->bridge, leave_data->bridge->uniqueid)) {
		return 0;
	}
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->fn_table != &bridge_state_fn_table) {
			continue;
		}
		if (!it_cdr->party_b.snapshot) {
			continue;
		}
		if (strcasecmp(it_cdr->party_b.snapshot->name, leave_data->channel->name)) {
			continue;
		}
		/* It is our Party B, in our bridge. Set the end time and let the handler
		 * transition our CDR appropriately when we leave the bridge.
		 */
		cdr_object_finalize(it_cdr);
	}
	return 0;
}

/*! \brief Filter bridge messages based on bridge technology */
static int filter_bridge_messages(struct ast_bridge_snapshot *bridge)
{
	/* Ignore holding bridge technology messages. We treat this simply as an application
	 * that a channel enters into.
	 */
	if (!strcmp(bridge->technology, "holding_bridge") && strcmp(bridge->subclass, "parking")) {
		return 1;
	}
	return 0;
}

/*!
 * \brief Handler for when a channel leaves a bridge
 * \param data Passed on
 * \param sub The stasis subscription for this message callback
 * \param topic The topic this message was published for
 * \param message The message - hopefully a bridge one!
 */
static void handle_bridge_leave_message(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_bridge_blob *update = stasis_message_data(message);
	struct ast_bridge_snapshot *bridge = update->bridge;
	struct ast_channel_snapshot *channel = update->channel;
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);
	RAII_VAR(struct cdr_object *, cdr,
			ao2_find(active_cdrs_by_channel, channel->uniqueid, OBJ_KEY),
			ao2_cleanup);
	struct cdr_object *it_cdr;
	struct bridge_leave_data leave_data = {
		.bridge = bridge,
		.channel = channel,
	};
	int left_bridge = 0;

	if (filter_bridge_messages(bridge)) {
		return;
	}

	if (filter_channel_snapshot(channel)) {
		return;
	}

	CDR_DEBUG(mod_cfg, "Bridge Leave message for %s: %u.%08u\n",
			channel->name,
			(unsigned int)stasis_message_timestamp(message)->tv_sec,
			(unsigned int)stasis_message_timestamp(message)->tv_usec);

	if (!cdr) {
		ast_log(AST_LOG_WARNING, "No CDR for channel %s\n", channel->name);
		ast_assert(0);
		return;
	}

	/* Party A */
	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (!it_cdr->fn_table->process_bridge_leave) {
			continue;
		}
		CDR_DEBUG(mod_cfg, "%p - Processing Bridge Leave for %s\n",
				it_cdr, channel->name);
		if (!it_cdr->fn_table->process_bridge_leave(it_cdr, bridge, channel)) {
			ast_string_field_set(it_cdr, bridge, "");
			left_bridge = 1;
		}
	}
	ao2_unlock(cdr);
	if (!left_bridge) {
		return;
	}

	if (strcmp(bridge->subclass, "parking")) {
		/* Party B */
		ao2_callback(active_cdrs_by_channel, OBJ_NODATA,
				cdr_object_party_b_left_bridge_cb,
				&leave_data);
	}
}

/*!
 * \internal
 * \brief Create a new CDR, append it to an existing CDR, and update its snapshots
 *
 * \note The new CDR will be automatically transitioned to the bridge state
 */
static void bridge_candidate_add_to_cdr(struct cdr_object *cdr,
		struct cdr_object_snapshot *party_b)
{
	RAII_VAR(struct module_config *,  mod_cfg,
		ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct cdr_object *new_cdr;

	new_cdr = cdr_object_create_and_append(cdr);
	if (!new_cdr) {
		return;
	}
	cdr_object_snapshot_copy(&new_cdr->party_b, party_b);
	cdr_object_check_party_a_answer(new_cdr);
	ast_string_field_set(new_cdr, bridge, cdr->bridge);
	cdr_object_transition_state(new_cdr, &bridge_state_fn_table);
	CDR_DEBUG(mod_cfg, "%p - Party A %s has new Party B %s\n",
		new_cdr, new_cdr->party_a.snapshot->name,
		party_b->snapshot->name);
}

/*!
 * \brief Process a single \ref bridge_candidate
 *
 * When a CDR enters a bridge, it needs to make pairings with everyone else
 * that it is not currently paired with. This function determines, for the
 * CDR for the channel that entered the bridge and the CDR for every other
 * channel currently in the bridge, who is Party A and makes new CDRs.
 *
 * \param cdr The \ref cdr_obj being processed
 * \param cand_cdr The \ref cdr_object that is a candidate
 *
 */
static int bridge_candidate_process(struct cdr_object *cdr, struct cdr_object *base_cand_cdr)
{
	RAII_VAR(struct module_config *, mod_cfg,
		ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct cdr_object_snapshot *party_a;
	struct cdr_object *cand_cdr;

	SCOPED_AO2LOCK(lock, base_cand_cdr);

	for (cand_cdr = base_cand_cdr; cand_cdr; cand_cdr = cand_cdr->next) {
		/* Skip any records that are not in this bridge */
		if (strcmp(cand_cdr->bridge, cdr->bridge)) {
			continue;
		}

		/* If the candidate is us or someone we've taken on, pass on by */
		if (!strcasecmp(cdr->party_a.snapshot->name, cand_cdr->party_a.snapshot->name)
			|| (cdr->party_b.snapshot
				&& !strcasecmp(cdr->party_b.snapshot->name, cand_cdr->party_a.snapshot->name))) {
			return 0;
		}

		party_a = cdr_object_pick_party_a(&cdr->party_a, &cand_cdr->party_a);
		/* We're party A - make a new CDR, append it to us, and set the candidate as
		 * Party B */
		if (!strcasecmp(party_a->snapshot->name, cdr->party_a.snapshot->name)) {
			bridge_candidate_add_to_cdr(cdr, &cand_cdr->party_a);
			return 0;
		}

		/* We're Party B. Check if we can add ourselves immediately or if we need
		 * a new CDR for them (they already have a Party B) */
		if (cand_cdr->party_b.snapshot
			&& strcasecmp(cand_cdr->party_b.snapshot->name, cdr->party_a.snapshot->name)) {
			bridge_candidate_add_to_cdr(cand_cdr, &cdr->party_a);
		} else {
			CDR_DEBUG(mod_cfg, "%p - Party A %s has new Party B %s\n",
				cand_cdr, cand_cdr->party_a.snapshot->name,
				cdr->party_a.snapshot->name);
			cdr_object_snapshot_copy(&cand_cdr->party_b, &cdr->party_a);
			/* It's possible that this joined at one point and was never chosen
			 * as party A. Clear their end time, as it would be set in such a
			 * case.
			 */
			memset(&cand_cdr->end, 0, sizeof(cand_cdr->end));
		}
		return 0;
	}
	return 0;
}

/*!
 * \brief Handle creating bridge pairings for the \ref cdr_object that just
 * entered a bridge
 * \param cdr The \ref cdr_object that just entered the bridge
 * \param bridge The \ref ast_bridge_snapshot representing the bridge it just entered
 */
static void handle_bridge_pairings(struct cdr_object *cdr, struct ast_bridge_snapshot *bridge)
{
	struct ao2_iterator it_channels;
	char *channel_id;

	it_channels = ao2_iterator_init(bridge->channels, 0);
	while ((channel_id = ao2_iterator_next(&it_channels))) {
		RAII_VAR(struct cdr_object *, cand_cdr,
			ao2_find(active_cdrs_by_channel, channel_id, OBJ_KEY),
			ao2_cleanup);

		if (!cand_cdr) {
			ao2_ref(channel_id, -1);
			continue;
		}

		bridge_candidate_process(cdr, cand_cdr);

		ao2_ref(channel_id, -1);
	}
	ao2_iterator_destroy(&it_channels);
}

/*! \brief Handle entering into a parking bridge
 * \param cdr The CDR to operate on
 * \param bridge The bridge the channel just entered
 * \param channel The channel snapshot
 */
static void handle_parking_bridge_enter_message(struct cdr_object *cdr,
		struct ast_bridge_snapshot *bridge,
		struct ast_channel_snapshot *channel)
{
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);
	int res = 1;
	struct cdr_object *it_cdr;
	struct cdr_object *new_cdr;

	ao2_lock(cdr);

	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->fn_table->process_parking_bridge_enter) {
			res &= it_cdr->fn_table->process_parking_bridge_enter(it_cdr, bridge, channel);
		}
		if (it_cdr->fn_table->process_party_a) {
			CDR_DEBUG(mod_cfg, "%p - Updating Party A %s snapshot\n", it_cdr,
					channel->name);
			it_cdr->fn_table->process_party_a(it_cdr, channel);
		}
	}

	if (res) {
		/* No one handled it - we need a new one! */
		new_cdr = cdr_object_create_and_append(cdr);
		if (new_cdr) {
			/* Let the single state transition us to Parked */
			cdr_object_transition_state(new_cdr, &single_state_fn_table);
			new_cdr->fn_table->process_parking_bridge_enter(new_cdr, bridge, channel);
		}
	}
	ao2_unlock(cdr);
}

/*! \brief Handle a bridge enter message for a 'normal' bridge
 * \param cdr The CDR to operate on
 * \param bridge The bridge the channel just entered
 * \param channel The channel snapshot
 */
static void handle_standard_bridge_enter_message(struct cdr_object *cdr,
		struct ast_bridge_snapshot *bridge,
		struct ast_channel_snapshot *channel)
{
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);
	enum process_bridge_enter_results result;
	struct cdr_object *it_cdr;
	struct cdr_object *new_cdr;
	struct cdr_object *handled_cdr = NULL;

	ao2_lock(cdr);

	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->fn_table->process_party_a) {
			CDR_DEBUG(mod_cfg, "%p - Updating Party A %s snapshot\n", it_cdr,
					channel->name);
			it_cdr->fn_table->process_party_a(it_cdr, channel);
		}

		/* Notify all states that they have entered a bridge */
		if (it_cdr->fn_table->process_bridge_enter) {
			CDR_DEBUG(mod_cfg, "%p - Processing bridge enter for %s\n", it_cdr,
					channel->name);
			result = it_cdr->fn_table->process_bridge_enter(it_cdr, bridge, channel);
			switch (result) {
			case BRIDGE_ENTER_ONLY_PARTY:
				/* Fall through */
			case BRIDGE_ENTER_OBTAINED_PARTY_B:
				if (!handled_cdr) {
					handled_cdr = it_cdr;
				}
				break;
			case BRIDGE_ENTER_NEED_CDR:
				/* Pass */
				break;
			case BRIDGE_ENTER_NO_PARTY_B:
				/* We didn't win on any - end this CDR. If someone else comes in later
				 * that is Party B to this CDR, it can re-activate this CDR.
				 */
				if (!handled_cdr) {
					handled_cdr = it_cdr;
				}
				cdr_object_finalize(cdr);
				break;
			}
		}
	}

	/* Create the new matchings, but only for either:
	 *  * The first CDR in the chain that handled it. This avoids issues with
	 *    forked CDRs.
	 *  * If no one handled it, the last CDR in the chain. This would occur if
	 *    a CDR joined a bridge and it wasn't Party A for anyone. We still need
	 *    to make pairings with everyone in the bridge.
	 */
	if (handled_cdr) {
		handle_bridge_pairings(handled_cdr, bridge);
	} else {
		/* Nothing handled it - we need a new one! */
		new_cdr = cdr_object_create_and_append(cdr);
		if (new_cdr) {
			/* This is guaranteed to succeed: the new CDR is created in the single state
			 * and will be able to handle the bridge enter message
			 */
			handle_standard_bridge_enter_message(cdr, bridge, channel);
		}
	}
	ao2_unlock(cdr);
}

/*!
 * \internal
 * \brief Handler for Stasis-Core bridge enter messages
 * \param data Passed on
 * \param sub The stasis subscription for this message callback
 * \param topic The topic this message was published for
 * \param message The message - hopefully a bridge one!
 */
static void handle_bridge_enter_message(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_bridge_blob *update = stasis_message_data(message);
	struct ast_bridge_snapshot *bridge = update->bridge;
	struct ast_channel_snapshot *channel = update->channel;
	RAII_VAR(struct cdr_object *, cdr,
			ao2_find(active_cdrs_by_channel, channel->uniqueid, OBJ_KEY),
			ao2_cleanup);
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);

	if (filter_bridge_messages(bridge)) {
		return;
	}

	if (filter_channel_snapshot(channel)) {
		return;
	}

	CDR_DEBUG(mod_cfg, "Bridge Enter message for channel %s: %u.%08u\n",
			channel->name,
			(unsigned int)stasis_message_timestamp(message)->tv_sec,
			(unsigned int)stasis_message_timestamp(message)->tv_usec);

	if (!cdr) {
		ast_log(AST_LOG_WARNING, "No CDR for channel %s\n", channel->name);
		ast_assert(0);
		return;
	}

	if (!strcmp(bridge->subclass, "parking")) {
		handle_parking_bridge_enter_message(cdr, bridge, channel);
	} else {
		handle_standard_bridge_enter_message(cdr, bridge, channel);
	}
}

/*!
 * \brief Handler for when a channel is parked
 * \param data Passed on
 * \param sub The stasis subscription for this message callback
 * \param topic The topic this message was published for
 * \param message The message about who got parked
 * */
static void handle_parked_call_message(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_parked_call_payload *payload = stasis_message_data(message);
	struct ast_channel_snapshot *channel = payload->parkee;
	RAII_VAR(struct cdr_object *, cdr, NULL, ao2_cleanup);
	RAII_VAR(struct module_config *, mod_cfg,
			ao2_global_obj_ref(module_configs), ao2_cleanup);
	int unhandled = 1;
	struct cdr_object *it_cdr;

	/* Anything other than getting parked will be handled by other updates */
	if (payload->event_type != PARKED_CALL) {
		return;
	}

	/* No one got parked? */
	if (!channel) {
		return;
	}

	if (filter_channel_snapshot(channel)) {
		return;
	}

	CDR_DEBUG(mod_cfg, "Parked Call message for channel %s: %u.%08u\n",
			channel->name,
			(unsigned int)stasis_message_timestamp(message)->tv_sec,
			(unsigned int)stasis_message_timestamp(message)->tv_usec);

	cdr = ao2_find(active_cdrs_by_channel, channel->uniqueid, OBJ_KEY);
	if (!cdr) {
		ast_log(AST_LOG_WARNING, "No CDR for channel %s\n", channel->name);
		ast_assert(0);
		return;
	}

	ao2_lock(cdr);

	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->fn_table->process_parked_channel) {
			unhandled &= it_cdr->fn_table->process_parked_channel(it_cdr, payload);
		}
	}

	if (unhandled) {
		/* Nothing handled the messgae - we need a new one! */
		struct cdr_object *new_cdr = cdr_object_create_and_append(cdr);
		if (new_cdr) {
			/* As the new CDR is created in the single state, it is guaranteed
			 * to have a function for the parked call message and will handle
			 * the message */
			new_cdr->fn_table->process_parked_channel(new_cdr, payload);
		}
	}

	ao2_unlock(cdr);

}

/*!
 * \brief Handler for a synchronization message
 * \param data Passed on
 * \param sub The stasis subscription for this message callback
 * \param topic The topic this message was published for
 * \param message A blank ao2 object
 * */
static void handle_cdr_sync_message(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	return;
}

struct ast_cdr_config *ast_cdr_get_config(void)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	ao2_ref(mod_cfg->general, +1);
	return mod_cfg->general;
}

void ast_cdr_set_config(struct ast_cdr_config *config)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	ao2_cleanup(mod_cfg->general);
	mod_cfg->general = config;
	ao2_ref(mod_cfg->general, +1);

	cdr_toggle_runtime_options();
}

int ast_cdr_is_enabled(void)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	return ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED);
}

int ast_cdr_backend_suspend(const char *name)
{
	int success = -1;
	struct cdr_beitem *i = NULL;

	AST_RWLIST_WRLOCK(&be_list);
	AST_RWLIST_TRAVERSE(&be_list, i, list) {
		if (!strcasecmp(name, i->name)) {
			ast_debug(3, "Suspending CDR backend %s\n", i->name);
			i->suspended = 1;
			success = 0;
		}
	}
	AST_RWLIST_UNLOCK(&be_list);

	return success;
}

int ast_cdr_backend_unsuspend(const char *name)
{
	int success = -1;
	struct cdr_beitem *i = NULL;

	AST_RWLIST_WRLOCK(&be_list);
	AST_RWLIST_TRAVERSE(&be_list, i, list) {
		if (!strcasecmp(name, i->name)) {
			ast_debug(3, "Unsuspending CDR backend %s\n", i->name);
			i->suspended = 0;
			success = 0;
		}
	}
	AST_RWLIST_UNLOCK(&be_list);

	return success;
}

int ast_cdr_register(const char *name, const char *desc, ast_cdrbe be)
{
	struct cdr_beitem *i = NULL;

	if (!name)
		return -1;

	if (!be) {
		ast_log(LOG_WARNING, "CDR engine '%s' lacks backend\n", name);
		return -1;
	}

	AST_RWLIST_WRLOCK(&be_list);
	AST_RWLIST_TRAVERSE(&be_list, i, list) {
		if (!strcasecmp(name, i->name)) {
			ast_log(LOG_WARNING, "Already have a CDR backend called '%s'\n", name);
			AST_RWLIST_UNLOCK(&be_list);
			return -1;
		}
	}

	if (!(i = ast_calloc(1, sizeof(*i))))
		return -1;

	i->be = be;
	ast_copy_string(i->name, name, sizeof(i->name));
	ast_copy_string(i->desc, desc, sizeof(i->desc));

	AST_RWLIST_INSERT_HEAD(&be_list, i, list);
	AST_RWLIST_UNLOCK(&be_list);

	return 0;
}

int ast_cdr_unregister(const char *name)
{
	struct cdr_beitem *match = NULL;
	int active_count;

	AST_RWLIST_WRLOCK(&be_list);
	AST_RWLIST_TRAVERSE(&be_list, match, list) {
		if (!strcasecmp(name, match->name)) {
			break;
		}
	}

	if (!match) {
		AST_RWLIST_UNLOCK(&be_list);
		return 0;
	}

	active_count = ao2_container_count(active_cdrs_by_channel);

	if (!match->suspended && active_count != 0) {
		AST_RWLIST_UNLOCK(&be_list);
		ast_log(AST_LOG_WARNING, "Unable to unregister CDR backend %s; %d CDRs are still active\n",
			name, active_count);
		return -1;
	}

	AST_RWLIST_REMOVE(&be_list, match, list);
	AST_RWLIST_UNLOCK(&be_list);

	ast_verb(2, "Unregistered '%s' CDR backend\n", name);
	ast_free(match);

	return 0;
}

struct ast_cdr *ast_cdr_dup(struct ast_cdr *cdr)
{
	struct ast_cdr *newcdr;

	if (!cdr) {
		return NULL;
	}
	newcdr = ast_cdr_alloc();
	if (!newcdr) {
		return NULL;
	}

	*newcdr = *cdr;
	AST_LIST_HEAD_INIT_NOLOCK(&newcdr->varshead);
	copy_variables(&newcdr->varshead, &cdr->varshead);
	newcdr->next = NULL;

	return newcdr;
}

static const char *cdr_format_var_internal(struct ast_cdr *cdr, const char *name)
{
	struct ast_var_t *variables;

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	AST_LIST_TRAVERSE(&cdr->varshead, variables, entries) {
		if (!strcasecmp(name, ast_var_name(variables))) {
			return ast_var_value(variables);
		}
	}

	return NULL;
}

static void cdr_get_tv(struct timeval when, const char *fmt, char *buf, int bufsize)
{
	if (fmt == NULL) {	/* raw mode */
		snprintf(buf, bufsize, "%ld.%06ld", (long)when.tv_sec, (long)when.tv_usec);
	} else {
		buf[0] = '\0';/* Ensure the buffer is initialized. */
		if (when.tv_sec) {
			struct ast_tm tm;

			ast_localtime(&when, &tm, NULL);
			ast_strftime(buf, bufsize, fmt, &tm);
		}
	}
}

void ast_cdr_format_var(struct ast_cdr *cdr, const char *name, char **ret, char *workspace, int workspacelen, int raw)
{
	const char *fmt = "%Y-%m-%d %T";
	const char *varbuf;

	if (!cdr) {
		return;
	}

	*ret = NULL;

	if (!strcasecmp(name, "clid")) {
		ast_copy_string(workspace, cdr->clid, workspacelen);
	} else if (!strcasecmp(name, "src")) {
		ast_copy_string(workspace, cdr->src, workspacelen);
	} else if (!strcasecmp(name, "dst")) {
		ast_copy_string(workspace, cdr->dst, workspacelen);
	} else if (!strcasecmp(name, "dcontext")) {
		ast_copy_string(workspace, cdr->dcontext, workspacelen);
	} else if (!strcasecmp(name, "channel")) {
		ast_copy_string(workspace, cdr->channel, workspacelen);
	} else if (!strcasecmp(name, "dstchannel")) {
		ast_copy_string(workspace, cdr->dstchannel, workspacelen);
	} else if (!strcasecmp(name, "lastapp")) {
		ast_copy_string(workspace, cdr->lastapp, workspacelen);
	} else if (!strcasecmp(name, "lastdata")) {
		ast_copy_string(workspace, cdr->lastdata, workspacelen);
	} else if (!strcasecmp(name, "start")) {
		cdr_get_tv(cdr->start, raw ? NULL : fmt, workspace, workspacelen);
	} else if (!strcasecmp(name, "answer")) {
		cdr_get_tv(cdr->answer, raw ? NULL : fmt, workspace, workspacelen);
	} else if (!strcasecmp(name, "end")) {
		cdr_get_tv(cdr->end, raw ? NULL : fmt, workspace, workspacelen);
	} else if (!strcasecmp(name, "duration")) {
		snprintf(workspace, workspacelen, "%ld", cdr->end.tv_sec != 0 ? cdr->duration : (long)ast_tvdiff_ms(ast_tvnow(), cdr->start) / 1000);
	} else if (!strcasecmp(name, "billsec")) {
		snprintf(workspace, workspacelen, "%ld", (cdr->billsec || !ast_tvzero(cdr->end) || ast_tvzero(cdr->answer)) ? cdr->billsec : (long)ast_tvdiff_ms(ast_tvnow(), cdr->answer) / 1000);
	} else if (!strcasecmp(name, "disposition")) {
		if (raw) {
			snprintf(workspace, workspacelen, "%ld", cdr->disposition);
		} else {
			ast_copy_string(workspace, ast_cdr_disp2str(cdr->disposition), workspacelen);
		}
	} else if (!strcasecmp(name, "amaflags")) {
		if (raw) {
			snprintf(workspace, workspacelen, "%ld", cdr->amaflags);
		} else {
			ast_copy_string(workspace, ast_channel_amaflags2string(cdr->amaflags), workspacelen);
		}
	} else if (!strcasecmp(name, "accountcode")) {
		ast_copy_string(workspace, cdr->accountcode, workspacelen);
	} else if (!strcasecmp(name, "peeraccount")) {
		ast_copy_string(workspace, cdr->peeraccount, workspacelen);
	} else if (!strcasecmp(name, "uniqueid")) {
		ast_copy_string(workspace, cdr->uniqueid, workspacelen);
	} else if (!strcasecmp(name, "linkedid")) {
		ast_copy_string(workspace, cdr->linkedid, workspacelen);
	} else if (!strcasecmp(name, "userfield")) {
		ast_copy_string(workspace, cdr->userfield, workspacelen);
	} else if (!strcasecmp(name, "sequence")) {
		snprintf(workspace, workspacelen, "%d", cdr->sequence);
	} else if ((varbuf = cdr_format_var_internal(cdr, name))) {
		ast_copy_string(workspace, varbuf, workspacelen);
	} else {
		workspace[0] = '\0';
	}

	if (!ast_strlen_zero(workspace)) {
		*ret = workspace;
	}
}

/*
 * \internal
 * \brief Callback that finds all CDRs that reference a particular channel by name
 */
static int cdr_object_select_all_by_name_cb(void *obj, void *arg, int flags)
{
	struct cdr_object *cdr = obj;
	const char *name = arg;

	if (!strcasecmp(cdr->party_a.snapshot->name, name) ||
			(cdr->party_b.snapshot && !strcasecmp(cdr->party_b.snapshot->name, name))) {
		return CMP_MATCH;
	}
	return 0;
}

/*
 * \internal
 * \brief Callback that finds a CDR by channel name
 */
static int cdr_object_get_by_name_cb(void *obj, void *arg, int flags)
{
	struct cdr_object *cdr = obj;
	const char *name = arg;

	if (!strcasecmp(cdr->party_a.snapshot->name, name)) {
		return CMP_MATCH;
	}
	return 0;
}

/* Read Only CDR variables */
static const char * const cdr_readonly_vars[] = {
	"clid",
	"src",
	"dst",
	"dcontext",
	"channel",
	"dstchannel",
	"lastapp",
	"lastdata",
	"start",
	"answer",
	"end",
	"duration",
	"billsec",
	"disposition",
	"amaflags",
	"accountcode",
	"uniqueid",
	"linkedid",
	"userfield",
	"sequence",
	NULL
};

int ast_cdr_setvar(const char *channel_name, const char *name, const char *value)
{
	struct cdr_object *cdr;
	struct cdr_object *it_cdr;
	struct ao2_iterator *it_cdrs;
	char *arg = ast_strdupa(channel_name);
	int x;

	for (x = 0; cdr_readonly_vars[x]; x++) {
		if (!strcasecmp(name, cdr_readonly_vars[x])) {
			ast_log(LOG_ERROR, "Attempt to set the '%s' read-only variable!\n", name);
			return -1;
		}
	}

	it_cdrs = ao2_callback(active_cdrs_by_channel, OBJ_MULTIPLE, cdr_object_select_all_by_name_cb, arg);
	if (!it_cdrs) {
		ast_log(AST_LOG_ERROR, "Unable to find CDR for channel %s\n", channel_name);
		return -1;
	}

	for (; (cdr = ao2_iterator_next(it_cdrs)); ao2_unlock(cdr), ao2_cleanup(cdr)) {
		ao2_lock(cdr);
		for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
			struct varshead *headp = NULL;

			if (it_cdr->fn_table == &finalized_state_fn_table) {
				continue;
			}
			if (!strcasecmp(channel_name, it_cdr->party_a.snapshot->name)) {
				headp = &it_cdr->party_a.variables;
			} else if (it_cdr->party_b.snapshot
				&& !strcasecmp(channel_name, it_cdr->party_b.snapshot->name)) {
				headp = &it_cdr->party_b.variables;
			}
			if (headp) {
				set_variable(headp, name, value);
			}
		}
	}
	ao2_iterator_destroy(it_cdrs);

	return 0;
}

/*!
 * \brief Format a variable on a \ref cdr_object
 */
static void cdr_object_format_var_internal(struct cdr_object *cdr, const char *name, char *value, size_t length)
{
	struct ast_var_t *variable;

	AST_LIST_TRAVERSE(&cdr->party_a.variables, variable, entries) {
		if (!strcasecmp(name, ast_var_name(variable))) {
			ast_copy_string(value, ast_var_value(variable), length);
			return;
		}
	}

	*value = '\0';
}

/*!
 * \brief Format one of the standard properties on a \ref cdr_object
 */
static int cdr_object_format_property(struct cdr_object *cdr_obj, const char *name, char *value, size_t length)
{
	struct ast_channel_snapshot *party_a = cdr_obj->party_a.snapshot;
	struct ast_channel_snapshot *party_b = cdr_obj->party_b.snapshot;

	if (!strcasecmp(name, "clid")) {
		ast_callerid_merge(value, length, party_a->caller_name, party_a->caller_number, "");
	} else if (!strcasecmp(name, "src")) {
		ast_copy_string(value, party_a->caller_number, length);
	} else if (!strcasecmp(name, "dst")) {
		ast_copy_string(value, party_a->exten, length);
	} else if (!strcasecmp(name, "dcontext")) {
		ast_copy_string(value, party_a->context, length);
	} else if (!strcasecmp(name, "channel")) {
		ast_copy_string(value, party_a->name, length);
	} else if (!strcasecmp(name, "dstchannel")) {
		if (party_b) {
			ast_copy_string(value, party_b->name, length);
		} else {
			ast_copy_string(value, "", length);
		}
	} else if (!strcasecmp(name, "lastapp")) {
		ast_copy_string(value, party_a->appl, length);
	} else if (!strcasecmp(name, "lastdata")) {
		ast_copy_string(value, party_a->data, length);
	} else if (!strcasecmp(name, "start")) {
		cdr_get_tv(cdr_obj->start, NULL, value, length);
	} else if (!strcasecmp(name, "answer")) {
		cdr_get_tv(cdr_obj->answer, NULL, value, length);
	} else if (!strcasecmp(name, "end")) {
		cdr_get_tv(cdr_obj->end, NULL, value, length);
	} else if (!strcasecmp(name, "duration")) {
		snprintf(value, length, "%ld", cdr_object_get_duration(cdr_obj));
	} else if (!strcasecmp(name, "billsec")) {
		snprintf(value, length, "%ld", cdr_object_get_billsec(cdr_obj));
	} else if (!strcasecmp(name, "disposition")) {
		snprintf(value, length, "%u", cdr_obj->disposition);
	} else if (!strcasecmp(name, "amaflags")) {
		snprintf(value, length, "%d", party_a->amaflags);
	} else if (!strcasecmp(name, "accountcode")) {
		ast_copy_string(value, party_a->accountcode, length);
	} else if (!strcasecmp(name, "peeraccount")) {
		if (party_b) {
			ast_copy_string(value, party_b->accountcode, length);
		} else {
			ast_copy_string(value, "", length);
		}
	} else if (!strcasecmp(name, "uniqueid")) {
		ast_copy_string(value, party_a->uniqueid, length);
	} else if (!strcasecmp(name, "linkedid")) {
		ast_copy_string(value, cdr_obj->linkedid, length);
	} else if (!strcasecmp(name, "userfield")) {
		ast_copy_string(value, cdr_obj->party_a.userfield, length);
	} else if (!strcasecmp(name, "sequence")) {
		snprintf(value, length, "%u", cdr_obj->sequence);
	} else {
		return 1;
	}

	return 0;
}

/*! \internal
 * \brief Look up and retrieve a CDR object by channel name
 * \param name The name of the channel
 * \retval NULL on error
 * \retval The \ref cdr_object for the channel on success, with the reference
 *	count bumped by one.
 */
static struct cdr_object *cdr_object_get_by_name(const char *name)
{
	char *param;

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	param = ast_strdupa(name);
	return ao2_callback(active_cdrs_by_channel, 0, cdr_object_get_by_name_cb, param);
}

int ast_cdr_getvar(const char *channel_name, const char *name, char *value, size_t length)
{
	RAII_VAR(struct cdr_object *, cdr, cdr_object_get_by_name(channel_name), ao2_cleanup);
	struct cdr_object *cdr_obj;

	if (!cdr) {
		ast_log(AST_LOG_ERROR, "Unable to find CDR for channel %s\n", channel_name);
		return 1;
	}

	if (ast_strlen_zero(name)) {
		return 1;
	}

	ao2_lock(cdr);

	cdr_obj = cdr->last;
	if (cdr_object_format_property(cdr_obj, name, value, length)) {
		/* Property failed; attempt variable */
		cdr_object_format_var_internal(cdr_obj, name, value, length);
	}

	ao2_unlock(cdr);

	return 0;
}

int ast_cdr_serialize_variables(const char *channel_name, struct ast_str **buf, char delim, char sep)
{
	RAII_VAR(struct cdr_object *, cdr, cdr_object_get_by_name(channel_name), ao2_cleanup);
	struct cdr_object *it_cdr;
	struct ast_var_t *variable;
	const char *var;
	char workspace[256];
	int total = 0, x = 0, i;

	if (!cdr) {
		RAII_VAR(struct module_config *, mod_cfg,
			 ao2_global_obj_ref(module_configs), ao2_cleanup);

		if (ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED)) {
			ast_log(AST_LOG_ERROR, "Unable to find CDR for channel %s\n", channel_name);
		}

		return 0;
	}

	ast_str_reset(*buf);

	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (++x > 1)
			ast_str_append(buf, 0, "\n");

		AST_LIST_TRAVERSE(&it_cdr->party_a.variables, variable, entries) {
			if (!(var = ast_var_name(variable))) {
				continue;
			}

			if (ast_str_append(buf, 0, "level %d: %s%c%s%c", x, var, delim, S_OR(ast_var_value(variable), ""), sep) < 0) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			}

			total++;
		}

		for (i = 0; cdr_readonly_vars[i]; i++) {
			if (cdr_object_format_property(it_cdr, cdr_readonly_vars[i], workspace, sizeof(workspace))) {
				/* Unhandled read-only CDR variable. */
				ast_assert(0);
				continue;
			}

			if (!ast_strlen_zero(workspace)
				&& ast_str_append(buf, 0, "level %d: %s%c%s%c", x, cdr_readonly_vars[i], delim, workspace, sep) < 0) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			}
			total++;
		}
	}
	ao2_unlock(cdr);
	return total;
}

void ast_cdr_free(struct ast_cdr *cdr)
{
	while (cdr) {
		struct ast_cdr *next = cdr->next;

		free_variables(&cdr->varshead);
		ast_free(cdr);
		cdr = next;
	}
}

struct ast_cdr *ast_cdr_alloc(void)
{
	struct ast_cdr *x;

	x = ast_calloc(1, sizeof(*x));
	return x;
}

const char *ast_cdr_disp2str(int disposition)
{
	switch (disposition) {
	case AST_CDR_NULL:
		return "NO ANSWER"; /* by default, for backward compatibility */
	case AST_CDR_NOANSWER:
		return "NO ANSWER";
	case AST_CDR_FAILED:
		return "FAILED";
	case AST_CDR_BUSY:
		return "BUSY";
	case AST_CDR_ANSWERED:
		return "ANSWERED";
	case AST_CDR_CONGESTION:
		return "CONGESTION";
	}
	return "UNKNOWN";
}

struct party_b_userfield_update {
	const char *channel_name;
	const char *userfield;
};

/*! \brief Callback used to update the userfield on Party B on all CDRs */
static int cdr_object_update_party_b_userfield_cb(void *obj, void *arg, int flags)
{
	struct cdr_object *cdr = obj;
	struct party_b_userfield_update *info = arg;
	struct cdr_object *it_cdr;
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->fn_table == &finalized_state_fn_table) {
			continue;
		}
		if (it_cdr->party_b.snapshot
			&& !strcasecmp(it_cdr->party_b.snapshot->name, info->channel_name)) {
			strcpy(it_cdr->party_b.userfield, info->userfield);
		}
	}
	return 0;
}

void ast_cdr_setuserfield(const char *channel_name, const char *userfield)
{
	RAII_VAR(struct cdr_object *, cdr, cdr_object_get_by_name(channel_name), ao2_cleanup);
	struct party_b_userfield_update party_b_info = {
			.channel_name = channel_name,
			.userfield = userfield,
	};
	struct cdr_object *it_cdr;

	/* Handle Party A */
	if (cdr) {
		ao2_lock(cdr);
		for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
			if (it_cdr->fn_table == &finalized_state_fn_table) {
				continue;
			}
			strcpy(it_cdr->party_a.userfield, userfield);
		}
		ao2_unlock(cdr);
	}

	/* Handle Party B */
	ao2_callback(active_cdrs_by_channel, OBJ_NODATA,
			cdr_object_update_party_b_userfield_cb,
			&party_b_info);

}

static void post_cdr(struct ast_cdr *cdr)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct cdr_beitem *i;

	for (; cdr ; cdr = cdr->next) {
		/* For people, who don't want to see unanswered single-channel events */
		if (!ast_test_flag(&mod_cfg->general->settings, CDR_UNANSWERED) &&
				cdr->disposition < AST_CDR_ANSWERED &&
				(ast_strlen_zero(cdr->channel) || ast_strlen_zero(cdr->dstchannel))) {
			ast_debug(1, "Skipping CDR  for %s since we weren't answered\n", cdr->channel);
			continue;
		}

		if (ast_test_flag(cdr, AST_CDR_FLAG_DISABLE)) {
			continue;
		}
		AST_RWLIST_RDLOCK(&be_list);
		AST_RWLIST_TRAVERSE(&be_list, i, list) {
			if (!i->suspended) {
				i->be(cdr);
			}
		}
		AST_RWLIST_UNLOCK(&be_list);
	}
}

int ast_cdr_set_property(const char *channel_name, enum ast_cdr_options option)
{
	RAII_VAR(struct cdr_object *, cdr, cdr_object_get_by_name(channel_name), ao2_cleanup);
	struct cdr_object *it_cdr;

	if (!cdr) {
		return -1;
	}

	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->fn_table == &finalized_state_fn_table) {
			continue;
		}
		/* Note: in general, set the flags on both the CDR record as well as the
		 * Party A. Sometimes all we have is the Party A to look at.
		 */
		ast_set_flag(&it_cdr->flags, option);
		ast_set_flag(&it_cdr->party_a, option);
	}
	ao2_unlock(cdr);

	return 0;
}

int ast_cdr_clear_property(const char *channel_name, enum ast_cdr_options option)
{
	RAII_VAR(struct cdr_object *, cdr, cdr_object_get_by_name(channel_name), ao2_cleanup);
	struct cdr_object *it_cdr;

	if (!cdr) {
		return -1;
	}

	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		if (it_cdr->fn_table == &finalized_state_fn_table) {
			continue;
		}
		ast_clear_flag(&it_cdr->flags, option);
	}
	ao2_unlock(cdr);

	return 0;
}

int ast_cdr_reset(const char *channel_name, int keep_variables)
{
	RAII_VAR(struct cdr_object *, cdr, cdr_object_get_by_name(channel_name), ao2_cleanup);
	struct ast_var_t *vardata;
	struct cdr_object *it_cdr;

	if (!cdr) {
		return -1;
	}

	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		/* clear variables */
		if (!keep_variables) {
			while ((vardata = AST_LIST_REMOVE_HEAD(&it_cdr->party_a.variables, entries))) {
				ast_var_delete(vardata);
			}
			if (cdr->party_b.snapshot) {
				while ((vardata = AST_LIST_REMOVE_HEAD(&it_cdr->party_b.variables, entries))) {
					ast_var_delete(vardata);
				}
			}
		}

		/* Reset to initial state */
		memset(&it_cdr->start, 0, sizeof(it_cdr->start));
		memset(&it_cdr->end, 0, sizeof(it_cdr->end));
		memset(&it_cdr->answer, 0, sizeof(it_cdr->answer));
		it_cdr->start = ast_tvnow();
		cdr_object_check_party_a_answer(it_cdr);
	}
	ao2_unlock(cdr);

	return 0;
}

int ast_cdr_fork(const char *channel_name, struct ast_flags *options)
{
	RAII_VAR(struct cdr_object *, cdr, cdr_object_get_by_name(channel_name), ao2_cleanup);
	struct cdr_object *new_cdr;
	struct cdr_object *it_cdr;
	struct cdr_object *cdr_obj;

	if (!cdr) {
		return -1;
	}

	{
		SCOPED_AO2LOCK(lock, cdr);

		cdr_obj = cdr->last;
		if (cdr_obj->fn_table == &finalized_state_fn_table) {
			/* If the last CDR in the chain is finalized, don't allow a fork -
			 * things are already dying at this point
			 */
			return -1;
		}

		/* Copy over the basic CDR information. The Party A information is
		 * copied over automatically as part of the append
		 */
		ast_debug(1, "Forking CDR for channel %s\n", cdr->party_a.snapshot->name);
		new_cdr = cdr_object_create_and_append(cdr);
		if (!new_cdr) {
			return -1;
		}
		new_cdr->fn_table = cdr_obj->fn_table;
		ast_string_field_set(new_cdr, bridge, cdr->bridge);
		ast_string_field_set(new_cdr, appl, cdr->appl);
		ast_string_field_set(new_cdr, data, cdr->data);
		ast_string_field_set(new_cdr, context, cdr->context);
		ast_string_field_set(new_cdr, exten, cdr->exten);
		new_cdr->flags = cdr->flags;
		/* Explicitly clear the AST_CDR_LOCK_APP flag - we want
		 * the application to be changed on the new CDR if the
		 * dialplan demands it
		 */
		ast_clear_flag(&new_cdr->flags, AST_CDR_LOCK_APP);

		/* If there's a Party B, copy it over as well */
		if (cdr_obj->party_b.snapshot) {
			new_cdr->party_b.snapshot = cdr_obj->party_b.snapshot;
			ao2_ref(new_cdr->party_b.snapshot, +1);
			strcpy(new_cdr->party_b.userfield, cdr_obj->party_b.userfield);
			new_cdr->party_b.flags = cdr_obj->party_b.flags;
			if (ast_test_flag(options, AST_CDR_FLAG_KEEP_VARS)) {
				copy_variables(&new_cdr->party_b.variables, &cdr_obj->party_b.variables);
			}
		}
		new_cdr->start = cdr_obj->start;
		new_cdr->answer = cdr_obj->answer;

		/* Modify the times based on the flags passed in */
		if (ast_test_flag(options, AST_CDR_FLAG_SET_ANSWER)
				&& new_cdr->party_a.snapshot->state == AST_STATE_UP) {
			new_cdr->answer = ast_tvnow();
		}
		if (ast_test_flag(options, AST_CDR_FLAG_RESET)) {
			new_cdr->answer = ast_tvnow();
			new_cdr->start = ast_tvnow();
		}

		/* Create and append, by default, copies over the variables */
		if (!ast_test_flag(options, AST_CDR_FLAG_KEEP_VARS)) {
			free_variables(&new_cdr->party_a.variables);
		}

		/* Finalize any current CDRs */
		if (ast_test_flag(options, AST_CDR_FLAG_FINALIZE)) {
			for (it_cdr = cdr; it_cdr != new_cdr; it_cdr = it_cdr->next) {
				if (it_cdr->fn_table == &finalized_state_fn_table) {
					continue;
				}
				/* Force finalization on the CDR. This will bypass any checks for
				 * end before 'h' extension.
				 */
				cdr_object_finalize(it_cdr);
				cdr_object_transition_state(it_cdr, &finalized_state_fn_table);
			}
		}
	}

	return 0;
}

/*! \note Don't call without cdr_batch_lock */
static void reset_batch(void)
{
	batch->size = 0;
	batch->head = NULL;
	batch->tail = NULL;
}

/*! \note Don't call without cdr_batch_lock */
static int init_batch(void)
{
	/* This is the single meta-batch used to keep track of all CDRs during the entire life of the program */
	if (!(batch = ast_malloc(sizeof(*batch))))
		return -1;

	reset_batch();

	return 0;
}

static void *do_batch_backend_process(void *data)
{
	struct cdr_batch_item *processeditem;
	struct cdr_batch_item *batchitem = data;

	/* Push each CDR into storage mechanism(s) and free all the memory */
	while (batchitem) {
		post_cdr(batchitem->cdr);
		ast_cdr_free(batchitem->cdr);
		processeditem = batchitem;
		batchitem = batchitem->next;
		ast_free(processeditem);
	}

	return NULL;
}

static void cdr_submit_batch(int do_shutdown)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	struct cdr_batch_item *oldbatchitems = NULL;
	pthread_t batch_post_thread = AST_PTHREADT_NULL;

	/* if there's no batch, or no CDRs in the batch, then there's nothing to do */
	if (!batch || !batch->head) {
		return;
	}

	/* move the old CDRs aside, and prepare a new CDR batch */
	ast_mutex_lock(&cdr_batch_lock);
	oldbatchitems = batch->head;
	reset_batch();
	ast_mutex_unlock(&cdr_batch_lock);

	/* if configured, spawn a new thread to post these CDRs,
	   also try to save as much as possible if we are shutting down safely */
	if (ast_test_flag(&mod_cfg->general->batch_settings.settings, BATCH_MODE_SCHEDULER_ONLY) || do_shutdown) {
		ast_debug(1, "CDR single-threaded batch processing begins now\n");
		do_batch_backend_process(oldbatchitems);
	} else {
		if (ast_pthread_create_detached_background(&batch_post_thread, NULL, do_batch_backend_process, oldbatchitems)) {
			ast_log(LOG_WARNING, "CDR processing thread could not detach, now trying in this thread\n");
			do_batch_backend_process(oldbatchitems);
		} else {
			ast_debug(1, "CDR multi-threaded batch processing begins now\n");
		}
	}
}

static int submit_scheduled_batch(const void *data)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	cdr_submit_batch(0);
	/* manually reschedule from this point in time */

	ast_mutex_lock(&cdr_sched_lock);
	cdr_sched = ast_sched_add(sched, mod_cfg->general->batch_settings.time * 1000, submit_scheduled_batch, NULL);
	ast_mutex_unlock(&cdr_sched_lock);
	/* returning zero so the scheduler does not automatically reschedule */
	return 0;
}

/*! Do not hold the batch lock while calling this function */
static void submit_unscheduled_batch(void)
{
	/* Prevent two deletes from happening at the same time */
	ast_mutex_lock(&cdr_sched_lock);
	/* this is okay since we are not being called from within the scheduler */
	AST_SCHED_DEL(sched, cdr_sched);
	/* schedule the submission to occur ASAP (1 ms) */
	cdr_sched = ast_sched_add(sched, 1, submit_scheduled_batch, NULL);
	ast_mutex_unlock(&cdr_sched_lock);

	/* signal the do_cdr thread to wakeup early and do some work (that lazy thread ;) */
	ast_mutex_lock(&cdr_pending_lock);
	ast_cond_signal(&cdr_pending_cond);
	ast_mutex_unlock(&cdr_pending_lock);
}

static void cdr_detach(struct ast_cdr *cdr)
{
	struct cdr_batch_item *newtail;
	int curr;
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	int submit_batch = 0;

	if (!cdr) {
		return;
	}

	/* maybe they disabled CDR stuff completely, so just drop it */
	if (!ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED)) {
		ast_debug(1, "Dropping CDR !\n");
		ast_cdr_free(cdr);
		return;
	}

	/* post stuff immediately if we are not in batch mode, this is legacy behaviour */
	if (!ast_test_flag(&mod_cfg->general->settings, CDR_BATCHMODE)) {
		post_cdr(cdr);
		ast_cdr_free(cdr);
		return;
	}

	/* otherwise, each CDR gets put into a batch list (at the end) */
	ast_debug(1, "CDR detaching from this thread\n");

	/* we'll need a new tail for every CDR */
	if (!(newtail = ast_calloc(1, sizeof(*newtail)))) {
		post_cdr(cdr);
		ast_cdr_free(cdr);
		return;
	}

	/* don't traverse a whole list (just keep track of the tail) */
	ast_mutex_lock(&cdr_batch_lock);
	if (!batch)
		init_batch();
	if (!batch->head) {
		/* new batch is empty, so point the head at the new tail */
		batch->head = newtail;
	} else {
		/* already got a batch with something in it, so just append a new tail */
		batch->tail->next = newtail;
	}
	newtail->cdr = cdr;
	batch->tail = newtail;
	curr = batch->size++;

	/* if we have enough stuff to post, then do it */
	if (curr >= (mod_cfg->general->batch_settings.size - 1)) {
		submit_batch = 1;
	}
	ast_mutex_unlock(&cdr_batch_lock);

	/* Don't call submit_unscheduled_batch with the cdr_batch_lock held */
	if (submit_batch) {
		submit_unscheduled_batch();
	}
}

static void *do_cdr(void *data)
{
	struct timespec timeout;
	int schedms;
	int numevents = 0;

	for (;;) {
		struct timeval now;
		schedms = ast_sched_wait(sched);
		/* this shouldn't happen, but provide a 1 second default just in case */
		if (schedms <= 0)
			schedms = 1000;
		now = ast_tvadd(ast_tvnow(), ast_samp2tv(schedms, 1000));
		timeout.tv_sec = now.tv_sec;
		timeout.tv_nsec = now.tv_usec * 1000;
		/* prevent stuff from clobbering cdr_pending_cond, then wait on signals sent to it until the timeout expires */
		ast_mutex_lock(&cdr_pending_lock);
		ast_cond_timedwait(&cdr_pending_cond, &cdr_pending_lock, &timeout);
		numevents = ast_sched_runq(sched);
		ast_mutex_unlock(&cdr_pending_lock);
		ast_debug(2, "Processed %d scheduled CDR batches from the run queue\n", numevents);
	}

	return NULL;
}

static char *handle_cli_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "cdr set debug [on|off]";
		e->usage = "Enable or disable extra debugging in the CDR Engine. Note\n"
				"that this will dump debug information to the VERBOSE setting\n"
				"and should only be used when debugging information from the\n"
				"CDR engine is needed.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[3], "on")
		&& !ast_test_flag(&mod_cfg->general->settings, CDR_DEBUG)) {
		ast_set_flag(&mod_cfg->general->settings, CDR_DEBUG);
		ast_cli(a->fd, "CDR debugging enabled\n");
	} else if (!strcasecmp(a->argv[3], "off")
		&& ast_test_flag(&mod_cfg->general->settings, CDR_DEBUG)) {
		ast_clear_flag(&mod_cfg->general->settings, CDR_DEBUG);
		ast_cli(a->fd, "CDR debugging disabled\n");
	}

	return CLI_SUCCESS;
}

/*! \brief Complete user input for 'cdr show' */
static char *cli_complete_show(struct ast_cli_args *a)
{
	char *result = NULL;
	int wordlen = strlen(a->word);
	int which = 0;
	struct ao2_iterator it_cdrs;
	struct cdr_object *cdr;

	it_cdrs = ao2_iterator_init(active_cdrs_by_channel, 0);
	while ((cdr = ao2_iterator_next(&it_cdrs))) {
		if (!strncasecmp(a->word, cdr->party_a.snapshot->name, wordlen) &&
			(++which > a->n)) {
			result = ast_strdup(cdr->party_a.snapshot->name);
			if (result) {
				ao2_ref(cdr, -1);
				break;
			}
		}
		ao2_ref(cdr, -1);
	}
	ao2_iterator_destroy(&it_cdrs);
	return result;
}

static void cli_show_channels(struct ast_cli_args *a)
{
	struct ao2_iterator it_cdrs;
	struct cdr_object *cdr;
	char start_time_buffer[64];
	char answer_time_buffer[64];
	char end_time_buffer[64];

#define TITLE_STRING "%-25.25s %-25.25s %-15.15s %-8.8s %-8.8s %-8.8s %-8.8s %-8.8s\n"
#define FORMAT_STRING "%-25.25s %-25.25s %-15.15s %-8.8s %-8.8s %-8.8s %-8.8ld %-8.8ld\n"

	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "Channels with Call Detail Record (CDR) Information\n");
	ast_cli(a->fd, "--------------------------------------------------\n");
	ast_cli(a->fd, TITLE_STRING, "Channel", "Dst. Channel", "LastApp", "Start", "Answer", "End", "Billsec", "Duration");

	it_cdrs = ao2_iterator_init(active_cdrs_by_channel, 0);
	for (; (cdr = ao2_iterator_next(&it_cdrs)); ao2_cleanup(cdr)) {
		struct cdr_object *it_cdr;
		struct timeval start_time = { 0, };
		struct timeval answer_time = { 0, };
		struct timeval end_time = { 0, };

		SCOPED_AO2LOCK(lock, cdr);

		/* Calculate the start, end, answer, billsec, and duration over the
		 * life of all of the CDR entries
		 */
		for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
			if (snapshot_is_dialed(it_cdr->party_a.snapshot)) {
				continue;
			}
			if (ast_tvzero(start_time)) {
				start_time = it_cdr->start;
			}
			if (!ast_tvzero(it_cdr->answer) && ast_tvzero(answer_time)) {
				answer_time = it_cdr->answer;
			}
		}

		/* If there was no start time, then all CDRs were for a dialed channel; skip */
		if (ast_tvzero(start_time)) {
			continue;
		}
		it_cdr = cdr->last;

		end_time = ast_tvzero(it_cdr->end) ? ast_tvnow() : it_cdr->end;
		cdr_get_tv(start_time, "%T", start_time_buffer, sizeof(start_time_buffer));
		cdr_get_tv(answer_time, "%T", answer_time_buffer, sizeof(answer_time_buffer));
		cdr_get_tv(end_time, "%T", end_time_buffer, sizeof(end_time_buffer));
		ast_cli(a->fd, FORMAT_STRING, it_cdr->party_a.snapshot->name,
				it_cdr->party_b.snapshot ? it_cdr->party_b.snapshot->name : "<none>",
				it_cdr->appl,
				start_time_buffer,
				answer_time_buffer,
				end_time_buffer,
				ast_tvzero(answer_time) ? 0 : (long)ast_tvdiff_ms(end_time, answer_time) / 1000,
				(long)ast_tvdiff_ms(end_time, start_time) / 1000);
	}
	ao2_iterator_destroy(&it_cdrs);
#undef FORMAT_STRING
#undef TITLE_STRING
}

static void cli_show_channel(struct ast_cli_args *a)
{
	struct cdr_object *it_cdr;
	char clid[64];
	char start_time_buffer[64];
	char answer_time_buffer[64];
	char end_time_buffer[64];
	const char *channel_name = a->argv[3];
	RAII_VAR(struct cdr_object *, cdr, NULL, ao2_cleanup);

#define TITLE_STRING "%-10.10s %-20.20s %-25.25s %-15.15s %-15.15s %-8.8s %-8.8s %-8.8s %-8.8s %-8.8s\n"
#define FORMAT_STRING "%-10.10s %-20.20s %-25.25s %-15.15s %-15.15s %-8.8s %-8.8s %-8.8s %-8.8ld %-8.8ld\n"

	cdr = cdr_object_get_by_name(channel_name);
	if (!cdr) {
		ast_cli(a->fd, "Unknown channel: %s\n", channel_name);
		return;
	}

	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "Call Detail Record (CDR) Information for %s\n", channel_name);
	ast_cli(a->fd, "--------------------------------------------------\n");
	ast_cli(a->fd, TITLE_STRING, "AccountCode", "CallerID", "Dst. Channel", "LastApp", "Data", "Start", "Answer", "End", "Billsec", "Duration");

	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		struct timeval end;
		if (snapshot_is_dialed(it_cdr->party_a.snapshot)) {
			continue;
		}
		ast_callerid_merge(clid, sizeof(clid), it_cdr->party_a.snapshot->caller_name, it_cdr->party_a.snapshot->caller_number, "");
		if (ast_tvzero(it_cdr->end)) {
			end = ast_tvnow();
		} else {
			end = it_cdr->end;
		}
		cdr_get_tv(it_cdr->start, "%T", start_time_buffer, sizeof(start_time_buffer));
		cdr_get_tv(it_cdr->answer, "%T", answer_time_buffer, sizeof(answer_time_buffer));
		cdr_get_tv(end, "%T", end_time_buffer, sizeof(end_time_buffer));
		ast_cli(a->fd, FORMAT_STRING,
				it_cdr->party_a.snapshot->accountcode,
				clid,
				it_cdr->party_b.snapshot ? it_cdr->party_b.snapshot->name : "<none>",
				it_cdr->appl,
				it_cdr->data,
				start_time_buffer,
				answer_time_buffer,
				end_time_buffer,
				(long)ast_tvdiff_ms(end, it_cdr->answer) / 1000,
				(long)ast_tvdiff_ms(end, it_cdr->start) / 1000);
	}
	ao2_unlock(cdr);
#undef FORMAT_STRING
#undef TITLE_STRING
}

static char *handle_cli_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
			e->command = "cdr show active";
			e->usage =
				"Usage: cdr show active [channel]\n"
				"	Displays a summary of all Call Detail Records when [channel]\n"
				"	is omitted; displays all of the Call Detail Records\n"
				"	currently in flight for a given [channel] when [channel] is\n"
				"	specified.\n\n"
				"	Note that this will not display Call Detail Records that\n"
				"	have already been dispatched to a backend storage, nor for\n"
				"	channels that are no longer active.\n";
			return NULL;
	case CLI_GENERATE:
		return cli_complete_show(a);
	}

	if (a->argc > 4) {
		return CLI_SHOWUSAGE;
	} else if (a->argc < 4) {
		cli_show_channels(a);
	} else {
		cli_show_channel(a);
	}

	return CLI_SUCCESS;
}

static char *handle_cli_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cdr_beitem *beitem = NULL;
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	int cnt = 0;
	long nextbatchtime = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "cdr show status";
		e->usage =
			"Usage: cdr show status\n"
			"	Displays the Call Detail Record engine system status.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "Call Detail Record (CDR) settings\n");
	ast_cli(a->fd, "----------------------------------\n");
	ast_cli(a->fd, "  Logging:                    %s\n", ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED) ? "Enabled" : "Disabled");
	ast_cli(a->fd, "  Mode:                       %s\n", ast_test_flag(&mod_cfg->general->settings, CDR_BATCHMODE) ? "Batch" : "Simple");
	if (ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED)) {
		ast_cli(a->fd, "  Log unanswered calls:       %s\n", ast_test_flag(&mod_cfg->general->settings, CDR_UNANSWERED) ? "Yes" : "No");
		ast_cli(a->fd, "  Log congestion:             %s\n\n", ast_test_flag(&mod_cfg->general->settings, CDR_CONGESTION) ? "Yes" : "No");
		if (ast_test_flag(&mod_cfg->general->settings, CDR_BATCHMODE)) {
			ast_cli(a->fd, "* Batch Mode Settings\n");
			ast_cli(a->fd, "  -------------------\n");
			if (batch)
				cnt = batch->size;
			if (cdr_sched > -1)
				nextbatchtime = ast_sched_when(sched, cdr_sched);
			ast_cli(a->fd, "  Safe shutdown:              %s\n", ast_test_flag(&mod_cfg->general->batch_settings.settings, BATCH_MODE_SAFE_SHUTDOWN) ? "Enabled" : "Disabled");
			ast_cli(a->fd, "  Threading model:            %s\n", ast_test_flag(&mod_cfg->general->batch_settings.settings, BATCH_MODE_SCHEDULER_ONLY) ? "Scheduler only" : "Scheduler plus separate threads");
			ast_cli(a->fd, "  Current batch size:         %d record%s\n", cnt, ESS(cnt));
			ast_cli(a->fd, "  Maximum batch size:         %u record%s\n", mod_cfg->general->batch_settings.size, ESS(mod_cfg->general->batch_settings.size));
			ast_cli(a->fd, "  Maximum batch time:         %u second%s\n", mod_cfg->general->batch_settings.time, ESS(mod_cfg->general->batch_settings.time));
			ast_cli(a->fd, "  Next batch processing time: %ld second%s\n\n", nextbatchtime, ESS(nextbatchtime));
		}
		ast_cli(a->fd, "* Registered Backends\n");
		ast_cli(a->fd, "  -------------------\n");
		AST_RWLIST_RDLOCK(&be_list);
		if (AST_RWLIST_EMPTY(&be_list)) {
			ast_cli(a->fd, "    (none)\n");
		} else {
			AST_RWLIST_TRAVERSE(&be_list, beitem, list) {
				ast_cli(a->fd, "    %s%s\n", beitem->name, beitem->suspended ? " (suspended) " : "");
			}
		}
		AST_RWLIST_UNLOCK(&be_list);
		ast_cli(a->fd, "\n");
	}

	return CLI_SUCCESS;
}

static char *handle_cli_submit(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "cdr submit";
		e->usage =
			"Usage: cdr submit\n"
			"Posts all pending batched CDR data to the configured CDR\n"
			"backend engine modules.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc > 2) {
		return CLI_SHOWUSAGE;
	}

	if (!ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED)) {
		ast_cli(a->fd, "Cannot submit CDR batch: CDR engine disabled.\n");
		return CLI_SUCCESS;
	}

	if (ast_test_flag(&mod_cfg->general->settings, CDR_BATCHMODE)) {
		ast_cli(a->fd, "Cannot submit CDR batch: batch mode not enabled.\n");
		return CLI_SUCCESS;
	}

	submit_unscheduled_batch();
	ast_cli(a->fd, "Submitted CDRs to backend engines for processing.  This may take a while.\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(handle_cli_submit, "Posts all pending batched CDR data"),
	AST_CLI_DEFINE(handle_cli_status, "Display the CDR status"),
	AST_CLI_DEFINE(handle_cli_show, "Display active CDRs for channels"),
	AST_CLI_DEFINE(handle_cli_debug, "Enable debugging in the CDR engine"),
};

/*!
 * \brief This dispatches *all* \ref cdr_objects. It should only be used during
 * shutdown, so that we get billing records for everything that we can.
 */
static int cdr_object_dispatch_all_cb(void *obj, void *arg, int flags)
{
	struct cdr_object *cdr = obj;
	struct cdr_object *it_cdr;

	ao2_lock(cdr);
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		cdr_object_transition_state(it_cdr, &finalized_state_fn_table);
	}
	cdr_object_dispatch(cdr);
	ao2_unlock(cdr);

	return 0;
}

static void finalize_batch_mode(void)
{
	if (cdr_thread == AST_PTHREADT_NULL) {
		return;
	}
	/* wake up the thread so it will exit */
	pthread_cancel(cdr_thread);
	pthread_kill(cdr_thread, SIGURG);
	pthread_join(cdr_thread, NULL);
	cdr_thread = AST_PTHREADT_NULL;
	ast_cond_destroy(&cdr_pending_cond);
	ast_cdr_engine_term();
}

struct stasis_message_router *ast_cdr_message_router(void)
{
	if (!stasis_router) {
		return NULL;
	}

	ao2_bump(stasis_router);
	return stasis_router;
}

/*!
 * \brief Destroy the active Stasis subscriptions
 */
static void destroy_subscriptions(void)
{
	channel_subscription = stasis_forward_cancel(channel_subscription);
	bridge_subscription = stasis_forward_cancel(bridge_subscription);
	parking_subscription = stasis_forward_cancel(parking_subscription);
}

/*!
 * \brief Create the Stasis subcriptions for CDRs
 */
static int create_subscriptions(void)
{
	if (!cdr_topic) {
		return -1;
	}

	if (channel_subscription || bridge_subscription || parking_subscription) {
		return 0;
	}

	channel_subscription = stasis_forward_all(ast_channel_topic_all_cached(), cdr_topic);
	if (!channel_subscription) {
		return -1;
	}
	bridge_subscription = stasis_forward_all(ast_bridge_topic_all_cached(), cdr_topic);
	if (!bridge_subscription) {
		return -1;
	}
	parking_subscription = stasis_forward_all(ast_parking_topic(), cdr_topic);
	if (!parking_subscription) {
		return -1;
	}

	return 0;
}

static int process_config(int reload)
{
	RAII_VAR(struct module_config *, mod_cfg, module_config_alloc(), ao2_cleanup);

	if (!reload) {
		if (aco_info_init(&cfg_info)) {
			return 1;
		}

		aco_option_register(&cfg_info, "enable", ACO_EXACT, general_options, DEFAULT_ENABLED, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, settings), CDR_ENABLED);
		aco_option_register(&cfg_info, "debug", ACO_EXACT, general_options, 0, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, settings), CDR_DEBUG);
		aco_option_register(&cfg_info, "unanswered", ACO_EXACT, general_options, DEFAULT_UNANSWERED, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, settings), CDR_UNANSWERED);
		aco_option_register(&cfg_info, "congestion", ACO_EXACT, general_options, 0, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, settings), CDR_CONGESTION);
		aco_option_register(&cfg_info, "batch", ACO_EXACT, general_options, DEFAULT_BATCHMODE, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, settings), CDR_BATCHMODE);
		aco_option_register(&cfg_info, "endbeforehexten", ACO_EXACT, general_options, DEFAULT_END_BEFORE_H_EXTEN, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, settings), CDR_END_BEFORE_H_EXTEN);
		aco_option_register(&cfg_info, "initiatedseconds", ACO_EXACT, general_options, DEFAULT_INITIATED_SECONDS, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, settings), CDR_INITIATED_SECONDS);
		aco_option_register(&cfg_info, "scheduleronly", ACO_EXACT, general_options, DEFAULT_BATCH_SCHEDULER_ONLY, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, batch_settings.settings), BATCH_MODE_SCHEDULER_ONLY);
		aco_option_register(&cfg_info, "safeshutdown", ACO_EXACT, general_options, DEFAULT_BATCH_SAFE_SHUTDOWN, OPT_BOOLFLAG_T, 1, FLDSET(struct ast_cdr_config, batch_settings.settings), BATCH_MODE_SAFE_SHUTDOWN);
		aco_option_register(&cfg_info, "size", ACO_EXACT, general_options, DEFAULT_BATCH_SIZE, OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_cdr_config, batch_settings.size), 0, MAX_BATCH_SIZE);
		aco_option_register(&cfg_info, "time", ACO_EXACT, general_options, DEFAULT_BATCH_TIME, OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_cdr_config, batch_settings.time), 0, MAX_BATCH_TIME);
	}

	if (aco_process_config(&cfg_info, reload)) {
		if (!mod_cfg) {
			return 1;
		}
		/* If we couldn't process the configuration and this wasn't a reload,
		 * create a default config
		 */
		if (!reload && !(aco_set_defaults(&general_option, "general", mod_cfg->general))) {
			ast_log(LOG_NOTICE, "Failed to process CDR configuration; using defaults\n");
			ao2_global_obj_replace_unref(module_configs, mod_cfg);
			return 0;
		}
		return 1;
	}

	return 0;
}

static void cdr_engine_cleanup(void)
{
	destroy_subscriptions();
}

static void cdr_engine_shutdown(void)
{
	stasis_message_router_unsubscribe_and_join(stasis_router);
	stasis_router = NULL;

	ao2_cleanup(cdr_topic);
	cdr_topic = NULL;

	STASIS_MESSAGE_TYPE_CLEANUP(cdr_sync_message_type);

	ao2_callback(active_cdrs_by_channel, OBJ_NODATA, cdr_object_dispatch_all_cb,
		NULL);
	finalize_batch_mode();
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sched_context_destroy(sched);
	sched = NULL;
	ast_free(batch);
	batch = NULL;

	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(module_configs);

	ao2_container_unregister("cdrs_by_channel");
	ao2_ref(active_cdrs_by_channel, -1);
	active_cdrs_by_channel = NULL;
}

static void cdr_enable_batch_mode(struct ast_cdr_config *config)
{
	SCOPED_LOCK(batch, &cdr_batch_lock, ast_mutex_lock, ast_mutex_unlock);

	/* Only create the thread level portions once */
	if (cdr_thread == AST_PTHREADT_NULL) {
		ast_cond_init(&cdr_pending_cond, NULL);
		if (ast_pthread_create_background(&cdr_thread, NULL, do_cdr, NULL) < 0) {
			ast_log(LOG_ERROR, "Unable to start CDR thread.\n");
			return;
		}
	}

	/* Kill the currently scheduled item */
	AST_SCHED_DEL(sched, cdr_sched);
	cdr_sched = ast_sched_add(sched, config->batch_settings.time * 1000, submit_scheduled_batch, NULL);
	ast_log(LOG_NOTICE, "CDR batch mode logging enabled, first of either size %u or time %u seconds.\n",
			config->batch_settings.size, config->batch_settings.time);
}

/*!
 * \internal
 * \brief Print channel object key (name).
 * \since 12.0.0
 *
 * \param v_obj A pointer to the object we want the key printed.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 *
 * \return Nothing
 */
static void cdr_container_print_fn(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct cdr_object *cdr = v_obj;
	struct cdr_object *it_cdr;
	if (!cdr) {
		return;
	}
	for (it_cdr = cdr; it_cdr; it_cdr = it_cdr->next) {
		prnt(where, "Party A: %s; Party B: %s; Bridge %s\n", it_cdr->party_a.snapshot->name, it_cdr->party_b.snapshot ? it_cdr->party_b.snapshot->name : "<unknown>",
				it_cdr->bridge);
	}
}

/*!
 * \brief Checks if CDRs are enabled and enables/disables the necessary options
 */
static int cdr_toggle_runtime_options(void)
{
	RAII_VAR(struct module_config *, mod_cfg,
		ao2_global_obj_ref(module_configs), ao2_cleanup);

	if (ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED)) {
		if (create_subscriptions()) {
			destroy_subscriptions();
			ast_log(AST_LOG_ERROR, "Failed to create Stasis subscriptions\n");
			return -1;
		}
		if (ast_test_flag(&mod_cfg->general->settings, CDR_BATCHMODE)) {
			cdr_enable_batch_mode(mod_cfg->general);
		} else {
			ast_log(LOG_NOTICE, "CDR simple logging enabled.\n");
		}
	} else {
		destroy_subscriptions();
		ast_log(LOG_NOTICE, "CDR logging disabled.\n");
	}

	return 0;
}

int ast_cdr_engine_init(void)
{
	if (process_config(0)) {
		return -1;
	}

	cdr_topic = stasis_topic_create("cdr_engine");
	if (!cdr_topic) {
		return -1;
	}

	stasis_router = stasis_message_router_create(cdr_topic);
	if (!stasis_router) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(cdr_sync_message_type)) {
		return -1;
	}

	stasis_message_router_add_cache_update(stasis_router, ast_channel_snapshot_type(), handle_channel_cache_message, NULL);
	stasis_message_router_add(stasis_router, ast_channel_dial_type(), handle_dial_message, NULL);
	stasis_message_router_add(stasis_router, ast_channel_entered_bridge_type(), handle_bridge_enter_message, NULL);
	stasis_message_router_add(stasis_router, ast_channel_left_bridge_type(), handle_bridge_leave_message, NULL);
	stasis_message_router_add(stasis_router, ast_parked_call_type(), handle_parked_call_message, NULL);
	stasis_message_router_add(stasis_router, cdr_sync_message_type(), handle_cdr_sync_message, NULL);

	active_cdrs_by_channel = ao2_container_alloc(NUM_CDR_BUCKETS,
		cdr_object_channel_hash_fn, cdr_object_channel_cmp_fn);
	if (!active_cdrs_by_channel) {
		return -1;
	}
	ao2_container_register("cdrs_by_channel", active_cdrs_by_channel, cdr_container_print_fn);

	sched = ast_sched_context_create();
	if (!sched) {
		ast_log(LOG_ERROR, "Unable to create schedule context.\n");
		return -1;
	}

	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_register_cleanup(cdr_engine_cleanup);
	ast_register_atexit(cdr_engine_shutdown);

	return cdr_toggle_runtime_options();
}

void ast_cdr_engine_term(void)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	RAII_VAR(void *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	/* Since this is called explicitly during process shutdown, we might not have ever
	 * been initialized. If so, the config object will be NULL.
	 */
	if (!mod_cfg) {
		return;
	}

	if (cdr_sync_message_type()) {
		/* Make sure we have the needed items */
		payload = ao2_alloc(sizeof(*payload), NULL);
		if (!stasis_router || !payload) {
			return;
		}

		ast_debug(1, "CDR Engine termination request received; waiting on messages...\n");

		message = stasis_message_create(cdr_sync_message_type(), payload);
		if (message) {
			stasis_message_router_publish_sync(stasis_router, message);
		}
	}

	if (ast_test_flag(&mod_cfg->general->settings, CDR_BATCHMODE)) {
		cdr_submit_batch(ast_test_flag(&mod_cfg->general->batch_settings.settings, BATCH_MODE_SAFE_SHUTDOWN));
	}
}

int ast_cdr_engine_reload(void)
{
	RAII_VAR(struct module_config *, old_mod_cfg, ao2_global_obj_ref(module_configs), ao2_cleanup);
	RAII_VAR(struct module_config *, mod_cfg, NULL, ao2_cleanup);

	if (process_config(1)) {
		return -1;
	}

	mod_cfg = ao2_global_obj_ref(module_configs);

	if (!ast_test_flag(&mod_cfg->general->settings, CDR_ENABLED) ||
			!(ast_test_flag(&mod_cfg->general->settings, CDR_BATCHMODE))) {
		/* If batch mode used to be enabled, finalize the batch */
		if (ast_test_flag(&old_mod_cfg->general->settings, CDR_BATCHMODE)) {
			finalize_batch_mode();
		}
	}

	return cdr_toggle_runtime_options();
}


