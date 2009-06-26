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

/*! Is the CEL subsystem enabled ? */
static unsigned char cel_enabled;

/*! \brief CEL is off by default */
static const unsigned char CEL_ENALBED_DEFAULT = 0;

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
static const int64_t CEL_DEFAULT_EVENTS = 0;

/*!
 * \brief Number of buckets for the appset container
 */
static const int NUM_APP_BUCKETS = 97;

/*!
 * \brief Container of Asterisk application names
 *
 * The apps in this container are the applications that were specified
 * in the configuration as applications that CEL events should be generated
 * for when they start and end on a channel.
 */
static struct ao2_container *appset;

/*!
 * \brief Configured date format for event timestamps
 */
static char cel_dateformat[256];

/*!
 * \brief Map of ast_cel_event_type to strings
 */
static const char const *cel_event_types[CEL_MAX_EVENT_IDS] = {
	[0]                        = "ALL",
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
static const char const *cel_ama_flags[AST_CEL_AMA_FLAG_TOTAL] = {
	[AST_CEL_AMA_FLAG_OMIT]          = "OMIT",
	[AST_CEL_AMA_FLAG_BILLING]       = "BILLING",
	[AST_CEL_AMA_FLAG_DOCUMENTATION] = "DOCUMENTATION",
};

unsigned int ast_cel_check_enabled(void)
{
	return cel_enabled;
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

	ao2_callback(appset, OBJ_NODATA, print_app, a);

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

		if (event_type == 0) {
			/* All events */
			eventset = (int64_t) -1;
		} else if (event_type == -1) {
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

		if (!(app = ao2_alloc(strlen(cur_app) + 1, NULL))) {
			continue;
		}
		strcpy(app, cur_app);

		ao2_link(appset, app);
		ao2_ref(app, -1);
		app = NULL;
	}
}

AST_MUTEX_DEFINE_STATIC(reload_lock);

static int do_reload(void)
{
	struct ast_config *config;
	const char *enabled_value;
	const char *val;
	int res = 0;
	struct ast_flags config_flags = { 0, };
	const char *s;

	ast_mutex_lock(&reload_lock);

	/* Reset all settings before reloading configuration */
	cel_enabled = CEL_ENALBED_DEFAULT;
	eventset = CEL_DEFAULT_EVENTS;
	*cel_dateformat = '\0';
	ao2_callback(appset, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	config = ast_config_load2("cel.conf", "cel", config_flags);

	if (config == CONFIG_STATUS_FILEMISSING) {
		config = NULL;
		goto return_cleanup;
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
	return S_OR(cel_ama_flags[flag], "Unknown");
}

/* called whenever a channel is destroyed or a linkedid is changed to
 * potentially emit a CEL_LINKEDID_END event */

struct channel_find_data {
	const struct ast_channel *chan;
	const char *linkedid;
};

static int linkedid_match(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *c = obj;
	struct channel_find_data *find_dat = data;
	int res;

	ast_channel_lock(c);
	res = (c != find_dat->chan && c->linkedid && !strcmp(find_dat->linkedid, c->linkedid));
	ast_channel_unlock(c);

	return res ? CMP_MATCH | CMP_STOP : 0;
}

void ast_cel_check_retire_linkedid(struct ast_channel *chan)
{
	const char *linkedid = chan->linkedid;
	struct channel_find_data find_dat;

	/* make sure we need to do all this work */

	if (!ast_strlen_zero(linkedid) && ast_cel_track_event(AST_CEL_LINKEDID_END)) {
		struct ast_channel *tmp = NULL;
		find_dat.chan = chan;
		find_dat.linkedid = linkedid;
		if ((tmp = ast_channel_callback(linkedid_match, NULL, &find_dat, 0))) {
			tmp = ast_channel_unref(tmp);
		} else {
			ast_cel_report_event(chan, AST_CEL_LINKEDID_END, NULL, NULL, NULL);
		}
	}
}

struct ast_channel *ast_cel_fabricate_channel_from_event(const struct ast_event *event)
{
	struct varshead *headp;
	struct ast_var_t *newvariable;
	char timebuf[30];
	struct ast_channel *tchan;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	/* do not call ast_channel_alloc because this is not really a real channel */
	if (!(tchan = ast_dummy_channel_alloc())) {
		return NULL;
	}

	headp = &tchan->varshead;

	/* first, get the variables from the event */
	if (ast_cel_fill_record(event, &record)) {
		ast_channel_release(tchan);
		return NULL;
	}

	/* next, fill the channel with their data */
	if ((newvariable = ast_var_assign("eventtype", record.event_name))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if (ast_strlen_zero(cel_dateformat)) {
		snprintf(timebuf, sizeof(timebuf), "%ld.%06ld", record.event_time.tv_sec, record.event_time.tv_usec);
	} else {
		struct ast_tm tm;
		ast_localtime(&record.event_time, &tm, NULL);
		ast_strftime(timebuf, sizeof(timebuf), cel_dateformat, &tm);
	}

	if ((newvariable = ast_var_assign("eventtime", timebuf))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if ((newvariable = ast_var_assign("eventextra", record.extra))) {
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	tchan->cid.cid_name = ast_strdup(record.caller_id_name);
	tchan->cid.cid_num = ast_strdup(record.caller_id_num);
	tchan->cid.cid_ani = ast_strdup(record.caller_id_ani);
	tchan->cid.cid_rdnis = ast_strdup(record.caller_id_rdnis);
	tchan->cid.cid_dnid = ast_strdup(record.caller_id_dnid);

	ast_copy_string(tchan->exten, record.extension, sizeof(tchan->exten));
	ast_copy_string(tchan->context, record.context, sizeof(tchan->context));
	ast_string_field_set(tchan, name, record.channel_name);
	ast_string_field_set(tchan, uniqueid, record.unique_id);
	ast_string_field_set(tchan, linkedid, record.linked_id);
	ast_string_field_set(tchan, accountcode, record.account_code);
	ast_string_field_set(tchan, peeraccount, record.peer_account);
	ast_string_field_set(tchan, userfield, record.user_field);

	pbx_builtin_setvar_helper(tchan, "BRIDGEPEER", record.peer);

	tchan->appl = ast_strdup(record.application_name);
	tchan->data = ast_strdup(record.application_data);
	tchan->amaflags = record.amaflag;

	return tchan;
}

int ast_cel_report_event(struct ast_channel *chan, enum ast_cel_event_type event_type,
		const char *userdefevname, const char *extra, struct ast_channel *peer2)
{
	struct timeval eventtime;
	struct ast_event *ev;
	const char *peername = "";
	struct ast_channel *peer;

	ast_channel_lock(chan);
	peer = ast_bridged_channel(chan);
	if (peer) {
		ast_channel_ref(peer);
	}
	ast_channel_unlock(chan);

	/* Make sure a reload is not occurring while we're checking to see if this
	 * is an event that we care about.  We could lose an important event in this
	 * process otherwise. */
	ast_mutex_lock(&reload_lock);

	if (!cel_enabled || !ast_cel_track_event(event_type)) {
		ast_mutex_unlock(&reload_lock);
		return 0;
	}

	if (event_type == AST_CEL_APP_START || event_type == AST_CEL_APP_END) {
		char *app;
		if (!(app = ao2_find(appset, (char *) chan->appl, OBJ_POINTER))) {
			ast_mutex_unlock(&reload_lock);
			return 0;
		}
		ao2_ref(app, -1);
	}

	ast_mutex_unlock(&reload_lock);

	if (peer) {
		ast_channel_lock(peer);
		peername = ast_strdupa(peer->name);
		ast_channel_unlock(peer);
	} else if (peer2) {
		ast_channel_lock(peer2);
		peername = ast_strdupa(peer2->name);
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
			AST_EVENT_IE_CEL_CIDNAME, AST_EVENT_IE_PLTYPE_STR, S_OR(chan->cid.cid_name, ""),
			AST_EVENT_IE_CEL_CIDNUM, AST_EVENT_IE_PLTYPE_STR, S_OR(chan->cid.cid_num, ""),
			AST_EVENT_IE_CEL_CIDANI, AST_EVENT_IE_PLTYPE_STR, S_OR(chan->cid.cid_ani, ""),
			AST_EVENT_IE_CEL_CIDRDNIS, AST_EVENT_IE_PLTYPE_STR, S_OR(chan->cid.cid_rdnis, ""),
			AST_EVENT_IE_CEL_CIDDNID, AST_EVENT_IE_PLTYPE_STR, S_OR(chan->cid.cid_dnid, ""),
			AST_EVENT_IE_CEL_EXTEN, AST_EVENT_IE_PLTYPE_STR, chan->exten,
			AST_EVENT_IE_CEL_CONTEXT, AST_EVENT_IE_PLTYPE_STR, chan->context,
			AST_EVENT_IE_CEL_CHANNAME, AST_EVENT_IE_PLTYPE_STR, chan->name,
			AST_EVENT_IE_CEL_APPNAME, AST_EVENT_IE_PLTYPE_STR, S_OR(chan->appl, ""),
			AST_EVENT_IE_CEL_APPDATA, AST_EVENT_IE_PLTYPE_STR, S_OR(chan->data, ""),
			AST_EVENT_IE_CEL_AMAFLAGS, AST_EVENT_IE_PLTYPE_UINT, chan->amaflags,
			AST_EVENT_IE_CEL_ACCTCODE, AST_EVENT_IE_PLTYPE_STR, chan->accountcode,
			AST_EVENT_IE_CEL_PEERACCT, AST_EVENT_IE_PLTYPE_STR, chan->peeraccount,
			AST_EVENT_IE_CEL_UNIQUEID, AST_EVENT_IE_PLTYPE_STR, chan->uniqueid,
			AST_EVENT_IE_CEL_LINKEDID, AST_EVENT_IE_PLTYPE_STR, chan->linkedid,
			AST_EVENT_IE_CEL_USERFIELD, AST_EVENT_IE_PLTYPE_STR, chan->userfield,
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

	r->user_defined_name = "";

	if (r->event_type == AST_CEL_USER_DEFINED) {
		r->user_defined_name = ast_event_get_ie_str(e, AST_EVENT_IE_CEL_USEREVENT_NAME);
		r->event_name = r->user_defined_name;
	} else {
		r->event_name = ast_cel_get_type_name(r->event_type);
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
	const char *app1 = obj, *app2 = arg;

	return !strcasecmp(app1, app2) ? CMP_MATCH | CMP_STOP : 0;
}

static void ast_cel_engine_term(void)
{
	if (appset) {
		ao2_ref(appset, -1);
		appset = NULL;
	}
}

int ast_cel_engine_init(void)
{
	if (!(appset = ao2_container_alloc(NUM_APP_BUCKETS, app_hash, app_cmp))) {
		return -1;
	}

	if (do_reload()) {
		ao2_ref(appset, -1);
		appset = NULL;
		return -1;
	}

	if (ast_cli_register(&cli_status)) {
		ao2_ref(appset, -1);
		appset = NULL;
		return -1;
	}

	ast_register_atexit(ast_cel_engine_term);

	return 0;
}

int ast_cel_engine_reload(void)
{
	return do_reload();
}

