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
 *
 * \todo Do thorough testing of all transfer methods to ensure that BLINDTRANSFER,
 *       ATTENDEDTRANSFER, BRIDGE_START, and BRIDGE_END events are all reported
 *       as expected.
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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

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
#include "asterisk/stasis_bridging.h"
#include "asterisk/bridging.h"
#include "asterisk/parking.h"

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
						<enum name="BRIDGE_START"/>
						<enum name="BRIDGE_END"/>
						<enum name="BRIDGE_TO_CONF"/>
						<enum name="CONF_START"/>
						<enum name="CONF_END"/>
						<enum name="PARK_START"/>
						<enum name="PARK_END"/>
						<enum name="TRANSFER"/>
						<enum name="USER_DEFINED"/>
						<enum name="CONF_ENTER"/>
						<enum name="CONF_EXIT"/>
						<enum name="BLINDTRANSFER"/>
						<enum name="ATTENDEDTRANSFER"/>
						<enum name="PICKUP"/>
						<enum name="FORWARD"/>
						<enum name="3WAY_START"/>
						<enum name="3WAY_END"/>
						<enum name="HOOKFLASH"/>
						<enum name="LINKEDID_END"/>

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
static struct stasis_subscription *cel_channel_forwarder;

/*! Subscription for forwarding the channel caching topic */
static struct stasis_subscription *cel_bridge_forwarder;

/*! Subscription for forwarding the parking topic */
static struct stasis_subscription *cel_parking_forwarder;

/*! Subscription for forwarding the CEL-specific topic */
static struct stasis_subscription *cel_cel_forwarder;

/*! Container for primary channel/bridge ID listing for 2 party bridges */
static struct ao2_container *bridge_primaries;

struct stasis_message_type *cel_generic_type(void);
STASIS_MESSAGE_TYPE_DEFN(cel_generic_type);

/*! The number of buckets into which primary channel uniqueids will be hashed */
#define BRIDGE_PRIMARY_BUCKETS 251

/*! Container for dial end multichannel blobs for holding on to dial statuses */
static struct ao2_container *cel_dialstatus_store;

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

/*!
 * \brief Container of Asterisk application names
 *
 * The apps in this container are the applications that were specified
 * in the configuration as applications that CEL events should be generated
 * for when they start and end on a channel.
 */
static struct ao2_container *linkedids;

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
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

/*! \brief The config file to be processed for the module. */
static struct aco_file cel_conf = {
	.filename = "cel.conf",                  /*!< The name of the config file */
	.types = ACO_TYPES(&general_option),     /*!< The mapping object types to be processed */
	.skip_category = "(^manager$|^radius$)", /*!< Config sections used by existing modules. Do not add to this list. */
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
	[0]                        = "ALL",
	[AST_CEL_CHANNEL_START]    = "CHAN_START",
	[AST_CEL_CHANNEL_END]      = "CHAN_END",
	[AST_CEL_ANSWER]           = "ANSWER",
	[AST_CEL_HANGUP]           = "HANGUP",
	[AST_CEL_APP_START]        = "APP_START",
	[AST_CEL_APP_END]          = "APP_END",
	[AST_CEL_BRIDGE_START]     = "BRIDGE_START",
	[AST_CEL_BRIDGE_END]       = "BRIDGE_END",
	[AST_CEL_BRIDGE_TO_CONF]   = "BRIDGE_TO_CONF",
	[AST_CEL_CONF_START]       = "CONF_START",
	[AST_CEL_CONF_END]         = "CONF_END",
	[AST_CEL_PARK_START]       = "PARK_START",
	[AST_CEL_PARK_END]         = "PARK_END",
	[AST_CEL_TRANSFER]         = "TRANSFER",
	[AST_CEL_USER_DEFINED]     = "USER_DEFINED",
	[AST_CEL_CONF_ENTER]       = "CONF_ENTER",
	[AST_CEL_CONF_EXIT]        = "CONF_EXIT",
	[AST_CEL_BLINDTRANSFER]    = "BLINDTRANSFER",
	[AST_CEL_ATTENDEDTRANSFER] = "ATTENDEDTRANSFER",
	[AST_CEL_PICKUP]           = "PICKUP",
	[AST_CEL_FORWARD]          = "FORWARD",
	[AST_CEL_3WAY_START]       = "3WAY_START",
	[AST_CEL_3WAY_END]         = "3WAY_END",
	[AST_CEL_HOOKFLASH]        = "HOOKFLASH",
	[AST_CEL_LINKEDID_END]     = "LINKEDID_END",
};

struct bridge_assoc {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(bridge_id);           /*!< UniqueID of the bridge */
		AST_STRING_FIELD(secondary_name);      /*!< UniqueID of the secondary/dialed channel */
	);
	struct ast_channel_snapshot *primary_snapshot; /*!< The snapshot for the initiating channel in the bridge */
	int track_as_conf;                             /*!< Whether this bridge will be treated like a conference in CEL terms */
};

static void bridge_assoc_dtor(void *obj)
{
	struct bridge_assoc *assoc = obj;
	ast_string_field_free_memory(assoc);
	ao2_cleanup(assoc->primary_snapshot);
	assoc->primary_snapshot = NULL;
}

static struct bridge_assoc *bridge_assoc_alloc(struct ast_channel_snapshot *primary, const char *bridge_id, const char *secondary_name)
{
	RAII_VAR(struct bridge_assoc *, assoc, ao2_alloc(sizeof(*assoc), bridge_assoc_dtor), ao2_cleanup);
	if (!primary || !assoc || ast_string_field_init(assoc, 64)) {
		return NULL;
	}

	ast_string_field_set(assoc, bridge_id, bridge_id);
	ast_string_field_set(assoc, secondary_name, secondary_name);

	assoc->primary_snapshot = primary;
	ao2_ref(primary, +1);

	ao2_ref(assoc, +1);
	return assoc;
}

static int add_bridge_primary(struct ast_channel_snapshot *primary, const char *bridge_id, const char *secondary_name)
{
	RAII_VAR(struct bridge_assoc *, assoc, bridge_assoc_alloc(primary, bridge_id, secondary_name), ao2_cleanup);
	if (!assoc) {
		return -1;
	}

	ao2_link(bridge_primaries, assoc);
	return 0;
}

static void remove_bridge_primary(const char *channel_id)
{
	ao2_find(bridge_primaries, channel_id, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK | OBJ_KEY);
}

/*! \brief Hashing function for bridge_assoc */
static int bridge_assoc_hash(const void *obj, int flags)
{
	const struct bridge_assoc *assoc = obj;
	const char *uniqueid = obj;
	if (!(flags & OBJ_KEY)) {
		uniqueid = assoc->primary_snapshot->uniqueid;
	}

	return ast_str_hash(uniqueid);
}

/*! \brief Comparator function for bridge_assoc */
static int bridge_assoc_cmp(void *obj, void *arg, int flags)
{
	struct bridge_assoc *assoc1 = obj, *assoc2 = arg;
	const char *assoc2_id = arg, *assoc1_id = assoc1->primary_snapshot->uniqueid;
	if (!(flags & OBJ_KEY)) {
		assoc2_id = assoc2->primary_snapshot->uniqueid;
	}

	return !strcmp(assoc1_id, assoc2_id) ? CMP_MATCH | CMP_STOP : 0;
}

static const char *get_caller_uniqueid(struct ast_multi_channel_blob *blob)
{
	struct ast_channel_snapshot *caller = ast_multi_channel_blob_get_channel(blob, "caller");
	if (!caller) {
		return NULL;
	}

	return caller->uniqueid;
}

/*! \brief Hashing function for dialstatus container */
static int dialstatus_hash(const void *obj, int flags)
{
	struct ast_multi_channel_blob *blob = (void *) obj;
	const char *uniqueid = obj;
	if (!(flags & OBJ_KEY)) {
		uniqueid = get_caller_uniqueid(blob);
	}

	return ast_str_hash(uniqueid);
}

/*! \brief Comparator function for dialstatus container */
static int dialstatus_cmp(void *obj, void *arg, int flags)
{
	struct ast_multi_channel_blob *blob1 = obj, *blob2 = arg;
	const char *blob2_id = arg, *blob1_id = get_caller_uniqueid(blob1);
	if (!(flags & OBJ_KEY)) {
		blob2_id = get_caller_uniqueid(blob2);
	}

	return !strcmp(blob1_id, blob2_id) ? CMP_MATCH | CMP_STOP : 0;
}

unsigned int ast_cel_check_enabled(void)
{
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);

	if (!cfg || !cfg->general) {
		return 0;
	}

	return cfg->general->enable;
}

static int print_app(void *obj, void *arg, int flags)
{
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, "CEL Tracking Application: %s\n", (const char *) obj);

	return 0;
}

static void print_cel_sub(const struct ast_event *event, void *data)
{
	struct ast_cli_args *a = data;

	ast_cli(a->fd, "CEL Event Subscriber: %s\n",
			ast_event_get_ie_str(event, AST_EVENT_IE_DESCRIPTION));
}

static char *handle_cli_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	unsigned int i;
	struct ast_event_sub *sub;
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);

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

	if (!cfg || !cfg->general) {
		return CLI_SUCCESS;
	}

	if (!cfg->general->enable) {
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

	ao2_callback(cfg->general->apps, OBJ_NODATA, print_app, a);

	if (!(sub = ast_event_subscribe_new(AST_EVENT_SUB, print_cel_sub, a))) {
		return CLI_FAILURE;
	}
	ast_event_sub_append_ie_uint(sub, AST_EVENT_IE_EVENTTYPE, AST_EVENT_CEL);
	ast_event_report_subs(sub);
	ast_event_sub_destroy(sub);
	sub = NULL;

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_status = AST_CLI_DEFINE(handle_cli_status, "Display the CEL status");

enum ast_cel_event_type ast_cel_str_to_event_type(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LEN(cel_event_types); i++) {
		if (!cel_event_types[i]) {
			continue;
		}

		if (!strcasecmp(name, cel_event_types[i])) {
			return i;
		}
	}

	return -1;
}

static int ast_cel_track_event(enum ast_cel_event_type et)
{
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);

	if (!cfg || !cfg->general) {
		return 0;
	}

	return (cfg->general->events & ((int64_t) 1 << et));
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

		if (event_type == 0) {
			/* All events */
			cfg->events = (int64_t) -1;
		} else if (event_type == -1) {
			ast_log(LOG_ERROR, "Unknown event name '%s'\n", cur_event);
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

static int do_reload(void)
{
	if (aco_process_config(&cel_cfg_info, 1) == ACO_PROCESS_ERROR) {
		return -1;
	}

	ast_verb(3, "CEL logging %sabled.\n", ast_cel_check_enabled() ? "en" : "dis");

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
	app = ao2_find(cfg->general->apps, app_lower, OBJ_KEY);
	if (!app) {
		return 0;
	}

	return 1;
}

static int cel_linkedid_ref(const char *linkedid);
struct ast_event *ast_cel_create_event(struct ast_channel_snapshot *snapshot,
		enum ast_cel_event_type event_type, const char *userdefevname,
		const char *extra, const char *peer_name)
{
	struct timeval eventtime = ast_tvnow();
	return ast_event_new(AST_EVENT_CEL,
		AST_EVENT_IE_CEL_EVENT_TYPE, AST_EVENT_IE_PLTYPE_UINT, event_type,
		AST_EVENT_IE_CEL_EVENT_TIME, AST_EVENT_IE_PLTYPE_UINT, eventtime.tv_sec,
		AST_EVENT_IE_CEL_EVENT_TIME_USEC, AST_EVENT_IE_PLTYPE_UINT, eventtime.tv_usec,
		AST_EVENT_IE_CEL_USEREVENT_NAME, AST_EVENT_IE_PLTYPE_STR, S_OR(userdefevname, ""),
		AST_EVENT_IE_CEL_CIDNAME, AST_EVENT_IE_PLTYPE_STR, snapshot->caller_name,
		AST_EVENT_IE_CEL_CIDNUM, AST_EVENT_IE_PLTYPE_STR, snapshot->caller_number,
		AST_EVENT_IE_CEL_CIDANI, AST_EVENT_IE_PLTYPE_STR, snapshot->caller_ani,
		AST_EVENT_IE_CEL_CIDRDNIS, AST_EVENT_IE_PLTYPE_STR, snapshot->caller_rdnis,
		AST_EVENT_IE_CEL_CIDDNID, AST_EVENT_IE_PLTYPE_STR, snapshot->caller_dnid,
		AST_EVENT_IE_CEL_EXTEN, AST_EVENT_IE_PLTYPE_STR, snapshot->exten,
		AST_EVENT_IE_CEL_CONTEXT, AST_EVENT_IE_PLTYPE_STR, snapshot->context,
		AST_EVENT_IE_CEL_CHANNAME, AST_EVENT_IE_PLTYPE_STR, snapshot->name,
		AST_EVENT_IE_CEL_APPNAME, AST_EVENT_IE_PLTYPE_STR, snapshot->appl,
		AST_EVENT_IE_CEL_APPDATA, AST_EVENT_IE_PLTYPE_STR, snapshot->data,
		AST_EVENT_IE_CEL_AMAFLAGS, AST_EVENT_IE_PLTYPE_UINT, snapshot->amaflags,
		AST_EVENT_IE_CEL_ACCTCODE, AST_EVENT_IE_PLTYPE_STR, snapshot->accountcode,
		AST_EVENT_IE_CEL_PEERACCT, AST_EVENT_IE_PLTYPE_STR, snapshot->peeraccount,
		AST_EVENT_IE_CEL_UNIQUEID, AST_EVENT_IE_PLTYPE_STR, snapshot->uniqueid,
		AST_EVENT_IE_CEL_LINKEDID, AST_EVENT_IE_PLTYPE_STR, snapshot->linkedid,
		AST_EVENT_IE_CEL_USERFIELD, AST_EVENT_IE_PLTYPE_STR, snapshot->userfield,
		AST_EVENT_IE_CEL_EXTRA, AST_EVENT_IE_PLTYPE_STR, S_OR(extra, ""),
		AST_EVENT_IE_CEL_PEER, AST_EVENT_IE_PLTYPE_STR, S_OR(peer_name, ""),
		AST_EVENT_IE_END);
}

static int report_event_snapshot(struct ast_channel_snapshot *snapshot,
		enum ast_cel_event_type event_type, const char *userdefevname,
		const char *extra, const char *peer2_name)
{
	struct ast_event *ev;
	char *linkedid = ast_strdupa(snapshot->linkedid);
	const char *peer_name = peer2_name;
	RAII_VAR(struct bridge_assoc *, assoc, NULL, ao2_cleanup);
	RAII_VAR(struct cel_config *, cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);

	if (!cfg || !cfg->general) {
		return 0;
	}

	if (!cfg->general->enable) {
		return 0;
	}

	if (ast_strlen_zero(peer_name)) {
		assoc = ao2_find(bridge_primaries, snapshot->uniqueid, OBJ_KEY);
		if (assoc) {
			peer_name = assoc->secondary_name;
		}
	}

	/* Record the linkedid of new channels if we are tracking LINKEDID_END even if we aren't
	 * reporting on CHANNEL_START so we can track when to send LINKEDID_END */
	if (ast_cel_track_event(AST_CEL_LINKEDID_END) && event_type == AST_CEL_CHANNEL_START && linkedid) {
		if (cel_linkedid_ref(linkedid)) {
			return -1;
		}
	}

	if (!ast_cel_track_event(event_type)) {
		return 0;
	}

	if ((event_type == AST_CEL_APP_START || event_type == AST_CEL_APP_END)
		&& !cel_track_app(snapshot->appl)) {
		return 0;
	}

	ev = ast_cel_create_event(snapshot, event_type, userdefevname, extra, peer_name);
	if (ev && ast_event_queue(ev)) {
		ast_event_destroy(ev);
		return -1;
	}

	return 0;
}

/* called whenever a channel is destroyed or a linkedid is changed to
 * potentially emit a CEL_LINKEDID_END event */
static void check_retire_linkedid(struct ast_channel_snapshot *snapshot)
{
	char *lid;

	/* make sure we need to do all this work */

	if (ast_strlen_zero(snapshot->linkedid) || !ast_cel_track_event(AST_CEL_LINKEDID_END)) {
		return;
	}

	if (!(lid = ao2_find(linkedids, (void *) snapshot->linkedid, OBJ_POINTER))) {
		ast_log(LOG_ERROR, "Something weird happened, couldn't find linkedid %s\n", snapshot->linkedid);
		return;
	}

	/* We have a ref for each channel with this linkedid, the link and the above find, so if
	 * before unreffing the channel we have a refcount of 3, we're done. Unlink and report. */
	if (ao2_ref(lid, -1) == 3) {
		ast_str_container_remove(linkedids, lid);
		report_event_snapshot(snapshot, AST_CEL_LINKEDID_END, NULL, NULL, NULL);
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
	ast_channel_uniqueid_set(tchan, record.unique_id);
	ast_channel_linkedid_set(tchan, record.linked_id);
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
	char *lid;

	if (ast_strlen_zero(linkedid)) {
		ast_log(LOG_ERROR, "The linkedid should never be empty\n");
		return -1;
	}

	if (!(lid = ao2_find(linkedids, (void *) linkedid, OBJ_POINTER))) {
		if (!(lid = ao2_alloc(strlen(linkedid) + 1, NULL))) {
			return -1;
		}
		strcpy(lid, linkedid);
		if (!ao2_link(linkedids, lid)) {
			ao2_ref(lid, -1);
			return -1;
		}
		/* Leave both the link and the alloc refs to show a count of 1 + the link */
	}
	/* If we've found, go ahead and keep the ref to increment count of how many channels
	 * have this linkedid. We'll clean it up in check_retire */
	return 0;
}

int ast_cel_report_event(struct ast_channel *chan, enum ast_cel_event_type event_type,
		const char *userdefevname, const char *extra, struct ast_channel *peer2)
{
	struct timeval eventtime;
	struct ast_event *ev;
	const char *peername = "";
	struct ast_channel *peer;
	char *linkedid = ast_strdupa(ast_channel_linkedid(chan));

	if (!ast_cel_check_enabled()) {
		return 0;
	}

	/* Record the linkedid of new channels if we are tracking LINKEDID_END even if we aren't
	 * reporting on CHANNEL_START so we can track when to send LINKEDID_END */
	if (ast_cel_track_event(AST_CEL_LINKEDID_END) && event_type == AST_CEL_CHANNEL_START && linkedid) {
		if (cel_linkedid_ref(linkedid)) {
			return -1;
		}
	}

	if (!ast_cel_track_event(event_type)) {
		return 0;
	}

	if ((event_type == AST_CEL_APP_START || event_type == AST_CEL_APP_END)
		&& !cel_track_app(ast_channel_appl(chan))) {
		return 0;
	}

	ast_channel_lock(chan);
	peer = ast_bridged_channel(chan);
	if (peer) {
		ast_channel_ref(peer);
	}
	ast_channel_unlock(chan);

	if (peer) {
		ast_channel_lock(peer);
		peername = ast_strdupa(ast_channel_name(peer));
		ast_channel_unlock(peer);
	} else if (peer2) {
		ast_channel_lock(peer2);
		peername = ast_strdupa(ast_channel_name(peer2));
		ast_channel_unlock(peer2);
	}

	if (!userdefevname) {
		userdefevname = "";
	}

	if (!extra) {
		extra = "";
	}

	eventtime = ast_tvnow();

	ast_channel_lock(chan);

	ev = ast_event_new(AST_EVENT_CEL,
		AST_EVENT_IE_CEL_EVENT_TYPE, AST_EVENT_IE_PLTYPE_UINT, event_type,
		AST_EVENT_IE_CEL_EVENT_TIME, AST_EVENT_IE_PLTYPE_UINT, eventtime.tv_sec,
		AST_EVENT_IE_CEL_EVENT_TIME_USEC, AST_EVENT_IE_PLTYPE_UINT, eventtime.tv_usec,
		AST_EVENT_IE_CEL_USEREVENT_NAME, AST_EVENT_IE_PLTYPE_STR, userdefevname,
		AST_EVENT_IE_CEL_CIDNAME, AST_EVENT_IE_PLTYPE_STR,
			S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, ""),
		AST_EVENT_IE_CEL_CIDNUM, AST_EVENT_IE_PLTYPE_STR,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, ""),
		AST_EVENT_IE_CEL_CIDANI, AST_EVENT_IE_PLTYPE_STR,
			S_COR(ast_channel_caller(chan)->ani.number.valid, ast_channel_caller(chan)->ani.number.str, ""),
		AST_EVENT_IE_CEL_CIDRDNIS, AST_EVENT_IE_PLTYPE_STR,
			S_COR(ast_channel_redirecting(chan)->from.number.valid, ast_channel_redirecting(chan)->from.number.str, ""),
		AST_EVENT_IE_CEL_CIDDNID, AST_EVENT_IE_PLTYPE_STR,
			S_OR(ast_channel_dialed(chan)->number.str, ""),
		AST_EVENT_IE_CEL_EXTEN, AST_EVENT_IE_PLTYPE_STR, ast_channel_exten(chan),
		AST_EVENT_IE_CEL_CONTEXT, AST_EVENT_IE_PLTYPE_STR, ast_channel_context(chan),
		AST_EVENT_IE_CEL_CHANNAME, AST_EVENT_IE_PLTYPE_STR, ast_channel_name(chan),
		AST_EVENT_IE_CEL_APPNAME, AST_EVENT_IE_PLTYPE_STR, S_OR(ast_channel_appl(chan), ""),
		AST_EVENT_IE_CEL_APPDATA, AST_EVENT_IE_PLTYPE_STR, S_OR(ast_channel_data(chan), ""),
		AST_EVENT_IE_CEL_AMAFLAGS, AST_EVENT_IE_PLTYPE_UINT, ast_channel_amaflags(chan),
		AST_EVENT_IE_CEL_ACCTCODE, AST_EVENT_IE_PLTYPE_STR, ast_channel_accountcode(chan),
		AST_EVENT_IE_CEL_PEERACCT, AST_EVENT_IE_PLTYPE_STR, ast_channel_peeraccount(chan),
		AST_EVENT_IE_CEL_UNIQUEID, AST_EVENT_IE_PLTYPE_STR, ast_channel_uniqueid(chan),
		AST_EVENT_IE_CEL_LINKEDID, AST_EVENT_IE_PLTYPE_STR, ast_channel_linkedid(chan),
		AST_EVENT_IE_CEL_USERFIELD, AST_EVENT_IE_PLTYPE_STR, ast_channel_userfield(chan),
		AST_EVENT_IE_CEL_EXTRA, AST_EVENT_IE_PLTYPE_STR, extra,
		AST_EVENT_IE_CEL_PEER, AST_EVENT_IE_PLTYPE_STR, peername,
		AST_EVENT_IE_END);

	ast_channel_unlock(chan);

	if (peer) {
		peer = ast_channel_unref(peer);
	}

	if (ev && ast_event_queue(ev)) {
		ast_event_destroy(ev);
		return -1;
	}

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
	r->peer_account     = S_OR(ast_event_get_ie_str(e, AST_EVENT_IE_CEL_ACCTCODE), "");
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
	struct ast_channel_snapshot *new_snapshot);

static struct ast_multi_channel_blob *get_dialstatus_blob(const char *uniqueid)
{
	return ao2_find(cel_dialstatus_store, uniqueid, OBJ_KEY | OBJ_UNLINK);
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
	struct ast_channel_snapshot *new_snapshot)
{
	int is_hungup, was_hungup;

	if (!new_snapshot) {
		report_event_snapshot(old_snapshot, AST_CEL_CHANNEL_END, NULL, NULL, NULL);
		check_retire_linkedid(old_snapshot);
		return;
	}

	if (!old_snapshot) {
		report_event_snapshot(new_snapshot, AST_CEL_CHANNEL_START, NULL, NULL, NULL);
		return;
	}

	was_hungup = ast_test_flag(&old_snapshot->flags, AST_FLAG_DEAD) ? 1 : 0;
	is_hungup = ast_test_flag(&new_snapshot->flags, AST_FLAG_DEAD) ? 1 : 0;

	if (!was_hungup && is_hungup) {
		RAII_VAR(struct ast_str *, extra_str, ast_str_create(128), ast_free);
		RAII_VAR(struct ast_multi_channel_blob *, blob, get_dialstatus_blob(new_snapshot->uniqueid), ao2_cleanup);
		const char *dialstatus = "";
		if (blob && !ast_strlen_zero(get_blob_variable(blob, "dialstatus"))) {
			dialstatus = get_blob_variable(blob, "dialstatus");
		}
		ast_str_set(&extra_str, 0, "%d,%s,%s",
			new_snapshot->hangupcause,
			new_snapshot->hangupsource,
			dialstatus);
		report_event_snapshot(new_snapshot, AST_CEL_HANGUP, NULL, ast_str_buffer(extra_str), NULL);
		return;
	}

	if (old_snapshot->state != new_snapshot->state && new_snapshot->state == AST_STATE_UP) {
		report_event_snapshot(new_snapshot, AST_CEL_ANSWER, NULL, NULL, NULL);
		return;
	}
}

static void cel_channel_linkedid_change(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	if (!old_snapshot || !new_snapshot) {
		return;
	}

	ast_assert(!ast_strlen_zero(new_snapshot->linkedid));
	ast_assert(!ast_strlen_zero(old_snapshot->linkedid));

	if (strcmp(old_snapshot->linkedid, new_snapshot->linkedid)) {
		cel_linkedid_ref(new_snapshot->linkedid);
		check_retire_linkedid(old_snapshot);
	}
}

static void cel_channel_app_change(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	if (new_snapshot && old_snapshot
		&& !strcmp(old_snapshot->appl, new_snapshot->appl)) {
		return;
	}

	/* old snapshot has an application, end it */
	if (old_snapshot && !ast_strlen_zero(old_snapshot->appl)) {
		report_event_snapshot(old_snapshot, AST_CEL_APP_END, NULL, NULL, NULL);
	}

	/* new snapshot has an application, start it */
	if (new_snapshot && !ast_strlen_zero(new_snapshot->appl)) {
		report_event_snapshot(new_snapshot, AST_CEL_APP_START, NULL, NULL, NULL);
	}
}

/* \brief Handlers for channel snapshot changes.
 * \note Order of the handlers matters. Application changes must come before state
 * changes to ensure that hangup notifications occur after application changes.
 * Linkedid checking should always come last.
 */
cel_channel_snapshot_monitor cel_channel_monitors[] = {
	cel_channel_app_change,
	cel_channel_state_change,
	cel_channel_linkedid_change,
};

static void update_bridge_primary(struct ast_channel_snapshot *snapshot)
{
	RAII_VAR(struct bridge_assoc *, assoc, NULL, ao2_cleanup);

	if (!snapshot) {
		return;
	}

	assoc = ao2_find(bridge_primaries, snapshot->uniqueid, OBJ_KEY);
	if (!assoc) {
		return;
	}

	ao2_cleanup(assoc->primary_snapshot);
	ao2_ref(snapshot, +1);
	assoc->primary_snapshot = snapshot;
}

static int bridge_match_cb(void *obj, void *arg, int flags)
{
	struct bridge_assoc *assoc = obj;
	char *bridge_id = arg;
	if (!strcmp(bridge_id, assoc->bridge_id)) {
		return CMP_MATCH;
	}
	return 0;
}

static struct bridge_assoc *find_bridge_primary_by_bridge_id(const char *bridge_id)
{
	char *dup_id = ast_strdupa(bridge_id);
	return ao2_callback(bridge_primaries, 0, bridge_match_cb, dup_id);
}

static void clear_bridge_primary(const char *bridge_id)
{
	char *dup_id = ast_strdupa(bridge_id);
	ao2_callback(bridge_primaries, OBJ_KEY | OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, bridge_match_cb, dup_id);
}

static void cel_snapshot_update_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct stasis_cache_update *update = stasis_message_data(message);
	if (ast_channel_snapshot_type() == update->type) {
		struct ast_channel_snapshot *old_snapshot;
		struct ast_channel_snapshot *new_snapshot;
		size_t i;

		old_snapshot = stasis_message_data(update->old_snapshot);
		new_snapshot = stasis_message_data(update->new_snapshot);

		update_bridge_primary(new_snapshot);

		for (i = 0; i < ARRAY_LEN(cel_channel_monitors); ++i) {
			cel_channel_monitors[i](old_snapshot, new_snapshot);
		}
	} else if (ast_bridge_snapshot_type() == update->type) {
		struct ast_bridge_snapshot *old_snapshot;
		struct ast_bridge_snapshot *new_snapshot;

		old_snapshot = stasis_message_data(update->old_snapshot);
		new_snapshot = stasis_message_data(update->new_snapshot);

		if (!old_snapshot) {
			return;
		}

		if (!new_snapshot) {
			clear_bridge_primary(old_snapshot->uniqueid);
			return;
		}
	}
}

static void cel_bridge_enter_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	struct ast_bridge_snapshot *snapshot = blob->bridge;
	struct ast_channel_snapshot *chan_snapshot = blob->channel;
	RAII_VAR(struct bridge_assoc *, assoc, find_bridge_primary_by_bridge_id(snapshot->uniqueid), ao2_cleanup);

	if (snapshot->capabilities & (AST_BRIDGE_CAPABILITY_1TO1MIX | AST_BRIDGE_CAPABILITY_NATIVE)) {
		if (assoc && assoc->track_as_conf) {
			report_event_snapshot(chan_snapshot, AST_CEL_CONF_ENTER, NULL, NULL, NULL);
			return;
		}

		if (ao2_container_count(snapshot->channels) == 2) {
			struct ao2_iterator i;
			RAII_VAR(char *, channel_id, NULL, ao2_cleanup);
			RAII_VAR(struct ast_channel_snapshot *, latest_primary, NULL, ao2_cleanup);

			/* get the name of the channel in the container we don't already know the name of */
			i = ao2_iterator_init(snapshot->channels, 0);
			while ((channel_id = ao2_iterator_next(&i))) {
				if (strcmp(channel_id, chan_snapshot->uniqueid)) {
					break;
				}
				ao2_cleanup(channel_id);
				channel_id = NULL;
			}
			ao2_iterator_destroy(&i);

			latest_primary = ast_channel_snapshot_get_latest(channel_id);
			if (!latest_primary) {
				return;
			}

			add_bridge_primary(latest_primary, snapshot->uniqueid, chan_snapshot->name);
			report_event_snapshot(latest_primary, AST_CEL_BRIDGE_START, NULL, NULL, chan_snapshot->name);
		} else if (ao2_container_count(snapshot->channels) > 2) {
			if (!assoc) {
				ast_log(LOG_ERROR, "No association found for bridge %s\n", snapshot->uniqueid);
				return;
			}

			/* this bridge will no longer be treated like a bridge, so mark the bridge_assoc as such */
			if (!assoc->track_as_conf) {
				assoc->track_as_conf = 1;
				report_event_snapshot(assoc->primary_snapshot, AST_CEL_BRIDGE_TO_CONF, NULL,
					chan_snapshot->name, assoc->secondary_name);
				ast_string_field_set(assoc, secondary_name, "");
			}
		}
	} else if (snapshot->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) {
		if (!assoc) {
			add_bridge_primary(chan_snapshot, snapshot->uniqueid, "");
			return;
		}
		report_event_snapshot(chan_snapshot, AST_CEL_CONF_ENTER, NULL, NULL, NULL);
	}
}

static void cel_bridge_leave_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	struct ast_bridge_snapshot *snapshot = blob->bridge;
	struct ast_channel_snapshot *chan_snapshot = blob->channel;

	if (snapshot->capabilities & (AST_BRIDGE_CAPABILITY_1TO1MIX | AST_BRIDGE_CAPABILITY_NATIVE)) {
		RAII_VAR(struct bridge_assoc *, assoc,
			find_bridge_primary_by_bridge_id(snapshot->uniqueid),
			ao2_cleanup);

		if (!assoc) {
			return;
		}

		if (assoc->track_as_conf) {
			report_event_snapshot(chan_snapshot, AST_CEL_CONF_EXIT, NULL, NULL, NULL);
			return;
		}

		if (ao2_container_count(snapshot->channels) == 1) {
			report_event_snapshot(assoc->primary_snapshot, AST_CEL_BRIDGE_END, NULL, NULL, assoc->secondary_name);
			remove_bridge_primary(assoc->primary_snapshot->uniqueid);
			return;
		}
	} else if (snapshot->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) {
		report_event_snapshot(chan_snapshot, AST_CEL_CONF_EXIT, NULL, NULL, NULL);
	}
}

static void cel_parking_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_parked_call_payload *parked_payload = stasis_message_data(message);

	switch (parked_payload->event_type) {
	case PARKED_CALL:
		report_event_snapshot(parked_payload->parkee, AST_CEL_PARK_START, NULL,
			parked_payload->parkinglot,
			parked_payload->parker_dial_string);
		break;
	case PARKED_CALL_TIMEOUT:
		report_event_snapshot(parked_payload->parkee, AST_CEL_PARK_END, NULL, "ParkedCallTimeOut", NULL);
		break;
	case PARKED_CALL_GIVEUP:
		report_event_snapshot(parked_payload->parkee, AST_CEL_PARK_END, NULL, "ParkedCallGiveUp", NULL);
		break;
	case PARKED_CALL_UNPARKED:
		report_event_snapshot(parked_payload->parkee, AST_CEL_PARK_END, NULL, "ParkedCallUnparked", NULL);
		break;
	case PARKED_CALL_FAILED:
		report_event_snapshot(parked_payload->parkee, AST_CEL_PARK_END, NULL, "ParkedCallFailed", NULL);
		break;
	}
}

static void save_dialstatus(struct ast_multi_channel_blob *blob)
{
	ao2_link(cel_dialstatus_store, blob);
}

static void cel_dial_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_multi_channel_blob *blob = stasis_message_data(message);

	if (!get_caller_uniqueid(blob)) {
		return;
	}

	if (!ast_strlen_zero(get_blob_variable(blob, "forward"))) {
		struct ast_channel_snapshot *caller = ast_multi_channel_blob_get_channel(blob, "caller");
		if (!caller) {
			return;
		}

		report_event_snapshot(caller, AST_CEL_FORWARD, NULL, get_blob_variable(blob, "forward"), NULL);
	}

	if (ast_strlen_zero(get_blob_variable(blob, "dialstatus"))) {
		return;
	}

	save_dialstatus(blob);
}

static void cel_generic_cb(
	void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	int event_type = ast_json_integer_get(ast_json_object_get(obj->blob, "event_type"));
	struct ast_json *event_details = ast_json_object_get(obj->blob, "event_details");

	switch (event_type) {
	case AST_CEL_USER_DEFINED:
		{
			const char *event = ast_json_string_get(ast_json_object_get(event_details, "event"));
			const char *extra = ast_json_string_get(ast_json_object_get(event_details, "extra"));
			report_event_snapshot(obj->snapshot, event_type, event, extra, NULL);
			break;
		}
	default:
		ast_log(LOG_ERROR, "Unhandled %s event blob\n", ast_cel_get_type_name(event_type));
		break;
	}
}

static void ast_cel_engine_term(void)
{
	aco_info_destroy(&cel_cfg_info);
	ao2_global_obj_release(cel_configs);
	stasis_message_router_unsubscribe_and_join(cel_state_router);
	cel_state_router = NULL;
	ao2_cleanup(cel_aggregation_topic);
	cel_aggregation_topic = NULL;
	ao2_cleanup(cel_topic);
	cel_topic = NULL;
	cel_channel_forwarder = stasis_unsubscribe_and_join(cel_channel_forwarder);
	cel_bridge_forwarder = stasis_unsubscribe_and_join(cel_bridge_forwarder);
	cel_parking_forwarder = stasis_unsubscribe_and_join(cel_parking_forwarder);
	cel_cel_forwarder = stasis_unsubscribe_and_join(cel_cel_forwarder);
	ao2_cleanup(bridge_primaries);
	bridge_primaries = NULL;
	ast_cli_unregister(&cli_status);
	ao2_cleanup(cel_dialstatus_store);
	cel_dialstatus_store = NULL;
	ao2_cleanup(linkedids);
	linkedids = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(cel_generic_type);
}

int ast_cel_engine_init(void)
{
	int ret = 0;
	if (!(linkedids = ast_str_container_alloc(NUM_APP_BUCKETS))) {
		return -1;
	}

	if (!(cel_dialstatus_store = ao2_container_alloc(NUM_DIALSTATUS_BUCKETS, dialstatus_hash, dialstatus_cmp))) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(cel_generic_type)) {
		return -1;
	}

	if (ast_cli_register(&cli_status)) {
		return -1;
	}

	bridge_primaries = ao2_container_alloc(BRIDGE_PRIMARY_BUCKETS, bridge_assoc_hash, bridge_assoc_cmp);
	if (!bridge_primaries) {
		return -1;
	}

	cel_aggregation_topic = stasis_topic_create("cel_aggregation_topic");
	if (!cel_aggregation_topic) {
		return -1;
	}

	cel_topic = stasis_topic_create("cel_topic");
	if (!cel_topic) {
		return -1;
	}

	cel_channel_forwarder = stasis_forward_all(
		stasis_caching_get_topic(ast_channel_topic_all_cached()),
		cel_aggregation_topic);
	if (!cel_channel_forwarder) {
		return -1;
	}

	cel_bridge_forwarder = stasis_forward_all(
		stasis_caching_get_topic(ast_bridge_topic_all_cached()),
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

	cel_state_router = stasis_message_router_create(cel_aggregation_topic);
	if (!cel_state_router) {
		return -1;
	}

	ret |= stasis_message_router_add(cel_state_router,
		stasis_cache_update_type(),
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

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		ast_cel_engine_term();
		return -1;
	}

	if (aco_info_init(&cel_cfg_info)) {
		return -1;
	}

	aco_option_register(&cel_cfg_info, "enable", ACO_EXACT, general_options, "no", OPT_BOOL_T, 1, FLDSET(struct ast_cel_general_config, enable));
	aco_option_register(&cel_cfg_info, "dateformat", ACO_EXACT, general_options, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_cel_general_config, date_format));
	aco_option_register_custom(&cel_cfg_info, "apps", ACO_EXACT, general_options, "", apps_handler, 0);
	aco_option_register_custom(&cel_cfg_info, "events", ACO_EXACT, general_options, "", events_handler, 0);

	aco_process_config(&cel_cfg_info, 0);

	ast_register_cleanup(ast_cel_engine_term);

	return 0;
}

int ast_cel_engine_reload(void)
{
	return do_reload();
}

void ast_cel_publish_event(struct ast_channel *chan,
	enum ast_cel_event_type event_type,
	struct ast_json *blob)
{
	RAII_VAR(struct ast_channel_blob *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, cel_blob, NULL, ast_json_unref);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	cel_blob = ast_json_pack("{s: i, s: O}",
		"event_type", event_type,
		"event_details", blob);

	message = ast_channel_blob_create(chan, cel_generic_type(), cel_blob);
	if (message) {
		stasis_publish(ast_cel_topic(), message);
	}
}

struct stasis_topic *ast_cel_topic(void)
{
	return cel_topic;
}

struct ast_cel_general_config *ast_cel_get_config(void)
{
	RAII_VAR(struct cel_config *, mod_cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);
	ao2_ref(mod_cfg->general, +1);
	return mod_cfg->general;
}

void ast_cel_set_config(struct ast_cel_general_config *config)
{
	RAII_VAR(struct cel_config *, mod_cfg, ao2_global_obj_ref(cel_configs), ao2_cleanup);
	ao2_cleanup(mod_cfg->general);
	mod_cfg->general = config;
	ao2_ref(mod_cfg->general, +1);
}

