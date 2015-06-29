/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 * Copyright (C) 2012, Russell Bryant
 *
 * Russell Bryant <russell@russellbryant.net>
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
 * \author Russell Bryant <russell@russellbryant.net>
 *
 * This module is based on and replaces the previous res_ais module.
 */

/*** MODULEINFO
	<depend>corosync</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE();

#include <corosync/cpg.h>
#include <corosync/cfg.h>

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/poll-compat.h"
#include "asterisk/config.h"
#include "asterisk/event.h"
#include "asterisk/cli.h"
#include "asterisk/devicestate.h"
#include "asterisk/app.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"

AST_RWLOCK_DEFINE_STATIC(event_types_lock);

static void publish_mwi_to_stasis(struct ast_event *event);
static void publish_device_state_to_stasis(struct ast_event *event);

/*! \brief The internal topic used for message forwarding and pings */
static struct stasis_topic *corosync_aggregate_topic;

/*! \brief Our \ref stasis message router */
static struct stasis_message_router *stasis_router;

/*! \brief Internal accessor for our topic */
static struct stasis_topic *corosync_topic(void)
{
	return corosync_aggregate_topic;
}

/*! \brief A payload wrapper around a corosync ping event */
struct corosync_ping_payload {
	/*! The corosync ping event being passed over \ref stasis */
	struct ast_event *event;
};

/*! \brief Destructor for the \ref corosync_ping_payload wrapper object */
static void corosync_ping_payload_dtor(void *obj)
{
	struct corosync_ping_payload *payload = obj;

	ast_free(payload->event);
}

/*! \brief Convert a Corosync PING to a \ref ast_event */
static struct ast_event *corosync_ping_to_event(struct stasis_message *message)
{
	struct corosync_ping_payload *payload;
	struct ast_event *event;
	struct ast_eid *event_eid;

	if (!message) {
		return NULL;
	}

	payload = stasis_message_data(message);

	if (!payload->event) {
		return NULL;
	}

	event_eid = (struct ast_eid *)ast_event_get_ie_raw(payload->event, AST_EVENT_IE_EID);

	event = ast_event_new(AST_EVENT_PING,
				AST_EVENT_IE_EID, AST_EVENT_IE_PLTYPE_RAW, event_eid, sizeof(*event_eid),
				AST_EVENT_IE_END);

	return event;
}

STASIS_MESSAGE_TYPE_DEFN_LOCAL(corosync_ping_message_type,
	.to_event = corosync_ping_to_event, );

/*! \brief Publish a Corosync ping to \ref stasis */
static void publish_corosync_ping_to_stasis(struct ast_event *event)
{
	struct corosync_ping_payload *payload;
	struct stasis_message *message;

	ast_assert(ast_event_get_type(event) == AST_EVENT_PING);
	ast_assert(event != NULL);

	if (!corosync_ping_message_type()) {
		return;
	}

	payload = ao2_t_alloc(sizeof(*payload), corosync_ping_payload_dtor, "Create ping payload");
	if (!payload) {
		return;
	}
	payload->event = event;

	message = stasis_message_create(corosync_ping_message_type(), payload);
	if (!message) {
		ao2_t_ref(payload, -1, "Destroy payload on off nominal");
		return;
	}

	stasis_publish(corosync_topic(), message);

	ao2_t_ref(payload, -1, "Hand ref to stasis");
	ao2_t_ref(message, -1, "Hand ref to stasis");
}

static struct {
	const char *name;
	struct stasis_forward *sub;
	unsigned char publish;
	unsigned char publish_default;
	unsigned char subscribe;
	unsigned char subscribe_default;
	struct stasis_topic *(* topic_fn)(void);
	struct stasis_cache *(* cache_fn)(void);
	struct stasis_message_type *(* message_type_fn)(void);
	void (* publish_to_stasis)(struct ast_event *);
} event_types[] = {
	[AST_EVENT_MWI] = { .name = "mwi",
	                    .topic_fn = ast_mwi_topic_all,
	                    .cache_fn = ast_mwi_state_cache,
	                    .message_type_fn = ast_mwi_state_type,
	                    .publish_to_stasis = publish_mwi_to_stasis, },
	[AST_EVENT_DEVICE_STATE_CHANGE] = { .name = "device_state",
	                                    .topic_fn = ast_device_state_topic_all,
	                                    .cache_fn = ast_device_state_cache,
	                                    .message_type_fn = ast_device_state_message_type,
	                                    .publish_to_stasis = publish_device_state_to_stasis, },
	[AST_EVENT_PING] = { .name = "ping",
	                     .publish_default = 1,
	                     .subscribe_default = 1,
	                     .topic_fn = corosync_topic,
	                     .message_type_fn = corosync_ping_message_type,
	                     .publish_to_stasis = publish_corosync_ping_to_stasis, },
};

static struct {
	pthread_t id;
	int alert_pipe[2];
	unsigned int stop:1;
} dispatch_thread = {
	.id = AST_PTHREADT_NULL,
	.alert_pipe = { -1, -1 },
};

static cpg_handle_t cpg_handle;
static corosync_cfg_handle_t cfg_handle;

#ifdef HAVE_COROSYNC_CFG_STATE_TRACK
static void cfg_state_track_cb(
		corosync_cfg_state_notification_buffer_t *notification_buffer,
		cs_error_t error);
#endif /* HAVE_COROSYNC_CFG_STATE_TRACK */

static void cfg_shutdown_cb(corosync_cfg_handle_t cfg_handle,
		corosync_cfg_shutdown_flags_t flags);

static corosync_cfg_callbacks_t cfg_callbacks = {
#ifdef HAVE_COROSYNC_CFG_STATE_TRACK
	.corosync_cfg_state_track_callback = cfg_state_track_cb,
#endif /* HAVE_COROSYNC_CFG_STATE_TRACK */
	.corosync_cfg_shutdown_callback = cfg_shutdown_cb,
};

/*! \brief Publish a received MWI \ref ast_event to \ref stasis */
static void publish_mwi_to_stasis(struct ast_event *event)
{
	const char *mailbox;
	const char *context;
	unsigned int new_msgs;
	unsigned int old_msgs;
	struct ast_eid *event_eid;

	ast_assert(ast_event_get_type(event) == AST_EVENT_MWI);

	mailbox = ast_event_get_ie_str(event, AST_EVENT_IE_MAILBOX);
	context = ast_event_get_ie_str(event, AST_EVENT_IE_CONTEXT);
	new_msgs = ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS);
	old_msgs = ast_event_get_ie_uint(event, AST_EVENT_IE_OLDMSGS);
	event_eid = (struct ast_eid *)ast_event_get_ie_raw(event, AST_EVENT_IE_EID);

	if (ast_strlen_zero(mailbox) || ast_strlen_zero(context)) {
		return;
	}

	if (new_msgs > INT_MAX) {
		new_msgs = INT_MAX;
	}

	if (old_msgs > INT_MAX) {
		old_msgs = INT_MAX;
	}

	if (ast_publish_mwi_state_full(mailbox, context, (int)new_msgs,
	                               (int)old_msgs, NULL, event_eid)) {
		char eid[16];
		ast_eid_to_str(eid, sizeof(eid), event_eid);
		ast_log(LOG_WARNING, "Failed to publish MWI message for %s@%s from %s\n",
			mailbox, context, eid);
	}
}

/*! \brief Publish a received device state \ref ast_event to \ref stasis */
static void publish_device_state_to_stasis(struct ast_event *event)
{
	const char *device;
	enum ast_device_state state;
	unsigned int cachable;
	struct ast_eid *event_eid;

	ast_assert(ast_event_get_type(event) == AST_EVENT_DEVICE_STATE_CHANGE);

	device = ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE);
	state = ast_event_get_ie_uint(event, AST_EVENT_IE_STATE);
	cachable = ast_event_get_ie_uint(event, AST_EVENT_IE_CACHABLE);
	event_eid = (struct ast_eid *)ast_event_get_ie_raw(event, AST_EVENT_IE_EID);

	if (ast_strlen_zero(device)) {
		return;
	}

	if (ast_publish_device_state_full(device, state, cachable, event_eid)) {
		char eid[16];
		ast_eid_to_str(eid, sizeof(eid), event_eid);
		ast_log(LOG_WARNING, "Failed to publish device state message for %s from %s\n",
			device, eid);
	}
}

static void cpg_deliver_cb(cpg_handle_t handle, const struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *msg, size_t msg_len);

static void cpg_confchg_cb(cpg_handle_t handle, const struct cpg_name *group_name,
		const struct cpg_address *member_list, size_t member_list_entries,
		const struct cpg_address *left_list, size_t left_list_entries,
		const struct cpg_address *joined_list, size_t joined_list_entries);

static cpg_callbacks_t cpg_callbacks = {
	.cpg_deliver_fn = cpg_deliver_cb,
	.cpg_confchg_fn = cpg_confchg_cb,
};

#ifdef HAVE_COROSYNC_CFG_STATE_TRACK
static void cfg_state_track_cb(
		corosync_cfg_state_notification_buffer_t *notification_buffer,
		cs_error_t error)
{
}
#endif /* HAVE_COROSYNC_CFG_STATE_TRACK */

static void cfg_shutdown_cb(corosync_cfg_handle_t cfg_handle,
		corosync_cfg_shutdown_flags_t flags)
{
}

static void cpg_deliver_cb(cpg_handle_t handle, const struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *msg, size_t msg_len)
{
	struct ast_event *event;
	void (*publish_handler)(struct ast_event *) = NULL;
	enum ast_event_type event_type;

	if (msg_len < ast_event_minimum_length()) {
		ast_debug(1, "Ignoring event that's too small. %u < %u\n",
			(unsigned int) msg_len,
			(unsigned int) ast_event_minimum_length());
		return;
	}

	if (!ast_eid_cmp(&ast_eid_default, ast_event_get_ie_raw(msg, AST_EVENT_IE_EID))) {
		/* Don't feed events back in that originated locally. */
		return;
	}

	event_type = ast_event_get_type(msg);
	if (event_type > AST_EVENT_TOTAL) {
		/* Egads, we don't support this */
		return;
	}

	ast_rwlock_rdlock(&event_types_lock);
	publish_handler = event_types[event_type].publish_to_stasis;
	if (!event_types[event_type].subscribe || !publish_handler) {
		/* We are not configured to subscribe to these events or
		   we have no way to publish it internally. */
		ast_rwlock_unlock(&event_types_lock);
		return;
	}
	ast_rwlock_unlock(&event_types_lock);

	if (!(event = ast_malloc(msg_len))) {
		return;
	}

	memcpy(event, msg, msg_len);

	if (event_type == AST_EVENT_PING) {
		const struct ast_eid *eid;
		char buf[128] = "";

		eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
		ast_eid_to_str(buf, sizeof(buf), (struct ast_eid *) eid);
		ast_log(LOG_NOTICE, "Got event PING from server with EID: '%s'\n", buf);
	}
	ast_debug(5, "Publishing event %s (%u) to stasis\n",
		ast_event_get_type_name(event), event_type);
	publish_handler(event);
}

static void publish_to_corosync(struct stasis_message *message)
{
	cs_error_t cs_err;
	struct iovec iov;
	struct ast_event *event;

	event = stasis_message_to_event(message);
	if (!event) {
		return;
	}

	if (ast_eid_cmp(&ast_eid_default, ast_event_get_ie_raw(event, AST_EVENT_IE_EID))) {
		/* If the event didn't originate from this server, don't send it back out. */
		ast_event_destroy(event);
		return;
	}

	if (ast_event_get_type(event) == AST_EVENT_PING) {
		const struct ast_eid *eid;
		char buf[128] = "";

		eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
		ast_eid_to_str(buf, sizeof(buf), (struct ast_eid *) eid);
		ast_log(LOG_NOTICE, "Sending event PING from this server with EID: '%s'\n", buf);
	}

	iov.iov_base = (void *)event;
	iov.iov_len = ast_event_get_size(event);

	ast_debug(5, "Publishing event %s (%u) to corosync\n",
		ast_event_get_type_name(event), ast_event_get_type(event));

	/* The stasis subscription will only exist if we are configured to publish
	 * these events, so just send away. */
	if ((cs_err = cpg_mcast_joined(cpg_handle, CPG_TYPE_FIFO, &iov, 1)) != CS_OK) {
		ast_log(LOG_WARNING, "CPG mcast failed (%u)\n", cs_err);
	}
}

static void stasis_message_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	if (!message) {
		return;
	}

	publish_to_corosync(message);
}

static int dump_cache_cb(void *obj, void *arg, int flags)
{
	struct stasis_message *message = obj;

	if (!message) {
		return 0;
	}

	publish_to_corosync(message);

	return 0;
}

static void cpg_confchg_cb(cpg_handle_t handle, const struct cpg_name *group_name,
		const struct cpg_address *member_list, size_t member_list_entries,
		const struct cpg_address *left_list, size_t left_list_entries,
		const struct cpg_address *joined_list, size_t joined_list_entries)
{
	unsigned int i;

	/* If any new nodes have joined, dump our cache of events we are publishing
	 * that originated from this server. */

	if (!joined_list_entries) {
		return;
	}

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		struct ao2_container *messages;

		ast_rwlock_rdlock(&event_types_lock);
		if (!event_types[i].publish) {
			ast_rwlock_unlock(&event_types_lock);
			continue;
		}

		if (!event_types[i].cache_fn || !event_types[i].message_type_fn) {
			ast_rwlock_unlock(&event_types_lock);
			continue;
		}

		messages = stasis_cache_dump_by_eid(event_types[i].cache_fn(),
			event_types[i].message_type_fn(),
			&ast_eid_default);
		ast_rwlock_unlock(&event_types_lock);

		ao2_callback(messages, OBJ_NODATA, dump_cache_cb, NULL);

		ao2_t_ref(messages, -1, "Dispose of dumped cache");
	}
}

static void *dispatch_thread_handler(void *data)
{
	cs_error_t cs_err;
	struct pollfd pfd[3] = {
		{ .events = POLLIN, },
		{ .events = POLLIN, },
		{ .events = POLLIN, },
	};

	if ((cs_err = cpg_fd_get(cpg_handle, &pfd[0].fd)) != CS_OK) {
		ast_log(LOG_ERROR, "Failed to get CPG fd.  This module is now broken.\n");
		return NULL;
	}

	if ((cs_err = corosync_cfg_fd_get(cfg_handle, &pfd[1].fd)) != CS_OK) {
		ast_log(LOG_ERROR, "Failed to get CFG fd.  This module is now broken.\n");
		return NULL;
	}

	pfd[2].fd = dispatch_thread.alert_pipe[0];

	while (!dispatch_thread.stop) {
		int res;

		cs_err = CS_OK;

		pfd[0].revents = 0;
		pfd[1].revents = 0;
		pfd[2].revents = 0;

		res = ast_poll(pfd, ARRAY_LEN(pfd), -1);
		if (res == -1 && errno != EINTR && errno != EAGAIN) {
			ast_log(LOG_ERROR, "poll() error: %s (%d)\n", strerror(errno), errno);
			continue;
		}

		if (pfd[0].revents & POLLIN) {
			if ((cs_err = cpg_dispatch(cpg_handle, CS_DISPATCH_ALL)) != CS_OK) {
				ast_log(LOG_WARNING, "Failed CPG dispatch: %u\n", cs_err);
			}
		}

		if (pfd[1].revents & POLLIN) {
			if ((cs_err = corosync_cfg_dispatch(cfg_handle, CS_DISPATCH_ALL)) != CS_OK) {
				ast_log(LOG_WARNING, "Failed CFG dispatch: %u\n", cs_err);
			}
		}

		if (cs_err == CS_ERR_LIBRARY || cs_err == CS_ERR_BAD_HANDLE) {
			struct cpg_name name;

			/* If corosync gets restarted out from under Asterisk, try to recover. */

			ast_log(LOG_NOTICE, "Attempting to recover from corosync failure.\n");

			if ((cs_err = corosync_cfg_initialize(&cfg_handle, &cfg_callbacks)) != CS_OK) {
				ast_log(LOG_ERROR, "Failed to initialize cfg (%d)\n", (int) cs_err);
				sleep(5);
				continue;
			}

			if ((cs_err = cpg_initialize(&cpg_handle, &cpg_callbacks) != CS_OK)) {
				ast_log(LOG_ERROR, "Failed to initialize cpg (%d)\n", (int) cs_err);
				sleep(5);
				continue;
			}

			if ((cs_err = cpg_fd_get(cpg_handle, &pfd[0].fd)) != CS_OK) {
				ast_log(LOG_ERROR, "Failed to get CPG fd.\n");
				sleep(5);
				continue;
			}

			if ((cs_err = corosync_cfg_fd_get(cfg_handle, &pfd[1].fd)) != CS_OK) {
				ast_log(LOG_ERROR, "Failed to get CFG fd.\n");
				sleep(5);
				continue;
			}

			ast_copy_string(name.value, "asterisk", sizeof(name.value));
			name.length = strlen(name.value);
			if ((cs_err = cpg_join(cpg_handle, &name)) != CS_OK) {
				ast_log(LOG_ERROR, "Failed to join cpg (%d)\n", (int) cs_err);
				sleep(5);
				continue;
			}

			ast_log(LOG_NOTICE, "Corosync recovery complete.\n");
		}
	}

	return NULL;
}

static char *corosync_show_members(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	cs_error_t cs_err;
	cpg_iteration_handle_t cpg_iter;
	struct cpg_iteration_description_t cpg_desc;
	unsigned int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "corosync show members";
		e->usage =
			"Usage: corosync show members\n"
			"       Show corosync cluster members\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	cs_err = cpg_iteration_initialize(cpg_handle, CPG_ITERATION_ALL, NULL, &cpg_iter);

	if (cs_err != CS_OK) {
		ast_cli(a->fd, "Failed to initialize CPG iterator.\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Cluster members =========================================\n"
	            "=============================================================\n"
	            "===\n");

	for (i = 1, cs_err = cpg_iteration_next(cpg_iter, &cpg_desc);
			cs_err == CS_OK;
			cs_err = cpg_iteration_next(cpg_iter, &cpg_desc), i++) {
		corosync_cfg_node_address_t addrs[8];
		int num_addrs = 0;
		unsigned int j;

		cs_err = corosync_cfg_get_node_addrs(cfg_handle, cpg_desc.nodeid,
				ARRAY_LEN(addrs), &num_addrs, addrs);
		if (cs_err != CS_OK) {
			ast_log(LOG_WARNING, "Failed to get node addresses\n");
			continue;
		}

		ast_cli(a->fd, "=== Node %u\n", i);
		ast_cli(a->fd, "=== --> Group: %s\n", cpg_desc.group.value);

		for (j = 0; j < num_addrs; j++) {
			struct sockaddr *sa = (struct sockaddr *) addrs[j].address;
			size_t sa_len = (size_t) addrs[j].address_length;
			char buf[128];

			getnameinfo(sa, sa_len, buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);

			ast_cli(a->fd, "=== --> Address %u: %s\n", j + 1, buf);
		}

	}

	ast_cli(a->fd, "===\n"
	               "=============================================================\n"
	               "\n");

	cpg_iteration_finalize(cpg_iter);

	return CLI_SUCCESS;
}

static char *corosync_ping(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_event *event;

	switch (cmd) {
	case CLI_INIT:
		e->command = "corosync ping";
		e->usage =
			"Usage: corosync ping\n"
			"       Send a test ping to the cluster.\n"
			"A NOTICE will be in the log for every ping received\n"
			"on a server.\n  If you send a ping, you should see a NOTICE\n"
			"in the log for every server in the cluster.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	event = ast_event_new(AST_EVENT_PING, AST_EVENT_IE_END);

	if (!event) {
		return CLI_FAILURE;
	}

	ast_rwlock_rdlock(&event_types_lock);
	event_types[AST_EVENT_PING].publish_to_stasis(event);
	ast_rwlock_unlock(&event_types_lock);

	return CLI_SUCCESS;
}

static char *corosync_show_config(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	unsigned int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "corosync show config";
		e->usage =
			"Usage: corosync show config\n"
			"       Show configuration loaded from res_corosync.conf\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== res_corosync config =====================================\n"
	            "=============================================================\n"
	            "===\n");

	ast_rwlock_rdlock(&event_types_lock);
	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (event_types[i].publish) {
			ast_cli(a->fd, "=== ==> Publishing Event Type: %s\n",
					event_types[i].name);
		}
		if (event_types[i].subscribe) {
			ast_cli(a->fd, "=== ==> Subscribing to Event Type: %s\n",
					event_types[i].name);
		}
	}
	ast_rwlock_unlock(&event_types_lock);

	ast_cli(a->fd, "===\n"
	               "=============================================================\n"
	               "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry corosync_cli[] = {
	AST_CLI_DEFINE(corosync_show_config, "Show configuration"),
	AST_CLI_DEFINE(corosync_show_members, "Show cluster members"),
	AST_CLI_DEFINE(corosync_ping, "Send a test ping to the cluster"),
};

enum {
	PUBLISH,
	SUBSCRIBE,
};

static int set_event(const char *event_type, int pubsub)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (!event_types[i].name || strcasecmp(event_type, event_types[i].name)) {
			continue;
		}

		switch (pubsub) {
		case PUBLISH:
			event_types[i].publish = 1;
			break;
		case SUBSCRIBE:
			event_types[i].subscribe = 1;
			break;
		}

		break;
	}

	return (i == ARRAY_LEN(event_types)) ? -1 : 0;
}

static int load_general_config(struct ast_config *cfg)
{
	struct ast_variable *v;
	int res = 0;
	unsigned int i;

	ast_rwlock_wrlock(&event_types_lock);

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		event_types[i].publish = event_types[i].publish_default;
		event_types[i].subscribe = event_types[i].subscribe_default;
	}

	for (v = ast_variable_browse(cfg, "general"); v && !res; v = v->next) {
		if (!strcasecmp(v->name, "publish_event")) {
			res = set_event(v->value, PUBLISH);
		} else if (!strcasecmp(v->name, "subscribe_event")) {
			res = set_event(v->value, SUBSCRIBE);
		} else {
			ast_log(LOG_WARNING, "Unknown option '%s'\n", v->name);
		}
	}

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (event_types[i].publish && !event_types[i].sub) {
			event_types[i].sub = stasis_forward_all(event_types[i].topic_fn(),
													corosync_topic());
			stasis_message_router_add(stasis_router,
			                          event_types[i].message_type_fn(),
			                          stasis_message_cb,
			                          NULL);
		} else if (!event_types[i].publish && event_types[i].sub) {
			event_types[i].sub = stasis_forward_cancel(event_types[i].sub);
			stasis_message_router_remove(stasis_router,
			                             event_types[i].message_type_fn());
		}
	}

	ast_rwlock_unlock(&event_types_lock);

	return res;
}

static int load_config(unsigned int reload)
{
	static const char filename[] = "res_corosync.conf";
	struct ast_config *cfg;
	const char *cat = NULL;
	struct ast_flags config_flags = { 0 };
	int res = 0;

	cfg = ast_config_load(filename, config_flags);

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return -1;
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			res = load_general_config(cfg);
		} else {
			ast_log(LOG_WARNING, "Unknown configuration section '%s'\n", cat);
		}
	}

	ast_config_destroy(cfg);

	return res;
}

static void cleanup_module(void)
{
	cs_error_t cs_err;
	unsigned int i;

	if (stasis_router) {

		/* Unsubscribe all topic forwards and cancel all message routes */
		ast_rwlock_wrlock(&event_types_lock);
		for (i = 0; i < ARRAY_LEN(event_types); i++) {
			if (event_types[i].sub) {
				event_types[i].sub = stasis_forward_cancel(event_types[i].sub);
				stasis_message_router_remove(stasis_router,
				                             event_types[i].message_type_fn());
			}
			event_types[i].publish = 0;
			event_types[i].subscribe = 0;
		}
		ast_rwlock_unlock(&event_types_lock);

		stasis_message_router_unsubscribe_and_join(stasis_router);
		stasis_router = NULL;
	}

	if (corosync_aggregate_topic) {
		ao2_t_ref(corosync_aggregate_topic, -1, "Dispose of topic on cleanup");
		corosync_aggregate_topic = NULL;
	}

	STASIS_MESSAGE_TYPE_CLEANUP(corosync_ping_message_type);

	if (dispatch_thread.id != AST_PTHREADT_NULL) {
		char meepmeep = 'x';
		dispatch_thread.stop = 1;
		if (ast_carefulwrite(dispatch_thread.alert_pipe[1], &meepmeep, 1,
					5000) == -1) {
			ast_log(LOG_ERROR, "Failed to write to pipe: %s (%d)\n",
					strerror(errno), errno);
		}
		pthread_join(dispatch_thread.id, NULL);
	}

	if (dispatch_thread.alert_pipe[0] != -1) {
		close(dispatch_thread.alert_pipe[0]);
		dispatch_thread.alert_pipe[0] = -1;
	}

	if (dispatch_thread.alert_pipe[1] != -1) {
		close(dispatch_thread.alert_pipe[1]);
		dispatch_thread.alert_pipe[1] = -1;
	}

	if (cpg_handle && (cs_err = cpg_finalize(cpg_handle)) != CS_OK) {
		ast_log(LOG_ERROR, "Failed to finalize cpg (%d)\n", (int) cs_err);
	}
	cpg_handle = 0;

	if (cfg_handle && (cs_err = corosync_cfg_finalize(cfg_handle)) != CS_OK) {
		ast_log(LOG_ERROR, "Failed to finalize cfg (%d)\n", (int) cs_err);
	}
	cfg_handle = 0;
}

static int load_module(void)
{
	cs_error_t cs_err;
	struct cpg_name name;

	corosync_aggregate_topic = stasis_topic_create("corosync_aggregate_topic");
	if (!corosync_aggregate_topic) {
		ast_log(AST_LOG_ERROR, "Failed to create stasis topic for corosync\n");
		goto failed;
	}

	stasis_router = stasis_message_router_create(corosync_aggregate_topic);
	if (!stasis_router) {
		ast_log(AST_LOG_ERROR, "Failed to create message router for corosync topic\n");
		goto failed;
	}

	if (STASIS_MESSAGE_TYPE_INIT(corosync_ping_message_type) != 0) {
		ast_log(AST_LOG_ERROR, "Failed to initialize corosync ping message type\n");
		goto failed;
	}

	if (load_config(0)) {
		/* simply not configured is not a fatal error */
		goto failed;
	}

	if ((cs_err = corosync_cfg_initialize(&cfg_handle, &cfg_callbacks)) != CS_OK) {
		ast_log(LOG_ERROR, "Failed to initialize cfg: (%d)\n", (int) cs_err);
		goto failed;
	}

	if ((cs_err = cpg_initialize(&cpg_handle, &cpg_callbacks)) != CS_OK) {
		ast_log(LOG_ERROR, "Failed to initialize cpg: (%d)\n", (int) cs_err);
		goto failed;
	}

	ast_copy_string(name.value, "asterisk", sizeof(name.value));
	name.length = strlen(name.value);

	if ((cs_err = cpg_join(cpg_handle, &name)) != CS_OK) {
		ast_log(LOG_ERROR, "Failed to join: (%d)\n", (int) cs_err);
		goto failed;
	}

	if (pipe(dispatch_thread.alert_pipe) == -1) {
		ast_log(LOG_ERROR, "Failed to create alert pipe: %s (%d)\n",
				strerror(errno), errno);
		goto failed;
	}

	if (ast_pthread_create_background(&dispatch_thread.id, NULL,
			dispatch_thread_handler, NULL)) {
		ast_log(LOG_ERROR, "Error starting CPG dispatch thread.\n");
		goto failed;
	}

	ast_cli_register_multiple(corosync_cli, ARRAY_LEN(corosync_cli));

	return AST_MODULE_LOAD_SUCCESS;

failed:
	cleanup_module();

	return AST_MODULE_LOAD_DECLINE;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(corosync_cli, ARRAY_LEN(corosync_cli));

	cleanup_module();

	return 0;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Corosync");

