/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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
 * \brief The Asterisk Management Interface - AMI (bridge event handling)
 *
 * \author Kinsey Moore <kmoore@digium.com>
 */

#include "asterisk.h"

#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/manager.h"
#include "asterisk/stasis_message_router.h"

/*! \brief Message router for cached bridge state snapshot updates */
static struct stasis_message_router *bridge_state_router;

/*** DOCUMENTATION
	<managerEvent language="en_US" name="BridgeCreate">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a bridge is created.</synopsis>
			<syntax>
				<bridge_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">BridgeDestroy</ref>
				<ref type="managerEvent">BridgeEnter</ref>
				<ref type="managerEvent">BridgeLeave</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="BridgeDestroy">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a bridge is destroyed.</synopsis>
			<syntax>
				<bridge_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">BridgeCreate</ref>
				<ref type="managerEvent">BridgeEnter</ref>
				<ref type="managerEvent">BridgeLeave</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="BridgeEnter">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel enters a bridge.</synopsis>
			<syntax>
				<bridge_snapshot/>
				<channel_snapshot/>
				<parameter name="SwapUniqueid">
					<para>The uniqueid of the channel being swapped out of the bridge</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">BridgeCreate</ref>
				<ref type="managerEvent">BridgeDestroy</ref>
				<ref type="managerEvent">BridgeLeave</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="BridgeLeave">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a bridge.</synopsis>
			<syntax>
				<bridge_snapshot/>
				<channel_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">BridgeCreate</ref>
				<ref type="managerEvent">BridgeDestroy</ref>
				<ref type="managerEvent">BridgeEnter</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="BridgeVideoSourceUpdate">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when the channel that is the source of video in a bridge changes.</synopsis>
			<syntax>
				<bridge_snapshot/>
				<parameter name="BridgePreviousVideoSource">
					<para>The unique ID of the channel that was the video source.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">BridgeCreate</ref>
				<ref type="managerEvent">BridgeDestroy</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<manager name="BridgeList" language="en_US">
		<synopsis>
			Get a list of bridges in the system.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="BridgeType">
				<para>Optional type for filtering the resulting list of bridges.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns a list of bridges, optionally filtering on a bridge type.</para>
		</description>
		<see-also>
			<ref type="manager">Bridge</ref>
			<ref type="manager">BridgeDestroy</ref>
			<ref type="manager">BridgeInfo</ref>
			<ref type="manager">BridgeKick</ref>
		</see-also>
	</manager>
	<manager name="BridgeInfo" language="en_US">
		<synopsis>
			Get information about a bridge.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="BridgeUniqueid" required="true">
				<para>The unique ID of the bridge about which to retrieve information.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns detailed information about a bridge and the channels in it.</para>
		</description>
		<see-also>
			<ref type="manager">Bridge</ref>
			<ref type="manager">BridgeDestroy</ref>
			<ref type="manager">BridgeKick</ref>
			<ref type="manager">BridgeList</ref>
		</see-also>
		<responses>
			<list-elements>
				<managerEvent language="en_US" name="BridgeInfoChannel">
					<managerEventInstance class="EVENT_FLAG_COMMAND">
						<synopsis>Information about a channel in a bridge.</synopsis>
						<syntax>
							<channel_snapshot/>
						</syntax>
					</managerEventInstance>
				</managerEvent>
			</list-elements>
			<managerEvent language="en_US" name="BridgeInfoComplete">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<synopsis>Information about a bridge.</synopsis>
					<syntax>
						<bridge_snapshot/>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
	<manager name="BridgeDestroy" language="en_US">
		<synopsis>
			Destroy a bridge.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="BridgeUniqueid" required="true">
				<para>The unique ID of the bridge to destroy.</para>
			</parameter>
		</syntax>
		<description>
			<para>Deletes the bridge, causing channels to continue or hang up.</para>
		</description>
		<see-also>
			<ref type="manager">Bridge</ref>
			<ref type="manager">BridgeInfo</ref>
			<ref type="manager">BridgeKick</ref>
			<ref type="manager">BridgeList</ref>
			<ref type="managerEvent">BridgeDestroy</ref>
		</see-also>
	</manager>
	<manager name="BridgeKick" language="en_US">
		<synopsis>
			Kick a channel from a bridge.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="BridgeUniqueid" required="false">
				<para>The unique ID of the bridge containing the channel to
				destroy.  This parameter can be omitted, or supplied to insure
				that the channel is not removed from the wrong bridge.</para>
			</parameter>
			<parameter name="Channel" required="true">
				<para>The channel to kick out of a bridge.</para>
			</parameter>
		</syntax>
		<description>
			<para>The channel is removed from the bridge.</para>
		</description>
		<see-also>
			<ref type="manager">Bridge</ref>
			<ref type="manager">BridgeDestroy</ref>
			<ref type="manager">BridgeInfo</ref>
			<ref type="manager">BridgeList</ref>
			<ref type="managerEvent">BridgeLeave</ref>
		</see-also>
	</manager>
 ***/

/*! \brief The \ref stasis subscription returned by the forwarding of the channel topic
 * to the manager topic
 */
static struct stasis_forward *topic_forwarder;

struct ast_str *ast_manager_build_bridge_state_string_prefix(
	const struct ast_bridge_snapshot *snapshot,
	const char *prefix)
{
	struct ast_str *out = ast_str_create(128);
	int res;

	if (!out) {
		return NULL;
	}

	res = ast_str_set(&out, 0,
		"%sBridgeUniqueid: %s\r\n"
		"%sBridgeType: %s\r\n"
		"%sBridgeTechnology: %s\r\n"
		"%sBridgeCreator: %s\r\n"
		"%sBridgeName: %s\r\n"
		"%sBridgeNumChannels: %u\r\n"
		"%sBridgeVideoSourceMode: %s\r\n",
		prefix, snapshot->uniqueid,
		prefix, snapshot->subclass,
		prefix, snapshot->technology,
		prefix, ast_strlen_zero(snapshot->creator) ? "<unknown>": snapshot->creator,
		prefix, ast_strlen_zero(snapshot->name) ? "<unknown>": snapshot->name,
		prefix, snapshot->num_channels,
		prefix, ast_bridge_video_mode_to_string(snapshot->video_mode));
	if (!res) {
		ast_free(out);
		return NULL;
	}

	if (snapshot->video_mode != AST_BRIDGE_VIDEO_MODE_NONE
		&& !ast_strlen_zero(snapshot->video_source_id)) {
		res = ast_str_append(&out, 0, "%sBridgeVideoSource: %s\r\n",
			prefix, snapshot->video_source_id);
		if (!res) {
			ast_free(out);
			return NULL;
		}
	}

	return out;
}

struct ast_str *ast_manager_build_bridge_state_string(
	const struct ast_bridge_snapshot *snapshot)
{
	return ast_manager_build_bridge_state_string_prefix(snapshot, "");
}

/*! \brief Typedef for callbacks that get called on channel snapshot updates */
typedef struct ast_manager_event_blob *(*bridge_snapshot_monitor)(
	struct ast_bridge_snapshot *old_snapshot,
	struct ast_bridge_snapshot *new_snapshot);

/*! \brief Handle bridge creation */
static struct ast_manager_event_blob *bridge_create(
	struct ast_bridge_snapshot *old_snapshot,
	struct ast_bridge_snapshot *new_snapshot)
{
	if (!new_snapshot || old_snapshot) {
		return NULL;
	}

	return ast_manager_event_blob_create(
		EVENT_FLAG_CALL, "BridgeCreate", NO_EXTRA_FIELDS);
}

/* \brief Handle video source updates */
static struct ast_manager_event_blob *bridge_video_update(
	struct ast_bridge_snapshot *old_snapshot,
	struct ast_bridge_snapshot *new_snapshot)
{
	if (!new_snapshot || !old_snapshot) {
		return NULL;
	}

	if (!strcmp(old_snapshot->video_source_id, new_snapshot->video_source_id)) {
		return NULL;
	}

	return ast_manager_event_blob_create(
		EVENT_FLAG_CALL, "BridgeVideoSourceUpdate",
		"BridgePreviousVideoSource: %s\r\n",
		old_snapshot->video_source_id);
}

/*! \brief Handle bridge destruction */
static struct ast_manager_event_blob *bridge_destroy(
	struct ast_bridge_snapshot *old_snapshot,
	struct ast_bridge_snapshot *new_snapshot)
{
	if (new_snapshot || !old_snapshot) {
		return NULL;
	}

	return ast_manager_event_blob_create(
		EVENT_FLAG_CALL, "BridgeDestroy", NO_EXTRA_FIELDS);
}

bridge_snapshot_monitor bridge_monitors[] = {
	bridge_create,
	bridge_video_update,
	bridge_destroy,
};

static void bridge_snapshot_update(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, bridge_event_string, NULL, ast_free);
	struct stasis_cache_update *update;
	struct ast_bridge_snapshot *old_snapshot;
	struct ast_bridge_snapshot *new_snapshot;
	size_t i;

	update = stasis_message_data(message);

	ast_assert(ast_bridge_snapshot_type() == update->type);

	old_snapshot = stasis_message_data(update->old_snapshot);
	new_snapshot = stasis_message_data(update->new_snapshot);

	for (i = 0; i < ARRAY_LEN(bridge_monitors); ++i) {
		RAII_VAR(struct ast_manager_event_blob *, event, NULL, ao2_cleanup);

		event = bridge_monitors[i](old_snapshot, new_snapshot);
		if (!event) {
			continue;
		}

		/* If we haven't already, build the channel event string */
		if (!bridge_event_string) {
			bridge_event_string =
				ast_manager_build_bridge_state_string(
					new_snapshot ? new_snapshot : old_snapshot);
			if (!bridge_event_string) {
				return;
			}
		}

		manager_event(event->event_flags, event->manager_event, "%s%s",
			ast_str_buffer(bridge_event_string),
			event->extra_fields);
	}
}

static void bridge_merge_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	struct ast_bridge_merge_message *merge_msg = stasis_message_data(message);
	RAII_VAR(struct ast_str *, to_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, from_text, NULL, ast_free);

	ast_assert(merge_msg->to != NULL);
	ast_assert(merge_msg->from != NULL);

	to_text = ast_manager_build_bridge_state_string_prefix(merge_msg->to, "To");
	from_text = ast_manager_build_bridge_state_string_prefix(merge_msg->from, "From");
	if (!to_text || !from_text) {
		return;
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when two bridges are merged.</synopsis>
			<syntax>
				<bridge_snapshot prefix="To"/>
				<bridge_snapshot prefix="From"/>
			</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "BridgeMerge",
		"%s"
		"%s",
		ast_str_buffer(to_text),
		ast_str_buffer(from_text));
}

static void channel_enter_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	static const char *swap_name = "SwapUniqueid: ";
	struct ast_bridge_blob *blob = stasis_message_data(message);
	RAII_VAR(struct ast_str *, bridge_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);
	const char *swap_id;

	bridge_text = ast_manager_build_bridge_state_string(blob->bridge);
	channel_text = ast_manager_build_channel_state_string(blob->channel);
	if (!bridge_text || !channel_text) {
		return;
	}

	swap_id = ast_json_string_get(ast_json_object_get(blob->blob, "swap"));

	manager_event(EVENT_FLAG_CALL, "BridgeEnter",
		"%s"
		"%s"
		"%s%s%s",
		ast_str_buffer(bridge_text),
		ast_str_buffer(channel_text),
		swap_id ? swap_name : "",
		S_OR(swap_id, ""),
		swap_id ? "\r\n" : "");
}

static void channel_leave_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	RAII_VAR(struct ast_str *, bridge_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);

	bridge_text = ast_manager_build_bridge_state_string(blob->bridge);
	channel_text = ast_manager_build_channel_state_string(blob->channel);
	if (!bridge_text || !channel_text) {
		return;
	}

	manager_event(EVENT_FLAG_CALL, "BridgeLeave",
		"%s"
		"%s",
		ast_str_buffer(bridge_text),
		ast_str_buffer(channel_text));
}

static int filter_bridge_type_cb(void *obj, void *arg, int flags)
{
	char *bridge_type = arg;
	struct ast_bridge_snapshot *snapshot = stasis_message_data(obj);
	/* unlink all the snapshots that do not match the bridge type */
	return strcmp(bridge_type, snapshot->technology) ? CMP_MATCH : 0;
}

struct bridge_list_data {
	const char *id_text;
	int count;
};

static int send_bridge_list_item_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_bridge_snapshot *snapshot = stasis_message_data(obj);
	struct mansession *s = arg;
	struct bridge_list_data *list_data = data;
	RAII_VAR(struct ast_str *, bridge_info, ast_manager_build_bridge_state_string(snapshot), ast_free);

	if (!bridge_info) {
		return 0;
	}

	astman_append(s,
		"Event: BridgeListItem\r\n"
		"%s"
		"%s"
		"\r\n",
		list_data->id_text,
		ast_str_buffer(bridge_info));
	++list_data->count;
	return 0;
}

static int manager_bridges_list(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *type_filter = astman_get_header(m, "BridgeType");
	RAII_VAR(struct ast_str *, id_text, ast_str_create(128), ast_free);
	RAII_VAR(struct ao2_container *, bridges, NULL, ao2_cleanup);
	struct bridge_list_data list_data;

	if (!id_text) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	if (!ast_strlen_zero(id)) {
		ast_str_set(&id_text, 0, "ActionID: %s\r\n", id);
	}

	bridges = stasis_cache_dump(ast_bridge_cache(), ast_bridge_snapshot_type());
	if (!bridges) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	astman_send_listack(s, m, "Bridge listing will follow", "start");

	if (!ast_strlen_zero(type_filter)) {
		char *type_filter_dup = ast_strdupa(type_filter);

		ao2_callback(bridges, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK,
			filter_bridge_type_cb, type_filter_dup);
	}

	list_data.id_text = ast_str_buffer(id_text);
	list_data.count = 0;
	ao2_callback_data(bridges, OBJ_NODATA, send_bridge_list_item_cb, s, &list_data);

	astman_send_list_complete_start(s, m, "BridgeListComplete", list_data.count);
	astman_send_list_complete_end(s);

	return 0;
}

static int send_bridge_info_item_cb(void *obj, void *arg, void *data, int flags)
{
	char *uniqueid = obj;
	struct mansession *s = arg;
	struct bridge_list_data *list_data = data;
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct ast_channel_snapshot *snapshot;
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);

	msg = stasis_cache_get(ast_channel_cache(),
		ast_channel_snapshot_type(), uniqueid);
	if (!msg) {
		return 0;
	}

	snapshot = stasis_message_data(msg);
	if (snapshot->tech_properties & AST_CHAN_TP_INTERNAL) {
		return 0;
	}

	channel_text = ast_manager_build_channel_state_string(snapshot);
	if (!channel_text) {
		return 0;
	}

	astman_append(s,
		"Event: BridgeInfoChannel\r\n"
		"%s"
		"%s"
		"\r\n",
		list_data->id_text,
		ast_str_buffer(channel_text));
	++list_data->count;
	return 0;
}

static int manager_bridge_info(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *bridge_uniqueid = astman_get_header(m, "BridgeUniqueid");
	RAII_VAR(struct ast_str *, id_text, ast_str_create(128), ast_free);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, bridge_info, NULL, ast_free);
	struct ast_bridge_snapshot *snapshot;
	struct bridge_list_data list_data;

	if (!id_text) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	if (ast_strlen_zero(bridge_uniqueid)) {
		astman_send_error(s, m, "BridgeUniqueid must be provided");
		return 0;
	}

	if (!ast_strlen_zero(id)) {
		ast_str_set(&id_text, 0, "ActionID: %s\r\n", id);
	}

	msg = stasis_cache_get(ast_bridge_cache(), ast_bridge_snapshot_type(), bridge_uniqueid);
	if (!msg) {
		astman_send_error(s, m, "Specified BridgeUniqueid not found");
		return 0;
	}

	snapshot = stasis_message_data(msg);
	bridge_info = ast_manager_build_bridge_state_string(snapshot);
	if (!bridge_info) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	astman_send_listack(s, m, "Bridge channel listing will follow", "start");

	list_data.id_text = ast_str_buffer(id_text);
	list_data.count = 0;
	ao2_callback_data(snapshot->channels, OBJ_NODATA, send_bridge_info_item_cb, s, &list_data);

	astman_send_list_complete_start(s, m, "BridgeInfoComplete", list_data.count);
	if (!ast_strlen_zero(ast_str_buffer(bridge_info))) {
		astman_append(s, "%s", ast_str_buffer(bridge_info));
	}
	astman_send_list_complete_end(s);

	return 0;
}

static int manager_bridge_destroy(struct mansession *s, const struct message *m)
{
	const char *bridge_uniqueid = astman_get_header(m, "BridgeUniqueid");
	struct ast_bridge *bridge;

	if (ast_strlen_zero(bridge_uniqueid)) {
		astman_send_error(s, m, "BridgeUniqueid must be provided");
		return 0;
	}

	bridge = ast_bridge_find_by_id(bridge_uniqueid);
	if (!bridge) {
		astman_send_error(s, m, "Specified BridgeUniqueid not found");
		return 0;
	}
	ast_bridge_destroy(bridge, 0);

	astman_send_ack(s, m, "Bridge has been destroyed");

	return 0;
}

static int manager_bridge_kick(struct mansession *s, const struct message *m)
{
	const char *bridge_uniqueid = astman_get_header(m, "BridgeUniqueid");
	const char *channel_name = astman_get_header(m, "Channel");
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, channel, NULL, ao2_cleanup);

	if (ast_strlen_zero(channel_name)) {
		astman_send_error(s, m, "Channel must be provided");
		return 0;
	}

	channel = ast_channel_get_by_name(channel_name);
	if (!channel) {
		astman_send_error(s, m, "Channel does not exist");
		return 0;
	}

	if (ast_strlen_zero(bridge_uniqueid)) {
		/* get the bridge from the channel */
		ast_channel_lock(channel);
		bridge = ast_channel_get_bridge(channel);
		ast_channel_unlock(channel);
		if (!bridge) {
			astman_send_error(s, m, "Channel is not in a bridge");
			return 0;
		}
	} else {
		bridge = ast_bridge_find_by_id(bridge_uniqueid);
		if (!bridge || ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_INVISIBLE)) {
			astman_send_error(s, m, "Bridge not found");
			return 0;
		}
	}

	if (ast_bridge_kick(bridge, channel)) {
		astman_send_error(s, m, "Channel kick from bridge failed");
		return 0;
	}

	astman_send_ack(s, m, "Channel has been kicked");
	return 0;
}

static void manager_bridging_cleanup(void)
{
	stasis_forward_cancel(topic_forwarder);
	topic_forwarder = NULL;

	ast_manager_unregister("BridgeList");
	ast_manager_unregister("BridgeInfo");
	ast_manager_unregister("BridgeDestroy");
	ast_manager_unregister("BridgeKick");
}

int manager_bridging_init(void)
{
	int ret = 0;
	struct stasis_topic *manager_topic;
	struct stasis_topic *bridge_topic;

	if (bridge_state_router) {
		/* Already initialized */
		return 0;
	}

	ast_register_cleanup(manager_bridging_cleanup);

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return -1;
	}

	bridge_topic = ast_bridge_topic_all_cached();
	if (!bridge_topic) {
		return -1;
	}

	topic_forwarder = stasis_forward_all(bridge_topic, manager_topic);
	if (!topic_forwarder) {
		return -1;
	}

	bridge_state_router = ast_manager_get_message_router();
	if (!bridge_state_router) {
		return -1;
	}

	ret |= stasis_message_router_add_cache_update(bridge_state_router,
		ast_bridge_snapshot_type(), bridge_snapshot_update, NULL);

	ret |= stasis_message_router_add(bridge_state_router,
		ast_bridge_merge_message_type(), bridge_merge_cb, NULL);

	ret |= stasis_message_router_add(bridge_state_router,
		ast_channel_entered_bridge_type(), channel_enter_cb, NULL);

	ret |= stasis_message_router_add(bridge_state_router,
		ast_channel_left_bridge_type(), channel_leave_cb, NULL);

	ret |= ast_manager_register_xml_core("BridgeList", 0, manager_bridges_list);
	ret |= ast_manager_register_xml_core("BridgeInfo", 0, manager_bridge_info);
	ret |= ast_manager_register_xml_core("BridgeDestroy", 0, manager_bridge_destroy);
	ret |= ast_manager_register_xml_core("BridgeKick", 0, manager_bridge_kick);

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		manager_bridging_cleanup();
		return -1;
	}

	return 0;
}
