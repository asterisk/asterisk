/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_pubsub</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_body_generator_types.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/serializer.h"
#include "asterisk/sorcery.h"
#include "asterisk/stasis.h"
#include "asterisk/mwi.h"

struct mwi_subscription;

AO2_GLOBAL_OBJ_STATIC(mwi_unsolicited);
AO2_GLOBAL_OBJ_STATIC(mwi_solicited);

static char *default_voicemail_extension;

#define STASIS_BUCKETS 13
#define MWI_BUCKETS 53

#define MWI_TYPE "application"
#define MWI_SUBTYPE "simple-message-summary"

#define MWI_DATASTORE "MWI datastore"

/*! Number of serializers in pool if one not supplied. */
#define MWI_SERIALIZER_POOL_SIZE 8

/*! Max timeout for all threads to join during an unload. */
#define MAX_UNLOAD_TIMEOUT_TIME 10 /* Seconds */

/*! Pool of serializers to use if not supplied. */
static struct ast_serializer_pool *mwi_serializer_pool;

static void mwi_subscription_shutdown(struct ast_sip_subscription *sub);
static void mwi_to_ami(struct ast_sip_subscription *sub, struct ast_str **buf);
static int mwi_new_subscribe(struct ast_sip_endpoint *endpoint,
		const char *resource);
static int mwi_subscription_established(struct ast_sip_subscription *sub);
static void *mwi_get_notify_data(struct ast_sip_subscription *sub);

static struct ast_sip_notifier mwi_notifier = {
	.default_accept = MWI_TYPE"/"MWI_SUBTYPE,
	.new_subscribe = mwi_new_subscribe,
	.subscription_established = mwi_subscription_established,
	.get_notify_data = mwi_get_notify_data,
};

static struct ast_sip_subscription_handler mwi_handler = {
	.event_name = "message-summary",
	.body_type = AST_SIP_MESSAGE_ACCUMULATOR,
	.accept = { MWI_TYPE"/"MWI_SUBTYPE, },
	.subscription_shutdown = mwi_subscription_shutdown,
	.to_ami = mwi_to_ami,
	.notifier = &mwi_notifier,
};

/*!
 * \brief Wrapper for stasis subscription
 *
 * An MWI subscription has a container of these. This
 * represents a stasis subscription for MWI state.
 */
struct mwi_stasis_subscription {
	/*! The MWI stasis subscription */
	struct ast_mwi_subscriber *mwi_subscriber;
	/*! The mailbox corresponding with the MWI subscription. Used as a hash key */
	char mailbox[1];
};

/*!
 * \brief A subscription for MWI
 *
 * This subscription is the basis for MWI for an endpoint. Each
 * endpoint that uses MWI will have a corresponding mwi_subscription.
 *
 * This structure acts as the owner for the underlying SIP subscription.
 * When the mwi_subscription is destroyed, the SIP subscription dies, too.
 * The mwi_subscription's lifetime is governed by its underlying stasis
 * subscriptions. When all stasis subscriptions are destroyed, the
 * mwi_subscription is destroyed as well.
 */
struct mwi_subscription {
	/*! Container of \ref mwi_stasis_subscription structures.
	 * A single MWI subscription may be for multiple mailboxes, thus
	 * requiring multiple stasis subscriptions
	 */
	struct ao2_container *stasis_subs;
	/*! The SIP subscription. Unsolicited MWI does not use this */
	struct ast_sip_subscription *sip_sub;
	/*! AORs we should react to for unsolicited MWI NOTIFY */
	char *aors;
	/*! Is the MWI solicited (i.e. Initiated with an external SUBSCRIBE) ? */
	unsigned int is_solicited;
	/*! True if this subscription is to be terminated */
	unsigned int terminate;
	/*! Identifier for the subscription.
	 * The identifier is the same as the corresponding endpoint's stasis ID.
	 * Used as a hash key
	 */
	char id[1];
};

static void mwi_stasis_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg);

static struct mwi_stasis_subscription *mwi_stasis_subscription_alloc(const char *mailbox, struct mwi_subscription *mwi_sub)
{
	struct mwi_stasis_subscription *mwi_stasis_sub;

	if (!mwi_sub) {
		return NULL;
	}

	mwi_stasis_sub = ao2_alloc(sizeof(*mwi_stasis_sub) + strlen(mailbox), NULL);
	if (!mwi_stasis_sub) {
		return NULL;
	}

	/* Safe strcpy */
	strcpy(mwi_stasis_sub->mailbox, mailbox);

	ao2_ref(mwi_sub, +1);
	mwi_stasis_sub->mwi_subscriber = ast_mwi_subscribe_pool(mailbox, mwi_stasis_cb, mwi_sub);
	if (!mwi_stasis_sub->mwi_subscriber) {
		/* Failed to subscribe. */
		ao2_ref(mwi_stasis_sub, -1);
		ao2_ref(mwi_sub, -1);
		return NULL;
	}

	stasis_subscription_accept_message_type(
		ast_mwi_subscriber_subscription(mwi_stasis_sub->mwi_subscriber),
		stasis_subscription_change_type());

	return mwi_stasis_sub;
}

static int stasis_sub_hash(const void *obj, const int flags)
{
	const struct mwi_stasis_subscription *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->mailbox;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

static int stasis_sub_cmp(void *obj, void *arg, int flags)
{
	const struct mwi_stasis_subscription *sub_left = obj;
	const struct mwi_stasis_subscription *sub_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = sub_right->mailbox;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(sub_left->mailbox, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(sub_left->mailbox, right_key, strlen(right_key));
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

static void mwi_subscription_destructor(void *obj)
{
	struct mwi_subscription *sub = obj;

	ast_debug(3, "Destroying MWI subscription for endpoint %s\n", sub->id);
	if (sub->is_solicited) {
		ast_sip_subscription_destroy(sub->sip_sub);
	}
	ao2_cleanup(sub->stasis_subs);
	ast_free(sub->aors);
}

static struct mwi_subscription *mwi_subscription_alloc(struct ast_sip_endpoint *endpoint,
		unsigned int is_solicited, struct ast_sip_subscription *sip_sub)
{
	struct mwi_subscription *sub;
	const char *endpoint_id = ast_sorcery_object_get_id(endpoint);

	sub = ao2_alloc(sizeof(*sub) + strlen(endpoint_id),
			mwi_subscription_destructor);

	if (!sub) {
		return NULL;
	}

	/* Safe strcpy */
	strcpy(sub->id, endpoint_id);

	/* Unsolicited MWI doesn't actually result in a SIP subscription being
	 * created. This is because a SIP subscription associates with a dialog.
	 * Most devices expect unsolicited MWI NOTIFYs to appear out of dialog. If
	 * they receive an in-dialog MWI NOTIFY (i.e. with a to-tag), then they
	 * will reject the NOTIFY with a 481, thus resulting in message-waiting
	 * state not being updated on the device
	 */
	if (is_solicited) {
		sub->sip_sub = sip_sub;
	}

	sub->stasis_subs = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		STASIS_BUCKETS, stasis_sub_hash, NULL, stasis_sub_cmp);
	if (!sub->stasis_subs) {
		ao2_cleanup(sub);
		return NULL;
	}
	sub->is_solicited = is_solicited;

	if (!is_solicited && !ast_strlen_zero(endpoint->aors)) {
		sub->aors = ast_strdup(endpoint->aors);
		if (!sub->aors) {
			ao2_ref(sub, -1);
			return NULL;
		}
	}

	ast_debug(3, "Created %s MWI subscription for endpoint %s\n", is_solicited ? "solicited" : "unsolicited", sub->id);

	return sub;
}

static int mwi_sub_hash(const void *obj, const int flags)
{
	const struct mwi_subscription *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->id;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

static int mwi_sub_cmp(void *obj, void *arg, int flags)
{
	const struct mwi_subscription *sub_left = obj;
	const struct mwi_subscription *sub_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = sub_right->id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(sub_left->id, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(sub_left->id, right_key, strlen(right_key));
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

static int get_message_count(void *obj, void *arg, int flags)
{
	struct mwi_stasis_subscription *mwi_stasis = obj;
	struct ast_sip_message_accumulator *counter = arg;
	struct ast_mwi_state *mwi_state;

	mwi_state = ast_mwi_subscriber_data(mwi_stasis->mwi_subscriber);
	if (!mwi_state) {
		return 0;
	}

	counter->old_msgs += mwi_state->old_msgs;
	counter->new_msgs += mwi_state->new_msgs;

	ao2_ref(mwi_state, -1);

	return 0;
}

static void set_voicemail_extension(pj_pool_t *pool, pjsip_sip_uri *local_uri,
	struct ast_sip_message_accumulator *counter, const char *voicemail_extension)
{
	pjsip_sip_uri *account_uri;
	const char *vm_exten;

	if (ast_strlen_zero(voicemail_extension)) {
		vm_exten = default_voicemail_extension;
	} else {
		vm_exten = voicemail_extension;
	}

	if (!ast_strlen_zero(vm_exten)) {
		account_uri = pjsip_uri_clone(pool, local_uri);
		pj_strdup2(pool, &account_uri->user, vm_exten);
		pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, account_uri, counter->message_account, sizeof(counter->message_account));
	}
}

struct unsolicited_mwi_data {
	struct mwi_subscription *sub;
	struct ast_sip_endpoint *endpoint;
	pjsip_evsub_state state;
	struct ast_sip_message_accumulator *counter;
};

static int send_unsolicited_mwi_notify_to_contact(void *obj, void *arg, int flags)
{
	struct unsolicited_mwi_data *mwi_data = arg;
	struct mwi_subscription *sub = mwi_data->sub;
	struct ast_sip_endpoint *endpoint = mwi_data->endpoint;
	pjsip_evsub_state state = mwi_data->state;
	struct ast_sip_contact *contact = obj;
	const char *state_name;
	pjsip_tx_data *tdata;
	pjsip_sub_state_hdr *sub_state;
	pjsip_event_hdr *event;
	pjsip_from_hdr *from;
	pjsip_sip_uri *from_uri;
	const pjsip_hdr *allow_events = pjsip_evsub_get_allow_events_hdr(NULL);
	struct ast_sip_body body;
	struct ast_str *body_text;
	struct ast_sip_body_data body_data = {
		.body_type = AST_SIP_MESSAGE_ACCUMULATOR,
		.body_data = mwi_data->counter,
	};

	if (ast_sip_create_request("NOTIFY", NULL, endpoint, NULL, contact, &tdata)) {
		ast_log(LOG_WARNING, "Unable to create unsolicited NOTIFY request to endpoint %s URI %s\n", sub->id, contact->uri);
		return 0;
	}

	body.type = MWI_TYPE;
	body.subtype = MWI_SUBTYPE;
	body_text = ast_str_create(64);
	if (!body_text) {
		pjsip_tx_data_dec_ref(tdata);
		return 0;
	}

	from = PJSIP_MSG_FROM_HDR(tdata->msg);
	from_uri = pjsip_uri_get_uri(from->uri);

	if (!ast_strlen_zero(endpoint->subscription.mwi.fromuser)) {
		pj_strdup2(tdata->pool, &from_uri->user, endpoint->subscription.mwi.fromuser);
	}

	set_voicemail_extension(tdata->pool, from_uri, mwi_data->counter, endpoint->subscription.mwi.voicemail_extension);

	if (ast_sip_pubsub_generate_body_content(body.type, body.subtype, &body_data, &body_text)) {
		ast_log(LOG_WARNING, "Unable to generate SIP MWI NOTIFY body.\n");
		ast_free(body_text);
		pjsip_tx_data_dec_ref(tdata);
		return 0;
	}

	body.body_text = ast_str_buffer(body_text);

	switch (state) {
	case PJSIP_EVSUB_STATE_ACTIVE:
		state_name = "active";
		break;
	case PJSIP_EVSUB_STATE_TERMINATED:
	default:
		state_name = "terminated";
		break;
	}

	sub_state = pjsip_sub_state_hdr_create(tdata->pool);
	pj_cstr(&sub_state->sub_state, state_name);
	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) sub_state);

	event = pjsip_event_hdr_create(tdata->pool);
	pj_cstr(&event->event_type, "message-summary");
	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) event);

	pjsip_msg_add_hdr(tdata->msg, pjsip_hdr_shallow_clone(tdata->pool, allow_events));
	ast_sip_add_body(tdata, &body);
	ast_sip_send_request(tdata, NULL, endpoint, NULL, NULL);

	ast_free(body_text);

	return 0;
}

static struct ast_sip_aor *find_aor_for_resource(struct ast_sip_endpoint *endpoint, const char *resource)
{
	struct ast_sip_aor *aor;
	char *aor_name;
	char *aors_copy;

	/* Direct match */
	if ((aor = ast_sip_location_retrieve_aor(resource))) {
		return aor;
	}

	if (!endpoint) {
		return NULL;
	}

	/*
	 * This may be a subscribe to the voicemail_extension.  If so,
	 * look for an aor belonging to this endpoint that has a matching
	 * voicemail_extension.
	 */
	aors_copy = ast_strdupa(endpoint->aors);
	while ((aor_name = ast_strip(strsep(&aors_copy, ",")))) {
		struct ast_sip_aor *check_aor = ast_sip_location_retrieve_aor(aor_name);

		if (!check_aor) {
			continue;
		}

		if (!ast_strlen_zero(check_aor->voicemail_extension)
			&& !strcasecmp(check_aor->voicemail_extension, resource)) {
			ast_debug(1, "Found an aor (%s) that matches voicemail_extension %s\n", aor_name, resource);
			return check_aor;
		}

		ao2_ref(check_aor, -1);
	}

	return NULL;
}

static void send_unsolicited_mwi_notify(struct mwi_subscription *sub,
		struct ast_sip_message_accumulator *counter)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(),
				"endpoint", sub->id), ao2_cleanup);
	char *endpoint_aors;
	char *aor_name;

	if (!endpoint) {
		ast_log(LOG_WARNING, "Unable to send unsolicited MWI to %s because endpoint does not exist\n",
				sub->id);
		return;
	}
	if (ast_strlen_zero(endpoint->aors)) {
		ast_log(LOG_WARNING, "Unable to send unsolicited MWI to %s because the endpoint has no"
				" configured AORs\n", sub->id);
		return;
	}

	endpoint_aors = ast_strdupa(endpoint->aors);

	ast_debug(5, "Sending unsolicited MWI NOTIFY to endpoint %s, new messages: %d, old messages: %d\n",
			sub->id, counter->new_msgs, counter->old_msgs);

	while ((aor_name = ast_strip(strsep(&endpoint_aors, ",")))) {
		RAII_VAR(struct ast_sip_aor *, aor, ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
		struct unsolicited_mwi_data mwi_data = {
			.sub = sub,
			.endpoint = endpoint,
			.counter = counter,
		};

		if (!aor) {
			ast_log(LOG_WARNING, "Unable to locate AOR %s for unsolicited MWI\n", aor_name);
			continue;
		}

		contacts = ast_sip_location_retrieve_aor_contacts(aor);
		if (!contacts || (ao2_container_count(contacts) == 0)) {
			ast_debug(1, "No contacts bound to AOR %s. Cannot send unsolicited MWI until a contact registers.\n", aor_name);
			continue;
		}

		ao2_callback(contacts, OBJ_NODATA, send_unsolicited_mwi_notify_to_contact, &mwi_data);
	}
}

static void send_mwi_notify(struct mwi_subscription *sub)
{
	struct ast_sip_message_accumulator counter = {
		.old_msgs = 0,
		.new_msgs = 0,
		.message_account[0] = '\0',
	};
	struct ast_sip_body_data data = {
		.body_type = AST_SIP_MESSAGE_ACCUMULATOR,
		.body_data = &counter,
	};

	ao2_callback(sub->stasis_subs, OBJ_NODATA, get_message_count, &counter);

	if (sub->is_solicited) {
		const char *resource = ast_sip_subscription_get_resource_name(sub->sip_sub);
		struct ast_sip_endpoint *endpoint = ast_sip_subscription_get_endpoint(sub->sip_sub);
		struct ast_sip_aor *aor = find_aor_for_resource(endpoint, resource);
		pjsip_dialog *dlg = ast_sip_subscription_get_dialog(sub->sip_sub);
		pjsip_sip_uri *sip_uri = ast_sip_subscription_get_sip_uri(sub->sip_sub);

		if (aor && dlg && sip_uri) {
			set_voicemail_extension(dlg->pool, sip_uri, &counter, aor->voicemail_extension);
		}

		ao2_cleanup(aor);
		ao2_cleanup(endpoint);
		ast_sip_subscription_notify(sub->sip_sub, &data, sub->terminate);

		return;
	}

	send_unsolicited_mwi_notify(sub, &counter);
}

static int unsubscribe_stasis(void *obj, void *arg, int flags)
{
	struct mwi_stasis_subscription *mwi_stasis = obj;

	if (mwi_stasis->mwi_subscriber) {
		ast_debug(3, "Removing stasis subscription to mailbox %s\n", mwi_stasis->mailbox);
		mwi_stasis->mwi_subscriber = ast_mwi_unsubscribe_and_join(mwi_stasis->mwi_subscriber);
	}
	return CMP_MATCH;
}

static int create_unsolicited_mwi_subscriptions(struct ast_sip_endpoint *endpoint,
	int recreate, int send_now, struct ao2_container *unsolicited_mwi, struct ao2_container *solicited_mwi);

static void mwi_subscription_shutdown(struct ast_sip_subscription *sub)
{
	struct mwi_subscription *mwi_sub;
	struct ast_datastore *mwi_datastore;
	struct ast_sip_endpoint *endpoint = NULL;
	struct ao2_container *unsolicited_mwi;
	struct ao2_container *solicited_mwi;

	mwi_datastore = ast_sip_subscription_get_datastore(sub, MWI_DATASTORE);
	if (!mwi_datastore) {
		return;
	}

	mwi_sub = mwi_datastore->data;

	ao2_callback(mwi_sub->stasis_subs, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe_stasis, NULL);
	ast_sip_subscription_remove_datastore(sub, MWI_DATASTORE);
	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", mwi_sub->id);

	ao2_ref(mwi_datastore, -1);

	solicited_mwi = ao2_global_obj_ref(mwi_solicited);
	if (solicited_mwi) {
		ao2_unlink(solicited_mwi, mwi_sub);
	}

	/*
	 * When a solicited subscription is removed it's possible an unsolicited one
	 * needs to be [re-]created. Attempt to establish unsolicited MWI.
	 */
	unsolicited_mwi = ao2_global_obj_ref(mwi_unsolicited);
	if (unsolicited_mwi && endpoint) {
		ao2_lock(unsolicited_mwi);
		create_unsolicited_mwi_subscriptions(endpoint, 1, 1, unsolicited_mwi, solicited_mwi);
		ao2_unlock(unsolicited_mwi);
		ao2_ref(unsolicited_mwi, -1);
	}

	ao2_cleanup(solicited_mwi);
	ao2_cleanup(endpoint);
}

static void mwi_ds_destroy(void *data)
{
	struct mwi_subscription *sub = data;

	ao2_ref(sub, -1);
}

static struct ast_datastore_info mwi_ds_info = {
	.destroy = mwi_ds_destroy,
};

static int add_mwi_datastore(struct mwi_subscription *sub)
{
	struct ast_datastore *mwi_datastore;
	int res;

	mwi_datastore = ast_sip_subscription_alloc_datastore(&mwi_ds_info, MWI_DATASTORE);
	if (!mwi_datastore) {
		return -1;
	}
	ao2_ref(sub, +1);
	mwi_datastore->data = sub;

	/*
	 * NOTE:  Adding the datastore to the subscription creates a ref loop
	 * that must be manually broken.
	 */
	res = ast_sip_subscription_add_datastore(sub->sip_sub, mwi_datastore);
	ao2_ref(mwi_datastore, -1);
	return res;
}

/*!
 * \internal
 * \brief Determine if an MWI subscription already exists for the given endpoint/mailbox
 *
 * Search the given container, and attempt to find out if the given endpoint has a
 * current subscription within. If so pass back the associated mwi_subscription and
 * mwi_stasis_subscription objects.
 *
 * \note If a subscription is located then the caller is responsible for removing the
 * references to the passed back mwi_subscription and mwi_stasis_subscription objects.
 *
 * \note Must be called with the given container already locked.
 *
 * \param container The ao2_container to search
 * \param endpoint The endpoint to find
 * \param mailbox The mailbox potentially subscribed
 * \param[out] mwi_sub May contain the located mwi_subscription
 * \param[out] mwi_stasis May contain the located mwi_stasis_subscription
 *
 * \retval 1 if a subscription was located, 0 otherwise
 */
static int has_mwi_subscription(struct ao2_container *container,
		struct ast_sip_endpoint *endpoint, const char *mailbox,
		struct mwi_subscription **mwi_sub, struct mwi_stasis_subscription **mwi_stasis)
{
	struct ao2_iterator *mwi_subs;

	*mwi_sub = NULL;
	*mwi_stasis = NULL;

	if (!container) {
		return 0;
	}

	mwi_subs = ao2_find(container, ast_sorcery_object_get_id(endpoint),
						OBJ_SEARCH_KEY | OBJ_MULTIPLE | OBJ_NOLOCK);
	if (!mwi_subs) {
		return 0;
	}

	while ((*mwi_sub = ao2_iterator_next(mwi_subs))) {
		*mwi_stasis = ao2_find((*mwi_sub)->stasis_subs, mailbox, OBJ_SEARCH_KEY);
		if (*mwi_stasis) {
			/* If found then caller is responsible for unrefs of passed back objects */
			break;
		}
		ao2_ref(*mwi_sub, -1);
	}

	ao2_iterator_destroy(mwi_subs);
	return *mwi_stasis ? 1 : 0;
}

/*!
 * \internal
 * \brief Allow and/or replace the unsolicited subscription
 *
 * Checks to see if solicited subscription is allowed. If allowed, and an
 * unsolicited one exists then prepare for replacement by removing the
 * current unsolicited subscription.
 *
 * \param endpoint The endpoint
 * \param mailbox The mailbox
 * \param unsolicited_mwi A container of unsolicited mwi objects
 *
 * \retval 1 if a solicited subscription is allowed for the endpoint/mailbox
 *         0 otherwise
 */
static int allow_and_or_replace_unsolicited(struct ast_sip_endpoint *endpoint, const char *mailbox,
	struct ao2_container *unsolicited_mwi)
{
	struct mwi_subscription *mwi_sub;
	struct mwi_stasis_subscription *mwi_stasis;

	if (!has_mwi_subscription(unsolicited_mwi, endpoint, mailbox, &mwi_sub, &mwi_stasis)) {
		/* If no unsolicited subscription then allow the solicited one */
		return 1;
	}

	if (!endpoint->subscription.mwi.subscribe_replaces_unsolicited) {
		/* Has unsolicited subscription and can't replace, so disallow */
		ao2_ref(mwi_stasis, -1);
		ao2_ref(mwi_sub, -1);
		return 0;
	}

	/*
	 * The unsolicited subscription exists, and it is allowed to be replaced.
	 * So, first remove the unsolicited stasis subscription, and if aggregation
	 * is not enabled then also remove the mwi_subscription object as well.
	 */
	ast_debug(1, "Unsolicited subscription being replaced by solicited for "
			"endpoint '%s' mailbox '%s'\n", ast_sorcery_object_get_id(endpoint), mailbox);

	unsubscribe_stasis(mwi_stasis, NULL, 0);
	ao2_unlink(mwi_sub->stasis_subs, mwi_stasis);

	if (!endpoint->subscription.mwi.aggregate) {
		ao2_unlink(unsolicited_mwi, mwi_sub);
	}

	ao2_ref(mwi_stasis, -1);
	ao2_ref(mwi_sub, -1);

	/* This solicited subscription is replacing an unsolicited one, so allow */
	return 1;
}

static int send_notify(void *obj, void *arg, int flags);

/*!
 * \internal
 * \brief Determine if an unsolicited MWI subscription is allowed
 *
 * \param endpoint The endpoint
 * \param mailbox The mailbox
 * \param unsolicited_mwi A container of unsolicited mwi objects
 * \param solicited_mwi A container of solicited mwi objects
 *
 * \retval 1 if an unsolicited subscription is allowed for the endpoint/mailbox
 *         0 otherwise
 */
static int is_unsolicited_allowed(struct ast_sip_endpoint *endpoint, const char *mailbox,
	struct ao2_container *unsolicited_mwi, struct ao2_container *solicited_mwi)
{
	struct mwi_subscription *mwi_sub;
	struct mwi_stasis_subscription *mwi_stasis;

	if (ast_strlen_zero(mailbox)) {
		return 0;
	}

	/*
	 * First check if an unsolicited subscription exists. If it does then we don't
	 * want to add another one.
	 */
	if (has_mwi_subscription(unsolicited_mwi, endpoint, mailbox, &mwi_sub, &mwi_stasis)) {
		ao2_ref(mwi_stasis, -1);
		ao2_ref(mwi_sub, -1);
		return 0;
	}

	/*
	 * If there is no unsolicited subscription, next check to see if a solicited
	 * subscription exists for the endpoint/mailbox. If not, then allow.
	 */
	if (!has_mwi_subscription(solicited_mwi, endpoint, mailbox, &mwi_sub, &mwi_stasis)) {
		return 1;
	}

	/*
	 * If however, a solicited subscription does exist then we'll need to see if that
	 * subscription is allowed to replace the unsolicited one. If is allowed to replace
	 * then disallow the unsolicited one.
	 */
	if (endpoint->subscription.mwi.subscribe_replaces_unsolicited) {
		ao2_ref(mwi_stasis, -1);
		ao2_ref(mwi_sub, -1);
		return 0;
	}

	/* Otherwise, shutdown the solicited subscription and allow the unsolicited */
	mwi_sub->terminate = 1;
	send_notify(mwi_sub, NULL, 0);

	ao2_ref(mwi_stasis, -1);
	ao2_ref(mwi_sub, -1);

	return 1;
}

/*!
 * \brief Determine if an endpoint is a candidate to be able to subscribe for MWI
 *
 * Currently, this just makes sure that the endpoint is not already receiving unsolicted
 * MWI for any of an AOR's configured mailboxes.
 *
 * \param obj The AOR to which the endpoint is subscribing.
 * \param arg The endpoint that is attempting to subscribe.
 * \param flags Unused.
 * \retval 0 Endpoint is a candidate to subscribe to MWI on the AOR.
 * \retval -1 The endpoint cannot subscribe to MWI on the AOR.
 */
static int mwi_validate_for_aor(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_endpoint *endpoint = arg;
	char *mailboxes;
	char *mailbox;
	struct ao2_container *unsolicited_mwi;

	if (ast_strlen_zero(aor->mailboxes)) {
		return 0;
	}

	/* A reload could be taking place so lock while checking if allowed */
	unsolicited_mwi = ao2_global_obj_ref(mwi_unsolicited);
	if (unsolicited_mwi) {
		ao2_lock(unsolicited_mwi);
	}

	mailboxes = ast_strdupa(aor->mailboxes);
	while ((mailbox = ast_strip(strsep(&mailboxes, ",")))) {
		if (ast_strlen_zero(mailbox)) {
			continue;
		}

		if (!allow_and_or_replace_unsolicited(endpoint, mailbox, unsolicited_mwi)) {
			ast_debug(1, "Endpoint '%s' already configured for unsolicited MWI for mailbox '%s'. "
					"Denying MWI subscription to %s\n", ast_sorcery_object_get_id(endpoint), mailbox,
					ast_sorcery_object_get_id(aor));

			if (unsolicited_mwi) {
				ao2_unlock(unsolicited_mwi);
				ao2_ref(unsolicited_mwi, -1);
			}
			return -1;
		}
	}

	if (unsolicited_mwi) {
		ao2_unlock(unsolicited_mwi);
		ao2_ref(unsolicited_mwi, -1);
	}

	return 0;
}

static int mwi_on_aor(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct mwi_subscription *sub = arg;
	char *mailboxes;
	char *mailbox;

	if (ast_strlen_zero(aor->mailboxes)) {
		return 0;
	}

	mailboxes = ast_strdupa(aor->mailboxes);
	while ((mailbox = ast_strip(strsep(&mailboxes, ",")))) {
		struct mwi_stasis_subscription *mwi_stasis_sub;

		if (ast_strlen_zero(mailbox)) {
			continue;
		}

		mwi_stasis_sub = mwi_stasis_subscription_alloc(mailbox, sub);
		if (!mwi_stasis_sub) {
			continue;
		}

		ao2_link(sub->stasis_subs, mwi_stasis_sub);
		ao2_ref(mwi_stasis_sub, -1);
	}

	return 0;
}

static struct mwi_subscription *mwi_create_subscription(
	struct ast_sip_endpoint *endpoint, struct ast_sip_subscription *sip_sub)
{
	struct mwi_subscription *sub = mwi_subscription_alloc(endpoint, 1, sip_sub);

	if (!sub) {
		return NULL;
	}

	if (add_mwi_datastore(sub)) {
		ast_log(LOG_WARNING, "Unable to add datastore for MWI subscription to %s\n",
			sub->id);
		ao2_ref(sub, -1);
		return NULL;
	}

	return sub;
}

static struct mwi_subscription *mwi_subscribe_single(
	struct ast_sip_endpoint *endpoint, struct ast_sip_subscription *sip_sub, const char *name)
{
	struct ast_sip_aor *aor;
	struct mwi_subscription *sub;

	aor = find_aor_for_resource(endpoint, name);
	if (!aor) {
		ast_log(LOG_WARNING, "Unable to locate aor %s. MWI subscription failed.\n", name);
		return NULL;
	}

	sub = mwi_create_subscription(endpoint, sip_sub);
	if (sub) {
		mwi_on_aor(aor, sub, 0);
	}

	ao2_ref(aor, -1);
	return sub;
}

static struct mwi_subscription *mwi_subscribe_all(
	struct ast_sip_endpoint *endpoint, struct ast_sip_subscription *sip_sub)
{
	struct mwi_subscription *sub;

	sub = mwi_create_subscription(endpoint, sip_sub);
	if (!sub) {
		return NULL;
	}

	ast_sip_for_each_aor(endpoint->aors, mwi_on_aor, sub);
	return sub;
}

static int mwi_new_subscribe(struct ast_sip_endpoint *endpoint,
		const char *resource)
{
	RAII_VAR(struct ast_sip_aor *, aor, NULL, ao2_cleanup);

	if (ast_strlen_zero(resource)) {
		if (ast_sip_for_each_aor(endpoint->aors, mwi_validate_for_aor, endpoint)) {
			return 500;
		}
		return 200;
	}

	aor = find_aor_for_resource(endpoint, resource);
	if (!aor) {
		ast_debug(1, "Unable to locate aor %s. MWI subscription failed.\n", resource);
		return 404;
	}

	if (ast_strlen_zero(aor->mailboxes)) {
		ast_debug(1, "AOR %s has no configured mailboxes. MWI subscription failed.\n",
			resource);
		return 404;
	}

	if (mwi_validate_for_aor(aor, endpoint, 0)) {
		return 500;
	}

	return 200;
}

static int mwi_subscription_established(struct ast_sip_subscription *sip_sub)
{
	const char *resource = ast_sip_subscription_get_resource_name(sip_sub);
	struct mwi_subscription *sub;
	struct ast_sip_endpoint *endpoint = ast_sip_subscription_get_endpoint(sip_sub);
	struct ao2_container *solicited_mwi;

	/* no aor in uri? subscribe to all on endpoint */
	if (ast_strlen_zero(resource)) {
		sub = mwi_subscribe_all(endpoint, sip_sub);
	} else {
		sub = mwi_subscribe_single(endpoint, sip_sub, resource);
	}
	if (!sub) {
		ao2_cleanup(endpoint);
		return -1;
	}

	if (!ao2_container_count(sub->stasis_subs)) {
		/*
		 * We setup no MWI subscriptions so remove the MWI datastore
		 * to break the ref loop.
		 */
		ast_sip_subscription_remove_datastore(sip_sub, MWI_DATASTORE);
	}

	solicited_mwi = ao2_global_obj_ref(mwi_solicited);
	if (solicited_mwi) {
		ao2_link(solicited_mwi, sub);
		ao2_ref(solicited_mwi, -1);
	}

	ao2_cleanup(sub);
	ao2_cleanup(endpoint);
	return 0;
}

static void *mwi_get_notify_data(struct ast_sip_subscription *sub)
{
	struct ast_sip_message_accumulator *counter;
	struct mwi_subscription *mwi_sub;
	struct ast_datastore *mwi_datastore;
	struct ast_sip_aor *aor;
	struct ast_sip_endpoint *endpoint = ast_sip_subscription_get_endpoint(sub);

	mwi_datastore = ast_sip_subscription_get_datastore(sub, MWI_DATASTORE);
	if (!mwi_datastore) {
		return NULL;
	}
	mwi_sub = mwi_datastore->data;

	counter = ao2_alloc(sizeof(*counter), NULL);
	if (!counter) {
		ao2_cleanup(mwi_datastore);
		return NULL;
	}

	if ((aor = find_aor_for_resource(endpoint, ast_sip_subscription_get_resource_name(sub)))) {
		pjsip_dialog *dlg = ast_sip_subscription_get_dialog(sub);
		pjsip_sip_uri *sip_uri = ast_sip_subscription_get_sip_uri(sub);

		if (dlg && sip_uri) {
			set_voicemail_extension(dlg->pool, sip_uri, counter, aor->voicemail_extension);
		}
		ao2_ref(aor, -1);
	}
	ao2_cleanup(endpoint);

	ao2_callback(mwi_sub->stasis_subs, OBJ_NODATA, get_message_count, counter);
	ao2_cleanup(mwi_datastore);
	return counter;
}

static void mwi_subscription_mailboxes_str(struct ao2_container *stasis_subs,
					   struct ast_str **str)
{
	int is_first = 1;
	struct mwi_stasis_subscription *node;
	struct ao2_iterator i = ao2_iterator_init(stasis_subs, 0);

	while ((node = ao2_iterator_next(&i))) {
		if (is_first) {
			is_first = 0;
			ast_str_append(str, 0, "%s", node->mailbox);
		} else {
			ast_str_append(str, 0, ",%s", node->mailbox);
		}
		ao2_ref(node, -1);
	}
	ao2_iterator_destroy(&i);
}

static void mwi_to_ami(struct ast_sip_subscription *sub,
		       struct ast_str **buf)
{
	struct mwi_subscription *mwi_sub;
	struct ast_datastore *mwi_datastore;

	mwi_datastore = ast_sip_subscription_get_datastore(sub, MWI_DATASTORE);
	if (!mwi_datastore) {
		return;
	}

	mwi_sub = mwi_datastore->data;

	ast_str_append(buf, 0, "SubscriptionType: mwi\r\n");
	ast_str_append(buf, 0, "Mailboxes: ");
	mwi_subscription_mailboxes_str(mwi_sub->stasis_subs, buf);
	ast_str_append(buf, 0, "\r\n");

	ao2_ref(mwi_datastore, -1);
}

static int serialized_notify(void *userdata)
{
	struct mwi_subscription *mwi_sub = userdata;

	send_mwi_notify(mwi_sub);
	ao2_ref(mwi_sub, -1);
	return 0;
}

static int serialized_cleanup(void *userdata)
{
	struct mwi_subscription *mwi_sub = userdata;

	/* This is getting rid of the reference that was added
	 * just before this serialized task was pushed.
	 */
	ao2_cleanup(mwi_sub);
	/* This is getting rid of the reference held by the
	 * stasis subscription
	 */
	ao2_cleanup(mwi_sub);
	return 0;
}

static int send_notify(void *obj, void *arg, int flags)
{
	struct mwi_subscription *mwi_sub = obj;
	struct ast_taskprocessor *serializer = mwi_sub->is_solicited
		? ast_sip_subscription_get_serializer(mwi_sub->sip_sub)
		: ast_serializer_pool_get(mwi_serializer_pool);

	if (ast_sip_push_task(serializer, serialized_notify, ao2_bump(mwi_sub))) {
		ao2_ref(mwi_sub, -1);
	}

	return 0;
}

static void mwi_stasis_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct mwi_subscription *mwi_sub = userdata;

	if (stasis_subscription_final_message(sub, msg)) {
		if (ast_sip_push_task(ast_serializer_pool_get(mwi_serializer_pool),
				serialized_cleanup, ao2_bump(mwi_sub))) {
			ao2_ref(mwi_sub, -1);
		}
		return;
	}

	if (ast_mwi_state_type() == stasis_message_type(msg)) {
		send_notify(mwi_sub, NULL, 0);
	}
}

/*!
 * \internal
 * \brief Create unsolicited MWI subscriptions for an endpoint
 *
 * \note Call with the unsolicited_mwi container lock held.
 *
 * \param endpoint An endpoint object
 * \param recreate Whether or not unsolicited subscriptions are potentially being recreated
 * \param send_now Whether or not to send a notify once the subscription is created
 * \param unsolicited_mwi A container of unsolicited mwi objects
 * \param solicited_mwi A container of solicited mwi objects
 *
 * \retval 0
 */
static int create_unsolicited_mwi_subscriptions(struct ast_sip_endpoint *endpoint,
	int recreate, int send_now, struct ao2_container *unsolicited_mwi, struct ao2_container *solicited_mwi)
{
	RAII_VAR(struct mwi_subscription *, aggregate_sub, NULL, ao2_cleanup);
	char *mailboxes;
	char *mailbox;
	int sub_added = 0;

	if (ast_strlen_zero(endpoint->subscription.mwi.mailboxes)) {
		return 0;
	}

	if (endpoint->subscription.mwi.aggregate) {
		const char *endpoint_id = ast_sorcery_object_get_id(endpoint);

		/* Check if aggregate subscription exists */
		aggregate_sub = ao2_find(unsolicited_mwi, endpoint_id, OBJ_SEARCH_KEY | OBJ_NOLOCK);

		/*
		 * If enabled there should only ever exist a single aggregate subscription object
		 * for an endpoint. So if it exists just return unless subscriptions are potentially
		 * being added back in. If that's the case then continue.
		 */
		if (aggregate_sub && !recreate) {
			return 0;
		}

		if (!aggregate_sub) {
			aggregate_sub = mwi_subscription_alloc(endpoint, 0, NULL);
			if (!aggregate_sub) {
				return 0; /* No MWI aggregation for you */
			}

			/*
			 * Just in case we somehow get in the position of recreating with no previous
			 * aggregate object, set recreate to false here in order to allow the new
			 * object to be linked into the container below
			 */
			recreate = 0;
		}
	}

	/* Lock solicited so we don't potentially add to both containers */
	if (solicited_mwi) {
		ao2_lock(solicited_mwi);
	}

	mailboxes = ast_strdupa(endpoint->subscription.mwi.mailboxes);
	while ((mailbox = ast_strip(strsep(&mailboxes, ",")))) {
		struct mwi_subscription *sub;
		struct mwi_stasis_subscription *mwi_stasis_sub;

		if (!is_unsolicited_allowed(endpoint, mailbox, unsolicited_mwi, solicited_mwi)) {
			continue;
		}

		sub = aggregate_sub ?: mwi_subscription_alloc(endpoint, 0, NULL);
		if (!sub) {
			continue;
		}

		mwi_stasis_sub = mwi_stasis_subscription_alloc(mailbox, sub);
		if (mwi_stasis_sub) {
			ao2_link(sub->stasis_subs, mwi_stasis_sub);
			ao2_ref(mwi_stasis_sub, -1);
		}
		if (!aggregate_sub) {
			ao2_link_flags(unsolicited_mwi, sub, OBJ_NOLOCK);
			if (send_now) {
				send_notify(sub, NULL, 0);
			}
			ao2_ref(sub, -1);
		}

		if (aggregate_sub && !sub_added) {
			/* If aggregation track if at least one subscription has been added */
			sub_added = 1;
		}
	}

	if (aggregate_sub) {
		if (ao2_container_count(aggregate_sub->stasis_subs)) {
			/* Only link if we're dealing with a new aggregate object */
			if (!recreate) {
				ao2_link_flags(unsolicited_mwi, aggregate_sub, OBJ_NOLOCK);
			}
			if (send_now && sub_added) {
				send_notify(aggregate_sub, NULL, 0);
			}
		}
	}

	if (solicited_mwi) {
		ao2_unlock(solicited_mwi);
	}

	return 0;
}

static int create_mwi_subscriptions_for_endpoint(void *obj, void *arg, void *data, int flags)
{
	return create_unsolicited_mwi_subscriptions(obj, 0, 0, arg, data);
}

static int unsubscribe(void *obj, void *arg, int flags)
{
	struct mwi_subscription *mwi_sub = obj;

	ao2_callback(mwi_sub->stasis_subs, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe_stasis, NULL);

	return CMP_MATCH;
}

static void create_mwi_subscriptions(void)
{
	struct ao2_container *unsolicited_mwi;
	struct ao2_container *solicited_mwi;
	struct ao2_container *endpoints;
	struct ast_variable *var;

	unsolicited_mwi = ao2_global_obj_ref(mwi_unsolicited);
	if (!unsolicited_mwi) {
		return;
	}

	var = ast_variable_new("mailboxes !=", "", "");

	endpoints = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "endpoint",
		AST_RETRIEVE_FLAG_MULTIPLE, var);

	ast_variables_destroy(var);
	if (!endpoints) {
		ao2_ref(unsolicited_mwi, -1);
		return;
	}

	solicited_mwi = ao2_global_obj_ref(mwi_solicited);

	/* We remove all the old stasis subscriptions first before applying the new configuration. This
	 * prevents a situation where there might be multiple overlapping stasis subscriptions for an
	 * endpoint for mailboxes. Though there may be mailbox changes during the gap between unsubscribing
	 * and resubscribing, up-to-date mailbox state will be sent out to the endpoint when the
	 * new stasis subscription is established
	 */
	ao2_lock(unsolicited_mwi);
	ao2_callback(unsolicited_mwi, OBJ_NOLOCK | OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe, NULL);
	ao2_callback_data(endpoints, OBJ_NODATA, create_mwi_subscriptions_for_endpoint, unsolicited_mwi, solicited_mwi);
	ao2_unlock(unsolicited_mwi);

	ao2_ref(endpoints, -1);
	ao2_cleanup(solicited_mwi);
	ao2_ref(unsolicited_mwi, -1);
}

/*! \brief Function called to send MWI NOTIFY on any unsolicited mailboxes relating to this AOR */
static int send_contact_notify(void *obj, void *arg, int flags)
{
	struct mwi_subscription *mwi_sub = obj;
	const char *aor = arg;

	if (!mwi_sub->aors || !strstr(mwi_sub->aors, aor)) {
		return 0;
	}

	if (ast_sip_push_task(ast_serializer_pool_get(mwi_serializer_pool),
			serialized_notify, ao2_bump(mwi_sub))) {
		ao2_ref(mwi_sub, -1);
	}

	return 0;
}

/*! \brief Create mwi subscriptions and notify */
static void mwi_contact_changed(const struct ast_sip_contact *contact)
{
	char *id = ast_strdupa(ast_sorcery_object_get_id(contact));
	char *aor = NULL;
	struct ast_sip_endpoint *endpoint = NULL;
	struct ao2_container *unsolicited_mwi;
	struct ao2_container *solicited_mwi;

	if (contact->endpoint) {
		endpoint = ao2_bump(contact->endpoint);
	} else {
		if (!ast_strlen_zero(contact->endpoint_name)) {
			endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", contact->endpoint_name);
		}
	}

	if (!endpoint || ast_strlen_zero(endpoint->subscription.mwi.mailboxes)) {
		ao2_cleanup(endpoint);
		return;
	}

	unsolicited_mwi = ao2_global_obj_ref(mwi_unsolicited);
	if (!unsolicited_mwi) {
		ao2_cleanup(endpoint);
		return;
	}

	solicited_mwi = ao2_global_obj_ref(mwi_solicited);

	ao2_lock(unsolicited_mwi);
	create_unsolicited_mwi_subscriptions(endpoint, 0, 0, unsolicited_mwi, solicited_mwi);
	ao2_unlock(unsolicited_mwi);
	ao2_cleanup(endpoint);
	ao2_cleanup(solicited_mwi);
	ao2_ref(unsolicited_mwi, -1);

	aor = strsep(&id, ";@");
	ao2_callback(unsolicited_mwi, OBJ_NODATA, send_contact_notify, aor);
}

/*! \brief Function called when a contact is updated */
static void mwi_contact_updated(const void *object)
{
	mwi_contact_changed(object);
}

/*! \brief Function called when a contact is added */
static void mwi_contact_added(const void *object)
{
	mwi_contact_changed(object);
}

/*! \brief Function called when a contact is deleted */
static void mwi_contact_deleted(const void *object)
{
	const struct ast_sip_contact *contact = object;
	struct ao2_iterator *mwi_subs;
	struct mwi_subscription *mwi_sub;
	struct ast_sip_endpoint *endpoint = NULL;
	struct ast_sip_contact *found_contact;
	struct ao2_container *unsolicited_mwi;

	if (contact->endpoint) {
		endpoint = ao2_bump(contact->endpoint);
	} else {
		if (!ast_strlen_zero(contact->endpoint_name)) {
			endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", contact->endpoint_name);
		}
	}

	if (!endpoint || ast_strlen_zero(endpoint->subscription.mwi.mailboxes)) {
		ao2_cleanup(endpoint);
		return;
	}

	/* Check if there is another contact */
	found_contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
	ao2_cleanup(endpoint);
	if (found_contact) {
		ao2_cleanup(found_contact);
		return;
	}

	unsolicited_mwi = ao2_global_obj_ref(mwi_unsolicited);
	if (!unsolicited_mwi) {
		return;
	}

	ao2_lock(unsolicited_mwi);
	mwi_subs = ao2_find(unsolicited_mwi, contact->endpoint_name,
		OBJ_SEARCH_KEY | OBJ_MULTIPLE | OBJ_NOLOCK | OBJ_UNLINK);
	if (mwi_subs) {
		for (; (mwi_sub = ao2_iterator_next(mwi_subs)); ao2_cleanup(mwi_sub)) {
			unsubscribe(mwi_sub, NULL, 0);
		}
		ao2_iterator_destroy(mwi_subs);
	}
	ao2_unlock(unsolicited_mwi);
	ao2_ref(unsolicited_mwi, -1);
}

/*! \brief Observer for contacts so unsolicited MWI is sent when a contact changes */
static const struct ast_sorcery_observer mwi_contact_observer = {
	.created = mwi_contact_added,
	.updated = mwi_contact_updated,
	.deleted = mwi_contact_deleted,
};

/*! \brief Task invoked to send initial MWI NOTIFY for unsolicited */
static int send_initial_notify_all(void *obj)
{
	struct ao2_container *unsolicited_mwi = ao2_global_obj_ref(mwi_unsolicited);

	if (unsolicited_mwi) {
		ao2_callback(unsolicited_mwi, OBJ_NODATA, send_notify, NULL);
		ao2_ref(unsolicited_mwi, -1);
	}

	return 0;
}

/*! \brief Event callback which fires initial unsolicited MWI NOTIFY messages when we're fully booted */
static void mwi_startup_event_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct ast_json_payload *payload;
	const char *type;

	if (stasis_message_type(message) != ast_manager_get_generic_type()) {
		return;
	}

	payload = stasis_message_data(message);
	type = ast_json_string_get(ast_json_object_get(payload->json, "type"));

	if (strcmp(type, "FullyBooted")) {
		return;
	}

	ast_sip_push_task(ast_serializer_pool_get(mwi_serializer_pool),
		send_initial_notify_all, NULL);

	stasis_unsubscribe(sub);
}

static void global_loaded(const char *object_type)
{
	ast_free(default_voicemail_extension);
	default_voicemail_extension = ast_sip_get_default_voicemail_extension();
	ast_serializer_pool_set_alerts(mwi_serializer_pool,
		ast_sip_get_mwi_tps_queue_high(), ast_sip_get_mwi_tps_queue_low());
}

static struct ast_sorcery_observer global_observer = {
	.loaded = global_loaded,
};

static int reload(void)
{
	if (!ast_sip_get_mwi_disable_initial_unsolicited()) {
		create_mwi_subscriptions();
	}
	return 0;
}

static int unload_module(void)
{
	struct ao2_container *unsolicited_mwi;

	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &global_observer);
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "contact", &mwi_contact_observer);

	unsolicited_mwi = ao2_global_obj_replace(mwi_unsolicited, NULL);
	if (unsolicited_mwi) {
		ao2_callback(unsolicited_mwi, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe, NULL);
		ao2_ref(unsolicited_mwi, -1);
	}

	ao2_global_obj_release(mwi_solicited);

	if (ast_serializer_pool_destroy(mwi_serializer_pool)) {
		ast_log(LOG_WARNING, "Unload incomplete. Try again later\n");
		return -1;
	}
	mwi_serializer_pool = NULL;

	ast_sip_unregister_subscription_handler(&mwi_handler);

	ast_free(default_voicemail_extension);
	default_voicemail_extension = NULL;
	return 0;
}

static int load_module(void)
{
	struct ao2_container *mwi_container;

	if (ast_sip_register_subscription_handler(&mwi_handler)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	mwi_serializer_pool = ast_serializer_pool_create("pjsip/mwi",
		MWI_SERIALIZER_POOL_SIZE, ast_sip_threadpool(), MAX_UNLOAD_TIMEOUT_TIME);
	if (!mwi_serializer_pool) {
		ast_log(AST_LOG_WARNING, "Failed to create MWI serializer pool. The default SIP pool will be used for MWI\n");
	}

	mwi_container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, MWI_BUCKETS,
		mwi_sub_hash, NULL, mwi_sub_cmp);
	if (!mwi_container) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	ao2_global_obj_replace_unref(mwi_solicited, mwi_container);
	ao2_ref(mwi_container, -1);

	mwi_container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, MWI_BUCKETS,
		mwi_sub_hash, NULL, mwi_sub_cmp);
	if (!mwi_container) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	ao2_global_obj_replace_unref(mwi_unsolicited, mwi_container);
	ao2_ref(mwi_container, -1);

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "contact", &mwi_contact_observer);
	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	if (!ast_sip_get_mwi_disable_initial_unsolicited()) {
		create_mwi_subscriptions();
		if (ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
			ast_sip_push_task(ast_serializer_pool_get(mwi_serializer_pool),
				send_initial_notify_all, NULL);
		} else {
			struct stasis_subscription *sub;

			sub = stasis_subscribe_pool(ast_manager_get_topic(), mwi_startup_event_cb, NULL);
			stasis_subscription_accept_message_type(sub, ast_manager_get_generic_type());
			stasis_subscription_set_filter(sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
		}
	}

	if (!mwi_serializer_pool) {
		/*
		 * If the mwi serializer pool was unable to be established then the module will
		 * use the default serializer pool. If this happens prevent manual unloading
		 * since there would now exist the potential for a crash on unload.
		 */
		ast_module_shutdown_ref(ast_module_info->self);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP MWI resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND + 5,
	.requires = "res_pjsip,res_pjsip_pubsub",
);
