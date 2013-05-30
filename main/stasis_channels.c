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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis.h"
#include "asterisk/astobj2.h"
#include "asterisk/stasis_channels.h"

#define NUM_MULTI_CHANNEL_BLOB_BUCKETS 7

/*!
 * @{ \brief Define channel message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_channel_snapshot_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_dial_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_varset_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_user_event_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_hangup_request_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_dtmf_begin_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_dtmf_end_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_hold_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_unhold_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_chanspy_start_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_chanspy_stop_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_fax_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_hangup_handler_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_moh_start_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_moh_stop_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_monitor_start_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_monitor_stop_type);
/*! @} */

/*! \brief Topic for all channels */
struct stasis_topic *channel_topic_all;

/*! \brief Caching topic for all channels */
struct stasis_caching_topic *channel_topic_all_cached;

struct stasis_topic *ast_channel_topic_all(void)
{
	return channel_topic_all;
}

struct stasis_caching_topic *ast_channel_topic_all_cached(void)
{
	return channel_topic_all_cached;
}

static const char *channel_snapshot_get_id(struct stasis_message *message)
{
	struct ast_channel_snapshot *snapshot;
	if (ast_channel_snapshot_type() != stasis_message_type(message)) {
		return NULL;
	}
	snapshot = stasis_message_data(message);
	return snapshot->uniqueid;
}

/*! \internal \brief Hash function for \ref ast_channel_snapshot objects */
static int channel_snapshot_hash_cb(const void *obj, const int flags)
{
	const struct ast_channel_snapshot *snapshot = obj;
	const char *name = (flags & OBJ_KEY) ? obj : snapshot->name;
	return ast_str_case_hash(name);
}

/*! \internal \brief Comparison function for \ref ast_channel_snapshot objects */
static int channel_snapshot_cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_channel_snapshot *left = obj;
	struct ast_channel_snapshot *right = arg;
	const char *match = (flags & OBJ_KEY) ? arg : right->name;
	return strcasecmp(left->name, match) ? 0 : (CMP_MATCH | CMP_STOP);
}

static void channel_snapshot_dtor(void *obj)
{
	struct ast_channel_snapshot *snapshot = obj;
	ast_string_field_free_memory(snapshot);
}

struct ast_channel_snapshot *ast_channel_snapshot_create(struct ast_channel *chan)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	snapshot = ao2_alloc(sizeof(*snapshot), channel_snapshot_dtor);
	if (!snapshot || ast_string_field_init(snapshot, 1024)) {
		return NULL;
	}

	ast_string_field_set(snapshot, name, ast_channel_name(chan));
	ast_string_field_set(snapshot, accountcode, ast_channel_accountcode(chan));
	ast_string_field_set(snapshot, peeraccount, ast_channel_peeraccount(chan));
	ast_string_field_set(snapshot, userfield, ast_channel_userfield(chan));
	ast_string_field_set(snapshot, uniqueid, ast_channel_uniqueid(chan));
	ast_string_field_set(snapshot, linkedid, ast_channel_linkedid(chan));
	ast_string_field_set(snapshot, parkinglot, ast_channel_parkinglot(chan));
	ast_string_field_set(snapshot, hangupsource, ast_channel_hangupsource(chan));
	if (ast_channel_appl(chan)) {
		ast_string_field_set(snapshot, appl, ast_channel_appl(chan));
	}
	if (ast_channel_data(chan)) {
		ast_string_field_set(snapshot, data, ast_channel_data(chan));
	}
	ast_string_field_set(snapshot, context, ast_channel_context(chan));
	ast_string_field_set(snapshot, exten, ast_channel_exten(chan));

	ast_string_field_set(snapshot, caller_name,
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, ""));
	ast_string_field_set(snapshot, caller_number,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, ""));

	ast_string_field_set(snapshot, connected_name,
		S_COR(ast_channel_connected(chan)->id.name.valid, ast_channel_connected(chan)->id.name.str, ""));
	ast_string_field_set(snapshot, connected_number,
		S_COR(ast_channel_connected(chan)->id.number.valid, ast_channel_connected(chan)->id.number.str, ""));
	ast_string_field_set(snapshot, language, ast_channel_language(chan));

	snapshot->creationtime = ast_channel_creationtime(chan);
	snapshot->state = ast_channel_state(chan);
	snapshot->priority = ast_channel_priority(chan);
	snapshot->amaflags = ast_channel_amaflags(chan);
	snapshot->hangupcause = ast_channel_hangupcause(chan);
	snapshot->flags = *ast_channel_flags(chan);
	snapshot->caller_pres = ast_party_id_presentation(&ast_channel_caller(chan)->id);

	snapshot->manager_vars = ast_channel_get_manager_vars(chan);

	ao2_ref(snapshot, +1);
	return snapshot;
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

void ast_channel_publish_dial(struct ast_channel *caller, struct ast_channel *peer, const char *dialstring, const char *dialstatus)
{
	RAII_VAR(struct ast_multi_channel_blob *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	struct ast_channel_snapshot *caller_snapshot;
	struct ast_channel_snapshot *peer_snapshot;

	ast_assert(peer != NULL);
	blob = ast_json_pack("{s: s, s: s}",
			     "dialstatus", S_OR(dialstatus, ""),
			     "dialstring", S_OR(dialstring, ""));
	if (!blob) {
		return;
	}
	payload = ast_multi_channel_blob_create(blob);
	if (!payload) {
		return;
	}

	if (caller) {
		caller_snapshot = ast_channel_snapshot_create(caller);
		if (!caller_snapshot) {
			return;
		}
		ast_multi_channel_blob_add_channel(payload, "caller", caller_snapshot);
	}

	peer_snapshot = ast_channel_snapshot_create(peer);
	if (!peer_snapshot) {
		return;
	}
	ast_multi_channel_blob_add_channel(payload, "peer", peer_snapshot);

	msg = stasis_message_create(ast_channel_dial_type(), payload);
	if (!msg) {
		return;
	}

	publish_message_for_channel_topics(msg, caller);
}

static struct stasis_message *create_channel_blob_message(struct ast_channel_snapshot *snapshot,
		struct stasis_message_type *type,
		struct ast_json *blob)

{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_blob *, obj, NULL, ao2_cleanup);

	if (blob == NULL) {
		blob = ast_json_null();
	}

	obj = ao2_alloc(sizeof(*obj), channel_blob_dtor);
	if (!obj) {
		return NULL;
	}

	if (snapshot) {
		obj->snapshot = snapshot;
		ao2_ref(obj->snapshot, +1);
	}
	obj->blob = ast_json_ref(blob);

	msg = stasis_message_create(type, obj);
	if (!msg) {
		return NULL;
	}

	ao2_ref(msg, +1);
	return msg;
}

struct stasis_message *ast_channel_blob_create_from_cache(const char *channel_id,
					       struct stasis_message_type *type,
					       struct ast_json *blob)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot,
			ast_channel_snapshot_get_latest(channel_id),
			ao2_cleanup);

	return create_channel_blob_message(snapshot, type, blob);
}

struct stasis_message *ast_channel_blob_create(struct ast_channel *chan,
	struct stasis_message_type *type, struct ast_json *blob)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	if (chan) {
		snapshot = ast_channel_snapshot_create(chan);
	}

	return create_channel_blob_message(snapshot, type, blob);
}

/*! \brief A channel snapshot wrapper object used in \ref ast_multi_channel_blob objects */
struct channel_role_snapshot {
	struct ast_channel_snapshot *snapshot;	/*!< A channel snapshot */
	char role[0];							/*!< The role assigned to the channel */
};

/*! \brief A multi channel blob data structure for multi_channel_blob stasis messages */
struct ast_multi_channel_blob {
	struct ao2_container *channel_snapshots;	/*!< A container holding the snapshots */
	struct ast_json *blob;						/*< A blob of JSON data */
};

/*! \internal \brief Standard comparison function for \ref channel_role_snapshot objects */
static int channel_role_single_cmp_cb(void *obj, void *arg, int flags)
{
	struct channel_role_snapshot *left = obj;
	struct channel_role_snapshot *right = arg;
	const char *match = (flags & OBJ_KEY) ? arg : right->role;
	return strcasecmp(left->role, match) ? 0 : (CMP_MATCH | CMP_STOP);
}

/*! \internal \brief Multi comparison function for \ref channel_role_snapshot objects */
static int channel_role_multi_cmp_cb(void *obj, void *arg, int flags)
{
	struct channel_role_snapshot *left = obj;
	struct channel_role_snapshot *right = arg;
	const char *match = (flags & OBJ_KEY) ? arg : right->role;
	return strcasecmp(left->role, match) ? 0 : (CMP_MATCH);
}

/*! \internal \brief Hash function for \ref channel_role_snapshot objects */
static int channel_role_hash_cb(const void *obj, const int flags)
{
	const struct channel_role_snapshot *snapshot = obj;
	const char *name = (flags & OBJ_KEY) ? obj : snapshot->role;
	return ast_str_case_hash(name);
}

/*! \internal \brief Destructor for \ref ast_multi_channel_blob objects */
static void multi_channel_blob_dtor(void *obj)
{
	struct ast_multi_channel_blob *multi_blob = obj;

	ao2_cleanup(multi_blob->channel_snapshots);
	ast_json_unref(multi_blob->blob);
}

struct ast_multi_channel_blob *ast_multi_channel_blob_create(struct ast_json *blob)
{
	RAII_VAR(struct ast_multi_channel_blob *, obj,
			ao2_alloc(sizeof(*obj), multi_channel_blob_dtor),
			ao2_cleanup);

	ast_assert(blob != NULL);

	if (!obj) {
		return NULL;
	}

	obj->channel_snapshots = ao2_container_alloc(NUM_MULTI_CHANNEL_BLOB_BUCKETS,
			channel_role_hash_cb, channel_role_single_cmp_cb);
	if (!obj->channel_snapshots) {
		return NULL;
	}

	obj->blob = ast_json_ref(blob);

	ao2_ref(obj, +1);
	return obj;
}

struct ast_channel_snapshot *ast_channel_snapshot_get_latest(const char *uniqueid)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	struct ast_channel_snapshot *snapshot;

	ast_assert(!ast_strlen_zero(uniqueid));

	message = stasis_cache_get(ast_channel_topic_all_cached(),
			ast_channel_snapshot_type(),
			uniqueid);
	if (!message) {
		return NULL;
	}

	snapshot = stasis_message_data(message);
	if (!snapshot) {
		return NULL;
	}
	ao2_ref(snapshot, +1);
	return snapshot;
}

static void channel_role_snapshot_dtor(void *obj)
{
	struct channel_role_snapshot *role_snapshot = obj;
	ao2_cleanup(role_snapshot->snapshot);
}

void ast_multi_channel_blob_add_channel(struct ast_multi_channel_blob *obj, const char *role, struct ast_channel_snapshot *snapshot)
{
	RAII_VAR(struct channel_role_snapshot *, role_snapshot, NULL, ao2_cleanup);
	int role_len = strlen(role) + 1;

	if (!obj || ast_strlen_zero(role) || !snapshot) {
		return;
	}

	role_snapshot = ao2_alloc(sizeof(*role_snapshot) + role_len, channel_role_snapshot_dtor);
	if (!role_snapshot) {
		return;
	}
	ast_copy_string(role_snapshot->role, role, role_len);
	role_snapshot->snapshot = snapshot;
	ao2_ref(role_snapshot->snapshot, +1);
	ao2_link(obj->channel_snapshots, role_snapshot);
}

struct ast_channel_snapshot *ast_multi_channel_blob_get_channel(struct ast_multi_channel_blob *obj, const char *role)
{
	struct channel_role_snapshot *role_snapshot;

	if (!obj || ast_strlen_zero(role)) {
		return NULL;
	}
	role_snapshot = ao2_find(obj->channel_snapshots, role, OBJ_KEY);
	/* Note that this function does not increase the ref count on snapshot */
	if (!role_snapshot) {
		return NULL;
	}
	ao2_ref(role_snapshot, -1);
	return role_snapshot->snapshot;
}

struct ao2_container *ast_multi_channel_blob_get_channels(struct ast_multi_channel_blob *obj, const char *role)
{
	RAII_VAR(struct ao2_container *, ret_container,
		ao2_container_alloc(NUM_MULTI_CHANNEL_BLOB_BUCKETS, channel_snapshot_hash_cb, channel_snapshot_cmp_cb),
		ao2_cleanup);
	struct ao2_iterator *it_role_snapshots;
	struct channel_role_snapshot *role_snapshot;
	char *arg;

	if (!obj || ast_strlen_zero(role) || !ret_container) {
		return NULL;
	}
	arg = ast_strdupa(role);

	it_role_snapshots = ao2_callback(obj->channel_snapshots, OBJ_MULTIPLE | OBJ_KEY, channel_role_multi_cmp_cb, arg);
	if (!it_role_snapshots) {
		return NULL;
	}

	while ((role_snapshot = ao2_iterator_next(it_role_snapshots))) {
		ao2_link(ret_container, role_snapshot->snapshot);
		ao2_ref(role_snapshot, -1);
	}
	ao2_iterator_destroy(it_role_snapshots);

	ao2_ref(ret_container, +1);
	return ret_container;
}

struct ast_json *ast_multi_channel_blob_get_json(struct ast_multi_channel_blob *obj)
{
	if (!obj) {
		return NULL;
	}
	return obj->blob;
}

void ast_channel_publish_snapshot(struct ast_channel *chan)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	snapshot = ast_channel_snapshot_create(chan);
	if (!snapshot) {
		return;
	}

	message = stasis_message_create(ast_channel_snapshot_type(), snapshot);
	if (!message) {
		return;
	}

	ast_assert(ast_channel_topic(chan) != NULL);
	stasis_publish(ast_channel_topic(chan), message);
}

void ast_channel_publish_varset(struct ast_channel *chan, const char *name, const char *value)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	ast_assert(name != NULL);
	ast_assert(value != NULL);

	blob = ast_json_pack("{s: s, s: s}",
			     "variable", name,
			     "value", value);
	if (!blob) {
		ast_log(LOG_ERROR, "Error creating message\n");
		return;
	}

	msg = ast_channel_blob_create(chan, ast_channel_varset_type(),
		ast_json_ref(blob));

	if (!msg) {
		return;
	}

	publish_message_for_channel_topics(msg, chan);
}

void ast_publish_channel_state(struct ast_channel *chan)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	ast_assert(chan != NULL);
	if (!chan) {
		return;
	}

	snapshot = ast_channel_snapshot_create(chan);
	if (!snapshot) {
		return;
	}

	message = stasis_message_create(ast_channel_snapshot_type(), snapshot);
	if (!message) {
		return;
	}

	ast_assert(ast_channel_topic(chan) != NULL);
	stasis_publish(ast_channel_topic(chan), message);
}

struct ast_json *ast_channel_snapshot_to_json(const struct ast_channel_snapshot *snapshot)
{
	RAII_VAR(struct ast_json *, json_chan, NULL, ast_json_unref);

	if (snapshot == NULL) {
		return NULL;
	}

	json_chan = ast_json_pack("{ s: s, s: s, s: s, s: s, s: s, s: s, s: s,"
				  "  s: s, s: s, s: s, s: s, s: o, s: o, s: o,"
				  "  s: o"
				  "}",
				  "name", snapshot->name,
				  "state", ast_state2str(snapshot->state),
				  "accountcode", snapshot->accountcode,
				  "peeraccount", snapshot->peeraccount,
				  "userfield", snapshot->userfield,
				  "uniqueid", snapshot->uniqueid,
				  "linkedid", snapshot->linkedid,
				  "parkinglot", snapshot->parkinglot,
				  "hangupsource", snapshot->hangupsource,
				  "appl", snapshot->appl,
				  "data", snapshot->data,
				  "dialplan", ast_json_dialplan_cep(snapshot->context, snapshot->exten, snapshot->priority),
				  "caller", ast_json_name_number(snapshot->caller_name, snapshot->caller_number),
				  "connected", ast_json_name_number(snapshot->connected_name, snapshot->connected_number),
				  "creationtime", ast_json_timeval(snapshot->creationtime, NULL));

	return ast_json_ref(json_chan);
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
	if (ast_strlen_zero(old_snapshot->appl) &&
	    !ast_strlen_zero(new_snapshot->appl)) {
		return 0;
	}

	return old_snapshot->priority == new_snapshot->priority &&
		strcmp(old_snapshot->context, new_snapshot->context) == 0 &&
		strcmp(old_snapshot->exten, new_snapshot->exten) == 0;
}

int ast_channel_snapshot_caller_id_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot)
{
	ast_assert(old_snapshot != NULL);
	ast_assert(new_snapshot != NULL);
	return strcmp(old_snapshot->caller_number, new_snapshot->caller_number) == 0 &&
		strcmp(old_snapshot->caller_name, new_snapshot->caller_name) == 0;
}

static void stasis_channels_cleanup(void)
{
	channel_topic_all_cached = stasis_caching_unsubscribe_and_join(channel_topic_all_cached);
	ao2_cleanup(channel_topic_all);
	channel_topic_all = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_snapshot_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_dial_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_varset_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_user_event_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_hangup_request_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_dtmf_begin_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_dtmf_end_type);
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
}

void ast_stasis_channels_init(void)
{
	ast_register_cleanup(stasis_channels_cleanup);

	STASIS_MESSAGE_TYPE_INIT(ast_channel_snapshot_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_dial_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_varset_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_user_event_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_hangup_request_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_dtmf_begin_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_dtmf_end_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_hold_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_unhold_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_chanspy_start_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_chanspy_stop_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_fax_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_hangup_handler_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_moh_start_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_moh_stop_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_monitor_start_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_monitor_stop_type);

	channel_topic_all = stasis_topic_create("ast_channel_topic_all");
	channel_topic_all_cached = stasis_caching_topic_create(channel_topic_all, channel_snapshot_get_id);
}
