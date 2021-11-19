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

#include "asterisk/app.h"
#include "asterisk/mwi.h"
#include "asterisk/stasis_channels.h"

/*!
 * \brief Define \ref stasis topic objects
 * @{
 */
static struct stasis_state_manager *mwi_state_manager;
static struct stasis_cache *mwi_state_cache;
static struct stasis_caching_topic *mwi_topic_cached;

/*! @} */

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

/*!
 * \brief Define \ref stasis message types for MWI
 * @{
 */
STASIS_MESSAGE_TYPE_DEFN(ast_mwi_state_type,
	.to_event = mwi_to_event, );
STASIS_MESSAGE_TYPE_DEFN(ast_mwi_vm_app_type);

/*! @} */

static void mwi_state_dtor(void *obj)
{
	struct ast_mwi_state *mwi_state = obj;
	ast_string_field_free_memory(mwi_state);
	ao2_cleanup(mwi_state->snapshot);
	mwi_state->snapshot = NULL;
}

struct stasis_topic *ast_mwi_topic_all(void)
{
	return stasis_state_all_topic(mwi_state_manager);
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
	return stasis_state_topic(mwi_state_manager, uniqueid);
}

static struct ast_mwi_state *mwi_create_state(const char *mailbox, const char *context,
	int urgent_msgs, int new_msgs, int old_msgs)
{
	struct ast_mwi_state *mwi_state;

	ast_assert(!ast_strlen_zero(mailbox));

	mwi_state = ao2_alloc(sizeof(*mwi_state), mwi_state_dtor);
	if (!mwi_state) {
		ast_log(LOG_ERROR, "Unable to create MWI state for mailbox '%s@%s'\n",
				mailbox, ast_strlen_zero(context) ? "" : context);
		return NULL;
	}

	if (ast_string_field_init(mwi_state, 256)) {
		ast_log(LOG_ERROR, "Unable to initialize MWI state for mailbox '%s@%s'\n",
				mailbox, ast_strlen_zero(context) ? "" : context);
		ao2_ref(mwi_state, -1);
		return NULL;
	}
	if (!ast_strlen_zero(context)) {
		ast_string_field_build(mwi_state, uniqueid, "%s@%s", mailbox, context);
	} else {
		ast_string_field_set(mwi_state, uniqueid, mailbox);
	}

	mwi_state->urgent_msgs = urgent_msgs;
	mwi_state->new_msgs = new_msgs;
	mwi_state->old_msgs = old_msgs;

	return mwi_state;
}

static struct ast_mwi_state *mwi_retrieve_then_create_state(const char *mailbox)
{
	int urgent_msgs;
	int new_msgs;
	int old_msgs;

	ast_app_inboxcount2(mailbox, &urgent_msgs, &new_msgs, &old_msgs);
	return mwi_create_state(mailbox, NULL, urgent_msgs, new_msgs, old_msgs);
}

struct ast_mwi_state *ast_mwi_create(const char *mailbox, const char *context)
{
	return mwi_create_state(mailbox, context, 0, 0, 0);
}

/*!
 * \internal
 * \brief Create a MWI state snapshot message.
 * \since 12.2.0
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param urgent_msgs
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 * \param[in] eid The EID of the server that originally published the message
 *
 * \return message on success.  Use ao2_cleanup() when done with it.
 * \retval NULL on error.
 */
static struct stasis_message *mwi_state_create_message(
	const char *mailbox,
	const char *context,
	int urgent_msgs,
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

	mwi_state = mwi_create_state(mailbox, context, urgent_msgs, new_msgs, old_msgs);
	if (!mwi_state) {
		return NULL;
	}

	if (!ast_strlen_zero(channel_id)) {
		mwi_state->snapshot = ast_channel_snapshot_get_latest(channel_id);
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

/*!
 * \internal
 *
 * This object currently acts as a typedef, but can also be thought of as a "child" object
 * of the stasis_state_subscriber type. As such the "base" pointer should always be the
 * first object attribute. Doing so allows this object to be easily type cast and used by
 * the stasis_state code.
 */
struct ast_mwi_subscriber {
	/*! The "base" state subscriber. (Must be first object attribute) */
	struct stasis_state_subscriber *base;
};

struct ast_mwi_subscriber *ast_mwi_add_subscriber(const char *mailbox)
{
	return (struct ast_mwi_subscriber *)stasis_state_add_subscriber(
		mwi_state_manager, mailbox);
}

struct ast_mwi_subscriber *ast_mwi_subscribe_pool(const char *mailbox,
	stasis_subscription_cb callback, void *data)
{
	struct stasis_subscription *stasis_sub;
	struct ast_mwi_subscriber *sub = (struct ast_mwi_subscriber *)stasis_state_subscribe_pool(
		mwi_state_manager, mailbox, callback, data);

	if (!sub) {
		return NULL;
	}

	stasis_sub = ast_mwi_subscriber_subscription(sub);

	stasis_subscription_accept_message_type(stasis_sub, ast_mwi_state_type());
	stasis_subscription_set_filter(stasis_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	return sub;
}

void *ast_mwi_unsubscribe(struct ast_mwi_subscriber *sub)
{
	return stasis_state_unsubscribe((struct stasis_state_subscriber *)sub);
}

void *ast_mwi_unsubscribe_and_join(struct ast_mwi_subscriber *sub)
{
	return stasis_state_unsubscribe_and_join((struct stasis_state_subscriber *)sub);
}

struct stasis_topic *ast_mwi_subscriber_topic(struct ast_mwi_subscriber *sub)
{
	return stasis_state_subscriber_topic((struct stasis_state_subscriber *)sub);
}

struct ast_mwi_state *ast_mwi_subscriber_data(struct ast_mwi_subscriber *sub)
{
	struct stasis_state_subscriber *s = (struct stasis_state_subscriber *)sub;
	struct ast_mwi_state *mwi_state = stasis_state_subscriber_data(s);

	return mwi_state ?: mwi_retrieve_then_create_state(stasis_state_subscriber_id(s));
}

struct stasis_subscription *ast_mwi_subscriber_subscription(struct ast_mwi_subscriber *sub)
{
	return stasis_state_subscriber_subscription((struct stasis_state_subscriber *)sub);
}

/*!
 * \internal
 *
 * This object currently acts as a typedef, but can also be thought of as a "child" object
 * of the stasis_state_publisher type. As such the "base" pointer should always be the
 * first object attribute. Doing so allows this object to be easily type cast and used by
 * the stasis_state code.
 */
struct ast_mwi_publisher {
	/*! The "base" state publisher. (Must be first object attribute) */
	struct stasis_state_publisher *base;
};

struct ast_mwi_publisher *ast_mwi_add_publisher(const char *mailbox)
{
	return (struct ast_mwi_publisher *)stasis_state_add_publisher(
		mwi_state_manager, mailbox);
}

int ast_mwi_add_observer(struct ast_mwi_observer *observer)
{
	return stasis_state_add_observer(mwi_state_manager,
		(struct stasis_state_observer *)observer);
}

void ast_mwi_remove_observer(struct ast_mwi_observer *observer)
{
	stasis_state_remove_observer(mwi_state_manager,
		(struct stasis_state_observer *)observer);
}

struct mwi_handler_data {
	on_mwi_state handler;
	void *data;
};

static int handle_mwi_state(const char *id, struct stasis_message *msg, void *user_data)
{
	struct mwi_handler_data *d = user_data;
	struct ast_mwi_state *mwi_state = stasis_message_data(msg);
	int res;

	if (mwi_state) {
		return d->handler(mwi_state, d->data);
	}

	mwi_state = mwi_create_state(id, NULL, 0, 0, 0);
	if (!mwi_state) {
		return 0;
	}

	res = d->handler(mwi_state, d->data);
	ao2_ref(mwi_state, -1);
	return res;
}

void ast_mwi_state_callback_all(on_mwi_state handler, void *data)
{
	struct mwi_handler_data d = {
		.handler = handler,
		.data = data
	};

	stasis_state_callback_all(mwi_state_manager, handle_mwi_state, &d);
}

void ast_mwi_state_callback_subscribed(on_mwi_state handler, void *data)
{
	struct mwi_handler_data d = {
		.handler = handler,
		.data = data
	};

	stasis_state_callback_subscribed(mwi_state_manager, handle_mwi_state, &d);
}

int ast_mwi_publish(struct ast_mwi_publisher *pub, int urgent_msgs,
	int new_msgs, int old_msgs, const char *channel_id, struct ast_eid *eid)
{
	struct stasis_state_publisher *p = (struct stasis_state_publisher *)pub;
	struct stasis_message *msg = mwi_state_create_message(stasis_state_publisher_id(p),
		NULL, urgent_msgs, new_msgs, old_msgs, channel_id, eid);

	if (!msg) {
		return -1;
	}

	stasis_state_publish(p, msg);
	ao2_ref(msg, -1);

	return 0;
}

int ast_mwi_publish_by_mailbox(const char *mailbox, const char *context, int urgent_msgs,
	int new_msgs, int old_msgs, const char *channel_id, struct ast_eid *eid)
{
	struct ast_mwi_state *mwi_state;
	struct stasis_message *msg = mwi_state_create_message(
		mailbox, context, urgent_msgs, new_msgs, old_msgs, channel_id, eid);

	if (!msg) {
		return -1;
	}

	mwi_state = stasis_message_data(msg);
	stasis_state_publish_by_id(mwi_state_manager, mwi_state->uniqueid, NULL, msg);
	ao2_ref(msg, -1);

	return 0;
}

int ast_publish_mwi_state_full(
	const char *mailbox,
	const char *context,
	int new_msgs,
	int old_msgs,
	const char *channel_id,
	struct ast_eid *eid)
{
	return ast_mwi_publish_by_mailbox(mailbox, context, 0, new_msgs, old_msgs, channel_id, eid);
}

int ast_delete_mwi_state_full(const char *mailbox, const char *context, struct ast_eid *eid)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct stasis_message *cached_msg;
	struct stasis_message *clear_msg;
	struct ast_mwi_state *mwi_state;

	msg = mwi_state_create_message(mailbox, context, 0, 0, 0, NULL, eid);
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
		/* Nothing to clear from the cache, but still need to remove state */
		stasis_state_remove_publish_by_id(mwi_state_manager, mwi_state->uniqueid, eid, NULL);
		return -1;
	}
	ao2_cleanup(cached_msg);

	clear_msg = stasis_cache_clear_create(msg);
	stasis_state_remove_publish_by_id(mwi_state_manager, mwi_state->uniqueid, eid, clear_msg);
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
					       struct stasis_message_type *message_type,
					       struct ast_json *blob)
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
	ao2_cleanup(mwi_state_cache);
	mwi_state_cache = NULL;
	mwi_topic_cached = stasis_caching_unsubscribe_and_join(mwi_topic_cached);
	ao2_cleanup(mwi_state_manager);
	mwi_state_manager = NULL;
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

	mwi_state_manager = stasis_state_manager_create("mwi:all");
	if (!mwi_state_manager) {
		return -1;
	}

	mwi_state_cache = stasis_cache_create(mwi_state_get_id);
	if (!mwi_state_cache) {
		return -1;
	}

	mwi_topic_cached = stasis_caching_topic_create(ast_mwi_topic_all(), mwi_state_cache);
	if (!mwi_topic_cached) {
		return -1;
	}

	return 0;
}
