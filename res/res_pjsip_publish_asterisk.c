/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_outbound_publish</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <regex.h>

#include <pjsip.h>
#include <pjsip_simple.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_outbound_publish.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_publish_asterisk" language="en_US">
		<synopsis>SIP resource for inbound and outbound Asterisk event publications</synopsis>
		<description><para>
			<emphasis>Inbound and outbound Asterisk event publication</emphasis>
			</para>
			<para>This module allows <literal>res_pjsip</literal> to send and receive Asterisk event publications.</para>
		</description>
		<configFile name="pjsip.conf">
			<configObject name="asterisk-publication">
				<synopsis>The configuration for inbound Asterisk event publication</synopsis>
				<description><para>
					Publish is <emphasis>COMPLETELY</emphasis> separate from the rest of
					<literal>pjsip.conf</literal>.
				</para></description>
				<configOption name="devicestate_publish">
					<synopsis>Optional name of a publish item that can be used to publish a request for full device state information.</synopsis>
				</configOption>
				<configOption name="mailboxstate_publish">
					<synopsis>Optional name of a publish item that can be used to publish a request for full mailbox state information.</synopsis>
				</configOption>
				<configOption name="device_state" default="no">
					<synopsis>Whether we should permit incoming device state events.</synopsis>
				</configOption>
				<configOption name="device_state_filter">
					<synopsis>Optional regular expression used to filter what devices we accept events for.</synopsis>
				</configOption>
				<configOption name="mailbox_state" default="no">
					<synopsis>Whether we should permit incoming mailbox state events.</synopsis>
				</configOption>
				<configOption name="mailbox_state_filter">
					<synopsis>Optional regular expression used to filter what mailboxes we accept events for.</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'asterisk-publication'.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! \brief Structure which contains Asterisk device state publisher state information */
struct asterisk_devicestate_publisher_state {
	/*! \brief The publish client to send PUBLISH messages on */
	struct ast_sip_outbound_publish_client *client;
	/*! \brief Device state subscription */
	struct stasis_subscription *device_state_subscription;
	/*! \brief Regex used for filtering outbound device state */
	regex_t device_state_regex;
	/*! \brief Device state should be filtered */
	unsigned int device_state_filter;
};

/*! \brief Structure which contains Asterisk mailbox publisher state information */
struct asterisk_mwi_publisher_state {
	/*! \brief The publish client to send PUBLISH messages on */
	struct ast_sip_outbound_publish_client *client;
	/*! \brief Mailbox state subscription */
	struct stasis_subscription *mailbox_state_subscription;
	/*! \brief Regex used for filtering outbound mailbox state */
	regex_t mailbox_state_regex;
	/*! \brief Mailbox state should be filtered */
	unsigned int mailbox_state_filter;
};

/*! \brief Structure which contains Asterisk publication information */
struct asterisk_publication_config {
	/*! \brief Sorcery object details */
	SORCERY_OBJECT(details);
	/*! \brief Stringfields */
	AST_DECLARE_STRING_FIELDS(
		/*! \brief Optional name of a device state publish item, used to request the remote side update us */
		AST_STRING_FIELD(devicestate_publish);
		/*! \brief Optional name of a mailbox state publish item, used to request the remote side update us */
		AST_STRING_FIELD(mailboxstate_publish);
	);
	/*! \brief Accept inbound device state events */
	unsigned int device_state;
	/*! \brief Regex used for filtering inbound device state */
	regex_t device_state_regex;
	/*! \brief Device state should be filtered */
	unsigned int device_state_filter;
	/*! \brief Accept inbound mailbox state events */
	unsigned int mailbox_state;
	/*! \brief Regex used for filtering inbound mailbox state */
	regex_t mailbox_state_regex;
	/*! \brief Mailbox state should be filtered */
	unsigned int mailbox_state_filter;
};

/*! \brief Destroy callback for Asterisk devicestate publisher state information from datastore */
static void asterisk_devicestate_publisher_state_destroy(void *obj)
{
	struct asterisk_devicestate_publisher_state *publisher_state = obj;

	ao2_cleanup(publisher_state->client);

	if (publisher_state->device_state_filter) {
		regfree(&publisher_state->device_state_regex);
	}
}

/*! \brief Datastore for attaching devicestate publisher state information */
static const struct ast_datastore_info asterisk_devicestate_publisher_state_datastore = {
	.type = "asterisk-devicestate-publisher",
	.destroy = asterisk_devicestate_publisher_state_destroy,
};

/*! \brief Destroy callback for Asterisk mwi publisher state information from datastore */
static void asterisk_mwi_publisher_state_destroy(void *obj)
{
	struct asterisk_mwi_publisher_state *publisher_state = obj;

	ao2_cleanup(publisher_state->client);

	if (publisher_state->mailbox_state_filter) {
		regfree(&publisher_state->mailbox_state_regex);
	}
}

/*! \brief Datastore for attaching devicestate publisher state information */
static const struct ast_datastore_info asterisk_mwi_publisher_state_datastore = {
	.type = "asterisk-mwi-publisher",
	.destroy = asterisk_mwi_publisher_state_destroy,
};

/*!
 * \brief Callback function for device state events
 * \param ast_event
 * \param data void pointer to ast_client structure
 * \return void
 */
static void asterisk_publisher_devstate_cb(void *data, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_datastore *datastore = data;
	struct asterisk_devicestate_publisher_state *publisher_state = datastore->data;
	struct ast_device_state_message *dev_state;
	char eid_str[20];
	struct ast_json *json;
	char *text;
	struct ast_sip_body body = {
		.type = "application",
		.subtype = "json",
	};

	if (!stasis_subscription_is_subscribed(sub) || ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	dev_state = stasis_message_data(msg);
	if (!dev_state->eid || ast_eid_cmp(&ast_eid_default, dev_state->eid)) {
		/* If the event is aggregate or didn't originate from this server, don't send it out. */
		return;
	}

	if (publisher_state->device_state_filter && regexec(&publisher_state->device_state_regex, dev_state->device, 0, NULL, 0)) {
		/* Outgoing device state has been filtered and the device name does not match */
		return;
	}

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);
	json = ast_json_pack(
		"{ s: s, s: s, s: s, s: i, s:s }",
		"type", "devicestate",
		"device", dev_state->device,
		"state", ast_devstate_str(dev_state->state),
		"cachable", dev_state->cachable,
		"eid", eid_str);
	if (!json) {
		return;
	}

	text = ast_json_dump_string(json);
	if (!text) {
		ast_json_unref(json);
		return;
	}
	body.body_text = text;

	ast_sip_publish_client_send(publisher_state->client, &body);

	ast_json_free(text);
	ast_json_unref(json);
}

/*!
 * \brief Callback function for mailbox state events
 * \param ast_event
 * \param data void pointer to ast_client structure
 * \return void
 */
static void asterisk_publisher_mwistate_cb(void *data, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_datastore *datastore = data;
	struct asterisk_mwi_publisher_state *publisher_state = datastore->data;
	struct ast_mwi_state *mwi_state;
	char eid_str[20];
	struct ast_json *json;
	char *text;
	struct ast_sip_body body = {
		.type = "application",
		.subtype = "json",
	};

	if (!stasis_subscription_is_subscribed(sub) || ast_mwi_state_type() != stasis_message_type(msg)) {
		return;
	}

	mwi_state = stasis_message_data(msg);
	if (ast_eid_cmp(&ast_eid_default, &mwi_state->eid)) {
		/* If the event is aggregate or didn't originate from this server, don't send it out. */
		return;
	}

	if (publisher_state->mailbox_state_filter && regexec(&publisher_state->mailbox_state_regex, mwi_state->uniqueid, 0, NULL, 0)) {
		/* Outgoing mailbox state has been filtered and the uniqueid does not match */
		return;
	}

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);
	json = ast_json_pack(
		"{ s: s, s: s, s: i, s: i, s:s }",
		"type", "mailboxstate",
		"uniqueid", mwi_state->uniqueid,
		"old", mwi_state->old_msgs,
		"new", mwi_state->new_msgs,
		"eid", eid_str);
	if (!json) {
		return;
	}

	text = ast_json_dump_string(json);
	if (!text) {
		ast_json_unref(json);
		return;
	}
	body.body_text = text;

	ast_sip_publish_client_send(publisher_state->client, &body);

	ast_json_free(text);
	ast_json_unref(json);
}

static int cached_devstate_cb(void *obj, void *arg, int flags)
{
	struct stasis_message *msg = obj;
	struct ast_datastore *datastore = arg;
	struct asterisk_devicestate_publisher_state *publisher_state = datastore->data;

	asterisk_publisher_devstate_cb(arg, publisher_state->device_state_subscription, msg);

	return 0;
}

static int cached_mwistate_cb(void *obj, void *arg, int flags)
{
	struct stasis_message *msg = obj;
	struct ast_datastore *datastore = arg;
	struct asterisk_mwi_publisher_state *publisher_state = datastore->data;

	asterisk_publisher_mwistate_cb(arg, publisher_state->mailbox_state_subscription, msg);

	return 0;
}

static int build_regex(regex_t *regex, const char *text)
{
	int res;

	if ((res = regcomp(regex, text, REG_EXTENDED | REG_ICASE | REG_NOSUB))) {
		size_t len = regerror(res, regex, NULL, 0);
		char buf[len];
		regerror(res, regex, buf, len);
		ast_log(LOG_ERROR, "Could not compile regex '%s': %s\n", text, buf);
		return -1;
	}

	return 0;
}

static int asterisk_start_devicestate_publishing(struct ast_sip_outbound_publish *configuration,
	struct ast_sip_outbound_publish_client *client)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	struct asterisk_devicestate_publisher_state *publisher_state;
	const char *value;
	struct ao2_container *cached;

	datastore = ast_sip_publish_client_alloc_datastore(&asterisk_devicestate_publisher_state_datastore,
		"asterisk-devicestate-publisher");
	if (!datastore) {
		return -1;
	}

	publisher_state = ast_calloc(1, sizeof(struct asterisk_devicestate_publisher_state));
	if (!publisher_state) {
		return -1;
	}
	datastore->data = publisher_state;

	value = ast_sorcery_object_get_extended(configuration, "device_state_filter");
	if (!ast_strlen_zero(value)) {
		if (build_regex(&publisher_state->device_state_regex, value)) {
			return -1;
		}
		publisher_state->device_state_filter = 1;
	}

	publisher_state->client = ao2_bump(client);

	if (ast_sip_publish_client_add_datastore(client, datastore)) {
		return -1;
	}

	publisher_state->device_state_subscription = stasis_subscribe(ast_device_state_topic_all(),
		asterisk_publisher_devstate_cb, ao2_bump(datastore));
	if (!publisher_state->device_state_subscription) {
		ast_sip_publish_client_remove_datastore(client, "asterisk-devicestate-publisher");
		ao2_ref(datastore, -1);
		return -1;
	}

	cached = stasis_cache_dump(ast_device_state_cache(), NULL);
	ao2_callback(cached, OBJ_NODATA, cached_devstate_cb, datastore);
	ao2_ref(cached, -1);

	return 0;
}

static int asterisk_stop_devicestate_publishing(struct ast_sip_outbound_publish_client *client)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_publish_client_get_datastore(client, "asterisk-devicestate-publisher"),
		ao2_cleanup);
	struct asterisk_devicestate_publisher_state *publisher_state;

	if (!datastore) {
		return 0;
	}

	publisher_state = datastore->data;
	if (publisher_state->device_state_subscription) {
		stasis_unsubscribe_and_join(publisher_state->device_state_subscription);
		ao2_ref(datastore, -1);
	}

	ast_sip_publish_client_remove_datastore(client, "asterisk-devicestate-publisher");

	return 0;
}

struct ast_sip_event_publisher_handler asterisk_devicestate_publisher_handler = {
	.event_name = "asterisk-devicestate",
	.start_publishing = asterisk_start_devicestate_publishing,
	.stop_publishing = asterisk_stop_devicestate_publishing,
};

static int asterisk_start_mwi_publishing(struct ast_sip_outbound_publish *configuration,
	struct ast_sip_outbound_publish_client *client)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	struct asterisk_mwi_publisher_state *publisher_state;
	const char *value;
	struct ao2_container *cached;

	datastore = ast_sip_publish_client_alloc_datastore(&asterisk_mwi_publisher_state_datastore, "asterisk-mwi-publisher");
	if (!datastore) {
		return -1;
	}

	publisher_state = ast_calloc(1, sizeof(struct asterisk_mwi_publisher_state));
	if (!publisher_state) {
		return -1;
	}
	datastore->data = publisher_state;

	value = ast_sorcery_object_get_extended(configuration, "mailbox_state_filter");
	if (!ast_strlen_zero(value)) {
		if (build_regex(&publisher_state->mailbox_state_regex, value)) {
			return -1;
		}
		publisher_state->mailbox_state_filter = 1;
	}

	publisher_state->client = ao2_bump(client);

	if (ast_sip_publish_client_add_datastore(client, datastore)) {
		return -1;
	}

	publisher_state->mailbox_state_subscription = stasis_subscribe(ast_mwi_topic_all(),
		asterisk_publisher_mwistate_cb, ao2_bump(datastore));
	if (!publisher_state->mailbox_state_subscription) {
		ast_sip_publish_client_remove_datastore(client, "asterisk-mwi-publisher");
		ao2_ref(datastore, -1);
		return -1;
	}

	cached = stasis_cache_dump(ast_mwi_state_cache(), NULL);
	ao2_callback(cached, OBJ_NODATA, cached_mwistate_cb, datastore);
	ao2_ref(cached, -1);

	return 0;
}

static int asterisk_stop_mwi_publishing(struct ast_sip_outbound_publish_client *client)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_publish_client_get_datastore(client, "asterisk-mwi-publisher"),
		ao2_cleanup);
	struct asterisk_mwi_publisher_state *publisher_state;

	if (!datastore) {
		return 0;
	}

	publisher_state = datastore->data;
	if (publisher_state->mailbox_state_subscription) {
		stasis_unsubscribe_and_join(publisher_state->mailbox_state_subscription);
		ao2_ref(datastore, -1);
	}

	ast_sip_publish_client_remove_datastore(client, "asterisk-mwi-publisher");

	return 0;
}

struct ast_sip_event_publisher_handler asterisk_mwi_publisher_handler = {
	.event_name = "asterisk-mwi",
	.start_publishing = asterisk_start_mwi_publishing,
	.stop_publishing = asterisk_stop_mwi_publishing,
};

static int asterisk_publication_new(struct ast_sip_endpoint *endpoint, const char *resource, const char *event_configuration)
{
	RAII_VAR(struct asterisk_publication_config *, config, ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "asterisk-publication",
		event_configuration), ao2_cleanup);

	/* If no inbound Asterisk publication configuration exists reject the PUBLISH */
	if (!config) {
		return 404;
	}

	return 200;
}

static int asterisk_publication_devicestate(struct ast_sip_publication *pub, struct asterisk_publication_config *config,
	struct ast_eid *pubsub_eid, struct ast_json *json)
{
	const char *device = ast_json_string_get(ast_json_object_get(json, "device"));
	const char *state = ast_json_string_get(ast_json_object_get(json, "state"));
	int cachable = ast_json_integer_get(ast_json_object_get(json, "cachable"));

	if (!config->device_state) {
		ast_debug(2, "Received device state event for resource '%s' but it is not configured to accept them\n",
			ast_sorcery_object_get_id(config));
		return 0;
	}

	if (ast_strlen_zero(device) || ast_strlen_zero(state)) {
		ast_debug(1, "Received incomplete device state event for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}

	if (config->device_state_filter && regexec(&config->device_state_regex, device, 0, NULL, 0)) {
		ast_debug(2, "Received device state on resource '%s' for device '%s' but it has been filtered out\n",
			ast_sorcery_object_get_id(config), device);
		return 0;
	}

	ast_publish_device_state_full(device, ast_devstate_val(state),
		cachable == AST_DEVSTATE_CACHABLE ? AST_DEVSTATE_CACHABLE : AST_DEVSTATE_NOT_CACHABLE,
		pubsub_eid);

	return 0;
}

static int asterisk_publication_mailboxstate(struct ast_sip_publication *pub, struct asterisk_publication_config *config,
	struct ast_eid *pubsub_eid, struct ast_json *json)
{
	const char *uniqueid = ast_json_string_get(ast_json_object_get(json, "uniqueid"));
	int old_msgs = ast_json_integer_get(ast_json_object_get(json, "old"));
	int new_msgs = ast_json_integer_get(ast_json_object_get(json, "new"));
	char *item_id;
	const char *mailbox;

	if (!config->mailbox_state) {
		ast_debug(2, "Received mailbox state event for resource '%s' but it is not configured to accept them\n",
			ast_sorcery_object_get_id(config));
		return 0;
	}

	if (ast_strlen_zero(uniqueid)) {
		ast_debug(1, "Received incomplete mailbox state event for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}

	if (config->mailbox_state_filter && regexec(&config->mailbox_state_regex, uniqueid, 0, NULL, 0)) {
		ast_debug(2, "Received mailbox state on resource '%s' for uniqueid '%s' but it has been filtered out\n",
			ast_sorcery_object_get_id(config), uniqueid);
		return 0;
	}

	item_id = ast_strdupa(uniqueid);
	mailbox = strsep(&item_id, "@");

	ast_publish_mwi_state_full(mailbox, item_id, new_msgs, old_msgs, NULL, pubsub_eid);

	return 0;
}

static int asterisk_publication_devicestate_refresh(struct ast_sip_publication *pub,
	struct asterisk_publication_config *config, struct ast_eid *pubsub_eid, struct ast_json *json)
{
	struct ast_sip_outbound_publish_client *client;
	struct ast_datastore *datastore;
	struct ao2_container *cached;

	if (ast_strlen_zero(config->devicestate_publish)) {
		return 0;
	}

	client = ast_sip_publish_client_get(config->devicestate_publish);
	if (!client) {
		ast_log(LOG_ERROR, "Received refresh request for devicestate on publication '%s' but publish '%s' is not available\n",
			ast_sorcery_object_get_id(config), config->devicestate_publish);
		return 0;
	}

	datastore = ast_sip_publish_client_get_datastore(client, "asterisk-devicestate-publisher");
	if (!datastore) {
		ao2_ref(client, -1);
		return 0;
	}

	cached = stasis_cache_dump(ast_device_state_cache(), NULL);
	if (cached) {
		ao2_callback(cached, OBJ_NODATA, cached_devstate_cb, datastore);
		ao2_ref(cached, -1);
	}
	ao2_ref(client, -1);
	ao2_ref(datastore, -1);

	return 0;
}

static int asterisk_publication_devicestate_state_change(struct ast_sip_publication *pub, pjsip_msg_body *body,
			enum ast_sip_publish_state state)
{
	RAII_VAR(struct asterisk_publication_config *, config, ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "asterisk-publication",
		ast_sip_publication_get_event_configuration(pub)), ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	const char *eid, *type;
	struct ast_eid pubsub_eid;
	int res = -1;

	/* If no configuration exists for this publication it has most likely been removed, so drop this immediately */
	if (!config) {
		return -1;
	}

	/* If no body exists this is a refresh and can be ignored */
	if (!body) {
		return 0;
	}

	/* We only accept JSON for content */
	if (pj_strcmp2(&body->content_type.type, "application") ||
		pj_strcmp2(&body->content_type.subtype, "json")) {
		ast_debug(2, "Received unsupported content type for Asterisk event on resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}

	json = ast_json_load_buf(body->data, body->len, NULL);
	if (!json) {
		ast_debug(1, "Received unparseable JSON event for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}

	eid = ast_json_string_get(ast_json_object_get(json, "eid"));
	if (!eid) {
		ast_debug(1, "Received event without eid for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}
	ast_str_to_eid(&pubsub_eid, eid);

	type = ast_json_string_get(ast_json_object_get(json, "type"));
	if (!type) {
		ast_debug(1, "Received event without type for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	} else if (!strcmp(type, "devicestate")) {
		res = asterisk_publication_devicestate(pub, config, &pubsub_eid, json);
	} else if (!strcmp(type, "refresh")) {
		res = asterisk_publication_devicestate_refresh(pub, config, &pubsub_eid, json);
	}

	return res;
}

static int asterisk_publication_mwi_refresh(struct ast_sip_publication *pub,
	struct asterisk_publication_config *config, struct ast_eid *pubsub_eid, struct ast_json *json)
{
	struct ast_sip_outbound_publish_client *client;
	struct ast_datastore *datastore;
	struct ao2_container *cached;

	if (ast_strlen_zero(config->mailboxstate_publish)) {
		return 0;
	}

	client = ast_sip_publish_client_get(config->mailboxstate_publish);
	if (!client) {
		ast_log(LOG_ERROR, "Received refresh request for mwi state on publication '%s' but publish '%s' is not available\n",
			ast_sorcery_object_get_id(config), config->mailboxstate_publish);
		return 0;
	}

	datastore = ast_sip_publish_client_get_datastore(client, "asterisk-mwi-publisher");
	if (!datastore) {
		ao2_ref(client, -1);
		return 0;
	}

	cached = stasis_cache_dump(ast_mwi_state_cache(), NULL);
	if (cached) {
		ao2_callback(cached, OBJ_NODATA, cached_mwistate_cb, datastore);
		ao2_ref(cached, -1);
	}
	ao2_ref(client, -1);
	ao2_ref(datastore, -1);

	return 0;
}

static int asterisk_publication_mwi_state_change(struct ast_sip_publication *pub, pjsip_msg_body *body,
			enum ast_sip_publish_state state)
{
	RAII_VAR(struct asterisk_publication_config *, config, ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "asterisk-publication",
		ast_sip_publication_get_event_configuration(pub)), ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	const char *eid, *type;
	struct ast_eid pubsub_eid;
	int res = -1;

	/* If no configuration exists for this publication it has most likely been removed, so drop this immediately */
	if (!config) {
		return -1;
	}

	/* If no body exists this is a refresh and can be ignored */
	if (!body) {
		return 0;
	}

	/* We only accept JSON for content */
	if (pj_strcmp2(&body->content_type.type, "application") ||
		pj_strcmp2(&body->content_type.subtype, "json")) {
		ast_debug(2, "Received unsupported content type for Asterisk event on resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}

	json = ast_json_load_buf(body->data, body->len, NULL);
	if (!json) {
		ast_debug(1, "Received unparseable JSON event for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}

	eid = ast_json_string_get(ast_json_object_get(json, "eid"));
	if (!eid) {
		ast_debug(1, "Received event without eid for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	}
	ast_str_to_eid(&pubsub_eid, eid);

	type = ast_json_string_get(ast_json_object_get(json, "type"));
	if (!type) {
		ast_debug(1, "Received event without type for resource '%s'\n",
			ast_sorcery_object_get_id(config));
		return -1;
	} else if (!strcmp(type, "mailboxstate")) {
		res = asterisk_publication_mailboxstate(pub, config, &pubsub_eid, json);
	} else if (!strcmp(type, "refresh")) {
		res = asterisk_publication_mwi_refresh(pub, config, &pubsub_eid, json);
	}

	return res;
}

static int send_refresh_cb(void *obj, void *arg, int flags)
{
	struct asterisk_publication_config *config = obj;
	struct ast_sip_outbound_publish_client *client;

	if (!ast_strlen_zero(config->devicestate_publish)) {
		client = ast_sip_publish_client_get(config->devicestate_publish);
		if (client) {
			ast_sip_publish_client_send(client, arg);
			ao2_ref(client, -1);
		}
	}

	if (!ast_strlen_zero(config->mailboxstate_publish)) {
		client = ast_sip_publish_client_get(config->mailboxstate_publish);
		if (client) {
			ast_sip_publish_client_send(client, arg);
			ao2_ref(client, -1);
		}
	}

	return 0;
}

/*! \brief Internal function to send refresh requests to all publications */
static void asterisk_publication_send_refresh(void)
{
	struct ao2_container *publications = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "asterisk-publication", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	char eid_str[20];
	struct ast_json *json;
	char *text;
	struct ast_sip_body body = {
		.type = "application",
		.subtype = "json",
	};

	if (!publications) {
		return;
	}

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);
	json = ast_json_pack(
		"{ s: s, s: s }",
		"type", "refresh",
		"eid", eid_str);
	if (!json) {
		ao2_ref(publications, -1);
		return;
	}

	text = ast_json_dump_string(json);
	if (!text) {
		ast_json_unref(json);
		ao2_ref(publications, -1);
		return;
	}
	body.body_text = text;

	ao2_callback(publications, OBJ_NODATA, send_refresh_cb, &body);

	ast_json_free(text);
	ast_json_unref(json);
	ao2_ref(publications, -1);
}

struct ast_sip_publish_handler asterisk_devicestate_publication_handler = {
	.event_name = "asterisk-devicestate",
	.new_publication = asterisk_publication_new,
	.publication_state_change = asterisk_publication_devicestate_state_change,
};

struct ast_sip_publish_handler asterisk_mwi_publication_handler = {
	.event_name = "asterisk-mwi",
	.new_publication = asterisk_publication_new,
	.publication_state_change = asterisk_publication_mwi_state_change,
};

/*! \brief Destructor function for Asterisk publication configuration */
static void asterisk_publication_config_destroy(void *obj)
{
	struct asterisk_publication_config *config = obj;

	ast_string_field_free_memory(config);
}

/*! \brief Allocator function for Asterisk publication configuration */
static void *asterisk_publication_config_alloc(const char *name)
{
	struct asterisk_publication_config *config = ast_sorcery_generic_alloc(sizeof(*config),
		asterisk_publication_config_destroy);

	if (!config || ast_string_field_init(config, 256)) {
		ao2_cleanup(config);
		return NULL;
	}

	return config;
}

static int regex_filter_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct asterisk_publication_config *config = obj;
	int res = -1;

	if (ast_strlen_zero(var->value)) {
		return 0;
	}

	if (!strcmp(var->name, "device_state_filter")) {
		if (!(res = build_regex(&config->device_state_regex, var->value))) {
			config->device_state_filter = 1;
		}
	} else if (!strcmp(var->name, "mailbox_state_filter")) {
		if (!(res = build_regex(&config->mailbox_state_regex, var->value))) {
			config->mailbox_state_filter = 1;
		}
	}

	return res;
}

static int load_module(void)
{
	CHECK_PJSIP_PUBSUB_MODULE_LOADED();

	ast_sorcery_apply_config(ast_sip_get_sorcery(), "asterisk-publication");
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "asterisk-publication", "config", "pjsip.conf,criteria=type=asterisk-publication");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "asterisk-publication", asterisk_publication_config_alloc, NULL, NULL)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "asterisk-publication", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "asterisk-publication", "devicestate_publish", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct asterisk_publication_config, devicestate_publish));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "asterisk-publication", "mailboxstate_publish", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct asterisk_publication_config, mailboxstate_publish));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "asterisk-publication", "device_state", "no", OPT_BOOL_T, 1, FLDSET(struct asterisk_publication_config, device_state));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "asterisk-publication", "device_state_filter", "", regex_filter_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "asterisk-publication", "mailbox_state", "no", OPT_BOOL_T, 1, FLDSET(struct asterisk_publication_config, mailbox_state));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "asterisk-publication", "mailbox_state_filter", "", regex_filter_handler, NULL, NULL, 0, 0);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "asterisk-publication");

	if (ast_sip_register_publish_handler(&asterisk_devicestate_publication_handler)) {
		ast_log(LOG_WARNING, "Unable to register event publication handler %s\n",
			asterisk_devicestate_publication_handler.event_name);
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_sip_register_publish_handler(&asterisk_mwi_publication_handler)) {
		ast_log(LOG_WARNING, "Unable to register event publication handler %s\n",
			asterisk_mwi_publication_handler.event_name);
		ast_sip_unregister_publish_handler(&asterisk_devicestate_publication_handler);
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_sip_register_event_publisher_handler(&asterisk_devicestate_publisher_handler)) {
		ast_log(LOG_WARNING, "Unable to register event publisher handler %s\n",
			asterisk_devicestate_publisher_handler.event_name);
		ast_sip_unregister_publish_handler(&asterisk_devicestate_publication_handler);
		ast_sip_unregister_publish_handler(&asterisk_mwi_publication_handler);
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_sip_register_event_publisher_handler(&asterisk_mwi_publisher_handler)) {
		ast_log(LOG_WARNING, "Unable to register event publisher handler %s\n",
			asterisk_mwi_publisher_handler.event_name);
		ast_sip_unregister_event_publisher_handler(&asterisk_mwi_publisher_handler);
		ast_sip_unregister_publish_handler(&asterisk_devicestate_publication_handler);
		ast_sip_unregister_publish_handler(&asterisk_mwi_publication_handler);
		return AST_MODULE_LOAD_DECLINE;
	}

	asterisk_publication_send_refresh();

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "asterisk-publication");
	asterisk_publication_send_refresh();
	return 0;
}

static int unload_module(void)
{
	ast_sip_unregister_publish_handler(&asterisk_devicestate_publication_handler);
	ast_sip_unregister_publish_handler(&asterisk_mwi_publication_handler);
	ast_sip_unregister_event_publisher_handler(&asterisk_devicestate_publisher_handler);
	ast_sip_unregister_event_publisher_handler(&asterisk_mwi_publisher_handler);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Asterisk Event PUBLISH Support",
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
