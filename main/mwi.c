/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/mwi.h"
#include "asterisk/stasis_channels.h"

/*
 * @{ \brief Define \ref stasis topic objects
 */
static struct stasis_topic *mwi_topic_all;
static struct stasis_cache *mwi_state_cache;
static struct stasis_caching_topic *mwi_topic_cached;
static struct stasis_topic_pool *mwi_topic_pool;
/* @} */

/*! \brief Convert a MWI \ref stasis_message to a \ref ast_event */
static struct ast_event *mwi_to_event(struct stasis_message *message)
{
	struct ast_event *event;
	struct ast_mwi_state *mwi_state;
	char *mailbox;
	char *context;

	if (!message) {
		return NULL;
	}

	mwi_state = stasis_message_data(message);

	/* Strip off @context */
	context = mailbox = ast_strdupa(mwi_state->uniqueid);
	strsep(&context, "@");
	if (ast_strlen_zero(context)) {
		context = "default";
	}

	event = ast_event_new(AST_EVENT_MWI,
				AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
				AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, context,
				AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, mwi_state->new_msgs,
				AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, mwi_state->old_msgs,
				AST_EVENT_IE_EID, AST_EVENT_IE_PLTYPE_RAW, &mwi_state->eid, sizeof(mwi_state->eid),
				AST_EVENT_IE_END);

	return event;
}

/*
 * @{ \brief Define \ref stasis message types for MWI
 */
STASIS_MESSAGE_TYPE_DEFN(ast_mwi_state_type,
	.to_event = mwi_to_event, );
STASIS_MESSAGE_TYPE_DEFN(ast_mwi_vm_app_type);
/* @} */

static void mwi_state_dtor(void *obj)
{
	struct ast_mwi_state *mwi_state = obj;
	ast_string_field_free_memory(mwi_state);
	ao2_cleanup(mwi_state->snapshot);
	mwi_state->snapshot = NULL;
}

struct stasis_topic *ast_mwi_topic_all(void)
{
	return mwi_topic_all;
}

struct stasis_cache *ast_mwi_state_cache(void)
{
	return mwi_state_cache;
}

struct stasis_topic *ast_mwi_topic_cached(void)
{
	return stasis_caching_get_topic(mwi_topic_cached);
}

struct stasis_topic *ast_mwi_topic(const char *uniqueid)
{
	return stasis_topic_pool_get_topic(mwi_topic_pool, uniqueid);
}

struct ast_mwi_state *ast_mwi_create(const char *mailbox, const char *context)
{
	struct ast_mwi_state *mwi_state;

	ast_assert(!ast_strlen_zero(mailbox));

	mwi_state = ao2_alloc(sizeof(*mwi_state), mwi_state_dtor);
	if (!mwi_state) {
		return NULL;
	}

	if (ast_string_field_init(mwi_state, 256)) {
		ao2_ref(mwi_state, -1);
		return NULL;
	}
	if (!ast_strlen_zero(context)) {
		ast_string_field_build(mwi_state, uniqueid, "%s@%s", mailbox, context);
	} else {
		ast_string_field_set(mwi_state, uniqueid, mailbox);
	}

	return mwi_state;
}

/*!
 * \internal
 * \brief Create a MWI state snapshot message.
 * \since 12.2.0
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 * \param[in] eid The EID of the server that originally published the message
 *
 * \retval message on success.  Use ao2_cleanup() when done with it.
 * \retval NULL on error.
 */
static struct stasis_message *mwi_state_create_message(
	const char *mailbox,
	const char *context,
	int new_msgs,
	int old_msgs,
	const char *channel_id,
	struct ast_eid *eid)
{
	struct ast_mwi_state *mwi_state;
	struct stasis_message *message;

	if (!ast_mwi_state_type()) {
		return NULL;
	}

	mwi_state = ast_mwi_create(mailbox, context);
	if (!mwi_state) {
		return NULL;
	}

	mwi_state->new_msgs = new_msgs;
	mwi_state->old_msgs = old_msgs;

	if (!ast_strlen_zero(channel_id)) {
		struct stasis_message *chan_message;

		chan_message = stasis_cache_get(ast_channel_cache(), ast_channel_snapshot_type(),
			channel_id);
		if (chan_message) {
			mwi_state->snapshot = stasis_message_data(chan_message);
			ao2_ref(mwi_state->snapshot, +1);
		}
		ao2_cleanup(chan_message);
	}

	if (eid) {
		mwi_state->eid = *eid;
	} else {
		mwi_state->eid = ast_eid_default;
	}

	/*
	 * XXX As far as stasis is concerned, all MWI events are local.
	 *
	 * We may in the future want to make MWI aggregate local/remote
	 * message counts similar to how device state aggregates state.
	 */
	message = stasis_message_create_full(ast_mwi_state_type(), mwi_state, &ast_eid_default);
	ao2_cleanup(mwi_state);
	return message;
}

int ast_publish_mwi_state_full(
	const char *mailbox,
	const char *context,
	int new_msgs,
	int old_msgs,
	const char *channel_id,
	struct ast_eid *eid)
{
	struct ast_mwi_state *mwi_state;
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	struct stasis_topic *mailbox_specific_topic;

	message = mwi_state_create_message(mailbox, context, new_msgs, old_msgs, channel_id, eid);
	if (!message) {
		return -1;
	}

	mwi_state = stasis_message_data(message);
	mailbox_specific_topic = ast_mwi_topic(mwi_state->uniqueid);
	if (!mailbox_specific_topic) {
		return -1;
	}

	stasis_publish(mailbox_specific_topic, message);

	return 0;
}

int ast_delete_mwi_state_full(const char *mailbox, const char *context, struct ast_eid *eid)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct stasis_message *cached_msg;
	struct stasis_message *clear_msg;
	struct ast_mwi_state *mwi_state;
	struct stasis_topic *mailbox_specific_topic;

	msg = mwi_state_create_message(mailbox, context, 0, 0, NULL, eid);
	if (!msg) {
		return -1;
	}

	mwi_state = stasis_message_data(msg);

	/*
	 * XXX As far as stasis is concerned, all MWI events are local.
	 *
	 * For now, it is assumed that there is only one entity
	 * maintaining the state of a particular mailbox.
	 *
	 * If we ever have multiple MWI event entities maintaining
	 * the same mailbox that wish to delete their cached entry
	 * we will need to do something about the race condition
	 * potential between checking the cache and removing the
	 * cache entry.
	 */
	cached_msg = stasis_cache_get_by_eid(ast_mwi_state_cache(),
		ast_mwi_state_type(), mwi_state->uniqueid, &ast_eid_default);
	if (!cached_msg) {
		/* Nothing to clear */
		return -1;
	}
	ao2_cleanup(cached_msg);

	mailbox_specific_topic = ast_mwi_topic(mwi_state->uniqueid);
	if (!mailbox_specific_topic) {
		return -1;
	}

	clear_msg = stasis_cache_clear_create(msg);
	if (clear_msg) {
		stasis_publish(mailbox_specific_topic, clear_msg);
	}

	ao2_cleanup(clear_msg);
	return 0;
}

static const char *mwi_state_get_id(struct stasis_message *message)
{
	if (ast_mwi_state_type() == stasis_message_type(message)) {
		struct ast_mwi_state *mwi_state = stasis_message_data(message);
		return mwi_state->uniqueid;
	} else if (stasis_subscription_change_type() == stasis_message_type(message)) {
		struct stasis_subscription_change *change = stasis_message_data(message);
		return change->uniqueid;
	}

	return NULL;
}

static void mwi_blob_dtor(void *obj)
{
	struct ast_mwi_blob *mwi_blob = obj;

	ao2_cleanup(mwi_blob->mwi_state);
	ast_json_unref(mwi_blob->blob);
}

struct stasis_message *ast_mwi_blob_create(struct ast_mwi_state *mwi_state,
	struct stasis_message_type *message_type, struct ast_json *blob)
{
	struct ast_mwi_blob *obj;
	struct stasis_message *msg;

	ast_assert(blob != NULL);

	if (!message_type) {
		return NULL;
	}

	obj = ao2_alloc(sizeof(*obj), mwi_blob_dtor);
	if (!obj) {
		return NULL;
	}

	obj->mwi_state = mwi_state;
	ao2_ref(obj->mwi_state, +1);
	obj->blob = ast_json_ref(blob);

	/* This is not a normal MWI event.  Only used by the MinivmNotify app. */
	msg = stasis_message_create(message_type, obj);
	ao2_ref(obj, -1);

	return msg;
}

static void mwi_cleanup(void)
{
	ao2_cleanup(mwi_topic_pool);
	mwi_topic_pool = NULL;
	ao2_cleanup(mwi_topic_all);
	mwi_topic_all = NULL;
	ao2_cleanup(mwi_state_cache);
	mwi_state_cache = NULL;
	mwi_topic_cached = stasis_caching_unsubscribe_and_join(mwi_topic_cached);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_mwi_state_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_mwi_vm_app_type);
}

int mwi_init(void)
{
	ast_register_cleanup(mwi_cleanup);

	if (STASIS_MESSAGE_TYPE_INIT(ast_mwi_state_type) != 0) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_mwi_vm_app_type) != 0) {
		return -1;
	}

	mwi_topic_all = stasis_topic_create("mwi:all");
	if (!mwi_topic_all) {
		return -1;
	}

	mwi_state_cache = stasis_cache_create(mwi_state_get_id);
	if (!mwi_state_cache) {
		return -1;
	}

	mwi_topic_cached = stasis_caching_topic_create(mwi_topic_all, mwi_state_cache);
	if (!mwi_topic_cached) {
		return -1;
	}

	mwi_topic_pool = stasis_topic_pool_create(mwi_topic_all);
	if (!mwi_topic_pool) {
		return -1;
	}

	return 0;
}
