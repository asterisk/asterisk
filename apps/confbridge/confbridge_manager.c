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
		channel_text = ast_manager_build_channel_state_string(blob->channel);
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
