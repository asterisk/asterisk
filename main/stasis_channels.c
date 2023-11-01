/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Stasis Messages and Data Types for Channel Objects
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/json.h"
#include "asterisk/pbx.h"
#include "asterisk/bridge.h"
#include "asterisk/translate.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/dial.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utf8.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="VarSet">
		<managerEventInstance class="EVENT_FLAG_DIALPLAN">
			<synopsis>Raised when a variable is set to a particular value.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Variable">
					<para>The variable being set.</para>
				</parameter>
				<parameter name="Value">
					<para>The new value of the variable.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentLogin">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when an Agent has logged in.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Agent">
					<para>Agent ID of the agent.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">AgentLogin</ref>
				<ref type="managerEvent">AgentLogoff</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentLogoff">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when an Agent has logged off.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='AgentLogin']/managerEventInstance/syntax/parameter)" />
				<parameter name="Logintime">
					<para>The number of seconds the agent was logged in.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">AgentLogin</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ChannelTalkingStart">
		<managerEventInstance class="EVENT_FLAG_CLASS">
			<synopsis>Raised when talking is detected on a channel.</synopsis>
			<syntax>
				<channel_snapshot/>
			</syntax>
			<see-also>
				<ref type="function">TALK_DETECT</ref>
				<ref type="managerEvent">ChannelTalkingStop</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ChannelTalkingStop">
		<managerEventInstance class="EVENT_FLAG_CLASS">
			<synopsis>Raised when talking is no longer detected on a channel.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Duration">
					<para>The length in time, in milliseconds, that talking was
					detected on the channel.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="function">TALK_DETECT</ref>
				<ref type="managerEvent">ChannelTalkingStart</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
***/

#define NUM_MULTI_CHANNEL_BLOB_BUCKETS 7

static struct stasis_topic *channel_topic_all;
static struct ao2_container *channel_cache;
static struct ao2_container *channel_cache_by_name;

struct ao2_container *ast_channel_cache_all(void)
{
	return ao2_bump(channel_cache);
}

struct stasis_topic *ast_channel_topic_all(void)
{
	return channel_topic_all;
}

struct ao2_container *ast_channel_cache_by_name(void)
{
	return ao2_bump(channel_cache_by_name);
}

/*!
 * \internal
 * \brief Hash function for \ref ast_channel_snapshot objects
 */
static int channel_snapshot_hash_cb(const void *obj, const int flags)
{
	const struct ast_channel_snapshot *object = obj;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		key = object->base->name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

/*!
 * \internal
 * \brief Comparison function for \ref ast_channel_snapshot objects
 */
static int channel_snapshot_cmp_cb(void *obj, void *arg, int flags)
{
	const struct ast_channel_snapshot *object_left = obj;
	const struct ast_channel_snapshot *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->base->name;
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(object_left->base->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncasecmp(object_left->base->name, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

/*!
 * \internal
 * \brief Hash function (using uniqueid) for \ref ast_channel_snapshot objects
 */
static int channel_snapshot_uniqueid_hash_cb(const void *obj, const int flags)
{
	const struct ast_channel_snapshot *object = obj;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		key = object->base->uniqueid;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

/*!
 * \internal
 * \brief Comparison function (using uniqueid) for \ref ast_channel_snapshot objects
 */
static int channel_snapshot_uniqueid_cmp_cb(void *obj, void *arg, int flags)
{
	const struct ast_channel_snapshot *object_left = obj;
	const struct ast_channel_snapshot *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->base->uniqueid;
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(object_left->base->uniqueid, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncasecmp(object_left->base->uniqueid, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

static void channel_snapshot_dtor(void *obj)
{
	struct ast_channel_snapshot *snapshot = obj;

	ao2_cleanup(snapshot->base);
	ao2_cleanup(snapshot->peer);
	ao2_cleanup(snapshot->caller);
	ao2_cleanup(snapshot->connected);
	ao2_cleanup(snapshot->bridge);
	ao2_cleanup(snapshot->dialplan);
	ao2_cleanup(snapshot->hangup);
	ao2_cleanup(snapshot->manager_vars);
	ao2_cleanup(snapshot->ari_vars);
}

static void channel_snapshot_base_dtor(void *obj)
{
	struct ast_channel_snapshot_base *snapshot = obj;

	ast_string_field_free_memory(snapshot);
}

static struct ast_channel_snapshot_base *channel_snapshot_base_create(struct ast_channel *chan)
{
	struct ast_channel_snapshot_base *snapshot;

	snapshot = ao2_alloc_options(sizeof(*snapshot), channel_snapshot_base_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	if (ast_string_field_init(snapshot, 256) || ast_string_field_init_extended(snapshot, protocol_id)) {
		ao2_ref(snapshot, -1);
		return NULL;
	}

	ast_string_field_set(snapshot, name, ast_channel_name(chan));
	ast_string_field_set(snapshot, type, ast_channel_tech(chan)->type);
	ast_string_field_set(snapshot, accountcode, ast_channel_accountcode(chan));
	ast_string_field_set(snapshot, userfield, ast_channel_userfield(chan));
	ast_string_field_set(snapshot, uniqueid, ast_channel_uniqueid(chan));
	ast_string_field_set(snapshot, language, ast_channel_language(chan));

	snapshot->creationtime = ast_channel_creationtime(chan);
	snapshot->tech_properties = ast_channel_tech(chan)->properties;

	if (ast_channel_tech(chan)->get_pvt_uniqueid) {
		ast_string_field_set(snapshot, protocol_id, ast_channel_tech(chan)->get_pvt_uniqueid(chan));
	}

	return snapshot;
}

static struct ast_channel_snapshot_peer *channel_snapshot_peer_create(struct ast_channel *chan)
{
	const char *linkedid = S_OR(ast_channel_linkedid(chan), "");
	const char *peeraccount = S_OR(ast_channel_peeraccount(chan), "");
	size_t linkedid_len = strlen(linkedid) + 1;
	size_t peeraccount_len = strlen(peeraccount) + 1;
	struct ast_channel_snapshot_peer *snapshot;

	snapshot = ao2_alloc_options(sizeof(*snapshot) + linkedid_len + peeraccount_len, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	strcpy(snapshot->account, peeraccount); /* Safe */
	snapshot->linkedid = snapshot->account + peeraccount_len;
	ast_copy_string(snapshot->linkedid, linkedid, linkedid_len); /* Safe */

	return snapshot;
}

static void channel_snapshot_caller_dtor(void *obj)
{
	struct ast_channel_snapshot_caller *snapshot = obj;

	ast_string_field_free_memory(snapshot);
}

static struct ast_channel_snapshot_caller *channel_snapshot_caller_create(struct ast_channel *chan)
{
	struct ast_channel_snapshot_caller *snapshot;

	snapshot = ao2_alloc_options(sizeof(*snapshot), channel_snapshot_caller_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	if (ast_string_field_init(snapshot, 256)) {
		ao2_ref(snapshot, -1);
		return NULL;
	}

	ast_string_field_set(snapshot, name,
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, ""));
	ast_string_field_set(snapshot, number,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, ""));
	ast_string_field_set(snapshot, subaddr,
		S_COR(ast_channel_caller(chan)->id.subaddress.valid, ast_channel_caller(chan)->id.subaddress.str, ""));
	ast_string_field_set(snapshot, ani,
		S_COR(ast_channel_caller(chan)->ani.number.valid, ast_channel_caller(chan)->ani.number.str, ""));

	ast_string_field_set(snapshot, rdnis,
		S_COR(ast_channel_redirecting(chan)->from.number.valid, ast_channel_redirecting(chan)->from.number.str, ""));

	ast_string_field_set(snapshot, dnid,
		S_OR(ast_channel_dialed(chan)->number.str, ""));
	ast_string_field_set(snapshot, dialed_subaddr,
		S_COR(ast_channel_dialed(chan)->subaddress.valid, ast_channel_dialed(chan)->subaddress.str, ""));

	snapshot->pres = ast_party_id_presentation(&ast_channel_caller(chan)->id);

	return snapshot;
}

static struct ast_channel_snapshot_connected *channel_snapshot_connected_create(struct ast_channel *chan)
{
	const char *name = S_COR(ast_channel_connected(chan)->id.name.valid, ast_channel_connected(chan)->id.name.str, "");
	const char *number = S_COR(ast_channel_connected(chan)->id.number.valid, ast_channel_connected(chan)->id.number.str, "");
	size_t name_len = strlen(name) + 1;
	size_t number_len = strlen(number) + 1;
	struct ast_channel_snapshot_connected *snapshot;

	snapshot = ao2_alloc_options(sizeof(*snapshot) + name_len + number_len, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	strcpy(snapshot->name, name); /* Safe */
	snapshot->number = snapshot->name + name_len;
	ast_copy_string(snapshot->number, number, number_len); /* Safe */

	return snapshot;
}

static struct ast_channel_snapshot_bridge *channel_snapshot_bridge_create(struct ast_channel *chan)
{
	const char *uniqueid = "";
	struct ast_bridge *bridge;
	struct ast_channel_snapshot_bridge *snapshot;

	bridge = ast_channel_get_bridge(chan);
	if (bridge && !ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_INVISIBLE)) {
		uniqueid = bridge->uniqueid;
	}
	ao2_cleanup(bridge);

	snapshot = ao2_alloc_options(sizeof(*snapshot) + strlen(uniqueid) + 1, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	strcpy(snapshot->id, uniqueid); /* Safe */

	return snapshot;
}

static void channel_snapshot_dialplan_dtor(void *obj)
{
	struct ast_channel_snapshot_dialplan *snapshot = obj;

	ast_string_field_free_memory(snapshot);
}

static struct ast_channel_snapshot_dialplan *channel_snapshot_dialplan_create(struct ast_channel *chan)
{
	struct ast_channel_snapshot_dialplan *snapshot;

	snapshot = ao2_alloc_options(sizeof(*snapshot), channel_snapshot_dialplan_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	if (ast_string_field_init(snapshot, 256)) {
		ao2_ref(snapshot, -1);
		return NULL;
	}

	if (ast_channel_appl(chan)) {
		ast_string_field_set(snapshot, appl, ast_channel_appl(chan));
	}
	if (ast_channel_data(chan)) {
		ast_string_field_set(snapshot, data, ast_channel_data(chan));
	}
	ast_string_field_set(snapshot, context, ast_channel_context(chan));
	ast_string_field_set(snapshot, exten, ast_channel_exten(chan));
	snapshot->priority = ast_channel_priority(chan);

	return snapshot;
}

static struct ast_channel_snapshot_hangup *channel_snapshot_hangup_create(struct ast_channel *chan)
{
	const char *hangupsource = S_OR(ast_channel_hangupsource(chan), "");
	struct ast_channel_snapshot_hangup *snapshot;

	snapshot = ao2_alloc_options(sizeof(*snapshot) + strlen(hangupsource) + 1, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	snapshot->cause = ast_channel_hangupcause(chan);
	strcpy(snapshot->source, hangupsource); /* Safe */

	return snapshot;
}

struct ast_channel_snapshot *ast_channel_snapshot_create(struct ast_channel *chan)
{
	struct ast_channel_snapshot *old_snapshot;
	struct ast_channel_snapshot *snapshot;

	/* no snapshots for dummy channels */
	if (!ast_channel_tech(chan)) {
		return NULL;
	}

	snapshot = ao2_alloc_options(sizeof(*snapshot), channel_snapshot_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snapshot) {
		return NULL;
	}

	old_snapshot = ast_channel_snapshot(chan);

	/* Channels automatically have all segments invalidated on them initially so a check for an old
	 * snapshot existing before usage is not done here, as it can not happen. If the stored snapshot
	 * on the channel is updated as a result of this then all segments marked as invalidated will be
	 * cleared.
	 */
	if (ast_test_flag(ast_channel_snapshot_segment_flags(chan), AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE)) {
		/* The base information has changed so update our snapshot */
		snapshot->base = channel_snapshot_base_create(chan);
		if (!snapshot->base) {
			ao2_ref(snapshot, -1);
			return NULL;
		}
	} else {
		snapshot->base = ao2_bump(old_snapshot->base);
	}

	if (ast_test_flag(ast_channel_snapshot_segment_flags(chan), AST_CHANNEL_SNAPSHOT_INVALIDATE_PEER)) {
		/* The peer information has changed so update our snapshot */
		snapshot->peer = channel_snapshot_peer_create(chan);
		if (!snapshot->peer) {
			ao2_ref(snapshot, -1);
			return NULL;
		}
	} else {
		snapshot->peer = ao2_bump(old_snapshot->peer);
	}

	/* Unfortunately both caller and connected information do not have an enforced contract with
	 * the channel API. This has allowed consumers to directly get the caller or connected structure
	 * and manipulate it. Until such time as there is an enforced contract (which is being tracked under
	 * ASTERISK-28164) they are each regenerated every time a channel snapshot is created.
	 */
	snapshot->caller = channel_snapshot_caller_create(chan);
	if (!snapshot->caller) {
		ao2_ref(snapshot, -1);
		return NULL;
	}

	snapshot->connected = channel_snapshot_connected_create(chan);
	if (!snapshot->connected) {
		ao2_ref(snapshot, -1);
		return NULL;
	}

	if (ast_test_flag(ast_channel_snapshot_segment_flags(chan), AST_CHANNEL_SNAPSHOT_INVALIDATE_BRIDGE)) {
		/* The bridge has changed so update our snapshot */
		snapshot->bridge = channel_snapshot_bridge_create(chan);
		if (!snapshot->bridge) {
			ao2_ref(snapshot, -1);
			return NULL;
		}
	} else {
		snapshot->bridge = ao2_bump(old_snapshot->bridge);
	}

	if (ast_test_flag(ast_channel_snapshot_segment_flags(chan), AST_CHANNEL_SNAPSHOT_INVALIDATE_DIALPLAN)) {
		/* The dialplan information has changed so update our snapshot */
		snapshot->dialplan = channel_snapshot_dialplan_create(chan);
		if (!snapshot->dialplan) {
			ao2_ref(snapshot, -1);
			return NULL;
		}
	} else {
		snapshot->dialplan = ao2_bump(old_snapshot->dialplan);
	}

	if (ast_test_flag(ast_channel_snapshot_segment_flags(chan), AST_CHANNEL_SNAPSHOT_INVALIDATE_HANGUP)) {
		/* The hangup information has changed so update our snapshot */
		snapshot->hangup = channel_snapshot_hangup_create(chan);
		if (!snapshot->hangup) {
			ao2_ref(snapshot, -1);
			return NULL;
		}
	} else {
		snapshot->hangup = ao2_bump(old_snapshot->hangup);
	}

	snapshot->state = ast_channel_state(chan);
	snapshot->amaflags = ast_channel_amaflags(chan);
	ast_copy_flags(&snapshot->flags, ast_channel_flags(chan), 0xFFFFFFFF);
	ast_set_flag(&snapshot->softhangup_flags, ast_channel_softhangup_internal_flag(chan));

	/* These have to be recreated as they may have changed, unfortunately */
	snapshot->manager_vars = ast_channel_get_manager_vars(chan);
	snapshot->ari_vars = ast_channel_get_ari_vars(chan);

	return snapshot;
}

static void channel_snapshot_update_dtor(void *obj)
{
	struct ast_channel_snapshot_update *update = obj;

	ao2_cleanup(update->old_snapshot);
	ao2_cleanup(update->new_snapshot);
}

static struct ast_channel_snapshot_update *channel_snapshot_update_create(struct ast_channel *chan)
{
	struct ast_channel_snapshot_update *update;

	update = ao2_alloc_options(sizeof(*update), channel_snapshot_update_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!update) {
		return NULL;
	}

	update->old_snapshot = ao2_bump(ast_channel_snapshot(chan));
	update->new_snapshot = ast_channel_snapshot_create(chan);
	if (!update->new_snapshot) {
		ao2_ref(update, -1);
		return NULL;
	}

	return update;
}

static void publish_message_for_channel_topics(struct stasis_message *message, struct ast_channel *chan)
{
	if (chan) {
		stasis_publish(ast_channel_topic(chan), message);
	} else {
		stasis_publish(ast_channel_topic_all(), message);
	}
}

static void channel_blob_dtor(void *obj)
{
	struct ast_channel_blob *event = obj;
	ao2_cleanup(event->snapshot);
	ast_json_unref(event->blob);
}

static void ast_channel_publish_dial_internal(struct ast_channel *caller,
	struct ast_channel *peer, struct ast_channel *forwarded, const char *dialstring,
	const char *dialstatus, const char *forward)
{
	struct ast_multi_channel_blob *payload;
	struct stasis_message *msg;
	struct ast_json *blob;
	struct ast_channel_snapshot *peer_snapshot;

	if (!ast_channel_dial_type()) {
		return;
	}

	ast_assert(peer != NULL);

	blob = ast_json_pack("{s: s, s: s, s: s}",
		"dialstatus", S_OR(dialstatus, ""),
		"forward", S_OR(forward, ""),
		"dialstring", S_OR(dialstring, ""));
	if (!blob) {
		return;
	}
	payload = ast_multi_channel_blob_create(blob);
	ast_json_unref(blob);
	if (!payload) {
		return;
	}

	if (caller) {
		struct ast_channel_snapshot *caller_snapshot;

		ast_channel_lock(caller);
		if (ast_strlen_zero(dialstatus)) {
			caller_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(caller));
		} else {
			caller_snapshot = ast_channel_snapshot_create(caller);
		}
		ast_channel_unlock(caller);
		if (!caller_snapshot) {
			ao2_ref(payload, -1);
			return;
		}
		ast_multi_channel_blob_add_channel(payload, "caller", caller_snapshot);
		ao2_ref(caller_snapshot, -1);
	}

	ast_channel_lock(peer);
	if (ast_strlen_zero(dialstatus)) {
		peer_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(peer));
	} else {
		peer_snapshot = ast_channel_snapshot_create(peer);
	}
	ast_channel_unlock(peer);
	if (!peer_snapshot) {
		ao2_ref(payload, -1);
		return;
	}
	ast_multi_channel_blob_add_channel(payload, "peer", peer_snapshot);
	ao2_ref(peer_snapshot, -1);

	if (forwarded) {
		struct ast_channel_snapshot *forwarded_snapshot;

		ast_channel_lock(forwarded);
		forwarded_snapshot = ast_channel_snapshot_create(forwarded);
		ast_channel_unlock(forwarded);
		if (!forwarded_snapshot) {
			ao2_ref(payload, -1);
			return;
		}
		ast_multi_channel_blob_add_channel(payload, "forwarded", forwarded_snapshot);
		ao2_ref(forwarded_snapshot, -1);
	}

	msg = stasis_message_create(ast_channel_dial_type(), payload);
	ao2_ref(payload, -1);
	if (msg) {
		publish_message_for_channel_topics(msg, caller ?: peer);
		ao2_ref(msg, -1);
	}
}

static void remove_dial_masquerade(struct ast_channel *peer);
static void remove_dial_masquerade_caller(struct ast_channel *caller);
static int set_dial_masquerade(struct ast_channel *caller,
	struct ast_channel *peer, const char *dialstring);

void ast_channel_publish_dial_forward(struct ast_channel *caller, struct ast_channel *peer,
	struct ast_channel *forwarded, const char *dialstring, const char *dialstatus,
	const char *forward)
{
	ast_assert(peer != NULL);

	/* XXX With an early bridge the below dial masquerade datastore code could, theoretically,
	 * go away as the act of changing the channel during dialing would be done using the bridge
	 * API itself and not a masquerade.
	 */

	if (caller) {
		/*
		 * Lock two or three channels.
		 *
		 * We need to hold the locks to hold off a potential masquerade
		 * messing up the stasis dial event ordering.
		 */
		for (;; ast_channel_unlock(caller), sched_yield()) {
			ast_channel_lock(caller);
			if (ast_channel_trylock(peer)) {
				continue;
			}
			if (forwarded && ast_channel_trylock(forwarded)) {
				ast_channel_unlock(peer);
				continue;
			}
			break;
		}

		if (ast_strlen_zero(dialstatus)) {
			set_dial_masquerade(caller, peer, dialstring);
		} else {
			remove_dial_masquerade(peer);
		}
	}

	ast_channel_publish_dial_internal(caller, peer, forwarded, dialstring, dialstatus,
		forward);

	if (caller) {
		if (forwarded) {
			ast_channel_unlock(forwarded);
		}
		ast_channel_unlock(peer);
		remove_dial_masquerade_caller(caller);
		ast_channel_unlock(caller);
	}
}

void ast_channel_publish_dial(struct ast_channel *caller, struct ast_channel *peer,
	const char *dialstring, const char *dialstatus)
{
	ast_channel_publish_dial_forward(caller, peer, NULL, dialstring, dialstatus, NULL);
}

static struct stasis_message *create_channel_blob_message(struct ast_channel_snapshot *snapshot,
		struct stasis_message_type *type,
		struct ast_json *blob)
{
	struct stasis_message *msg;
	struct ast_channel_blob *obj;

	obj = ao2_alloc(sizeof(*obj), channel_blob_dtor);
	if (!obj) {
		return NULL;
	}

	if (snapshot) {
		obj->snapshot = snapshot;
		ao2_ref(obj->snapshot, +1);
	}
	if (!blob) {
		blob = ast_json_null();
	}
	obj->blob = ast_json_ref(blob);

	msg = stasis_message_create(type, obj);
	ao2_cleanup(obj);
	return msg;
}

struct stasis_message *ast_channel_blob_create_from_cache(const char *channel_id,
					       struct stasis_message_type *type,
					       struct ast_json *blob)
{
	struct ast_channel_snapshot *snapshot;
	struct stasis_message *msg;

	if (!type) {
		return NULL;
	}

	snapshot = ast_channel_snapshot_get_latest(channel_id);
	msg = create_channel_blob_message(snapshot, type, blob);
	ao2_cleanup(snapshot);
	return msg;
}

struct stasis_message *ast_channel_blob_create(struct ast_channel *chan,
	struct stasis_message_type *type, struct ast_json *blob)
{
	struct ast_channel_snapshot *snapshot;
	struct stasis_message *msg;

	if (!type) {
		return NULL;
	}

	snapshot = chan ? ao2_bump(ast_channel_snapshot(chan)) : NULL;
	msg = create_channel_blob_message(snapshot, type, blob);
	ao2_cleanup(snapshot);
	return msg;
}

/*! \brief A channel snapshot wrapper object used in \ref ast_multi_channel_blob objects */
struct channel_role_snapshot {
	struct ast_channel_snapshot *snapshot;	/*!< A channel snapshot */
	char role[0];							/*!< The role assigned to the channel */
};

/*! \brief A multi channel blob data structure for multi_channel_blob stasis messages */
struct ast_multi_channel_blob {
	struct ao2_container *channel_snapshots;	/*!< A container holding the snapshots */
	struct ast_json *blob;						/*!< A blob of JSON data */
};

/*!
 * \internal
 * \brief Comparison function for \ref channel_role_snapshot objects
 */
static int channel_role_cmp_cb(void *obj, void *arg, int flags)
{
	const struct channel_role_snapshot *object_left = obj;
	const struct channel_role_snapshot *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->role;
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(object_left->role, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncasecmp(object_left->role, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

/*!
 * \internal
 * \brief Hash function for \ref channel_role_snapshot objects
 */
static int channel_role_hash_cb(const void *obj, const int flags)
{
	const struct channel_role_snapshot *object = obj;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		key = object->role;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

/*!
 * \internal
 * \brief Destructor for \ref ast_multi_channel_blob objects
 */
static void multi_channel_blob_dtor(void *obj)
{
	struct ast_multi_channel_blob *multi_blob = obj;

	ao2_cleanup(multi_blob->channel_snapshots);
	ast_json_unref(multi_blob->blob);
}

struct ast_multi_channel_blob *ast_multi_channel_blob_create(struct ast_json *blob)
{
	struct ast_multi_channel_blob *obj;

	ast_assert(blob != NULL);

	obj = ao2_alloc(sizeof(*obj), multi_channel_blob_dtor);
	if (!obj) {
		return NULL;
	}

	obj->channel_snapshots = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NUM_MULTI_CHANNEL_BLOB_BUCKETS, channel_role_hash_cb, NULL, channel_role_cmp_cb);
	if (!obj->channel_snapshots) {
		ao2_ref(obj, -1);
		return NULL;
	}

	obj->blob = ast_json_ref(blob);
	return obj;
}

struct ast_channel_snapshot *ast_channel_snapshot_get_latest(const char *uniqueid)
{
	ast_assert(!ast_strlen_zero(uniqueid));

	return ao2_find(channel_cache, uniqueid, OBJ_SEARCH_KEY);
}

struct ast_channel_snapshot *ast_channel_snapshot_get_latest_by_name(const char *name)
{
	ast_assert(!ast_strlen_zero(name));

	return ao2_find(channel_cache_by_name, name, OBJ_SEARCH_KEY);
}

void ast_channel_publish_final_snapshot(struct ast_channel *chan)
{
	struct ast_channel_snapshot_update *update;
	struct stasis_message *message;

	if (!ast_channel_snapshot_type()) {
		return;
	}

	update = channel_snapshot_update_create(chan);
	if (!update) {
		return;
	}

	message = stasis_message_create(ast_channel_snapshot_type(), update);
	/* In the success path message holds a reference to update so it will be valid
	 * for the lifetime of this function until the end.
	 */
	ao2_ref(update, -1);
	if (!message) {
		return;
	}

	ao2_unlink(channel_cache, update->old_snapshot);
	ao2_unlink(channel_cache_by_name, update->old_snapshot);

	ast_channel_snapshot_set(chan, NULL);

	stasis_publish(ast_channel_topic(chan), message);
	ao2_ref(message, -1);
}

static void channel_role_snapshot_dtor(void *obj)
{
	struct channel_role_snapshot *role_snapshot = obj;

	ao2_cleanup(role_snapshot->snapshot);
}

void ast_multi_channel_blob_add_channel(struct ast_multi_channel_blob *obj, const char *role, struct ast_channel_snapshot *snapshot)
{
	struct channel_role_snapshot *role_snapshot;
	int role_len = strlen(role) + 1;

	if (!obj || ast_strlen_zero(role) || !snapshot) {
		return;
	}

	role_snapshot = ao2_alloc_options(sizeof(*role_snapshot) + role_len,
		channel_role_snapshot_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!role_snapshot) {
		return;
	}
	ast_copy_string(role_snapshot->role, role, role_len);
	role_snapshot->snapshot = snapshot;
	ao2_ref(role_snapshot->snapshot, +1);
	ao2_link(obj->channel_snapshots, role_snapshot);
	ao2_ref(role_snapshot, -1);
}

struct ast_channel_snapshot *ast_multi_channel_blob_get_channel(struct ast_multi_channel_blob *obj, const char *role)
{
	struct channel_role_snapshot *role_snapshot;
	struct ast_channel_snapshot *snapshot;

	if (!obj || ast_strlen_zero(role)) {
		return NULL;
	}
	role_snapshot = ao2_find(obj->channel_snapshots, role, OBJ_SEARCH_KEY);
	/* Note that this function does not increase the ref count on snapshot */
	if (!role_snapshot) {
		return NULL;
	}
	snapshot = role_snapshot->snapshot;
	ao2_ref(role_snapshot, -1);
	return snapshot;
}

struct ao2_container *ast_multi_channel_blob_get_channels(struct ast_multi_channel_blob *obj, const char *role)
{
	struct ao2_container *ret_container;
	struct ao2_iterator *it_role_snapshots;
	struct channel_role_snapshot *role_snapshot;
	char *arg;

	if (!obj || ast_strlen_zero(role)) {
		return NULL;
	}

	ret_container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NUM_MULTI_CHANNEL_BLOB_BUCKETS,
		channel_snapshot_hash_cb, NULL, channel_snapshot_cmp_cb);
	if (!ret_container) {
		return NULL;
	}

	arg = ast_strdupa(role);
	it_role_snapshots = ao2_callback(obj->channel_snapshots,
		OBJ_MULTIPLE | OBJ_SEARCH_KEY, channel_role_cmp_cb, arg);
	if (!it_role_snapshots) {
		ao2_ref(ret_container, -1);
		return NULL;
	}

	while ((role_snapshot = ao2_iterator_next(it_role_snapshots))) {
		ao2_link(ret_container, role_snapshot->snapshot);
		ao2_ref(role_snapshot, -1);
	}
	ao2_iterator_destroy(it_role_snapshots);

	return ret_container;
}

struct ast_json *ast_multi_channel_blob_get_json(struct ast_multi_channel_blob *obj)
{
	if (!obj) {
		return NULL;
	}
	return obj->blob;
}

void ast_channel_stage_snapshot(struct ast_channel *chan)
{
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_SNAPSHOT_STAGE);
}

void ast_channel_stage_snapshot_done(struct ast_channel *chan)
{
	ast_clear_flag(ast_channel_flags(chan), AST_FLAG_SNAPSHOT_STAGE);
	ast_channel_publish_snapshot(chan);
}

void ast_channel_snapshot_invalidate_segment(struct ast_channel *chan,
	enum ast_channel_snapshot_segment_invalidation segment)
{
	ast_set_flag(ast_channel_snapshot_segment_flags(chan), segment);
}

void ast_channel_publish_snapshot(struct ast_channel *chan)
{
	struct ast_channel_snapshot_update *update;
	struct stasis_message *message;

	if (!ast_channel_snapshot_type()) {
		return;
	}

	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_SNAPSHOT_STAGE)) {
		return;
	}

	update = channel_snapshot_update_create(chan);
	if (!update) {
		return;
	}

	/* If an old snapshot exists and is the same as this newly created one don't bother
	 * raising a message as it hasn't changed.
	 */
	if (update->old_snapshot && !memcmp(update->old_snapshot, update->new_snapshot, sizeof(struct ast_channel_snapshot))) {
		ao2_ref(update, -1);
		return;
	}

	message = stasis_message_create(ast_channel_snapshot_type(), update);
	/* In the success path message holds a reference to update so it will be valid
	 * for the lifetime of this function until the end.
	 */
	ao2_ref(update, -1);
	if (!message) {
		return;
	}

	/* We lock these ourselves so that the update is atomic and there isn't time where a
	 * snapshot is not in the cache.
	 */
	ao2_wrlock(channel_cache);
	if (update->old_snapshot) {
		ao2_unlink_flags(channel_cache, update->old_snapshot, OBJ_NOLOCK);
	}
	ao2_link_flags(channel_cache, update->new_snapshot, OBJ_NOLOCK);
	ao2_unlock(channel_cache);

	/* The same applies here. */
	ao2_wrlock(channel_cache_by_name);
	if (update->old_snapshot) {
		ao2_unlink_flags(channel_cache_by_name, update->old_snapshot, OBJ_NOLOCK);
	}
	ao2_link_flags(channel_cache_by_name, update->new_snapshot, OBJ_NOLOCK);
	ao2_unlock(channel_cache_by_name);

	ast_channel_snapshot_set(chan, update->new_snapshot);

	/* As this is now the new snapshot any existing invalidated segments have been
	 * created fresh and are up to date.
	 */
	ast_clear_flag(ast_channel_snapshot_segment_flags(chan), AST_FLAGS_ALL);

	ast_assert(ast_channel_topic(chan) != NULL);
	stasis_publish(ast_channel_topic(chan), message);
	ao2_ref(message, -1);
}

void ast_channel_publish_cached_blob(struct ast_channel *chan, struct stasis_message_type *type, struct ast_json *blob)
{
	struct stasis_message *message;

	if (!blob) {
		blob = ast_json_null();
	}

	message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(chan), type, blob);
	if (message) {
		stasis_publish(ast_channel_topic(chan), message);
		ao2_ref(message, -1);
	}
}

void ast_channel_publish_blob(struct ast_channel *chan, struct stasis_message_type *type, struct ast_json *blob)
{
	struct stasis_message *message;

	if (!blob) {
		blob = ast_json_null();
	}

	message = ast_channel_blob_create(chan, type, blob);
	if (message) {
		stasis_publish(ast_channel_topic(chan), message);
		ao2_ref(message, -1);
	}
}

void ast_channel_publish_varset(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_json *blob;
	enum ast_utf8_replace_result result;
	char *new_value = NULL;
	size_t new_value_size = 0;

	ast_assert(name != NULL);
	ast_assert(value != NULL);

	/*
	 * Call with new-value == NULL to just check for invalid UTF-8
	 * sequences and get size of buffer needed.
	 */
	result = ast_utf8_replace_invalid_chars(new_value, &new_value_size,
			value, strlen(value));

	if (result == AST_UTF8_REPLACE_VALID) {
		/*
		 * If there were no invalid sequences, we can use
		 * the value directly.
		 */
		new_value = (char *)value;
	} else {
		/*
		 * If there were invalid sequences, we need to replace
		 * them with the UTF-8 U+FFFD replacement character.
		 */
		new_value = ast_alloca(new_value_size);

		result = ast_utf8_replace_invalid_chars(new_value, &new_value_size,
			value, strlen(value));

		ast_log(LOG_WARNING, "%s: The contents of variable '%s' had invalid UTF-8 sequences which were replaced",
			ast_channel_name(chan), name);
	}

	blob = ast_json_pack("{s: s, s: s}",
			     "variable", name,
			     "value", new_value);
	if (!blob) {
		ast_log(LOG_ERROR, "Error creating message\n");
		return;
	}

	/*! If there are manager variables, force a cache update */
	if (chan && ast_channel_has_manager_vars()) {
		ast_channel_publish_snapshot(chan);
	}

	/* This function is NULL safe for global variables */
	ast_channel_publish_blob(chan, ast_channel_varset_type(), blob);
	ast_json_unref(blob);
}

static struct ast_manager_event_blob *varset_to_ami(struct stasis_message *msg)
{
	struct ast_str *channel_event_string;
	struct ast_channel_blob *obj = stasis_message_data(msg);
	const char *variable =
		ast_json_string_get(ast_json_object_get(obj->blob, "variable"));
	char *value;
	struct ast_manager_event_blob *ev;

	value = ast_escape_c_alloc(ast_json_string_get(ast_json_object_get(obj->blob,
		"value")));
	if (!value) {
		return NULL;
	}

	if (obj->snapshot) {
		channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);
	} else {
		channel_event_string = ast_str_create(35);
		ast_str_set(&channel_event_string, 0,
			"Channel: none\r\n"
			"Uniqueid: none\r\n");
	}
	if (!channel_event_string) {
		ast_free(value);
		return NULL;
	}

	ev = ast_manager_event_blob_create(EVENT_FLAG_DIALPLAN, "VarSet",
		"%s"
		"Variable: %s\r\n"
		"Value: %s\r\n",
		ast_str_buffer(channel_event_string), variable, value);
	ast_free(channel_event_string);
	ast_free(value);
	return ev;
}

static struct ast_manager_event_blob *agent_login_to_ami(struct stasis_message *msg)
{
	struct ast_str *channel_string;
	struct ast_channel_blob *obj = stasis_message_data(msg);
	const char *agent = ast_json_string_get(ast_json_object_get(obj->blob, "agent"));
	struct ast_manager_event_blob *ev;

	channel_string = ast_manager_build_channel_state_string(obj->snapshot);
	if (!channel_string) {
		return NULL;
	}

	ev = ast_manager_event_blob_create(EVENT_FLAG_AGENT, "AgentLogin",
		"%s"
		"Agent: %s\r\n",
		ast_str_buffer(channel_string), agent);
	ast_free(channel_string);
	return ev;
}

static struct ast_manager_event_blob *agent_logoff_to_ami(struct stasis_message *msg)
{
	struct ast_str *channel_string;
	struct ast_channel_blob *obj = stasis_message_data(msg);
	const char *agent = ast_json_string_get(ast_json_object_get(obj->blob, "agent"));
	long logintime = ast_json_integer_get(ast_json_object_get(obj->blob, "logintime"));
	struct ast_manager_event_blob *ev;

	channel_string = ast_manager_build_channel_state_string(obj->snapshot);
	if (!channel_string) {
		return NULL;
	}

	ev = ast_manager_event_blob_create(EVENT_FLAG_AGENT, "AgentLogoff",
		"%s"
		"Agent: %s\r\n"
		"Logintime: %ld\r\n",
		ast_str_buffer(channel_string), agent, logintime);
	ast_free(channel_string);
	return ev;
}

struct ast_json *ast_channel_snapshot_to_json(
	const struct ast_channel_snapshot *snapshot,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_json *json_chan;

	if (snapshot == NULL
		|| (sanitize
			&& sanitize->channel_snapshot
			&& sanitize->channel_snapshot(snapshot))) {
		return NULL;
	}

	json_chan = ast_json_pack(
		/* Broken up into groups for readability */
		"{ s: s, s: s, s: s, s: s,"
		"  s: o, s: o, s: s,"
		"  s: o, s: o, s: s }",
		/* First line */
		"id", snapshot->base->uniqueid,
		"name", snapshot->base->name,
		"state", ast_state2str(snapshot->state),
		"protocol_id", snapshot->base->protocol_id,
		/* Second line */
		"caller", ast_json_name_number(
			snapshot->caller->name, snapshot->caller->number),
		"connected", ast_json_name_number(
			snapshot->connected->name, snapshot->connected->number),
		"accountcode", snapshot->base->accountcode,
		/* Third line */
		"dialplan", ast_json_dialplan_cep_app(
			snapshot->dialplan->context, snapshot->dialplan->exten, snapshot->dialplan->priority,
			snapshot->dialplan->appl, snapshot->dialplan->data),
		"creationtime", ast_json_timeval(snapshot->base->creationtime, NULL),
		"language", snapshot->base->language);

	if (snapshot->ari_vars && !AST_LIST_EMPTY(snapshot->ari_vars)) {
		ast_json_object_set(json_chan, "channelvars", ast_json_channel_vars(snapshot->ari_vars));
	}

	return json_chan;
}

int ast_channel_snapshot_cep_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot)
{
	ast_assert(old_snapshot != NULL);
	ast_assert(new_snapshot != NULL);

	/* We actually get some snapshots with CEP set, but before the
	 * application is set. Since empty application is invalid, we treat
	 * setting the application from nothing as a CEP change.
	 */
	if (ast_strlen_zero(old_snapshot->dialplan->appl) &&
	    !ast_strlen_zero(new_snapshot->dialplan->appl)) {
		return 0;
	}

	return old_snapshot->dialplan->priority == new_snapshot->dialplan->priority &&
		strcmp(old_snapshot->dialplan->context, new_snapshot->dialplan->context) == 0 &&
		strcmp(old_snapshot->dialplan->exten, new_snapshot->dialplan->exten) == 0;
}

int ast_channel_snapshot_caller_id_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot)
{
	ast_assert(old_snapshot != NULL);
	ast_assert(new_snapshot != NULL);
	return strcmp(old_snapshot->caller->number, new_snapshot->caller->number) == 0 &&
		strcmp(old_snapshot->caller->name, new_snapshot->caller->name) == 0;
}

int ast_channel_snapshot_connected_line_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot)
{
	ast_assert(old_snapshot != NULL);
	ast_assert(new_snapshot != NULL);
	return strcmp(old_snapshot->connected->number, new_snapshot->connected->number) == 0 &&
		strcmp(old_snapshot->connected->name, new_snapshot->connected->name) == 0;
}

static struct ast_json *channel_blob_to_json(
	struct stasis_message *message,
	const char *type,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_json *to_json;
	struct ast_channel_blob *channel_blob = stasis_message_data(message);
	struct ast_json *blob = channel_blob->blob;
	struct ast_channel_snapshot *snapshot = channel_blob->snapshot;
	const struct timeval *tv = stasis_message_timestamp(message);
	int res = 0;

	if (blob == NULL || ast_json_is_null(blob)) {
		to_json = ast_json_object_create();
	} else {
		/* blobs are immutable, so shallow copies are fine */
		to_json = ast_json_copy(blob);
	}
	if (!to_json) {
		return NULL;
	}

	res |= ast_json_object_set(to_json, "type", ast_json_string_create(type));
	res |= ast_json_object_set(to_json, "timestamp",
		ast_json_timeval(*tv, NULL));

	/* For global channel messages, the snapshot is optional */
	if (snapshot) {
		struct ast_json *json_channel;

		json_channel = ast_channel_snapshot_to_json(snapshot, sanitize);
		if (!json_channel) {
			ast_json_unref(to_json);
			return NULL;
		}

		res |= ast_json_object_set(to_json, "channel", json_channel);
	}

	if (res != 0) {
		ast_json_unref(to_json);
		return NULL;
	}

	return to_json;
}

static struct ast_json *dtmf_end_to_json(
	struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_channel_blob *channel_blob = stasis_message_data(message);
	struct ast_json *blob = channel_blob->blob;
	struct ast_channel_snapshot *snapshot = channel_blob->snapshot;
	const char *direction =
		ast_json_string_get(ast_json_object_get(blob, "direction"));
	const char *digit =
		ast_json_string_get(ast_json_object_get(blob, "digit"));
	long duration_ms =
		ast_json_integer_get(ast_json_object_get(blob, "duration_ms"));
	const struct timeval *tv = stasis_message_timestamp(message);
	struct ast_json *json_channel;

	/* Only present received DTMF end events as JSON */
	if (strcasecmp("Received", direction) != 0) {
		return NULL;
	}

	json_channel = ast_channel_snapshot_to_json(snapshot, sanitize);
	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: s, s: I, s: o}",
		"type", "ChannelDtmfReceived",
		"timestamp", ast_json_timeval(*tv, NULL),
		"digit", digit,
		"duration_ms", (ast_json_int_t)duration_ms,
		"channel", json_channel);
}

static struct ast_json *varset_to_json(
	struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	return channel_blob_to_json(message, "ChannelVarset", sanitize);
}

static struct ast_json *hangup_request_to_json(
	struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	return channel_blob_to_json(message, "ChannelHangupRequest", sanitize);
}

static struct ast_json *dial_to_json(
	struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_multi_channel_blob *payload = stasis_message_data(message);
	struct ast_json *blob = ast_multi_channel_blob_get_json(payload);
	const char *dialstatus =
		ast_json_string_get(ast_json_object_get(blob, "dialstatus"));
	const char *forward =
		ast_json_string_get(ast_json_object_get(blob, "forward"));
	const char *dialstring =
		ast_json_string_get(ast_json_object_get(blob, "dialstring"));
	struct ast_json *caller_json = ast_channel_snapshot_to_json(ast_multi_channel_blob_get_channel(payload, "caller"), sanitize);
	struct ast_json *peer_json = ast_channel_snapshot_to_json(ast_multi_channel_blob_get_channel(payload, "peer"), sanitize);
	struct ast_json *forwarded_json = ast_channel_snapshot_to_json(ast_multi_channel_blob_get_channel(payload, "forwarded"), sanitize);
	struct ast_json *json;
	const struct timeval *tv = stasis_message_timestamp(message);
	int res = 0;

	json = ast_json_pack("{s: s, s: o, s: s, s: s, s: s}",
		"type", "Dial",
		"timestamp", ast_json_timeval(*tv, NULL),
		"dialstatus", dialstatus,
		"forward", forward,
		"dialstring", dialstring);
	if (!json) {
		ast_json_unref(caller_json);
		ast_json_unref(peer_json);
		ast_json_unref(forwarded_json);
		return NULL;
	}

	if (caller_json) {
		res |= ast_json_object_set(json, "caller", caller_json);
	}
	if (peer_json) {
		res |= ast_json_object_set(json, "peer", peer_json);
	}
	if (forwarded_json) {
		res |= ast_json_object_set(json, "forwarded", forwarded_json);
	}

	if (res) {
		ast_json_unref(json);
		return NULL;
	}

	return json;
}

static struct ast_manager_event_blob *talking_start_to_ami(struct stasis_message *msg)
{
	struct ast_str *channel_string;
	struct ast_channel_blob *obj = stasis_message_data(msg);
	struct ast_manager_event_blob *blob;

	channel_string = ast_manager_build_channel_state_string(obj->snapshot);
	if (!channel_string) {
		return NULL;
	}

	blob = ast_manager_event_blob_create(EVENT_FLAG_CALL, "ChannelTalkingStart",
	                                     "%s", ast_str_buffer(channel_string));
	ast_free(channel_string);

	return blob;
}

static struct ast_json *talking_start_to_json(struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	return channel_blob_to_json(message, "ChannelTalkingStarted", sanitize);
}

static struct ast_manager_event_blob *talking_stop_to_ami(struct stasis_message *msg)
{
	struct ast_str *channel_string;
	struct ast_channel_blob *obj = stasis_message_data(msg);
	int duration = ast_json_integer_get(ast_json_object_get(obj->blob, "duration"));
	struct ast_manager_event_blob *blob;

	channel_string = ast_manager_build_channel_state_string(obj->snapshot);
	if (!channel_string) {
		return NULL;
	}

	blob = ast_manager_event_blob_create(EVENT_FLAG_CALL, "ChannelTalkingStop",
	                                     "%s"
	                                     "Duration: %d\r\n",
	                                     ast_str_buffer(channel_string),
	                                     duration);
	ast_free(channel_string);

	return blob;
}

static struct ast_json *talking_stop_to_json(struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	return channel_blob_to_json(message, "ChannelTalkingFinished", sanitize);
}

static struct ast_json *hold_to_json(struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_channel_blob *channel_blob = stasis_message_data(message);
	struct ast_json *blob = channel_blob->blob;
	struct ast_channel_snapshot *snapshot = channel_blob->snapshot;
	const char *musicclass = ast_json_string_get(ast_json_object_get(blob, "musicclass"));
	const struct timeval *tv = stasis_message_timestamp(message);
	struct ast_json *json_channel;

	json_channel = ast_channel_snapshot_to_json(snapshot, sanitize);
	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: s, s: o}",
		"type", "ChannelHold",
		"timestamp", ast_json_timeval(*tv, NULL),
		"musicclass", S_OR(musicclass, "N/A"),
		"channel", json_channel);
}

static struct ast_json *unhold_to_json(struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_channel_blob *channel_blob = stasis_message_data(message);
	struct ast_channel_snapshot *snapshot = channel_blob->snapshot;
	const struct timeval *tv = stasis_message_timestamp(message);
	struct ast_json *json_channel;

	json_channel = ast_channel_snapshot_to_json(snapshot, sanitize);
	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: o}",
		"type", "ChannelUnhold",
		"timestamp", ast_json_timeval(*tv, NULL),
		"channel", json_channel);
}

/*!
 * @{ \brief Define channel message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_channel_snapshot_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_dial_type,
	.to_json = dial_to_json,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_varset_type,
	.to_ami = varset_to_ami,
	.to_json = varset_to_json,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_hangup_request_type,
	.to_json = hangup_request_to_json,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_masquerade_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_dtmf_begin_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_dtmf_end_type,
	.to_json = dtmf_end_to_json,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_hold_type,
	.to_json = hold_to_json,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_unhold_type,
	.to_json = unhold_to_json,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_flash_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_wink_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_chanspy_start_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_chanspy_stop_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_fax_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_hangup_handler_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_moh_start_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_moh_stop_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_monitor_start_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_monitor_stop_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_mixmonitor_start_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_mixmonitor_stop_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_mixmonitor_mute_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_agent_login_type,
	.to_ami = agent_login_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_agent_logoff_type,
	.to_ami = agent_logoff_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_talking_start,
	.to_ami = talking_start_to_ami,
	.to_json = talking_start_to_json,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_talking_stop,
	.to_ami = talking_stop_to_ami,
	.to_json = talking_stop_to_json,
	);

/*! @} */

static void stasis_channels_cleanup(void)
{
	ao2_cleanup(channel_topic_all);
	channel_topic_all = NULL;
	ao2_cleanup(channel_cache);
	channel_cache = NULL;
	ao2_cleanup(channel_cache_by_name);
	channel_cache_by_name = NULL;

	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_snapshot_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_dial_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_varset_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_hangup_request_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_masquerade_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_dtmf_begin_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_dtmf_end_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_flash_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_wink_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_hold_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_unhold_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_chanspy_start_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_chanspy_stop_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_fax_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_hangup_handler_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_moh_start_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_moh_stop_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_monitor_start_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_monitor_stop_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_mixmonitor_start_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_mixmonitor_stop_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_mixmonitor_mute_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_agent_login_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_agent_logoff_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_talking_start);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_talking_stop);
}

int ast_stasis_channels_init(void)
{
	int res = 0;

	ast_register_cleanup(stasis_channels_cleanup);

	channel_topic_all = stasis_topic_create("channel:all");
	if (!channel_topic_all) {
		return -1;
	}

	channel_cache = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK,
		0, AST_NUM_CHANNEL_BUCKETS, channel_snapshot_uniqueid_hash_cb,
		NULL, channel_snapshot_uniqueid_cmp_cb);
	if (!channel_cache) {
		return -1;
	}

	channel_cache_by_name = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK,
		0, AST_NUM_CHANNEL_BUCKETS, channel_snapshot_hash_cb,
		NULL, channel_snapshot_cmp_cb);
	if (!channel_cache_by_name) {
		return -1;
	}

	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_agent_login_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_agent_logoff_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_snapshot_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_dial_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_varset_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_hangup_request_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_masquerade_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_dtmf_begin_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_dtmf_end_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_flash_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_wink_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_hold_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_unhold_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_chanspy_start_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_chanspy_stop_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_fax_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_hangup_handler_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_moh_start_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_moh_stop_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_monitor_start_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_monitor_stop_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_mixmonitor_start_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_mixmonitor_stop_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_mixmonitor_mute_type);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_talking_start);
	res |= STASIS_MESSAGE_TYPE_INIT(ast_channel_talking_stop);

	return res;
}

/*!
 * \internal
 * \brief A list element for the dial_masquerade_datastore -- stores data about a dialed peer
 */
struct dial_target {
	/*! Called party channel. */
	struct ast_channel *peer;
	/*! Dialstring used to call the peer. */
	char *dialstring;
	/*! Next entry in the list. */
	AST_LIST_ENTRY(dial_target) list;
};

static void dial_target_free(struct dial_target *doomed)
{
	if (!doomed) {
		return;
	}
	ast_free(doomed->dialstring);
	ast_channel_cleanup(doomed->peer);
	ast_free(doomed);
}

/*!
 * \internal
 * \brief Datastore used for advancing dial state in the case of a masquerade
 *        against a channel in the process of dialing.
 */
struct dial_masquerade_datastore {
	/*! Calling party channel. */
	struct ast_channel *caller;
	/*! List of called peers. */
	AST_LIST_HEAD_NOLOCK(, dial_target) dialed_peers;
};

static void dial_masquerade_datastore_cleanup(struct dial_masquerade_datastore *masq_data)
{
	struct dial_target *cur;

	while ((cur = AST_LIST_REMOVE_HEAD(&masq_data->dialed_peers, list))) {
		dial_target_free(cur);
	}
}

static void dial_masquerade_datastore_remove_chan(struct dial_masquerade_datastore *masq_data, struct ast_channel *chan)
{
	struct dial_target *cur;

	ao2_lock(masq_data);
	if (masq_data->caller == chan) {
		dial_masquerade_datastore_cleanup(masq_data);
	} else {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&masq_data->dialed_peers, cur, list) {
			if (cur->peer == chan) {
				AST_LIST_REMOVE_CURRENT(list);
				dial_target_free(cur);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}
	ao2_unlock(masq_data);
}

static void dial_masquerade_datastore_dtor(void *vdoomed)
{
	dial_masquerade_datastore_cleanup(vdoomed);
}

static struct dial_masquerade_datastore *dial_masquerade_datastore_alloc(void)
{
	struct dial_masquerade_datastore *masq_data;

	masq_data = ao2_alloc(sizeof(struct dial_masquerade_datastore),
		dial_masquerade_datastore_dtor);
	if (!masq_data) {
		return NULL;
	}
	AST_LIST_HEAD_INIT_NOLOCK(&masq_data->dialed_peers);
	return masq_data;
}

/*!
 * \internal
 * \brief Datastore destructor for dial_masquerade_datastore
 */
static void dial_masquerade_datastore_destroy(void *data)
{
	ao2_ref(data, -1);
}

/*!
 * \internal
 * \brief Datastore destructor for dial_masquerade_datastore
 */
static void dial_masquerade_caller_datastore_destroy(void *data)
{
	dial_masquerade_datastore_cleanup(data);
	ao2_ref(data, -1);
}

static struct ast_datastore *dial_masquerade_datastore_find(struct ast_channel *chan);

static void dial_masquerade_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct dial_masquerade_datastore *masq_data = data;
	struct dial_target *cur;
	struct ast_datastore *datastore;

	ao2_lock(masq_data);
	if (!masq_data->caller) {
		/* Nothing to do but remove the datastore */
	} else if (masq_data->caller == old_chan) {
		/* The caller channel is being masqueraded out. */
		ast_debug(1, "Caller channel %s being masqueraded out to %s (is_empty:%d)\n",
			ast_channel_name(new_chan), ast_channel_name(old_chan),
			AST_LIST_EMPTY(&masq_data->dialed_peers));
		AST_LIST_TRAVERSE(&masq_data->dialed_peers, cur, list) {
			ast_channel_publish_dial_internal(new_chan, cur->peer, NULL,
				cur->dialstring, "NOANSWER", NULL);
			ast_channel_publish_dial_internal(old_chan, cur->peer, NULL,
				cur->dialstring, NULL, NULL);
		}
		dial_masquerade_datastore_cleanup(masq_data);
	} else {
		/* One of the peer channels is being masqueraded out. */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&masq_data->dialed_peers, cur, list) {
			if (cur->peer == old_chan) {
				ast_debug(1, "Peer channel %s being masqueraded out to %s\n",
					ast_channel_name(new_chan), ast_channel_name(old_chan));
				ast_channel_publish_dial_internal(masq_data->caller, new_chan, NULL,
					cur->dialstring, "CANCEL", NULL);
				ast_channel_publish_dial_internal(masq_data->caller, old_chan, NULL,
					cur->dialstring, NULL, NULL);

				AST_LIST_REMOVE_CURRENT(list);
				dial_target_free(cur);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}
	ao2_unlock(masq_data);

	/* Remove the datastore from the channel. */
	datastore = dial_masquerade_datastore_find(old_chan);
	if (!datastore) {
		return;
	}
	ast_channel_datastore_remove(old_chan, datastore);
	ast_datastore_free(datastore);
}

/*!
 * \internal
 * \brief Primary purpose for dial_masquerade_datastore, publishes
 *        the channel dial event needed to set the incoming channel into the
 *        dial state during a masquerade.
 * \param data pointer to the dial_masquerade_datastore
 * \param old_chan Channel being replaced
 * \param new_chan Channel being pushed to dial mode
 */
static void dial_masquerade_breakdown(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct dial_masquerade_datastore *masq_data = data;
	struct dial_target *cur;

	ao2_lock(masq_data);

	if (!masq_data->caller) {
		ao2_unlock(masq_data);
		return;
	}

	if (masq_data->caller == new_chan) {
		/*
		 * The caller channel is being masqueraded into.
		 * The masquerade is likely because of a blonde transfer.
		 */
		ast_debug(1, "Caller channel %s being masqueraded into by %s (is_empty:%d)\n",
			ast_channel_name(old_chan), ast_channel_name(new_chan),
			AST_LIST_EMPTY(&masq_data->dialed_peers));
		AST_LIST_TRAVERSE(&masq_data->dialed_peers, cur, list) {
			ast_channel_publish_dial_internal(old_chan, cur->peer, NULL,
				cur->dialstring, "NOANSWER", NULL);
			ast_channel_publish_dial_internal(new_chan, cur->peer, NULL,
				cur->dialstring, NULL, NULL);
		}

		ao2_unlock(masq_data);
		return;
	}

	/*
	 * One of the peer channels is being masqueraded into.
	 * The masquerade is likely because of a call pickup.
	 */
	AST_LIST_TRAVERSE(&masq_data->dialed_peers, cur, list) {
		if (cur->peer == new_chan) {
			ast_debug(1, "Peer channel %s being masqueraded into by %s\n",
				ast_channel_name(old_chan), ast_channel_name(new_chan));
			ast_channel_publish_dial_internal(masq_data->caller, old_chan, NULL,
				cur->dialstring, "CANCEL", NULL);
			ast_channel_publish_dial_internal(masq_data->caller, new_chan, NULL,
				cur->dialstring, NULL, NULL);
			break;
		}
	}

	ao2_unlock(masq_data);
}

static const struct ast_datastore_info dial_masquerade_info = {
	.type = "stasis-chan-dial-masq",
	.destroy = dial_masquerade_datastore_destroy,
	.chan_fixup = dial_masquerade_fixup,
	.chan_breakdown = dial_masquerade_breakdown,
};

static const struct ast_datastore_info dial_masquerade_caller_info = {
	.type = "stasis-chan-dial-masq",
	.destroy = dial_masquerade_caller_datastore_destroy,
	.chan_fixup = dial_masquerade_fixup,
	.chan_breakdown = dial_masquerade_breakdown,
};

/*!
 * \internal
 * \brief Find the dial masquerade datastore on the given channel.
 *
 * \param chan Channel a datastore data is wanted from
 *
 * \return A pointer to the datastore if it exists.
 */
static struct ast_datastore *dial_masquerade_datastore_find(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = ast_channel_datastore_find(chan, &dial_masquerade_info, NULL);
	if (!datastore) {
		datastore = ast_channel_datastore_find(chan, &dial_masquerade_caller_info, NULL);
	}

	return datastore;
}

/*!
 * \internal
 * \brief Add the dial masquerade datastore to a channel.
 *
 * \param chan Channel to setup dial masquerade datastore on.
 * \param masq_data NULL to setup caller datastore otherwise steals the ref on success.
 *
 * \retval masq_data given or created on success.
 *         (A ref is not returned but can be obtained before chan is unlocked.)
 * \retval NULL on error.  masq_data ref is not stolen.
 */
static struct dial_masquerade_datastore *dial_masquerade_datastore_add(
	struct ast_channel *chan, struct dial_masquerade_datastore *masq_data)
{
	struct ast_datastore *datastore;

	datastore = ast_datastore_alloc(!masq_data ? &dial_masquerade_caller_info : &dial_masquerade_info, NULL);
	if (!datastore) {
		return NULL;
	}

	if (!masq_data) {
		masq_data = dial_masquerade_datastore_alloc();
		if (!masq_data) {
			ast_datastore_free(datastore);
			return NULL;
		}
		masq_data->caller = chan;
	}

	datastore->data = masq_data;
	ast_channel_datastore_add(chan, datastore);

	return masq_data;
}

static int set_dial_masquerade(struct ast_channel *caller, struct ast_channel *peer, const char *dialstring)
{
	struct ast_datastore *datastore;
	struct dial_masquerade_datastore *masq_data;
	struct dial_target *target;

	/* Find or create caller datastore */
	datastore = dial_masquerade_datastore_find(caller);
	if (!datastore) {
		masq_data = dial_masquerade_datastore_add(caller, NULL);
	} else {
		masq_data = datastore->data;
	}
	if (!masq_data) {
		return -1;
	}
	ao2_ref(masq_data, +1);

	/*
	 * Someone likely forgot to do an ast_channel_publish_dial()
	 * or ast_channel_publish_dial_forward() with a final dial
	 * status on the channel.
	 */
	ast_assert(masq_data->caller == caller);

	/* Create peer target to put into datastore */
	target = ast_calloc(1, sizeof(*target));
	if (!target) {
		ao2_ref(masq_data, -1);
		return -1;
	}
	if (dialstring) {
		target->dialstring = ast_strdup(dialstring);
		if (!target->dialstring) {
			ast_free(target);
			ao2_ref(masq_data, -1);
			return -1;
		}
	}
	target->peer = ast_channel_ref(peer);

	/* Put peer target into datastore */
	ao2_lock(masq_data);
	dial_masquerade_datastore_remove_chan(masq_data, peer);
	AST_LIST_INSERT_HEAD(&masq_data->dialed_peers, target, list);
	ao2_unlock(masq_data);

	datastore = dial_masquerade_datastore_find(peer);
	if (datastore) {
		if (datastore->data == masq_data) {
			/*
			 * Peer already had the datastore for this dial masquerade.
			 * This was a redundant peer dial masquerade setup.
			 */
			ao2_ref(masq_data, -1);
			return 0;
		}

		/* Something is wrong.  Try to fix if the assert doesn't abort. */
		ast_assert(0);

		/* Remove the stale dial masquerade datastore */
		dial_masquerade_datastore_remove_chan(datastore->data, peer);
		ast_channel_datastore_remove(peer, datastore);
		ast_datastore_free(datastore);
	}

	/* Create the peer dial masquerade datastore */
	if (dial_masquerade_datastore_add(peer, masq_data)) {
		/* Success */
		return 0;
	}

	/* Failed to create the peer datastore */
	dial_masquerade_datastore_remove_chan(masq_data, peer);
	ao2_ref(masq_data, -1);
	return -1;
}

static void remove_dial_masquerade(struct ast_channel *peer)
{
	struct ast_datastore *datastore;
	struct dial_masquerade_datastore *masq_data;

	datastore = dial_masquerade_datastore_find(peer);
	if (!datastore) {
		return;
	}

	masq_data = datastore->data;
	if (masq_data) {
		dial_masquerade_datastore_remove_chan(masq_data, peer);
	}

	ast_channel_datastore_remove(peer, datastore);
	ast_datastore_free(datastore);
}

static void remove_dial_masquerade_caller(struct ast_channel *caller)
{
	struct ast_datastore *datastore;
	struct dial_masquerade_datastore *masq_data;

	datastore = dial_masquerade_datastore_find(caller);
	if (!datastore) {
		return;
	}

	masq_data = datastore->data;
	if (!masq_data || !AST_LIST_EMPTY(&masq_data->dialed_peers)) {
		return;
	}

	dial_masquerade_datastore_remove_chan(masq_data, caller);

	ast_channel_datastore_remove(caller, datastore);
	ast_datastore_free(datastore);
}
