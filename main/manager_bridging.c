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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis_bridging.h"
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
				<parameter name="BridgeUniqueid">
				</parameter>
				<parameter name="BridgeType">
					<para>The type of bridge</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="BridgeDestroy">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a bridge is destroyed.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="BridgeEnter">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel enters a bridge.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<parameter name="Uniqueid">
					<para>The uniqueid of the channel entering the bridge</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="BridgeLeave">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a bridge.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<parameter name="Uniqueid">
					<para>The uniqueid of the channel leaving the bridge</para>
				</parameter>
			</syntax>
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
	</manager>
	<manager name="BridgeInfo" language="en_US">
		<synopsis>
			Get information about a bridge.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="BridgeUniqueid" required="true">
				<para>The unique ID of the bridge about which to retreive information.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns detailed information about a bridge and the channels in it.</para>
		</description>
	</manager>
 ***/

struct ast_str *ast_manager_build_bridge_state_string(
	const struct ast_bridge_snapshot *snapshot,
	const char *suffix)
{
	struct ast_str *out = ast_str_create(128);
	int res = 0;
	if (!out) {
		return NULL;
	}
	res = ast_str_set(&out, 0,
		"BridgeUniqueid%s: %s\r\n"
		"BridgeType%s: %s\r\n",
		suffix, snapshot->uniqueid,
		suffix, snapshot->technology);

	if (!res) {
		return NULL;
	}

	return out;
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
	bridge_destroy,
};

static void bridge_snapshot_update(void *data, struct stasis_subscription *sub,
				    struct stasis_topic *topic,
				    struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, bridge_event_string, NULL, ast_free);
	struct stasis_cache_update *update;
	struct ast_bridge_snapshot *old_snapshot;
	struct ast_bridge_snapshot *new_snapshot;
	size_t i;

	update = stasis_message_data(message);

	if (ast_bridge_snapshot_type() != update->type) {
		return;
	}

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
					new_snapshot ? new_snapshot : old_snapshot, "");
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
				    struct stasis_topic *topic,
				    struct stasis_message *message)
{
	struct ast_bridge_merge_message *merge_msg = stasis_message_data(message);
	RAII_VAR(struct ast_str *, to_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, from_text, NULL, ast_free);

	ast_assert(merge_msg->to != NULL);
	ast_assert(merge_msg->from != NULL);

	to_text = ast_manager_build_bridge_state_string(merge_msg->to, "");
	from_text = ast_manager_build_bridge_state_string(merge_msg->from, "From");

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when two bridges are merged.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<parameter name="BridgeUniqueidFrom">
					<para>The uniqueid of the bridge being dissolved in the merge</para>
				</parameter>
				<parameter name="BridgeTypeFrom">
					<para>The type of bridge that is being dissolved in the merge</para>
				</parameter>
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
				    struct stasis_topic *topic,
				    struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	RAII_VAR(struct ast_str *, bridge_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);

	bridge_text = ast_manager_build_bridge_state_string(blob->bridge, "");
	channel_text = ast_manager_build_channel_state_string(blob->channel);

	manager_event(EVENT_FLAG_CALL, "BridgeEnter",
		"%s"
		"%s",
		ast_str_buffer(bridge_text),
		ast_str_buffer(channel_text));
}

static void channel_leave_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_topic *topic,
				    struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	RAII_VAR(struct ast_str *, bridge_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);

	bridge_text = ast_manager_build_bridge_state_string(blob->bridge, "");
	channel_text = ast_manager_build_channel_state_string(blob->channel);

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

static int send_bridge_list_item_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_bridge_snapshot *snapshot = stasis_message_data(obj);
	struct mansession *s = arg;
	char *id_text = data;
	RAII_VAR(struct ast_str *, bridge_info, ast_manager_build_bridge_state_string(snapshot, ""), ast_free);

	astman_append(s,
		"Event: BridgeListItem\r\n"
		"%s"
		"%s"
		"\r\n",
		ast_str_buffer(bridge_info),
		id_text);
	return 0;
}

static int manager_bridges_list(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *type_filter = astman_get_header(m, "BridgeType");
	RAII_VAR(struct ast_str *, id_text, ast_str_create(128), ast_free);
	RAII_VAR(struct ao2_container *, bridges, NULL, ao2_cleanup);

	if (!id_text) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	if (!ast_strlen_zero(id)) {
		ast_str_set(&id_text, 0, "ActionID: %s\r\n", id);
	}

	bridges = stasis_cache_dump(ast_bridge_topic_all_cached(), ast_bridge_snapshot_type());
	if (!bridges) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	astman_send_ack(s, m, "Bridge listing will follow");

	if (!ast_strlen_zero(type_filter)) {
		char *type_filter_dup = ast_strdupa(type_filter);
		ao2_callback(bridges, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK, filter_bridge_type_cb, type_filter_dup);
	}

	ao2_callback_data(bridges, OBJ_NODATA, send_bridge_list_item_cb, s, ast_str_buffer(id_text));

	astman_append(s,
		"Event: BridgeListComplete\r\n"
		"%s"
		"\r\n",
		ast_str_buffer(id_text));

	return 0;
}

static int send_bridge_info_item_cb(void *obj, void *arg, void *data, int flags)
{
	char *uniqueid = obj;
	struct mansession *s = arg;
	char *id_text = data;

	astman_append(s,
		"Event: BridgeInfoChannel\r\n"
		"Uniqueid: %s\r\n"
		"%s"
		"\r\n",
		uniqueid,
		id_text);
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

	if (!id_text) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	if (ast_strlen_zero(bridge_uniqueid)) {
		astman_send_error(s, m, "BridgeUniqueid must be provided");
		return -1;
	}

	if (!ast_strlen_zero(id)) {
		ast_str_set(&id_text, 0, "ActionID: %s\r\n", id);
	}

	msg = stasis_cache_get(ast_bridge_topic_all_cached(), ast_bridge_snapshot_type(), bridge_uniqueid);
	if (!msg) {
		astman_send_error(s, m, "Specified BridgeUniqueid not found");
		return -1;
	}

	astman_send_ack(s, m, "Bridge channel listing will follow");

	snapshot = stasis_message_data(msg);
	bridge_info = ast_manager_build_bridge_state_string(snapshot, "");

	ao2_callback_data(snapshot->channels, OBJ_NODATA, send_bridge_info_item_cb, s, ast_str_buffer(id_text));

	astman_append(s,
		"Event: BridgeInfoComplete\r\n"
		"%s"
		"%s"
		"\r\n",
		ast_str_buffer(bridge_info),
		ast_str_buffer(id_text));

	return 0;
}

static void manager_bridging_shutdown(void)
{
	stasis_message_router_unsubscribe(bridge_state_router);
	bridge_state_router = NULL;
	ast_manager_unregister("BridgeList");
	ast_manager_unregister("BridgeInfo");
}

int manager_bridging_init(void)
{
	int ret = 0;

	if (bridge_state_router) {
		/* Already initialized */
		return 0;
	}

	ast_register_atexit(manager_bridging_shutdown);

	bridge_state_router = stasis_message_router_create(
		stasis_caching_get_topic(ast_bridge_topic_all_cached()));

	if (!bridge_state_router) {
		return -1;
	}

	ret |= stasis_message_router_add(bridge_state_router,
					 stasis_cache_update_type(),
					 bridge_snapshot_update,
					 NULL);

	ret |= stasis_message_router_add(bridge_state_router,
					 ast_bridge_merge_message_type(),
					 bridge_merge_cb,
					 NULL);

	ret |= stasis_message_router_add(bridge_state_router,
					 ast_channel_entered_bridge_type(),
					 channel_enter_cb,
					 NULL);

	ret |= stasis_message_router_add(bridge_state_router,
					 ast_channel_left_bridge_type(),
					 channel_leave_cb,
					 NULL);

	ret |= ast_manager_register_xml_core("BridgeList", 0, manager_bridges_list);
	ret |= ast_manager_register_xml_core("BridgeInfo", 0, manager_bridge_info);

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		manager_bridging_shutdown();
		return -1;
	}

	return 0;
}
