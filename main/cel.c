/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, Digium, Inc.
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

/*!
 * \file
 *
 * \brief Channel Event Logging API
 *
 * \author Steve Murphy <murf@digium.com>
 * \author Russell Bryant <russell@digium.com>
 */

/*! \li \ref cel.c uses the configuration file \ref cel.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cel.conf cel.conf
 * \verbinclude cel.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cel.h"
#include "asterisk/logger.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/bridge.h"
#include "asterisk/parking.h"
#include "asterisk/pickup.h"
#include "asterisk/core_local.h"
#include "asterisk/taskprocessor.h"

/*** DOCUMENTATION
	<configInfo name="cel" language="en_US">
		<configFile name="cel.conf">
			<configObject name="general">
				<synopsis>Options that apply globally to Channel Event Logging (CEL)</synopsis>
				<configOption name="enable">
					<synopsis>Determines whether CEL is enabled</synopsis>
				</configOption>
				<configOption name="dateformat">
					<synopsis>The format to be used for dates when logging</synopsis>
				</configOption>
				<configOption name="apps">
					<synopsis>List of apps for CEL to track</synopsis>
					<description><para>A case-insensitive, comma-separated list of applications
					to track when one or both of APP_START and APP_END events are flagged for
					tracking</para></description>
				</configOption>
				<configOption name="events">
					<synopsis>List of events for CEL to track</synopsis>
					<description><para>A case-sensitive, comma-separated list of event names
					to track. These event names do not include the leading <literal>AST_CEL</literal>.
					</para>
					<enumlist>
						<enum name="ALL">
							<para>Special value which tracks all events.</para>
						</enum>
						<enum name="CHAN_START"/>
						<enum name="CHAN_END"/>
						<enum name="ANSWER"/>
						<enum name="HANGUP"/>
						<enum name="APP_START"/>
						<enum name="APP_END"/>
						<enum name="PARK_START"/>
						<enum name="PARK_END"/>
						<enum name="USER_DEFINED"/>
						<enum name="BRIDGE_ENTER"/>
						<enum name="BRIDGE_EXIT"/>
						<enum name="BLINDTRANSFER"/>
						<enum name="ATTENDEDTRANSFER"/>
						<enum name="PICKUP"/>
						<enum name="FORWARD"/>
						<enum name="LINKEDID_END"/>
						<enum name="LOCAL_OPTIMIZE"/>
						<enum name="LOCAL_OPTIMIZE_BEGIN"/>
					</enumlist>
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! Message router for state that CEL needs to know about */
static struct stasis_message_router *cel_state_router;

/*! Topic for CEL-specific messages */
static struct stasis_topic *cel_topic;

/*! Aggregation topic for all topics CEL needs to know about */
static struct stasis_topic *cel_aggregation_topic;

/*! Subscription for forwarding the channel caching topic */
static struct stasis_forward *cel_channel_forwarder;

/*! Subscription for forwarding the channel caching topic */
static struct stasis_forward *cel_bridge_forwarder;

/*! Subscription for forwarding the parking topic */
static struct stasis_forward *cel_parking_forwarder;

/*! Subscription for forwarding the CEL-specific topic */
static struct stasis_forward *cel_cel_forwarder;

struct stasis_message_type *cel_generic_type(void);
STASIS_MESSAGE_TYPE_DEFN(cel_generic_type);

/*! Container for CEL backend information */
static AO2_GLOBAL_OBJ_STATIC(cel_backends);

/*! The number of buckets into which backend names will be hashed */
#define BACKEND_BUCKETS 13

/*! Container for dial end multichannel blobs for holding on to dial statuses */
static AO2_GLOBAL_OBJ_STATIC(cel_dialstatus_store);

/*!
 * \brief Maximum possible CEL event IDs
 * \note This limit is currently imposed by the eventset definition
 */
#define CEL_MAX_EVENT_IDS 64

/*!
 * \brief Number of buckets for the appset container
 */
#define NUM_APP_BUCKETS		97

/*!
 * \brief Number of buckets for the dialstatus container
 */
#define NUM_DIALSTATUS_BUCKETS	251

struct cel_linkedid {
	/*! Number of channels with this linkedid. */
	unsigned int count;
	/*! Linkedid stored at end of struct. */
	char id[0];
};

/*! Container of channel references to a linkedid for CEL purposes. */
static AO2_GLOBAL_OBJ_STATIC(cel_linkedids);

struct cel_dialstatus {
	/*! Uniqueid of the channel */
	char uniqueid[AST_MAX_UNIQUEID];
	/*! The dial status */
	char dialstatus[0];
};

/*! \brief Destructor for cel_config */
static void cel_general_config_dtor(void *obj)
{
	struct ast_cel_general_config *cfg = obj;
	ast_string_field_free_memory(cfg);
	ao2_cleanup(cfg->apps);
	cfg->apps = NULL;
}

void *ast_cel_general_config_alloc(void)
{
	RAII_VAR(struct ast_cel_general_config *, cfg, NULL, ao2_cleanup);

	if (!(cfg = ao2_alloc(sizeof(*cfg), cel_general_config_dtor))) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 64)) {
		return NULL;
	}

	if (!(cfg->apps = ast_str_container_alloc(NUM_APP_BUCKETS))) {
		return NULL;
	}

	ao2_ref(cfg, +1);
	return cfg;
}

/*! \brief A container that holds all config-related information */
struct cel_config {
	struct ast_cel_general_config *general;
};


static AO2_GLOBAL_OBJ_STATIC(cel_configs);

/*! \brief Destructor for cel_config */
static void cel_config_dtor(void *obj)
{
	struct cel_config *cfg = obj;
	ao2_cleanup(cfg->general);
	cfg->general = NULL;
}

static void *cel_config_alloc(void)
{
	RAII_VAR(struct cel_config *, cfg, NULL, ao2_cleanup);

	if (!(cfg = ao2_alloc(sizeof(*cfg), cel_config_dtor))) {
		return NULL;
	}

	if (!(cfg->general = ast_cel_general_config_alloc())) {
		return NULL;
	}

	ao2_ref(cfg, +1);
	return cfg;
}

/*! \brief An aco_type structure to link the "general" category to the ast_cel_general_config type */
static struct aco_type general_option = {
	.type = ACO_GLOBAL,
	.name = "general",
	.item_offset = offsetof(struct cel_config, general),
	.category_match = ACO_WHITELIST_EXACT,
	.category = "general",
};

/*! Config sections used by existing modules. Do not add to this list. */
static const char *ignore_categories[] = {
	"manager",
	"radius",
	NULL,
};

static struct aco_type ignore_option = {
	.type = ACO_IGNORE,
	.name = "modules",
	.category = (const char*)ignore_categories,
	.category_match = ACO_WHITELIST_ARRAY,
};

/*! \brief The config file to be processed for the module. */
static struct aco_file cel_conf = {
	.filename = "cel.conf",                  /*!< The name of the config file */
	.types = ACO_TYPES(&general_option, &ignore_option),     /*!< The mapping object types to be processed */
};

static int cel_pre_apply_config(void);

CONFIG_INFO_CORE("cel", cel_cfg_info, cel_configs, cel_config_alloc,
	.files = ACO_FILES(&cel_conf),
	.pre_apply_config = cel_pre_apply_config,
);

static int cel_pre_apply_config(void)
{
	struct cel_config *cfg = aco_pending_config(&cel_cfg_info);

	if (!cfg->general) {
		return -1;
	}

	if (!ao2_container_count(cfg->general->apps)) {
		return 0;
	}

	if (cfg->general->events & ((int64_t) 1 << AST_CEL_APP_START)) {
		return 0;
	}

	if (cfg->general->events & ((int64_t) 1 << AST_CEL_APP_END)) {
		return 0;
	}

	ast_log(LOG_ERROR, "Applications are listed to be tracked, but APP events are not tracked\n");
	return -1;
}

static struct aco_type *general_options[] = ACO_TYPES(&general_option);

/*!
 * \brief Map of ast_cel_event_type to strings
 */
static const char * const cel_event_types[CEL_MAX_EVENT_IDS] = {
	[AST_CEL_ALL]              = "ALL",
	[AST_CEL_CHANNEL_START]    = "CHAN_START",
	[AST_CEL_CHANNEL_END]      = "CHAN_END",
	[AST_CEL_ANSWER]           = "ANSWER",
	[AST_CEL_HANGUP]           = "HANGUP",
	[AST_CEL_APP_START]        = "APP_START",
	[AST_CEL_APP_END]          = "APP_END",
	[AST_CEL_PARK_START]       = "PARK_START",
	[AST_CEL_PARK_END]         = "PARK_END",
	[AST_CEL_USER_DEFINED]     = "USER_DEFINED",
	[AST_CEL_BRIDGE_ENTER]     = "BRIDGE_ENTER",
	[AST_CEL_BRIDGE_EXIT]      = "BRIDGE_EXIT",
	[AST_CEL_BLINDTRANSFER]    = "BLINDTRANSFER",
	[AST_CEL_ATTENDEDTRANSFER] = "ATTENDEDTRANSFER",
	[AST_CEL_PICKUP]           = "PICKUP",
	[AST_CEL_FORWARD]          = "FORWARD",
	[AST_CEL_LINKEDID_END]     = "LINKEDID_END",
	[AST_CEL_LOCAL_OPTIMIZE]   = "LOCAL_OPTIMIZE",
	[AST_CEL_LOCAL_OPTIMIZE_BEGIN]   = "LOCAL_OPTIMIZE_BEGIN",
};

struct cel_backend {
	ast_cel_backend_cb callback; /*!< Callback for this backend */
	char name[0];                /*!< Name of this backend */
};

/*! \brief Hashing function for cel_backend */
AO2_STRING_FIELD_HASH_FN(cel_backend, name)

/*! \brief Comparator function for cel_backend */
AO2_STRING_FIELD_CMP_FN(cel_backend, name)

/*! \brief Hashing function for dialstatus container */
AO2_STRING_FIELD_HASH_FN(cel_dialstatus, uniqueid)

/*! \brief Comparator function for dialstatus container */
AO2_STRING_FIELD_CMP_FN(cel_dialstatus, uniqueid)

unsigned int ast_cel_check_enabled(void)
{
	unsigned int enabled;
	struct cel_config *cfg = ao2_global_obj_ref(cel_configs);

	enabled = (!cfg || !cfg->general) ? 0 : cfg->general->enable;
	ao2_cleanup(cfg);
	return enabled;
}

static char *handle_cli_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	unsigned int i;
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);
	RAII_VAR(struct ao2_container *, backends, ao2_global_obj_ref(cel_backends), ao2_cleanup);
	struct ao2_iterator iter;
	char *app;

	switch (cmd) {
	case CLI_INIT:
		e->command = "cel show status";
		e->usage =
			"Usage: cel show status\n"
			"       Displays the Channel Event Logging system status.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	case CLI_HANDLER:
		break;
	}

	if (a->argc > 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "CEL Logging: %s\n", ast_cel_check_enabled() ? "Enabled" : "Disabled");

	if (!cfg || !cfg->general || !cfg->general->enable) {
		return CLI_SUCCESS;
	}

	for (i = 0; i < (sizeof(cfg->general->events) * 8); i++) {
		const char *name;

		if (!(cfg->general->events & ((int64_t) 1 << i))) {
			continue;
		}

		name = ast_cel_get_type_name(i);
		if (strcasecmp(name, "Unknown")) {
			ast_cli(a->fd, "CEL Tracking Event: %s\n", name);
		}
	}

	iter = ao2_iterator_init(cfg->general->apps, 0);
	for (; (app = ao2_iterator_next(&iter)); ao2_ref(app, -1)) {
		ast_cli(a->fd, "CEL Tracking Application: %s\n", app);
	}
	ao2_iterator_destroy(&iter);

	if (backends) {
		struct cel_backend *backend;

		iter = ao2_iterator_init(backends, 0);
		for (; (backend = ao2_iterator_next(&iter)); ao2_ref(backend, -1)) {
			ast_cli(a->fd, "CEL Event Subscriber: %s\n", backend->name);
		}
		ao2_iterator_destroy(&iter);
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_status = AST_CLI_DEFINE(handle_cli_status, "Display the CEL status");

enum ast_cel_event_type ast_cel_str_to_event_type(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LEN(cel_event_types); i++) {
		if (cel_event_types[i] && !strcasecmp(name, cel_event_types[i])) {
			return i;
		}
	}

	ast_log(LOG_ERROR, "Unknown event name '%s'\n", name);
	return AST_CEL_INVALID_VALUE;
}

static int ast_cel_track_event(enum ast_cel_event_type et)
{
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);

	if (!cfg || !cfg->general) {
		return 0;
	}

	return (cfg->general->events & ((int64_t) 1 << et)) ? 1 : 0;
}

static int events_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_cel_general_config *cfg = obj;
	char *events = ast_strdupa(var->value);
	char *cur_event;

	while ((cur_event = strsep(&events, ","))) {
		enum ast_cel_event_type event_type;

		cur_event = ast_strip(cur_event);
		if (ast_strlen_zero(cur_event)) {
			continue;
		}

		event_type = ast_cel_str_to_event_type(cur_event);

		if (event_type == AST_CEL_ALL) {
			/* All events */
			cfg->events = (int64_t) -1;
		} else if (event_type == AST_CEL_INVALID_VALUE) {
			return -1;
		} else {
			cfg->events |= ((int64_t) 1 << event_type);
		}
	}

	return 0;
}

static int apps_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_cel_general_config *cfg = obj;
	char *apps = ast_strdupa(var->value);
	char *cur_app;

	while ((cur_app = strsep(&apps, ","))) {
		cur_app = ast_strip(cur_app);
		if (ast_strlen_zero(cur_app)) {
			continue;
		}

		cur_app = ast_str_to_lower(cur_app);
		ast_str_container_add(cfg->apps, cur_app);
	}

	return 0;
}

const char *ast_cel_get_type_name(enum ast_cel_event_type type)
{
	return S_OR(cel_event_types[type], "Unknown");
}

static int cel_track_app(const char *const_app)
{
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);
	RAII_VAR(char *, app, NULL, ao2_cleanup);
	char *app_lower;

	if (!cfg || !cfg->general) {
		return 0;
	}

	app_lower = ast_str_to_lower(ast_strdupa(const_app));
	app = ao2_find(cfg->general->apps, app_lower, OBJ_SEARCH_KEY);
	if (!app) {
		return 0;
	}

	return 1;
}

static int cel_linkedid_ref(const char *linkedid);

struct ast_event *ast_cel_create_event(struct ast_channel_snapshot *snapshot,
		enum ast_cel_event_type event_type, const char *userdefevname,
		struct ast_json *extra, const char *peer)
{
	struct timeval eventtime = ast_tvnow();

	return ast_cel_create_event_with_time(snapshot, event_type, &eventtime,
		userdefevname, extra, peer);
}

struct ast_event *ast_cel_create_event_with_time(struct ast_channel_snapshot *snapshot,
		enum ast_cel_event_type event_type, const struct timeval *event_time,
		const char *userdefevname, struct ast_json *extra, const char *peer)
{
	RAII_VAR(char *, extra_txt, NULL, ast_json_free);
	if (extra) {
		extra_txt = ast_json_dump_string(extra);
	}
	return ast_event_new(AST_EVENT_CEL,
		AST_EVENT_IE_CEL_EVENT_TYPE, AST_EVENT_IE_PLTYPE_UINT, event_type,
		AST_EVENT_IE_CEL_EVENT_TIME, AST_EVENT_IE_PLTYPE_UINT, event_time->tv_sec,
		AST_EVENT_IE_CEL_EVENT_TIME_USEC, AST_EVENT_IE_PLTYPE_UINT, event_time->tv_usec,
		AST_EVENT_IE_CEL_USEREVENT_NAME, AST_EVENT_IE_PLTYPE_STR, S_OR(userdefevname, ""),
		AST_EVENT_IE_CEL_CIDNAME, AST_EVENT_IE_PLTYPE_STR, snapshot->caller->name,
		AST_EVENT_IE_CEL_CIDNUM, AST_EVENT_IE_PLTYPE_STR, snapshot->caller->number,
		AST_EVENT_IE_CEL_CIDANI, AST_EVENT_IE_PLTYPE_STR, snapshot->caller->ani,
		AST_EVENT_IE_CEL_CIDRDNIS, AST_EVENT_IE_PLTYPE_STR, snapshot->caller->rdnis,
		AST_EVENT_IE_CEL_CIDDNID, AST_EVENT_IE_PLTYPE_STR, snapshot->caller->dnid,
		AST_EVENT_IE_CEL_EXTEN, AST_EVENT_IE_PLTYPE_STR, snapshot->dialplan->exten,
		AST_EVENT_IE_CEL_CONTEXT, AST_EVENT_IE_PLTYPE_STR, snapshot->dialplan->context,
		AST_EVENT_IE_CEL_CHANNAME, AST_EVENT_IE_PLTYPE_STR, snapshot->base->name,
		AST_EVENT_IE_CEL_APPNAME, AST_EVENT_IE_PLTYPE_STR, snapshot->dialplan->appl,
		AST_EVENT_IE_CEL_APPDATA, AST_EVENT_IE_PLTYPE_STR, snapshot->dialplan->data,
		AST_EVENT_IE_CEL_AMAFLAGS, AST_EVENT_IE_PLTYPE_UINT, snapshot->amaflags,
		AST_EVENT_IE_CEL_ACCTCODE, AST_EVENT_IE_PLTYPE_STR, snapshot->base->accountcode,
		AST_EVENT_IE_CEL_PEERACCT, AST_EVENT_IE_PLTYPE_STR, snapshot->peer->account,
		AST_EVENT_IE_CEL_UNIQUEID, AST_EVENT_IE_PLTYPE_STR, snapshot->base->uniqueid,
		AST_EVENT_IE_CEL_LINKEDID, AST_EVENT_IE_PLTYPE_STR, snapshot->peer->linkedid,
		AST_EVENT_IE_CEL_USERFIELD, AST_EVENT_IE_PLTYPE_STR, snapshot->base->userfield,
		AST_EVENT_IE_CEL_EXTRA, AST_EVENT_IE_PLTYPE_STR, S_OR(extra_txt, ""),
		AST_EVENT_IE_CEL_PEER, AST_EVENT_IE_PLTYPE_STR, S_OR(peer, ""),
		AST_EVENT_IE_END);
}

static int cel_backend_send_cb(void *obj, void *arg, int flags)
{
	struct cel_backend *backend = obj;

	backend->callback(arg);
	return 0;
}

static int cel_report_event(struct ast_channel_snapshot *snapshot,
		enum ast_cel_event_type event_type, const struct timeval *event_time,
		const char *userdefevname, struct ast_json *extra,
		const char *peer_str)
{
	struct ast_event *ev;
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);
	RAII_VAR(struct ao2_container *, backends, ao2_global_obj_ref(cel_backends), ao2_cleanup);

	if (!cfg || !cfg->general || !cfg->general->enable || !backends) {
		return 0;
	}

	/* Record the linkedid of new channels if we are tracking LINKEDID_END even if we aren't
	 * reporting on CHANNEL_START so we can track when to send LINKEDID_END */
	if (event_type == AST_CEL_CHANNEL_START
		&& ast_cel_track_event(AST_CEL_LINKEDID_END)) {
		if (cel_linkedid_ref(snapshot->peer->linkedid)) {
			return -1;
		}
	}

	if (!ast_cel_track_event(event_type)) {
		return 0;
	}

	if ((event_type == AST_CEL_APP_START || event_type == AST_CEL_APP_END)
		&& !cel_track_app(snapshot->dialplan->appl)) {
		return 0;
	}

	ev = ast_cel_create_event_with_time(snapshot, event_type, event_time, userdefevname, extra, peer_str);
	if (!ev) {
		return -1;
	}

	/* Distribute event to backends */
	ao2_callback(backends, OBJ_MULTIPLE | OBJ_NODATA, cel_backend_send_cb, ev);
	ast_event_destroy(ev);

	return 0;
}

/* called whenever a channel is destroyed or a linkedid is changed to
 * potentially emit a CEL_LINKEDID_END event */
static void check_retire_linkedid(struct ast_channel_snapshot *snapshot, const struct timeval *event_time)
{
	RAII_VAR(struct ao2_container *, linkedids, ao2_global_obj_ref(cel_linkedids), ao2_cleanup);
	struct cel_linkedid *lid;

	if (!linkedids || ast_strlen_zero(snapshot->peer->linkedid)) {
		/* The CEL module is shutdown.  Abort. */
		return;
	}

	ao2_lock(linkedids);

	lid = ao2_find(linkedids, (void *) snapshot->peer->linkedid, OBJ_SEARCH_KEY);
	if (!lid) {
		ao2_unlock(linkedids);

		/*
		 * The user may have done a reload to start tracking linkedids
		 * when a call was already in progress.  This is an unusual kind
		 * of change to make after starting Asterisk.
		 */
		ast_log(LOG_ERROR, "Something weird happened, couldn't find linkedid %s\n",
			snapshot->peer->linkedid);
		return;
	}

	if (!--lid->count) {
		/* No channels use this linkedid anymore. */
		ao2_unlink(linkedids, lid);
		ao2_unlock(linkedids);

		cel_report_event(snapshot, AST_CEL_LINKEDID_END, event_time, NULL, NULL, NULL);
	} else {
		ao2_unlock(linkedids);
	}
	ao2_ref(lid, -1);
}

/* Note that no 'chan_fixup' function is provided for this datastore type,
 * because the channels that will use it will never be involved in masquerades.
 */
static const struct ast_datastore_info fabricated_channel_datastore = {
	.type = "CEL fabricated channel",
	.destroy = ast_free_ptr,
};

struct ast_channel *ast_cel_fabricate_channel_from_event(const struct ast_event *event)
{
	struct varshead *headp;
	struct ast_var_t *newvariable;
	const char *mixed_name;
	char timebuf[30];
	struct ast_channel *tchan;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};
	struct ast_datastore *datastore;
	char *app_data;
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);

	if (!cfg || !cfg->general) {
		return NULL;
	}

	/* do not call ast_channel_alloc because this is not really a real channel */
	if (!(tchan = ast_dummy_channel_alloc())) {
		return NULL;
	}

	headp = ast_channel_varshead(tchan);

	/* first, get the variables from the event */
	if (ast_cel_fill_record(event, &record)) {
		ast_channel_unref(tchan);
		return NULL;
	}

	/* next, fill the channel with their data */
	mixed_name = (record.event_type == AST_CEL_USER_DEFINED)
		? record.user_defined_name : record.event_name;
	if ((newvariable = ast_var_assign("eventtype", mixed_name))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if (ast_strlen_zero(cfg->general->date_format)) {
		snprintf(timebuf, sizeof(timebuf), "%ld.%06ld", (long) record.event_time.tv_sec,
				(long) record.event_time.tv_usec);
	} else {
		struct ast_tm tm;
		ast_localtime(&record.event_time, &tm, NULL);
		ast_strftime(timebuf, sizeof(timebuf), cfg->general->date_format, &tm);
	}

	if ((newvariable = ast_var_assign("eventtime", timebuf))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if ((newvariable = ast_var_assign("eventenum", record.event_name))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}
	if ((newvariable = ast_var_assign("userdeftype", record.user_defined_name))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}
	if ((newvariable = ast_var_assign("eventextra", record.extra))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	ast_channel_caller(tchan)->id.name.valid = 1;
	ast_channel_caller(tchan)->id.name.str = ast_strdup(record.caller_id_name);
	ast_channel_caller(tchan)->id.number.valid = 1;
	ast_channel_caller(tchan)->id.number.str = ast_strdup(record.caller_id_num);
	ast_channel_caller(tchan)->ani.number.valid = 1;
	ast_channel_caller(tchan)->ani.number.str = ast_strdup(record.caller_id_ani);
	ast_channel_redirecting(tchan)->from.number.valid = 1;
	ast_channel_redirecting(tchan)->from.number.str = ast_strdup(record.caller_id_rdnis);
	ast_channel_dialed(tchan)->number.str = ast_strdup(record.caller_id_dnid);

	ast_channel_exten_set(tchan, record.extension);
	ast_channel_context_set(tchan, record.context);
	ast_channel_name_set(tchan, record.channel_name);
	ast_channel_internal_set_fake_ids(tchan, record.unique_id, record.linked_id);
	ast_channel_accountcode_set(tchan, record.account_code);
	ast_channel_peeraccount_set(tchan, record.peer_account);
	ast_channel_userfield_set(tchan, record.user_field);

	if ((newvariable = ast_var_assign("BRIDGEPEER", record.peer))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	ast_channel_amaflags_set(tchan, record.amaflag);

	/* We need to store an 'application name' and 'application
	 * data' on the channel for logging purposes, but the channel
	 * structure only provides a place to store pointers, and it
	 * expects these pointers to be pointing to data that does not
	 * need to be freed. This means that the channel's destructor
	 * does not attempt to free any storage that these pointers
	 * point to. However, we can't provide data in that form directly for
	 * these structure members. In order to ensure that these data
	 * elements have a lifetime that matches the channel's
	 * lifetime, we'll put them in a datastore attached to the
	 * channel, and set's the channel's pointers to point into the
	 * datastore.  The datastore will then be automatically destroyed
	 * when the channel is destroyed.
	 */

	if (!(datastore = ast_datastore_alloc(&fabricated_channel_datastore, NULL))) {
		ast_channel_unref(tchan);
		return NULL;
	}

	if (!(app_data = ast_malloc(strlen(record.application_name) + strlen(record.application_data) + 2))) {
		ast_datastore_free(datastore);
		ast_channel_unref(tchan);
		return NULL;
	}

	ast_channel_appl_set(tchan, strcpy(app_data, record.application_name));
	ast_channel_data_set(tchan, strcpy(app_data + strlen(record.application_name) + 1,
		record.application_data));

	datastore->data = app_data;
	ast_channel_datastore_add(tchan, datastore);

	return tchan;
}

static int cel_linkedid_ref(const char *linkedid)
{
	RAII_VAR(struct ao2_container *, linkedids, ao2_global_obj_ref(cel_linkedids), ao2_cleanup);
	struct cel_linkedid *lid;

	if (ast_strlen_zero(linkedid)) {
		ast_log(LOG_ERROR, "The linkedid should never be empty\n");
		return -1;
	}
	if (!linkedids) {
		/* The CEL module is shutdown.  Abort. */
		return -1;
	}

	ao2_lock(linkedids);
	lid = ao2_find(linkedids, (void *) linkedid, OBJ_SEARCH_KEY);
	if (!lid) {
		/*
		 * Changes to the lid->count member are protected by the
		 * container lock so the lid object does not need its own lock.
		 */
		lid = ao2_alloc_options(sizeof(*lid) + strlen(linkedid) + 1, NULL,
			AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!lid) {
			ao2_unlock(linkedids);
			return -1;
		}
		strcpy(lid->id, linkedid);/* Safe */

		ao2_link(linkedids, lid);
	}
	++lid->count;
	ao2_unlock(linkedids);
	ao2_ref(lid, -1);

	return 0;
}

int ast_cel_fill_record(const struct ast_event *e, struct ast_cel_event_record *r)
{
	if (r->version != AST_CEL_EVENT_RECORD_VERSION) {
		ast_log(LOG_ERROR, "Module ABI mismatch for ast_cel_event_record.  "
				"Please ensure all modules were compiled for "
				"this version of Asterisk.\n");
		return -1;
	}

	r->event_type = ast_event_get_ie_uint(e, AST_EVENT_IE_CEL_EVENT_TYPE);

	r->event_time.tv_sec = ast_event_get_ie_uint(e, AST_EVENT_IE_CEL_EVENT_TIME);
	r->event_time.tv_usec = ast_event_get_ie_uint(e, AST_EVENT_IE_CEL_EVENT_TIME_USEC);

	r->event_name = ast_cel_get_type_name(r->event_type);
	if (r->event_type == AST_CEL_USER_DEFINED) {
		r->user_defined_name = ast_event_get_ie_str(e, AST_EVENT_IE_CEL_USEREVENT_NAME);
	} else {
		r->user_defined_name = "";
	}

	r->caller_id_name   = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_CIDNAME), "");
	r->caller_id_num    = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_CIDNUM), "");
	r->caller_id_ani    = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_CIDANI), "");
	r->caller_id_rdnis  = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_CIDRDNIS), "");
	r->caller_id_dnid   = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_CIDDNID), "");
	r->extension        = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_EXTEN), "");
	r->context          = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_CONTEXT), "");
	r->channel_name     = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_CHANNAME), "");
	r->application_name = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_APPNAME), "");
	r->application_data = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_APPDATA), "");
	r->account_code     = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_ACCTCODE), "");
	r->peer_account     = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_PEERACCT), "");
	r->unique_id        = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_UNIQUEID), "");
	r->linked_id        = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_LINKEDID), "");
	r->amaflag          = ast_event_get_ie_uint(e, AST_EVENT_IE_CEL_AMAFLAGS);
	r->user_field       = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_USERFIELD), "");
	r->peer             = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_PEER), "");
	r->extra            = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_EXTRA), "");

	return 0;
}

/*! \brief Typedef for callbacks that get called on channel snapshot updates */
typedef void (*cel_channel_snapshot_monitor)(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *event_time);

static struct cel_dialstatus *get_dialstatus(const char *uniqueid)
{
	struct ao2_container *dial_statuses = ao2_global_obj_ref(cel_dialstatus_store);
	struct cel_dialstatus *dialstatus = NULL;

	if (dial_statuses) {
		dialstatus = ao2_find(dial_statuses, uniqueid, OBJ_SEARCH_KEY | OBJ_UNLINK);
		ao2_ref(dial_statuses, -1);
	}
	return dialstatus;
}

static const char *get_blob_variable(struct ast_multi_channel_blob *blob, const char *varname)
{
	struct ast_json *json = ast_multi_channel_blob_get_json(blob);
	if (!json) {
		return NULL;
	}

	json = ast_json_object_get(json, varname);
	if (!json) {
		return NULL;
	}

	return ast_json_string_get(json);
}

/*! \brief Handle channel state changes */
static void cel_channel_state_change(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *event_time)
{
	int is_hungup, was_hungup;

	if (!old_snapshot) {
		cel_report_event(new_snapshot, AST_CEL_CHANNEL_START, event_time, NULL, NULL, NULL);
		return;
	}

	was_hungup = ast_test_flag(&old_snapshot->flags, AST_FLAG_DEAD) ? 1 : 0;
	is_hungup = ast_test_flag(&new_snapshot->flags, AST_FLAG_DEAD) ? 1 : 0;

	if (!was_hungup && is_hungup) {
		struct ast_json *extra;
		struct cel_dialstatus *dialstatus = get_dialstatus(new_snapshot->base->uniqueid);

		extra = ast_json_pack("{s: i, s: s, s: s}",
			"hangupcause", new_snapshot->hangup->cause,
			"hangupsource", new_snapshot->hangup->source,
			"dialstatus", dialstatus ? dialstatus->dialstatus : "");
		cel_report_event(new_snapshot, AST_CEL_HANGUP, event_time, NULL, extra, NULL);
		ast_json_unref(extra);
		ao2_cleanup(dialstatus);

		cel_report_event(new_snapshot, AST_CEL_CHANNEL_END, event_time, NULL, NULL, NULL);
		if (ast_cel_track_event(AST_CEL_LINKEDID_END)) {
			check_retire_linkedid(new_snapshot, event_time);
		}
		return;
	}

	if (old_snapshot->state != new_snapshot->state && new_snapshot->state == AST_STATE_UP) {
		cel_report_event(new_snapshot, AST_CEL_ANSWER, event_time, NULL, NULL, NULL);
		return;
	}
}

static void cel_channel_linkedid_change(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *event_time)
{
	if (!old_snapshot) {
		return;
	}

	ast_assert(!ast_strlen_zero(new_snapshot->peer->linkedid));
	ast_assert(!ast_strlen_zero(old_snapshot->peer->linkedid));

	if (ast_cel_track_event(AST_CEL_LINKEDID_END)
		&& strcmp(old_snapshot->peer->linkedid, new_snapshot->peer->linkedid)) {
		cel_linkedid_ref(new_snapshot->peer->linkedid);
		check_retire_linkedid(old_snapshot, event_time);
	}
}

static void cel_channel_app_change(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *event_time)
{
	if (old_snapshot && !strcmp(old_snapshot->dialplan->appl, new_snapshot->dialplan->appl)) {
		return;
	}

	/* old snapshot has an application, end it */
	if (old_snapshot && !ast_strlen_zero(old_snapshot->dialplan->appl)) {
		cel_report_event(old_snapshot, AST_CEL_APP_END, event_time, NULL, NULL, NULL);
	}

	/* new snapshot has an application, start it */
	if (!ast_strlen_zero(new_snapshot->dialplan->appl)) {
		cel_report_event(new_snapshot, AST_CEL_APP_START, event_time, NULL, NULL, NULL);
	}
}

/*! \brief Handlers for channel snapshot changes.
 * \note Order of the handlers matters. Application changes must come before state
 * changes to ensure that hangup notifications occur after application changes.
 * Linkedid checking should always come last.
 */
cel_channel_snapshot_monitor cel_channel_monitors[] = {
	cel_channel_app_change,
	cel_channel_state_change,
	cel_channel_linkedid_change,
};

static int cel_filter_channel_snapshot(struct ast_channel_snapshot *snapshot)
{
	if (!snapshot) {
		return 0;
	}
	return snapshot->base->tech_properties & AST_CHAN_TP_INTERNAL;
}

static void cel_snapshot_update_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_channel_snapshot_update *update = stasis_message_data(message);
	size_t i;

	if (cel_filter_channel_snapshot(update->old_snapshot) || cel_filter_channel_snapshot(update->new_snapshot)) {
		return;
	}

	for (i = 0; i < ARRAY_LEN(cel_channel_monitors); ++i) {
		cel_channel_monitors[i](update->old_snapshot, update->new_snapshot, stasis_message_timestamp(message));
	}
}

static struct ast_str *cel_generate_peer_str(
	struct ast_bridge_snapshot *bridge,
	struct ast_channel_snapshot *chan)
{
	struct ast_str *peer_str = ast_str_create(32);
	struct ao2_iterator i;
	char *current_chan = NULL;

	if (!peer_str) {
		return NULL;
	}

	for (i = ao2_iterator_init(bridge->channels, 0);
		(current_chan = ao2_iterator_next(&i));
		ao2_cleanup(current_chan)) {
		struct ast_channel_snapshot *current_snapshot;

		/* Don't add the channel for which this message is being generated */
		if (!strcmp(current_chan, chan->base->uniqueid)) {
			continue;
		}

		current_snapshot = ast_channel_snapshot_get_latest(current_chan);
		if (!current_snapshot) {
			continue;
		}

		ast_str_append(&peer_str, 0, "%s,", current_snapshot->base->name);
		ao2_cleanup(current_snapshot);
	}
	ao2_iterator_destroy(&i);

	/* Rip off the trailing comma */
	ast_str_truncate(peer_str, -1);

	return peer_str;
}

static void cel_bridge_enter_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	struct ast_bridge_snapshot *snapshot = blob->bridge;
	struct ast_channel_snapshot *chan_snapshot = blob->channel;
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref);
	RAII_VAR(struct ast_str *, peer_str, NULL, ast_free);

	if (cel_filter_channel_snapshot(chan_snapshot)) {
		return;
	}

	extra = ast_json_pack("{s: s, s: s}",
		"bridge_id", snapshot->uniqueid,
		"bridge_technology", snapshot->technology);
	if (!extra) {
		return;
	}

	peer_str = cel_generate_peer_str(snapshot, chan_snapshot);
	if (!peer_str) {
		return;
	}

	cel_report_event(chan_snapshot, AST_CEL_BRIDGE_ENTER, stasis_message_timestamp(message),
		NULL, extra, ast_str_buffer(peer_str));
}

static void cel_bridge_leave_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	struct ast_bridge_snapshot *snapshot = blob->bridge;
	struct ast_channel_snapshot *chan_snapshot = blob->channel;
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref);
	RAII_VAR(struct ast_str *, peer_str, NULL, ast_free);

	if (cel_filter_channel_snapshot(chan_snapshot)) {
		return;
	}

	extra = ast_json_pack("{s: s, s: s}",
		"bridge_id", snapshot->uniqueid,
		"bridge_technology", snapshot->technology);
	if (!extra) {
		return;
	}

	peer_str = cel_generate_peer_str(snapshot, chan_snapshot);
	if (!peer_str) {
		return;
	}

	cel_report_event(chan_snapshot, AST_CEL_BRIDGE_EXIT, stasis_message_timestamp(message),
		NULL, extra, ast_str_buffer(peer_str));
}

static void cel_parking_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_parked_call_payload *parked_payload = stasis_message_data(message);
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref);
	const char *reason = NULL;

	switch (parked_payload->event_type) {
	case PARKED_CALL:
		extra = ast_json_pack("{s: s, s: s}",
			"parker_dial_string", parked_payload->parker_dial_string,
			"parking_lot", parked_payload->parkinglot);
		if (extra) {
			cel_report_event(parked_payload->parkee, AST_CEL_PARK_START, stasis_message_timestamp(message),
				NULL, extra, NULL);
		}
		return;
	case PARKED_CALL_TIMEOUT:
		reason = "ParkedCallTimeOut";
		break;
	case PARKED_CALL_GIVEUP:
		reason = "ParkedCallGiveUp";
		break;
	case PARKED_CALL_UNPARKED:
		reason = "ParkedCallUnparked";
		break;
	case PARKED_CALL_FAILED:
		reason = "ParkedCallFailed";
		break;
	case PARKED_CALL_SWAP:
		reason = "ParkedCallSwap";
		break;
	}

	if (parked_payload->retriever) {
		extra = ast_json_pack("{s: s, s: s}",
			"reason", reason ?: "",
			"retriever", parked_payload->retriever->base->name);
	} else {
		extra = ast_json_pack("{s: s}", "reason", reason ?: "");
	}

	if (extra) {
		cel_report_event(parked_payload->parkee, AST_CEL_PARK_END, stasis_message_timestamp(message),
			NULL, extra, NULL);
	}
}

static void save_dialstatus(struct ast_multi_channel_blob *blob, struct ast_channel_snapshot *snapshot)
{
	struct ao2_container *dial_statuses = ao2_global_obj_ref(cel_dialstatus_store);
	const char *dialstatus_string = get_blob_variable(blob, "dialstatus");
	struct cel_dialstatus *dialstatus;
	size_t dialstatus_string_len;

	if (!dial_statuses || ast_strlen_zero(dialstatus_string)) {
		ao2_cleanup(dial_statuses);
		return;
	}

	dialstatus = ao2_find(dial_statuses, snapshot->base->uniqueid, OBJ_SEARCH_KEY);
	if (dialstatus) {
		if (!strcasecmp(dialstatus_string, "ANSWER") && strcasecmp(dialstatus->dialstatus, "ANSWER")) {
			/* In the case of an answer after we already have a dial status we give
			 * priority to the answer since the call was, well, answered. In the case of
			 * failure dial status results we simply let the first failure be the status.
			 */
			ao2_unlink(dial_statuses, dialstatus);
			ao2_ref(dialstatus, -1);
		} else {
			ao2_ref(dialstatus, -1);
			ao2_ref(dial_statuses, -1);
			return;
		}
	}

	dialstatus_string_len = strlen(dialstatus_string) + 1;
	dialstatus = ao2_alloc_options(sizeof(*dialstatus) + dialstatus_string_len, NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!dialstatus) {
		ao2_ref(dial_statuses, -1);
		return;
	}

	ast_copy_string(dialstatus->uniqueid, snapshot->base->uniqueid, sizeof(dialstatus->uniqueid));
	ast_copy_string(dialstatus->dialstatus, dialstatus_string, dialstatus_string_len);

	ao2_link(dial_statuses, dialstatus);
	ao2_ref(dialstatus, -1);
	ao2_ref(dial_statuses, -1);
}

static int is_valid_dialstatus(struct ast_multi_channel_blob *blob)
{
	const char *dialstatus = get_blob_variable(blob, "dialstatus");
	int res = 0;

	if (ast_strlen_zero(dialstatus)) {
		res = 0;
	} else if (!strcasecmp(dialstatus, "CHANUNAVAIL")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "CONGESTION")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "NOANSWER")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "BUSY")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "ANSWER")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "CANCEL")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "DONTCALL")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "TORTURE")) {
		res = 1;
	} else if (!strcasecmp(dialstatus, "INVALIDARGS")) {
		res = 1;
	}
	return res;
}

static void cel_dial_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_multi_channel_blob *blob = stasis_message_data(message);
	struct ast_channel_snapshot *snapshot;

	snapshot = ast_multi_channel_blob_get_channel(blob, "caller");
	if (!snapshot || cel_filter_channel_snapshot(snapshot)) {
		return;
	}

	if (!ast_strlen_zero(get_blob_variable(blob, "forward"))) {
		struct ast_json *extra;

		extra = ast_json_pack("{s: s}", "forward", get_blob_variable(blob, "forward"));
		if (extra) {
			cel_report_event(snapshot, AST_CEL_FORWARD, stasis_message_timestamp(message),
				NULL, extra, NULL);
			ast_json_unref(extra);
		}
	}

	if (is_valid_dialstatus(blob)) {
		save_dialstatus(blob, snapshot);
	}
}

static void cel_generic_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	int event_type = ast_json_integer_get(ast_json_object_get(obj->blob, "event_type"));
	struct ast_json *event_details = ast_json_object_get(obj->blob, "event_details");

	switch (event_type) {
	case AST_CEL_USER_DEFINED:
		{
			const char *event = ast_json_string_get(ast_json_object_get(event_details, "event"));
			struct ast_json *extra = ast_json_object_get(event_details, "extra");
			cel_report_event(obj->snapshot, event_type, stasis_message_timestamp(message),
				event, extra, NULL);
			break;
		}
	default:
		ast_log(LOG_ERROR, "Unhandled %s event blob\n", ast_cel_get_type_name(event_type));
		break;
	}
}

static void cel_blind_transfer_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_blind_transfer_message *transfer_msg = stasis_message_data(message);
	struct ast_channel_snapshot *chan_snapshot = transfer_msg->transferer;
	struct ast_bridge_snapshot *bridge_snapshot = transfer_msg->bridge;
	struct ast_json *extra;

	if (transfer_msg->result != AST_BRIDGE_TRANSFER_SUCCESS) {
		return;
	}

	extra = ast_json_pack("{s: s, s: s, s: s, s: s, s: s}",
		"extension", transfer_msg->exten,
		"context", transfer_msg->context,
		"bridge_id", bridge_snapshot->uniqueid,
		"transferee_channel_name", transfer_msg->transferee ? transfer_msg->transferee->base->name : "N/A",
		"transferee_channel_uniqueid", transfer_msg->transferee ? transfer_msg->transferee->base->uniqueid  : "N/A");
	if (extra) {
		cel_report_event(chan_snapshot, AST_CEL_BLINDTRANSFER, stasis_message_timestamp(message),
			NULL, extra, NULL);
		ast_json_unref(extra);
	}
}

static void cel_attended_transfer_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_attended_transfer_message *xfer = stasis_message_data(message);
	struct ast_json *extra = NULL;
	struct ast_bridge_snapshot *bridge1, *bridge2;
	struct ast_channel_snapshot *channel1, *channel2;

	/* Make sure bridge1 is always non-NULL */
	if (!xfer->to_transferee.bridge_snapshot) {
		bridge1 = xfer->to_transfer_target.bridge_snapshot;
		bridge2 = xfer->to_transferee.bridge_snapshot;
		channel1 = xfer->to_transfer_target.channel_snapshot;
		channel2 = xfer->to_transferee.channel_snapshot;
	} else {
		bridge1 = xfer->to_transferee.bridge_snapshot;
		bridge2 = xfer->to_transfer_target.bridge_snapshot;
		channel1 = xfer->to_transferee.channel_snapshot;
		channel2 = xfer->to_transfer_target.channel_snapshot;
	}

	switch (xfer->dest_type) {
	case AST_ATTENDED_TRANSFER_DEST_FAIL:
		return;
		/* handle these three the same */
	case AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE:
	case AST_ATTENDED_TRANSFER_DEST_LINK:
	case AST_ATTENDED_TRANSFER_DEST_THREEWAY:
		extra = ast_json_pack("{s: s, s: s, s: s, s: s, s: s, s: s, s: s, s: s}",
			"bridge1_id", bridge1->uniqueid,
			"channel2_name", channel2->base->name,
			"channel2_uniqueid", channel2->base->uniqueid,
			"bridge2_id", bridge2->uniqueid,
			"transferee_channel_name", xfer->transferee ? xfer->transferee->base->name : "N/A",
			"transferee_channel_uniqueid", xfer->transferee ? xfer->transferee->base->uniqueid : "N/A",
			"transfer_target_channel_name", xfer->target ? xfer->target->base->name : "N/A",
			"transfer_target_channel_uniqueid", xfer->target ? xfer->target->base->uniqueid : "N/A");
		if (!extra) {
			return;
		}
		break;
	case AST_ATTENDED_TRANSFER_DEST_APP:
	case AST_ATTENDED_TRANSFER_DEST_LOCAL_APP:
		extra = ast_json_pack("{s: s, s: s, s: s, s: s, s: s, s: s, s: s, s: s}",
			"bridge1_id", bridge1->uniqueid,
			"channel2_name", channel2->base->name,
			"channel2_uniqueid", channel2->base->uniqueid,
			"app", xfer->dest.app,
			"transferee_channel_name", xfer->transferee ? xfer->transferee->base->name : "N/A",
			"transferee_channel_uniqueid", xfer->transferee ? xfer->transferee->base->uniqueid : "N/A",
			"transfer_target_channel_name", xfer->target ? xfer->target->base->name : "N/A",
			"transfer_target_channel_uniqueid", xfer->target ? xfer->target->base->uniqueid : "N/A");
		if (!extra) {
			return;
		}
		break;
	}
	cel_report_event(channel1, AST_CEL_ATTENDEDTRANSFER, stasis_message_timestamp(message),
		NULL, extra, NULL);
	ast_json_unref(extra);
}

static void cel_pickup_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_multi_channel_blob *obj = stasis_message_data(message);
	struct ast_channel_snapshot *channel = ast_multi_channel_blob_get_channel(obj, "channel");
	struct ast_channel_snapshot *target = ast_multi_channel_blob_get_channel(obj, "target");
	struct ast_json *extra;

	if (!channel || !target) {
		return;
	}

	extra = ast_json_pack("{s: s, s: s}",
		"pickup_channel", channel->base->name,
		"pickup_channel_uniqueid", channel->base->uniqueid);
	if (!extra) {
		return;
	}

	cel_report_event(target, AST_CEL_PICKUP, stasis_message_timestamp(message), NULL, extra, NULL);
	ast_json_unref(extra);
}


static void cel_local_optimization_cb_helper(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message,
	enum ast_cel_event_type event_type)
{
	struct ast_multi_channel_blob *obj = stasis_message_data(message);
	struct ast_channel_snapshot *localone = ast_multi_channel_blob_get_channel(obj, "1");
	struct ast_channel_snapshot *localtwo = ast_multi_channel_blob_get_channel(obj, "2");
	struct ast_json *extra;

	if (!localone || !localtwo) {
		return;
	}

	extra = ast_json_pack("{s: s, s: s}",
		"local_two", localtwo->base->name,
		"local_two_uniqueid", localtwo->base->uniqueid);
	if (!extra) {
		return;
	}

	cel_report_event(localone, event_type, stasis_message_timestamp(message), NULL, extra, NULL);
	ast_json_unref(extra);
}

static void cel_local_optimization_end_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	/* The AST_CEL_LOCAL_OPTIMIZE event has always been triggered by the end of optimization.
	   This can either be used as an indication that the call was locally optimized, or as
	   the END event in combination with the subsequently added BEGIN event. */
	cel_local_optimization_cb_helper(data, sub, message, AST_CEL_LOCAL_OPTIMIZE);
}

static void cel_local_optimization_begin_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	cel_local_optimization_cb_helper(data, sub, message, AST_CEL_LOCAL_OPTIMIZE_BEGIN);
}

static void destroy_routes(void)
{
	stasis_message_router_unsubscribe_and_join(cel_state_router);
	cel_state_router = NULL;
}

static void destroy_subscriptions(void)
{
	ao2_cleanup(cel_aggregation_topic);
	cel_aggregation_topic = NULL;
	ao2_cleanup(cel_topic);
	cel_topic = NULL;

	cel_channel_forwarder = stasis_forward_cancel(cel_channel_forwarder);
	cel_bridge_forwarder = stasis_forward_cancel(cel_bridge_forwarder);
	cel_parking_forwarder = stasis_forward_cancel(cel_parking_forwarder);
	cel_cel_forwarder = stasis_forward_cancel(cel_cel_forwarder);
}

static int unload_module(void)
{
	destroy_routes();
	destroy_subscriptions();
	STASIS_MESSAGE_TYPE_CLEANUP(cel_generic_type);

	ast_cli_unregister(&cli_status);
	aco_info_destroy(&cel_cfg_info);
	ao2_global_obj_release(cel_configs);
	ao2_global_obj_release(cel_dialstatus_store);
	ao2_global_obj_release(cel_linkedids);
	ao2_global_obj_release(cel_backends);

	return 0;
}

/*!
 * \brief Create the Stasis subscriptions for CEL
 */
static int create_subscriptions(void)
{
	cel_aggregation_topic = stasis_topic_create("cel:aggregator");
	if (!cel_aggregation_topic) {
		return -1;
	}

	cel_topic = stasis_topic_create("cel:misc");
	if (!cel_topic) {
		return -1;
	}

	cel_channel_forwarder = stasis_forward_all(
		ast_channel_topic_all(),
		cel_aggregation_topic);
	if (!cel_channel_forwarder) {
		return -1;
	}

	cel_bridge_forwarder = stasis_forward_all(
		ast_bridge_topic_all(),
		cel_aggregation_topic);
	if (!cel_bridge_forwarder) {
		return -1;
	}

	cel_parking_forwarder = stasis_forward_all(
		ast_parking_topic(),
		cel_aggregation_topic);
	if (!cel_parking_forwarder) {
		return -1;
	}

	cel_cel_forwarder = stasis_forward_all(
		ast_cel_topic(),
		cel_aggregation_topic);
	if (!cel_cel_forwarder) {
		return -1;
	}

	return 0;
}

/*!
 * \brief Create the Stasis message router and routes for CEL
 */
static int create_routes(void)
{
	int ret = 0;

	cel_state_router = stasis_message_router_create(cel_aggregation_topic);
	if (!cel_state_router) {
		return -1;
	}
	stasis_message_router_set_congestion_limits(cel_state_router, -1,
		6 * AST_TASKPROCESSOR_HIGH_WATER_LEVEL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_channel_snapshot_type(),
		cel_snapshot_update_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_channel_dial_type(),
		cel_dial_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_channel_entered_bridge_type(),
		cel_bridge_enter_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_channel_left_bridge_type(),
		cel_bridge_leave_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_parked_call_type(),
		cel_parking_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		cel_generic_type(),
		cel_generic_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_blind_transfer_type(),
		cel_blind_transfer_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_attended_transfer_type(),
		cel_attended_transfer_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_call_pickup_type(),
		cel_pickup_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_local_optimization_end_type(),
		cel_local_optimization_end_cb,
		NULL);

	ret |= stasis_message_router_add(cel_state_router,
		ast_local_optimization_begin_type(),
		cel_local_optimization_begin_cb,
		NULL);

	if (ret) {
		ast_log(AST_LOG_ERROR, "Failed to register for Stasis messages\n");
	}

	return ret;
}

AO2_STRING_FIELD_HASH_FN(cel_linkedid, id)
AO2_STRING_FIELD_CMP_FN(cel_linkedid, id)

static int load_module(void)
{
	struct ao2_container *container;

	container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NUM_APP_BUCKETS, cel_linkedid_hash_fn, NULL, cel_linkedid_cmp_fn);
	ao2_global_obj_replace_unref(cel_linkedids, container);
	ao2_cleanup(container);
	if (!container) {
		return AST_MODULE_LOAD_FAILURE;
	}

	container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NUM_DIALSTATUS_BUCKETS, cel_dialstatus_hash_fn, NULL, cel_dialstatus_cmp_fn);
	ao2_global_obj_replace_unref(cel_dialstatus_store, container);
	ao2_cleanup(container);
	if (!container) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (STASIS_MESSAGE_TYPE_INIT(cel_generic_type)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_cli_register(&cli_status)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, BACKEND_BUCKETS,
		cel_backend_hash_fn, NULL, cel_backend_cmp_fn);
	ao2_global_obj_replace_unref(cel_backends, container);
	ao2_cleanup(container);
	if (!container) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (aco_info_init(&cel_cfg_info)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	aco_option_register(&cel_cfg_info, "enable", ACO_EXACT, general_options, "no", OPT_BOOL_T, 1, FLDSET(struct ast_cel_general_config, enable));
	aco_option_register(&cel_cfg_info, "dateformat", ACO_EXACT, general_options, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_cel_general_config, date_format));
	aco_option_register_custom(&cel_cfg_info, "apps", ACO_EXACT, general_options, "", apps_handler, 0);
	aco_option_register_custom(&cel_cfg_info, "events", ACO_EXACT, general_options, "", events_handler, 0);

	if (aco_process_config(&cel_cfg_info, 0)) {
		struct cel_config *cel_cfg = cel_config_alloc();

		if (!cel_cfg) {
			return AST_MODULE_LOAD_FAILURE;
		}

		/* We couldn't process the configuration so create a default config. */
		if (!aco_set_defaults(&general_option, "general", cel_cfg->general)) {
			ast_log(LOG_NOTICE, "Failed to process CEL configuration; using defaults\n");
			ao2_global_obj_replace_unref(cel_configs, cel_cfg);
		}
		ao2_ref(cel_cfg, -1);
	}

	if (create_subscriptions()) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_cel_check_enabled() && create_routes()) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	unsigned int was_enabled = ast_cel_check_enabled();
	unsigned int is_enabled;

	if (aco_process_config(&cel_cfg_info, 1) == ACO_PROCESS_ERROR) {
		return -1;
	}

	is_enabled = ast_cel_check_enabled();

	if (!was_enabled && is_enabled) {
		if (create_routes()) {
			return -1;
		}
	} else if (was_enabled && !is_enabled) {
		destroy_routes();
	}

	ast_verb(3, "CEL logging %sabled.\n", is_enabled ? "en" : "dis");

	return 0;
}

void ast_cel_publish_user_event(struct ast_channel *chan,
	const char *event,
	const char *extra)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	blob = ast_json_pack("{s: s, s: {s: s}}",
		"event", event,
		"extra", "extra", S_OR(extra, ""));
	if (!blob) {
		return;
	}
	ast_cel_publish_event(chan, AST_CEL_USER_DEFINED, blob);
}

void ast_cel_publish_event(struct ast_channel *chan,
	enum ast_cel_event_type event_type,
	struct ast_json *blob)
{
	struct ast_json *cel_blob;
	struct stasis_message *message;

	cel_blob = ast_json_pack("{s: i, s: o}",
		"event_type", event_type,
		"event_details", ast_json_ref(blob));

	message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(chan), cel_generic_type(), cel_blob);
	if (message) {
		stasis_publish(ast_cel_topic(), message);
	}
	ao2_cleanup(message);
	ast_json_unref(cel_blob);
}

struct stasis_topic *ast_cel_topic(void)
{
	return cel_topic;
}

struct ast_cel_general_config *ast_cel_get_config(void)
{
	RAII_VAR(struct cel_config *, mod_cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);

	if (!mod_cfg || !mod_cfg->general) {
		return NULL;
	}

	ao2_ref(mod_cfg->general, +1);
	return mod_cfg->general;
}

void ast_cel_set_config(struct ast_cel_general_config *config)
{
	int was_enabled;
	int is_enabled;
	struct ast_cel_general_config *cleanup_config;
	struct cel_config *mod_cfg = ao2_global_obj_ref(cel_configs);

	if (mod_cfg) {
		was_enabled = ast_cel_check_enabled();

		cleanup_config = mod_cfg->general;
		ao2_bump(config);
		mod_cfg->general = config;
		ao2_cleanup(cleanup_config);

		is_enabled = ast_cel_check_enabled();
		if (!was_enabled && is_enabled) {
			create_routes();
		} else if (was_enabled && !is_enabled) {
			destroy_routes();
		}

		ao2_ref(mod_cfg, -1);
	}
}

int ast_cel_backend_unregister(const char *name)
{
	struct ao2_container *backends = ao2_global_obj_ref(cel_backends);

	if (backends) {
		ao2_find(backends, name, OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK);
		ao2_ref(backends, -1);
	}

	return 0;
}

int ast_cel_backend_register(const char *name, ast_cel_backend_cb backend_callback)
{
	RAII_VAR(struct ao2_container *, backends, ao2_global_obj_ref(cel_backends), ao2_cleanup);
	struct cel_backend *backend;

	if (!backends || ast_strlen_zero(name) || !backend_callback) {
		return -1;
	}

	/* The backend object is immutable so it doesn't need a lock of its own. */
	backend = ao2_alloc_options(sizeof(*backend) + 1 + strlen(name), NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!backend) {
		return -1;
	}
	strcpy(backend->name, name);/* Safe */
	backend->callback = backend_callback;

	ao2_link(backends, backend);
	ao2_ref(backend, -1);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "CEL Engine",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CORE,
	.requires = "extconfig",
);
