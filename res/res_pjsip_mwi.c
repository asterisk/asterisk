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
#include "asterisk/sorcery.h"
#include "asterisk/stasis.h"
#include "asterisk/app.h"

struct mwi_subscription;
static struct ao2_container *unsolicited_mwi;

static char *default_voicemail_extension;

#define STASIS_BUCKETS 13
#define MWI_BUCKETS 53

#define MWI_TYPE "application"
#define MWI_SUBTYPE "simple-message-summary"

#define MWI_DATASTORE "MWI datastore"

/*! Number of serializers in pool if one not supplied. */
#define MWI_SERIALIZER_POOL_SIZE 8

/*! Pool of serializers to use if not supplied. */
static struct ast_taskprocessor *mwi_serializer_pool[MWI_SERIALIZER_POOL_SIZE];

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
	struct stasis_subscription *stasis_sub;
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
	/*! Identifier for the subscription.
	 * The identifier is the same as the corresponding endpoint's stasis ID.
	 * Used as a hash key
	 */
	char id[1];
};

/*!
 * \internal
 * \brief Shutdown the serializers in the mwi pool.
 * \since 13.12.0
 *
 * \return Nothing
 */
static void mwi_serializer_pool_shutdown(void)
{
	int idx;

	for (idx = 0; idx < MWI_SERIALIZER_POOL_SIZE; ++idx) {
		ast_taskprocessor_unreference(mwi_serializer_pool[idx]);
		mwi_serializer_pool[idx] = NULL;
	}
}

/*!
 * \internal
 * \brief Setup the serializers in the mwi pool.
 * \since 13.12.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int mwi_serializer_pool_setup(void)
{
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];
	int idx;

	for (idx = 0; idx < MWI_SERIALIZER_POOL_SIZE; ++idx) {
		/* Create name with seq number appended. */
		ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/mwi");

		mwi_serializer_pool[idx] = ast_sip_create_serializer(tps_name);
		if (!mwi_serializer_pool[idx]) {
			mwi_serializer_pool_shutdown();
			return -1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Pick a mwi serializer from the pool.
 * \since 13.12.0
 *
 * \retval least queue size task processor.
 */
static struct ast_taskprocessor *get_mwi_serializer(void)
{
	int idx;
	int pos;

	if (!mwi_serializer_pool[0]) {
		return NULL;
	}

	for (pos = idx = 0; idx < MWI_SERIALIZER_POOL_SIZE; ++idx) {
		if (ast_taskprocessor_size(mwi_serializer_pool[idx]) < ast_taskprocessor_size(mwi_serializer_pool[pos])) {
			pos = idx;
		}
	}

	return mwi_serializer_pool[pos];
}

/*!
 * \internal
 * \brief Set taskprocessor alert levels for the serializers in the mwi pool.
 * \since 13.12.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int mwi_serializer_set_alert_levels(void)
{
	int idx;
	long tps_queue_high;
	long tps_queue_low;

	if (!mwi_serializer_pool[0]) {
		return -1;
	}

	tps_queue_high = ast_sip_get_mwi_tps_queue_high();
	if (tps_queue_high <= 0) {
		ast_log(AST_LOG_WARNING, "Invalid taskprocessor high water alert trigger level '%ld'\n",
			tps_queue_high);
		tps_queue_high = AST_TASKPROCESSOR_HIGH_WATER_LEVEL;
	}

	tps_queue_low = ast_sip_get_mwi_tps_queue_low();
	if (tps_queue_low < -1 || tps_queue_high < tps_queue_low) {
		ast_log(AST_LOG_WARNING, "Invalid taskprocessor low water clear alert level '%ld'\n",
			tps_queue_low);
		tps_queue_low = -1;
	}

	for (idx = 0; idx < MWI_SERIALIZER_POOL_SIZE; ++idx) {
		if (ast_taskprocessor_alert_set_levels(mwi_serializer_pool[idx], tps_queue_low, tps_queue_high)) {
			ast_log(AST_LOG_WARNING, "Failed to set alert levels for MWI serializer pool #%d.\n",
				idx);
		}
	}

	return 0;
}


static void mwi_stasis_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg);

static struct mwi_stasis_subscription *mwi_stasis_subscription_alloc(const char *mailbox, struct mwi_subscription *mwi_sub)
{
	struct mwi_stasis_subscription *mwi_stasis_sub;
	struct stasis_topic *topic;

	if (!mwi_sub) {
		return NULL;
	}

	mwi_stasis_sub = ao2_alloc(sizeof(*mwi_stasis_sub) + strlen(mailbox), NULL);
	if (!mwi_stasis_sub) {
		return NULL;
	}

	topic = ast_mwi_topic(mailbox);

	/* Safe strcpy */
	strcpy(mwi_stasis_sub->mailbox, mailbox);

	ast_debug(3, "Creating stasis MWI subscription to mailbox %s for endpoint %s\n",
		mailbox, mwi_sub->id);
	ao2_ref(mwi_sub, +1);
	mwi_stasis_sub->stasis_sub = stasis_subscribe_pool(topic, mwi_stasis_cb, mwi_sub);
	if (!mwi_stasis_sub->stasis_sub) {
		/* Failed to subscribe. */
		ao2_ref(mwi_stasis_sub, -1);
		ao2_ref(mwi_sub, -1);
		mwi_stasis_sub = NULL;
	}
	stasis_subscription_accept_message_type(mwi_stasis_sub->stasis_sub, ast_mwi_state_type());
	stasis_subscription_accept_message_type(mwi_stasis_sub->stasis_sub, stasis_subscription_change_type());
	stasis_subscription_set_filter(mwi_stasis_sub->stasis_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
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
	struct stasis_message *msg;
	struct mwi_stasis_subscription *mwi_stasis = obj;
	struct ast_sip_message_accumulator *counter = arg;
	struct ast_mwi_state *mwi_state;

	msg = stasis_cache_get(ast_mwi_state_cache(), ast_mwi_state_type(), mwi_stasis->mailbox);
	if (!msg) {
		return 0;
	}

	mwi_state = stasis_message_data(msg);
	counter->old_msgs += mwi_state->old_msgs;
	counter->new_msgs += mwi_state->new_msgs;

	ao2_ref(msg, -1);

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
		ast_sip_subscription_notify(sub->sip_sub, &data, 0);

		return;
	}

	send_unsolicited_mwi_notify(sub, &counter);
}

static int unsubscribe_stasis(void *obj, void *arg, int flags)
{
	struct mwi_stasis_subscription *mwi_stasis = obj;
	if (mwi_stasis->stasis_sub) {
		ast_debug(3, "Removing stasis subscription to mailbox %s\n", mwi_stasis->mailbox);
		mwi_stasis->stasis_sub = stasis_unsubscribe_and_join(mwi_stasis->stasis_sub);
	}
	return CMP_MATCH;
}

static void mwi_subscription_shutdown(struct ast_sip_subscription *sub)
{
	struct mwi_subscription *mwi_sub;
	struct ast_datastore *mwi_datastore;

	mwi_datastore = ast_sip_subscription_get_datastore(sub, MWI_DATASTORE);
	if (!mwi_datastore) {
		return;
	}

	mwi_sub = mwi_datastore->data;
	ao2_callback(mwi_sub->stasis_subs, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe_stasis, NULL);
	ast_sip_subscription_remove_datastore(sub, MWI_DATASTORE);

	ao2_ref(mwi_datastore, -1);
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
 * \brief Determines if an endpoint is receiving unsolicited MWI for a particular mailbox.
 *
 * \param endpoint The endpoint to check
 * \param mailbox The candidate mailbox
 * \retval 0 The endpoint does not receive unsolicited MWI for this mailbox
 * \retval 1 The endpoint receives unsolicited MWI for this mailbox
 */
static int endpoint_receives_unsolicited_mwi_for_mailbox(struct ast_sip_endpoint *endpoint,
		const char *mailbox)
{
	struct ao2_iterator *mwi_subs;
	struct mwi_subscription *mwi_sub;
	const char *endpoint_id = ast_sorcery_object_get_id(endpoint);
	int ret = 0;

	mwi_subs = ao2_find(unsolicited_mwi, endpoint_id, OBJ_SEARCH_KEY | OBJ_MULTIPLE);
	if (!mwi_subs) {
		return 0;
	}

	for (; (mwi_sub = ao2_iterator_next(mwi_subs)) && !ret; ao2_cleanup(mwi_sub)) {
		struct mwi_stasis_subscription *mwi_stasis;

		mwi_stasis = ao2_find(mwi_sub->stasis_subs, mailbox, OBJ_SEARCH_KEY);
		if (mwi_stasis) {
			if (endpoint->subscription.mwi.subscribe_replaces_unsolicited) {
				unsubscribe_stasis(mwi_stasis, NULL, 0);
				ao2_unlink(mwi_sub->stasis_subs, mwi_stasis);
			} else {
				ret = 1;
			}
			ao2_cleanup(mwi_stasis);
		}
	}

	ao2_iterator_destroy(mwi_subs);
	return ret;
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

	if (ast_strlen_zero(aor->mailboxes)) {
		return 0;
	}

	mailboxes = ast_strdupa(aor->mailboxes);
	while ((mailbox = ast_strip(strsep(&mailboxes, ",")))) {
		if (ast_strlen_zero(mailbox)) {
			continue;
		}

		if (endpoint_receives_unsolicited_mwi_for_mailbox(endpoint, mailbox)) {
			ast_debug(1, "Endpoint '%s' already configured for unsolicited MWI for mailbox '%s'. "
					"Denying MWI subscription to %s\n", ast_sorcery_object_get_id(endpoint), mailbox,
					ast_sorcery_object_get_id(aor));
			return -1;
		}
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
		: get_mwi_serializer();

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
		if (ast_sip_push_task(NULL, serialized_cleanup, ao2_bump(mwi_sub))) {
			ao2_ref(mwi_sub, -1);
		}
		return;
	}

	if (ast_mwi_state_type() == stasis_message_type(msg)) {
		send_notify(mwi_sub, NULL, 0);
	}
}

/*! \note Called with the unsolicited_mwi container lock held. */
static int create_mwi_subscriptions_for_endpoint(void *obj, void *arg, int flags)
{
	RAII_VAR(struct mwi_subscription *, aggregate_sub, NULL, ao2_cleanup);
	struct ast_sip_endpoint *endpoint = obj;
	char *mailboxes, *mailbox;

	if (ast_strlen_zero(endpoint->subscription.mwi.mailboxes)) {
		return 0;
	}

	if (endpoint->subscription.mwi.aggregate) {
		const char *endpoint_id = ast_sorcery_object_get_id(endpoint);

		/* Check if subscription exists */
		aggregate_sub = ao2_find(unsolicited_mwi, endpoint_id, OBJ_SEARCH_KEY | OBJ_NOLOCK);
		if (aggregate_sub) {
			return 0;
		}
		aggregate_sub = mwi_subscription_alloc(endpoint, 0, NULL);
		if (!aggregate_sub) {
			return 0;
		}
	}

	mailboxes = ast_strdupa(endpoint->subscription.mwi.mailboxes);
	while ((mailbox = ast_strip(strsep(&mailboxes, ",")))) {
		struct mwi_subscription *sub;
		struct mwi_stasis_subscription *mwi_stasis_sub;

		/* check if subscription exists */
		if (ast_strlen_zero(mailbox) ||
			(!aggregate_sub && endpoint_receives_unsolicited_mwi_for_mailbox(endpoint, mailbox))) {
			continue;
		}

		sub = aggregate_sub ?: mwi_subscription_alloc(endpoint, 0, NULL);
		mwi_stasis_sub = mwi_stasis_subscription_alloc(mailbox, sub);
		if (mwi_stasis_sub) {
			ao2_link(sub->stasis_subs, mwi_stasis_sub);
			ao2_ref(mwi_stasis_sub, -1);
		}
		if (!aggregate_sub && sub) {
			ao2_link_flags(unsolicited_mwi, sub, OBJ_NOLOCK);
			ao2_ref(sub, -1);
		}
	}
	if (aggregate_sub) {
		ao2_link_flags(unsolicited_mwi, aggregate_sub, OBJ_NOLOCK);
	}
	return 0;
}

static int unsubscribe(void *obj, void *arg, int flags)
{
	struct mwi_subscription *mwi_sub = obj;

	ao2_callback(mwi_sub->stasis_subs, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe_stasis, NULL);

	return CMP_MATCH;
}

static void create_mwi_subscriptions(void)
{
	struct ao2_container *endpoints;
	struct ast_variable *var;

	var = ast_variable_new("mailboxes !=", "", "");

	endpoints = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "endpoint",
		AST_RETRIEVE_FLAG_MULTIPLE, var);

	ast_variables_destroy(var);
	if (!endpoints) {
		return;
	}

	/* We remove all the old stasis subscriptions first before applying the new configuration. This
	 * prevents a situation where there might be multiple overlapping stasis subscriptions for an
	 * endpoint for mailboxes. Though there may be mailbox changes during the gap between unsubscribing
	 * and resubscribing, up-to-date mailbox state will be sent out to the endpoint when the
	 * new stasis subscription is established
	 */
	ao2_lock(unsolicited_mwi);
	ao2_callback(unsolicited_mwi, OBJ_NOLOCK | OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe, NULL);
	ao2_callback(endpoints, OBJ_NODATA, create_mwi_subscriptions_for_endpoint, NULL);
	ao2_unlock(unsolicited_mwi);

	ao2_ref(endpoints, -1);
}

/*! \brief Function called to send MWI NOTIFY on any unsolicited mailboxes relating to this AOR */
static int send_contact_notify(void *obj, void *arg, int flags)
{
	struct mwi_subscription *mwi_sub = obj;
	const char *aor = arg;

	if (!mwi_sub->aors || !strstr(mwi_sub->aors, aor)) {
		return 0;
	}

	if (ast_sip_push_task(get_mwi_serializer(), serialized_notify, ao2_bump(mwi_sub))) {
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

	ao2_lock(unsolicited_mwi);
	create_mwi_subscriptions_for_endpoint(endpoint, NULL, 0);
	ao2_unlock(unsolicited_mwi);
	ao2_cleanup(endpoint);

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
	ao2_callback(unsolicited_mwi, OBJ_NODATA, send_notify, NULL);

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

	ast_sip_push_task(NULL, send_initial_notify_all, NULL);

	stasis_unsubscribe(sub);
}

static void global_loaded(const char *object_type)
{
	ast_free(default_voicemail_extension);
	default_voicemail_extension = ast_sip_get_default_voicemail_extension();
	mwi_serializer_set_alert_levels();
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

static int load_module(void)
{
	if (ast_sip_register_subscription_handler(&mwi_handler)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (mwi_serializer_pool_setup()) {
		ast_log(AST_LOG_WARNING, "Failed to create MWI serializer pool. The default SIP pool will be used for MWI\n");
	}

	unsolicited_mwi = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, MWI_BUCKETS,
		mwi_sub_hash, NULL, mwi_sub_cmp);
	if (!unsolicited_mwi) {
		mwi_serializer_pool_shutdown();
		ast_sip_unregister_subscription_handler(&mwi_handler);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "contact", &mwi_contact_observer);
	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	if (!ast_sip_get_mwi_disable_initial_unsolicited()) {
		create_mwi_subscriptions();
		if (ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
			ast_sip_push_task(NULL, send_initial_notify_all, NULL);
		} else {
			struct stasis_subscription *sub;

			sub = stasis_subscribe_pool(ast_manager_get_topic(), mwi_startup_event_cb, NULL);
			stasis_subscription_accept_message_type(sub, ast_manager_get_generic_type());
			stasis_subscription_set_filter(sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
		}
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &global_observer);
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "contact", &mwi_contact_observer);

	ao2_callback(unsolicited_mwi, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, unsubscribe, NULL);
	ao2_ref(unsolicited_mwi, -1);
	unsolicited_mwi = NULL;

	mwi_serializer_pool_shutdown();

	ast_sip_unregister_subscription_handler(&mwi_handler);

	ast_free(default_voicemail_extension);
	default_voicemail_extension = NULL;
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP MWI resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND + 5,
	.requires = "res_pjsip,res_pjsip_pubsub",
);
