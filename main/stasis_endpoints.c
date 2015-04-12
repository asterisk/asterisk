/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Stasis endpoint API.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_endpoints.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="PeerStatus">
		<managerEventInstance class="EVENT_FLAG_SYSTEM">
			<synopsis>Raised when the state of a peer changes.</synopsis>
			<syntax>
				<parameter name="ChannelType">
					<para>The channel technology of the peer.</para>
				</parameter>
				<parameter name="Peer">
					<para>The name of the peer (including channel technology).</para>
				</parameter>
				<parameter name="PeerStatus">
					<para>New status of the peer.</para>
					<enumlist>
						<enum name="Unknown"/>
						<enum name="Registered"/>
						<enum name="Unregistered"/>
						<enum name="Rejected"/>
						<enum name="Lagged"/>
					</enumlist>
				</parameter>
				<parameter name="Cause">
					<para>The reason the status has changed.</para>
				</parameter>
				<parameter name="Address">
					<para>New address of the peer.</para>
				</parameter>
				<parameter name="Port">
					<para>New port for the peer.</para>
				</parameter>
				<parameter name="Time">
					<para>Time it takes to reach the peer and receive a response.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
***/

static struct stasis_cp_all *endpoint_cache_all;

struct stasis_cp_all *ast_endpoint_cache_all(void)
{
	return endpoint_cache_all;
}

struct stasis_cache *ast_endpoint_cache(void)
{
	return stasis_cp_all_cache(endpoint_cache_all);
}

struct stasis_topic *ast_endpoint_topic_all(void)
{
	return stasis_cp_all_topic(endpoint_cache_all);
}

struct stasis_topic *ast_endpoint_topic_all_cached(void)
{
	return stasis_cp_all_topic_cached(endpoint_cache_all);
}

static struct ast_manager_event_blob *peerstatus_to_ami(struct stasis_message *msg);

STASIS_MESSAGE_TYPE_DEFN(ast_endpoint_snapshot_type);
STASIS_MESSAGE_TYPE_DEFN(ast_endpoint_state_type,
	.to_ami = peerstatus_to_ami,
);

static struct ast_manager_event_blob *peerstatus_to_ami(struct stasis_message *msg)
{
	struct ast_endpoint_blob *obj = stasis_message_data(msg);
	RAII_VAR(struct ast_str *, peerstatus_event_string, ast_str_create(64), ast_free);
	const char *value;

	/* peer_status is the only *required* thing */
	if (!(value = ast_json_string_get(ast_json_object_get(obj->blob, "peer_status")))) {
		return NULL;
	}
	ast_str_append(&peerstatus_event_string, 0, "PeerStatus: %s\r\n", value);

	if ((value = ast_json_string_get(ast_json_object_get(obj->blob, "cause")))) {
		ast_str_append(&peerstatus_event_string, 0, "Cause: %s\r\n", value);
	}
	if ((value = ast_json_string_get(ast_json_object_get(obj->blob, "address")))) {
		ast_str_append(&peerstatus_event_string, 0, "Address: %s\r\n", value);
	}
	if ((value = ast_json_string_get(ast_json_object_get(obj->blob, "port")))) {
		ast_str_append(&peerstatus_event_string, 0, "Port: %s\r\n", value);
	}
	if ((value = ast_json_string_get(ast_json_object_get(obj->blob, "time")))) {
		ast_str_append(&peerstatus_event_string, 0, "Time: %s\r\n", value);
	}

	return ast_manager_event_blob_create(EVENT_FLAG_SYSTEM, "PeerStatus",
		"ChannelType: %s\r\n"
		"Peer: %s/%s\r\n"
		"%s",
		obj->snapshot->tech,
		obj->snapshot->tech,
		obj->snapshot->resource,
		ast_str_buffer(peerstatus_event_string));
}

static void endpoint_blob_dtor(void *obj)
{
	struct ast_endpoint_blob *event = obj;
	ao2_cleanup(event->snapshot);
	ast_json_unref(event->blob);
}

struct stasis_message *ast_endpoint_blob_create(struct ast_endpoint *endpoint,
	struct stasis_message_type *type, struct ast_json *blob)
{
	RAII_VAR(struct ast_endpoint_blob *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	if (!type) {
		return NULL;
	}
	if (!blob) {
		blob = ast_json_null();
	}

	if (!(obj = ao2_alloc(sizeof(*obj), endpoint_blob_dtor))) {
		return NULL;
	}

	if (endpoint) {
		if (!(obj->snapshot = ast_endpoint_snapshot_create(endpoint))) {
			return NULL;
		}
	}

	obj->blob = ast_json_ref(blob);

	if (!(msg = stasis_message_create(type, obj))) {
		return NULL;
	}

	ao2_ref(msg, +1);
	return msg;
}

void ast_endpoint_blob_publish(struct ast_endpoint *endpoint, struct stasis_message_type *type,
	struct ast_json *blob)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	if (blob) {
		message = ast_endpoint_blob_create(endpoint, type, blob);
	}
	if (message) {
		stasis_publish(ast_endpoint_topic(endpoint), message);
	}
}

struct ast_endpoint_snapshot *ast_endpoint_latest_snapshot(const char *tech,
	const char *name)
{
	RAII_VAR(char *, id, NULL, ast_free);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct ast_endpoint_snapshot *snapshot;

	if (ast_strlen_zero(name)) {
		ast_asprintf(&id, "%s", tech);
	} else {
		ast_asprintf(&id, "%s/%s", tech, name);
	}
	if (!id) {
		return NULL;
	}
	ast_tech_to_upper(id);

	msg = stasis_cache_get(ast_endpoint_cache(),
		ast_endpoint_snapshot_type(), id);
	if (!msg) {
		return NULL;
	}

	snapshot = stasis_message_data(msg);
	ast_assert(snapshot != NULL);

	ao2_ref(snapshot, +1);
	return snapshot;
}

/*!
 * \brief Callback extract a unique identity from a snapshot message.
 *
 * This identity is unique to the underlying object of the snapshot, such as the
 * UniqueId field of a channel.
 *
 * \param message Message to extract id from.
 * \return String representing the snapshot's id.
 * \return \c NULL if the message_type of the message isn't a handled snapshot.
 * \since 12
 */
static const char *endpoint_snapshot_get_id(struct stasis_message *message)
{
	struct ast_endpoint_snapshot *snapshot;

	if (ast_endpoint_snapshot_type() != stasis_message_type(message)) {
		return NULL;
	}

	snapshot = stasis_message_data(message);

	return snapshot->id;
}


struct ast_json *ast_endpoint_snapshot_to_json(
	const struct ast_endpoint_snapshot *snapshot,
	const struct stasis_message_sanitizer *sanitize)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ast_json *channel_array;
	int i;

	json = ast_json_pack("{s: s, s: s, s: s, s: []}",
		"technology", snapshot->tech,
		"resource", snapshot->resource,
		"state", ast_endpoint_state_to_string(snapshot->state),
		"channel_ids");

	if (json == NULL) {
		return NULL;
	}

	if (snapshot->max_channels != -1) {
		int res = ast_json_object_set(json, "max_channels",
			ast_json_integer_create(snapshot->max_channels));
		if (res != 0) {
			return NULL;
		}
	}

	channel_array = ast_json_object_get(json, "channel_ids");
	ast_assert(channel_array != NULL);
	for (i = 0; i < snapshot->num_channels; ++i) {
		int res;

		if (sanitize && sanitize->channel_id
			&& sanitize->channel_id(snapshot->channel_ids[i])) {
			continue;
		}

		res = ast_json_array_append(channel_array,
			ast_json_string_create(snapshot->channel_ids[i]));
		if (res != 0) {
			return NULL;
		}
	}

	return ast_json_ref(json);
}

static void endpoints_stasis_cleanup(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(ast_endpoint_snapshot_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_endpoint_state_type);

	ao2_cleanup(endpoint_cache_all);
	endpoint_cache_all = NULL;
}

int ast_endpoint_stasis_init(void)
{
	int res = 0;
	ast_register_cleanup(endpoints_stasis_cleanup);

	endpoint_cache_all = stasis_cp_all_create("endpoint_topic_all",
		endpoint_snapshot_get_id);
	if (!endpoint_cache_all) {
		return -1;
	}

	res |= STASIS_MESSAGE_TYPE_INIT(ast_endpoint_snapshot_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_endpoint_state_type);

	return res;
}
