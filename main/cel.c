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
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"

/*! Config file to load for the CEL feature. */
static const char cel_conf_file[] = "cel.conf";

/*! Is the CEL subsystem enabled ? */
static unsigned char cel_enabled;

/*! \brief CEL is off by default */
#define CEL_ENABLED_DEFAULT		0

/*!
 * \brief which events we want to track
 *
 * \note bit field, up to 64 events
 */
static int64_t eventset;

/*!
 * \brief Maximum possible CEL event IDs
 * \note This limit is currently imposed by the eventset definition
 */
#define CEL_MAX_EVENT_IDS 64

/*!
 * \brief Track no events by default.
 */
#define CEL_DEFAULT_EVENTS	0

/*!
 * \brief Number of buckets for the appset container
 */
#define NUM_APP_BUCKETS		97

/*!
 * \brief Lock protecting CEL.
 *
 * \note It protects during reloads, shutdown, and accesses to
 * the appset and linkedids containers.
 */
AST_MUTEX_DEFINE_STATIC(reload_lock);

/*!
 * \brief Container of Asterisk application names
 *
 * The apps in this container are the applications that were specified
 * in the configuration as applications that CEL events should be generated
 * for when they start and end on a channel.
 *
 * \note Accesses to the appset container must be done while
 * holding the reload_lock.
 */
static struct ao2_container *appset;

struct cel_linkedid {
	/*! Number of channels with this linkedid. */
	unsigned int count;
	/*! Linkedid stored at end of struct. */
	char id[0];
};

/*!
 * \brief Container of channel references to a linkedid for CEL purposes.
 *
 * \note Accesses to the linkedids container must be done while
 * holding the reload_lock.
 */
static struct ao2_container *linkedids;

/*!
 * \brief Configured date format for event timestamps
 */
static char cel_dateformat[256];

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
	[AST_CEL_BRIDGE_START]     = "BRIDGE_START",
	[AST_CEL_BRIDGE_END]       = "BRIDGE_END",
	[AST_CEL_BRIDGE_UPDATE]    = "BRIDGE_UPDATE",
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

/*!
 * \brief Map of ast_cel_ama_flags to strings
 */
static const char * const cel_ama_flags[AST_CEL_AMA_FLAG_TOTAL] = {
	[AST_CEL_AMA_FLAG_NONE]          = "NONE",
	[AST_CEL_AMA_FLAG_OMIT]          = "OMIT",
	[AST_CEL_AMA_FLAG_BILLING]       = "BILLING",
	[AST_CEL_AMA_FLAG_DOCUMENTATION] = "DOCUMENTATION",
};

unsigned int ast_cel_check_enabled(void)
{
	return cel_enabled;
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

	ast_cli(a->fd, "CEL Logging: %s\n", cel_enabled ? "Enabled" : "Disabled");

	if (!cel_enabled) {
		return CLI_SUCCESS;
	}

	for (i = 0; i < (sizeof(eventset) * 8); i++) {
		const char *name;

		if (!(eventset & ((int64_t) 1 << i))) {
			continue;
		}

		name = ast_cel_get_type_name(i);
		if (strcasecmp(name, "Unknown")) {
			ast_cli(a->fd, "CEL Tracking Event: %s\n", name);
		}
	}

	/* Accesses to the appset container must be done while holding the reload_lock. */
	ast_mutex_lock(&reload_lock);
	if (appset) {
		struct ao2_iterator iter;
		char *app;

		iter = ao2_iterator_init(appset, 0);
		for (;;) {
			app = ao2_iterator_next(&iter);
			if (!app) {
				break;
			}
			ast_mutex_unlock(&reload_lock);

			ast_cli(a->fd, "CEL Tracking Application: %s\n", app);

			ao2_ref(app, -1);
			ast_mutex_lock(&reload_lock);
		}
		ao2_iterator_destroy(&iter);
	}
	ast_mutex_unlock(&reload_lock);

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
		if (cel_event_types[i] && !strcasecmp(name, cel_event_types[i])) {
			return i;
		}
	}

	ast_log(LOG_ERROR, "Unknown event name '%s'\n", name);
	return -1;
}

static int ast_cel_track_event(enum ast_cel_event_type et)
{
	return (eventset & ((int64_t) 1 << et));
}

static void parse_events(const char *val)
{
	char *events = ast_strdupa(val);
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
			eventset = (int64_t) -1;
		} else if (event_type == AST_CEL_INVALID_VALUE) {
			ast_log(LOG_WARNING, "Unknown event name '%s'\n",
					cur_event);
		} else {
			eventset |= ((int64_t) 1 << event_type);
		}
	}
}

static void parse_apps(const char *val)
{
	char *apps = ast_strdupa(val);
	char *cur_app;

	if (!ast_cel_track_event(AST_CEL_APP_START) && !ast_cel_track_event(AST_CEL_APP_END)) {
		ast_log(LOG_WARNING, "An apps= config line, but not tracking APP events\n");
		return;
	}

	while ((cur_app = strsep(&apps, ","))) {
		char *app;

		cur_app = ast_strip(cur_app);
		if (ast_strlen_zero(cur_app)) {
			continue;
		}

		/* The app object is immutable so it doesn't need a lock of its own. */
		app = ao2_alloc_options(strlen(cur_app) + 1, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!app) {
			continue;
		}
		strcpy(app, cur_app);/* Safe */

		ao2_link(appset, app);
		ao2_ref(app, -1);
		app = NULL;
	}
}

static void set_defaults(void)
{
	cel_enabled = CEL_ENABLED_DEFAULT;
	eventset = CEL_DEFAULT_EVENTS;
	*cel_dateformat = '\0';
	ao2_callback(appset, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
}

static int do_reload(int is_reload)
{
	struct ast_config *config;
	const char *enabled_value;
	const char *val;
	int res = 0;
	struct ast_flags config_flags = { 0, };
	const char *s;

	ast_mutex_lock(&reload_lock);

	if (!is_reload) {
		/* Initialize all settings before first configuration load. */
		set_defaults();
	}

	/*
	 * Unfortunately we have to always load the config file because
	 * other modules read the same file.
	 */
	config = ast_config_load2(cel_conf_file, "cel", config_flags);
	if (!config || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Could not load %s\n", cel_conf_file);
		config = NULL;
		goto return_cleanup;
	}
	if (config == CONFIG_STATUS_FILEUNCHANGED) {
		/* This should never happen because we always load the config file. */
		config = NULL;
		goto return_cleanup;
	}

	if (is_reload) {
		/* Reset all settings before reloading configuration */
		set_defaults();
	}

	if ((enabled_value = ast_variable_retrieve(config, "general", "enable"))) {
		cel_enabled = ast_true(enabled_value);
	}

	if (!cel_enabled) {
		goto return_cleanup;
	}

	/* get the date format for logging */
	if ((s = ast_variable_retrieve(config, "general", "dateformat"))) {
		ast_copy_string(cel_dateformat, s, sizeof(cel_dateformat));
	}

	if ((val = ast_variable_retrieve(config, "general", "events"))) {
		parse_events(val);
	}

	if ((val = ast_variable_retrieve(config, "general", "apps"))) {
		parse_apps(val);
	}

return_cleanup:
	ast_verb(3, "CEL logging %sabled.\n", cel_enabled ? "en" : "dis");

	ast_mutex_unlock(&reload_lock);

	if (config) {
		ast_config_destroy(config);
	}

	return res;
}

const char *ast_cel_get_type_name(enum ast_cel_event_type type)
{
	return S_OR(cel_event_types[type], "Unknown");
}

const char *ast_cel_get_ama_flag_name(enum ast_cel_ama_flag flag)
{
	if (flag >= ARRAY_LEN(cel_ama_flags)) {
		ast_log(LOG_WARNING, "Invalid AMA flag: %u\n", flag);
		return "Unknown";
	}

	return S_OR(cel_ama_flags[flag], "Unknown");
}

/* called whenever a channel is destroyed or a linkedid is changed to
 * potentially emit a CEL_LINKEDID_END event */
void ast_cel_check_retire_linkedid(struct ast_channel *chan)
{
	const char *linkedid = ast_channel_linkedid(chan);
	struct cel_linkedid *lid;

	if (ast_strlen_zero(linkedid)) {
		return;
	}

	/* Get the lock in case any CEL events are still in flight when we shutdown. */
	ast_mutex_lock(&reload_lock);

	if (!cel_enabled || !ast_cel_track_event(AST_CEL_LINKEDID_END)
		|| !linkedids) {
		/*
		 * CEL is disabled or we are not tracking linkedids
		 * or the CEL module is shutdown.
		 */
		ast_mutex_unlock(&reload_lock);
		return;
	}

	lid = ao2_find(linkedids, (void *) linkedid, OBJ_KEY);
	if (!lid) {
		ast_mutex_unlock(&reload_lock);

		/*
		 * The user may have done a reload to start tracking linkedids
		 * when a call was already in progress.  This is an unusual kind
		 * of change to make after starting Asterisk.
		 */
		ast_log(LOG_ERROR, "Something weird happened, couldn't find linkedid %s\n", linkedid);
		return;
	}

	if (!--lid->count) {
		/* No channels use this linkedid anymore. */
		ao2_unlink(linkedids, lid);
		ast_mutex_unlock(&reload_lock);

		ast_cel_report_event(chan, AST_CEL_LINKEDID_END, NULL, NULL, NULL);
	} else {
		ast_mutex_unlock(&reload_lock);
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

	if (ast_strlen_zero(cel_dateformat)) {
		snprintf(timebuf, sizeof(timebuf), "%ld.%06ld", (long) record.event_time.tv_sec,
				(long) record.event_time.tv_usec);
	} else {
		struct ast_tm tm;
		ast_localtime(&record.event_time, &tm, NULL);
		ast_strftime(timebuf, sizeof(timebuf), cel_dateformat, &tm);
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

int ast_cel_linkedid_ref(const char *linkedid)
{
	struct cel_linkedid *lid;

	if (ast_strlen_zero(linkedid)) {
		ast_log(LOG_ERROR, "The linkedid should never be empty\n");
		return -1;
	}

	/* Get the lock in case any CEL events are still in flight when we shutdown. */
	ast_mutex_lock(&reload_lock);

	if (!cel_enabled || !ast_cel_track_event(AST_CEL_LINKEDID_END)) {
		/* CEL is disabled or we are not tracking linkedids. */
		ast_mutex_unlock(&reload_lock);
		return 0;
	}
	if (!linkedids) {
		/* The CEL module is shutdown.  Abort. */
		ast_mutex_unlock(&reload_lock);
		return -1;
	}

	lid = ao2_find(linkedids, (void *) linkedid, OBJ_KEY);
	if (!lid) {
		/*
		 * Changes to the lid->count member are protected by the
		 * reload_lock so the lid object does not need its own lock.
		 */
		lid = ao2_alloc_options(sizeof(*lid) + strlen(linkedid) + 1, NULL,
			AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!lid) {
			ast_mutex_unlock(&reload_lock);
			return -1;
		}
		strcpy(lid->id, linkedid);/* Safe */

		ao2_link(linkedids, lid);
	}
	++lid->count;
	ast_mutex_unlock(&reload_lock);
	ao2_ref(lid, -1);

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

	/* Make sure a reload is not occurring while we're checking to see if this
	 * is an event that we care about.  We could lose an important event in this
	 * process otherwise. */
	ast_mutex_lock(&reload_lock);

	if (!appset) {
		/* The CEL module is shutdown.  Abort. */
		ast_mutex_unlock(&reload_lock);
		return -1;
	}

	/* Record the linkedid of new channels if we are tracking LINKEDID_END even if we aren't
	 * reporting on CHANNEL_START so we can track when to send LINKEDID_END */
	if (cel_enabled && ast_cel_track_event(AST_CEL_LINKEDID_END) && event_type == AST_CEL_CHANNEL_START && linkedid) {
		if (ast_cel_linkedid_ref(linkedid)) {
			ast_mutex_unlock(&reload_lock);
			return -1;
		}
	}

	if (!cel_enabled || !ast_cel_track_event(event_type)) {
		ast_mutex_unlock(&reload_lock);
		return 0;
	}

	if (event_type == AST_CEL_APP_START || event_type == AST_CEL_APP_END) {
		char *app;
		if (!(app = ao2_find(appset, (char *) ast_channel_appl(chan), OBJ_POINTER))) {
			ast_mutex_unlock(&reload_lock);
			return 0;
		}
		ao2_ref(app, -1);
	}

	ast_mutex_unlock(&reload_lock);

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

static int app_hash(const void *obj, const int flags)
{
	return ast_str_case_hash((const char *) obj);
}

static int app_cmp(void *obj, void *arg, int flags)
{
	const char *app1 = obj;
	const char *app2 = arg;

	return !strcasecmp(app1, app2) ? CMP_MATCH : 0;
}

static int lid_hash(const void *obj, const int flags)
{
	const struct cel_linkedid *lid = obj;
	const char *key = obj;

	switch (flags & (OBJ_POINTER | OBJ_KEY)) {
	case OBJ_POINTER:
	default:
		key = lid->id;
		break;
	case OBJ_KEY:
		break;
	}

	return ast_str_case_hash(key);
}

static int lid_cmp(void *obj, void *arg, int flags)
{
	struct cel_linkedid *lid1 = obj;
	struct cel_linkedid *lid2 = arg;
	const char *key = arg;

	switch (flags & (OBJ_POINTER | OBJ_KEY)) {
	case OBJ_POINTER:
	default:
		key = lid2->id;
		break;
	case OBJ_KEY:
		break;
	}

	return !strcasecmp(lid1->id, key) ? CMP_MATCH : 0;
}

static void ast_cel_engine_term(void)
{
	/* Get the lock in case any CEL events are still in flight when we shutdown. */
	ast_mutex_lock(&reload_lock);

	ao2_cleanup(appset);
	appset = NULL;
	ao2_cleanup(linkedids);
	linkedids = NULL;

	ast_mutex_unlock(&reload_lock);

	ast_cli_unregister(&cli_status);
}

int ast_cel_engine_init(void)
{
	/*
	 * Accesses to the appset and linkedids containers have to be
	 * protected by the reload_lock so they don't need a lock of
	 * their own.
	 */
	appset = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, NUM_APP_BUCKETS,
		app_hash, app_cmp);
	if (!appset) {
		return -1;
	}
	linkedids = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, NUM_APP_BUCKETS,
		lid_hash, lid_cmp);
	if (!linkedids) {
		ast_cel_engine_term();
		return -1;
	}

	if (do_reload(0) || ast_cli_register(&cli_status)) {
		ast_cel_engine_term();
		return -1;
	}

	ast_register_cleanup(ast_cel_engine_term);

	return 0;
}

int ast_cel_engine_reload(void)
{
	return do_reload(1);
}

