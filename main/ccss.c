/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief Call Completion Supplementary Services implementation
 * \author Mark Michelson <mmichelson@digium.com>
 */

/*! \li \ref ccss.c uses the configuration file \ref ccss.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page ccss.conf ccss.conf
 * \verbinclude ccss.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/ccss.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/devicestate.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/causes.h"
#include "asterisk/stasis_system.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="CallCompletionRequest" language="en_US">
		<synopsis>
			Request call completion service for previous call
		</synopsis>
		<syntax />
		<description>
			<para>Request call completion service for a previously failed
			call attempt.</para>
			<para>This application sets the following channel variables:</para>
			<variablelist>
				<variable name="CC_REQUEST_RESULT">
					<para>This is the returned status of the request.</para>
					<value name="SUCCESS" />
					<value name="FAIL" />
				</variable>
				<variable name="CC_REQUEST_REASON">
					<para>This is the reason the request failed.</para>
					<value name="NO_CORE_INSTANCE" />
					<value name="NOT_GENERIC" />
					<value name="TOO_MANY_REQUESTS" />
					<value name="UNSPECIFIED" />
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="CallCompletionCancel" language="en_US">
		<synopsis>
			Cancel call completion service
		</synopsis>
		<syntax />
		<description>
			<para>Cancel a Call Completion Request.</para>
			<para>This application sets the following channel variables:</para>
			<variablelist>
				<variable name="CC_CANCEL_RESULT">
					<para>This is the returned status of the cancel.</para>
					<value name="SUCCESS" />
					<value name="FAIL" />
				</variable>
				<variable name="CC_CANCEL_REASON">
					<para>This is the reason the cancel failed.</para>
					<value name="NO_CORE_INSTANCE" />
					<value name="NOT_GENERIC" />
					<value name="UNSPECIFIED" />
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

/* These are some file-scoped variables. It would be
 * nice to define them closer to their first usage, but since
 * they are used in many places throughout the file, defining
 * them here at the top is easiest.
 */

/*!
 * The ast_sched_context used for all generic CC timeouts
 */
static struct ast_sched_context *cc_sched_context;
/*!
 * Counter used to create core IDs for CC calls. Each new
 * core ID is created by atomically adding 1 to the core_id_counter
 */
static int core_id_counter;
/*!
 * Taskprocessor from which all CC agent and monitor callbacks
 * are called.
 */
static struct ast_taskprocessor *cc_core_taskprocessor;
/*!
 * Name printed on all CC log messages.
 */
static const char *CC_LOGGER_LEVEL_NAME = "CC";
/*!
 * Logger level registered by the CC core.
 */
static int cc_logger_level;
/*!
 * Parsed configuration value for cc_max_requests
 */
static unsigned int global_cc_max_requests;
/*!
 * The current number of CC requests in the system
 */
static int cc_request_count;

static inline void *cc_ref(void *obj, const char *debug)
{
	ao2_t_ref(obj, +1, debug);
	return obj;
}

static inline void *cc_unref(void *obj, const char *debug)
{
	ao2_t_ref(obj, -1, debug);
	return NULL;
}

/*!
 * \since 1.8
 * \internal
 * \brief A structure for holding the configuration parameters
 * relating to CCSS
 */
struct ast_cc_config_params {
	enum ast_cc_agent_policies cc_agent_policy;
	enum ast_cc_monitor_policies cc_monitor_policy;
	unsigned int cc_offer_timer;
	unsigned int ccnr_available_timer;
	unsigned int ccbs_available_timer;
	unsigned int cc_recall_timer;
	unsigned int cc_max_agents;
	unsigned int cc_max_monitors;
	char cc_callback_macro[AST_MAX_EXTENSION];
	char cc_callback_sub[AST_MAX_EXTENSION];
	char cc_agent_dialstring[AST_MAX_EXTENSION];
};

/*!
 * \since 1.8
 * \brief The states used in the CCSS core state machine
 *
 * For more information, see doc/CCSS_architecture.pdf
 */
enum cc_state {
	/*! Entered when it is determined that CCSS may be used for the call */
	CC_AVAILABLE,
	/*! Entered when a CCSS agent has offered CCSS to a caller */
	CC_CALLER_OFFERED,
	/*! Entered when a CCSS agent confirms that a caller has
	 * requested CCSS */
	CC_CALLER_REQUESTED,
	/*! Entered when a CCSS monitor confirms acknowledgment of an
	 * outbound CCSS request */
	CC_ACTIVE,
	/*! Entered when a CCSS monitor alerts the core that the called party
	 * has become available */
	CC_CALLEE_READY,
	/*! Entered when a CCSS agent alerts the core that the calling party
	 * may not be recalled because he is unavailable
	 */
	CC_CALLER_BUSY,
	/*! Entered when a CCSS agent alerts the core that the calling party
	 * is attempting to recall the called party
	 */
	CC_RECALLING,
	/*! Entered when an application alerts the core that the calling party's
	 * recall attempt has had a call progress response indicated
	 */
	CC_COMPLETE,
	/*! Entered any time that something goes wrong during the process, thus
	 * resulting in the failure of the attempted CCSS transaction. Note also
	 * that cancellations of CC are treated as failures.
	 */
	CC_FAILED,
};

/*!
 * \brief The payload for an AST_CONTROL_CC frame
 *
 * \details
 * This contains all the necessary data regarding
 * a called device so that the CC core will be able
 * to allocate the proper monitoring resources.
 */
struct cc_control_payload {
	/*!
	 * \brief The type of monitor to allocate.
	 *
	 * \details
	 * The type of monitor to allocate. This is a string which corresponds
	 * to a set of monitor callbacks registered. Examples include "generic"
	 * and "SIP"
	 *
	 * \note This really should be an array of characters in case this payload
	 * is sent accross an IAX2 link.  However, this would not make too much sense
	 * given this type may not be recognized by the other end.
	 * Protection may be necessary to prevent it from being transmitted.
	 *
	 * In addition the following other problems are also possible:
	 * 1) Endian issues with the integers/enums stored in the config_params.
	 * 2) Alignment padding issues for the element types.
	 */
	const char *monitor_type;
	/*!
	 * \brief Private data allocated by the callee
	 *
	 * \details
	 * All channel drivers that monitor endpoints will need to allocate
	 * data that is not usable by the CC core. In most cases, some or all
	 * of this data is allocated at the time that the channel driver offers
	 * CC to the caller. There are many opportunities for failures to occur
	 * between when a channel driver offers CC and when a monitor is actually
	 * allocated to watch the endpoint. For this reason, the channel driver
	 * must give the core a pointer to the private data that was allocated so
	 * that the core can call back into the channel driver to destroy it if
	 * a failure occurs. If no private data has been allocated at the time that
	 * CC is offered, then it is perfectly acceptable to pass NULL for this
	 * field.
	 */
	void *private_data;
	/*!
	 * \brief Service offered by the endpoint
	 *
	 * \details
	 * This indicates the type of call completion service offered by the
	 * endpoint. This data is not crucial to the machinations of the CC core,
	 * but it is helpful for debugging purposes.
	 */
	enum ast_cc_service_type service;
	/*!
	 * \brief Configuration parameters used by this endpoint
	 *
	 * \details
	 * Each time an endpoint offers call completion, it must provide its call
	 * completion configuration parameters. This is because settings may be different
	 * depending on the circumstances.
	 */
	struct ast_cc_config_params config_params;
	/*!
	 * \brief ID of parent extension
	 *
	 * \details
	 * This is the only datum that the CC core derives on its own and is not
	 * provided by the offerer of CC. This provides the core with information on
	 * which extension monitor is the most immediate parent of this device.
	 */
	int parent_interface_id;
	/*!
	 * \brief Name of device to be monitored
	 *
	 * \details
	 * The device name by which this monitored endpoint will be referred in the
	 * CC core. It is highly recommended that this device name is derived by using
	 * the function ast_channel_get_device_name.
	 */
	char device_name[AST_CHANNEL_NAME];
	/*!
	 * \brief Recall dialstring
	 *
	 * \details
	 * Certain channel drivers (DAHDI in particular) will require that a special
	 * dialstring be used to indicate that the outgoing call is to interpreted as
	 * a CC recall. If the channel driver has such a requirement, then this is
	 * where that special recall dialstring is placed. If no special dialstring
	 * is to be used, then the channel driver must provide the original dialstring
	 * used to call this endpoint.
	 */
	char dialstring[AST_CHANNEL_NAME];
};

/*!
 * \brief The "tree" of interfaces that is dialed.
 *
 * \details
 * Though this is a linked list, it is logically treated
 * as a tree of monitors. Each monitor has an id and a parent_id
 * associated with it. The id is a unique ID for that monitor, and
 * the parent_id is the unique ID of the monitor's parent in the
 * tree. The tree is structured such that all of a parent's children
 * will appear after the parent in the tree. However, it cannot be
 * guaranteed exactly where after the parent the children are.
 *
 * The tree is reference counted since several threads may need
 * to use it, and it may last beyond the lifetime of a single
 * thread.
 */
AST_LIST_HEAD(cc_monitor_tree, ast_cc_monitor);

static const int CC_CORE_INSTANCES_BUCKETS = 17;
static struct ao2_container *cc_core_instances;

struct cc_core_instance {
	/*!
	 * Unique identifier for this instance of the CC core.
	 */
	int core_id;
	/*!
	 * The current state for this instance of the CC core.
	 */
	enum cc_state current_state;
	/*!
	 * The CC agent in use for this call
	 */
	struct ast_cc_agent *agent;
	/*!
	 * Reference to the monitor tree formed during the initial call
	 */
	struct cc_monitor_tree *monitors;
};

/*!
 * \internal
 * \brief Request that the core change states
 * \param state The state to which we wish to change
 * \param core_id The unique identifier for this instance of the CCSS core state machine
 * \param debug Optional message explaining the reason for the state change
 * \param ap varargs list
 * \retval 0 State change successfully queued
 * \retval -1 Unable to queue state change request
 */
static int __attribute__((format(printf, 3, 0))) cc_request_state_change(enum cc_state state, const int core_id, const char *debug, va_list ap);

/*!
 * \internal
 * \brief create a new instance of the CC core and an agent for the calling channel
 *
 * This function will check to make sure that the incoming channel
 * is allowed to request CC by making sure that the incoming channel
 * has not exceeded its maximum number of allowed agents.
 *
 * Should that check pass, the core instance is created, and then the
 * agent for the channel.
 *
 * \param caller_chan The incoming channel for this particular call
 * \param called_tree A reference to the tree of called devices. The agent
 * will gain a reference to this tree as well
 * \param core_id The core_id that this core_instance will assume
 * \retval NULL Failed to create the core instance either due to memory allocation
 * errors or due to the agent count for the caller being too high
 * \retval non-NULL A reference to the newly created cc_core_instance
 */
static struct cc_core_instance *cc_core_init_instance(struct ast_channel *caller_chan,
		struct cc_monitor_tree *called_tree, const int core_id, struct cc_control_payload *cc_data);

static const struct {
	enum ast_cc_service_type service;
	const char *service_string;
} cc_service_to_string_map[] = {
	{AST_CC_NONE, "NONE"},
	{AST_CC_CCBS, "CCBS"},
	{AST_CC_CCNR, "CCNR"},
	{AST_CC_CCNL, "CCNL"},
};

static const struct {
	enum cc_state state;
	const char *state_string;
} cc_state_to_string_map[] = {
	{CC_AVAILABLE,          "CC is available"},
	{CC_CALLER_OFFERED,     "CC offered to caller"},
	{CC_CALLER_REQUESTED,   "CC requested by caller"},
	{CC_ACTIVE,             "CC accepted by callee"},
	{CC_CALLEE_READY,       "Callee has become available"},
	{CC_CALLER_BUSY,        "Callee was ready, but caller is now unavailable"},
	{CC_RECALLING,          "Caller is attempting to recall"},
	{CC_COMPLETE,           "Recall complete"},
	{CC_FAILED,             "CC has failed"},
};

static const char *cc_state_to_string(enum cc_state state)
{
	return cc_state_to_string_map[state].state_string;
}

static const char *cc_service_to_string(enum ast_cc_service_type service)
{
	return cc_service_to_string_map[service].service_string;
}

static int cc_core_instance_hash_fn(const void *obj, const int flags)
{
	const struct cc_core_instance *core_instance = obj;
	return core_instance->core_id;
}

static int cc_core_instance_cmp_fn(void *obj, void *arg, int flags)
{
	struct cc_core_instance *core_instance1 = obj;
	struct cc_core_instance *core_instance2 = arg;

	return core_instance1->core_id == core_instance2->core_id ? CMP_MATCH | CMP_STOP : 0;
}

static struct cc_core_instance *find_cc_core_instance(const int core_id)
{
	struct cc_core_instance finder = {.core_id = core_id,};

	return ao2_t_find(cc_core_instances, &finder, OBJ_POINTER, "Finding a core_instance");
}

struct cc_callback_helper {
	ao2_callback_fn *function;
	void *args;
	const char *type;
};

static int cc_agent_callback_helper(void *obj, void *args, int flags)
{
	struct cc_core_instance *core_instance = obj;
	struct cc_callback_helper *helper = args;

	if (strcmp(core_instance->agent->callbacks->type, helper->type)) {
		return 0;
	}

	return helper->function(core_instance->agent, helper->args, flags);
}

struct ast_cc_agent *ast_cc_agent_callback(int flags, ao2_callback_fn *function, void *args, const char * const type)
{
	struct cc_callback_helper helper = {.function = function, .args = args, .type = type};
	struct cc_core_instance *core_instance;
	if ((core_instance = ao2_t_callback(cc_core_instances, flags, cc_agent_callback_helper, &helper,
					"Calling provided agent callback function"))) {
		struct ast_cc_agent *agent = cc_ref(core_instance->agent, "An outside entity needs the agent");
		cc_unref(core_instance, "agent callback done with the core_instance");
		return agent;
	}
	return NULL;
}

enum match_flags {
	/* Only match agents that have not yet
	 * made a CC request
	 */
	MATCH_NO_REQUEST = (1 << 0),
	/* Only match agents that have made
	 * a CC request
	 */
	MATCH_REQUEST = (1 << 1),
};

/* ao2_callbacks for cc_core_instances */

/*!
 * \internal
 * \brief find a core instance based on its agent
 *
 * The match flags tell whether we wish to find core instances
 * that have a monitor or core instances that do not. Core instances
 * with no monitor are core instances for which a caller has not yet
 * requested CC. Core instances with a monitor are ones for which the
 * caller has requested CC.
 */
static int match_agent(void *obj, void *arg, void *data, int flags)
{
	struct cc_core_instance *core_instance = obj;
	const char *name = arg;
	unsigned long match_flags = *(unsigned long *)data;
	int possible_match = 0;

	if ((match_flags & MATCH_NO_REQUEST) && core_instance->current_state < CC_CALLER_REQUESTED) {
		possible_match = 1;
	}

	if ((match_flags & MATCH_REQUEST) && core_instance->current_state >= CC_CALLER_REQUESTED) {
		possible_match = 1;
	}

	if (!possible_match) {
		return 0;
	}

	if (!strcmp(core_instance->agent->device_name, name)) {
		return CMP_MATCH | CMP_STOP;
	}
	return 0;
}

struct count_agents_cb_data {
	int count;
	int core_id_exception;
};

/*!
 * \internal
 * \brief Count the number of agents a specific interface is using
 *
 * We're only concerned with the number of agents that have requested
 * CC, so we restrict our search to core instances which have a non-NULL
 * monitor pointer
 */
static int count_agents_cb(void *obj, void *arg, void *data, int flags)
{
	struct cc_core_instance *core_instance = obj;
	const char *name = arg;
	struct count_agents_cb_data *cb_data = data;

	if (cb_data->core_id_exception == core_instance->core_id) {
		ast_log_dynamic_level(cc_logger_level, "Found agent with core_id %d but not counting it toward total\n", core_instance->core_id);
		return 0;
	}

	if (core_instance->current_state >= CC_CALLER_REQUESTED && !strcmp(core_instance->agent->device_name, name)) {
		cb_data->count++;
	}
	return 0;
}

/* default values mapping from cc_state to ast_dev_state */

#define CC_AVAILABLE_DEVSTATE_DEFAULT        AST_DEVICE_NOT_INUSE
#define CC_CALLER_OFFERED_DEVSTATE_DEFAULT   AST_DEVICE_NOT_INUSE
#define CC_CALLER_REQUESTED_DEVSTATE_DEFAULT AST_DEVICE_NOT_INUSE
#define CC_ACTIVE_DEVSTATE_DEFAULT           AST_DEVICE_INUSE
#define CC_CALLEE_READY_DEVSTATE_DEFAULT     AST_DEVICE_RINGING
#define CC_CALLER_BUSY_DEVSTATE_DEFAULT      AST_DEVICE_ONHOLD
#define CC_RECALLING_DEVSTATE_DEFAULT        AST_DEVICE_RINGING
#define CC_COMPLETE_DEVSTATE_DEFAULT         AST_DEVICE_NOT_INUSE
#define CC_FAILED_DEVSTATE_DEFAULT           AST_DEVICE_NOT_INUSE

/*!
 * \internal
 * \brief initialization of defaults for CC_STATE to DEVICE_STATE map
 */
static enum ast_device_state cc_state_to_devstate_map[] = {
	[CC_AVAILABLE] =        CC_AVAILABLE_DEVSTATE_DEFAULT,
	[CC_CALLER_OFFERED] =   CC_CALLER_OFFERED_DEVSTATE_DEFAULT,
	[CC_CALLER_REQUESTED] = CC_CALLER_REQUESTED_DEVSTATE_DEFAULT,
	[CC_ACTIVE] =           CC_ACTIVE_DEVSTATE_DEFAULT,
	[CC_CALLEE_READY] =     CC_CALLEE_READY_DEVSTATE_DEFAULT,
	[CC_CALLER_BUSY] =      CC_CALLER_BUSY_DEVSTATE_DEFAULT,
	[CC_RECALLING] =        CC_RECALLING_DEVSTATE_DEFAULT,
	[CC_COMPLETE] =         CC_COMPLETE_DEVSTATE_DEFAULT,
	[CC_FAILED] =           CC_FAILED_DEVSTATE_DEFAULT,
};

/*!
 * \internal
 * \brief lookup the ast_device_state mapped to cc_state
 *
 * \param state
 *
 * \return the correponding DEVICE STATE from the cc_state_to_devstate_map
 * when passed an internal state.
 */
static enum ast_device_state cc_state_to_devstate(enum cc_state state)
{
	return cc_state_to_devstate_map[state];
}

/*!
 * \internal
 * \brief Callback for devicestate providers
 *
 * \details
 * Initialize with ast_devstate_prov_add() and returns the corresponding
 * DEVICE STATE based on the current CC_STATE state machine if the requested
 * device is found and is a generic device. Returns the equivalent of
 * CC_FAILED, which defaults to NOT_INUSE, if no device is found.  NOT_INUSE would
 * indicate that there is no presence of any pending call back.
 */
static enum ast_device_state ccss_device_state(const char *device_name)
{
	struct cc_core_instance *core_instance;
	unsigned long match_flags;
	enum ast_device_state cc_current_state;

	match_flags = MATCH_NO_REQUEST;
	core_instance = ao2_t_callback_data(cc_core_instances, 0, match_agent,
		(char *) device_name, &match_flags,
		"Find Core Instance for ccss_device_state reqeust.");
	if (!core_instance) {
		ast_log_dynamic_level(cc_logger_level,
			"Couldn't find a core instance for caller %s\n", device_name);
		return cc_state_to_devstate(CC_FAILED);
	}

	ast_log_dynamic_level(cc_logger_level,
		"Core %d: Found core_instance for caller %s in state %s\n",
		core_instance->core_id, device_name, cc_state_to_string(core_instance->current_state));

	if (strcmp(core_instance->agent->callbacks->type, "generic")) {
		ast_log_dynamic_level(cc_logger_level,
			"Core %d: Device State is only for generic agent types.\n",
			core_instance->core_id);
		cc_unref(core_instance, "Unref core_instance since ccss_device_state was called with native agent");
		return cc_state_to_devstate(CC_FAILED);
	}
	cc_current_state = cc_state_to_devstate(core_instance->current_state);
	cc_unref(core_instance, "Unref core_instance done with ccss_device_state");
	return cc_current_state;
}

/*!
 * \internal
 * \brief Notify Device State Changes from CC STATE MACHINE
 *
 * \details
 * Any time a state is changed, we call this function to notify the DEVICE STATE
 * subsystem of the change so that subscribed phones to any corresponding hints that
 * are using that state are updated.
 */
static void ccss_notify_device_state_change(const char *device, enum cc_state state)
{
	enum ast_device_state devstate;

	devstate = cc_state_to_devstate(state);

	ast_log_dynamic_level(cc_logger_level,
		"Notification of CCSS state change to '%s', device state '%s' for device '%s'\n",
		cc_state_to_string(state), ast_devstate2str(devstate), device);

	ast_devstate_changed(devstate, AST_DEVSTATE_CACHABLE, "ccss:%s", device);
}

#define CC_OFFER_TIMER_DEFAULT			20		/* Seconds */
#define CCNR_AVAILABLE_TIMER_DEFAULT	7200	/* Seconds */
#define CCBS_AVAILABLE_TIMER_DEFAULT	4800	/* Seconds */
#define CC_RECALL_TIMER_DEFAULT			20		/* Seconds */
#define CC_MAX_AGENTS_DEFAULT			5
#define CC_MAX_MONITORS_DEFAULT			5
#define GLOBAL_CC_MAX_REQUESTS_DEFAULT	20

static const struct ast_cc_config_params cc_default_params = {
	.cc_agent_policy = AST_CC_AGENT_NEVER,
	.cc_monitor_policy = AST_CC_MONITOR_NEVER,
	.cc_offer_timer = CC_OFFER_TIMER_DEFAULT,
	.ccnr_available_timer = CCNR_AVAILABLE_TIMER_DEFAULT,
	.ccbs_available_timer = CCBS_AVAILABLE_TIMER_DEFAULT,
	.cc_recall_timer = CC_RECALL_TIMER_DEFAULT,
	.cc_max_agents = CC_MAX_AGENTS_DEFAULT,
	.cc_max_monitors = CC_MAX_MONITORS_DEFAULT,
	.cc_callback_macro = "",
	.cc_callback_sub = "",
	.cc_agent_dialstring = "",
};

void ast_cc_default_config_params(struct ast_cc_config_params *params)
{
	*params = cc_default_params;
}

struct ast_cc_config_params *__ast_cc_config_params_init(const char *file, int line, const char *function)
{
#if defined(__AST_DEBUG_MALLOC)
	struct ast_cc_config_params *params = __ast_malloc(sizeof(*params), file, line, function);
#else
	struct ast_cc_config_params *params = ast_malloc(sizeof(*params));
#endif

	if (!params) {
		return NULL;
	}

	ast_cc_default_config_params(params);
	return params;
}

void ast_cc_config_params_destroy(struct ast_cc_config_params *params)
{
	ast_free(params);
}

static enum ast_cc_agent_policies str_to_agent_policy(const char * const value)
{
	if (!strcasecmp(value, "never")) {
		return AST_CC_AGENT_NEVER;
	} else if (!strcasecmp(value, "native")) {
		return AST_CC_AGENT_NATIVE;
	} else if (!strcasecmp(value, "generic")) {
		return AST_CC_AGENT_GENERIC;
	} else {
		ast_log(LOG_WARNING, "%s is an invalid value for cc_agent_policy. Switching to 'never'\n", value);
		return AST_CC_AGENT_NEVER;
	}
}

static enum ast_cc_monitor_policies str_to_monitor_policy(const char * const value)
{
	if (!strcasecmp(value, "never")) {
		return AST_CC_MONITOR_NEVER;
	} else if (!strcasecmp(value, "native")) {
		return AST_CC_MONITOR_NATIVE;
	} else if (!strcasecmp(value, "generic")) {
		return AST_CC_MONITOR_GENERIC;
	} else if (!strcasecmp(value, "always")) {
		return AST_CC_MONITOR_ALWAYS;
	} else {
		ast_log(LOG_WARNING, "%s is an invalid value for cc_monitor_policy. Switching to 'never'\n", value);
		return AST_CC_MONITOR_NEVER;
	}
}

static const char *agent_policy_to_str(enum ast_cc_agent_policies policy)
{
	switch (policy) {
	case AST_CC_AGENT_NEVER:
		return "never";
	case AST_CC_AGENT_NATIVE:
		return "native";
	case AST_CC_AGENT_GENERIC:
		return "generic";
	default:
		/* This should never happen... */
		return "";
	}
}

static const char *monitor_policy_to_str(enum ast_cc_monitor_policies policy)
{
	switch (policy) {
	case AST_CC_MONITOR_NEVER:
		return "never";
	case AST_CC_MONITOR_NATIVE:
		return "native";
	case AST_CC_MONITOR_GENERIC:
		return "generic";
	case AST_CC_MONITOR_ALWAYS:
		return "always";
	default:
		/* This should never happen... */
		return "";
	}
}
int ast_cc_get_param(struct ast_cc_config_params *params, const char * const name,
		char *buf, size_t buf_len)
{
	const char *value = NULL;

	if (!strcasecmp(name, "cc_callback_macro")) {
		value = ast_get_cc_callback_macro(params);
	} else if (!strcasecmp(name, "cc_callback_sub")) {
		value = ast_get_cc_callback_sub(params);
	} else if (!strcasecmp(name, "cc_agent_policy")) {
		value = agent_policy_to_str(ast_get_cc_agent_policy(params));
	} else if (!strcasecmp(name, "cc_monitor_policy")) {
		value = monitor_policy_to_str(ast_get_cc_monitor_policy(params));
	} else if (!strcasecmp(name, "cc_agent_dialstring")) {
		value = ast_get_cc_agent_dialstring(params);
	}
	if (value) {
		ast_copy_string(buf, value, buf_len);
		return 0;
	}

	/* The rest of these are all ints of some sort and require some
	 * snprintf-itude
	 */

	if (!strcasecmp(name, "cc_offer_timer")) {
		snprintf(buf, buf_len, "%u", ast_get_cc_offer_timer(params));
	} else if (!strcasecmp(name, "ccnr_available_timer")) {
		snprintf(buf, buf_len, "%u", ast_get_ccnr_available_timer(params));
	} else if (!strcasecmp(name, "ccbs_available_timer")) {
		snprintf(buf, buf_len, "%u", ast_get_ccbs_available_timer(params));
	} else if (!strcasecmp(name, "cc_max_agents")) {
		snprintf(buf, buf_len, "%u", ast_get_cc_max_agents(params));
	} else if (!strcasecmp(name, "cc_max_monitors")) {
		snprintf(buf, buf_len, "%u", ast_get_cc_max_monitors(params));
	} else if (!strcasecmp(name, "cc_recall_timer")) {
		snprintf(buf, buf_len, "%u", ast_get_cc_recall_timer(params));
	} else {
		ast_log(LOG_WARNING, "%s is not a valid CC parameter. Ignoring.\n", name);
		return -1;
	}

	return 0;
}

int ast_cc_set_param(struct ast_cc_config_params *params, const char * const name,
		const char * const value)
{
	unsigned int value_as_uint;
	if (!strcasecmp(name, "cc_agent_policy")) {
		return ast_set_cc_agent_policy(params, str_to_agent_policy(value));
	} else if (!strcasecmp(name, "cc_monitor_policy")) {
		return ast_set_cc_monitor_policy(params, str_to_monitor_policy(value));
	} else if (!strcasecmp(name, "cc_agent_dialstring")) {
		ast_set_cc_agent_dialstring(params, value);
	} else if (!strcasecmp(name, "cc_callback_macro")) {
		ast_set_cc_callback_macro(params, value);
		return 0;
	} else if (!strcasecmp(name, "cc_callback_sub")) {
		ast_set_cc_callback_sub(params, value);
		return 0;
	}

	if (sscanf(value, "%30u", &value_as_uint) != 1) {
		return -1;
	}

	if (!strcasecmp(name, "cc_offer_timer")) {
		ast_set_cc_offer_timer(params, value_as_uint);
	} else if (!strcasecmp(name, "ccnr_available_timer")) {
		ast_set_ccnr_available_timer(params, value_as_uint);
	} else if (!strcasecmp(name, "ccbs_available_timer")) {
		ast_set_ccbs_available_timer(params, value_as_uint);
	} else if (!strcasecmp(name, "cc_max_agents")) {
		ast_set_cc_max_agents(params, value_as_uint);
	} else if (!strcasecmp(name, "cc_max_monitors")) {
		ast_set_cc_max_monitors(params, value_as_uint);
	} else if (!strcasecmp(name, "cc_recall_timer")) {
		ast_set_cc_recall_timer(params, value_as_uint);
	} else {
		ast_log(LOG_WARNING, "%s is not a valid CC parameter. Ignoring.\n", name);
		return -1;
	}

	return 0;
}

int ast_cc_is_config_param(const char * const name)
{
	return (!strcasecmp(name, "cc_agent_policy") ||
				!strcasecmp(name, "cc_monitor_policy") ||
				!strcasecmp(name, "cc_offer_timer") ||
				!strcasecmp(name, "ccnr_available_timer") ||
				!strcasecmp(name, "ccbs_available_timer") ||
				!strcasecmp(name, "cc_max_agents") ||
				!strcasecmp(name, "cc_max_monitors") ||
				!strcasecmp(name, "cc_callback_macro") ||
				!strcasecmp(name, "cc_callback_sub") ||
				!strcasecmp(name, "cc_agent_dialstring") ||
				!strcasecmp(name, "cc_recall_timer"));
}

void ast_cc_copy_config_params(struct ast_cc_config_params *dest, const struct ast_cc_config_params *src)
{
	*dest = *src;
}

enum ast_cc_agent_policies ast_get_cc_agent_policy(struct ast_cc_config_params *config)
{
	return config->cc_agent_policy;
}

int ast_set_cc_agent_policy(struct ast_cc_config_params *config, enum ast_cc_agent_policies value)
{
	/* Screw C and its weak type checking for making me have to do this
	 * validation at runtime.
	 */
	if (value < AST_CC_AGENT_NEVER || value > AST_CC_AGENT_GENERIC) {
		return -1;
	}
	config->cc_agent_policy = value;
	return 0;
}

enum ast_cc_monitor_policies ast_get_cc_monitor_policy(struct ast_cc_config_params *config)
{
	return config->cc_monitor_policy;
}

int ast_set_cc_monitor_policy(struct ast_cc_config_params *config, enum ast_cc_monitor_policies value)
{
	/* Screw C and its weak type checking for making me have to do this
	 * validation at runtime.
	 */
	if (value < AST_CC_MONITOR_NEVER || value > AST_CC_MONITOR_ALWAYS) {
		return -1;
	}
	config->cc_monitor_policy = value;
	return 0;
}

unsigned int ast_get_cc_offer_timer(struct ast_cc_config_params *config)
{
	return config->cc_offer_timer;
}

void ast_set_cc_offer_timer(struct ast_cc_config_params *config, unsigned int value)
{
	/* 0 is an unreasonable value for any timer. Stick with the default */
	if (value == 0) {
		ast_log(LOG_WARNING, "0 is an invalid value for cc_offer_timer. Retaining value as %u\n", config->cc_offer_timer);
		return;
	}
	config->cc_offer_timer = value;
}

unsigned int ast_get_ccnr_available_timer(struct ast_cc_config_params *config)
{
	return config->ccnr_available_timer;
}

void ast_set_ccnr_available_timer(struct ast_cc_config_params *config, unsigned int value)
{
	/* 0 is an unreasonable value for any timer. Stick with the default */
	if (value == 0) {
		ast_log(LOG_WARNING, "0 is an invalid value for ccnr_available_timer. Retaining value as %u\n", config->ccnr_available_timer);
		return;
	}
	config->ccnr_available_timer = value;
}

unsigned int ast_get_cc_recall_timer(struct ast_cc_config_params *config)
{
	return config->cc_recall_timer;
}

void ast_set_cc_recall_timer(struct ast_cc_config_params *config, unsigned int value)
{
	/* 0 is an unreasonable value for any timer. Stick with the default */
	if (value == 0) {
		ast_log(LOG_WARNING, "0 is an invalid value for ccnr_available_timer. Retaining value as %u\n", config->cc_recall_timer);
		return;
	}
	config->cc_recall_timer = value;
}

unsigned int ast_get_ccbs_available_timer(struct ast_cc_config_params *config)
{
	return config->ccbs_available_timer;
}

void ast_set_ccbs_available_timer(struct ast_cc_config_params *config, unsigned int value)
{
	/* 0 is an unreasonable value for any timer. Stick with the default */
	if (value == 0) {
		ast_log(LOG_WARNING, "0 is an invalid value for ccbs_available_timer. Retaining value as %u\n", config->ccbs_available_timer);
		return;
	}
	config->ccbs_available_timer = value;
}

const char *ast_get_cc_agent_dialstring(struct ast_cc_config_params *config)
{
	return config->cc_agent_dialstring;
}

void ast_set_cc_agent_dialstring(struct ast_cc_config_params *config, const char *const value)
{
	if (ast_strlen_zero(value)) {
		config->cc_agent_dialstring[0] = '\0';
	} else {
		ast_copy_string(config->cc_agent_dialstring, value, sizeof(config->cc_agent_dialstring));
	}
}

unsigned int ast_get_cc_max_agents(struct ast_cc_config_params *config)
{
	return config->cc_max_agents;
}

void ast_set_cc_max_agents(struct ast_cc_config_params *config, unsigned int value)
{
	config->cc_max_agents = value;
}

unsigned int ast_get_cc_max_monitors(struct ast_cc_config_params *config)
{
	return config->cc_max_monitors;
}

void ast_set_cc_max_monitors(struct ast_cc_config_params *config, unsigned int value)
{
	config->cc_max_monitors = value;
}

const char *ast_get_cc_callback_macro(struct ast_cc_config_params *config)
{
	return config->cc_callback_macro;
}

const char *ast_get_cc_callback_sub(struct ast_cc_config_params *config)
{
	return config->cc_callback_sub;
}

void ast_set_cc_callback_macro(struct ast_cc_config_params *config, const char * const value)
{
	ast_log(LOG_WARNING, "Usage of cc_callback_macro is deprecated.  Please use cc_callback_sub instead.\n");
	if (ast_strlen_zero(value)) {
		config->cc_callback_macro[0] = '\0';
	} else {
		ast_copy_string(config->cc_callback_macro, value, sizeof(config->cc_callback_macro));
	}
}

void ast_set_cc_callback_sub(struct ast_cc_config_params *config, const char * const value)
{
	if (ast_strlen_zero(value)) {
		config->cc_callback_sub[0] = '\0';
	} else {
		ast_copy_string(config->cc_callback_sub, value, sizeof(config->cc_callback_sub));
	}
}

static int cc_publish(struct stasis_message_type *message_type, int core_id, struct ast_json *extras)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	RAII_VAR(struct ast_json_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	if (!message_type) {
		return -1;
	}

	blob = ast_json_pack("{s: i}",
		"core_id", core_id);
	if (!blob) {
		return -1;
	}

	if (extras) {
		ast_json_object_update(blob, extras);
	}

	if (!(payload = ast_json_payload_create(blob))) {
		return -1;
	}

	if (!(message = stasis_message_create(message_type, payload))) {
		return -1;
	}

	stasis_publish(ast_system_topic(), message);

	return 0;
}

static void cc_publish_available(int core_id, const char *callee, const char *service)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s, s: s}",
		"callee", callee,
		"service", service);

	cc_publish(ast_cc_available_type(), core_id, extras);
}

static void cc_publish_offertimerstart(int core_id, const char *caller, unsigned int expires)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s, s: i}",
		"caller", caller,
		"expires", expires);

	cc_publish(ast_cc_offertimerstart_type(), core_id, extras);
}

static void cc_publish_requested(int core_id, const char *caller, const char *callee)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s, s: s}",
		"caller", caller,
		"callee", callee);

	cc_publish(ast_cc_requested_type(), core_id, extras);
}

static void cc_publish_requestacknowledged(int core_id, const char *caller)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s}",
		"caller", caller);

	cc_publish(ast_cc_requestacknowledged_type(), core_id, extras);
}

static void cc_publish_callerstopmonitoring(int core_id, const char *caller)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s}",
		"caller", caller);

	cc_publish(ast_cc_callerstopmonitoring_type(), core_id, extras);
}

static void cc_publish_callerstartmonitoring(int core_id, const char *caller)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s}",
		"caller", caller);

	cc_publish(ast_cc_callerstartmonitoring_type(), core_id, extras);
}

static void cc_publish_callerrecalling(int core_id, const char *caller)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s}",
		"caller", caller);

	cc_publish(ast_cc_callerrecalling_type(), core_id, extras);
}

static void cc_publish_recallcomplete(int core_id, const char *caller)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s}",
		"caller", caller);

	cc_publish(ast_cc_recallcomplete_type(), core_id, extras);
}

static void cc_publish_failure(int core_id, const char *caller, const char *reason)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s, s: s}",
		"caller", caller,
		"reason", reason);

	cc_publish(ast_cc_failure_type(), core_id, extras);
}

static void cc_publish_monitorfailed(int core_id, const char *callee)
{
	RAII_VAR(struct ast_json *, extras, NULL, ast_json_unref);

	extras = ast_json_pack("{s: s}",
		"callee", callee);

	cc_publish(ast_cc_monitorfailed_type(), core_id, extras);
}

struct cc_monitor_backend {
	AST_LIST_ENTRY(cc_monitor_backend) next;
	const struct ast_cc_monitor_callbacks *callbacks;
};

AST_RWLIST_HEAD_STATIC(cc_monitor_backends, cc_monitor_backend);

int ast_cc_monitor_register(const struct ast_cc_monitor_callbacks *callbacks)
{
	struct cc_monitor_backend *backend = ast_calloc(1, sizeof(*backend));

	if (!backend) {
		return -1;
	}

	backend->callbacks = callbacks;

	AST_RWLIST_WRLOCK(&cc_monitor_backends);
	AST_RWLIST_INSERT_TAIL(&cc_monitor_backends, backend, next);
	AST_RWLIST_UNLOCK(&cc_monitor_backends);
	return 0;
}

static const struct ast_cc_monitor_callbacks *find_monitor_callbacks(const char * const type)
{
	struct cc_monitor_backend *backend;
	const struct ast_cc_monitor_callbacks *callbacks = NULL;

	AST_RWLIST_RDLOCK(&cc_monitor_backends);
	AST_RWLIST_TRAVERSE(&cc_monitor_backends, backend, next) {
		if (!strcmp(backend->callbacks->type, type)) {
			ast_log_dynamic_level(cc_logger_level, "Returning monitor backend %s\n", backend->callbacks->type);
			callbacks = backend->callbacks;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&cc_monitor_backends);
	return callbacks;
}

void ast_cc_monitor_unregister(const struct ast_cc_monitor_callbacks *callbacks)
{
	struct cc_monitor_backend *backend;
	AST_RWLIST_WRLOCK(&cc_monitor_backends);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&cc_monitor_backends, backend, next) {
		if (backend->callbacks == callbacks) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_free(backend);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&cc_monitor_backends);
}

struct cc_agent_backend {
	AST_LIST_ENTRY(cc_agent_backend) next;
	const struct ast_cc_agent_callbacks *callbacks;
};

AST_RWLIST_HEAD_STATIC(cc_agent_backends, cc_agent_backend);

int ast_cc_agent_register(const struct ast_cc_agent_callbacks *callbacks)
{
	struct cc_agent_backend *backend = ast_calloc(1, sizeof(*backend));

	if (!backend) {
		return -1;
	}

	backend->callbacks = callbacks;
	AST_RWLIST_WRLOCK(&cc_agent_backends);
	AST_RWLIST_INSERT_TAIL(&cc_agent_backends, backend, next);
	AST_RWLIST_UNLOCK(&cc_agent_backends);
	return 0;
}

void ast_cc_agent_unregister(const struct ast_cc_agent_callbacks *callbacks)
{
	struct cc_agent_backend *backend;
	AST_RWLIST_WRLOCK(&cc_agent_backends);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&cc_agent_backends, backend, next) {
		if (backend->callbacks == callbacks) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_free(backend);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&cc_agent_backends);
}

static const struct ast_cc_agent_callbacks *find_agent_callbacks(struct ast_channel *chan)
{
	struct cc_agent_backend *backend;
	const struct ast_cc_agent_callbacks *callbacks = NULL;
	struct ast_cc_config_params *cc_params;
	char type[32];

	cc_params = ast_channel_get_cc_config_params(chan);
	if (!cc_params) {
		return NULL;
	}
	switch (ast_get_cc_agent_policy(cc_params)) {
	case AST_CC_AGENT_GENERIC:
		ast_copy_string(type, "generic", sizeof(type));
		break;
	case AST_CC_AGENT_NATIVE:
		ast_channel_get_cc_agent_type(chan, type, sizeof(type));
		break;
	default:
		ast_log_dynamic_level(cc_logger_level, "Not returning agent callbacks since this channel is configured not to have a CC agent\n");
		return NULL;
	}

	AST_RWLIST_RDLOCK(&cc_agent_backends);
	AST_RWLIST_TRAVERSE(&cc_agent_backends, backend, next) {
		if (!strcmp(backend->callbacks->type, type)) {
			ast_log_dynamic_level(cc_logger_level, "Returning agent backend %s\n", backend->callbacks->type);
			callbacks = backend->callbacks;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&cc_agent_backends);
	return callbacks;
}

/*!
 * \internal
 * \brief Determine if the given device state is considered available by generic CCSS.
 * \since 1.8
 *
 * \param state Device state to test.
 *
 * \return TRUE if the given device state is considered available by generic CCSS.
 */
static int cc_generic_is_device_available(enum ast_device_state state)
{
	return state == AST_DEVICE_NOT_INUSE || state == AST_DEVICE_UNKNOWN;
}

static int cc_generic_monitor_request_cc(struct ast_cc_monitor *monitor, int *available_timer_id);
static int cc_generic_monitor_suspend(struct ast_cc_monitor *monitor);
static int cc_generic_monitor_unsuspend(struct ast_cc_monitor *monitor);
static int cc_generic_monitor_cancel_available_timer(struct ast_cc_monitor *monitor, int *sched_id);
static void cc_generic_monitor_destructor(void *private_data);

static struct ast_cc_monitor_callbacks generic_monitor_cbs = {
	.type = "generic",
	.request_cc = cc_generic_monitor_request_cc,
	.suspend = cc_generic_monitor_suspend,
	.unsuspend = cc_generic_monitor_unsuspend,
	.cancel_available_timer = cc_generic_monitor_cancel_available_timer,
	.destructor = cc_generic_monitor_destructor,
};

struct ao2_container *generic_monitors;

struct generic_monitor_instance {
	int core_id;
	int is_suspended;
	int monitoring;
	AST_LIST_ENTRY(generic_monitor_instance) next;
};

struct generic_monitor_instance_list {
	const char *device_name;
	enum ast_device_state current_state;
	/* If there are multiple instances monitoring the
	 * same device and one should fail, we need to know
	 * whether to signal that the device can be recalled.
	 * The problem is that the device state is not enough
	 * to check. If a caller has requested CCNR, then the
	 * fact that the device is available does not indicate
	 * that the device is ready to be recalled. Instead, as
	 * soon as one instance of the monitor becomes available
	 * for a recall, we mark the entire list as being fit
	 * for recall. If a CCNR request comes in, then we will
	 * have to mark the list as unfit for recall since this
	 * is a clear indicator that the person at the monitored
	 * device has gone away and is actuall not fit to be
	 * recalled
	 */
	int fit_for_recall;
	struct stasis_subscription *sub;
	AST_LIST_HEAD_NOLOCK(, generic_monitor_instance) list;
};

/*!
 * \brief private data for generic device monitor
 */
struct generic_monitor_pvt {
	/*!
	 * We need the device name during destruction so we
	 * can find the appropriate item to destroy.
	 */
	const char *device_name;
	/*!
	 * We need the core ID for similar reasons. Once we
	 * find the appropriate item in our ao2_container, we
	 * need to remove the appropriate cc_monitor from the
	 * list of monitors.
	 */
	int core_id;
};

static int generic_monitor_hash_fn(const void *obj, const int flags)
{
	const struct generic_monitor_instance_list *generic_list = obj;
	return ast_str_hash(generic_list->device_name);
}

static int generic_monitor_cmp_fn(void *obj, void *arg, int flags)
{
	const struct generic_monitor_instance_list *generic_list1 = obj;
	const struct generic_monitor_instance_list *generic_list2 = arg;

	return !strcmp(generic_list1->device_name, generic_list2->device_name) ? CMP_MATCH | CMP_STOP : 0;
}

static struct generic_monitor_instance_list *find_generic_monitor_instance_list(const char * const device_name)
{
	struct generic_monitor_instance_list finder = {0};
	char *uppertech = ast_strdupa(device_name);
	ast_tech_to_upper(uppertech);
	finder.device_name = uppertech;

	return ao2_t_find(generic_monitors, &finder, OBJ_POINTER, "Finding generic monitor instance list");
}

static void generic_monitor_instance_list_destructor(void *obj)
{
	struct generic_monitor_instance_list *generic_list = obj;
	struct generic_monitor_instance *generic_instance;

	generic_list->sub = stasis_unsubscribe(generic_list->sub);
	while ((generic_instance = AST_LIST_REMOVE_HEAD(&generic_list->list, next))) {
		ast_free(generic_instance);
	}
	ast_free((char *)generic_list->device_name);
}

static void generic_monitor_devstate_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *msg);
static struct generic_monitor_instance_list *create_new_generic_list(struct ast_cc_monitor *monitor)
{
	struct generic_monitor_instance_list *generic_list = ao2_t_alloc(sizeof(*generic_list),
			generic_monitor_instance_list_destructor, "allocate generic monitor instance list");
	char * device_name;
	struct stasis_topic *device_specific_topic;

	if (!generic_list) {
		return NULL;
	}

	if (!(device_name = ast_strdup(monitor->interface->device_name))) {
		cc_unref(generic_list, "Failed to strdup the monitor's device name");
		return NULL;
	}
	ast_tech_to_upper(device_name);
	generic_list->device_name = device_name;

	device_specific_topic = ast_device_state_topic(device_name);
	if (!device_specific_topic) {
		return NULL;
	}

	if (!(generic_list->sub = stasis_subscribe(device_specific_topic, generic_monitor_devstate_cb, NULL))) {
		cc_unref(generic_list, "Failed to subscribe to device state");
		return NULL;
	}
	generic_list->current_state = ast_device_state(monitor->interface->device_name);
	ao2_t_link(generic_monitors, generic_list, "linking new generic monitor instance list");
	return generic_list;
}

static int generic_monitor_devstate_tp_cb(void *data)
{
	RAII_VAR(struct ast_device_state_message *, dev_state, data, ao2_cleanup);
	enum ast_device_state new_state = dev_state->state;
	enum ast_device_state previous_state;
	struct generic_monitor_instance_list *generic_list;
	struct generic_monitor_instance *generic_instance;

	if (!(generic_list = find_generic_monitor_instance_list(dev_state->device))) {
		/* The most likely cause for this is that we destroyed the monitor in the
		 * time between subscribing to its device state and the time this executes.
		 * Not really a big deal.
		 */
		return 0;
	}

	if (generic_list->current_state == new_state) {
		/* The device state hasn't actually changed, so we don't really care */
		cc_unref(generic_list, "Kill reference of generic list in devstate taskprocessor callback");
		return 0;
	}

	previous_state = generic_list->current_state;
	generic_list->current_state = new_state;

	if (cc_generic_is_device_available(new_state) &&
			(previous_state == AST_DEVICE_INUSE || previous_state == AST_DEVICE_UNAVAILABLE ||
			 previous_state == AST_DEVICE_BUSY)) {
		AST_LIST_TRAVERSE(&generic_list->list, generic_instance, next) {
			if (!generic_instance->is_suspended && generic_instance->monitoring) {
				generic_instance->monitoring = 0;
				generic_list->fit_for_recall = 1;
				ast_cc_monitor_callee_available(generic_instance->core_id, "Generic monitored party has become available");
				break;
			}
		}
	}
	cc_unref(generic_list, "Kill reference of generic list in devstate taskprocessor callback");
	return 0;
}

static void generic_monitor_devstate_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *msg)
{
	/* Wow, it's cool that we've picked up on a state change, but we really want
	 * the actual work to be done in the core's taskprocessor execution thread
	 * so that all monitor operations can be serialized. Locks?! We don't need
	 * no steenkin' locks!
	 */
	struct ast_device_state_message *dev_state;
	if (ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	dev_state = stasis_message_data(msg);
	if (dev_state->eid) {
		/* ignore non-aggregate states */
		return;
	}

	ao2_t_ref(dev_state, +1, "Bumping dev_state ref for cc_core_taskprocessor");
	if (ast_taskprocessor_push(cc_core_taskprocessor, generic_monitor_devstate_tp_cb, dev_state)) {
		ao2_cleanup(dev_state);
		return;
	}
}

int ast_cc_available_timer_expire(const void *data)
{
	struct ast_cc_monitor *monitor = (struct ast_cc_monitor *) data;
	int res;
	monitor->available_timer_id = -1;
	res = ast_cc_monitor_failed(monitor->core_id, monitor->interface->device_name, "Available timer expired for monitor");
	cc_unref(monitor, "Unref reference from scheduler\n");
	return res;
}

static int cc_generic_monitor_request_cc(struct ast_cc_monitor *monitor, int *available_timer_id)
{
	struct generic_monitor_instance_list *generic_list;
	struct generic_monitor_instance *generic_instance;
	struct generic_monitor_pvt *gen_mon_pvt;
	enum ast_cc_service_type service = monitor->service_offered;
	int when;

	/* First things first. Native channel drivers will have their private data allocated
	 * at the time that they tell the core that they can offer CC. Generic is quite a bit
	 * different, and we wait until this point to allocate our private data.
	 */
	if (!(gen_mon_pvt = ast_calloc(1, sizeof(*gen_mon_pvt)))) {
		return -1;
	}

	if (!(gen_mon_pvt->device_name = ast_strdup(monitor->interface->device_name))) {
		ast_free(gen_mon_pvt);
		return -1;
	}

	gen_mon_pvt->core_id = monitor->core_id;

	monitor->private_data = gen_mon_pvt;

	if (!(generic_list = find_generic_monitor_instance_list(monitor->interface->device_name))) {
		if (!(generic_list = create_new_generic_list(monitor))) {
			return -1;
		}
	}

	if (!(generic_instance = ast_calloc(1, sizeof(*generic_instance)))) {
		/* The generic monitor destructor will take care of the appropriate
		 * deallocations
		 */
		cc_unref(generic_list, "Generic monitor instance failed to allocate");
		return -1;
	}
	generic_instance->core_id = monitor->core_id;
	generic_instance->monitoring = 1;
	AST_LIST_INSERT_TAIL(&generic_list->list, generic_instance, next);
	when = service == AST_CC_CCBS ? ast_get_ccbs_available_timer(monitor->interface->config_params) :
		ast_get_ccnr_available_timer(monitor->interface->config_params);

	*available_timer_id = ast_sched_add(cc_sched_context, when * 1000,
			ast_cc_available_timer_expire, cc_ref(monitor, "Give the scheduler a monitor reference"));
	if (*available_timer_id == -1) {
		cc_unref(monitor, "Failed to schedule available timer. (monitor)");
		cc_unref(generic_list, "Failed to schedule available timer. (generic_list)");
		return -1;
	}
	/* If the new instance was created as CCNR, then that means this device is not currently
	 * fit for recall even if it previously was.
	 */
	if (service == AST_CC_CCNR || service == AST_CC_CCNL) {
		generic_list->fit_for_recall = 0;
	}
	ast_cc_monitor_request_acked(monitor->core_id, "Generic monitor for %s subscribed to device state.",
			monitor->interface->device_name);
	cc_unref(generic_list, "Finished with monitor instance reference in request cc callback");
	return 0;
}

static int cc_generic_monitor_suspend(struct ast_cc_monitor *monitor)
{
	struct generic_monitor_instance_list *generic_list;
	struct generic_monitor_instance *generic_instance;
	enum ast_device_state state = ast_device_state(monitor->interface->device_name);

	if (!(generic_list = find_generic_monitor_instance_list(monitor->interface->device_name))) {
		return -1;
	}

	/* First we need to mark this particular monitor as being suspended. */
	AST_LIST_TRAVERSE(&generic_list->list, generic_instance, next) {
		if (generic_instance->core_id == monitor->core_id) {
			generic_instance->is_suspended = 1;
			break;
		}
	}

	/* If the device being suspended is currently in use, then we don't need to
	 * take any further actions
	 */
	if (!cc_generic_is_device_available(state)) {
		cc_unref(generic_list, "Device is in use. Nothing to do. Unref generic list.");
		return 0;
	}

	/* If the device is not in use, though, then it may be possible to report the
	 * device's availability using a different monitor which is monitoring the
	 * same device
	 */

	AST_LIST_TRAVERSE(&generic_list->list, generic_instance, next) {
		if (!generic_instance->is_suspended) {
			ast_cc_monitor_callee_available(generic_instance->core_id, "Generic monitored party has become available");
			break;
		}
	}
	cc_unref(generic_list, "Done with generic list in suspend callback");
	return 0;
}

static int cc_generic_monitor_unsuspend(struct ast_cc_monitor *monitor)
{
	struct generic_monitor_instance *generic_instance;
	struct generic_monitor_instance_list *generic_list = find_generic_monitor_instance_list(monitor->interface->device_name);
	enum ast_device_state state = ast_device_state(monitor->interface->device_name);

	if (!generic_list) {
		return -1;
	}
	/* If the device is currently available, we can immediately announce
	 * its availability
	 */
	if (cc_generic_is_device_available(state)) {
		ast_cc_monitor_callee_available(monitor->core_id, "Generic monitored party has become available");
	}

	/* In addition, we need to mark this generic_monitor_instance as not being suspended anymore */
	AST_LIST_TRAVERSE(&generic_list->list, generic_instance, next) {
		if (generic_instance->core_id == monitor->core_id) {
			generic_instance->is_suspended = 0;
			generic_instance->monitoring = 1;
			break;
		}
	}
	cc_unref(generic_list, "Done with generic list in cc_generic_monitor_unsuspend");
	return 0;
}

static int cc_generic_monitor_cancel_available_timer(struct ast_cc_monitor *monitor, int *sched_id)
{
	ast_assert(sched_id != NULL);

	if (*sched_id == -1) {
		return 0;
	}

	ast_log_dynamic_level(cc_logger_level, "Core %d: Canceling generic monitor available timer for monitor %s\n",
			monitor->core_id, monitor->interface->device_name);
	if (!ast_sched_del(cc_sched_context, *sched_id)) {
		cc_unref(monitor, "Remove scheduler's reference to the monitor");
	}
	*sched_id = -1;
	return 0;
}

static void cc_generic_monitor_destructor(void *private_data)
{
	struct generic_monitor_pvt *gen_mon_pvt = private_data;
	struct generic_monitor_instance_list *generic_list;
	struct generic_monitor_instance *generic_instance;

	if (!private_data) {
		/* If the private data is NULL, that means that the monitor hasn't even
		 * been created yet, but that the destructor was called. While this sort
		 * of behavior is useful for native monitors, with a generic one, there is
		 * nothing in particular to do.
		 */
		return;
	}

	ast_log_dynamic_level(cc_logger_level, "Core %d: Destroying generic monitor %s\n",
			gen_mon_pvt->core_id, gen_mon_pvt->device_name);

	if (!(generic_list = find_generic_monitor_instance_list(gen_mon_pvt->device_name))) {
		/* If there's no generic list, that means that the monitor is being destroyed
		 * before we actually got to request CC. Not a biggie. Same in the situation
		 * below if the list traversal should complete without finding an entry.
		 */
		ast_free((char *)gen_mon_pvt->device_name);
		ast_free(gen_mon_pvt);
		return;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(&generic_list->list, generic_instance, next) {
		if (generic_instance->core_id == gen_mon_pvt->core_id) {
			AST_LIST_REMOVE_CURRENT(next);
			ast_free(generic_instance);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (AST_LIST_EMPTY(&generic_list->list)) {
		/* No more monitors with this device name exist. Time to unlink this
		 * list from the container
		 */
		ao2_t_unlink(generic_monitors, generic_list, "Generic list is empty. Unlink it from the container");
	} else {
		/* There are still instances for this particular device. The situation
		 * may be that we were attempting a CC recall and a failure occurred, perhaps
		 * on the agent side. If a failure happens here and the device being monitored
		 * is available, then we need to signal on the first unsuspended instance that
		 * the device is available for recall.
		 */

		/* First things first. We don't even want to consider this action if
		 * the device in question isn't available right now.
		 */
		if (generic_list->fit_for_recall
			&& cc_generic_is_device_available(generic_list->current_state)) {
			AST_LIST_TRAVERSE(&generic_list->list, generic_instance, next) {
				if (!generic_instance->is_suspended && generic_instance->monitoring) {
					ast_cc_monitor_callee_available(generic_instance->core_id, "Signaling generic monitor "
							"availability due to other instance's failure.");
					break;
				}
			}
		}
	}
	cc_unref(generic_list, "Done with generic list in generic monitor destructor");
	ast_free((char *)gen_mon_pvt->device_name);
	ast_free(gen_mon_pvt);
}

static void cc_interface_destroy(void *data)
{
	struct ast_cc_interface *interface = data;
	ast_log_dynamic_level(cc_logger_level, "Destroying cc interface %s\n", interface->device_name);
	ast_cc_config_params_destroy(interface->config_params);
}

/*!
 * \brief Data regarding an extension monitor's child's dialstrings
 *
 * \details
 * In developing CCSS, we had most aspects of its operation finished,
 * but there was one looming problem that we had failed to get right.
 * In our design document, we stated that when a CC recall occurs, all
 * endpoints that had been dialed originally would be called back.
 * Unfortunately, our implementation only allowed for devices which had
 * active monitors to inhabit the CC_INTERFACES channel variable, thus
 * making the automated recall only call monitored devices.
 *
 * Devices that were not CC-capable, or devices which failed CC at some
 * point during the process would not make it into the CC_INTERFACES
 * channel variable. This struct is meant as a remedy for the problem.
 */
struct extension_child_dialstring {
	/*!
	 * \brief the original dialstring used to call a particular device
	 *
	 * \details
	 * When someone dials a particular endpoint, the dialstring used in
	 * the dialplan is copied into this buffer. What's important here is
	 * that this is the ORIGINAL dialstring, not the dialstring saved on
	 * a device monitor. The dialstring on a device monitor is what should
	 * be used when recalling that device. The two dialstrings may not be
	 * the same.
	 *
	 * By keeping a copy of the original dialstring used, we can fall back
	 * to using it if the device either does not ever offer CC or if the
	 * device at some point fails for some reason, such as a timer expiration.
	 */
	char original_dialstring[AST_CHANNEL_NAME];
	/*!
	 * \brief The name of the device being dialed
	 *
	 * \details
	 * This serves mainly as a key when searching for a particular dialstring.
	 * For instance, let's say that we have called device SIP/400\@somepeer. This
	 * device offers call completion, but then due to some unforeseen circumstance,
	 * this device backs out and makes CC unavailable. When that happens, we need
	 * to find the dialstring that corresponds to that device, and we use the
	 * stored device name as a way to find it.
	 *
	 * \note There is one particular case where the device name stored here
	 * will be empty. This is the case where we fail to request a channel, but we
	 * still can make use of generic call completion. In such a case, since we never
	 * were able to request the channel, we can't find what its device name is. In
	 * this case, however, it is not important because the dialstring is guaranteed
	 * to be the same both here and in the device monitor.
	 */
	char device_name[AST_CHANNEL_NAME];
	/*!
	 * \brief Is this structure valid for use in CC_INTERFACES?
	 *
	 * \details
	 * When this structure is first created, all information stored here is planned
	 * to be used, so we set the is_valid flag. However, if a device offers call
	 * completion, it will potentially have its own dialstring to use for the recall,
	 * so we find this structure and clear the is_valid flag. By clearing the is_valid
	 * flag, we won't try to populate the CC_INTERFACES variable with the dialstring
	 * stored in this struct. Now, if later, the device which had offered CC should fail,
	 * perhaps due to a timer expiration, then we need to re-set the is_valid flag. This
	 * way, we still will end up placing a call to the device again, and the dialstring
	 * used will be the same as was originally used.
	 */
	int is_valid;
	AST_LIST_ENTRY(extension_child_dialstring) next;
};

/*!
 * \brief Private data for an extension monitor
 */
struct extension_monitor_pvt {
	AST_LIST_HEAD_NOLOCK(, extension_child_dialstring) child_dialstrings;
};

static void cc_extension_monitor_destructor(void *private_data)
{
	struct extension_monitor_pvt *extension_pvt = private_data;
	struct extension_child_dialstring *child_dialstring;

	/* This shouldn't be possible, but I'm paranoid */
	if (!extension_pvt) {
		return;
	}

	while ((child_dialstring = AST_LIST_REMOVE_HEAD(&extension_pvt->child_dialstrings, next))) {
		ast_free(child_dialstring);
	}
	ast_free(extension_pvt);
}

static void cc_monitor_destroy(void *data)
{
	struct ast_cc_monitor *monitor = data;
	/* During the monitor creation process, it is possible for this
	 * function to be called prior to when callbacks are assigned
	 * to the monitor. Also, extension monitors do not have callbacks
	 * assigned to them, so we wouldn't want to segfault when we try
	 * to destroy one of them.
	 */
	ast_log_dynamic_level(cc_logger_level, "Core %d: Calling destructor for monitor %s\n",
			monitor->core_id, monitor->interface->device_name);
	if (monitor->interface->monitor_class == AST_CC_EXTENSION_MONITOR) {
		cc_extension_monitor_destructor(monitor->private_data);
	}
	if (monitor->callbacks) {
		monitor->callbacks->destructor(monitor->private_data);
	}
	cc_unref(monitor->interface, "Unreffing tree's reference to interface");
	ast_free(monitor->dialstring);
}

static void cc_interface_tree_destroy(void *data)
{
	struct cc_monitor_tree *cc_interface_tree = data;
	struct ast_cc_monitor *monitor;
	while ((monitor = AST_LIST_REMOVE_HEAD(cc_interface_tree, next))) {
		if (monitor->callbacks) {
			monitor->callbacks->cancel_available_timer(monitor, &monitor->available_timer_id);
		}
		cc_unref(monitor, "Destroying all monitors");
	}
	AST_LIST_HEAD_DESTROY(cc_interface_tree);
}

/*!
 * This counter is used for assigning unique ids
 * to CC-enabled dialed interfaces.
 */
static int dialed_cc_interface_counter;

/*!
 * \internal
 * \brief data stored in CC datastore
 *
 * The datastore creates a list of interfaces that were
 * dialed, including both extensions and devices. In addition
 * to the intrinsic data of the tree, some extra information
 * is needed for use by app_dial.
 */
struct dialed_cc_interfaces {
	/*!
	 * This value serves a dual-purpose. When dial starts, if the
	 * dialed_cc_interfaces datastore currently exists on the calling
	 * channel, then the dial_parent_id will serve as a means of
	 * letting the new extension cc_monitor we create know
	 * who his parent is. This value will be the extension
	 * cc_monitor that dialed the local channel that resulted
	 * in the new Dial app being called.
	 *
	 * In addition, once an extension cc_monitor is created,
	 * the dial_parent_id will be changed to the id of that newly
	 * created interface. This way, device interfaces created from
	 * receiving AST_CONTROL_CC frames can use this field to determine
	 * who their parent extension interface should be.
	 */
	unsigned int dial_parent_id;
	/*!
	 * Identifier for the potential CC request that may be made
	 * based on this call. Even though an instance of the core may
	 * not be made (since the caller may not request CC), we allocate
	 * a new core_id at the beginning of the call so that recipient
	 * channel drivers can have the information handy just in case
	 * the caller does end up requesting CC.
	 */
	int core_id;
	/*!
	 * When a new Dial application is started, and the datastore
	 * already exists on the channel, we can determine if we
	 * should be adding any new interface information to tree.
	 */
	char ignore;
	/*!
	 * When it comes time to offer CC to the caller, we only want to offer
	 * it to the original incoming channel. For nested Dials and outbound
	 * channels, it is incorrect to attempt such a thing. This flag indicates
	 * if the channel to which this datastore is attached may be legally
	 * offered CC when the call is finished.
	 */
	char is_original_caller;
	/*!
	 * Reference-counted "tree" of interfaces.
	 */
	struct cc_monitor_tree *interface_tree;
};

/*!
 * \internal
 * \brief Destructor function for cc_interfaces datastore
 *
 * This function will free the actual datastore and drop
 * the refcount for the monitor tree by one. In cases
 * where CC can actually be used, this unref will not
 * result in the destruction of the monitor tree, because
 * the CC core will still have a reference.
 *
 * \param data The dialed_cc_interfaces struct to destroy
 */
static void dialed_cc_interfaces_destroy(void *data)
{
	struct dialed_cc_interfaces *cc_interfaces = data;
	cc_unref(cc_interfaces->interface_tree, "Unref dial's ref to monitor tree");
	ast_free(cc_interfaces);
}

/*!
 * \internal
 * \brief Duplicate callback for cc_interfaces datastore
 *
 * Integers are copied by value, but the monitor tree
 * is done via a shallow copy and a bump of the refcount.
 * This way, sub-Dials will be appending interfaces onto
 * the same list as this call to Dial.
 *
 * \param data The old dialed_cc_interfaces we want to copy
 * \retval NULL Could not allocate memory for new dialed_cc_interfaces
 * \retval non-NULL The new copy of the dialed_cc_interfaces
 */
static void *dialed_cc_interfaces_duplicate(void *data)
{
	struct dialed_cc_interfaces *old_cc_interfaces = data;
	struct dialed_cc_interfaces *new_cc_interfaces = ast_calloc(1, sizeof(*new_cc_interfaces));
	if (!new_cc_interfaces) {
		return NULL;
	}
	new_cc_interfaces->ignore = old_cc_interfaces->ignore;
	new_cc_interfaces->dial_parent_id = old_cc_interfaces->dial_parent_id;
	new_cc_interfaces->is_original_caller = 0;
	cc_ref(old_cc_interfaces->interface_tree, "New ref due to duplication of monitor tree");
	new_cc_interfaces->core_id = old_cc_interfaces->core_id;
	new_cc_interfaces->interface_tree = old_cc_interfaces->interface_tree;
	return new_cc_interfaces;
}

/*!
 * \internal
 * \brief information regarding the dialed_cc_interfaces datastore
 *
 * The dialed_cc_interfaces datastore is responsible for keeping track
 * of what CC-enabled interfaces have been dialed by the caller. For
 * more information regarding the actual structure of the tree, see
 * the documentation provided in include/asterisk/ccss.h
 */
static const struct ast_datastore_info dialed_cc_interfaces_info = {
	.type = "Dial CC Interfaces",
	.duplicate = dialed_cc_interfaces_duplicate,
	.destroy = dialed_cc_interfaces_destroy,
};

static struct extension_monitor_pvt *extension_monitor_pvt_init(void)
{
	struct extension_monitor_pvt *ext_pvt = ast_calloc(1, sizeof(*ext_pvt));
	if (!ext_pvt) {
		return NULL;
	}
	AST_LIST_HEAD_INIT_NOLOCK(&ext_pvt->child_dialstrings);
	return ext_pvt;
}

void ast_cc_extension_monitor_add_dialstring(struct ast_channel *incoming, const char * const dialstring, const char * const device_name)
{
	struct ast_datastore *cc_datastore;
	struct dialed_cc_interfaces *cc_interfaces;
	struct ast_cc_monitor *monitor;
	struct extension_monitor_pvt *extension_pvt;
	struct extension_child_dialstring *child_dialstring;
	struct cc_monitor_tree *interface_tree;
	int id;

	ast_channel_lock(incoming);
	if (!(cc_datastore = ast_channel_datastore_find(incoming, &dialed_cc_interfaces_info, NULL))) {
		ast_channel_unlock(incoming);
		return;
	}

	cc_interfaces = cc_datastore->data;
	interface_tree = cc_interfaces->interface_tree;
	id = cc_interfaces->dial_parent_id;
	ast_channel_unlock(incoming);

	AST_LIST_LOCK(interface_tree);
	AST_LIST_TRAVERSE(interface_tree, monitor, next) {
		if (monitor->id == id) {
			break;
		}
	}

	if (!monitor) {
		AST_LIST_UNLOCK(interface_tree);
		return;
	}

	extension_pvt = monitor->private_data;
	if (!(child_dialstring = ast_calloc(1, sizeof(*child_dialstring)))) {
		AST_LIST_UNLOCK(interface_tree);
		return;
	}
	ast_copy_string(child_dialstring->original_dialstring, dialstring, sizeof(child_dialstring->original_dialstring));
	ast_copy_string(child_dialstring->device_name, device_name, sizeof(child_dialstring->device_name));
	child_dialstring->is_valid = 1;
	AST_LIST_INSERT_TAIL(&extension_pvt->child_dialstrings, child_dialstring, next);
	AST_LIST_UNLOCK(interface_tree);
}

static void cc_extension_monitor_change_is_valid(struct cc_core_instance *core_instance, unsigned int parent_id, const char * const device_name, int is_valid)
{
	struct ast_cc_monitor *monitor_iter;
	struct extension_monitor_pvt *extension_pvt;
	struct extension_child_dialstring *child_dialstring;

	AST_LIST_TRAVERSE(core_instance->monitors, monitor_iter, next) {
		if (monitor_iter->id == parent_id) {
			break;
		}
	}

	if (!monitor_iter) {
		return;
	}
	extension_pvt = monitor_iter->private_data;

	AST_LIST_TRAVERSE(&extension_pvt->child_dialstrings, child_dialstring, next) {
		if (!strcmp(child_dialstring->device_name, device_name)) {
			child_dialstring->is_valid = is_valid;
			break;
		}
	}
}

/*!
 * \internal
 * \brief Allocate and initialize an "extension" interface for CC purposes
 *
 * When app_dial starts, this function is called in order to set up the
 * information about the extension in which this Dial is occurring. Any
 * devices dialed will have this particular cc_monitor as a parent.
 *
 * \param exten Extension from which Dial is occurring
 * \param context Context to which exten belongs
 * \param parent_id What should we set the parent_id of this interface to?
 * \retval NULL Memory allocation failure
 * \retval non-NULL The newly-created cc_monitor for the extension
 */
static struct ast_cc_monitor *cc_extension_monitor_init(const char * const exten, const char * const context, const unsigned int parent_id)
{
	struct ast_str *str = ast_str_alloca(2 * AST_MAX_EXTENSION);
	struct ast_cc_interface *cc_interface;
	struct ast_cc_monitor *monitor;

	ast_str_set(&str, 0, "%s@%s", exten, context);

	if (!(cc_interface = ao2_t_alloc(sizeof(*cc_interface) + ast_str_strlen(str), cc_interface_destroy,
					"Allocating new ast_cc_interface"))) {
		return NULL;
	}

	if (!(monitor = ao2_t_alloc(sizeof(*monitor), cc_monitor_destroy, "Allocating new ast_cc_monitor"))) {
		cc_unref(cc_interface, "failed to allocate the monitor, so unref the interface");
		return NULL;
	}

	if (!(monitor->private_data = extension_monitor_pvt_init())) {
		cc_unref(monitor, "Failed to initialize extension monitor private data. uref monitor");
		cc_unref(cc_interface, "Failed to initialize extension monitor private data. unref cc_interface");
	}

	monitor->id = ast_atomic_fetchadd_int(&dialed_cc_interface_counter, +1);
	monitor->parent_id = parent_id;
	cc_interface->monitor_type = "extension";
	cc_interface->monitor_class = AST_CC_EXTENSION_MONITOR;
	strcpy(cc_interface->device_name, ast_str_buffer(str));
	monitor->interface = cc_interface;
	ast_log_dynamic_level(cc_logger_level, "Created an extension cc interface for '%s' with id %u and parent %u\n", cc_interface->device_name, monitor->id, monitor->parent_id);
	return monitor;
}

/*!
 * \internal
 * \brief allocate dialed_cc_interfaces datastore and initialize fields
 *
 * This function is called when Situation 1 occurs in ast_cc_call_init.
 * See that function for more information on what Situation 1 is.
 *
 * In this particular case, we have to do a lot of memory allocation in order
 * to create the datastore, the data for the datastore, the tree of interfaces
 * that we'll be adding to, and the initial extension interface for this Dial
 * attempt.
 *
 * \param chan The channel onto which the datastore should be added.
 * \retval -1 An error occurred
 * \retval 0 Success
 */
static int cc_interfaces_datastore_init(struct ast_channel *chan) {
	struct dialed_cc_interfaces *interfaces;
	struct ast_cc_monitor *monitor;
	struct ast_datastore *dial_cc_datastore;

	/*XXX This may be a bit controversial. In an attempt to not allocate
	 * extra resources, I make sure that a future request will be within
	 * limits. The problem here is that it is reasonable to think that
	 * even if we're not within the limits at this point, we may be by
	 * the time the requestor will have made his request. This may be
	 * deleted at some point.
	 */
	if (!ast_cc_request_is_within_limits()) {
		return 0;
	}

	if (!(interfaces = ast_calloc(1, sizeof(*interfaces)))) {
		return -1;
	}

	if (!(monitor = cc_extension_monitor_init(S_OR(ast_channel_macroexten(chan), ast_channel_exten(chan)), S_OR(ast_channel_macrocontext(chan), ast_channel_context(chan)), 0))) {
		ast_free(interfaces);
		return -1;
	}

	if (!(dial_cc_datastore = ast_datastore_alloc(&dialed_cc_interfaces_info, NULL))) {
		cc_unref(monitor, "Could not allocate the dialed interfaces datastore. Unreffing monitor");
		ast_free(interfaces);
		return -1;
	}

	if (!(interfaces->interface_tree = ao2_t_alloc(sizeof(*interfaces->interface_tree), cc_interface_tree_destroy,
					"Allocate monitor tree"))) {
		ast_datastore_free(dial_cc_datastore);
		cc_unref(monitor, "Could not allocate monitor tree on dialed interfaces datastore. Unreffing monitor");
		ast_free(interfaces);
		return -1;
	}

	/* Finally, all that allocation is done... */
	AST_LIST_HEAD_INIT(interfaces->interface_tree);
	AST_LIST_INSERT_TAIL(interfaces->interface_tree, monitor, next);
	cc_ref(monitor, "List's reference to extension monitor");
	dial_cc_datastore->data = interfaces;
	dial_cc_datastore->inheritance = DATASTORE_INHERIT_FOREVER;
	interfaces->dial_parent_id = monitor->id;
	interfaces->core_id = monitor->core_id = ast_atomic_fetchadd_int(&core_id_counter, +1);
	interfaces->is_original_caller = 1;
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, dial_cc_datastore);
	ast_channel_unlock(chan);
	cc_unref(monitor, "Unreffing allocation's reference");
	return 0;
}

/*!
 * \internal
 * \brief  Call a monitor's destructor before the monitor has been allocated
 * \since 1.8
 *
 * \param monitor_type The type of monitor callbacks to use when calling the destructor
 * \param private_data Data allocated by a channel driver that must be freed
 *
 * \details
 * I'll admit, this is a bit evil.
 *
 * When a channel driver determines that it can offer a call completion service to
 * a caller, it is very likely that the channel driver will need to allocate some
 * data so that when the time comes to request CC, the channel driver will have the
 * necessary data at hand.
 *
 * The problem is that there are many places where failures may occur before the monitor
 * has been properly allocated and had its callbacks assigned to it. If one of these
 * failures should occur, then we still need to let the channel driver know that it
 * must destroy the data that it allocated.
 *
 * \return Nothing
 */
static void call_destructor_with_no_monitor(const char * const monitor_type, void *private_data)
{
	const struct ast_cc_monitor_callbacks *monitor_callbacks = find_monitor_callbacks(monitor_type);

	if (!monitor_callbacks) {
		return;
	}

	monitor_callbacks->destructor(private_data);
}

/*!
 * \internal
 * \brief Allocate and intitialize a device cc_monitor
 *
 * For all intents and purposes, this is the same as
 * cc_extension_monitor_init, except that there is only
 * a single parameter used for naming the interface.
 *
 * This function is called when handling AST_CONTROL_CC frames.
 * The device has reported that CC is possible, so we add it
 * to the interface_tree.
 *
 * Note that it is not necessarily erroneous to add the same
 * device to the tree twice. If the same device is called by
 * two different extension during the same call, then
 * that is a legitimate situation.
 *
 * \param device_name The name of the device being added to the tree
 * \param dialstring The dialstring used to dial the device being added
 * \param parent_id The parent of this new tree node.
 * \retval NULL Memory allocation failure
 * \retval non-NULL The new ast_cc_interface created.
 */
static struct ast_cc_monitor *cc_device_monitor_init(const char * const device_name, const char * const dialstring, const struct cc_control_payload *cc_data, int core_id)
{
	struct ast_cc_interface *cc_interface;
	struct ast_cc_monitor *monitor;
	size_t device_name_len = strlen(device_name);
	int parent_id = cc_data->parent_interface_id;

	if (!(cc_interface = ao2_t_alloc(sizeof(*cc_interface) + device_name_len, cc_interface_destroy,
					"Allocating new ast_cc_interface"))) {
		return NULL;
	}

	if (!(cc_interface->config_params = ast_cc_config_params_init())) {
		cc_unref(cc_interface, "Failed to allocate config params, unref interface");
		return NULL;
	}

	if (!(monitor = ao2_t_alloc(sizeof(*monitor), cc_monitor_destroy, "Allocating new ast_cc_monitor"))) {
		cc_unref(cc_interface, "Failed to allocate monitor, unref interface");
		return NULL;
	}

	if (!(monitor->dialstring = ast_strdup(dialstring))) {
		cc_unref(monitor, "Failed to copy dialable name. Unref monitor");
		cc_unref(cc_interface, "Failed to copy dialable name");
		return NULL;
	}

	if (!(monitor->callbacks = find_monitor_callbacks(cc_data->monitor_type))) {
		cc_unref(monitor, "Failed to find monitor callbacks. Unref monitor");
		cc_unref(cc_interface, "Failed to find monitor callbacks");
		return NULL;
	}

	strcpy(cc_interface->device_name, device_name);
	monitor->id = ast_atomic_fetchadd_int(&dialed_cc_interface_counter, +1);
	monitor->parent_id = parent_id;
	monitor->core_id = core_id;
	monitor->service_offered = cc_data->service;
	monitor->private_data = cc_data->private_data;
	cc_interface->monitor_type = cc_data->monitor_type;
	cc_interface->monitor_class = AST_CC_DEVICE_MONITOR;
	monitor->interface = cc_interface;
	monitor->available_timer_id = -1;
	ast_cc_copy_config_params(cc_interface->config_params, &cc_data->config_params);
	ast_log_dynamic_level(cc_logger_level, "Core %d: Created a device cc interface for '%s' with id %u and parent %u\n",
			monitor->core_id, cc_interface->device_name, monitor->id, monitor->parent_id);
	return monitor;
}

/*!
 * \details
 * Unless we are ignoring CC for some reason, we will always
 * call this function when we read an AST_CONTROL_CC frame
 * from an outbound channel.
 *
 * This function will call cc_device_monitor_init to
 * create the new cc_monitor for the device from which
 * we read the frame. In addition, the new device will be added
 * to the monitor tree on the dialed_cc_interfaces datastore
 * on the inbound channel.
 *
 * If this is the first AST_CONTROL_CC frame that we have handled
 * for this call, then we will also initialize the CC core for
 * this call.
 */
void ast_handle_cc_control_frame(struct ast_channel *inbound, struct ast_channel *outbound, void *frame_data)
{
	char *device_name;
	char *dialstring;
	struct ast_cc_monitor *monitor;
	struct ast_datastore *cc_datastore;
	struct dialed_cc_interfaces *cc_interfaces;
	struct cc_control_payload *cc_data = frame_data;
	struct cc_core_instance *core_instance;

	device_name = cc_data->device_name;
	dialstring = cc_data->dialstring;

	ast_channel_lock(inbound);
	if (!(cc_datastore = ast_channel_datastore_find(inbound, &dialed_cc_interfaces_info, NULL))) {
		ast_log(LOG_WARNING, "Unable to retrieve CC datastore while processing CC frame from '%s'. CC services will be unavailable.\n", device_name);
		ast_channel_unlock(inbound);
		call_destructor_with_no_monitor(cc_data->monitor_type, cc_data->private_data);
		return;
	}

	cc_interfaces = cc_datastore->data;

	if (cc_interfaces->ignore) {
		ast_channel_unlock(inbound);
		call_destructor_with_no_monitor(cc_data->monitor_type, cc_data->private_data);
		return;
	}

	if (!cc_interfaces->is_original_caller) {
		/* If the is_original_caller is not set on the *inbound* channel, then
		 * it must be a local channel. As such, we do not want to create a core instance
		 * or an agent for the local channel. Instead, we want to pass this along to the
		 * other side of the local channel so that the original caller can benefit.
		 */
		ast_channel_unlock(inbound);
		ast_indicate_data(inbound, AST_CONTROL_CC, cc_data, sizeof(*cc_data));
		return;
	}

	core_instance = find_cc_core_instance(cc_interfaces->core_id);
	if (!core_instance) {
		core_instance = cc_core_init_instance(inbound, cc_interfaces->interface_tree,
			cc_interfaces->core_id, cc_data);
		if (!core_instance) {
			cc_interfaces->ignore = 1;
			ast_channel_unlock(inbound);
			call_destructor_with_no_monitor(cc_data->monitor_type, cc_data->private_data);
			return;
		}
	}

	ast_channel_unlock(inbound);

	/* Yeah this kind of sucks, but luckily most people
	 * aren't dialing thousands of interfaces on every call
	 *
	 * This traversal helps us to not create duplicate monitors in
	 * case a device queues multiple CC control frames.
	 */
	AST_LIST_LOCK(cc_interfaces->interface_tree);
	AST_LIST_TRAVERSE(cc_interfaces->interface_tree, monitor, next) {
		if (!strcmp(monitor->interface->device_name, device_name)) {
			ast_log_dynamic_level(cc_logger_level, "Core %d: Device %s sent us multiple CC control frames. Ignoring those beyond the first.\n",
					core_instance->core_id, device_name);
			AST_LIST_UNLOCK(cc_interfaces->interface_tree);
			cc_unref(core_instance, "Returning early from ast_handle_cc_control_frame. Unref core_instance");
			call_destructor_with_no_monitor(cc_data->monitor_type, cc_data->private_data);
			return;
		}
	}
	AST_LIST_UNLOCK(cc_interfaces->interface_tree);

	if (!(monitor = cc_device_monitor_init(device_name, dialstring, cc_data, core_instance->core_id))) {
		ast_log(LOG_WARNING, "Unable to create CC device interface for '%s'. CC services will be unavailable on this interface.\n", device_name);
		cc_unref(core_instance, "Returning early from ast_handle_cc_control_frame. Unref core_instance");
		call_destructor_with_no_monitor(cc_data->monitor_type, cc_data->private_data);
		return;
	}

	AST_LIST_LOCK(cc_interfaces->interface_tree);
	cc_ref(monitor, "monitor tree's reference to the monitor");
	AST_LIST_INSERT_TAIL(cc_interfaces->interface_tree, monitor, next);
	AST_LIST_UNLOCK(cc_interfaces->interface_tree);

	cc_extension_monitor_change_is_valid(core_instance, monitor->parent_id, monitor->interface->device_name, 0);

	cc_publish_available(cc_interfaces->core_id, device_name, cc_service_to_string(cc_data->service));

	cc_unref(core_instance, "Done with core_instance after handling CC control frame");
	cc_unref(monitor, "Unref reference from allocating monitor");
}

int ast_cc_call_init(struct ast_channel *chan, int *ignore_cc)
{
	/* There are three situations to deal with here:
	 *
	 * 1. The channel does not have a dialed_cc_interfaces datastore on
	 * it. This means that this is the first time that Dial has
	 * been called. We need to create/initialize the datastore.
	 *
	 * 2. The channel does have a cc_interface datastore on it and
	 * the "ignore" indicator is 0. This means that a Local channel
	 * was called by a "parent" dial. We can check the datastore's
	 * parent field to see who the root of this particular dial tree
	 * is.
	 *
	 * 3. The channel does have a cc_interface datastore on it and
	 * the "ignore" indicator is 1. This means that a second Dial call
	 * is being made from an extension. In this case, we do not
	 * want to make any additions/modifications to the datastore. We
	 * will instead set a flag to indicate that CCSS is completely
	 * disabled for this Dial attempt.
	 */

	struct ast_datastore *cc_interfaces_datastore;
	struct dialed_cc_interfaces *interfaces;
	struct ast_cc_monitor *monitor;
	struct ast_cc_config_params *cc_params;

	ast_channel_lock(chan);

	cc_params = ast_channel_get_cc_config_params(chan);
	if (!cc_params) {
		ast_channel_unlock(chan);
		return -1;
	}
	if (ast_get_cc_agent_policy(cc_params) == AST_CC_AGENT_NEVER) {
		/* We can't offer CC to this caller anyway, so don't bother with CC on this call
		 */
		*ignore_cc = 1;
		ast_channel_unlock(chan);
		ast_log_dynamic_level(cc_logger_level, "Agent policy for %s is 'never'. CC not possible\n", ast_channel_name(chan));
		return 0;
	}

	if (!(cc_interfaces_datastore = ast_channel_datastore_find(chan, &dialed_cc_interfaces_info, NULL))) {
		/* Situation 1 has occurred */
		ast_channel_unlock(chan);
		return cc_interfaces_datastore_init(chan);
	}
	interfaces = cc_interfaces_datastore->data;
	ast_channel_unlock(chan);

	if (interfaces->ignore) {
		/* Situation 3 has occurred */
		*ignore_cc = 1;
		ast_log_dynamic_level(cc_logger_level, "Datastore is present with ignore flag set. Ignoring CC offers on this call\n");
		return 0;
	}

	/* Situation 2 has occurred */
	if (!(monitor = cc_extension_monitor_init(S_OR(ast_channel_macroexten(chan), ast_channel_exten(chan)),
			S_OR(ast_channel_macrocontext(chan), ast_channel_context(chan)), interfaces->dial_parent_id))) {
		return -1;
	}
	monitor->core_id = interfaces->core_id;
	AST_LIST_LOCK(interfaces->interface_tree);
	cc_ref(monitor, "monitor tree's reference to the monitor");
	AST_LIST_INSERT_TAIL(interfaces->interface_tree, monitor, next);
	AST_LIST_UNLOCK(interfaces->interface_tree);
	interfaces->dial_parent_id = monitor->id;
	cc_unref(monitor, "Unref monitor's allocation reference");
	return 0;
}

int ast_cc_request_is_within_limits(void)
{
	return cc_request_count < global_cc_max_requests;
}

int ast_cc_get_current_core_id(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct dialed_cc_interfaces *cc_interfaces;
	int core_id_return;

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &dialed_cc_interfaces_info, NULL))) {
		ast_channel_unlock(chan);
		return -1;
	}

	cc_interfaces = datastore->data;
	core_id_return = cc_interfaces->ignore ? -1 : cc_interfaces->core_id;
	ast_channel_unlock(chan);
	return core_id_return;

}

static long count_agents(const char * const caller, const int core_id_exception)
{
	struct count_agents_cb_data data = {.core_id_exception = core_id_exception,};

	ao2_t_callback_data(cc_core_instances, OBJ_NODATA, count_agents_cb, (char *)caller, &data, "Counting agents");
	ast_log_dynamic_level(cc_logger_level, "Counted %d agents\n", data.count);
	return data.count;
}

static void kill_duplicate_offers(char *caller)
{
	unsigned long match_flags = MATCH_NO_REQUEST;
	struct ao2_iterator *dups_iter;

	/*
	 * Must remove the ref that was in cc_core_instances outside of
	 * the container lock to prevent deadlock.
	 */
	dups_iter = ao2_t_callback_data(cc_core_instances, OBJ_MULTIPLE | OBJ_UNLINK,
		match_agent, caller, &match_flags, "Killing duplicate offers");
	if (dups_iter) {
		/* Now actually unref any duplicate offers by simply destroying the iterator. */
		ao2_iterator_destroy(dups_iter);
	}
}

static void check_callback_sanity(const struct ast_cc_agent_callbacks *callbacks)
{
	ast_assert(callbacks->init != NULL);
	ast_assert(callbacks->start_offer_timer != NULL);
	ast_assert(callbacks->stop_offer_timer != NULL);
	ast_assert(callbacks->respond != NULL);
	ast_assert(callbacks->status_request != NULL);
	ast_assert(callbacks->start_monitoring != NULL);
	ast_assert(callbacks->callee_available != NULL);
	ast_assert(callbacks->destructor != NULL);
}

static void agent_destroy(void *data)
{
	struct ast_cc_agent *agent = data;

	if (agent->callbacks) {
		agent->callbacks->destructor(agent);
	}
	ast_cc_config_params_destroy(agent->cc_params);
}

static struct ast_cc_agent *cc_agent_init(struct ast_channel *caller_chan,
		const char * const caller_name, const int core_id,
		struct cc_monitor_tree *interface_tree)
{
	struct ast_cc_agent *agent;
	struct ast_cc_config_params *cc_params;

	if (!(agent = ao2_t_alloc(sizeof(*agent) + strlen(caller_name), agent_destroy,
					"Allocating new ast_cc_agent"))) {
		return NULL;
	}

	agent->core_id = core_id;
	strcpy(agent->device_name, caller_name);

	cc_params = ast_channel_get_cc_config_params(caller_chan);
	if (!cc_params) {
		cc_unref(agent, "Could not get channel config params.");
		return NULL;
	}
	if (!(agent->cc_params = ast_cc_config_params_init())) {
		cc_unref(agent, "Could not init agent config params.");
		return NULL;
	}
	ast_cc_copy_config_params(agent->cc_params, cc_params);

	if (!(agent->callbacks = find_agent_callbacks(caller_chan))) {
		cc_unref(agent, "Could not find agent callbacks.");
		return NULL;
	}
	check_callback_sanity(agent->callbacks);

	if (agent->callbacks->init(agent, caller_chan)) {
		cc_unref(agent, "Agent init callback failed.");
		return NULL;
	}
	ast_log_dynamic_level(cc_logger_level, "Core %u: Created an agent for caller %s\n",
			agent->core_id, agent->device_name);
	return agent;
}

/* Generic agent callbacks */
static int cc_generic_agent_init(struct ast_cc_agent *agent, struct ast_channel *chan);
static int cc_generic_agent_start_offer_timer(struct ast_cc_agent *agent);
static int cc_generic_agent_stop_offer_timer(struct ast_cc_agent *agent);
static void cc_generic_agent_respond(struct ast_cc_agent *agent, enum ast_cc_agent_response_reason reason);
static int cc_generic_agent_status_request(struct ast_cc_agent *agent);
static int cc_generic_agent_stop_ringing(struct ast_cc_agent *agent);
static int cc_generic_agent_start_monitoring(struct ast_cc_agent *agent);
static int cc_generic_agent_recall(struct ast_cc_agent *agent);
static void cc_generic_agent_destructor(struct ast_cc_agent *agent);

static struct ast_cc_agent_callbacks generic_agent_callbacks = {
	.type = "generic",
	.init = cc_generic_agent_init,
	.start_offer_timer = cc_generic_agent_start_offer_timer,
	.stop_offer_timer = cc_generic_agent_stop_offer_timer,
	.respond = cc_generic_agent_respond,
	.status_request = cc_generic_agent_status_request,
	.stop_ringing = cc_generic_agent_stop_ringing,
	.start_monitoring = cc_generic_agent_start_monitoring,
	.callee_available = cc_generic_agent_recall,
	.destructor = cc_generic_agent_destructor,
};

struct cc_generic_agent_pvt {
	/*!
	 * Subscription to device state
	 *
	 * Used in the CC_CALLER_BUSY state. The
	 * generic agent will subscribe to the
	 * device state of the caller in order to
	 * determine when we may move on
	 */
	struct stasis_subscription *sub;
	/*!
	 * Scheduler id of offer timer.
	 */
	int offer_timer_id;
	/*!
	 * Caller ID number
	 *
	 * When we re-call the caller, we need
	 * to provide this information to
	 * ast_request_and_dial so that the
	 * information will be present in the
	 * call to the callee
	 */
	char cid_num[AST_CHANNEL_NAME];
	/*!
	 * Caller ID name
	 *
	 * See the description of cid_num.
	 * The same applies here, except this
	 * is the caller's name.
	 */
	char cid_name[AST_CHANNEL_NAME];
	/*!
	 * Extension dialed
	 *
	 * The original extension dialed. This is used
	 * so that when performing a recall, we can
	 * call the proper extension.
	 */
	char exten[AST_CHANNEL_NAME];
	/*!
	 * Context dialed
	 *
	 * The original context dialed. This is used
	 * so that when performaing a recall, we can
	 * call into the proper context
	 */
	char context[AST_CHANNEL_NAME];
};

static int cc_generic_agent_init(struct ast_cc_agent *agent, struct ast_channel *chan)
{
	struct cc_generic_agent_pvt *generic_pvt = ast_calloc(1, sizeof(*generic_pvt));

	if (!generic_pvt) {
		return -1;
	}

	generic_pvt->offer_timer_id = -1;
	if (ast_channel_caller(chan)->id.number.valid && ast_channel_caller(chan)->id.number.str) {
		ast_copy_string(generic_pvt->cid_num, ast_channel_caller(chan)->id.number.str, sizeof(generic_pvt->cid_num));
	}
	if (ast_channel_caller(chan)->id.name.valid && ast_channel_caller(chan)->id.name.str) {
		ast_copy_string(generic_pvt->cid_name, ast_channel_caller(chan)->id.name.str, sizeof(generic_pvt->cid_name));
	}
	ast_copy_string(generic_pvt->exten, S_OR(ast_channel_macroexten(chan), ast_channel_exten(chan)), sizeof(generic_pvt->exten));
	ast_copy_string(generic_pvt->context, S_OR(ast_channel_macrocontext(chan), ast_channel_context(chan)), sizeof(generic_pvt->context));
	agent->private_data = generic_pvt;
	ast_set_flag(agent, AST_CC_AGENT_SKIP_OFFER);
	return 0;
}

static int offer_timer_expire(const void *data)
{
	struct ast_cc_agent *agent = (struct ast_cc_agent *) data;
	struct cc_generic_agent_pvt *agent_pvt = agent->private_data;
	ast_log_dynamic_level(cc_logger_level, "Core %u: Queuing change request because offer timer has expired.\n",
			agent->core_id);
	agent_pvt->offer_timer_id = -1;
	ast_cc_failed(agent->core_id, "Generic agent %s offer timer expired", agent->device_name);
	cc_unref(agent, "Remove scheduler's reference to the agent");
	return 0;
}

static int cc_generic_agent_start_offer_timer(struct ast_cc_agent *agent)
{
	int when;
	int sched_id;
	struct cc_generic_agent_pvt *generic_pvt = agent->private_data;

	ast_assert(cc_sched_context != NULL);
	ast_assert(agent->cc_params != NULL);

	when = ast_get_cc_offer_timer(agent->cc_params) * 1000;
	ast_log_dynamic_level(cc_logger_level, "Core %u: About to schedule offer timer expiration for %d ms\n",
			agent->core_id, when);
	if ((sched_id = ast_sched_add(cc_sched_context, when, offer_timer_expire, cc_ref(agent, "Give scheduler an agent ref"))) == -1) {
		return -1;
	}
	generic_pvt->offer_timer_id = sched_id;
	return 0;
}

static int cc_generic_agent_stop_offer_timer(struct ast_cc_agent *agent)
{
	struct cc_generic_agent_pvt *generic_pvt = agent->private_data;

	if (generic_pvt->offer_timer_id != -1) {
		if (!ast_sched_del(cc_sched_context, generic_pvt->offer_timer_id)) {
			cc_unref(agent, "Remove scheduler's reference to the agent");
		}
		generic_pvt->offer_timer_id = -1;
	}
	return 0;
}

static void cc_generic_agent_respond(struct ast_cc_agent *agent, enum ast_cc_agent_response_reason reason)
{
	/* The generic agent doesn't have to do anything special to
	 * acknowledge a CC request. Just return.
	 */
	return;
}

static int cc_generic_agent_status_request(struct ast_cc_agent *agent)
{
	ast_cc_agent_status_response(agent->core_id, ast_device_state(agent->device_name));
	return 0;
}

static int cc_generic_agent_stop_ringing(struct ast_cc_agent *agent)
{
	struct ast_channel *recall_chan = ast_channel_get_by_name_prefix(agent->device_name, strlen(agent->device_name));

	if (!recall_chan) {
		return 0;
	}

	ast_softhangup(recall_chan, AST_SOFTHANGUP_EXPLICIT);
	return 0;
}

static void generic_agent_devstate_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_cc_agent *agent = userdata;
	enum ast_device_state new_state;
	struct ast_device_state_message *dev_state;
	struct cc_generic_agent_pvt *generic_pvt = agent->private_data;

	if (stasis_subscription_final_message(sub, msg)) {
		cc_unref(agent, "Done holding ref for subscription");
		return;
	} else if (ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	dev_state = stasis_message_data(msg);
	if (dev_state->eid) {
		/* ignore non-aggregate states */
		return;
	}

	new_state = dev_state->state;
	if (!cc_generic_is_device_available(new_state)) {
		/* Not interested in this new state of the device.  It is still busy. */
		return;
	}

	generic_pvt->sub = stasis_unsubscribe(sub);
	ast_cc_agent_caller_available(agent->core_id, "%s is no longer busy", agent->device_name);
}

static int cc_generic_agent_start_monitoring(struct ast_cc_agent *agent)
{
	struct cc_generic_agent_pvt *generic_pvt = agent->private_data;
	struct ast_str *str = ast_str_alloca(128);
	struct stasis_topic *device_specific_topic;

	ast_assert(generic_pvt->sub == NULL);
	ast_str_set(&str, 0, "Agent monitoring %s device state since it is busy\n",
		agent->device_name);

	device_specific_topic = ast_device_state_topic(agent->device_name);
	if (!device_specific_topic) {
		return -1;
	}

	if (!(generic_pvt->sub = stasis_subscribe(device_specific_topic, generic_agent_devstate_cb, agent))) {
		return -1;
	}
	cc_ref(agent, "Ref agent for subscription");
	return 0;
}

static void *generic_recall(void *data)
{
	struct ast_cc_agent *agent = data;
	struct cc_generic_agent_pvt *generic_pvt = agent->private_data;
	const char *interface = S_OR(ast_get_cc_agent_dialstring(agent->cc_params), ast_strdupa(agent->device_name));
	const char *tech;
	char *target;
	int reason;
	struct ast_channel *chan;
	const char *callback_macro = ast_get_cc_callback_macro(agent->cc_params);
	const char *callback_sub = ast_get_cc_callback_sub(agent->cc_params);
	unsigned int recall_timer = ast_get_cc_recall_timer(agent->cc_params) * 1000;
	struct ast_format_cap *tmp_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	if (!tmp_cap) {
		return NULL;
	}

	tech = interface;
	if ((target = strchr(interface, '/'))) {
		*target++ = '\0';
	}

	ast_format_cap_append(tmp_cap, ast_format_slin, 0);
	if (!(chan = ast_request_and_dial(tech, tmp_cap, NULL, NULL, target, recall_timer, &reason, generic_pvt->cid_num, generic_pvt->cid_name))) {
		/* Hmm, no channel. Sucks for you, bud.
		 */
		ast_log_dynamic_level(cc_logger_level, "Core %u: Failed to call back %s for reason %d\n",
				agent->core_id, agent->device_name, reason);
		ast_cc_failed(agent->core_id, "Failed to call back device %s/%s", tech, target);
		ao2_ref(tmp_cap, -1);
		return NULL;
	}
	ao2_ref(tmp_cap, -1);
	
	/* We have a channel. It's time now to set up the datastore of recalled CC interfaces.
	 * This will be a common task for all recall functions. If it were possible, I'd have
	 * the core do it automatically, but alas I cannot. Instead, I will provide a public
	 * function to do so.
	 */
	ast_setup_cc_recall_datastore(chan, agent->core_id);
	ast_cc_agent_set_interfaces_chanvar(chan);

	ast_channel_exten_set(chan, generic_pvt->exten);
	ast_channel_context_set(chan, generic_pvt->context);
	ast_channel_priority_set(chan, 1);

	pbx_builtin_setvar_helper(chan, "CC_EXTEN", generic_pvt->exten);
	pbx_builtin_setvar_helper(chan, "CC_CONTEXT", generic_pvt->context);

	if (!ast_strlen_zero(callback_macro)) {
		ast_log_dynamic_level(cc_logger_level, "Core %u: There's a callback macro configured for agent %s\n",
				agent->core_id, agent->device_name);
		if (ast_app_exec_macro(NULL, chan, callback_macro)) {
			ast_cc_failed(agent->core_id, "Callback macro to %s failed. Maybe a hangup?", agent->device_name);
			ast_hangup(chan);
			return NULL;
		}
	}

	if (!ast_strlen_zero(callback_sub)) {
		ast_log_dynamic_level(cc_logger_level, "Core %u: There's a callback subroutine configured for agent %s\n",
				agent->core_id, agent->device_name);
		if (ast_app_exec_sub(NULL, chan, callback_sub, 0)) {
			ast_cc_failed(agent->core_id, "Callback subroutine to %s failed. Maybe a hangup?", agent->device_name);
			ast_hangup(chan);
			return NULL;
		}
	}
	if (ast_pbx_start(chan)) {
		ast_cc_failed(agent->core_id, "PBX failed to start for %s.", agent->device_name);
		ast_hangup(chan);
		return NULL;
	}
	ast_cc_agent_recalling(agent->core_id, "Generic agent %s is recalling",
		agent->device_name);
	return NULL;
}

static int cc_generic_agent_recall(struct ast_cc_agent *agent)
{
	pthread_t clotho;
	enum ast_device_state current_state = ast_device_state(agent->device_name);

	if (!cc_generic_is_device_available(current_state)) {
		/* We can't try to contact the device right now because he's not available
		 * Let the core know he's busy.
		 */
		ast_cc_agent_caller_busy(agent->core_id, "Generic agent caller %s is busy", agent->device_name);
		return 0;
	}
	ast_pthread_create_detached_background(&clotho, NULL, generic_recall, agent);
	return 0;
}

static void cc_generic_agent_destructor(struct ast_cc_agent *agent)
{
	struct cc_generic_agent_pvt *agent_pvt = agent->private_data;

	if (!agent_pvt) {
		/* The agent constructor probably failed. */
		return;
	}

	cc_generic_agent_stop_offer_timer(agent);
	if (agent_pvt->sub) {
		agent_pvt->sub = stasis_unsubscribe(agent_pvt->sub);
	}

	ast_free(agent_pvt);
}

static void cc_core_instance_destructor(void *data)
{
	struct cc_core_instance *core_instance = data;
	ast_log_dynamic_level(cc_logger_level, "Core %d: Destroying core instance\n", core_instance->core_id);
	if (core_instance->agent) {
		cc_unref(core_instance->agent, "Core instance is done with the agent now");
	}
	if (core_instance->monitors) {
		core_instance->monitors = cc_unref(core_instance->monitors, "Core instance is done with interface list");
	}
}

static struct cc_core_instance *cc_core_init_instance(struct ast_channel *caller_chan,
		struct cc_monitor_tree *called_tree, const int core_id, struct cc_control_payload *cc_data)
{
	char caller[AST_CHANNEL_NAME];
	struct cc_core_instance *core_instance;
	struct ast_cc_config_params *cc_params;
	long agent_count;
	int recall_core_id;

	ast_channel_get_device_name(caller_chan, caller, sizeof(caller));
	cc_params = ast_channel_get_cc_config_params(caller_chan);
	if (!cc_params) {
		ast_log_dynamic_level(cc_logger_level, "Could not get CC parameters for %s\n",
			caller);
		return NULL;
	}
	/* First, we need to kill off other pending CC offers from caller. If the caller is going
	 * to request a CC service, it may only be for the latest call he made.
	 */
	if (ast_get_cc_agent_policy(cc_params) == AST_CC_AGENT_GENERIC) {
		kill_duplicate_offers(caller);
	}

	ast_cc_is_recall(caller_chan, &recall_core_id, NULL);
	agent_count = count_agents(caller, recall_core_id);
	if (agent_count >= ast_get_cc_max_agents(cc_params)) {
		ast_log_dynamic_level(cc_logger_level, "Caller %s already has the maximum number of agents configured\n", caller);
		return NULL;
	}

	/* Generic agents can only have a single outstanding CC request per caller. */
	if (agent_count > 0 && ast_get_cc_agent_policy(cc_params) == AST_CC_AGENT_GENERIC) {
		ast_log_dynamic_level(cc_logger_level, "Generic agents can only have a single outstanding request\n");
		return NULL;
	}

	/* Next, we need to create the core instance for this call */
	if (!(core_instance = ao2_t_alloc(sizeof(*core_instance), cc_core_instance_destructor, "Creating core instance for CC"))) {
		return NULL;
	}

	core_instance->core_id = core_id;
	if (!(core_instance->agent = cc_agent_init(caller_chan, caller, core_instance->core_id, called_tree))) {
		cc_unref(core_instance, "Couldn't allocate agent, unref core_instance");
		return NULL;
	}

	core_instance->monitors = cc_ref(called_tree, "Core instance getting ref to monitor tree");

	ao2_t_link(cc_core_instances, core_instance, "Link core instance into container");

	return core_instance;
}

struct cc_state_change_args {
	struct cc_core_instance *core_instance;/*!< Holds reference to core instance. */
	enum cc_state state;
	int core_id;
	char debug[1];
};

static int is_state_change_valid(enum cc_state current_state, const enum cc_state new_state, struct ast_cc_agent *agent)
{
	int is_valid = 0;
	switch (new_state) {
	case CC_AVAILABLE:
		ast_log_dynamic_level(cc_logger_level, "Core %u: Asked to change to state %u? That should never happen.\n",
				agent->core_id, new_state);
		break;
	case CC_CALLER_OFFERED:
		if (current_state == CC_AVAILABLE) {
			is_valid = 1;
		}
		break;
	case CC_CALLER_REQUESTED:
		if (current_state == CC_CALLER_OFFERED ||
				(current_state == CC_AVAILABLE && ast_test_flag(agent, AST_CC_AGENT_SKIP_OFFER))) {
			is_valid = 1;
		}
		break;
	case CC_ACTIVE:
		if (current_state == CC_CALLER_REQUESTED || current_state == CC_CALLER_BUSY) {
			is_valid = 1;
		}
		break;
	case CC_CALLEE_READY:
		if (current_state == CC_ACTIVE) {
			is_valid = 1;
		}
		break;
	case CC_CALLER_BUSY:
		if (current_state == CC_CALLEE_READY) {
			is_valid = 1;
		}
		break;
	case CC_RECALLING:
		if (current_state == CC_CALLEE_READY) {
			is_valid = 1;
		}
		break;
	case CC_COMPLETE:
		if (current_state == CC_RECALLING) {
			is_valid = 1;
		}
		break;
	case CC_FAILED:
		is_valid = 1;
		break;
	default:
		ast_log_dynamic_level(cc_logger_level, "Core %u: Asked to change to unknown state %u\n",
				agent->core_id, new_state);
		break;
	}

	return is_valid;
}

static int cc_available(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	/* This should never happen... */
	ast_log(LOG_WARNING, "Someone requested to change to CC_AVAILABLE? Ignoring.\n");
	return -1;
}

static int cc_caller_offered(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	if (core_instance->agent->callbacks->start_offer_timer(core_instance->agent)) {
		ast_cc_failed(core_instance->core_id, "Failed to start the offer timer for %s\n",
				core_instance->agent->device_name);
		return -1;
	}
	cc_publish_offertimerstart(core_instance->core_id, core_instance->agent->device_name, core_instance->agent->cc_params->cc_offer_timer);
	ast_log_dynamic_level(cc_logger_level, "Core %d: Started the offer timer for the agent %s!\n",
			core_instance->core_id, core_instance->agent->device_name);
	return 0;
}

/*!
 * \brief check if the core instance has any device monitors
 *
 * In any case where we end up removing a device monitor from the
 * list of device monitors, it is important to see what the state
 * of the list is afterwards. If we find that we only have extension
 * monitors left, then no devices are actually being monitored.
 * In such a case, we need to declare that CC has failed for this
 * call. This function helps those cases to determine if they should
 * declare failure.
 *
 * \param core_instance The core instance we are checking for the existence
 * of device monitors
 * \retval 0 No device monitors exist on this core_instance
 * \retval 1 There is still at least 1 device monitor remaining
 */
static int has_device_monitors(struct cc_core_instance *core_instance)
{
	struct ast_cc_monitor *iter;
	int res = 0;

	AST_LIST_TRAVERSE(core_instance->monitors, iter, next) {
		if (iter->interface->monitor_class == AST_CC_DEVICE_MONITOR) {
			res = 1;
			break;
		}
	}

	return res;
}

static void request_cc(struct cc_core_instance *core_instance)
{
	struct ast_cc_monitor *monitor_iter;
	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE_SAFE_BEGIN(core_instance->monitors, monitor_iter, next) {
		if (monitor_iter->interface->monitor_class == AST_CC_DEVICE_MONITOR) {
			if (monitor_iter->callbacks->request_cc(monitor_iter, &monitor_iter->available_timer_id)) {
				AST_LIST_REMOVE_CURRENT(next);
				cc_extension_monitor_change_is_valid(core_instance, monitor_iter->parent_id,
						monitor_iter->interface->device_name, 1);
				cc_unref(monitor_iter, "request_cc failed. Unref list's reference to monitor");
			} else {
				cc_publish_requested(core_instance->core_id, core_instance->agent->device_name, monitor_iter->interface->device_name);
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!has_device_monitors(core_instance)) {
		ast_cc_failed(core_instance->core_id, "All device monitors failed to request CC");
	}
	AST_LIST_UNLOCK(core_instance->monitors);
}

static int cc_caller_requested(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	if (!ast_cc_request_is_within_limits()) {
		ast_log(LOG_WARNING, "Cannot request CC since there is no more room for requests\n");
		core_instance->agent->callbacks->respond(core_instance->agent,
			AST_CC_AGENT_RESPONSE_FAILURE_TOO_MANY);
		ast_cc_failed(core_instance->core_id, "Too many requests in the system");
		return -1;
	}
	core_instance->agent->callbacks->stop_offer_timer(core_instance->agent);
	request_cc(core_instance);
	return 0;
}

static void unsuspend(struct cc_core_instance *core_instance)
{
	struct ast_cc_monitor *monitor_iter;
	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE_SAFE_BEGIN(core_instance->monitors, monitor_iter, next) {
		if (monitor_iter->interface->monitor_class == AST_CC_DEVICE_MONITOR) {
			if (monitor_iter->callbacks->unsuspend(monitor_iter)) {
				AST_LIST_REMOVE_CURRENT(next);
				cc_extension_monitor_change_is_valid(core_instance, monitor_iter->parent_id,
						monitor_iter->interface->device_name, 1);
				cc_unref(monitor_iter, "unsuspend failed. Unref list's reference to monitor");
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!has_device_monitors(core_instance)) {
		ast_cc_failed(core_instance->core_id, "All device monitors failed to unsuspend CC");
	}
	AST_LIST_UNLOCK(core_instance->monitors);
}

static int cc_active(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	/* Either
	 * 1. Callee accepted CC request, call agent's ack callback.
	 * 2. Caller became available, call agent's stop_monitoring callback and
	 *    call monitor's unsuspend callback.
	 */
	if (previous_state == CC_CALLER_REQUESTED) {
		core_instance->agent->callbacks->respond(core_instance->agent,
			AST_CC_AGENT_RESPONSE_SUCCESS);
		cc_publish_requestacknowledged(core_instance->core_id, core_instance->agent->device_name);
	} else if (previous_state == CC_CALLER_BUSY) {
		cc_publish_callerstopmonitoring(core_instance->core_id, core_instance->agent->device_name);
		unsuspend(core_instance);
	}
	/* Not possible for previous_state to be anything else due to the is_state_change_valid check at the beginning */
	return 0;
}

static int cc_callee_ready(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	core_instance->agent->callbacks->callee_available(core_instance->agent);
	return 0;
}

static void suspend(struct cc_core_instance *core_instance)
{
	struct ast_cc_monitor *monitor_iter;
	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE_SAFE_BEGIN(core_instance->monitors, monitor_iter, next) {
		if (monitor_iter->interface->monitor_class == AST_CC_DEVICE_MONITOR) {
			if (monitor_iter->callbacks->suspend(monitor_iter)) {
				AST_LIST_REMOVE_CURRENT(next);
				cc_extension_monitor_change_is_valid(core_instance, monitor_iter->parent_id,
						monitor_iter->interface->device_name, 1);
				cc_unref(monitor_iter, "suspend failed. Unref list's reference to monitor");
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!has_device_monitors(core_instance)) {
		ast_cc_failed(core_instance->core_id, "All device monitors failed to suspend CC");
	}
	AST_LIST_UNLOCK(core_instance->monitors);
}

static int cc_caller_busy(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	/* Callee was available, but caller was busy, call agent's begin_monitoring callback
	 * and call monitor's suspend callback.
	 */
	suspend(core_instance);
	core_instance->agent->callbacks->start_monitoring(core_instance->agent);
	cc_publish_callerstartmonitoring(core_instance->core_id, core_instance->agent->device_name);
	return 0;
}

static void cancel_available_timer(struct cc_core_instance *core_instance)
{
	struct ast_cc_monitor *monitor_iter;
	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE_SAFE_BEGIN(core_instance->monitors, monitor_iter, next) {
		if (monitor_iter->interface->monitor_class == AST_CC_DEVICE_MONITOR) {
			if (monitor_iter->callbacks->cancel_available_timer(monitor_iter, &monitor_iter->available_timer_id)) {
				AST_LIST_REMOVE_CURRENT(next);
				cc_extension_monitor_change_is_valid(core_instance, monitor_iter->parent_id,
						monitor_iter->interface->device_name, 1);
				cc_unref(monitor_iter, "cancel_available_timer failed. Unref list's reference to monitor");
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!has_device_monitors(core_instance)) {
		ast_cc_failed(core_instance->core_id, "All device monitors failed to cancel their available timers");
	}
	AST_LIST_UNLOCK(core_instance->monitors);
}

static int cc_recalling(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	/* Both caller and callee are available, call agent's recall callback
	 */
	cancel_available_timer(core_instance);
	cc_publish_callerrecalling(core_instance->core_id, core_instance->agent->device_name);
	return 0;
}

static int cc_complete(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	/* Recall has made progress, call agent and monitor destructor functions
	 */
	cc_publish_recallcomplete(core_instance->core_id, core_instance->agent->device_name);
	ao2_t_unlink(cc_core_instances, core_instance, "Unlink core instance since CC recall has completed");
	return 0;
}

static int cc_failed(struct cc_core_instance *core_instance, struct cc_state_change_args *args, enum cc_state previous_state)
{
	cc_publish_failure(core_instance->core_id, core_instance->agent->device_name, args->debug);
	ao2_t_unlink(cc_core_instances, core_instance, "Unlink core instance since CC failed");
	return 0;
}

static int (* const state_change_funcs [])(struct cc_core_instance *, struct cc_state_change_args *, enum cc_state previous_state) = {
	[CC_AVAILABLE] = cc_available,
	[CC_CALLER_OFFERED] = cc_caller_offered,
	[CC_CALLER_REQUESTED] = cc_caller_requested,
	[CC_ACTIVE] = cc_active,
	[CC_CALLEE_READY] = cc_callee_ready,
	[CC_CALLER_BUSY] = cc_caller_busy,
	[CC_RECALLING] = cc_recalling,
	[CC_COMPLETE] = cc_complete,
	[CC_FAILED] = cc_failed,
};

static int cc_do_state_change(void *datap)
{
	struct cc_state_change_args *args = datap;
	struct cc_core_instance *core_instance;
	enum cc_state previous_state;
	int res;

	ast_log_dynamic_level(cc_logger_level, "Core %d: State change to %u requested. Reason: %s\n",
			args->core_id, args->state, args->debug);

	core_instance = args->core_instance;

	if (!is_state_change_valid(core_instance->current_state, args->state, core_instance->agent)) {
		ast_log_dynamic_level(cc_logger_level, "Core %d: Invalid state change requested. Cannot go from %s to %s\n",
				args->core_id, cc_state_to_string(core_instance->current_state), cc_state_to_string(args->state));
		if (args->state == CC_CALLER_REQUESTED) {
			/*
			 * For out-of-order requests, we need to let the requester know that
			 * we can't handle the request now.
			 */
			core_instance->agent->callbacks->respond(core_instance->agent,
				AST_CC_AGENT_RESPONSE_FAILURE_INVALID);
		}
		ast_free(args);
		cc_unref(core_instance, "Unref core instance from when it was found earlier");
		return -1;
	}

	/* We can change to the new state now. */
	previous_state = core_instance->current_state;
	core_instance->current_state = args->state;
	res = state_change_funcs[core_instance->current_state](core_instance, args, previous_state);

	/* If state change successful then notify any device state watchers of the change */
	if (!res && !strcmp(core_instance->agent->callbacks->type, "generic")) {
		ccss_notify_device_state_change(core_instance->agent->device_name, core_instance->current_state);
	}

	ast_free(args);
	cc_unref(core_instance, "Unref since state change has completed"); /* From ao2_find */
	return res;
}

static int cc_request_state_change(enum cc_state state, const int core_id, const char *debug, va_list ap)
{
	int res;
	int debuglen;
	char dummy[1];
	va_list aq;
	struct cc_core_instance *core_instance;
	struct cc_state_change_args *args;
	/* This initial call to vsnprintf is simply to find what the
	 * size of the string needs to be
	 */
	va_copy(aq, ap);
	/* We add 1 to the result since vsnprintf's return does not
	 * include the terminating null byte
	 */
	debuglen = vsnprintf(dummy, sizeof(dummy), debug, aq) + 1;
	va_end(aq);

	if (!(args = ast_calloc(1, sizeof(*args) + debuglen))) {
		return -1;
	}

	core_instance = find_cc_core_instance(core_id);
	if (!core_instance) {
		ast_log_dynamic_level(cc_logger_level, "Core %d: Unable to find core instance.\n",
			core_id);
		ast_free(args);
		return -1;
	}

	args->core_instance = core_instance;
	args->state = state;
	args->core_id = core_id;
	vsnprintf(args->debug, debuglen, debug, ap);

	res = ast_taskprocessor_push(cc_core_taskprocessor, cc_do_state_change, args);
	if (res) {
		cc_unref(core_instance, "Unref core instance. ast_taskprocessor_push failed");
		ast_free(args);
	}
	return res;
}

struct cc_recall_ds_data {
	int core_id;
	char ignore;
	char nested;
	struct cc_monitor_tree *interface_tree;
};

static void *cc_recall_ds_duplicate(void *data)
{
	struct cc_recall_ds_data *old_data = data;
	struct cc_recall_ds_data *new_data = ast_calloc(1, sizeof(*new_data));

	if (!new_data) {
		return NULL;
	}
	new_data->interface_tree = cc_ref(old_data->interface_tree, "Bump refcount of monitor tree for recall datastore duplicate");
	new_data->core_id = old_data->core_id;
	new_data->nested = 1;
	return new_data;
}

static void cc_recall_ds_destroy(void *data)
{
	struct cc_recall_ds_data *recall_data = data;
	recall_data->interface_tree = cc_unref(recall_data->interface_tree, "Unref recall monitor tree");
	ast_free(recall_data);
}

static const struct ast_datastore_info recall_ds_info = {
	.type = "cc_recall",
	.duplicate = cc_recall_ds_duplicate,
	.destroy = cc_recall_ds_destroy,
};

int ast_setup_cc_recall_datastore(struct ast_channel *chan, const int core_id)
{
	struct ast_datastore *recall_datastore = ast_datastore_alloc(&recall_ds_info, NULL);
	struct cc_recall_ds_data *recall_data;
	struct cc_core_instance *core_instance;

	if (!recall_datastore) {
		return -1;
	}

	if (!(recall_data = ast_calloc(1, sizeof(*recall_data)))) {
		ast_datastore_free(recall_datastore);
		return -1;
	}

	if (!(core_instance = find_cc_core_instance(core_id))) {
		ast_free(recall_data);
		ast_datastore_free(recall_datastore);
		return -1;
	}

	recall_data->interface_tree = cc_ref(core_instance->monitors,
			"Bump refcount for monitor tree for recall datastore");
	recall_data->core_id = core_id;
	recall_datastore->data = recall_data;
	recall_datastore->inheritance = DATASTORE_INHERIT_FOREVER;
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, recall_datastore);
	ast_channel_unlock(chan);
	cc_unref(core_instance, "Recall datastore set up. No need for core_instance ref");
	return 0;
}

int ast_cc_is_recall(struct ast_channel *chan, int *core_id, const char * const monitor_type)
{
	struct ast_datastore *recall_datastore;
	struct cc_recall_ds_data *recall_data;
	struct cc_monitor_tree *interface_tree;
	char device_name[AST_CHANNEL_NAME];
	struct ast_cc_monitor *device_monitor;
	int core_id_candidate;

	ast_assert(core_id != NULL);

	*core_id = -1;

	ast_channel_lock(chan);
	if (!(recall_datastore = ast_channel_datastore_find(chan, &recall_ds_info, NULL))) {
		/* Obviously not a recall if the datastore isn't present */
		ast_channel_unlock(chan);
		return 0;
	}

	recall_data = recall_datastore->data;

	if (recall_data->ignore) {
		/* Though this is a recall, the call to this particular interface is not part of the
		 * recall either because this is a call forward or because this is not the first
		 * invocation of Dial during this call
		 */
		ast_channel_unlock(chan);
		return 0;
	}

	if (!recall_data->nested) {
		/* If the nested flag is not set, then this means that
		 * the channel passed to this function is the caller making
		 * the recall. This means that we shouldn't look through
		 * the monitor tree for the channel because it shouldn't be
		 * there. However, this is a recall though, so return true.
		 */
		*core_id = recall_data->core_id;
		ast_channel_unlock(chan);
		return 1;
	}

	if (ast_strlen_zero(monitor_type)) {
		/* If someone passed a NULL or empty monitor type, then it is clear
		 * the channel they passed in was an incoming channel, and so searching
		 * the list of dialed interfaces is not going to be helpful. Just return
		 * false immediately.
		 */
		ast_channel_unlock(chan);
		return 0;
	}

	interface_tree = recall_data->interface_tree;
	ast_channel_get_device_name(chan, device_name, sizeof(device_name));
	/* We grab the value of the recall_data->core_id so that we
	 * can unlock the channel before we start looking through the
	 * interface list. That way we don't have to worry about a possible
	 * clash between the channel lock and the monitor tree lock.
	 */
	core_id_candidate = recall_data->core_id;
	ast_channel_unlock(chan);

	/*
	 * Now we need to find out if the channel device name
	 * is in the list of interfaces in the called tree.
	 */
	AST_LIST_LOCK(interface_tree);
	AST_LIST_TRAVERSE(interface_tree, device_monitor, next) {
		if (!strcmp(device_monitor->interface->device_name, device_name) &&
				!strcmp(device_monitor->interface->monitor_type, monitor_type)) {
			/* BOOM! Device is in the tree! We have a winner! */
			*core_id = core_id_candidate;
			AST_LIST_UNLOCK(interface_tree);
			return 1;
		}
	}
	AST_LIST_UNLOCK(interface_tree);
	return 0;
}

struct ast_cc_monitor *ast_cc_get_monitor_by_recall_core_id(const int core_id, const char * const device_name)
{
	struct cc_core_instance *core_instance = find_cc_core_instance(core_id);
	struct ast_cc_monitor *monitor_iter;

	if (!core_instance) {
		return NULL;
	}

	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE(core_instance->monitors, monitor_iter, next) {
		if (!strcmp(monitor_iter->interface->device_name, device_name)) {
			/* Found a monitor. */
			cc_ref(monitor_iter, "Hand the requester of the monitor a reference");
			break;
		}
	}
	AST_LIST_UNLOCK(core_instance->monitors);
	cc_unref(core_instance, "Done with core instance ref in ast_cc_get_monitor_by_recall_core_id");
	return monitor_iter;
}

/*!
 * \internal
 * \brief uniquely append a dialstring to our CC_INTERFACES chanvar string.
 *
 * We will only append a string if it has not already appeared in our channel
 * variable earlier. We ensure that we don't erroneously match substrings by
 * adding an ampersand to the end of our potential dialstring and searching for
 * it plus the ampersand in our variable.
 *
 * It's important to note that once we have built the full CC_INTERFACES string,
 * there will be an extra ampersand at the end which must be stripped off by
 * the caller of this function.
 *
 * \param str An ast_str holding what we will add to CC_INTERFACES
 * \param dialstring A new dialstring to add
 * \retval void
 */
static void cc_unique_append(struct ast_str **str, const char *dialstring)
{
	char dialstring_search[AST_CHANNEL_NAME];

	if (ast_strlen_zero(dialstring)) {
		/* No dialstring to append. */
		return;
	}
	snprintf(dialstring_search, sizeof(dialstring_search), "%s%c", dialstring, '&');
	if (strstr(ast_str_buffer(*str), dialstring_search)) {
		return;
	}
	ast_str_append(str, 0, "%s", dialstring_search);
}

/*!
 * \internal
 * \brief Build the CC_INTERFACES channel variable
 *
 * The method used is to traverse the child dialstrings in the
 * passed-in extension monitor, adding any that have the is_valid
 * flag set. Then, traverse the monitors, finding all children
 * of the starting extension monitor and adding their dialstrings
 * as well.
 *
 * \param starting_point The extension monitor that is the parent to all
 * monitors whose dialstrings should be added to CC_INTERFACES
 * \param str Where we will store CC_INTERFACES
 * \retval void
 */
static void build_cc_interfaces_chanvar(struct ast_cc_monitor *starting_point, struct ast_str **str)
{
	struct extension_monitor_pvt *extension_pvt;
	struct extension_child_dialstring *child_dialstring;
	struct ast_cc_monitor *monitor_iter = starting_point;
	int top_level_id = starting_point->id;
	size_t length;

	/* Init to an empty string. */
	ast_str_truncate(*str, 0);

	/* First we need to take all of the is_valid child_dialstrings from
	 * the extension monitor we found and add them to the CC_INTERFACES
	 * chanvar
	 */
	extension_pvt = starting_point->private_data;
	AST_LIST_TRAVERSE(&extension_pvt->child_dialstrings, child_dialstring, next) {
		if (child_dialstring->is_valid) {
			cc_unique_append(str, child_dialstring->original_dialstring);
		}
	}

	/* And now we get the dialstrings from each of the device monitors */
	while ((monitor_iter = AST_LIST_NEXT(monitor_iter, next))) {
		if (monitor_iter->parent_id == top_level_id) {
			cc_unique_append(str, monitor_iter->dialstring);
		}
	}

	/* str will have an extra '&' tacked onto the end of it, so we need
	 * to get rid of that.
	 */
	length = ast_str_strlen(*str);
	if (length) {
		ast_str_truncate(*str, length - 1);
	}
	if (length <= 1) {
		/* Nothing to recall?  This should not happen. */
		ast_log(LOG_ERROR, "CC_INTERFACES is empty. starting device_name:'%s'\n",
			starting_point->interface->device_name);
	}
}

int ast_cc_agent_set_interfaces_chanvar(struct ast_channel *chan)
{
	struct ast_datastore *recall_datastore;
	struct cc_monitor_tree *interface_tree;
	struct ast_cc_monitor *monitor;
	struct cc_recall_ds_data *recall_data;
	struct ast_str *str = ast_str_create(64);
	int core_id;

	if (!str) {
		return -1;
	}

	ast_channel_lock(chan);
	if (!(recall_datastore = ast_channel_datastore_find(chan, &recall_ds_info, NULL))) {
		ast_channel_unlock(chan);
		ast_free(str);
		return -1;
	}
	recall_data = recall_datastore->data;
	interface_tree = recall_data->interface_tree;
	core_id = recall_data->core_id;
	ast_channel_unlock(chan);

	AST_LIST_LOCK(interface_tree);
	monitor = AST_LIST_FIRST(interface_tree);
	build_cc_interfaces_chanvar(monitor, &str);
	AST_LIST_UNLOCK(interface_tree);

	pbx_builtin_setvar_helper(chan, "CC_INTERFACES", ast_str_buffer(str));
	ast_log_dynamic_level(cc_logger_level, "Core %d: CC_INTERFACES set to %s\n",
			core_id, ast_str_buffer(str));

	ast_free(str);
	return 0;
}

int ast_set_cc_interfaces_chanvar(struct ast_channel *chan, const char * const extension)
{
	struct ast_datastore *recall_datastore;
	struct cc_monitor_tree *interface_tree;
	struct ast_cc_monitor *monitor_iter;
	struct cc_recall_ds_data *recall_data;
	struct ast_str *str = ast_str_create(64);
	int core_id;

	if (!str) {
		return -1;
	}

	ast_channel_lock(chan);
	if (!(recall_datastore = ast_channel_datastore_find(chan, &recall_ds_info, NULL))) {
		ast_channel_unlock(chan);
		ast_free(str);
		return -1;
	}
	recall_data = recall_datastore->data;
	interface_tree = recall_data->interface_tree;
	core_id = recall_data->core_id;
	ast_channel_unlock(chan);

	AST_LIST_LOCK(interface_tree);
	AST_LIST_TRAVERSE(interface_tree, monitor_iter, next) {
		if (!strcmp(monitor_iter->interface->device_name, extension)) {
			break;
		}
	}

	if (!monitor_iter) {
		/* We couldn't find this extension. This may be because
		 * we have been directed into an unexpected extension because
		 * the admin has changed a CC_INTERFACES variable at some point.
		 */
		AST_LIST_UNLOCK(interface_tree);
		ast_free(str);
		return -1;
	}

	build_cc_interfaces_chanvar(monitor_iter, &str);
	AST_LIST_UNLOCK(interface_tree);

	pbx_builtin_setvar_helper(chan, "CC_INTERFACES", ast_str_buffer(str));
	ast_log_dynamic_level(cc_logger_level, "Core %d: CC_INTERFACES set to %s\n",
			core_id, ast_str_buffer(str));

	ast_free(str);
	return 0;
}

void ast_ignore_cc(struct ast_channel *chan)
{
	struct ast_datastore *cc_datastore;
	struct ast_datastore *cc_recall_datastore;
	struct dialed_cc_interfaces *cc_interfaces;
	struct cc_recall_ds_data *recall_cc_data;

	ast_channel_lock(chan);
	if ((cc_datastore = ast_channel_datastore_find(chan, &dialed_cc_interfaces_info, NULL))) {
		cc_interfaces = cc_datastore->data;
		cc_interfaces->ignore = 1;
	}

	if ((cc_recall_datastore = ast_channel_datastore_find(chan, &recall_ds_info, NULL))) {
		recall_cc_data = cc_recall_datastore->data;
		recall_cc_data->ignore = 1;
	}
	ast_channel_unlock(chan);
}

static __attribute__((format(printf, 2, 3))) int cc_offer(const int core_id, const char * const debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_CALLER_OFFERED, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_offer(struct ast_channel *caller_chan)
{
	int core_id;
	int res = -1;
	struct ast_datastore *datastore;
	struct dialed_cc_interfaces *cc_interfaces;
	char cc_is_offerable;

	ast_channel_lock(caller_chan);
	if (!(datastore = ast_channel_datastore_find(caller_chan, &dialed_cc_interfaces_info, NULL))) {
		ast_channel_unlock(caller_chan);
		return res;
	}

	cc_interfaces = datastore->data;
	cc_is_offerable = cc_interfaces->is_original_caller;
	core_id = cc_interfaces->core_id;
	ast_channel_unlock(caller_chan);

	if (cc_is_offerable) {
		res = cc_offer(core_id, "CC offered to caller %s", ast_channel_name(caller_chan));
	}
	return res;
}

int ast_cc_agent_accept_request(int core_id, const char * const debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_CALLER_REQUESTED, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_monitor_request_acked(int core_id, const char * const debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_ACTIVE, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_monitor_callee_available(const int core_id, const char * const debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_CALLEE_READY, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_agent_caller_busy(int core_id, const char * debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_CALLER_BUSY, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_agent_caller_available(int core_id, const char * const debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_ACTIVE, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_agent_recalling(int core_id, const char * const debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_RECALLING, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_completed(struct ast_channel *chan, const char * const debug, ...)
{
	struct ast_datastore *recall_datastore;
	struct cc_recall_ds_data *recall_data;
	int core_id;
	va_list ap;
	int res;

	ast_channel_lock(chan);
	if (!(recall_datastore = ast_channel_datastore_find(chan, &recall_ds_info, NULL))) {
		/* Silly! Why did you call this function if there's no recall DS? */
		ast_channel_unlock(chan);
		return -1;
	}
	recall_data = recall_datastore->data;
	if (recall_data->nested || recall_data->ignore) {
		/* If this is being called from a nested Dial, it is too
		 * early to determine if the recall has actually completed.
		 * The outermost dial is the only one with the authority to
		 * declare the recall to be complete.
		 *
		 * Similarly, if this function has been called when the
		 * recall has progressed beyond the first dial, this is not
		 * a legitimate time to declare the recall to be done. In fact,
		 * that should have been done already.
		 */
		ast_channel_unlock(chan);
		return -1;
	}
	core_id = recall_data->core_id;
	ast_channel_unlock(chan);
	va_start(ap, debug);
	res = cc_request_state_change(CC_COMPLETE, core_id, debug, ap);
	va_end(ap);
	return res;
}

int ast_cc_failed(int core_id, const char * const debug, ...)
{
	va_list ap;
	int res;

	va_start(ap, debug);
	res = cc_request_state_change(CC_FAILED, core_id, debug, ap);
	va_end(ap);
	return res;
}

struct ast_cc_monitor_failure_data {
	const char *device_name;
	char *debug;
	int core_id;
};

static int cc_monitor_failed(void *data)
{
	struct ast_cc_monitor_failure_data *failure_data = data;
	struct cc_core_instance *core_instance;
	struct ast_cc_monitor *monitor_iter;

	core_instance = find_cc_core_instance(failure_data->core_id);
	if (!core_instance) {
		/* Core instance no longer exists or invalid core_id. */
		ast_log_dynamic_level(cc_logger_level,
			"Core %d: Could not find core instance for device %s '%s'\n",
			failure_data->core_id, failure_data->device_name, failure_data->debug);
		ast_free((char *) failure_data->device_name);
		ast_free((char *) failure_data->debug);
		ast_free(failure_data);
		return -1;
	}

	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE_SAFE_BEGIN(core_instance->monitors, monitor_iter, next) {
		if (monitor_iter->interface->monitor_class == AST_CC_DEVICE_MONITOR) {
			if (!strcmp(monitor_iter->interface->device_name, failure_data->device_name)) {
				AST_LIST_REMOVE_CURRENT(next);
				cc_extension_monitor_change_is_valid(core_instance, monitor_iter->parent_id,
						monitor_iter->interface->device_name, 1);
				monitor_iter->callbacks->cancel_available_timer(monitor_iter, &monitor_iter->available_timer_id);
				cc_publish_monitorfailed(monitor_iter->core_id, monitor_iter->interface->device_name);
				cc_unref(monitor_iter, "Monitor reported failure. Unref list's reference.");
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!has_device_monitors(core_instance)) {
		ast_cc_failed(core_instance->core_id, "All monitors have failed\n");
	}
	AST_LIST_UNLOCK(core_instance->monitors);
	cc_unref(core_instance, "Finished with core_instance in cc_monitor_failed\n");

	ast_free((char *) failure_data->device_name);
	ast_free((char *) failure_data->debug);
	ast_free(failure_data);
	return 0;
}

int ast_cc_monitor_failed(int core_id, const char *const monitor_name, const char * const debug, ...)
{
	struct ast_cc_monitor_failure_data *failure_data;
	int res;
	va_list ap;

	if (!(failure_data = ast_calloc(1, sizeof(*failure_data)))) {
		return -1;
	}

	if (!(failure_data->device_name = ast_strdup(monitor_name))) {
		ast_free(failure_data);
		return -1;
	}

	va_start(ap, debug);
	if (ast_vasprintf(&failure_data->debug, debug, ap) == -1) {
		va_end(ap);
		ast_free((char *)failure_data->device_name);
		ast_free(failure_data);
		return -1;
	}
	va_end(ap);

	failure_data->core_id = core_id;

	res = ast_taskprocessor_push(cc_core_taskprocessor, cc_monitor_failed, failure_data);
	if (res) {
		ast_free((char *)failure_data->device_name);
		ast_free((char *)failure_data->debug);
		ast_free(failure_data);
	}
	return res;
}

static int cc_status_request(void *data)
{
	struct cc_core_instance *core_instance= data;
	int res;

	res = core_instance->agent->callbacks->status_request(core_instance->agent);
	cc_unref(core_instance, "Status request finished. Unref core instance");
	return res;
}

int ast_cc_monitor_status_request(int core_id)
{
	int res;
	struct cc_core_instance *core_instance = find_cc_core_instance(core_id);

	if (!core_instance) {
		return -1;
	}

	res = ast_taskprocessor_push(cc_core_taskprocessor, cc_status_request, core_instance);
	if (res) {
		cc_unref(core_instance, "Unref core instance. ast_taskprocessor_push failed");
	}
	return res;
}

static int cc_stop_ringing(void *data)
{
	struct cc_core_instance *core_instance = data;
	int res = 0;

	if (core_instance->agent->callbacks->stop_ringing) {
		res = core_instance->agent->callbacks->stop_ringing(core_instance->agent);
	}
	/* If an agent is being asked to stop ringing, then he needs to be prepared if for
	 * whatever reason he needs to be called back again. The proper state to be in to
	 * detect such a circumstance is the CC_ACTIVE state.
	 *
	 * We get to this state using the slightly unintuitive method of calling
	 * ast_cc_monitor_request_acked because it gets us to the proper state.
	 */
	ast_cc_monitor_request_acked(core_instance->core_id, "Agent %s asked to stop ringing. Be prepared to be recalled again.",
			core_instance->agent->device_name);
	cc_unref(core_instance, "Stop ringing finished. Unref core_instance");
	return res;
}

int ast_cc_monitor_stop_ringing(int core_id)
{
	int res;
	struct cc_core_instance *core_instance = find_cc_core_instance(core_id);

	if (!core_instance) {
		return -1;
	}

	res = ast_taskprocessor_push(cc_core_taskprocessor, cc_stop_ringing, core_instance);
	if (res) {
		cc_unref(core_instance, "Unref core instance. ast_taskprocessor_push failed");
	}
	return res;
}

static int cc_party_b_free(void *data)
{
	struct cc_core_instance *core_instance = data;
	int res = 0;

	if (core_instance->agent->callbacks->party_b_free) {
		res = core_instance->agent->callbacks->party_b_free(core_instance->agent);
	}
	cc_unref(core_instance, "Party B free finished. Unref core_instance");
	return res;
}

int ast_cc_monitor_party_b_free(int core_id)
{
	int res;
	struct cc_core_instance *core_instance = find_cc_core_instance(core_id);

	if (!core_instance) {
		return -1;
	}

	res = ast_taskprocessor_push(cc_core_taskprocessor, cc_party_b_free, core_instance);
	if (res) {
		cc_unref(core_instance, "Unref core instance. ast_taskprocessor_push failed");
	}
	return res;
}

struct cc_status_response_args {
	struct cc_core_instance *core_instance;
	enum ast_device_state devstate;
};

static int cc_status_response(void *data)
{
	struct cc_status_response_args *args = data;
	struct cc_core_instance *core_instance = args->core_instance;
	struct ast_cc_monitor *monitor_iter;
	enum ast_device_state devstate = args->devstate;

	ast_free(args);

	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE(core_instance->monitors, monitor_iter, next) {
		if (monitor_iter->interface->monitor_class == AST_CC_DEVICE_MONITOR &&
				monitor_iter->callbacks->status_response) {
			monitor_iter->callbacks->status_response(monitor_iter, devstate);
		}
	}
	AST_LIST_UNLOCK(core_instance->monitors);
	cc_unref(core_instance, "Status response finished. Unref core instance");
	return 0;
}

int ast_cc_agent_status_response(int core_id, enum ast_device_state devstate)
{
	struct cc_status_response_args *args;
	struct cc_core_instance *core_instance;
	int res;

	args = ast_calloc(1, sizeof(*args));
	if (!args) {
		return -1;
	}

	core_instance = find_cc_core_instance(core_id);
	if (!core_instance) {
		ast_free(args);
		return -1;
	}

	args->core_instance = core_instance;
	args->devstate = devstate;

	res = ast_taskprocessor_push(cc_core_taskprocessor, cc_status_response, args);
	if (res) {
		cc_unref(core_instance, "Unref core instance. ast_taskprocessor_push failed");
		ast_free(args);
	}
	return res;
}

static int cc_build_payload(struct ast_channel *chan, struct ast_cc_config_params *cc_params,
	const char *monitor_type, const char * const device_name, const char * dialstring,
	enum ast_cc_service_type service, void *private_data, struct cc_control_payload *payload)
{
	struct ast_datastore *datastore;
	struct dialed_cc_interfaces *cc_interfaces;
	int dial_parent_id;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &dialed_cc_interfaces_info, NULL);
	if (!datastore) {
		ast_channel_unlock(chan);
		return -1;
	}
	cc_interfaces = datastore->data;
	dial_parent_id = cc_interfaces->dial_parent_id;
	ast_channel_unlock(chan);

	payload->monitor_type = monitor_type;
	payload->private_data = private_data;
	payload->service = service;
	ast_cc_copy_config_params(&payload->config_params, cc_params);
	payload->parent_interface_id = dial_parent_id;
	ast_copy_string(payload->device_name, device_name, sizeof(payload->device_name));
	ast_copy_string(payload->dialstring, dialstring, sizeof(payload->dialstring));
	return 0;
}

int ast_queue_cc_frame(struct ast_channel *chan, const char *monitor_type,
		const char * const dialstring, enum ast_cc_service_type service, void *private_data)
{
	struct ast_frame frame = {0,};
	char device_name[AST_CHANNEL_NAME];
	int retval;
	struct ast_cc_config_params *cc_params;

	cc_params = ast_channel_get_cc_config_params(chan);
	if (!cc_params) {
		return -1;
	}
	ast_channel_get_device_name(chan, device_name, sizeof(device_name));
	if (ast_cc_monitor_count(device_name, monitor_type) >= ast_get_cc_max_monitors(cc_params)) {
		ast_log(LOG_NOTICE, "Not queuing a CC frame for device %s since it already has its maximum monitors allocated\n", device_name);
		return -1;
	}

	if (ast_cc_build_frame(chan, cc_params, monitor_type, device_name, dialstring, service, private_data, &frame)) {
		/* Frame building failed. We can't use this. */
		return -1;
	}
	retval = ast_queue_frame(chan, &frame);
	ast_frfree(&frame);
	return retval;
}

int ast_cc_build_frame(struct ast_channel *chan, struct ast_cc_config_params *cc_params,
	const char *monitor_type, const char * const device_name,
	const char * const dialstring, enum ast_cc_service_type service, void *private_data,
	struct ast_frame *frame)
{
	struct cc_control_payload *payload = ast_calloc(1, sizeof(*payload));

	if (!payload) {
		return -1;
	}
	if (cc_build_payload(chan, cc_params, monitor_type, device_name, dialstring, service, private_data, payload)) {
		/* Something screwed up, we can't make a frame with this */
		ast_free(payload);
		return -1;
	}
	frame->frametype = AST_FRAME_CONTROL;
	frame->subclass.integer = AST_CONTROL_CC;
	frame->data.ptr = payload;
	frame->datalen = sizeof(*payload);
	frame->mallocd = AST_MALLOCD_DATA;
	return 0;
}

void ast_cc_call_failed(struct ast_channel *incoming, struct ast_channel *outgoing, const char * const dialstring)
{
	char device_name[AST_CHANNEL_NAME];
	struct cc_control_payload payload;
	struct ast_cc_config_params *cc_params;

	if (ast_channel_hangupcause(outgoing) != AST_CAUSE_BUSY && ast_channel_hangupcause(outgoing) != AST_CAUSE_CONGESTION) {
		/* It doesn't make sense to try to offer CCBS to the caller if the reason for ast_call
		 * failing is something other than busy or congestion
		 */
		return;
	}

	cc_params = ast_channel_get_cc_config_params(outgoing);
	if (!cc_params) {
		return;
	}
	if (ast_get_cc_monitor_policy(cc_params) != AST_CC_MONITOR_GENERIC) {
		/* This sort of CCBS only works if using generic CC. For native, we would end up sending
		 * a CC request for a non-existent call. The far end will reject this every time
		 */
		return;
	}

	ast_channel_get_device_name(outgoing, device_name, sizeof(device_name));
	if (cc_build_payload(outgoing, cc_params, AST_CC_GENERIC_MONITOR_TYPE, device_name,
		dialstring, AST_CC_CCBS, NULL, &payload)) {
		/* Something screwed up, we can't make a frame with this */
		return;
	}
	ast_handle_cc_control_frame(incoming, outgoing, &payload);
}

void ast_cc_busy_interface(struct ast_channel *inbound, struct ast_cc_config_params *cc_params,
	const char *monitor_type, const char * const device_name, const char * const dialstring, void *private_data)
{
	struct cc_control_payload payload;
	if (cc_build_payload(inbound, cc_params, monitor_type, device_name, dialstring, AST_CC_CCBS, private_data, &payload)) {
		/* Something screwed up. Don't try to handle this payload */
		call_destructor_with_no_monitor(monitor_type, private_data);
		return;
	}
	ast_handle_cc_control_frame(inbound, NULL, &payload);
}

int ast_cc_callback(struct ast_channel *inbound, const char * const tech, const char * const dest, ast_cc_callback_fn callback)
{
	const struct ast_channel_tech *chantech = ast_get_channel_tech(tech);

	if (chantech && chantech->cc_callback) {
		chantech->cc_callback(inbound, dest, callback);
	}

	return 0;
}

static const char *ccreq_app = "CallCompletionRequest";

static int ccreq_exec(struct ast_channel *chan, const char *data)
{
	struct cc_core_instance *core_instance;
	char device_name[AST_CHANNEL_NAME];
	unsigned long match_flags;
	int res;

	ast_channel_get_device_name(chan, device_name, sizeof(device_name));

	match_flags = MATCH_NO_REQUEST;
	if (!(core_instance = ao2_t_callback_data(cc_core_instances, 0, match_agent, device_name, &match_flags, "Find core instance for CallCompletionRequest"))) {
		ast_log_dynamic_level(cc_logger_level, "Couldn't find a core instance for caller %s\n", device_name);
		pbx_builtin_setvar_helper(chan, "CC_REQUEST_RESULT", "FAIL");
		pbx_builtin_setvar_helper(chan, "CC_REQUEST_REASON", "NO_CORE_INSTANCE");
		return 0;
	}

	ast_log_dynamic_level(cc_logger_level, "Core %d: Found core_instance for caller %s\n",
			core_instance->core_id, device_name);

	if (strcmp(core_instance->agent->callbacks->type, "generic")) {
		ast_log_dynamic_level(cc_logger_level, "Core %d: CallCompletionRequest is only for generic agent types.\n",
				core_instance->core_id);
		pbx_builtin_setvar_helper(chan, "CC_REQUEST_RESULT", "FAIL");
		pbx_builtin_setvar_helper(chan, "CC_REQUEST_REASON", "NOT_GENERIC");
		cc_unref(core_instance, "Unref core_instance since CallCompletionRequest was called with native agent");
		return 0;
	}

	if (!ast_cc_request_is_within_limits()) {
		ast_log_dynamic_level(cc_logger_level, "Core %d: CallCompletionRequest failed. Too many requests in the system\n",
				core_instance->core_id);
		ast_cc_failed(core_instance->core_id, "Too many CC requests\n");
		pbx_builtin_setvar_helper(chan, "CC_REQUEST_RESULT", "FAIL");
		pbx_builtin_setvar_helper(chan, "CC_REQUEST_REASON", "TOO_MANY_REQUESTS");
		cc_unref(core_instance, "Unref core_instance since too many CC requests");
		return 0;
	}

	res = ast_cc_agent_accept_request(core_instance->core_id, "CallCompletionRequest called by caller %s for core_id %d", device_name, core_instance->core_id);
	pbx_builtin_setvar_helper(chan, "CC_REQUEST_RESULT", res ? "FAIL" : "SUCCESS");
	if (res) {
		pbx_builtin_setvar_helper(chan, "CC_REQUEST_REASON", "UNSPECIFIED");
	}

	cc_unref(core_instance, "Done with CallCompletionRequest");
	return 0;
}

static const char *cccancel_app = "CallCompletionCancel";

static int cccancel_exec(struct ast_channel *chan, const char *data)
{
	struct cc_core_instance *core_instance;
	char device_name[AST_CHANNEL_NAME];
	unsigned long match_flags;
	int res;

	ast_channel_get_device_name(chan, device_name, sizeof(device_name));

	match_flags = MATCH_REQUEST;
	if (!(core_instance = ao2_t_callback_data(cc_core_instances, 0, match_agent, device_name, &match_flags, "Find core instance for CallCompletionCancel"))) {
		ast_log_dynamic_level(cc_logger_level, "Cannot find CC transaction to cancel for caller %s\n", device_name);
		pbx_builtin_setvar_helper(chan, "CC_CANCEL_RESULT", "FAIL");
		pbx_builtin_setvar_helper(chan, "CC_CANCEL_REASON", "NO_CORE_INSTANCE");
		return 0;
	}

	if (strcmp(core_instance->agent->callbacks->type, "generic")) {
		ast_log(LOG_WARNING, "CallCompletionCancel may only be used for calles with a generic agent\n");
		cc_unref(core_instance, "Unref core instance found during CallCompletionCancel");
		pbx_builtin_setvar_helper(chan, "CC_CANCEL_RESULT", "FAIL");
		pbx_builtin_setvar_helper(chan, "CC_CANCEL_REASON", "NOT_GENERIC");
		return 0;
	}
	res = ast_cc_failed(core_instance->core_id, "Call completion request Cancelled for core ID %d by caller %s",
			core_instance->core_id, device_name);
	cc_unref(core_instance, "Unref core instance found during CallCompletionCancel");
	pbx_builtin_setvar_helper(chan, "CC_CANCEL_RESULT", res ? "FAIL" : "SUCCESS");
	if (res) {
		pbx_builtin_setvar_helper(chan, "CC_CANCEL_REASON", "UNSPECIFIED");
	}
	return 0;
}

struct count_monitors_cb_data {
	const char *device_name;
	const char *monitor_type;
	int count;
};

static int count_monitors_cb(void *obj, void *arg, int flags)
{
	struct cc_core_instance *core_instance = obj;
	struct count_monitors_cb_data *cb_data = arg;
	const char *device_name = cb_data->device_name;
	const char *monitor_type = cb_data->monitor_type;
	struct ast_cc_monitor *monitor_iter;

	AST_LIST_LOCK(core_instance->monitors);
	AST_LIST_TRAVERSE(core_instance->monitors, monitor_iter, next) {
		if (!strcmp(monitor_iter->interface->device_name, device_name) &&
				!strcmp(monitor_iter->interface->monitor_type, monitor_type)) {
			cb_data->count++;
			break;
		}
	}
	AST_LIST_UNLOCK(core_instance->monitors);
	return 0;
}

int ast_cc_monitor_count(const char * const name, const char * const type)
{
	struct count_monitors_cb_data data = {.device_name = name, .monitor_type = type,};

	ao2_t_callback(cc_core_instances, OBJ_NODATA, count_monitors_cb, &data, "Counting agents");
	ast_log_dynamic_level(cc_logger_level, "Counted %d monitors\n", data.count);
	return data.count;
}

static void initialize_cc_max_requests(void)
{
	struct ast_config *cc_config;
	const char *cc_max_requests_str;
	struct ast_flags config_flags = {0,};
	char *endptr;

	cc_config = ast_config_load2("ccss.conf", "ccss", config_flags);
	if (!cc_config || cc_config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Could not find valid ccss.conf file. Using cc_max_requests default\n");
		global_cc_max_requests = GLOBAL_CC_MAX_REQUESTS_DEFAULT;
		return;
	}

	if (!(cc_max_requests_str = ast_variable_retrieve(cc_config, "general", "cc_max_requests"))) {
		ast_config_destroy(cc_config);
		global_cc_max_requests = GLOBAL_CC_MAX_REQUESTS_DEFAULT;
		return;
	}

	global_cc_max_requests = strtol(cc_max_requests_str, &endptr, 10);

	if (!ast_strlen_zero(endptr)) {
		ast_log(LOG_WARNING, "Invalid input given for cc_max_requests. Using default\n");
		global_cc_max_requests = GLOBAL_CC_MAX_REQUESTS_DEFAULT;
	}

	ast_config_destroy(cc_config);
	return;
}

/*!
 * \internal
 * \brief helper function to parse and configure each devstate map
 */
static void initialize_cc_devstate_map_helper(struct ast_config *cc_config, enum cc_state state, const char *cc_setting)
{
	const char *cc_devstate_str;
	enum ast_device_state this_devstate;

	if ((cc_devstate_str = ast_variable_retrieve(cc_config, "general", cc_setting))) {
		this_devstate = ast_devstate_val(cc_devstate_str);
		if (this_devstate != AST_DEVICE_UNKNOWN) {
			cc_state_to_devstate_map[state] = this_devstate;
		}
	}
}

/*!
 * \internal
 * \brief initializes cc_state_to_devstate_map from ccss.conf
 *
 * \details
 * The cc_state_to_devstate_map[] is already initialized with all the
 * default values. This will update that structure with any changes
 * from the ccss.conf file. The configuration parameters in ccss.conf
 * should use any valid device state form that is recognized by
 * ast_devstate_val() function.
 */
static void initialize_cc_devstate_map(void)
{
	struct ast_config *cc_config;
	struct ast_flags config_flags = { 0, };

	cc_config = ast_config_load2("ccss.conf", "ccss", config_flags);
	if (!cc_config || cc_config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING,
			"Could not find valid ccss.conf file. Using cc_[state]_devstate defaults\n");
		return;
	}

	initialize_cc_devstate_map_helper(cc_config, CC_AVAILABLE, "cc_available_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_CALLER_OFFERED, "cc_caller_offered_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_CALLER_REQUESTED, "cc_caller_requested_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_ACTIVE, "cc_active_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_CALLEE_READY, "cc_callee_ready_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_CALLER_BUSY, "cc_caller_busy_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_RECALLING, "cc_recalling_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_COMPLETE, "cc_complete_devstate");
	initialize_cc_devstate_map_helper(cc_config, CC_FAILED, "cc_failed_devstate");

	ast_config_destroy(cc_config);
}

static void cc_cli_print_monitor_stats(struct ast_cc_monitor *monitor, int fd, int parent_id)
{
	struct ast_cc_monitor *child_monitor_iter = monitor;
	if (!monitor) {
		return;
	}

	ast_cli(fd, "\t\t|-->%s", monitor->interface->device_name);
	if (monitor->interface->monitor_class == AST_CC_DEVICE_MONITOR) {
		ast_cli(fd, "(%s)", cc_service_to_string(monitor->service_offered));
	}
	ast_cli(fd, "\n");

	while ((child_monitor_iter = AST_LIST_NEXT(child_monitor_iter, next))) {
		if (child_monitor_iter->parent_id == monitor->id) {
			cc_cli_print_monitor_stats(child_monitor_iter, fd, child_monitor_iter->id);
		}
	}
}

static int print_stats_cb(void *obj, void *arg, int flags)
{
	int *cli_fd = arg;
	struct cc_core_instance *core_instance = obj;

	ast_cli(*cli_fd, "%d\t\t%s\t\t%s\n", core_instance->core_id, core_instance->agent->device_name,
			cc_state_to_string(core_instance->current_state));
	AST_LIST_LOCK(core_instance->monitors);
	cc_cli_print_monitor_stats(AST_LIST_FIRST(core_instance->monitors), *cli_fd, 0);
	AST_LIST_UNLOCK(core_instance->monitors);
	return 0;
}

static int cc_cli_output_status(void *data)
{
	int *cli_fd = data;
	int count = ao2_container_count(cc_core_instances);

	if (!count) {
		ast_cli(*cli_fd, "There are currently no active call completion transactions\n");
	} else {
		ast_cli(*cli_fd, "%d Call completion transactions\n", count);
		ast_cli(*cli_fd, "Core ID\t\tCaller\t\t\t\tStatus\n");
		ast_cli(*cli_fd, "----------------------------------------------------------------------------\n");
		ao2_t_callback(cc_core_instances, OBJ_NODATA, print_stats_cb, cli_fd, "Printing stats to CLI");
	}
	ast_free(cli_fd);
	return 0;
}

static char *handle_cc_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int *cli_fd;

	switch (cmd) {
	case CLI_INIT:
		e->command = "cc report status";
		e->usage =
			"Usage: cc report status\n"
			"       Report the current status of any ongoing CC transactions\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	cli_fd = ast_malloc(sizeof(*cli_fd));
	if (!cli_fd) {
		return CLI_FAILURE;
	}

	*cli_fd = a->fd;

	if (ast_taskprocessor_push(cc_core_taskprocessor, cc_cli_output_status, cli_fd)) {
		ast_free(cli_fd);
		return CLI_FAILURE;
	}
	return CLI_SUCCESS;
}

static int kill_cores(void *obj, void *arg, int flags)
{
	int *core_id = arg;
	struct cc_core_instance *core_instance = obj;

	if (!core_id || (core_instance->core_id == *core_id)) {
		ast_cc_failed(core_instance->core_id, "CC transaction canceled administratively\n");
	}
	return 0;
}

static char *complete_core_id(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	int wordlen = strlen(word);
	char *ret = NULL;
	struct ao2_iterator core_iter = ao2_iterator_init(cc_core_instances, 0);
	struct cc_core_instance *core_instance;

	for (; (core_instance = ao2_t_iterator_next(&core_iter, "Next core instance"));
			cc_unref(core_instance, "CLI tab completion iteration")) {
		char core_id_str[20];
		snprintf(core_id_str, sizeof(core_id_str), "%d", core_instance->core_id);
		if (!strncmp(word, core_id_str, wordlen) && ++which > state) {
			ret = ast_strdup(core_id_str);
			cc_unref(core_instance, "Found a matching core ID for CLI tab-completion");
			break;
		}
	}
	ao2_iterator_destroy(&core_iter);

	return ret;
}

static char *handle_cc_kill(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	static const char * const option[] = { "core", "all", NULL };

	switch (cmd) {
	case CLI_INIT:
		e->command = "cc cancel";
		e->usage =
			"Usage: cc cancel can be used in two ways.\n"
			"       1. 'cc cancel core [core ID]' will cancel the CC transaction with\n"
			"          core ID equal to the specified core ID.\n"
			"       2. 'cc cancel all' will cancel all active CC transactions.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return ast_cli_complete(a->word, option, a->n);
		}
		if (a->pos == 3) {
			return complete_core_id(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc == 4) {
		int core_id;
		char *endptr;
		if (strcasecmp(a->argv[2], "core")) {
			return CLI_SHOWUSAGE;
		}
		core_id = strtol(a->argv[3], &endptr, 10);
		if ((errno != 0 && core_id == 0) || (endptr == a->argv[3])) {
			return CLI_SHOWUSAGE;
		}
		ao2_t_callback(cc_core_instances, OBJ_NODATA, kill_cores, &core_id, "CLI Killing Core Id");
	} else if (a->argc == 3) {
		if (strcasecmp(a->argv[2], "all")) {
			return CLI_SHOWUSAGE;
		}
		ao2_t_callback(cc_core_instances, OBJ_NODATA, kill_cores, NULL, "CLI Killing all CC cores");
	} else {
		return CLI_SHOWUSAGE;
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cc_cli[] = {
	AST_CLI_DEFINE(handle_cc_status, "Reports CC stats"),
	AST_CLI_DEFINE(handle_cc_kill, "Kill a CC transaction"),
};

static void cc_shutdown(void)
{
	ast_devstate_prov_del("ccss");
	ast_cc_agent_unregister(&generic_agent_callbacks);
	ast_cc_monitor_unregister(&generic_monitor_cbs);
	ast_unregister_application(cccancel_app);
	ast_unregister_application(ccreq_app);
	ast_logger_unregister_level(CC_LOGGER_LEVEL_NAME);
	ast_cli_unregister_multiple(cc_cli, ARRAY_LEN(cc_cli));

	if (cc_sched_context) {
		ast_sched_context_destroy(cc_sched_context);
		cc_sched_context = NULL;
	}
	if (cc_core_taskprocessor) {
		cc_core_taskprocessor = ast_taskprocessor_unreference(cc_core_taskprocessor);
	}
	/* Note that core instances must be destroyed prior to the generic_monitors */
	if (cc_core_instances) {
		ao2_t_ref(cc_core_instances, -1, "Unref cc_core_instances container in cc_shutdown");
		cc_core_instances = NULL;
	}
	if (generic_monitors) {
		ao2_t_ref(generic_monitors, -1, "Unref generic_monitor container in cc_shutdown");
		generic_monitors = NULL;
	}
}

int ast_cc_init(void)
{
	int res;

	if (!(cc_core_instances = ao2_t_container_alloc(CC_CORE_INSTANCES_BUCKETS,
					cc_core_instance_hash_fn, cc_core_instance_cmp_fn,
					"Create core instance container"))) {
		return -1;
	}
	if (!(generic_monitors = ao2_t_container_alloc(CC_CORE_INSTANCES_BUCKETS,
					generic_monitor_hash_fn, generic_monitor_cmp_fn,
					"Create generic monitor container"))) {
		return -1;
	}
	if (!(cc_core_taskprocessor = ast_taskprocessor_get("CCSS core", TPS_REF_DEFAULT))) {
		return -1;
	}
	if (!(cc_sched_context = ast_sched_context_create())) {
		return -1;
	}
	if (ast_sched_start_thread(cc_sched_context)) {
		return -1;
	}
	res = ast_register_application2(ccreq_app, ccreq_exec, NULL, NULL, NULL);
	res |= ast_register_application2(cccancel_app, cccancel_exec, NULL, NULL, NULL);
	res |= ast_cc_monitor_register(&generic_monitor_cbs);
	res |= ast_cc_agent_register(&generic_agent_callbacks);

	ast_cli_register_multiple(cc_cli, ARRAY_LEN(cc_cli));
	cc_logger_level = ast_logger_register_level(CC_LOGGER_LEVEL_NAME);
	dialed_cc_interface_counter = 1;
	initialize_cc_max_requests();

	/* Read the map and register the device state callback for generic agents */
	initialize_cc_devstate_map();
	res |= ast_devstate_prov_add("ccss", ccss_device_state);

	ast_register_cleanup(cc_shutdown);

	return res;
}
