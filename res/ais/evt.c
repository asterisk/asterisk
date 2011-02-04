/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Usage of the SAForum AIS (Application Interface Specification)
 *
 * \arg http://www.openais.org/
 *
 * This file contains the code specific to the use of the EVT
 * (Event) Service.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "ais.h"

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/event.h"
#include "asterisk/config.h"
#include "asterisk/linkedlists.h"
#include "asterisk/devicestate.h"

#ifndef AST_MODULE
/* XXX HACK */
#define AST_MODULE "res_ais"
#endif

SaEvtHandleT evt_handle;
static SaAisErrorT evt_init_res;

void evt_channel_open_cb(SaInvocationT invocation, SaEvtChannelHandleT channel_handle,
	SaAisErrorT error);
void evt_event_deliver_cb(SaEvtSubscriptionIdT subscription_id,
	const SaEvtEventHandleT event_handle, const SaSizeT event_datalen);

static const SaEvtCallbacksT evt_callbacks = {
	.saEvtChannelOpenCallback  = evt_channel_open_cb,
	.saEvtEventDeliverCallback = evt_event_deliver_cb,
};

static const struct {
	const char *str;
	enum ast_event_type type;
} supported_event_types[] = {
	{ "mwi", AST_EVENT_MWI },
	{ "device_state", AST_EVENT_DEVICE_STATE_CHANGE },
};

/*! Used to provide unique id's to egress subscriptions */
static int unique_id;

struct subscribe_event {
	AST_LIST_ENTRY(subscribe_event) entry;
	/*! This is a unique identifier to identify this subscription in the event
	 *  channel through the different API calls, subscribe, unsubscribe, and
	 *  the event deliver callback. */
	SaEvtSubscriptionIdT id;
	enum ast_event_type type;
};

struct publish_event {
	AST_LIST_ENTRY(publish_event) entry;
	/*! We subscribe to events internally so that we can publish them
	 *  on this event channel. */
	struct ast_event_sub *sub;
	enum ast_event_type type;
};

struct event_channel {
	AST_RWLIST_ENTRY(event_channel) entry;
	AST_LIST_HEAD_NOLOCK(, subscribe_event) subscribe_events;
	AST_LIST_HEAD_NOLOCK(, publish_event) publish_events;
	SaEvtChannelHandleT handle;
	char name[1];
};

static AST_RWLIST_HEAD_STATIC(event_channels, event_channel);

void evt_channel_open_cb(SaInvocationT invocation, SaEvtChannelHandleT channel_handle,
	SaAisErrorT error)
{

}

static void queue_event(struct ast_event *ast_event)
{
	ast_event_queue_and_cache(ast_event);
}

void evt_event_deliver_cb(SaEvtSubscriptionIdT sub_id,
	const SaEvtEventHandleT event_handle, const SaSizeT event_datalen)
{
	/* It is important to note that this works because we *know* that this
	 * function will only be called by a single thread, the dispatch_thread.
	 * If this module gets changed such that this is no longer the case, this
	 * should get changed to a thread-local buffer, instead. */
	static unsigned char buf[4096];
	struct ast_event *event_dup, *event = (void *) buf;
	SaAisErrorT ais_res;
	SaSizeT len = sizeof(buf);

	if (event_datalen > len) {
		ast_log(LOG_ERROR, "Event received with size %u, which is too big\n"
			"for the allocated size %u. Change the code to increase the size.\n",
			(unsigned int) event_datalen, (unsigned int) len);
		return;
	}

	ais_res = saEvtEventDataGet(event_handle, event, &len);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error retrieving event payload: %s\n",
			ais_err2str(ais_res));
		return;
	}

	if (!ast_eid_cmp(&ast_eid_default, ast_event_get_ie_raw(event, AST_EVENT_IE_EID))) {
		/* Don't feed events back in that originated locally. */
		return;
	}

	if (!(event_dup = ast_malloc(len)))
		return;

	memcpy(event_dup, event, len);

	queue_event(event_dup);
}

static const char *type_to_filter_str(enum ast_event_type type)
{
	const char *filter_str = NULL;
	int i;

	for (i = 0; i < ARRAY_LEN(supported_event_types); i++) {
		if (supported_event_types[i].type == type) {
			filter_str = supported_event_types[i].str;
			break;
		}
	}

	return filter_str;
}

static void ast_event_cb(const struct ast_event *ast_event, void *data)
{
	SaEvtEventHandleT event_handle;
	SaAisErrorT ais_res;
	struct event_channel *event_channel = data;
	SaClmClusterNodeT local_node;
	SaEvtEventPatternArrayT pattern_array;
	SaEvtEventPatternT pattern;
	SaSizeT len;
	const char *filter_str;
	SaEvtEventIdT event_id;

	ast_debug(1, "Got an event to forward\n");

	if (ast_eid_cmp(&ast_eid_default, ast_event_get_ie_raw(ast_event, AST_EVENT_IE_EID))) {
		/* If the event didn't originate from this server, don't send it back out. */
		ast_debug(1, "Returning here\n");
		return;
	}

	ais_res = saEvtEventAllocate(event_channel->handle, &event_handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error allocating event: %s\n", ais_err2str(ais_res));
		ast_debug(1, "Returning here\n");
		return;
	}

	ais_res = saClmClusterNodeGet(clm_handle, SA_CLM_LOCAL_NODE_ID,
		SA_TIME_ONE_SECOND, &local_node);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error getting local node name: %s\n", ais_err2str(ais_res));
		goto return_event_free;
	}

	filter_str = type_to_filter_str(ast_event_get_type(ast_event));
	len = strlen(filter_str) + 1;
	pattern.pattern = (SaUint8T *) filter_str;
	pattern.patternSize = len;
	pattern.allocatedSize = len;

	pattern_array.allocatedNumber = 1;
	pattern_array.patternsNumber = 1;
	pattern_array.patterns = &pattern;

	/*!
	 * /todo Make retention time configurable
	 * /todo Make event priorities configurable
	 */
	ais_res = saEvtEventAttributesSet(event_handle, &pattern_array,
		SA_EVT_LOWEST_PRIORITY, SA_TIME_ONE_MINUTE, &local_node.nodeName);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error setting event attributes: %s\n", ais_err2str(ais_res));
		goto return_event_free;
	}

	ais_res = saEvtEventPublish(event_handle,
		ast_event, ast_event_get_size(ast_event), &event_id);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error publishing event: %s\n", ais_err2str(ais_res));
		goto return_event_free;
	}

return_event_free:
	ais_res = saEvtEventFree(event_handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error freeing allocated event: %s\n", ais_err2str(ais_res));
	}
	ast_debug(1, "Returning here (event_free)\n");
}

static char *ais_evt_show_event_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct event_channel *event_channel;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ais evt show event channels";
		e->usage =
			"Usage: ais evt show event channels\n"
			"       List configured event channels for the (EVT) Eventing service.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Event Channels ==========================================\n"
	            "=============================================================\n"
	            "===\n");

	AST_RWLIST_RDLOCK(&event_channels);
	AST_RWLIST_TRAVERSE(&event_channels, event_channel, entry) {
		struct publish_event *publish_event;
		struct subscribe_event *subscribe_event;

		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		               "=== Event Channel Name: %s\n", event_channel->name);

		AST_LIST_TRAVERSE(&event_channel->publish_events, publish_event, entry) {
			ast_cli(a->fd, "=== ==> Publishing Event Type: %s\n",
				type_to_filter_str(publish_event->type));
		}

		AST_LIST_TRAVERSE(&event_channel->subscribe_events, subscribe_event, entry) {
			ast_cli(a->fd, "=== ==> Subscribing to Event Type: %s\n",
				type_to_filter_str(subscribe_event->type));
		}

		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		               "===\n");
	}
	AST_RWLIST_UNLOCK(&event_channels);

	ast_cli(a->fd, "=============================================================\n"
	               "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry ais_cli[] = {
	AST_CLI_DEFINE(ais_evt_show_event_channels, "Show configured event channels"),
};

static void add_publish_event(struct event_channel *event_channel, const char *event_type)
{
	int i;
	enum ast_event_type type = -1;
	struct publish_event *publish_event;

	for (i = 0; i < ARRAY_LEN(supported_event_types); i++) {
		if (!strcasecmp(event_type, supported_event_types[i].str)) {
			type = supported_event_types[i].type;
			break;
		}
	}

	if (type == -1) {
		ast_log(LOG_WARNING, "publish_event option given with invalid value '%s'\n", event_type);
		return;
	}

	if (type == AST_EVENT_DEVICE_STATE_CHANGE && ast_enable_distributed_devstate()) {
		return;
	}

	if (!(publish_event = ast_calloc(1, sizeof(*publish_event)))) {
		return;
	}

	publish_event->type = type;
	ast_debug(1, "Subscribing to event type %d\n", type);
	publish_event->sub = ast_event_subscribe(type, ast_event_cb, "AIS", event_channel,
		AST_EVENT_IE_END);
	ast_event_dump_cache(publish_event->sub);

	AST_LIST_INSERT_TAIL(&event_channel->publish_events, publish_event, entry);
}

static SaAisErrorT set_egress_subscription(struct event_channel *event_channel,
	struct subscribe_event *subscribe_event)
{
	SaAisErrorT ais_res;
	SaEvtEventFilterArrayT filter_array;
	SaEvtEventFilterT filter;
	const char *filter_str = NULL;
	SaSizeT len;

	/* We know it's going to be valid.  It was checked earlier. */
	filter_str = type_to_filter_str(subscribe_event->type);

	filter.filterType = SA_EVT_EXACT_FILTER;
	len = strlen(filter_str) + 1;
	filter.filter.allocatedSize = len;
	filter.filter.patternSize = len;
	filter.filter.pattern = (SaUint8T *) filter_str;

	filter_array.filtersNumber = 1;
	filter_array.filters = &filter;

	ais_res = saEvtEventSubscribe(event_channel->handle, &filter_array,
		subscribe_event->id);

	return ais_res;
}

static void add_subscribe_event(struct event_channel *event_channel, const char *event_type)
{
	int i;
	enum ast_event_type type = -1;
	struct subscribe_event *subscribe_event;
	SaAisErrorT ais_res;

	for (i = 0; i < ARRAY_LEN(supported_event_types); i++) {
		if (!strcasecmp(event_type, supported_event_types[i].str)) {
			type = supported_event_types[i].type;
			break;
		}
	}

	if (type == -1) {
		ast_log(LOG_WARNING, "subscribe_event option given with invalid value '%s'\n", event_type);
		return;
	}

	if (type == AST_EVENT_DEVICE_STATE_CHANGE && ast_enable_distributed_devstate()) {
		return;
	}

	if (!(subscribe_event = ast_calloc(1, sizeof(*subscribe_event)))) {
		return;
	}

	subscribe_event->type = type;
	subscribe_event->id = ast_atomic_fetchadd_int(&unique_id, +1);

	ais_res = set_egress_subscription(event_channel, subscribe_event);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error setting up egress subscription: %s\n",
			ais_err2str(ais_res));
		free(subscribe_event);
		return;
	}

	AST_LIST_INSERT_TAIL(&event_channel->subscribe_events, subscribe_event, entry);
}

static void build_event_channel(struct ast_config *cfg, const char *cat)
{
	struct ast_variable *var;
	struct event_channel *event_channel;
	SaAisErrorT ais_res;
	SaNameT sa_name = { 0, };

	AST_RWLIST_WRLOCK(&event_channels);
	AST_RWLIST_TRAVERSE(&event_channels, event_channel, entry) {
		if (!strcasecmp(event_channel->name, cat))
			break;
	}
	AST_RWLIST_UNLOCK(&event_channels);
	if (event_channel) {
		ast_log(LOG_WARNING, "Event channel '%s' was specified twice in "
			"configuration.  Second instance ignored.\n", cat);
		return;
	}

	if (!(event_channel = ast_calloc(1, sizeof(*event_channel) + strlen(cat))))
		return;

	strcpy(event_channel->name, cat);
	ast_copy_string((char *) sa_name.value, cat, sizeof(sa_name.value));
	sa_name.length = strlen((char *) sa_name.value);
	ais_res = saEvtChannelOpen(evt_handle, &sa_name,
		SA_EVT_CHANNEL_PUBLISHER | SA_EVT_CHANNEL_SUBSCRIBER | SA_EVT_CHANNEL_CREATE,
		SA_TIME_MAX, &event_channel->handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error opening event channel: %s\n", ais_err2str(ais_res));
		free(event_channel);
		return;
	}

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "type")) {
			continue;
		} else if (!strcasecmp(var->name, "publish_event")) {
			add_publish_event(event_channel, var->value);
		} else if (!strcasecmp(var->name, "subscribe_event")) {
			add_subscribe_event(event_channel, var->value);
		} else {
			ast_log(LOG_WARNING, "Event channel '%s' contains invalid option '%s'\n",
				event_channel->name, var->name);
		}
	}

	AST_RWLIST_WRLOCK(&event_channels);
	AST_RWLIST_INSERT_TAIL(&event_channels, event_channel, entry);
	AST_RWLIST_UNLOCK(&event_channels);
}

static void load_config(void)
{
	static const char filename[] = "ais.conf";
	struct ast_config *cfg;
	const char *cat = NULL;
	struct ast_flags config_flags = { 0 };

	if (!(cfg = ast_config_load(filename, config_flags)) || cfg == CONFIG_STATUS_FILEINVALID)
		return;

	while ((cat = ast_category_browse(cfg, cat))) {
		const char *type;

		if (!strcasecmp(cat, "general"))
			continue;

		if (!(type = ast_variable_retrieve(cfg, cat, "type"))) {
			ast_log(LOG_WARNING, "Invalid entry in %s defined with no type!\n",
				filename);
			continue;
		}

		if (!strcasecmp(type, "event_channel")) {
			build_event_channel(cfg, cat);
		} else {
			ast_log(LOG_WARNING, "Entry in %s defined with invalid type '%s'\n",
				filename, type);
		}
	}

	ast_config_destroy(cfg);
}

static void publish_event_destroy(struct publish_event *publish_event)
{
	ast_event_unsubscribe(publish_event->sub);

	free(publish_event);
}

static void subscribe_event_destroy(const struct event_channel *event_channel,
	struct subscribe_event *subscribe_event)
{
	SaAisErrorT ais_res;

	/* saEvtChannelClose() will actually do this automatically, but it just
	 * feels cleaner to go ahead and do it manually ... */
	ais_res = saEvtEventUnsubscribe(event_channel->handle, subscribe_event->id);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error unsubscribing: %s\n", ais_err2str(ais_res));
	}

	free(subscribe_event);
}

static void event_channel_destroy(struct event_channel *event_channel)
{
	struct publish_event *publish_event;
	struct subscribe_event *subscribe_event;
	SaAisErrorT ais_res;

	while ((publish_event = AST_LIST_REMOVE_HEAD(&event_channel->publish_events, entry)))
		publish_event_destroy(publish_event);
	while ((subscribe_event = AST_LIST_REMOVE_HEAD(&event_channel->subscribe_events, entry)))
		subscribe_event_destroy(event_channel, subscribe_event);

	ais_res = saEvtChannelClose(event_channel->handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error closing event channel '%s': %s\n",
			event_channel->name, ais_err2str(ais_res));
	}

	free(event_channel);
}

static void destroy_event_channels(void)
{
	struct event_channel *event_channel;

	AST_RWLIST_WRLOCK(&event_channels);
	while ((event_channel = AST_RWLIST_REMOVE_HEAD(&event_channels, entry))) {
		event_channel_destroy(event_channel);
	}
	AST_RWLIST_UNLOCK(&event_channels);
}

int ast_ais_evt_load_module(void)
{
	evt_init_res = saEvtInitialize(&evt_handle, &evt_callbacks, &ais_version);
	if (evt_init_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Could not initialize eventing service: %s\n",
			ais_err2str(evt_init_res));
		return -1;
	}

	load_config();

	ast_cli_register_multiple(ais_cli, ARRAY_LEN(ais_cli));

	return 0;
}

int ast_ais_evt_unload_module(void)
{
	SaAisErrorT ais_res;

	if (evt_init_res != SA_AIS_OK) {
		return 0;
	}

	destroy_event_channels();

	ais_res = saEvtFinalize(evt_handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Problem stopping eventing service: %s\n",
			ais_err2str(ais_res));
		return -1;
	}

	return 0;
}
