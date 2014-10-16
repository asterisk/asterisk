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
/*!
 * \brief Opaque structure representing an RFC 3265 SIP subscription
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/astobj2.h"
#include "asterisk/datastore.h"
#include "asterisk/uuid.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/sched.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/callerid.h"
#include "asterisk/manager.h"
#include "asterisk/test.h"
#include "res_pjsip/include/res_pjsip_private.h"

/*** DOCUMENTATION
	<manager name="PJSIPShowSubscriptionsInbound" language="en_US">
		<synopsis>
			Lists subscriptions.
		</synopsis>
		<syntax />
		<description>
			<para>
			Provides a listing of all inbound subscriptions.  An event <literal>InboundSubscriptionDetail</literal>
			is issued for each subscription object.  Once all detail events are completed an
			<literal>InboundSubscriptionDetailComplete</literal> event is issued.
                        </para>
		</description>
	</manager>
	<manager name="PJSIPShowSubscriptionsOutbound" language="en_US">
		<synopsis>
			Lists subscriptions.
		</synopsis>
		<syntax />
		<description>
			<para>
			Provides a listing of all outbound subscriptions.  An event <literal>OutboundSubscriptionDetail</literal>
			is issued for each subscription object.  Once all detail events are completed an
			<literal>OutboundSubscriptionDetailComplete</literal> event is issued.
                        </para>
		</description>
	</manager>
	<configInfo name="res_pjsip_pubsub" language="en_US">
		<synopsis>Module that implements publish and subscribe support.</synopsis>
		<configFile name="pjsip.conf">
			<configObject name="subscription_persistence">
				<synopsis>Persists SIP subscriptions so they survive restarts.</synopsis>
				<configOption name="packet">
					<synopsis>Entire SIP SUBSCRIBE packet that created the subscription</synopsis>
				</configOption>
				<configOption name="src_name">
					<synopsis>The source address of the subscription</synopsis>
				</configOption>
				<configOption name="src_port">
					<synopsis>The source port of the subscription</synopsis>
				</configOption>
				<configOption name="transport_key">
					<synopsis>The type of transport the subscription was received on</synopsis>
				</configOption>
				<configOption name="local_name">
					<synopsis>The local address the subscription was received on</synopsis>
				</configOption>
				<configOption name="local_port">
					<synopsis>The local port the subscription was received on</synopsis>
				</configOption>
				<configOption name="cseq">
					<synopsis>The sequence number of the next NOTIFY to be sent</synopsis>
				</configOption>
				<configOption name="tag">
					<synopsis>The local tag of the dialog for the subscription</synopsis>
				</configOption>
				<configOption name="endpoint">
					<synopsis>The name of the endpoint that subscribed</synopsis>
				</configOption>
				<configOption name="expires">
					<synopsis>The time at which the subscription expires</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

static pj_bool_t pubsub_on_rx_request(pjsip_rx_data *rdata);

static struct pjsip_module pubsub_module = {
	.name = { "PubSub Module", 13 },
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = pubsub_on_rx_request,
};

#define MOD_DATA_BODY_GENERATOR "sub_body_generator"
#define MOD_DATA_PERSISTENCE "sub_persistence"

static const pj_str_t str_event_name = { "Event", 5 };

/*! \brief Scheduler used for automatically expiring publications */
static struct ast_sched_context *sched;

/*! \brief Number of buckets for publications (on a per handler) */
#define PUBLICATIONS_BUCKETS 37

/*! \brief Default expiration time for PUBLISH if one is not specified */
#define DEFAULT_PUBLISH_EXPIRES 3600

/*! \brief Defined method for PUBLISH */
const pjsip_method pjsip_publish_method =
{
	PJSIP_OTHER_METHOD,
	{ "PUBLISH", 7 }
};

/*!
 * \brief The types of PUBLISH messages defined in RFC 3903
 */
enum sip_publish_type {
	/*!
	 * \brief Unknown
	 *
	 * \details
	 * This actually is not defined in RFC 3903. We use this as a constant
	 * to indicate that an incoming PUBLISH does not fit into any of the
	 * other categories and is thus invalid.
	 */
	SIP_PUBLISH_UNKNOWN,

	/*!
	 * \brief Initial
	 *
	 * \details
	 * The first PUBLISH sent. This will contain a non-zero Expires header
	 * as well as a body that indicates the current state of the endpoint
	 * that has sent the message. The initial PUBLISH is the only type
	 * of PUBLISH to not contain a Sip-If-Match header in it.
	 */
	SIP_PUBLISH_INITIAL,

	/*!
	 * \brief Refresh
	 *
	 * \details
	 * Used to keep a published state from expiring. This will contain a
	 * non-zero Expires header but no body since its purpose is not to
	 * update state.
	 */
	SIP_PUBLISH_REFRESH,

	/*!
	 * \brief Modify
	 *
	 * \details
	 * Used to change state from its previous value. This will contain
	 * a body updating the published state. May or may not contain an
	 * Expires header.
	 */
	SIP_PUBLISH_MODIFY,

	/*!
	 * \brief Remove
	 *
	 * \details
	 * Used to remove published state from an ESC. This will contain
	 * an Expires header set to 0 and likely no body.
	 */
	SIP_PUBLISH_REMOVE,
};

/*!
 * Used to create new entity IDs by ESCs.
 */
static int esc_etag_counter;

/*!
 * \brief Structure representing a SIP publication
 */
struct ast_sip_publication {
	/*! Publication datastores set up by handlers */
	struct ao2_container *datastores;
	/*! \brief Entity tag for the publication */
	int entity_tag;
	/*! \brief Handler for this publication */
	struct ast_sip_publish_handler *handler;
	/*! \brief The endpoint with which the subscription is communicating */
	struct ast_sip_endpoint *endpoint;
	/*! \brief Expiration time of the publication */
	int expires;
	/*! \brief Scheduled item for expiration of publication */
	int sched_id;
};


/*!
 * \brief Structure used for persisting an inbound subscription
 */
struct subscription_persistence {
	/*! Sorcery object details */
	SORCERY_OBJECT(details);
	/*! The name of the endpoint involved in the subscrption */
	char *endpoint;
	/*! SIP message that creates the subscription */
	char packet[PJSIP_MAX_PKT_LEN];
	/*! Source address of the message */
	char src_name[PJ_INET6_ADDRSTRLEN];
	/*! Source port of the message */
	int src_port;
	/*! Local transport key type */
	char transport_key[32];
	/*! Local transport address */
	char local_name[PJ_INET6_ADDRSTRLEN];
	/*! Local transport port */
	int local_port;
	/*! Next CSeq to use for message */
	unsigned int cseq;
	/*! Local tag of the dialog */
	char *tag;
	/*! When this subscription expires */
	struct timeval expires;
};

/*!
 * \brief Structure representing a SIP subscription
 */
struct ast_sip_subscription {
	/*! Subscription datastores set up by handlers */
	struct ao2_container *datastores;
	/*! The endpoint with which the subscription is communicating */
	struct ast_sip_endpoint *endpoint;
	/*! Serializer on which to place operations for this subscription */
	struct ast_taskprocessor *serializer;
	/*! The handler for this subscription */
	const struct ast_sip_subscription_handler *handler;
	/*! The role for this subscription */
	enum ast_sip_subscription_role role;
	/*! The underlying PJSIP event subscription structure */
	pjsip_evsub *evsub;
	/*! The underlying PJSIP dialog */
	pjsip_dialog *dlg;
	/*! Body generaator for NOTIFYs */
	struct ast_sip_pubsub_body_generator *body_generator;
	/*! Persistence information */
	struct subscription_persistence *persistence;
	/*! Next item in the list */
	AST_LIST_ENTRY(ast_sip_subscription) next;
};

static const char *sip_subscription_roles_map[] = {
	[AST_SIP_SUBSCRIBER] = "Subscriber",
	[AST_SIP_NOTIFIER] = "Notifier"
};

AST_RWLIST_HEAD_STATIC(subscriptions, ast_sip_subscription);

AST_RWLIST_HEAD_STATIC(body_generators, ast_sip_pubsub_body_generator);
AST_RWLIST_HEAD_STATIC(body_supplements, ast_sip_pubsub_body_supplement);

/*! \brief Destructor for subscription persistence */
static void subscription_persistence_destroy(void *obj)
{
	struct subscription_persistence *persistence = obj;

	ast_free(persistence->endpoint);
	ast_free(persistence->tag);
}

/*! \brief Allocator for subscription persistence */
static void *subscription_persistence_alloc(const char *name)
{
	return ast_sorcery_generic_alloc(sizeof(struct subscription_persistence), subscription_persistence_destroy);
}

/*! \brief Function which creates initial persistence information of a subscription in sorcery */
static struct subscription_persistence *subscription_persistence_create(struct ast_sip_subscription *sub)
{
	char tag[PJ_GUID_STRING_LENGTH + 1];

	/* The id of this persistence object doesn't matter as we keep it on the subscription and don't need to
	 * look it up by id at all.
	 */
	struct subscription_persistence *persistence = ast_sorcery_alloc(ast_sip_get_sorcery(),
		"subscription_persistence", NULL);

	if (!persistence) {
		return NULL;
	}

	persistence->endpoint = ast_strdup(ast_sorcery_object_get_id(sub->endpoint));
	ast_copy_pj_str(tag, &sub->dlg->local.info->tag, sizeof(tag));
	persistence->tag = ast_strdup(tag);

	ast_sorcery_create(ast_sip_get_sorcery(), persistence);
	return persistence;
}

/*! \brief Function which updates persistence information of a subscription in sorcery */
static void subscription_persistence_update(struct ast_sip_subscription *sub,
	pjsip_rx_data *rdata)
{
	if (!sub->persistence) {
		return;
	}

	sub->persistence->cseq = sub->dlg->local.cseq;

	if (rdata) {
		int expires;
		pjsip_expires_hdr *expires_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL);

		expires = expires_hdr ? expires_hdr->ivalue : DEFAULT_PUBLISH_EXPIRES;
		sub->persistence->expires = ast_tvadd(ast_tvnow(), ast_samp2tv(expires, 1));

		ast_copy_string(sub->persistence->packet, rdata->pkt_info.packet, sizeof(sub->persistence->packet));
		ast_copy_string(sub->persistence->src_name, rdata->pkt_info.src_name, sizeof(sub->persistence->src_name));
		sub->persistence->src_port = rdata->pkt_info.src_port;
		ast_copy_string(sub->persistence->transport_key, rdata->tp_info.transport->type_name,
			sizeof(sub->persistence->transport_key));
		ast_copy_pj_str(sub->persistence->local_name, &rdata->tp_info.transport->local_name.host,
			sizeof(sub->persistence->local_name));
		sub->persistence->local_port = rdata->tp_info.transport->local_name.port;
	}

	ast_sorcery_update(ast_sip_get_sorcery(), sub->persistence);
}

/*! \brief Function which removes persistence of a subscription from sorcery */
static void subscription_persistence_remove(struct ast_sip_subscription *sub)
{
	if (!sub->persistence) {
		return;
	}

	ast_sorcery_delete(ast_sip_get_sorcery(), sub->persistence);
	ao2_ref(sub->persistence, -1);
}


static struct ast_sip_subscription_handler *find_sub_handler_for_event_name(const char *event_name);
static struct ast_sip_pubsub_body_generator *find_body_generator(char accept[AST_SIP_MAX_ACCEPT][64],
		size_t num_accept, const char *body_type);

/*! \brief Retrieve a handler using the Event header of an rdata message */
static struct ast_sip_subscription_handler *subscription_get_handler_from_rdata(pjsip_rx_data *rdata)
{
	pjsip_event_hdr *event_header;
	char event[32];
	struct ast_sip_subscription_handler *handler;

	event_header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_event_name, rdata->msg_info.msg->hdr.next);
	if (!event_header) {
		ast_log(LOG_WARNING, "Incoming SUBSCRIBE request with no Event header\n");
		return NULL;
	}
	ast_copy_pj_str(event, &event_header->event_type, sizeof(event));

	handler = find_sub_handler_for_event_name(event);
	if (!handler) {
		ast_log(LOG_WARNING, "No registered subscribe handler for event %s\n", event);
	}

	return handler;
}

/*! \brief Retrieve a body generator using the Accept header of an rdata message */
static struct ast_sip_pubsub_body_generator *subscription_get_generator_from_rdata(pjsip_rx_data *rdata,
	const struct ast_sip_subscription_handler *handler)
{
	pjsip_accept_hdr *accept_header;
	char accept[AST_SIP_MAX_ACCEPT][64];
	size_t num_accept_headers;

	accept_header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_ACCEPT, rdata->msg_info.msg->hdr.next);
	if (accept_header) {
		int i;

		for (i = 0; i < accept_header->count; ++i) {
			ast_copy_pj_str(accept[i], &accept_header->values[i], sizeof(accept[i]));
		}
		num_accept_headers = accept_header->count;
	} else {
		/* If a SUBSCRIBE contains no Accept headers, then we must assume that
		 * the default accept type for the event package is to be used.
		 */
		ast_copy_string(accept[0], handler->default_accept, sizeof(accept[0]));
		num_accept_headers = 1;
	}

	return find_body_generator(accept, num_accept_headers, handler->body_type);
}

/*! \brief Callback function to perform the actual recreation of a subscription */
static int subscription_persistence_recreate(void *obj, void *arg, int flags)
{
	struct subscription_persistence *persistence = obj;
	pj_pool_t *pool = arg;
	pjsip_rx_data rdata = { { 0, }, };
	pjsip_expires_hdr *expires_header;
	struct ast_sip_subscription_handler *handler;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct ast_sip_subscription *sub;
	struct ast_sip_pubsub_body_generator *generator;

	/* If this subscription has already expired remove it */
	if (ast_tvdiff_ms(persistence->expires, ast_tvnow()) <= 0) {
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
		return 0;
	}

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", persistence->endpoint);
	if (!endpoint) {
		ast_log(LOG_WARNING, "A subscription for '%s' could not be recreated as the endpoint was not found\n",
			persistence->endpoint);
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
		return 0;
	}

	pj_pool_reset(pool);
	rdata.tp_info.pool = pool;

	if (ast_sip_create_rdata(&rdata, persistence->packet, persistence->src_name, persistence->src_port,
		persistence->transport_key, persistence->local_name, persistence->local_port)) {
		ast_log(LOG_WARNING, "A subscription for '%s' could not be recreated as the message could not be parsed\n",
			persistence->endpoint);
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
		return 0;
	}

	/* Update the expiration header with the new expiration */
	expires_header = pjsip_msg_find_hdr(rdata.msg_info.msg, PJSIP_H_EXPIRES, rdata.msg_info.msg->hdr.next);
	if (!expires_header) {
		expires_header = pjsip_expires_hdr_create(pool, 0);
		if (!expires_header) {
			ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
			return 0;
		}
		pjsip_msg_add_hdr(rdata.msg_info.msg, (pjsip_hdr*)expires_header);
	}
	expires_header->ivalue = (ast_tvdiff_ms(persistence->expires, ast_tvnow()) / 1000);

	handler = subscription_get_handler_from_rdata(&rdata);
	if (!handler) {
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
		return 0;
	}

	generator = subscription_get_generator_from_rdata(&rdata, handler);
	if (!generator) {
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
		return 0;
	}

	ast_sip_mod_data_set(rdata.tp_info.pool, rdata.endpt_info.mod_data,
			pubsub_module.id, MOD_DATA_BODY_GENERATOR, generator);
	ast_sip_mod_data_set(rdata.tp_info.pool, rdata.endpt_info.mod_data,
			pubsub_module.id, MOD_DATA_PERSISTENCE, persistence);

	sub = handler->new_subscribe(endpoint, &rdata);
	if (sub) {
		sub->persistence = ao2_bump(persistence);
		subscription_persistence_update(sub, &rdata);
	} else {
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
	}

	return 0;
}

/*! \brief Function which loads and recreates persisted subscriptions upon startup when the system is fully booted */
static int subscription_persistence_load(void *data)
{
	struct ao2_container *persisted_subscriptions = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(),
		"subscription_persistence", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	pj_pool_t *pool;

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "rtd%p", PJSIP_POOL_RDATA_LEN,
		PJSIP_POOL_RDATA_INC);
	if (!pool) {
		ast_log(LOG_WARNING, "Could not create a memory pool for recreating SIP subscriptions\n");
		return 0;
	}

	ao2_callback(persisted_subscriptions, OBJ_NODATA, subscription_persistence_recreate, pool);

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);

	ao2_ref(persisted_subscriptions, -1);
	return 0;
}

/*! \brief Event callback which fires subscription persistence recreation when the system is fully booted */
static void subscription_persistence_event_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct ast_json_payload *payload;
	const char *type;

	if (stasis_message_type(message) != ast_manager_get_generic_type()) {
		return;
	}

	payload = stasis_message_data(message);
	type = ast_json_string_get(ast_json_object_get(payload->json, "type"));

	/* This subscription only responds to the FullyBooted event so that all modules have been loaded when we
	 * recreate SIP subscriptions.
	 */
	if (strcmp(type, "FullyBooted")) {
		return;
	}

	/* This has to be here so the subscription is recreated when the body generator is available */
	ast_sip_push_task(NULL, subscription_persistence_load, NULL);

	/* Once the system is fully booted we don't care anymore */
	stasis_unsubscribe(sub);
}

static void add_subscription(struct ast_sip_subscription *obj)
{
	SCOPED_LOCK(lock, &subscriptions, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_INSERT_TAIL(&subscriptions, obj, next);
	ast_module_ref(ast_module_info->self);
}

static void remove_subscription(struct ast_sip_subscription *obj)
{
	struct ast_sip_subscription *i;
	SCOPED_LOCK(lock, &subscriptions, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&subscriptions, i, next) {
		if (i == obj) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

typedef int (*on_subscription_t)(struct ast_sip_subscription *sub, void *arg);

static int for_each_subscription(on_subscription_t on_subscription, void *arg)
{
	int num = 0;
	struct ast_sip_subscription *i;
	SCOPED_LOCK(lock, &subscriptions, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);

	if (!on_subscription) {
		return num;
	}

	AST_RWLIST_TRAVERSE(&subscriptions, i, next) {
		if (on_subscription(i, arg)) {
			break;
		}
		++num;
	}
	return num;
}

static void sip_subscription_to_ami(struct ast_sip_subscription *sub,
				    struct ast_str **buf)
{
	char str[256];
	struct ast_sip_endpoint_id_configuration *id = &sub->endpoint->id;

	ast_str_append(buf, 0, "Role: %s\r\n",
		       sip_subscription_roles_map[sub->role]);
	ast_str_append(buf, 0, "Endpoint: %s\r\n",
		       ast_sorcery_object_get_id(sub->endpoint));

	ast_copy_pj_str(str, &sub->dlg->call_id->id, sizeof(str));
	ast_str_append(buf, 0, "Callid: %s\r\n", str);

	ast_str_append(buf, 0, "State: %s\r\n", pjsip_evsub_get_state_name(
			       ast_sip_subscription_get_evsub(sub)));

	ast_callerid_merge(str, sizeof(str),
			   S_COR(id->self.name.valid, id->self.name.str, NULL),
			   S_COR(id->self.number.valid, id->self.number.str, NULL),
			   "Unknown");

	ast_str_append(buf, 0, "Callerid: %s\r\n", str);

	if (sub->handler->to_ami) {
		sub->handler->to_ami(sub, buf);
	}
}

#define DATASTORE_BUCKETS 53

#define DEFAULT_EXPIRES 3600

static int datastore_hash(const void *obj, int flags)
{
	const struct ast_datastore *datastore = obj;
	const char *uid = flags & OBJ_KEY ? obj : datastore->uid;

	ast_assert(uid != NULL);

	return ast_str_hash(uid);
}

static int datastore_cmp(void *obj, void *arg, int flags)
{
	const struct ast_datastore *datastore1 = obj;
	const struct ast_datastore *datastore2 = arg;
	const char *uid2 = flags & OBJ_KEY ? arg : datastore2->uid;

	ast_assert(datastore1->uid != NULL);
	ast_assert(uid2 != NULL);

	return strcmp(datastore1->uid, uid2) ? 0 : CMP_MATCH | CMP_STOP;
}

static int subscription_remove_serializer(void *obj)
{
	struct ast_sip_subscription *sub = obj;

	/* This is why we keep the dialog on the subscription. When the subscription
	 * is destroyed, there is no guarantee that the underlying dialog is ready
	 * to be destroyed. Furthermore, there's no guarantee in the opposite direction
	 * either. The dialog could be destroyed before our subscription is. We fix
	 * this problem by keeping a reference to the dialog until it is time to
	 * destroy the subscription. We need to have the dialog available when the
	 * subscription is destroyed so that we can guarantee that our attempt to
	 * remove the serializer will be successful.
	 */
	ast_sip_dialog_set_serializer(sub->dlg, NULL);
	pjsip_dlg_dec_session(sub->dlg, &pubsub_module);

	return 0;
}

static void subscription_destructor(void *obj)
{
	struct ast_sip_subscription *sub = obj;

	ast_debug(3, "Destroying SIP subscription\n");

	subscription_persistence_remove(sub);

	remove_subscription(sub);

	ao2_cleanup(sub->datastores);
	ao2_cleanup(sub->endpoint);

	if (sub->dlg) {
		ast_sip_push_task_synchronous(NULL, subscription_remove_serializer, sub);
	}
	ast_taskprocessor_unreference(sub->serializer);
}

static void pubsub_on_evsub_state(pjsip_evsub *sub, pjsip_event *event);
static void pubsub_on_tsx_state(pjsip_evsub *sub, pjsip_transaction *tsx, pjsip_event *event);
static void pubsub_on_rx_refresh(pjsip_evsub *sub, pjsip_rx_data *rdata,
		int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);
static void pubsub_on_rx_notify(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);
static void pubsub_on_client_refresh(pjsip_evsub *sub);
static void pubsub_on_server_timeout(pjsip_evsub *sub);


static pjsip_evsub_user pubsub_cb = {
	.on_evsub_state = pubsub_on_evsub_state,
	.on_tsx_state = pubsub_on_tsx_state,
	.on_rx_refresh = pubsub_on_rx_refresh,
	.on_rx_notify = pubsub_on_rx_notify,
	.on_client_refresh = pubsub_on_client_refresh,
	.on_server_timeout = pubsub_on_server_timeout,
};

static pjsip_evsub *allocate_evsub(const char *event, enum ast_sip_subscription_role role,
		struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, pjsip_dialog *dlg)
{
	pjsip_evsub *evsub;
	/* PJSIP is kind enough to have some built-in support for certain
	 * events. We need to use the correct initialization function for the
	 * built-in events
	 */
	if (role == AST_SIP_NOTIFIER) {
		pjsip_evsub_create_uas(dlg, &pubsub_cb, rdata, 0, &evsub);
	} else {
		pj_str_t pj_event;
		pj_cstr(&pj_event, event);
		pjsip_evsub_create_uac(dlg, &pubsub_cb, &pj_event, 0, &evsub);
	}
	return evsub;
}

struct ast_sip_subscription *ast_sip_create_subscription(const struct ast_sip_subscription_handler *handler,
		enum ast_sip_subscription_role role, struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	struct ast_sip_subscription *sub = ao2_alloc(sizeof(*sub), subscription_destructor);
	pjsip_dialog *dlg;
	struct subscription_persistence *persistence;

	if (!sub) {
		return NULL;
	}
	sub->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
	if (!sub->datastores) {
		ao2_ref(sub, -1);
		return NULL;
	}
	sub->serializer = ast_sip_create_serializer();
	if (!sub->serializer) {
		ao2_ref(sub, -1);
		return NULL;
	}
	sub->body_generator = ast_sip_mod_data_get(rdata->endpt_info.mod_data,
			pubsub_module.id, MOD_DATA_BODY_GENERATOR);
	sub->role = role;
	if (role == AST_SIP_NOTIFIER) {
		dlg = ast_sip_create_dialog_uas(endpoint, rdata);
	} else {
		RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

		contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
		if (!contact || ast_strlen_zero(contact->uri)) {
			ast_log(LOG_WARNING, "No contacts configured for endpoint %s. Unable to create SIP subsription\n",
					ast_sorcery_object_get_id(endpoint));
			ao2_ref(sub, -1);
			return NULL;
		}
		dlg = ast_sip_create_dialog_uac(endpoint, contact->uri, NULL);
	}
	if (!dlg) {
		ast_log(LOG_WARNING, "Unable to create dialog for SIP subscription\n");
		ao2_ref(sub, -1);
		return NULL;
	}
	persistence = ast_sip_mod_data_get(rdata->endpt_info.mod_data,
			pubsub_module.id, MOD_DATA_PERSISTENCE);
	if (persistence) {
		/* Update the created dialog with the persisted information */
		pjsip_ua_unregister_dlg(pjsip_ua_instance(), dlg);
		pj_strdup2(dlg->pool, &dlg->local.info->tag, persistence->tag);
		dlg->local.tag_hval = pj_hash_calc_tolower(0, NULL, &dlg->local.info->tag);
		pjsip_ua_register_dlg(pjsip_ua_instance(), dlg);
		dlg->local.cseq = persistence->cseq;
		dlg->remote.cseq = persistence->cseq;
	}
	sub->evsub = allocate_evsub(handler->event_name, role, endpoint, rdata, dlg);
	/* We keep a reference to the dialog until our subscription is destroyed. See
	 * the subscription_destructor for more details
	 */
	pjsip_dlg_inc_session(dlg, &pubsub_module);
	sub->dlg = dlg;
	ast_sip_dialog_set_serializer(dlg, sub->serializer);
	pjsip_evsub_set_mod_data(sub->evsub, pubsub_module.id, sub);
	ao2_ref(endpoint, +1);
	sub->endpoint = endpoint;
	sub->handler = handler;

	add_subscription(sub);
	return sub;
}

struct ast_sip_endpoint *ast_sip_subscription_get_endpoint(struct ast_sip_subscription *sub)
{
	ast_assert(sub->endpoint != NULL);
	ao2_ref(sub->endpoint, +1);
	return sub->endpoint;
}

struct ast_taskprocessor *ast_sip_subscription_get_serializer(struct ast_sip_subscription *sub)
{
	ast_assert(sub->serializer != NULL);
	return sub->serializer;
}

pjsip_evsub *ast_sip_subscription_get_evsub(struct ast_sip_subscription *sub)
{
	return sub->evsub;
}

pjsip_dialog *ast_sip_subscription_get_dlg(struct ast_sip_subscription *sub)
{
	return sub->dlg;
}

int ast_sip_subscription_accept(struct ast_sip_subscription *sub, pjsip_rx_data *rdata, int response)
{
	/* If this is a persistence recreation the subscription has already been accepted */
	if (ast_sip_mod_data_get(rdata->endpt_info.mod_data, pubsub_module.id, MOD_DATA_PERSISTENCE)) {
		return 0;
	}

	return pjsip_evsub_accept(ast_sip_subscription_get_evsub(sub), rdata, response, NULL) == PJ_SUCCESS ? 0 : -1;
}

int ast_sip_subscription_send_request(struct ast_sip_subscription *sub, pjsip_tx_data *tdata)
{
	struct ast_sip_endpoint *endpoint = ast_sip_subscription_get_endpoint(sub);
	int res;

	ao2_ref(sub, +1);
	res = pjsip_evsub_send_request(ast_sip_subscription_get_evsub(sub),
			tdata) == PJ_SUCCESS ? 0 : -1;

	subscription_persistence_update(sub, NULL);

	ast_test_suite_event_notify("SUBSCRIPTION_STATE_SET",
		"StateText: %s\r\n"
		"Endpoint: %s\r\n",
		pjsip_evsub_get_state_name(ast_sip_subscription_get_evsub(sub)),
		ast_sorcery_object_get_id(endpoint));
	ao2_cleanup(sub);
	ao2_cleanup(endpoint);

	return res;
}

static void subscription_datastore_destroy(void *obj)
{
	struct ast_datastore *datastore = obj;

	/* Using the destroy function (if present) destroy the data */
	if (datastore->info->destroy != NULL && datastore->data != NULL) {
		datastore->info->destroy(datastore->data);
		datastore->data = NULL;
	}

	ast_free((void *) datastore->uid);
	datastore->uid = NULL;
}

struct ast_datastore *ast_sip_subscription_alloc_datastore(const struct ast_datastore_info *info, const char *uid)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	char uuid_buf[AST_UUID_STR_LEN];
	const char *uid_ptr = uid;

	if (!info) {
		return NULL;
	}

	datastore = ao2_alloc(sizeof(*datastore), subscription_datastore_destroy);
	if (!datastore) {
		return NULL;
	}

	datastore->info = info;
	if (ast_strlen_zero(uid)) {
		/* They didn't provide an ID so we'll provide one ourself */
		uid_ptr = ast_uuid_generate_str(uuid_buf, sizeof(uuid_buf));
	}

	datastore->uid = ast_strdup(uid_ptr);
	if (!datastore->uid) {
		return NULL;
	}

	ao2_ref(datastore, +1);
	return datastore;
}

int ast_sip_subscription_add_datastore(struct ast_sip_subscription *subscription, struct ast_datastore *datastore)
{
	ast_assert(datastore != NULL);
	ast_assert(datastore->info != NULL);
	ast_assert(!ast_strlen_zero(datastore->uid));

	if (!ao2_link(subscription->datastores, datastore)) {
		return -1;
	}
	return 0;
}

struct ast_datastore *ast_sip_subscription_get_datastore(struct ast_sip_subscription *subscription, const char *name)
{
	return ao2_find(subscription->datastores, name, OBJ_KEY);
}

void ast_sip_subscription_remove_datastore(struct ast_sip_subscription *subscription, const char *name)
{
	ao2_callback(subscription->datastores, OBJ_KEY | OBJ_UNLINK | OBJ_NODATA, NULL, (void *) name);
}

int ast_sip_publication_add_datastore(struct ast_sip_publication *publication, struct ast_datastore *datastore)
{
	ast_assert(datastore != NULL);
	ast_assert(datastore->info != NULL);
	ast_assert(!ast_strlen_zero(datastore->uid));

	if (!ao2_link(publication->datastores, datastore)) {
		return -1;
	}
	return 0;
}

struct ast_datastore *ast_sip_publication_get_datastore(struct ast_sip_publication *publication, const char *name)
{
	return ao2_find(publication->datastores, name, OBJ_KEY);
}

void ast_sip_publication_remove_datastore(struct ast_sip_publication *publication, const char *name)
{
	ao2_callback(publication->datastores, OBJ_KEY | OBJ_UNLINK | OBJ_NODATA, NULL, (void *) name);
}

AST_RWLIST_HEAD_STATIC(publish_handlers, ast_sip_publish_handler);

static int publication_hash_fn(const void *obj, const int flags)
{
	const struct ast_sip_publication *publication = obj;
	const int *entity_tag = obj;

	return flags & OBJ_KEY ? *entity_tag : publication->entity_tag;
}

static int publication_cmp_fn(void *obj, void *arg, int flags)
{
	const struct ast_sip_publication *publication1 = obj;
	const struct ast_sip_publication *publication2 = arg;
	const int *entity_tag = arg;

	return (publication1->entity_tag == (flags & OBJ_KEY ? *entity_tag : publication2->entity_tag) ?
		CMP_MATCH | CMP_STOP : 0);
}

static void publish_add_handler(struct ast_sip_publish_handler *handler)
{
	SCOPED_LOCK(lock, &publish_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_INSERT_TAIL(&publish_handlers, handler, next);
}

int ast_sip_register_publish_handler(struct ast_sip_publish_handler *handler)
{
	if (ast_strlen_zero(handler->event_name)) {
		ast_log(LOG_ERROR, "No event package specified for publish handler. Cannot register\n");
		return -1;
	}

	if (!(handler->publications = ao2_container_alloc(PUBLICATIONS_BUCKETS,
		publication_hash_fn, publication_cmp_fn))) {
		ast_log(LOG_ERROR, "Could not allocate publications container for event '%s'\n",
			handler->event_name);
		return -1;
	}

	publish_add_handler(handler);

	ast_module_ref(ast_module_info->self);

	return 0;
}

void ast_sip_unregister_publish_handler(struct ast_sip_publish_handler *handler)
{
	struct ast_sip_publish_handler *iter;
	SCOPED_LOCK(lock, &publish_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&publish_handlers, iter, next) {
		if (handler == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ao2_cleanup(handler->publications);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

AST_RWLIST_HEAD_STATIC(subscription_handlers, ast_sip_subscription_handler);

static void sub_add_handler(struct ast_sip_subscription_handler *handler)
{
	SCOPED_LOCK(lock, &subscription_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_INSERT_TAIL(&subscription_handlers, handler, next);
	ast_module_ref(ast_module_info->self);
}

static struct ast_sip_subscription_handler *find_sub_handler_for_event_name(const char *event_name)
{
	struct ast_sip_subscription_handler *iter;
	SCOPED_LOCK(lock, &subscription_handlers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE(&subscription_handlers, iter, next) {
		if (!strcmp(iter->event_name, event_name)) {
			break;
		}
	}
	return iter;
}

int ast_sip_register_subscription_handler(struct ast_sip_subscription_handler *handler)
{
	pj_str_t event;
	pj_str_t accept[AST_SIP_MAX_ACCEPT];
	struct ast_sip_subscription_handler *existing;
	int i;

	if (ast_strlen_zero(handler->event_name)) {
		ast_log(LOG_ERROR, "No event package specified for subscription handler. Cannot register\n");
		return -1;
	}

	existing = find_sub_handler_for_event_name(handler->event_name);
	if (existing) {
		ast_log(LOG_ERROR, "Unable to register subscription handler for event %s."
				"A handler is already registered\n", handler->event_name);
		return -1;
	}

	for (i = 0; i < AST_SIP_MAX_ACCEPT && !ast_strlen_zero(handler->accept[i]); ++i) {
		pj_cstr(&accept[i], handler->accept[i]);
	}

	pj_cstr(&event, handler->event_name);

	pjsip_evsub_register_pkg(&pubsub_module, &event, DEFAULT_EXPIRES, i, accept);

	sub_add_handler(handler);

	return 0;
}

void ast_sip_unregister_subscription_handler(struct ast_sip_subscription_handler *handler)
{
	struct ast_sip_subscription_handler *iter;
	SCOPED_LOCK(lock, &subscription_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&subscription_handlers, iter, next) {
		if (handler == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static struct ast_sip_pubsub_body_generator *find_body_generator_type_subtype(const char *content_type,
		const char *content_subtype)
{
	struct ast_sip_pubsub_body_generator *iter;
	SCOPED_LOCK(lock, &body_generators, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);

	AST_LIST_TRAVERSE(&body_generators, iter, list) {
		if (!strcmp(iter->type, content_type) &&
				!strcmp(iter->subtype, content_subtype)) {
			break;
		}
	};

	return iter;
}

static struct ast_sip_pubsub_body_generator *find_body_generator_accept(const char *accept)
{
	char *accept_copy = ast_strdupa(accept);
	char *subtype = accept_copy;
	char *type = strsep(&subtype, "/");

	if (ast_strlen_zero(type) || ast_strlen_zero(subtype)) {
		return NULL;
	}

	return find_body_generator_type_subtype(type, subtype);
}

static struct ast_sip_pubsub_body_generator *find_body_generator(char accept[AST_SIP_MAX_ACCEPT][64],
		size_t num_accept, const char *body_type)
{
	int i;
	struct ast_sip_pubsub_body_generator *generator = NULL;

	for (i = 0; i < num_accept; ++i) {
		generator = find_body_generator_accept(accept[i]);
		if (generator) {
			ast_debug(3, "Body generator %p found for accept type %s\n", generator, accept[i]);
			if (strcmp(generator->body_type, body_type)) {
				ast_log(LOG_WARNING, "Body generator '%s/%s'(%p) does not accept the type of data this event generates\n",
						generator->type, generator->subtype, generator);
				generator = NULL;
				continue;
			}
			break;
		} else {
			ast_debug(3, "No body generator found for accept type %s\n", accept[i]);
		}
	}

	return generator;
}

static pj_bool_t pubsub_on_rx_subscribe_request(pjsip_rx_data *rdata)
{
	pjsip_expires_hdr *expires_header;
	struct ast_sip_subscription_handler *handler;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct ast_sip_subscription *sub;
	struct ast_sip_pubsub_body_generator *generator;

	endpoint = ast_pjsip_rdata_get_endpoint(rdata);
	ast_assert(endpoint != NULL);

	if (!endpoint->subscription.allow) {
		ast_log(LOG_WARNING, "Subscriptions not permitted for endpoint %s.\n", ast_sorcery_object_get_id(endpoint));
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 603, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	expires_header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, rdata->msg_info.msg->hdr.next);

	if (expires_header) {
		if (expires_header->ivalue == 0) {
			ast_log(LOG_WARNING, "Susbscription request from endpoint %s rejected. Expiration of 0 is invalid\n",
				ast_sorcery_object_get_id(endpoint));
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 400, NULL, NULL, NULL);
				return PJ_TRUE;
		}
		if (expires_header->ivalue < endpoint->subscription.minexpiry) {
			ast_log(LOG_WARNING, "Subscription expiration %d is too brief for endpoint %s. Minimum is %u\n",
				expires_header->ivalue, ast_sorcery_object_get_id(endpoint), endpoint->subscription.minexpiry);
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 423, NULL, NULL, NULL);
			return PJ_TRUE;
		}
        }

	handler = subscription_get_handler_from_rdata(rdata);
	if (!handler) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 489, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	generator = subscription_get_generator_from_rdata(rdata, handler);
	if (!generator) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 489, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	ast_sip_mod_data_set(rdata->tp_info.pool, rdata->endpt_info.mod_data,
			pubsub_module.id, MOD_DATA_BODY_GENERATOR, generator);

	sub = handler->new_subscribe(endpoint, rdata);
	if (!sub) {
		pjsip_transaction *trans = pjsip_rdata_get_tsx(rdata);

		if (trans) {
			pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
			pjsip_tx_data *tdata;

			if (pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, &tdata) != PJ_SUCCESS) {
				return PJ_TRUE;
			}
			pjsip_dlg_send_response(dlg, trans, tdata);
		} else {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		}
	} else {
		sub->persistence = subscription_persistence_create(sub);
		subscription_persistence_update(sub, rdata);
	}

	return PJ_TRUE;
}

static struct ast_sip_publish_handler *find_pub_handler(const char *event)
{
	struct ast_sip_publish_handler *iter = NULL;
	SCOPED_LOCK(lock, &publish_handlers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE(&publish_handlers, iter, next) {
		if (strcmp(event, iter->event_name)) {
			ast_debug(3, "Event %s does not match %s\n", event, iter->event_name);
			continue;
		}
		ast_debug(3, "Event name match: %s = %s\n", event, iter->event_name);
		break;
	}

	return iter;
}

static enum sip_publish_type determine_sip_publish_type(pjsip_rx_data *rdata,
	pjsip_generic_string_hdr *etag_hdr, int *expires, int *entity_id)
{
	pjsip_expires_hdr *expires_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL);

	if (etag_hdr) {
		char etag[pj_strlen(&etag_hdr->hvalue) + 1];

		ast_copy_pj_str(etag, &etag_hdr->hvalue, sizeof(etag));

		if (sscanf(etag, "%30d", entity_id) != 1) {
			return SIP_PUBLISH_UNKNOWN;
		}
	}

	*expires = expires_hdr ? expires_hdr->ivalue : DEFAULT_PUBLISH_EXPIRES;

	if (!(*expires)) {
		return SIP_PUBLISH_REMOVE;
	} else if (!etag_hdr && rdata->msg_info.msg->body) {
		return SIP_PUBLISH_INITIAL;
	} else if (etag_hdr && !rdata->msg_info.msg->body) {
		return SIP_PUBLISH_REFRESH;
	} else if (etag_hdr && rdata->msg_info.msg->body) {
		return SIP_PUBLISH_MODIFY;
	}

	return SIP_PUBLISH_UNKNOWN;
}

static struct ast_sip_publication *publish_request_initial(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata,
	struct ast_sip_publish_handler *handler)
{
	struct ast_sip_publication *publication = handler->new_publication(endpoint, rdata);

	if (!publication) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 503, NULL, NULL, NULL);
		return NULL;
	}

	publication->handler = handler;

	return publication;
}

static int publish_expire_callback(void *data)
{
	RAII_VAR(struct ast_sip_publication *, publication, data, ao2_cleanup);

	publication->handler->publish_expire(publication);

	return 0;
}

static int publish_expire(const void *data)
{
	struct ast_sip_publication *publication = (struct ast_sip_publication*)data;

	ao2_unlink(publication->handler->publications, publication);
	publication->sched_id = -1;

	if (ast_sip_push_task(NULL, publish_expire_callback, publication)) {
		ao2_cleanup(publication);
	}

	return 0;
}

static pj_bool_t pubsub_on_rx_publish_request(pjsip_rx_data *rdata)
{
	pjsip_event_hdr *event_header;
	struct ast_sip_publish_handler *handler;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	char event[32];
	static const pj_str_t str_sip_if_match = { "SIP-If-Match", 12 };
	pjsip_generic_string_hdr *etag_hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_sip_if_match, NULL);
	enum sip_publish_type publish_type;
	RAII_VAR(struct ast_sip_publication *, publication, NULL, ao2_cleanup);
	int expires = 0, entity_id;

	endpoint = ast_pjsip_rdata_get_endpoint(rdata);
	ast_assert(endpoint != NULL);

	event_header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_event_name, rdata->msg_info.msg->hdr.next);
	if (!event_header) {
		ast_log(LOG_WARNING, "Incoming PUBLISH request with no Event header\n");
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 489, NULL, NULL, NULL);
		return PJ_TRUE;
	}
	ast_copy_pj_str(event, &event_header->event_type, sizeof(event));

	handler = find_pub_handler(event);
	if (!handler) {
		ast_log(LOG_WARNING, "No registered publish handler for event %s\n", event);
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 489, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	publish_type = determine_sip_publish_type(rdata, etag_hdr, &expires, &entity_id);

	/* If this is not an initial publish ensure that a publication is present */
	if ((publish_type != SIP_PUBLISH_INITIAL) && (publish_type != SIP_PUBLISH_UNKNOWN)) {
		if (!(publication = ao2_find(handler->publications, &entity_id, OBJ_KEY | OBJ_UNLINK))) {
			static const pj_str_t str_conditional_request_failed = { "Conditional Request Failed", 26 };

			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 412, &str_conditional_request_failed,
				NULL, NULL);
			return PJ_TRUE;
		}

		/* Per the RFC every response has to have a new entity tag */
		publication->entity_tag = ast_atomic_fetchadd_int(&esc_etag_counter, +1);

		/* Update the expires here so that the created responses will contain the correct value */
		publication->expires = expires;
	}

	switch (publish_type) {
		case SIP_PUBLISH_INITIAL:
			publication = publish_request_initial(endpoint, rdata, handler);
			break;
		case SIP_PUBLISH_REFRESH:
		case SIP_PUBLISH_MODIFY:
			if (handler->publish_refresh(publication, rdata)) {
				/* If an error occurs we want to terminate the publication */
				expires = 0;
			}
			break;
		case SIP_PUBLISH_REMOVE:
			handler->publish_termination(publication, rdata);
			break;
		case SIP_PUBLISH_UNKNOWN:
		default:
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 400, NULL, NULL, NULL);
			break;
	}

	if (publication) {
		if (expires) {
			ao2_link(handler->publications, publication);

			AST_SCHED_REPLACE_UNREF(publication->sched_id, sched, expires * 1000, publish_expire, publication,
						ao2_ref(publication, -1), ao2_ref(publication, -1), ao2_ref(publication, +1));
		} else {
			AST_SCHED_DEL_UNREF(sched, publication->sched_id, ao2_ref(publication, -1));
		}
	}

	return PJ_TRUE;
}

/*! \brief Internal destructor for publications */
static void publication_destroy_fn(void *obj)
{
	struct ast_sip_publication *publication = obj;

	ast_debug(3, "Destroying SIP publication\n");

	ao2_cleanup(publication->datastores);
	ao2_cleanup(publication->endpoint);
}

struct ast_sip_publication *ast_sip_create_publication(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	struct ast_sip_publication *publication;
	pjsip_expires_hdr *expires_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL);

	ast_assert(endpoint != NULL);

	if (!(publication = ao2_alloc(sizeof(*publication), publication_destroy_fn))) {
		return NULL;
	}

	if (!(publication->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp))) {
		ao2_ref(publication, -1);
		return NULL;
	}

	publication->entity_tag = ast_atomic_fetchadd_int(&esc_etag_counter, +1);
	ao2_ref(endpoint, +1);
	publication->endpoint = endpoint;
	publication->expires = expires_hdr ? expires_hdr->ivalue : DEFAULT_PUBLISH_EXPIRES;
	publication->sched_id = -1;

	return publication;
}

struct ast_sip_endpoint *ast_sip_publication_get_endpoint(struct ast_sip_publication *pub)
{
	return pub->endpoint;
}

int ast_sip_publication_create_response(struct ast_sip_publication *pub, int status_code, pjsip_rx_data *rdata,
	pjsip_tx_data **tdata)
{
	if (pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, status_code, NULL, tdata) != PJ_SUCCESS) {
		return -1;
	}

	if (PJSIP_IS_STATUS_IN_CLASS(status_code, 200)) {
		RAII_VAR(char *, entity_tag, NULL, ast_free_ptr);
		RAII_VAR(char *, expires, NULL, ast_free_ptr);

		if ((ast_asprintf(&entity_tag, "%d", pub->entity_tag) < 0) ||
			(ast_asprintf(&expires, "%d", pub->expires) < 0)) {
			pjsip_tx_data_dec_ref(*tdata);
			return -1;
		}

		ast_sip_add_header(*tdata, "SIP-ETag", entity_tag);
		ast_sip_add_header(*tdata, "Expires", expires);
	}

	return 0;
}

pj_status_t ast_sip_publication_send_response(struct ast_sip_publication *pub, pjsip_rx_data *rdata,
	pjsip_tx_data *tdata)
{
	pj_status_t status;
	pjsip_transaction *tsx;

	if ((status = pjsip_tsx_create_uas(&pubsub_module, rdata, &tsx)) != PJ_SUCCESS) {
		return status;
	}

	pjsip_tsx_recv_msg(tsx, rdata);

	return pjsip_tsx_send_msg(tsx, tdata);
}

int ast_sip_pubsub_register_body_generator(struct ast_sip_pubsub_body_generator *generator)
{
	struct ast_sip_pubsub_body_generator *existing;
	pj_str_t accept;
	pj_size_t accept_len;

	existing = find_body_generator_type_subtype(generator->type, generator->subtype);
	if (existing) {
		ast_log(LOG_WARNING, "Cannot register body generator of %s/%s."
				"One is already registered.\n", generator->type, generator->subtype);
		return -1;
	}

	AST_RWLIST_WRLOCK(&body_generators);
	AST_LIST_INSERT_HEAD(&body_generators, generator, list);
	AST_RWLIST_UNLOCK(&body_generators);

	/* Lengths of type and subtype plus space for a slash. pj_str_t is not
	 * null-terminated, so there is no need to allocate for the extra null
	 * byte
	 */
	accept_len = strlen(generator->type) + strlen(generator->subtype) + 1;

	accept.ptr = alloca(accept_len);
	accept.slen = accept_len;
	/* Safe use of sprintf */
	sprintf(accept.ptr, "%s/%s", generator->type, generator->subtype);
	pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), &pubsub_module,
			PJSIP_H_ACCEPT, NULL, 1, &accept);

	return 0;
}

void ast_sip_pubsub_unregister_body_generator(struct ast_sip_pubsub_body_generator *generator)
{
	struct ast_sip_pubsub_body_generator *iter;
	SCOPED_LOCK(lock, &body_generators, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&body_generators, iter, list) {
		if (iter == generator) {
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

int ast_sip_pubsub_register_body_supplement(struct ast_sip_pubsub_body_supplement *supplement)
{
	AST_RWLIST_WRLOCK(&body_supplements);
	AST_RWLIST_INSERT_TAIL(&body_supplements, supplement, list);
	AST_RWLIST_UNLOCK(&body_supplements);

	return 0;
}

void ast_sip_pubsub_unregister_body_supplement(struct ast_sip_pubsub_body_supplement *supplement)
{
	struct ast_sip_pubsub_body_supplement *iter;
	SCOPED_LOCK(lock, &body_supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&body_supplements, iter, list) {
		if (iter == supplement) {
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

const char *ast_sip_subscription_get_body_type(struct ast_sip_subscription *sub)
{
	return sub->body_generator->type;
}

const char *ast_sip_subscription_get_body_subtype(struct ast_sip_subscription *sub)
{
	return sub->body_generator->subtype;
}

int ast_sip_pubsub_generate_body_content(const char *type, const char *subtype,
		struct ast_sip_body_data *data, struct ast_str **str)
{
	struct ast_sip_pubsub_body_supplement *supplement;
	struct ast_sip_pubsub_body_generator *generator;
	int res = 0;
	void *body;

	generator = find_body_generator_type_subtype(type, subtype);
	if (!generator) {
		ast_log(LOG_WARNING, "Unable to find a body generator for %s/%s\n",
				type, subtype);
		return -1;
	}

	if (strcmp(data->body_type, generator->body_type)) {
		ast_log(LOG_WARNING, "Body generator does not accept the type of data provided\n");
		return -1;
	}

	body = generator->allocate_body(data->body_data);
	if (!body) {
		ast_log(LOG_WARNING, "Unable to allocate a NOTIFY body of type %s/%s\n",
				type, subtype);
		return -1;
	}

	if (generator->generate_body_content(body, data->body_data)) {
		res = -1;
		goto end;
	}

	AST_RWLIST_RDLOCK(&body_supplements);
	AST_RWLIST_TRAVERSE(&body_supplements, supplement, list) {
		if (!strcmp(generator->type, supplement->type) &&
				!strcmp(generator->subtype, supplement->subtype)) {
			res = supplement->supplement_body(body, data->body_data);
			if (res) {
				break;
			}
		}
	}
	AST_RWLIST_UNLOCK(&body_supplements);

	if (!res) {
		generator->to_string(body, str);
	}

end:
	if (generator->destroy_body) {
		generator->destroy_body(body);
	}

	return res;
}

static pj_bool_t pubsub_on_rx_request(pjsip_rx_data *rdata)
{
	if (!pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, pjsip_get_subscribe_method())) {
		return pubsub_on_rx_subscribe_request(rdata);
	} else if (!pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_publish_method)) {
		return pubsub_on_rx_publish_request(rdata);
	}

	return PJ_FALSE;
}

static void pubsub_on_evsub_state(pjsip_evsub *evsub, pjsip_event *event)
{
	struct ast_sip_subscription *sub;
	if (pjsip_evsub_get_state(evsub) != PJSIP_EVSUB_STATE_TERMINATED) {
		return;
	}

	sub = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);
	if (!sub) {
		return;
	}

	if (sub->handler->subscription_shutdown) {
		sub->handler->subscription_shutdown(sub);
	}
	pjsip_evsub_set_mod_data(evsub, pubsub_module.id, NULL);
}

static void pubsub_on_tsx_state(pjsip_evsub *evsub, pjsip_transaction *tsx, pjsip_event *event)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);

	if (!sub) {
		return;
	}

	if (sub->handler->notify_response && tsx->role == PJSIP_ROLE_UAC &&
	    event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		sub->handler->notify_response(sub, event->body.tsx_state.src.rdata);
	}
}

static void set_parameters_from_response_data(pj_pool_t *pool, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body,
		struct ast_sip_subscription_response_data *response_data)
{
	ast_assert(response_data->status_code >= 200 && response_data->status_code <= 699);
	*p_st_code = response_data->status_code;

	if (!ast_strlen_zero(response_data->status_text)) {
		pj_strdup2(pool, *p_st_text, response_data->status_text);
	}

	if (response_data->headers) {
		struct ast_variable *iter;
		for (iter = response_data->headers; iter; iter = iter->next) {
			pj_str_t header_name;
			pj_str_t header_value;
			pjsip_generic_string_hdr *hdr;

			pj_cstr(&header_name, iter->name);
			pj_cstr(&header_value, iter->value);
			hdr = pjsip_generic_string_hdr_create(pool, &header_name, &header_value);
			pj_list_insert_before(res_hdr, hdr);
		}
	}

	if (response_data->body) {
		pj_str_t type;
		pj_str_t subtype;
		pj_str_t body_text;

		pj_cstr(&type, response_data->body->type);
		pj_cstr(&subtype, response_data->body->subtype);
		pj_cstr(&body_text, response_data->body->body_text);

		*p_body = pjsip_msg_body_create(pool, &type, &subtype, &body_text);
	}
}

static int response_data_changed(struct ast_sip_subscription_response_data *response_data)
{
	if (response_data->status_code != 200 ||
			!ast_strlen_zero(response_data->status_text) ||
			response_data->headers ||
			response_data->body) {
		return 1;
	}
	return 0;
}

static void pubsub_on_rx_refresh(pjsip_evsub *evsub, pjsip_rx_data *rdata,
		int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);
	struct ast_sip_subscription_response_data response_data = {
		.status_code = 200,
	};

	if (!sub) {
		return;
	}

	if (pjsip_evsub_get_state(sub->evsub) == PJSIP_EVSUB_STATE_TERMINATED) {
		sub->handler->subscription_terminated(sub, rdata);
		return;
	}

	sub->handler->resubscribe(sub, rdata, &response_data);

	if (!response_data_changed(&response_data)) {
		return;
	}

	set_parameters_from_response_data(rdata->tp_info.pool, p_st_code, p_st_text,
			res_hdr, p_body, &response_data);
}

static void pubsub_on_rx_notify(pjsip_evsub *evsub, pjsip_rx_data *rdata, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);
	struct ast_sip_subscription_response_data response_data = {
		.status_code = 200,
	};

	if (!sub || !sub->handler->notify_request) {
		return;
	}

	sub->handler->notify_request(sub, rdata, &response_data);

	if (!response_data_changed(&response_data)) {
		return;
	}

	set_parameters_from_response_data(rdata->tp_info.pool, p_st_code, p_st_text,
			res_hdr, p_body, &response_data);
}

static int serialized_pubsub_on_client_refresh(void *userdata)
{
	struct ast_sip_subscription *sub = userdata;

	sub->handler->refresh_subscription(sub);
	ao2_cleanup(sub);
	return 0;
}

static void pubsub_on_client_refresh(pjsip_evsub *evsub)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);

	ao2_ref(sub, +1);
	ast_sip_push_task(sub->serializer, serialized_pubsub_on_client_refresh, sub);
}

static int serialized_pubsub_on_server_timeout(void *userdata)
{
	struct ast_sip_subscription *sub = userdata;

	sub->handler->subscription_timeout(sub);
	ao2_cleanup(sub);
	return 0;
}

static void pubsub_on_server_timeout(pjsip_evsub *evsub)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);

	if (!sub) {
		/* if a subscription has been terminated and the subscription
		   timeout/expires is less than the time it takes for all pending
		   transactions to end then the subscription timer will not have
		   been canceled yet and sub will be null, so do nothing since
		   the subscription has already been terminated. */
		return;
	}

	ao2_ref(sub, +1);
	ast_sip_push_task(sub->serializer, serialized_pubsub_on_server_timeout, sub);
}

static int ami_subscription_detail(struct ast_sip_subscription *sub,
				   struct ast_sip_ami *ami,
				   const char *event)
{
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event(event, ami), ast_free);

	if (!buf) {
		return -1;
	}

	sip_subscription_to_ami(sub, &buf);
	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	return 0;
}

static int ami_subscription_detail_inbound(struct ast_sip_subscription *sub, void *arg)
{
	return sub->role == AST_SIP_NOTIFIER ? ami_subscription_detail(
		sub, arg, "InboundSubscriptionDetail") : 0;
}

static int ami_subscription_detail_outbound(struct ast_sip_subscription *sub, void *arg)
{
	return sub->role == AST_SIP_SUBSCRIBER ? ami_subscription_detail(
		sub, arg, "OutboundSubscriptionDetail") : 0;
}

static int ami_show_subscriptions_inbound(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	int num;

	astman_send_listack(s, m, "Following are Events for "
			    "each inbound Subscription", "start");

	num = for_each_subscription(ami_subscription_detail_inbound, &ami);

	astman_append(s, "Event: InboundSubscriptionDetailComplete\r\n");
	if (!ast_strlen_zero(ami.action_id)) {
		astman_append(s, "ActionID: %s\r\n", ami.action_id);
	}
	astman_append(s, "EventList: Complete\r\n"
		      "ListItems: %d\r\n\r\n", num);
	return 0;
}

static int ami_show_subscriptions_outbound(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	int num;

	astman_send_listack(s, m, "Following are Events for "
			    "each outbound Subscription", "start");

	num = for_each_subscription(ami_subscription_detail_outbound, &ami);

	astman_append(s, "Event: OutboundSubscriptionDetailComplete\r\n");
	if (!ast_strlen_zero(ami.action_id)) {
		astman_append(s, "ActionID: %s\r\n", ami.action_id);
	}
	astman_append(s, "EventList: Complete\r\n"
		      "ListItems: %d\r\n\r\n", num);
	return 0;
}

#define AMI_SHOW_SUBSCRIPTIONS_INBOUND "PJSIPShowSubscriptionsInbound"
#define AMI_SHOW_SUBSCRIPTIONS_OUTBOUND "PJSIPShowSubscriptionsOutbound"

static int persistence_endpoint_str2struct(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct subscription_persistence *persistence = obj;

	persistence->endpoint = ast_strdup(var->value);
	return 0;
}

static int persistence_endpoint_struct2str(const void *obj, const intptr_t *args, char **buf)
{
	const struct subscription_persistence *persistence = obj;

	*buf = ast_strdup(persistence->endpoint);
	return 0;
}

static int persistence_tag_str2struct(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct subscription_persistence *persistence = obj;

	persistence->tag = ast_strdup(var->value);
	return 0;
}

static int persistence_tag_struct2str(const void *obj, const intptr_t *args, char **buf)
{
	const struct subscription_persistence *persistence = obj;

	*buf = ast_strdup(persistence->tag);
	return 0;
}

static int persistence_expires_str2struct(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct subscription_persistence *persistence = obj;
	return ast_get_timeval(var->value, &persistence->expires, ast_tv(0, 0), NULL);
}

static int persistence_expires_struct2str(const void *obj, const intptr_t *args, char **buf)
{
	const struct subscription_persistence *persistence = obj;
	return (ast_asprintf(buf, "%ld", persistence->expires.tv_sec) < 0) ? -1 : 0;
}

static int load_module(void)
{
	static const pj_str_t str_PUBLISH = { "PUBLISH", 7 };
	struct ast_sorcery *sorcery;

	CHECK_PJSIP_MODULE_LOADED();

	sorcery = ast_sip_get_sorcery();

	pjsip_evsub_init_module(ast_sip_get_pjsip_endpoint());

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Could not create scheduler for publication expiration\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Could not start scheduler thread for publication expiration\n");
		ast_sched_context_destroy(sched);
		return AST_MODULE_LOAD_FAILURE;
	}

	pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), NULL, PJSIP_H_ALLOW, NULL, 1, &str_PUBLISH);

	if (ast_sip_register_service(&pubsub_module)) {
		ast_log(LOG_ERROR, "Could not register pubsub service\n");
		ast_sched_context_destroy(sched);
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_sorcery_apply_config(sorcery, "res_pjsip_pubsub");
	ast_sorcery_apply_default(sorcery, "subscription_persistence", "astdb", "subscription_persistence");
	if (ast_sorcery_object_register(sorcery, "subscription_persistence", subscription_persistence_alloc,
		NULL, NULL)) {
		ast_log(LOG_ERROR, "Could not register subscription persistence object support\n");
		ast_sip_unregister_service(&pubsub_module);
		ast_sched_context_destroy(sched);
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_sorcery_object_field_register(sorcery, "subscription_persistence", "packet", "", OPT_CHAR_ARRAY_T, 0,
		CHARFLDSET(struct subscription_persistence, packet));
	ast_sorcery_object_field_register(sorcery, "subscription_persistence", "src_name", "", OPT_CHAR_ARRAY_T, 0,
		CHARFLDSET(struct subscription_persistence, src_name));
	ast_sorcery_object_field_register(sorcery, "subscription_persistence", "src_port", "0", OPT_UINT_T, 0,
		FLDSET(struct subscription_persistence, src_port));
	ast_sorcery_object_field_register(sorcery, "subscription_persistence", "transport_key", "0", OPT_CHAR_ARRAY_T, 0,
		CHARFLDSET(struct subscription_persistence, transport_key));
	ast_sorcery_object_field_register(sorcery, "subscription_persistence", "local_name", "", OPT_CHAR_ARRAY_T, 0,
		CHARFLDSET(struct subscription_persistence, local_name));
	ast_sorcery_object_field_register(sorcery, "subscription_persistence", "local_port", "0", OPT_UINT_T, 0,
		FLDSET(struct subscription_persistence, local_port));
	ast_sorcery_object_field_register(sorcery, "subscription_persistence", "cseq", "0", OPT_UINT_T, 0,
		FLDSET(struct subscription_persistence, cseq));
	ast_sorcery_object_field_register_custom(sorcery, "subscription_persistence", "endpoint", "",
		persistence_endpoint_str2struct, persistence_endpoint_struct2str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "subscription_persistence", "tag", "",
		persistence_tag_str2struct, persistence_tag_struct2str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "subscription_persistence", "expires", "",
		persistence_expires_str2struct, persistence_expires_struct2str, NULL, 0, 0);

	if (ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		ast_sip_push_task(NULL, subscription_persistence_load, NULL);
	} else {
		stasis_subscribe(ast_manager_get_topic(), subscription_persistence_event_cb, NULL);
	}

	ast_manager_register_xml(AMI_SHOW_SUBSCRIPTIONS_INBOUND, EVENT_FLAG_SYSTEM,
				 ami_show_subscriptions_inbound);
	ast_manager_register_xml(AMI_SHOW_SUBSCRIPTIONS_OUTBOUND, EVENT_FLAG_SYSTEM,
				 ami_show_subscriptions_outbound);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_manager_unregister(AMI_SHOW_SUBSCRIPTIONS_OUTBOUND);
	ast_manager_unregister(AMI_SHOW_SUBSCRIPTIONS_INBOUND);

	if (sched) {
		ast_sched_context_destroy(sched);
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP event resource",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
