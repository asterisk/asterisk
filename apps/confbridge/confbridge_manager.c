/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief Confbridge manager events for stasis messages
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/manager.h"
#include "asterisk/stasis_message_router.h"
#include "include/confbridge.h"
#include "asterisk/message.h"
#include "asterisk/stream.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="ConfbridgeStart">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference starts.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeEnd</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeEnd">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference ends.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeStart</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeJoin">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel joins a Confbridge conference.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
				<channel_snapshot/>
				<parameter name="Admin">
					<para>Identifies this user as an admin user.</para>
					<enumlist>
						<enum name="Yes"/>
						<enum name="No"/>
					</enumlist>
				</parameter>
				<parameter name="Muted">
					<para>The joining mute status.</para>
					<enumlist>
						<enum name="Yes"/>
						<enum name="No"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeLeave</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeLeave">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a Confbridge conference.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
				<channel_snapshot/>
				<parameter name="Admin">
					<para>Identifies this user as an admin user.</para>
					<enumlist>
						<enum name="Yes"/>
						<enum name="No"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeJoin</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeRecord">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference starts recording.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeStopRecord</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeStopRecord">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference that was recording stops recording.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeRecord</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeMute">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a Confbridge participant mutes.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
				<channel_snapshot/>
				<parameter name="Admin">
					<para>Identifies this user as an admin user.</para>
					<enumlist>
						<enum name="Yes"/>
						<enum name="No"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeUnmute</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeUnmute">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a confbridge participant unmutes.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
				<channel_snapshot/>
				<parameter name="Admin">
					<para>Identifies this user as an admin user.</para>
					<enumlist>
						<enum name="Yes"/>
						<enum name="No"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeMute</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeTalking">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a confbridge participant begins or ends talking.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<bridge_snapshot/>
				<channel_snapshot/>
				<parameter name="TalkingStatus">
					<enumlist>
						<enum name="on"/>
						<enum name="off"/>
					</enumlist>
				</parameter>
				<parameter name="Admin">
					<para>Identifies this user as an admin user.</para>
					<enumlist>
						<enum name="Yes"/>
						<enum name="No"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
***/

static struct stasis_message_router *bridge_state_router;
static struct stasis_message_router *channel_state_router;

STASIS_MESSAGE_TYPE_DEFN(confbridge_start_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_end_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_join_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_leave_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_start_record_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_stop_record_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_mute_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_unmute_type);
STASIS_MESSAGE_TYPE_DEFN(confbridge_talking_type);
/*
 * The welcome message is defined here but is only sent
 * to participants and only when events are enabled.
 * At the current time, no actual stasis or AMI events
 * are generated for this type.
 */
STASIS_MESSAGE_TYPE_DEFN(confbridge_welcome_type);

const char *confbridge_event_type_to_string(struct stasis_message_type *event_type)
{
	if (event_type == confbridge_start_type()) {
		return "ConfbridgeStart";
	} else if (event_type == confbridge_end_type()) {
		return "ConfbridgeEnd";
	} else if (event_type == confbridge_join_type()) {
		return "ConfbridgeJoin";
	} else if (event_type == confbridge_leave_type()) {
		return "ConfbridgeLeave";
	} else if (event_type == confbridge_start_record_type()) {
		return "ConfbridgeRecord";
	} else if (event_type == confbridge_stop_record_type()) {
		return "ConfbridgeStopRecord";
	} else if (event_type == confbridge_mute_type()) {
		return "ConfbridgeMute";
	} else if (event_type == confbridge_unmute_type()) {
		return "ConfbridgeUnmute";
	} else if (event_type == confbridge_talking_type()) {
		return "ConfbridgeTalking";
	} else if (event_type == confbridge_welcome_type()) {
		return "ConfbridgeWelcome";
	} else {
		return "unknown";
	}
}

static struct ast_json *channel_to_json(struct ast_channel_snapshot *channel_snapshot,
	struct ast_json *conf_blob, struct ast_json *labels_blob)
{
	struct ast_json *json_channel = ast_channel_snapshot_to_json(channel_snapshot, NULL);

	if (!json_channel) {
		return NULL;
	}

	/* These items are removed for privacy reasons. */
	ast_json_object_del(json_channel, "dialplan");
	ast_json_object_del(json_channel, "connected");
	ast_json_object_del(json_channel, "accountcode");

	/* conf_blob contains flags such as talking, admin, mute, etc. */
	if (conf_blob) {
		struct ast_json *conf_copy = ast_json_copy(conf_blob);

		if (!conf_copy) {
			ast_json_unref(json_channel);
			return NULL;
		}
		ast_json_object_del(conf_copy, "conference");
		ast_json_object_update(json_channel, conf_copy);
		ast_json_unref(conf_copy);
	}

	/* labels_blob contains the msid labels to correlate to streams. */
	if (labels_blob) {
		ast_json_object_update(json_channel, labels_blob);
	}

	return json_channel;
}

static struct ast_json *bridge_to_json(struct ast_bridge_snapshot *bridge_snapshot)
{
	struct ast_json *json_bridge = ast_bridge_snapshot_to_json(bridge_snapshot, NULL);

	if (!json_bridge) {
		return NULL;
	}

	/* These items have no use in the context of bridge participant info. */
	ast_json_object_del(json_bridge, "technology");
	ast_json_object_del(json_bridge, "bridge_type");
	ast_json_object_del(json_bridge, "bridge_class");
	ast_json_object_del(json_bridge, "creator");
	ast_json_object_del(json_bridge, "channels");

	return json_bridge;
}

static struct ast_json *pack_bridge_and_channels(
	struct ast_json *json_bridge, struct ast_json *json_channels,
	struct stasis_message * msg)
{
	const struct timeval *tv = stasis_message_timestamp(msg);
	const char *msg_name = confbridge_event_type_to_string(stasis_message_type(msg));
	const char *fmt = ast_json_typeof(json_channels) == AST_JSON_ARRAY ?
		"{s: s, s: o, s: o, s: o }" : "{s: s, s: o, s: o, s: [ o ] }";

	return ast_json_pack(fmt,
		"type", msg_name,
		"timestamp", ast_json_timeval(*tv, NULL),
		"bridge", json_bridge,
		"channels", json_channels);
}

static struct ast_json *pack_snapshots(	struct ast_bridge_snapshot *bridge_snapshot,
	struct ast_channel_snapshot *channel_snapshot, 	struct ast_json *conf_blob,
	struct ast_json *labels_blob, struct stasis_message * msg)
{
	struct ast_json *json_bridge;
	struct ast_json *json_channel;

	json_bridge = bridge_to_json(bridge_snapshot);
	json_channel = channel_to_json(channel_snapshot, conf_blob, labels_blob);

	return pack_bridge_and_channels(json_bridge, json_channel, msg);
}

static void send_message(const char *msg_name, char *conf_name, struct ast_json *json_object,
	struct ast_channel *chan)
{
	struct ast_msg_data *data_msg;
	struct ast_msg_data_attribute attrs[] = {
		{ .type = AST_MSG_DATA_ATTR_FROM, conf_name },
		{ .type = AST_MSG_DATA_ATTR_CONTENT_TYPE, .value = "application/x-asterisk-confbridge-event+json"},
		{ .type = AST_MSG_DATA_ATTR_BODY, },
	};
	char *json;
	int rc = 0;
	struct ast_frame f;
	struct ast_bridge_channel *bridge_chan;

	bridge_chan = ast_channel_get_bridge_channel(chan);
	if (!bridge_chan) {
		/* Don't complain if we can't get the bridge_chan. The channel is probably gone. */
		return;
	}

	json = ast_json_dump_string_format(json_object, AST_JSON_PRETTY);
	if (!json) {
		ast_log(LOG_ERROR, "Unable to convert json_object for %s message to string\n", msg_name);
		return;
	}
	attrs[2].value = json;

	data_msg = ast_msg_data_alloc(AST_MSG_DATA_SOURCE_TYPE_IN_DIALOG, attrs, ARRAY_LEN(attrs));
	if (!data_msg) {
		ast_log(LOG_ERROR, "Unable to create %s message for channel '%s'\n", msg_name,
			ast_channel_name(chan));
		ast_json_free(json);
		return;
	}

	memset(&f, 0, sizeof(f));
	f.frametype = AST_FRAME_TEXT_DATA;
	f.data.ptr = data_msg;
	f.datalen = ast_msg_data_get_length(data_msg);

	rc = ast_bridge_channel_queue_frame(bridge_chan, &f);
	ast_free(data_msg);
	if (rc != 0) {
		/* Don't complain if we can't send a leave message. The channel is probably gone. */
		if (strcmp(confbridge_event_type_to_string(confbridge_leave_type()), msg_name) != 0) {
			ast_log(LOG_ERROR, "Failed to queue %s message to '%s'\n%s\n", msg_name,
				ast_channel_name(chan), json);
		}
		ast_json_free(json);
		return;
	}

	ast_debug(3, "Queued %s message to '%s'\n%s\n", msg_name, ast_channel_name(chan), json);
	ast_json_free(json);
}

void conf_send_event_to_participants(struct confbridge_conference *conference,
	struct ast_channel *chan, struct stasis_message *msg)
{
	struct ast_bridge_blob *obj = stasis_message_data(msg);
	struct ast_json *extras = obj->blob;
	struct user_profile u_profile = {{0}};
	int source_send_events = 0;
	int source_echo_events = 0;
	struct ast_json* json_channels = NULL;
	struct confbridge_user *user;
	const char *msg_name = confbridge_event_type_to_string(stasis_message_type(msg));

	ast_debug(3, "Distributing %s event to participants\n", msg_name);

	/* This could be a channel level event or a bridge level event */
	if (chan) {
		if (!conf_find_user_profile(chan, NULL, &u_profile)) {
			ast_log(LOG_ERROR, "Unable to retrieve user profile for channel '%s'\n",
				ast_channel_name(chan));
			return;
		}
		source_send_events = ast_test_flag(&u_profile, USER_OPT_SEND_EVENTS);
		source_echo_events = ast_test_flag(&u_profile, USER_OPT_ECHO_EVENTS);
		ast_debug(3, "send_events: %d  echo_events: %d for profile %s\n",
			source_send_events, source_echo_events, u_profile.name);
	}

	/* Now send a message to the participants with the json string. */
	ao2_lock(conference);
	AST_LIST_TRAVERSE(&conference->active_list, user, list) {
		struct ast_json *json_object;

		/*
		 * If the msg type is join, we need to capture all targets channel info so we can
		 * send a welcome message to the source channel with all current participants.
		 */
		if (source_send_events && stasis_message_type(msg) == confbridge_join_type()) {
			struct ast_channel_snapshot *target_snapshot;
			struct ast_json *target_json_channel;

			target_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(user->chan));
			if (!target_snapshot) {
				ast_log(LOG_ERROR, "Unable to get a channel snapshot for '%s'\n",
					ast_channel_name(user->chan));
				continue;
			}

			target_json_channel = channel_to_json(target_snapshot, extras, NULL);
			ao2_ref(target_snapshot, -1);

			if (!json_channels) {
				json_channels = ast_json_array_create();
				if (!json_channels) {
					ast_log(LOG_ERROR, "Unable to allocate json array\n");
					ast_json_unref(target_json_channel);
					return;
				}
			}

			ast_json_array_append(json_channels, target_json_channel);
		}

		/* Don't send a message to the user that triggered the event. */
		if (!source_echo_events && user->chan == chan) {
			ast_debug(3, "Skipping queueing %s message to '%s'. Same channel.\n", msg_name,
				ast_channel_name(user->chan));
			continue;
		}

		/* Don't send a message to users in profiles not sending events. */
		if (!ast_test_flag(&user->u_profile, USER_OPT_SEND_EVENTS)) {
			ast_debug(3, "Skipping queueing %s message to '%s'. Not receiving events.\n", msg_name,
				ast_channel_name(user->chan));
			continue;
		}

		json_object = pack_snapshots(obj->bridge, obj->channel, extras, NULL, msg);

		if (!json_object) {
			ast_log(LOG_ERROR, "Unable to convert %s message to json\n", msg_name);
			continue;
		}

		send_message(msg_name, conference->name, json_object, user->chan);
		ast_json_unref(json_object);
	}
	ao2_unlock(conference);

	/*
	 * If this is a join event, send the welcome message to just the joining user
	 * if it's not audio-only or otherwise restricted.
	 */
	if (source_send_events && json_channels
		&& stasis_message_type(msg) == confbridge_join_type()) {
		struct ast_json *json_object;
		struct ast_json *json_bridge;
		const char *welcome_msg_name = confbridge_event_type_to_string(confbridge_welcome_type());

		json_bridge = bridge_to_json(obj->bridge);
		json_object = pack_bridge_and_channels(json_bridge, json_channels, msg);
		if (!json_object) {
			ast_log(LOG_ERROR, "Unable to convert ConfbridgeWelcome message to json\n");
			return;
		}
		ast_json_string_set(ast_json_object_get(json_object, "type"), welcome_msg_name);

		send_message(welcome_msg_name, conference->name, json_object, chan);
		ast_json_unref(json_object);
	}
}

static void confbridge_publish_manager_event(
	struct stasis_message *message,
	struct ast_str *extra_text)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	const char *event = confbridge_event_type_to_string(stasis_message_type(message));
	const char *conference_name;
	RAII_VAR(struct ast_str *, bridge_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);

	ast_assert(blob != NULL);
	ast_assert(event != NULL);

	bridge_text = ast_manager_build_bridge_state_string(blob->bridge);
	if (!bridge_text) {
		return;
	}

	conference_name = ast_json_string_get(ast_json_object_get(blob->blob, "conference"));
	ast_assert(conference_name != NULL);

	if (blob->channel) {
		struct confbridge_conference *conference = conf_find_bridge(conference_name);

		channel_text = ast_manager_build_channel_state_string(blob->channel);
		ao2_cleanup(conference);
	}

	manager_event(EVENT_FLAG_CALL, event,
		"Conference: %s\r\n"
		"%s"
		"%s"
		"%s",
		conference_name,
		ast_str_buffer(bridge_text),
		channel_text ? ast_str_buffer(channel_text) : "",
		extra_text ? ast_str_buffer(extra_text) : "");
}

static int get_bool_header(struct ast_str **extra_text, struct stasis_message *message,
	const char *json_key, const char *ami_header)
{
	const struct ast_bridge_blob *blob = stasis_message_data(message);
	const struct ast_json *obj;

	obj = ast_json_object_get(blob->blob, json_key);
	if (!obj) {
		return -1;
	}

	return ast_str_append_event_header(extra_text, ami_header,
		AST_YESNO(ast_json_is_true(obj)));
}

static int get_admin_header(struct ast_str **extra_text, struct stasis_message *message)
{
	return get_bool_header(extra_text, message, "admin", "Admin");
}

static int get_muted_header(struct ast_str **extra_text, struct stasis_message *message)
{
	return get_bool_header(extra_text, message, "muted", "Muted");
}

static void confbridge_start_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	confbridge_publish_manager_event(message, NULL);
}

static void confbridge_end_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	confbridge_publish_manager_event(message, NULL);
}

static void confbridge_leave_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_str *extra_text = NULL;

	if (!get_admin_header(&extra_text, message)) {
		confbridge_publish_manager_event(message, extra_text);
	}
	ast_free(extra_text);
}

static void confbridge_join_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_str *extra_text = NULL;

	if (!get_admin_header(&extra_text, message)
		&& !get_muted_header(&extra_text, message)) {
		confbridge_publish_manager_event(message, extra_text);
	}
	ast_free(extra_text);
}

static void confbridge_atxfer_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_attended_transfer_message *msg = stasis_message_data(message);

	if (msg->result != AST_BRIDGE_TRANSFER_SUCCESS) {
		return;
	}

	/*
	 * This callback will get called for ALL attended transfers
	 * so we need to make sure this transfer belongs to
	 * a conference bridge before trying to handle it.
	 */
	if (msg->dest_type == AST_ATTENDED_TRANSFER_DEST_APP
		&& strcmp(msg->dest.app, "ConfBridge") == 0) {
		confbridge_handle_atxfer(msg);
	}
}

static void confbridge_start_record_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	confbridge_publish_manager_event(message, NULL);
}

static void confbridge_stop_record_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	confbridge_publish_manager_event(message, NULL);
}

static void confbridge_mute_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_str *extra_text = NULL;

	if (!get_admin_header(&extra_text, message)) {
		confbridge_publish_manager_event(message, extra_text);
	}
	ast_free(extra_text);
}

static void confbridge_unmute_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_str *extra_text = NULL;

	if (!get_admin_header(&extra_text, message)) {
		confbridge_publish_manager_event(message, extra_text);
	}
	ast_free(extra_text);
}

static void confbridge_talking_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, extra_text, NULL, ast_free);
	const struct ast_bridge_blob *blob = stasis_message_data(message);
	const char *talking_status = ast_json_string_get(ast_json_object_get(blob->blob, "talking_status"));
	if (!talking_status) {
		return;
	}

	ast_str_append_event_header(&extra_text, "TalkingStatus", talking_status);
	if (!extra_text) {
		return;
	}

	if (!get_admin_header(&extra_text, message)) {
		confbridge_publish_manager_event(message, extra_text);
	}
}

void manager_confbridge_shutdown(void) {
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_start_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_end_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_join_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_leave_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_start_record_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_stop_record_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_mute_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_unmute_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_talking_type);
	STASIS_MESSAGE_TYPE_CLEANUP(confbridge_welcome_type);

	if (bridge_state_router) {
		stasis_message_router_unsubscribe(bridge_state_router);
		bridge_state_router = NULL;
	}

	if (channel_state_router) {
		stasis_message_router_unsubscribe(channel_state_router);
		channel_state_router = NULL;
	}
}

int manager_confbridge_init(void)
{
	STASIS_MESSAGE_TYPE_INIT(confbridge_start_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_end_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_join_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_leave_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_start_record_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_stop_record_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_mute_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_unmute_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_talking_type);
	STASIS_MESSAGE_TYPE_INIT(confbridge_welcome_type);

	bridge_state_router = stasis_message_router_create(
		ast_bridge_topic_all_cached());

	if (!bridge_state_router) {
		return -1;
	}

	if (stasis_message_router_add(bridge_state_router,
			confbridge_start_type(),
			confbridge_start_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_end_type(),
			confbridge_end_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_join_type(),
			confbridge_join_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			ast_attended_transfer_type(),
			confbridge_atxfer_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_leave_type(),
			confbridge_leave_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_start_record_type(),
			confbridge_start_record_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_stop_record_type(),
			confbridge_stop_record_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_mute_type(),
			confbridge_mute_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_unmute_type(),
			confbridge_unmute_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(bridge_state_router,
			confbridge_talking_type(),
			confbridge_talking_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}

	channel_state_router = stasis_message_router_create(
		ast_channel_topic_all_cached());

	if (!channel_state_router) {
		manager_confbridge_shutdown();
		return -1;
	}

	if (stasis_message_router_add(channel_state_router,
			confbridge_start_type(),
			confbridge_start_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_end_type(),
			confbridge_end_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_join_type(),
			confbridge_join_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_leave_type(),
			confbridge_leave_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_start_record_type(),
			confbridge_start_record_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_stop_record_type(),
			confbridge_stop_record_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_mute_type(),
			confbridge_mute_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_unmute_type(),
			confbridge_unmute_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
	if (stasis_message_router_add(channel_state_router,
			confbridge_talking_type(),
			confbridge_talking_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}

	/* FYI: confbridge_welcome_type is never routed */

	return 0;
}
