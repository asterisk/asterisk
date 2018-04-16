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
 * \brief Stasis Messages and Data Types for Bridge Objects
 *
 * \author Kinsey Moore <kmoore@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_cache_pattern.h"
#include "asterisk/channel.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_technology.h"

/* The container of channel snapshots in a bridge snapshot should always be
   equivalent to a linked list; otherwise things (like CDRs) that depend on some
   consistency in the ordering of channels in a bridge will break. */
#define SNAPSHOT_CHANNELS_BUCKETS 1

/*** DOCUMENTATION
	<managerEvent language="en_US" name="BlindTransfer">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a blind transfer is complete.</synopsis>
			<syntax>
				<parameter name="Result">
					<para>Indicates if the transfer was successful or if it failed.</para>
					<enumlist>
						<enum name="Fail"><para>An internal error occurred.</para></enum>
						<enum name="Invalid"><para>Invalid configuration for transfer (e.g. Not bridged)</para></enum>
						<enum name="Not Permitted"><para>Bridge does not permit transfers</para></enum>
						<enum name="Success"><para>Transfer completed successfully</para></enum>
					</enumlist>
					<note><para>A result of <literal>Success</literal> does not necessarily mean that a target was succesfully
					contacted. It means that a party was succesfully placed into the dialplan at the expected location.</para></note>
				</parameter>
				<channel_snapshot prefix="Transferer"/>
				<channel_snapshot prefix="Transferee"/>
				<bridge_snapshot/>
				<parameter name="IsExternal">
					<para>Indicates if the transfer was performed outside of Asterisk. For instance,
					a channel protocol native transfer is external. A DTMF transfer is internal.</para>
						<enumlist>
							<enum name="Yes" />
							<enum name="No" />
						</enumlist>
				</parameter>
				<parameter name="Context">
					<para>Destination context for the blind transfer.</para>
				</parameter>
				<parameter name="Extension">
					<para>Destination extension for the blind transfer.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="manager">BlindTransfer</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AttendedTransfer">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when an attended transfer is complete.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(docs/managerEvent[@name='BlindTransfer']/managerEventInstance/syntax/parameter[@name='Result'])" />
				<channel_snapshot prefix="OrigTransferer"/>
				<bridge_snapshot prefix="Orig"/>
				<channel_snapshot prefix="SecondTransferer"/>
				<bridge_snapshot prefix="Second"/>
				<parameter name="DestType">
					<para>Indicates the method by which the attended transfer completed.</para>
					<enumlist>
						<enum name="Bridge"><para>The transfer was accomplished by merging two bridges into one.</para></enum>
						<enum name="App"><para>The transfer was accomplished by having a channel or bridge run a dialplan application.</para></enum>
						<enum name="Link"><para>The transfer was accomplished by linking two bridges together using a local channel pair.</para></enum>
						<enum name="Threeway"><para>The transfer was accomplished by placing all parties into a threeway call.</para></enum>
						<enum name="Fail"><para>The transfer failed.</para></enum>
					</enumlist>
				</parameter>
				<parameter name="DestBridgeUniqueid">
					<para>Indicates the surviving bridge when bridges were merged to complete the transfer</para>
					<note><para>This header is only present when <replaceable>DestType</replaceable> is <literal>Bridge</literal> or <literal>Threeway</literal></para></note>
				</parameter>
				<parameter name="DestApp">
					<para>Indicates the application that is running when the transfer completes</para>
					<note><para>This header is only present when <replaceable>DestType</replaceable> is <literal>App</literal></para></note>
				</parameter>
				<channel_snapshot prefix="LocalOne"/>
				<channel_snapshot prefix="LocalTwo"/>
				<parameter name="DestTransfererChannel">
					<para>The name of the surviving transferer channel when a transfer results in a threeway call</para>
					<note><para>This header is only present when <replaceable>DestType</replaceable> is <literal>Threeway</literal></para></note>
				</parameter>
				<channel_snapshot prefix="Transferee" />
			</syntax>
			<description>
				<para>The headers in this event attempt to describe all the major details of the attended transfer. The two transferer channels
				and the two bridges are determined based on their chronological establishment. So consider that Alice calls Bob, and then Alice
				transfers the call to Voicemail. The transferer and bridge headers would be arranged as follows:</para>
				<para>	<replaceable>OrigTransfererChannel</replaceable>: Alice's channel in the bridge with Bob.</para>
				<para>	<replaceable>OrigBridgeUniqueid</replaceable>: The bridge between Alice and Bob.</para>
				<para>	<replaceable>SecondTransfererChannel</replaceable>: Alice's channel that called Voicemail.</para>
				<para>	<replaceable>SecondBridgeUniqueid</replaceable>: Not present, since a call to Voicemail has no bridge.</para>
				<para>Now consider if the order were reversed; instead of having Alice call Bob and transfer him to Voicemail, Alice instead
				calls her Voicemail and transfers that to Bob. The transferer and bridge headers would be arranged as follows:</para>
				<para>	<replaceable>OrigTransfererChannel</replaceable>: Alice's channel that called Voicemail.</para>
				<para>	<replaceable>OrigBridgeUniqueid</replaceable>: Not present, since a call to Voicemail has no bridge.</para>
				<para>	<replaceable>SecondTransfererChannel</replaceable>: Alice's channel in the bridge with Bob.</para>
				<para>	<replaceable>SecondBridgeUniqueid</replaceable>: The bridge between Alice and Bob.</para>
			</description>
			<see-also>
				<ref type="manager">AtxFer</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
 ***/

static struct ast_json *attended_transfer_to_json(struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize);
static struct ast_manager_event_blob *attended_transfer_to_ami(struct stasis_message *message);
static struct ast_json *blind_transfer_to_json(struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize);
static struct ast_manager_event_blob *blind_transfer_to_ami(struct stasis_message *message);
static struct ast_json *ast_channel_entered_bridge_to_json(
	struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize);
static struct ast_json *ast_channel_left_bridge_to_json(
	struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize);
static struct ast_json *ast_bridge_merge_message_to_json(
	struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize);

static struct stasis_cp_all *bridge_cache_all;

/*!
 * @{ \brief Define bridge message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_bridge_snapshot_type);
STASIS_MESSAGE_TYPE_DEFN(ast_bridge_merge_message_type,
	.to_json = ast_bridge_merge_message_to_json);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_entered_bridge_type,
	.to_json = ast_channel_entered_bridge_to_json);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_left_bridge_type,
	.to_json = ast_channel_left_bridge_to_json);
STASIS_MESSAGE_TYPE_DEFN(ast_blind_transfer_type,
	.to_json = blind_transfer_to_json,
	.to_ami = blind_transfer_to_ami);
STASIS_MESSAGE_TYPE_DEFN(ast_attended_transfer_type,
	.to_json = attended_transfer_to_json,
	.to_ami = attended_transfer_to_ami);
/*! @} */

struct stasis_cache *ast_bridge_cache(void)
{
	return stasis_cp_all_cache(bridge_cache_all);
}

struct stasis_topic *ast_bridge_topic_all(void)
{
	return stasis_cp_all_topic(bridge_cache_all);
}

struct stasis_topic *ast_bridge_topic_all_cached(void)
{
	return stasis_cp_all_topic_cached(bridge_cache_all);
}

int bridge_topics_init(struct ast_bridge *bridge)
{
	if (ast_strlen_zero(bridge->uniqueid)) {
		ast_log(LOG_ERROR, "Bridge id initialization required\n");
		return -1;
	}
	bridge->topics = stasis_cp_single_create(bridge_cache_all,
		bridge->uniqueid);
	if (!bridge->topics) {
		return -1;
	}
	return 0;
}

struct stasis_topic *ast_bridge_topic(struct ast_bridge *bridge)
{
	if (!bridge) {
		return ast_bridge_topic_all();
	}

	return stasis_cp_single_topic(bridge->topics);
}

struct stasis_topic *ast_bridge_topic_cached(struct ast_bridge *bridge)
{
	if (!bridge) {
		return ast_bridge_topic_all_cached();
	}

	return stasis_cp_single_topic_cached(bridge->topics);
}

/*! \brief Destructor for bridge snapshots */
static void bridge_snapshot_dtor(void *obj)
{
	struct ast_bridge_snapshot *snapshot = obj;
	ast_string_field_free_memory(snapshot);
	ao2_cleanup(snapshot->channels);
	snapshot->channels = NULL;
}

struct ast_bridge_snapshot *ast_bridge_snapshot_create(struct ast_bridge *bridge)
{
	struct ast_bridge_snapshot *snapshot;
	struct ast_bridge_channel *bridge_channel;

	if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_INVISIBLE)) {
		return NULL;
	}

	snapshot = ao2_alloc_options(sizeof(*snapshot), bridge_snapshot_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	if (ast_string_field_init(snapshot, 128)) {
		ao2_ref(snapshot, -1);

		return NULL;
	}

	snapshot->channels = ast_str_container_alloc(SNAPSHOT_CHANNELS_BUCKETS);
	if (!snapshot->channels) {
		ao2_ref(snapshot, -1);

		return NULL;
	}

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (ast_str_container_add(snapshot->channels,
				ast_channel_uniqueid(bridge_channel->chan))) {
			ao2_ref(snapshot, -1);

			return NULL;
		}
	}

	ast_string_field_set(snapshot, uniqueid, bridge->uniqueid);
	ast_string_field_set(snapshot, technology, bridge->technology->name);
	ast_string_field_set(snapshot, subclass, bridge->v_table->name);
	ast_string_field_set(snapshot, creator, bridge->creator);
	ast_string_field_set(snapshot, name, bridge->name);

	snapshot->feature_flags = bridge->feature_flags;
	snapshot->capabilities = bridge->technology->capabilities;
	snapshot->num_channels = bridge->num_channels;
	snapshot->num_active = bridge->num_active;
	snapshot->video_mode = bridge->softmix.video_mode.mode;
	if (snapshot->video_mode == AST_BRIDGE_VIDEO_MODE_SINGLE_SRC
		&& bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc) {
		ast_string_field_set(snapshot, video_source_id,
			ast_channel_uniqueid(bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc));
	} else if (snapshot->video_mode == AST_BRIDGE_VIDEO_MODE_TALKER_SRC
		&& bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc) {
		ast_string_field_set(snapshot, video_source_id,
			ast_channel_uniqueid(bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc));
	}

	return snapshot;
}

void ast_bridge_publish_state(struct ast_bridge *bridge)
{
	struct ast_bridge_snapshot *snapshot;
	struct stasis_message *msg;

	if (!ast_bridge_snapshot_type()) {
		return;
	}

	ast_assert(bridge != NULL);

	snapshot = ast_bridge_snapshot_create(bridge);
	if (!snapshot) {
		return;
	}

	msg = stasis_message_create(ast_bridge_snapshot_type(), snapshot);
	ao2_ref(snapshot, -1);
	if (!msg) {
		return;
	}

	stasis_publish(ast_bridge_topic(bridge), msg);
	ao2_ref(msg, -1);
}

static void bridge_publish_state_from_blob(struct ast_bridge *bridge,
	struct ast_bridge_blob *obj)
{
	struct stasis_message *msg;

	ast_assert(obj != NULL);

	msg = stasis_message_create(ast_bridge_snapshot_type(), obj->bridge);
	if (!msg) {
		return;
	}

	stasis_publish(ast_bridge_topic(bridge), msg);
	ao2_ref(msg, -1);
}

/*! \brief Destructor for bridge merge messages */
static void bridge_merge_message_dtor(void *obj)
{
	struct ast_bridge_merge_message *msg = obj;

	ao2_cleanup(msg->to);
	msg->to = NULL;
	ao2_cleanup(msg->from);
	msg->from = NULL;
}

/*! \brief Bridge merge message creation helper */
static struct ast_bridge_merge_message *bridge_merge_message_create(struct ast_bridge *to, struct ast_bridge *from)
{
	struct ast_bridge_merge_message *msg;

	msg = ao2_alloc(sizeof(*msg), bridge_merge_message_dtor);
	if (!msg) {
		return NULL;
	}

	msg->to = ast_bridge_snapshot_create(to);
	msg->from = ast_bridge_snapshot_create(from);
	if (!msg->to || !msg->from) {
		ao2_ref(msg, -1);

		return NULL;
	}

	return msg;
}

static struct ast_json *ast_bridge_merge_message_to_json(
	struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_bridge_merge_message *merge = stasis_message_data(msg);
	struct ast_json *json_bridge_to = ast_bridge_snapshot_to_json(merge->to, sanitize);
	struct ast_json *json_bridge_from = ast_bridge_snapshot_to_json(merge->from, sanitize);

	if (!json_bridge_to || !json_bridge_from) {
		ast_json_unref(json_bridge_to);
		ast_json_unref(json_bridge_from);

		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: o, s: o}",
		"type", "BridgeMerged",
		"timestamp", ast_json_timeval(*stasis_message_timestamp(msg), NULL),
		"bridge", json_bridge_to,
		"bridge_from", json_bridge_from);
}

void ast_bridge_publish_merge(struct ast_bridge *to, struct ast_bridge *from)
{
	struct ast_bridge_merge_message *merge_msg;
	struct stasis_message *msg;

	if (!ast_bridge_merge_message_type()) {
		return;
	}

	ast_assert(to != NULL);
	ast_assert(from != NULL);
	ast_assert(ast_test_flag(&to->feature_flags, AST_BRIDGE_FLAG_INVISIBLE) == 0);
	ast_assert(ast_test_flag(&from->feature_flags, AST_BRIDGE_FLAG_INVISIBLE) == 0);

	merge_msg = bridge_merge_message_create(to, from);
	if (!merge_msg) {
		return;
	}

	msg = stasis_message_create(ast_bridge_merge_message_type(), merge_msg);
	ao2_ref(merge_msg, -1);
	if (!msg) {
		return;
	}

	stasis_publish(ast_bridge_topic_all(), msg);
	ao2_ref(msg, -1);
}

static void bridge_blob_dtor(void *obj)
{
	struct ast_bridge_blob *event = obj;
	ao2_cleanup(event->bridge);
	event->bridge = NULL;
	ao2_cleanup(event->channel);
	event->channel = NULL;
	ast_json_unref(event->blob);
	event->blob = NULL;
}

struct stasis_message *ast_bridge_blob_create(
	struct stasis_message_type *message_type,
	struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_json *blob)
{
	struct ast_bridge_blob *obj;
	struct stasis_message *msg;

	if (!message_type) {
		return NULL;
	}

	obj = ao2_alloc(sizeof(*obj), bridge_blob_dtor);
	if (!obj) {
		return NULL;
	}

	if (bridge) {
		obj->bridge = ast_bridge_snapshot_create(bridge);
		if (obj->bridge == NULL) {
			ao2_ref(obj, -1);

			return NULL;
		}
	}

	if (chan) {
		obj->channel = ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan));
		if (obj->channel == NULL) {
			ao2_ref(obj, -1);

			return NULL;
		}
	}

	if (blob) {
		obj->blob = ast_json_ref(blob);
	}

	msg = stasis_message_create(message_type, obj);
	ao2_ref(obj, -1);

	return msg;
}

void ast_bridge_publish_enter(struct ast_bridge *bridge, struct ast_channel *chan,
		struct ast_channel *swap)
{
	struct stasis_message *msg;
	struct ast_json *blob = NULL;

	if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_INVISIBLE)) {
		return;
	}

	if (swap) {
		blob = ast_json_pack("{s: s}", "swap", ast_channel_uniqueid(swap));
		if (!blob) {
			return;
		}
	}

	msg = ast_bridge_blob_create(ast_channel_entered_bridge_type(), bridge, chan, blob);
	ast_json_unref(blob);
	if (!msg) {
		return;
	}

	/* enter blob first, then state */
	stasis_publish(ast_bridge_topic(bridge), msg);
	bridge_publish_state_from_blob(bridge, stasis_message_data(msg));
	ao2_ref(msg, -1);
}

void ast_bridge_publish_leave(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct stasis_message *msg;

	if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_INVISIBLE)) {
		return;
	}
	msg = ast_bridge_blob_create(ast_channel_left_bridge_type(), bridge, chan, NULL);
	if (!msg) {
		return;
	}

	/* state first, then leave blob (opposite of enter, preserves nesting of events) */
	bridge_publish_state_from_blob(bridge, stasis_message_data(msg));
	stasis_publish(ast_bridge_topic(bridge), msg);
	ao2_ref(msg, -1);
}

static struct ast_json *simple_bridge_channel_event(
	const char *type,
	struct ast_bridge_snapshot *bridge_snapshot,
	struct ast_channel_snapshot *channel_snapshot,
	const struct timeval *tv,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_json *json_bridge = ast_bridge_snapshot_to_json(bridge_snapshot, sanitize);
	struct ast_json *json_channel = ast_channel_snapshot_to_json(channel_snapshot, sanitize);

	if (!json_bridge || !json_channel) {
		ast_json_unref(json_bridge);
		ast_json_unref(json_channel);

		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: o, s: o}",
		"type", type,
		"timestamp", ast_json_timeval(*tv, NULL),
		"bridge", json_bridge,
		"channel", json_channel);
}

struct ast_json *ast_channel_entered_bridge_to_json(
	struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_bridge_blob *obj = stasis_message_data(msg);

	return simple_bridge_channel_event("ChannelEnteredBridge", obj->bridge,
		obj->channel, stasis_message_timestamp(msg), sanitize);
}

struct ast_json *ast_channel_left_bridge_to_json(
	struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_bridge_blob *obj = stasis_message_data(msg);

	return simple_bridge_channel_event("ChannelLeftBridge", obj->bridge,
		obj->channel, stasis_message_timestamp(msg), sanitize);
}

static struct ast_json *container_to_json_array(struct ao2_container *items,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_json *json_items = ast_json_array_create();
	char *item;
	struct ao2_iterator it;

	if (!json_items) {
		return NULL;
	}

	for (it = ao2_iterator_init(items, 0);
		(item = ao2_iterator_next(&it)); ao2_cleanup(item)) {
		if (sanitize && sanitize->channel_id && sanitize->channel_id(item)) {
			continue;
		}

		if (ast_json_array_append(json_items, ast_json_string_create(item))) {
			ao2_cleanup(item);
			ao2_iterator_destroy(&it);
			ast_json_unref(json_items);

			return NULL;
		}
	}
	ao2_iterator_destroy(&it);

	return json_items;
}

static const char *capability2str(uint32_t capabilities)
{
	if (capabilities & AST_BRIDGE_CAPABILITY_HOLDING) {
		return "holding";
	} else {
		return "mixing";
	}
}

struct ast_json *ast_bridge_snapshot_to_json(
	const struct ast_bridge_snapshot *snapshot,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_json *json_bridge;
	struct ast_json *json_channels;

	if (snapshot == NULL) {
		return NULL;
	}

	json_channels = container_to_json_array(snapshot->channels, sanitize);
	if (!json_channels) {
		return NULL;
	}

	json_bridge = ast_json_pack("{s: s, s: s, s: s, s: s, s: s, s: s, s: o, s: s}",
		"id", snapshot->uniqueid,
		"technology", snapshot->technology,
		"bridge_type", capability2str(snapshot->capabilities),
		"bridge_class", snapshot->subclass,
		"creator", snapshot->creator,
		"name", snapshot->name,
		"channels", json_channels,
		"video_mode", ast_bridge_video_mode_to_string(snapshot->video_mode));
	if (!json_bridge) {
		return NULL;
	}

	if (snapshot->video_mode != AST_BRIDGE_VIDEO_MODE_NONE
		&& !ast_strlen_zero(snapshot->video_source_id)) {
		ast_json_object_set(json_bridge, "video_source_id",
			ast_json_string_create(snapshot->video_source_id));
	}

	return json_bridge;
}

/*!
 * \internal
 * \brief Allocate the fields of an \ref ast_bridge_channel_snapshot_pair.
 *
 * \param pair A bridge and channel to get snapshots of
 * \param[out] snapshot_pair An allocated snapshot pair.
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int bridge_channel_snapshot_pair_init(struct ast_channel *channel, struct ast_bridge *bridge,
		struct ast_bridge_channel_snapshot_pair *snapshot_pair)
{
	if (bridge) {
		ast_bridge_lock(bridge);
		snapshot_pair->bridge_snapshot = ast_bridge_snapshot_create(bridge);
		ast_bridge_unlock(bridge);
		if (!snapshot_pair->bridge_snapshot) {
			return -1;
		}
	}

	snapshot_pair->channel_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(channel));
	if (!snapshot_pair->channel_snapshot) {
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Free the fields of an \ref ast_bridge_channel_snapshot_pair.
 *
 * \param pair The snapshot pair whose fields are to be cleaned up
 */
static void bridge_channel_snapshot_pair_cleanup(struct ast_bridge_channel_snapshot_pair *pair)
{
	ao2_cleanup(pair->bridge_snapshot);
	ao2_cleanup(pair->channel_snapshot);
}

static const char *result_strs[] = {
	[AST_BRIDGE_TRANSFER_FAIL] = "Fail",
	[AST_BRIDGE_TRANSFER_INVALID] = "Invalid",
	[AST_BRIDGE_TRANSFER_NOT_PERMITTED] = "Not Permitted",
	[AST_BRIDGE_TRANSFER_SUCCESS] = "Success",
};

static struct ast_json *blind_transfer_to_json(struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_blind_transfer_message *transfer_msg = stasis_message_data(msg);
	struct ast_json *json_transferer;
	struct ast_json *json_transferee = NULL;
	struct ast_json *out;
	struct ast_json *json_replace = NULL;
	const struct timeval *tv = stasis_message_timestamp(msg);

	json_transferer = ast_channel_snapshot_to_json(transfer_msg->transferer, sanitize);
	if (!json_transferer) {
		return NULL;
	}

	if (transfer_msg->transferee) {
		json_transferee = ast_channel_snapshot_to_json(transfer_msg->transferee, sanitize);
		if (!json_transferee) {
			ast_json_unref(json_transferer);
			return NULL;
		}
	}

	if (transfer_msg->replace_channel) {
		json_replace = ast_channel_snapshot_to_json(transfer_msg->replace_channel, sanitize);
		if (!json_replace) {
			ast_json_unref(json_transferee);
			ast_json_unref(json_transferer);
			return NULL;
		}
	}

	out = ast_json_pack("{s: s, s: o, s: o, s: s, s: s, s: s, s: o}",
		"type", "BridgeBlindTransfer",
		"timestamp", ast_json_timeval(*tv, NULL),
		"channel", json_transferer,
		"exten", transfer_msg->exten,
		"context", transfer_msg->context,
		"result", result_strs[transfer_msg->result],
		"is_external", ast_json_boolean(transfer_msg->is_external));

	if (!out) {
		ast_json_unref(json_transferee);
		ast_json_unref(json_replace);
		return NULL;
	}

	if (json_transferee && ast_json_object_set(out, "transferee", json_transferee)) {
		ast_json_unref(out);
		ast_json_unref(json_replace);
		return NULL;
	}

	if (json_replace && ast_json_object_set(out, "replace_channel", json_replace)) {
		ast_json_unref(out);
		return NULL;
	}

	if (transfer_msg->bridge) {
		struct ast_json *json_bridge = ast_bridge_snapshot_to_json(
				transfer_msg->bridge, sanitize);

		if (!json_bridge || ast_json_object_set(out, "bridge", json_bridge)) {
			ast_json_unref(out);
			return NULL;
		}
	}

	return out;
}

static struct ast_manager_event_blob *blind_transfer_to_ami(struct stasis_message *msg)
{
	RAII_VAR(struct ast_str *, transferer_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, bridge_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, transferee_state, NULL, ast_free_ptr);
	struct ast_blind_transfer_message *transfer_msg = stasis_message_data(msg);

	if (!transfer_msg) {
		return NULL;
	}

	transferer_state = ast_manager_build_channel_state_string_prefix(
			transfer_msg->transferer, "Transferer");
	if (!transferer_state) {
		return NULL;
	}

	if (transfer_msg->bridge) {
		bridge_state = ast_manager_build_bridge_state_string(transfer_msg->bridge);
		if (!bridge_state) {
			return NULL;
		}
	}

	if (transfer_msg->transferee) {
		transferee_state = ast_manager_build_channel_state_string_prefix(
				transfer_msg->transferee, "Transferee");
		if (!transferee_state) {
			return NULL;
		}
	}

	return ast_manager_event_blob_create(EVENT_FLAG_CALL, "BlindTransfer",
			"Result: %s\r\n"
			"%s"
			"%s"
			"%s"
			"IsExternal: %s\r\n"
			"Context: %s\r\n"
			"Extension: %s\r\n",
			result_strs[transfer_msg->result],
			ast_str_buffer(transferer_state),
			transferee_state ? ast_str_buffer(transferee_state) : "",
			bridge_state ? ast_str_buffer(bridge_state) : "",
			transfer_msg->is_external ? "Yes" : "No",
			transfer_msg->context,
			transfer_msg->exten);
}

static void blind_transfer_dtor(void *obj)
{
	struct ast_blind_transfer_message *msg = obj;

	ao2_cleanup(msg->transferer);
	ao2_cleanup(msg->bridge);
	ao2_cleanup(msg->transferee);
	ao2_cleanup(msg->replace_channel);
}

struct ast_blind_transfer_message *ast_blind_transfer_message_create(int is_external,
		struct ast_channel *transferer, const char *exten, const char *context)
{
	struct ast_blind_transfer_message *msg;

	msg = ao2_alloc(sizeof(*msg), blind_transfer_dtor);
	if (!msg) {
		return NULL;
	}

	msg->transferer = ast_channel_snapshot_get_latest(ast_channel_uniqueid(transferer));
	if (!msg->transferer) {
		ao2_cleanup(msg);
		return NULL;
	}

	msg->is_external = is_external;
	ast_copy_string(msg->context, context, sizeof(msg->context));
	ast_copy_string(msg->exten, exten, sizeof(msg->exten));

	return msg;
}


void ast_bridge_publish_blind_transfer(struct ast_blind_transfer_message *transfer_message)
{
	struct stasis_message *stasis;

	stasis = stasis_message_create(ast_blind_transfer_type(), transfer_message);
	if (!stasis) {
		return;
	}

	stasis_publish(ast_bridge_topic_all(), stasis);

	ao2_cleanup(stasis);
}

static struct ast_json *attended_transfer_to_json(struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_attended_transfer_message *transfer_msg = stasis_message_data(msg);
	RAII_VAR(struct ast_json *, out, NULL, ast_json_unref);
	struct ast_json *json_transferer1, *json_transferer2, *json_bridge, *json_channel;
	struct ast_json *json_transferee = NULL, *json_target = NULL;
	const struct timeval *tv = stasis_message_timestamp(msg);
	int res = 0;

	json_transferer1 = ast_channel_snapshot_to_json(transfer_msg->to_transferee.channel_snapshot, sanitize);
	if (!json_transferer1) {
		return NULL;
	}

	json_transferer2 = ast_channel_snapshot_to_json(transfer_msg->to_transfer_target.channel_snapshot, sanitize);
	if (!json_transferer2) {
		ast_json_unref(json_transferer1);
		return NULL;
	}

	if (transfer_msg->transferee) {
		json_transferee = ast_channel_snapshot_to_json(transfer_msg->transferee, sanitize);
		if (!json_transferee) {
			ast_json_unref(json_transferer2);
			ast_json_unref(json_transferer1);
			return NULL;
		}
	}

	if (transfer_msg->target) {
		json_target = ast_channel_snapshot_to_json(transfer_msg->target, sanitize);
		if (!json_target) {
			ast_json_unref(json_transferee);
			ast_json_unref(json_transferer2);
			ast_json_unref(json_transferer1);
			return NULL;
		}
	}

	out = ast_json_pack("{s: s, s: o, s: o, s: o, s: s, s: o}",
		"type", "BridgeAttendedTransfer",
		"timestamp", ast_json_timeval(*tv, NULL),
		"transferer_first_leg", json_transferer1,
		"transferer_second_leg", json_transferer2,
		"result", result_strs[transfer_msg->result],
		"is_external", ast_json_boolean(transfer_msg->is_external));
	if (!out) {
		ast_json_unref(json_target);
		ast_json_unref(json_transferee);
		return NULL;
	}
	if (json_transferee && ast_json_object_set(out, "transferee", json_transferee)) {
		ast_json_unref(json_target);
		return NULL;
	}
	if (json_target && ast_json_object_set(out, "transfer_target", json_target)) {
		return NULL;
	}

	if (transfer_msg->to_transferee.bridge_snapshot) {
		json_bridge = ast_bridge_snapshot_to_json(transfer_msg->to_transferee.bridge_snapshot, sanitize);

		if (!json_bridge) {
			return NULL;
		}

		res |= ast_json_object_set(out, "transferer_first_leg_bridge", json_bridge);
	}

	if (transfer_msg->to_transfer_target.bridge_snapshot) {
		json_bridge = ast_bridge_snapshot_to_json(transfer_msg->to_transfer_target.bridge_snapshot, sanitize);

		if (!json_bridge) {
			return NULL;
		}

		res |= ast_json_object_set(out, "transferer_second_leg_bridge", json_bridge);
	}

	switch (transfer_msg->dest_type) {
	case AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE:
		res |= ast_json_object_set(out, "destination_type", ast_json_string_create("bridge"));
		res |= ast_json_object_set(out, "destination_bridge", ast_json_string_create(transfer_msg->dest.bridge));
		break;
	case AST_ATTENDED_TRANSFER_DEST_LOCAL_APP:
		res |= ast_json_object_set(out, "replace_channel", ast_channel_snapshot_to_json(transfer_msg->replace_channel, sanitize));
		/* fallthrough */
	case AST_ATTENDED_TRANSFER_DEST_APP:
		res |= ast_json_object_set(out, "destination_type", ast_json_string_create("application"));
		res |= ast_json_object_set(out, "destination_application", ast_json_string_create(transfer_msg->dest.app));
		break;
	case AST_ATTENDED_TRANSFER_DEST_LINK:
		res |= ast_json_object_set(out, "destination_type", ast_json_string_create("link"));

		json_channel = ast_channel_snapshot_to_json(transfer_msg->dest.links[0], sanitize);
		if (!json_channel) {
			return NULL;
		}
		res |= ast_json_object_set(out, "destination_link_first_leg", json_channel);

		json_channel = ast_channel_snapshot_to_json(transfer_msg->dest.links[1], sanitize);
		if (!json_channel) {
			return NULL;
		}
		res |= ast_json_object_set(out, "destination_link_second_leg", json_channel);

		break;
	case AST_ATTENDED_TRANSFER_DEST_THREEWAY:
		res |= ast_json_object_set(out, "destination_type", ast_json_string_create("threeway"));

		json_channel = ast_channel_snapshot_to_json(transfer_msg->dest.threeway.channel_snapshot, sanitize);
		if (!json_channel) {
			return NULL;
		}
		res |= ast_json_object_set(out, "destination_threeway_channel", json_channel);

		json_bridge = ast_bridge_snapshot_to_json(transfer_msg->dest.threeway.bridge_snapshot, sanitize);
		if (!json_bridge) {
			return NULL;
		}
		res |= ast_json_object_set(out, "destination_threeway_bridge", json_bridge);

		break;
	case AST_ATTENDED_TRANSFER_DEST_FAIL:
		res |= ast_json_object_set(out, "destination_type", ast_json_string_create("fail"));
		break;
	}

	if (res) {
		return NULL;
	}

	return ast_json_ref(out);
}

static struct ast_manager_event_blob *attended_transfer_to_ami(struct stasis_message *msg)
{
	RAII_VAR(struct ast_str *, variable_data, ast_str_create(64), ast_free_ptr);
	RAII_VAR(struct ast_str *, transferer1_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, bridge1_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, transferer2_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, bridge2_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, local1_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, local2_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, transferee_state, NULL, ast_free_ptr);
	RAII_VAR(struct ast_str *, target_state, NULL, ast_free_ptr);
	struct ast_attended_transfer_message *transfer_msg = stasis_message_data(msg);

	if (!variable_data) {
		return NULL;
	}

	transferer1_state = ast_manager_build_channel_state_string_prefix(transfer_msg->to_transferee.channel_snapshot, "OrigTransferer");
	transferer2_state = ast_manager_build_channel_state_string_prefix(transfer_msg->to_transfer_target.channel_snapshot, "SecondTransferer");
	if (!transferer1_state || !transferer2_state) {
		return NULL;
	}
	if (transfer_msg->transferee) {
		transferee_state = ast_manager_build_channel_state_string_prefix(transfer_msg->transferee, "Transferee");
		if (!transferee_state) {
			return NULL;
		}
	}

	if (transfer_msg->target) {
		target_state = ast_manager_build_channel_state_string_prefix(transfer_msg->target, "TransferTarget");
		if (!target_state) {
			return NULL;
		}
	}

	if (transfer_msg->to_transferee.bridge_snapshot) {
		bridge1_state = ast_manager_build_bridge_state_string_prefix(
			transfer_msg->to_transferee.bridge_snapshot, "Orig");
		if (!bridge1_state) {
			return NULL;
		}
	}

	if (transfer_msg->to_transfer_target.bridge_snapshot) {
		bridge2_state = ast_manager_build_bridge_state_string_prefix(
			transfer_msg->to_transfer_target.bridge_snapshot, "Second");
		if (!bridge2_state) {
			return NULL;
		}
	}

	switch (transfer_msg->dest_type) {
	case AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE:
		ast_str_append(&variable_data, 0, "DestType: Bridge\r\n");
		ast_str_append(&variable_data, 0, "DestBridgeUniqueid: %s\r\n", transfer_msg->dest.bridge);
		break;
	case AST_ATTENDED_TRANSFER_DEST_APP:
	case AST_ATTENDED_TRANSFER_DEST_LOCAL_APP:
		ast_str_append(&variable_data, 0, "DestType: App\r\n");
		ast_str_append(&variable_data, 0, "DestApp: %s\r\n", transfer_msg->dest.app);
		break;
	case AST_ATTENDED_TRANSFER_DEST_LINK:
		local1_state = ast_manager_build_channel_state_string_prefix(transfer_msg->dest.links[0], "LocalOne");
		local2_state = ast_manager_build_channel_state_string_prefix(transfer_msg->dest.links[1], "LocalTwo");
		if (!local1_state || !local2_state) {
			return NULL;
		}
		ast_str_append(&variable_data, 0, "DestType: Link\r\n");
		ast_str_append(&variable_data, 0, "%s", ast_str_buffer(local1_state));
		ast_str_append(&variable_data, 0, "%s", ast_str_buffer(local2_state));
		break;
	case AST_ATTENDED_TRANSFER_DEST_THREEWAY:
		ast_str_append(&variable_data, 0, "DestType: Threeway\r\n");
		ast_str_append(&variable_data, 0, "DestBridgeUniqueid: %s\r\n", transfer_msg->dest.threeway.bridge_snapshot->uniqueid);
		ast_str_append(&variable_data, 0, "DestTransfererChannel: %s\r\n", transfer_msg->dest.threeway.channel_snapshot->name);
		break;
	case AST_ATTENDED_TRANSFER_DEST_FAIL:
		ast_str_append(&variable_data, 0, "DestType: Fail\r\n");
		break;
	}

	return ast_manager_event_blob_create(EVENT_FLAG_CALL, "AttendedTransfer",
			"Result: %s\r\n"
			"%s"
			"%s"
			"%s"
			"%s"
			"%s"
			"%s"
			"IsExternal: %s\r\n"
			"%s",
			result_strs[transfer_msg->result],
			ast_str_buffer(transferer1_state),
			bridge1_state ? ast_str_buffer(bridge1_state) : "",
			ast_str_buffer(transferer2_state),
			bridge2_state ? ast_str_buffer(bridge2_state) : "",
			transferee_state ? ast_str_buffer(transferee_state) : "",
			target_state ? ast_str_buffer(target_state) : "",
			transfer_msg->is_external ? "Yes" : "No",
			ast_str_buffer(variable_data));
}

static void attended_transfer_dtor(void *obj)
{
	struct ast_attended_transfer_message *msg = obj;
	int i;

	bridge_channel_snapshot_pair_cleanup(&msg->to_transferee);
	bridge_channel_snapshot_pair_cleanup(&msg->to_transfer_target);
	ao2_cleanup(msg->replace_channel);
	ao2_cleanup(msg->transferee);
	ao2_cleanup(msg->target);

	if (msg->dest_type != AST_ATTENDED_TRANSFER_DEST_LINK) {
		return;
	}

	for (i = 0; i < ARRAY_LEN(msg->dest.links); ++i) {
		ao2_cleanup(msg->dest.links[i]);
	}
}

struct ast_attended_transfer_message *ast_attended_transfer_message_create(int is_external,
		struct ast_channel *to_transferee, struct ast_bridge *transferee_bridge,
		struct ast_channel *to_transfer_target, struct ast_bridge *target_bridge,
		struct ast_channel *transferee, struct ast_channel *transfer_target)
{
	struct ast_attended_transfer_message *transfer_msg;

	transfer_msg = ao2_alloc(sizeof(*transfer_msg), attended_transfer_dtor);
	if (!transfer_msg) {
		return NULL;
	}

	if (bridge_channel_snapshot_pair_init(to_transferee, transferee_bridge, &transfer_msg->to_transferee) ||
			bridge_channel_snapshot_pair_init(to_transfer_target, target_bridge, &transfer_msg->to_transfer_target)) {
		ao2_cleanup(transfer_msg);
		return NULL;
	}

	if (transferee) {
		transfer_msg->transferee = ast_channel_snapshot_get_latest(ast_channel_uniqueid(transferee));
		if (!transfer_msg->transferee) {
			ao2_cleanup(transfer_msg);
			return NULL;
		}
	} else if (transferee_bridge) {
		transferee = ast_bridge_peer(transferee_bridge, to_transferee);
		if (transferee) {
			transfer_msg->transferee = ast_channel_snapshot_get_latest(ast_channel_uniqueid(transferee));
			ao2_cleanup(transferee);
			if (!transfer_msg->transferee) {
				ao2_cleanup(transfer_msg);
				return NULL;
			}
		}
	}

	if (transfer_target) {
		transfer_msg->target = ast_channel_snapshot_get_latest(ast_channel_uniqueid(transfer_target));
		if (!transfer_msg->target) {
			ao2_cleanup(transfer_msg);
			return NULL;
		}
	} else if (target_bridge) {
		transfer_target = ast_bridge_peer(target_bridge, to_transfer_target);
		if (transfer_target) {
			transfer_msg->target = ast_channel_snapshot_get_latest(ast_channel_uniqueid(transfer_target));
			ao2_cleanup(transfer_target);
			if (!transfer_msg->target) {
				ao2_cleanup(transfer_msg);
				return NULL;
			}
		}
	}

	return transfer_msg;
}

int ast_attended_transfer_message_add_merge(struct ast_attended_transfer_message *transfer_msg,
		struct ast_bridge *final_bridge)
{
	transfer_msg->dest_type = AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE;
	ast_copy_string(transfer_msg->dest.bridge, final_bridge->uniqueid,
			sizeof(transfer_msg->dest.bridge));

	return 0;
}

int ast_attended_transfer_message_add_threeway(struct ast_attended_transfer_message *transfer_msg,
		struct ast_channel *survivor_channel, struct ast_bridge *survivor_bridge)
{
	transfer_msg->dest_type = AST_ATTENDED_TRANSFER_DEST_THREEWAY;

	if (!strcmp(ast_channel_uniqueid(survivor_channel), transfer_msg->to_transferee.channel_snapshot->uniqueid)) {
		transfer_msg->dest.threeway.channel_snapshot = transfer_msg->to_transferee.channel_snapshot;
	} else {
		transfer_msg->dest.threeway.channel_snapshot = transfer_msg->to_transfer_target.channel_snapshot;
	}

	if (!strcmp(survivor_bridge->uniqueid, transfer_msg->to_transferee.bridge_snapshot->uniqueid)) {
		transfer_msg->dest.threeway.bridge_snapshot = transfer_msg->to_transferee.bridge_snapshot;
	} else {
		transfer_msg->dest.threeway.bridge_snapshot = transfer_msg->to_transfer_target.bridge_snapshot;
	}

	return 0;
}

int ast_attended_transfer_message_add_app(struct ast_attended_transfer_message *transfer_msg,
		const char *app, struct ast_channel *replace_channel)
{
	transfer_msg->dest_type = replace_channel ? AST_ATTENDED_TRANSFER_DEST_LOCAL_APP : AST_ATTENDED_TRANSFER_DEST_APP;

	if (replace_channel) {
		transfer_msg->replace_channel = ast_channel_snapshot_get_latest(ast_channel_uniqueid(replace_channel));
		if (!transfer_msg->replace_channel) {
			return -1;
		}
	}

	ast_copy_string(transfer_msg->dest.app, app, sizeof(transfer_msg->dest.app));

	return 0;
}

int ast_attended_transfer_message_add_link(struct ast_attended_transfer_message *transfer_msg,
		struct ast_channel *locals[2])
{
	int i;

	transfer_msg->dest_type = AST_ATTENDED_TRANSFER_DEST_LINK;
	for (i = 0; i < 2; ++i) {
		transfer_msg->dest.links[i] = ast_channel_snapshot_get_latest(ast_channel_uniqueid(locals[i]));
		if (!transfer_msg->dest.links[i]) {
			return -1;
		}
	}

	return 0;
}

void ast_bridge_publish_attended_transfer(struct ast_attended_transfer_message *transfer_msg)
{
	struct stasis_message *msg;

	msg = stasis_message_create(ast_attended_transfer_type(), transfer_msg);
	if (!msg) {
		return;
	}

	stasis_publish(ast_bridge_topic_all(), msg);
	ao2_ref(msg, -1);
}

struct ast_bridge_snapshot *ast_bridge_snapshot_get_latest(const char *uniqueid)
{
	struct stasis_message *message;
	struct ast_bridge_snapshot *snapshot;

	ast_assert(!ast_strlen_zero(uniqueid));

	message = stasis_cache_get(ast_bridge_cache(),
			ast_bridge_snapshot_type(),
			uniqueid);
	if (!message) {
		return NULL;
	}

	snapshot = ao2_bump(stasis_message_data(message));
	ao2_ref(message, -1);

	return snapshot;
}

/*! \brief snapshot ID getter for caching topic */
static const char *bridge_snapshot_get_id(struct stasis_message *msg)
{
	struct ast_bridge_snapshot *snapshot;
	if (stasis_message_type(msg) != ast_bridge_snapshot_type()) {
		return NULL;
	}
	snapshot = stasis_message_data(msg);
	return snapshot->uniqueid;
}

static void stasis_bridging_cleanup(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(ast_bridge_snapshot_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_bridge_merge_message_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_entered_bridge_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_left_bridge_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_blind_transfer_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_attended_transfer_type);

	ao2_cleanup(bridge_cache_all);
	bridge_cache_all = NULL;
}

int ast_stasis_bridging_init(void)
{
	int res = 0;

	ast_register_cleanup(stasis_bridging_cleanup);

	bridge_cache_all = stasis_cp_all_create("ast_bridge_topic_all",
		bridge_snapshot_get_id);

	if (!bridge_cache_all) {
		return -1;
	}

	res |= STASIS_MESSAGE_TYPE_INIT(ast_bridge_snapshot_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_bridge_merge_message_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_entered_bridge_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_left_bridge_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_blind_transfer_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_attended_transfer_type);

	return res;
}
