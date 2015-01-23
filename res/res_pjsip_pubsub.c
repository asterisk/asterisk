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
#include "asterisk/res_pjsip_presence_xml.h"

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
	<manager name="PJSIPShowResourceLists" language="en_US">
		<synopsis>
			Displays settings for configured resource lists.
		</synopsis>
		<syntax />
		<description>
			<para>
			Provides a listing of all resource lists.  An event <literal>ResourceListDetail</literal>
			is issued for each resource list object.  Once all detail events are completed a
			<literal>ResourceListDetailComplete</literal> event is issued.
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
			<configObject name="resource_list">
				<synopsis>Resource list configuration parameters.</synopsis>
				<description>
					<para>This configuration object allows for RFC 4662 resource list subscriptions
					to be specified. This can be useful to decrease the amount of subscription traffic
					that a server has to process.</para>
					<note>
						<para>Current limitations limit the size of SIP NOTIFY requests that Asterisk sends
						to 64000 bytes. If your resource list notifications are larger than this maximum, you
						will need to make adjustments.</para>
					</note>
				</description>
				<configOption name="type">
					<synopsis>Must be of type 'resource_list'</synopsis>
				</configOption>
				<configOption name="event">
					<synopsis>The SIP event package that the list resource belong to.</synopsis>
					<description><para>
						The SIP event package describes the types of resources that Asterisk reports
						the state of.
					</para>
						<enumlist>
							<enum name="presence"><para>
								Device state and presence reporting.
							</para></enum>
							<enum name="message-summary"><para>
								Message-waiting indication (MWI) reporting.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="list_item">
					<synopsis>The name of a resource to report state on</synopsis>
					<description>
						<para>In general Asterisk looks up list items in the following way:</para>
						<para>1. Check if the list item refers to another configured resource list.</para>
						<para>2. Pass the name of the resource off to event-package-specific handlers
						   to find the specified resource.</para>
						<para>The second part means that the way the list item is specified depends
						on what type of list this is. For instance, if you have the <replaceable>event</replaceable>
						set to <literal>presence</literal>, then list items should be in the form of
						dialplan_extension@dialplan_context. For <literal>message-summary</literal> mailbox
						names should be listed.</para>
					</description>
				</configOption>
				<configOption name="full_state" default="no">
					<synopsis>Indicates if the entire list's state should be sent out.</synopsis>
					<description>
						<para>If this option is enabled, and a resource changes state, then Asterisk will construct
						a notification that contains the state of all resources in the list. If the option is
						disabled, Asterisk will construct a notification that only contains the states of
						resources that have changed.</para>
						<note>
							<para>Even with this option disabled, there are certain situations where Asterisk is forced
							to send a notification with the states of all resources in the list. When a subscriber
							renews or terminates its subscription to the list, Asterisk MUST send a full state
							notification.</para>
						</note>
					</description>
				</configOption>
				<configOption name="notification_batch_interval" default="0">
					<synopsis>Time Asterisk should wait, in milliseconds, before sending notifications.</synopsis>
					<description>
						<para>When a resource's state changes, it may be desired to wait a certain amount before Asterisk
						sends a notification to subscribers. This allows for other state changes to accumulate, so that
						Asterisk can communicate multiple state changes in a single notification instead of rapidly sending
						many notifications.</para>
					</description>
				</configOption>
			</configObject>
			<configObject name="inbound-publication">
				<synopsis>The configuration for inbound publications</synopsis>
				<configOption name="endpoint" default="">
					<synopsis>Optional name of an endpoint that is only allowed to publish to this resource</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'inbound-publication'.</synopsis>
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

#define MOD_DATA_PERSISTENCE "sub_persistence"
#define MOD_DATA_MSG "sub_msg"

static const pj_str_t str_event_name = { "Event", 5 };

/*! \brief Scheduler used for automatically expiring publications */
static struct ast_sched_context *sched;

/*! \brief Number of buckets for publications (on a per handler) */
#define PUBLICATIONS_BUCKETS 37

/*! \brief Default expiration time for PUBLISH if one is not specified */
#define DEFAULT_PUBLISH_EXPIRES 3600

/*! \brief Number of buckets for subscription datastore */
#define DATASTORE_BUCKETS 53

/*! \brief Default expiration for subscriptions */
#define DEFAULT_EXPIRES 3600

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
 * \brief A vector of strings commonly used throughout this module
 */
AST_VECTOR(resources, const char *);

/*!
 * \brief Resource list configuration item
 */
struct resource_list {
	SORCERY_OBJECT(details);
	/*! SIP event package the list uses. */
	char event[32];
	/*! Strings representing resources in the list. */
	struct resources items;
	/*! Indicates if Asterisk sends full or partial state on notifications. */
	unsigned int full_state;
	/*! Time, in milliseconds Asterisk waits before sending a batched notification.*/
	unsigned int notification_batch_interval;
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
	/*! \brief The resource the publication is to */
	char *resource;
	/*! \brief The name of the event type configuration */
	char *event_configuration_name;
	/*! \brief Data containing the above */
	char data[0];
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
 * \brief A tree of SIP subscriptions
 *
 * Because of the ability to subscribe to resource lists, a SIP
 * subscription can result in a tree of subscriptions being created.
 * This structure represents the information relevant to the subscription
 * as a whole, to include the underlying PJSIP structure for the
 * subscription.
 */
struct sip_subscription_tree {
	/*! The endpoint with which the subscription is communicating */
	struct ast_sip_endpoint *endpoint;
	/*! Serializer on which to place operations for this subscription */
	struct ast_taskprocessor *serializer;
	/*! The role for this subscription */
	enum ast_sip_subscription_role role;
	/*! Persistence information */
	struct subscription_persistence *persistence;
	/*! The underlying PJSIP event subscription structure */
	pjsip_evsub *evsub;
	/*! The underlying PJSIP dialog */
	pjsip_dialog *dlg;
	/*! Interval to use for batching notifications */
	unsigned int notification_batch_interval;
	/*! Scheduler ID for batched notification */
	int notify_sched_id;
	/*! Indicator if scheduled batched notification should be sent */
	unsigned int send_scheduled_notify;
	/*! The root of the subscription tree */
	struct ast_sip_subscription *root;
	/*! Is this subscription to a list? */
	int is_list;
	/*! Next item in the list */
	AST_LIST_ENTRY(sip_subscription_tree) next;
};

/*!
 * \brief Structure representing a "virtual" SIP subscription.
 *
 * This structure serves a dual purpose. Structurally, it is
 * the constructed tree of subscriptions based on the resources
 * being subscribed to. API-wise, this serves as the handle that
 * subscription handlers use in order to interact with the pubsub API.
 */
struct ast_sip_subscription {
	/*! Subscription datastores set up by handlers */
	struct ao2_container *datastores;
	/*! The handler for this subscription */
	const struct ast_sip_subscription_handler *handler;
	/*! Pointer to the base of the tree */
	struct sip_subscription_tree *tree;
	/*! Body generaator for NOTIFYs */
	struct ast_sip_pubsub_body_generator *body_generator;
	/*! Vector of child subscriptions */
	AST_VECTOR(, struct ast_sip_subscription *) children;
	/*! Saved NOTIFY body text for this subscription */
	struct ast_str *body_text;
	/*! Indicator that the body text has changed since the last notification */
	int body_changed;
	/*! The current state of the subscription */
	pjsip_evsub_state subscription_state;
	/*! For lists, the current version to place in the RLMI body */
	unsigned int version;
	/*! For lists, indicates if full state should always be communicated. */
	unsigned int full_state;
	/*! URI associated with the subscription */
	pjsip_sip_uri *uri;
	/*! Name of resource being subscribed to */
	char resource[0];
};

/*!
 * \brief Structure representing a publication resource
 */
struct ast_sip_publication_resource {
	/*! \brief Sorcery object details */
	SORCERY_OBJECT(details);
	/*! \brief Optional name of an endpoint that is only allowed to publish to this resource */
	char *endpoint;
	/*! \brief Mapping for event types to configuration */
	struct ast_variable *events;
};

static const char *sip_subscription_roles_map[] = {
	[AST_SIP_SUBSCRIBER] = "Subscriber",
	[AST_SIP_NOTIFIER] = "Notifier"
};

AST_RWLIST_HEAD_STATIC(subscriptions, sip_subscription_tree);

AST_RWLIST_HEAD_STATIC(body_generators, ast_sip_pubsub_body_generator);
AST_RWLIST_HEAD_STATIC(body_supplements, ast_sip_pubsub_body_supplement);

static void pubsub_on_evsub_state(pjsip_evsub *sub, pjsip_event *event);
static void pubsub_on_rx_refresh(pjsip_evsub *sub, pjsip_rx_data *rdata,
		int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);
static void pubsub_on_rx_notify(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);
static void pubsub_on_client_refresh(pjsip_evsub *sub);
static void pubsub_on_server_timeout(pjsip_evsub *sub);
 
static pjsip_evsub_user pubsub_cb = {
	.on_evsub_state = pubsub_on_evsub_state,
	.on_rx_refresh = pubsub_on_rx_refresh,
	.on_rx_notify = pubsub_on_rx_notify,
	.on_client_refresh = pubsub_on_client_refresh,
	.on_server_timeout = pubsub_on_server_timeout,
};

/*! \brief Destructor for publication resource */
static void publication_resource_destroy(void *obj)
{
	struct ast_sip_publication_resource *resource = obj;

	ast_free(resource->endpoint);
	ast_variables_destroy(resource->events);
}

/*! \brief Allocator for publication resource */
static void *publication_resource_alloc(const char *name)
{
	return ast_sorcery_generic_alloc(sizeof(struct ast_sip_publication_resource), publication_resource_destroy);
}

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
static struct subscription_persistence *subscription_persistence_create(struct sip_subscription_tree *sub_tree)
{
	char tag[PJ_GUID_STRING_LENGTH + 1];

	/* The id of this persistence object doesn't matter as we keep it on the subscription and don't need to
	 * look it up by id at all.
	 */
	struct subscription_persistence *persistence = ast_sorcery_alloc(ast_sip_get_sorcery(),
		"subscription_persistence", NULL);

	pjsip_dialog *dlg = sub_tree->dlg;

	if (!persistence) {
		return NULL;
	}

	persistence->endpoint = ast_strdup(ast_sorcery_object_get_id(sub_tree->endpoint));
	ast_copy_pj_str(tag, &dlg->local.info->tag, sizeof(tag));
	persistence->tag = ast_strdup(tag);

	ast_sorcery_create(ast_sip_get_sorcery(), persistence);
	return persistence;
}

/*! \brief Function which updates persistence information of a subscription in sorcery */
static void subscription_persistence_update(struct sip_subscription_tree *sub_tree,
	pjsip_rx_data *rdata)
{
	pjsip_dialog *dlg;

	if (!sub_tree->persistence) {
		return;
	}

	dlg = sub_tree->dlg;
	sub_tree->persistence->cseq = dlg->local.cseq;

	if (rdata) {
		int expires;
		pjsip_expires_hdr *expires_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL);

		expires = expires_hdr ? expires_hdr->ivalue : DEFAULT_PUBLISH_EXPIRES;
		sub_tree->persistence->expires = ast_tvadd(ast_tvnow(), ast_samp2tv(expires, 1));

		ast_copy_string(sub_tree->persistence->packet, rdata->pkt_info.packet,
				sizeof(sub_tree->persistence->packet));
		ast_copy_string(sub_tree->persistence->src_name, rdata->pkt_info.src_name,
				sizeof(sub_tree->persistence->src_name));
		sub_tree->persistence->src_port = rdata->pkt_info.src_port;
		ast_copy_string(sub_tree->persistence->transport_key, rdata->tp_info.transport->type_name,
			sizeof(sub_tree->persistence->transport_key));
		ast_copy_pj_str(sub_tree->persistence->local_name, &rdata->tp_info.transport->local_name.host,
			sizeof(sub_tree->persistence->local_name));
		sub_tree->persistence->local_port = rdata->tp_info.transport->local_name.port;
	}

	ast_sorcery_update(ast_sip_get_sorcery(), sub_tree->persistence);
}

/*! \brief Function which removes persistence of a subscription from sorcery */
static void subscription_persistence_remove(struct sip_subscription_tree *sub_tree)
{
	if (!sub_tree->persistence) {
		return;
	}

	ast_sorcery_delete(ast_sip_get_sorcery(), sub_tree->persistence);
	ao2_ref(sub_tree->persistence, -1);
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

/*!
 * \brief Accept headers that are exceptions to the rule
 *
 * Typically, when a SUBSCRIBE arrives, we attempt to find a
 * body generator that matches one of the Accept headers in
 * the request. When subscribing to a single resource, this works
 * great. However, when subscribing to a list, things work
 * differently. Most Accept header values are fine, but there
 * are a couple that are endemic to resource lists that need
 * to be ignored when searching for a body generator to use
 * for the individual resources of the subscription.
 */
const char *accept_exceptions[] =  {
	"multipart/related",
	"application/rlmi+xml",
};

/*!
 * \brief Is the Accept header from the SUBSCRIBE in the list of exceptions?
 *
 * \retval 1 This Accept header value is an exception to the rule.
 * \retval 0 This Accept header is not an exception to the rule.
 */
static int exceptional_accept(const pj_str_t *accept)
{
	int i;

	for (i = 0; i < ARRAY_LEN(accept_exceptions); ++i) {
		if (!pj_strcmp2(accept, accept_exceptions[i])) {
			return 1;
		}
	}

	return 0;
}

/*! \brief Retrieve a body generator using the Accept header of an rdata message */
static struct ast_sip_pubsub_body_generator *subscription_get_generator_from_rdata(pjsip_rx_data *rdata,
	const struct ast_sip_subscription_handler *handler)
{
	pjsip_accept_hdr *accept_header = (pjsip_accept_hdr *) &rdata->msg_info.msg->hdr;
	char accept[AST_SIP_MAX_ACCEPT][64];
	size_t num_accept_headers = 0;

	while ((accept_header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_ACCEPT, accept_header->next))) {
		int i;

		for (i = 0; i < accept_header->count; ++i) {
			if (!exceptional_accept(&accept_header->values[i])) {
				ast_copy_pj_str(accept[num_accept_headers], &accept_header->values[i], sizeof(accept[num_accept_headers]));
				++num_accept_headers;
			}
		}
	}

	if (num_accept_headers == 0) {
		/* If a SUBSCRIBE contains no Accept headers, then we must assume that
		 * the default accept type for the event package is to be used.
		 */
		ast_copy_string(accept[0], handler->notifier->default_accept, sizeof(accept[0]));
		num_accept_headers = 1;
	}

	return find_body_generator(accept, num_accept_headers, handler->body_type);
}

/*! \brief Check if the rdata has a Supported header containing 'eventlist'
 *
 *  \retval 1 rdata has an eventlist containing supported header
 *  \retval 0 rdata doesn't have an eventlist containing supported header
 */
static int ast_sip_pubsub_has_eventlist_support(pjsip_rx_data *rdata)
{
	pjsip_supported_hdr *supported_header = (pjsip_supported_hdr *) &rdata->msg_info.msg->hdr;

	while ((supported_header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_SUPPORTED, supported_header->next))) {
		int i;

		for (i = 0; i < supported_header->count; i++) {
			if (!pj_stricmp2(&supported_header->values[i], "eventlist")) {
				return 1;
			}
		}
	}

	return 0;
}

struct resource_tree;

/*!
 * \brief A node for a resource tree.
 */
struct tree_node {
	AST_VECTOR(, struct tree_node *) children;
	unsigned int full_state;
	char resource[0];
};

/*!
 * \brief Helper function for retrieving a resource list for a given event.
 *
 * This will retrieve a resource list that corresponds to the resource and event provided.
 *
 * \param resource The name of the resource list to retrieve
 * \param event The expected event name on the resource list
 */
static struct resource_list *retrieve_resource_list(const char *resource, const char *event)
{
	struct resource_list *list;

	list = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "resource_list", resource);
	if (!list) {
		return NULL;
	}

	if (strcmp(list->event, event)) {
		ast_log(LOG_WARNING, "Found resource list %s, but its event type (%s) does not match SUBSCRIBE's (%s)\n",
				resource, list->event, event);
		ao2_cleanup(list);
		return NULL;
	}

	return list;
}

/*!
 * \brief Allocate a tree node
 *
 * In addition to allocating and initializing the tree node, the node is also added
 * to the vector of visited resources. See \ref build_resource_tree for more information
 * on the visited resources.
 *
 * \param resource The name of the resource for this tree node.
 * \param visited The vector of resources that have been visited.
 * \param if allocating a list, indicate whether full state is requested in notifications.
 * \retval NULL Allocation failure.
 * \retval non-NULL The newly-allocated tree_node
 */
static struct tree_node *tree_node_alloc(const char *resource, struct resources *visited, unsigned int full_state)
{
	struct tree_node *node;

	node = ast_calloc(1, sizeof(*node) + strlen(resource) + 1);
	if (!node) {
		return NULL;
	}

	strcpy(node->resource, resource);
	if (AST_VECTOR_INIT(&node->children, 4)) {
		ast_free(node);
		return NULL;
	}
	node->full_state = full_state;

	if (visited) {
		AST_VECTOR_APPEND(visited, resource);
	}
	return node;
}

/*!
 * \brief Destructor for a tree node
 *
 * This function calls recursively in order to destroy
 * all nodes lower in the tree from the given node in
 * addition to the node itself.
 *
 * \param node The node to destroy.
 */
static void tree_node_destroy(struct tree_node *node)
{
	int i;
	if (!node) {
		return;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&node->children); ++i) {
		tree_node_destroy(AST_VECTOR_GET(&node->children, i));
	}
	AST_VECTOR_FREE(&node->children);
	ast_free(node);
}

/*!
 * \brief Determine if this resource has been visited already
 *
 * See \ref build_resource_tree for more information
 *
 * \param resource The resource currently being visited
 * \param visited The resources that have previously been visited
 */
static int have_visited(const char *resource, struct resources *visited)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(visited); ++i) {
		if (!strcmp(resource, AST_VECTOR_GET(visited, i))) {
			return 1;
		}
	}

	return 0;
}

/*!
 * \brief Build child nodes for a given parent.
 *
 * This iterates through the items on a resource list and creates tree nodes for each one. The
 * tree nodes created are children of the supplied parent node. If an item in the resource
 * list is itself a list, then this function is called recursively to provide children for
 * the the new node.
 *
 * If an item in a resource list is not a list, then the supplied subscription handler is
 * called into as if a new SUBSCRIBE for the list item were presented. The handler's response
 * is used to determine if the node can be added to the tree or not.
 *
 * If a parent node ends up having no child nodes added under it, then the parent node is
 * pruned from the tree.
 *
 * \param endpoint The endpoint that sent the inbound SUBSCRIBE.
 * \param handler The subscription handler for leaf nodes in the tree.
 * \param list The configured resource list from which the child node is being built.
 * \param parent The parent node for these children.
 * \param visited The resources that have already been visited.
 */
static void build_node_children(struct ast_sip_endpoint *endpoint, const struct ast_sip_subscription_handler *handler,
		struct resource_list *list, struct tree_node *parent, struct resources *visited)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&list->items); ++i) {
		struct tree_node *current;
		struct resource_list *child_list;
		const char *resource = AST_VECTOR_GET(&list->items, i);

		if (have_visited(resource, visited)) {
			ast_debug(1, "Already visited resource %s. Avoiding duplicate resource or potential loop.\n", resource);
			continue;
		}

		child_list = retrieve_resource_list(resource, list->event);
		if (!child_list) {
			int resp = handler->notifier->new_subscribe(endpoint, resource);
			if (PJSIP_IS_STATUS_IN_CLASS(resp, 200)) {
				current = tree_node_alloc(resource, visited, 0);
				if (!current) {
					ast_debug(1, "Subscription to leaf resource %s was successful, but encountered"
							"allocation error afterwards\n", resource);
					continue;
				}
				ast_debug(1, "Subscription to leaf resource %s resulted in success. Adding to parent %s\n",
						resource, parent->resource);
				AST_VECTOR_APPEND(&parent->children, current);
			} else {
				ast_debug(1, "Subscription to leaf resource %s resulted in error response %d\n",
						resource, resp);
			}
		} else {
			ast_debug(1, "Resource %s (child of %s) is a list\n", resource, parent->resource);
			current = tree_node_alloc(resource, visited, child_list->full_state);
			if (!current) {
				ast_debug(1, "Cannot build children of resource %s due to allocation failure\n", resource);
				continue;
			}
			build_node_children(endpoint, handler, child_list, current, visited);
			if (AST_VECTOR_SIZE(&current->children) > 0) {
				ast_debug(1, "List %s had no successful children.\n", resource);
				AST_VECTOR_APPEND(&parent->children, current);
			} else {
				ast_debug(1, "List %s had successful children. Adding to parent %s\n",
						resource, parent->resource);
				tree_node_destroy(current);
			}
			ao2_cleanup(child_list);
		}
	}
}

/*!
 * \brief A resource tree
 *
 * When an inbound SUBSCRIBE arrives, the resource being subscribed to may
 * be a resource list. If this is the case, the resource list may contain resources
 * that are themselves lists. The structure needed to hold the resources is
 * a tree.
 *
 * Upon receipt of the SUBSCRIBE, the tree is built by determining if subscriptions
 * to the individual resources in the tree would be successful or not. Any successful
 * subscriptions result in a node in the tree being created. Any unsuccessful subscriptions
 * result in no node being created.
 *
 * This tree can be seen as a bare-bones analog of the tree of ast_sip_subscriptions that
 * will end up being created to actually carry out the duties of a SIP SUBSCRIBE dialog.
 */
struct resource_tree {
	struct tree_node *root;
	unsigned int notification_batch_interval;
};

/*!
 * \brief Destroy a resource tree.
 *
 * This function makes no assumptions about how the tree itself was
 * allocated and does not attempt to free the tree itself. Callers
 * of this function are responsible for freeing the tree.
 *
 * \param tree The tree to destroy.
 */
static void resource_tree_destroy(struct resource_tree *tree)
{
	if (tree) {
		tree_node_destroy(tree->root);
	}
}

/*!
 * \brief Build a resource tree
 *
 * This function builds a resource tree based on the requested resource in a SUBSCRIBE request.
 *
 * This function also creates a container that has all resources that have been visited during
 * creation of the tree, whether those resources resulted in a tree node being created or not.
 * Keeping this container of visited resources allows for misconfigurations such as loops in
 * the tree or duplicated resources to be detected.
 *
 * \param endpoint The endpoint that sent the SUBSCRIBE request.
 * \param handler The subscription handler for leaf nodes in the tree.
 * \param resource The resource requested in the SUBSCRIBE request.
 * \param tree The tree that is to be built.
 * \param has_eventlist_support
 *
 * \retval 200-299 Successfully subscribed to at least one resource.
 * \retval 300-699 Failure to subscribe to requested resource.
 */
static int build_resource_tree(struct ast_sip_endpoint *endpoint, const struct ast_sip_subscription_handler *handler,
		const char *resource, struct resource_tree *tree, int has_eventlist_support)
{
	RAII_VAR(struct resource_list *, list, NULL, ao2_cleanup);
	struct resources visited;

	if (!has_eventlist_support || !(list = retrieve_resource_list(resource, handler->event_name))) {
		ast_debug(1, "Subscription to resource %s is not to a list\n", resource);
		tree->root = tree_node_alloc(resource, NULL, 0);
		if (!tree->root) {
			return 500;
		}
		return handler->notifier->new_subscribe(endpoint, resource);
	}

	ast_debug(1, "Subscription to resource %s is a list\n", resource);
	if (AST_VECTOR_INIT(&visited, AST_VECTOR_SIZE(&list->items))) {
		return 500;
	}

	tree->root = tree_node_alloc(resource, &visited, list->full_state);
	if (!tree->root) {
		return 500;
	}

	tree->notification_batch_interval = list->notification_batch_interval;

	build_node_children(endpoint, handler, list, tree->root, &visited);
	AST_VECTOR_FREE(&visited);

	if (AST_VECTOR_SIZE(&tree->root->children) > 0) {
		return 200;
	} else {
		return 500;
	}
}

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
	struct sip_subscription_tree *sub_tree = obj;

	/* This is why we keep the dialog on the subscription. When the subscription
	 * is destroyed, there is no guarantee that the underlying dialog is ready
	 * to be destroyed. Furthermore, there's no guarantee in the opposite direction
	 * either. The dialog could be destroyed before our subscription is. We fix
	 * this problem by keeping a reference to the dialog until it is time to
	 * destroy the subscription. We need to have the dialog available when the
	 * subscription is destroyed so that we can guarantee that our attempt to
	 * remove the serializer will be successful.
	 */
	ast_sip_dialog_set_serializer(sub_tree->dlg, NULL);
	pjsip_dlg_dec_session(sub_tree->dlg, &pubsub_module);

	return 0;
}

static void add_subscription(struct sip_subscription_tree *obj)
{
	SCOPED_LOCK(lock, &subscriptions, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_INSERT_TAIL(&subscriptions, obj, next);
}

static void remove_subscription(struct sip_subscription_tree *obj)
{
	struct sip_subscription_tree *i;
	SCOPED_LOCK(lock, &subscriptions, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&subscriptions, i, next) {
		if (i == obj) {
			AST_RWLIST_REMOVE_CURRENT(next);
			if (i->root) {
				ast_debug(1, "Removing subscription to resource %s from list of subscriptions\n",
						ast_sip_subscription_get_resource_name(i->root));
			}
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static void subscription_destructor(void *obj)
{
	struct ast_sip_subscription *sub = obj;

	ast_debug(3, "Destroying SIP subscription to resource %s\n", sub->resource);
	ast_free(sub->body_text);

	ao2_cleanup(sub->datastores);
}

static struct ast_sip_subscription *allocate_subscription(const struct ast_sip_subscription_handler *handler,
		const char *resource, struct sip_subscription_tree *tree)
{
	struct ast_sip_subscription *sub;
	pjsip_sip_uri *contact_uri;

	sub = ao2_alloc(sizeof(*sub) + strlen(resource) + 1, subscription_destructor);
	if (!sub) {
		return NULL;
	}
	strcpy(sub->resource, resource); /* Safe */

	sub->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
	if (!sub->datastores) {
		ao2_ref(sub, -1);
		return NULL;
	}

	sub->body_text = ast_str_create(128);
	if (!sub->body_text) {
		ao2_ref(sub, -1);
		return NULL;
	}

	sub->uri = pjsip_sip_uri_create(tree->dlg->pool, PJ_FALSE);
	contact_uri = pjsip_uri_get_uri(tree->dlg->local.contact->uri);
	pjsip_sip_uri_assign(tree->dlg->pool, sub->uri, contact_uri);
	pj_strdup2(tree->dlg->pool, &sub->uri->user, resource);

	sub->handler = handler;
	sub->subscription_state = PJSIP_EVSUB_STATE_ACTIVE;
	sub->tree = tree;

	return sub;
}

/*!
 * \brief Create a tree of virtual subscriptions based on a resource tree node.
 *
 * \param handler The handler to supply to leaf subscriptions.
 * \param resource The requested resource for this subscription.
 * \param generator Body generator to use for leaf subscriptions.
 * \param tree The root of the subscription tree.
 * \param current The tree node that corresponds to the subscription being created.
 */
static struct ast_sip_subscription *create_virtual_subscriptions(const struct ast_sip_subscription_handler *handler,
		const char *resource, struct ast_sip_pubsub_body_generator *generator,
		struct sip_subscription_tree *tree, struct tree_node *current)
{
	int i;
	struct ast_sip_subscription *sub;

	sub = allocate_subscription(handler, resource, tree);
	if (!sub) {
		return NULL;
	}

	sub->full_state = current->full_state;
	sub->body_generator = generator;

	for (i = 0; i < AST_VECTOR_SIZE(&current->children); ++i) {
		struct ast_sip_subscription *child;
		struct tree_node *child_node = AST_VECTOR_GET(&current->children, i);

		child = create_virtual_subscriptions(handler, child_node->resource, generator,
				tree, child_node);

		if (!child) {
			ast_debug(1, "Child subscription to resource %s could not be created\n",
					child_node->resource);
			continue;
		}

		if (AST_VECTOR_APPEND(&sub->children, child)) {
			ast_debug(1, "Child subscription to resource %s could not be appended\n",
					child_node->resource);
		}
	}

	return sub;
}

static void shutdown_subscriptions(struct ast_sip_subscription *sub)
{
	int i;

	if (!sub) {
		return;
	}

	if (AST_VECTOR_SIZE(&sub->children) > 0) {
		for (i = 0; i < AST_VECTOR_SIZE(&sub->children); ++i) {
			shutdown_subscriptions(AST_VECTOR_GET(&sub->children, i));
			ao2_cleanup(AST_VECTOR_GET(&sub->children, i));
		}
		return;
	}

	if (sub->handler->subscription_shutdown) {
		sub->handler->subscription_shutdown(sub);
	}
}

static void subscription_tree_destructor(void *obj)
{
	struct sip_subscription_tree *sub_tree = obj;

	remove_subscription(sub_tree);

	subscription_persistence_remove(sub_tree);
	ao2_cleanup(sub_tree->endpoint);

	if (sub_tree->dlg) {
		ast_sip_push_task_synchronous(NULL, subscription_remove_serializer, sub_tree);
	}

	shutdown_subscriptions(sub_tree->root);
	ao2_cleanup(sub_tree->root);

	ast_taskprocessor_unreference(sub_tree->serializer);
	ast_module_unref(ast_module_info->self);
}

static void subscription_setup_dialog(struct sip_subscription_tree *sub_tree, pjsip_dialog *dlg)
{
	/* We keep a reference to the dialog until our subscription is destroyed. See
	 * the subscription_destructor for more details
	 */
	pjsip_dlg_inc_session(dlg, &pubsub_module);
	sub_tree->dlg = dlg;
	ast_sip_dialog_set_serializer(dlg, sub_tree->serializer);
	pjsip_evsub_set_mod_data(sub_tree->evsub, pubsub_module.id, sub_tree);
}

static struct sip_subscription_tree *allocate_subscription_tree(struct ast_sip_endpoint *endpoint)
{
	struct sip_subscription_tree *sub_tree;

	sub_tree = ao2_alloc(sizeof *sub_tree, subscription_tree_destructor);
	if (!sub_tree) {
		return NULL;
	}

	ast_module_ref(ast_module_info->self);

	sub_tree->serializer = ast_sip_create_serializer();
	if (!sub_tree->serializer) {
		ao2_ref(sub_tree, -1);
		return NULL;
	}

	sub_tree->endpoint = ao2_bump(endpoint);
	sub_tree->notify_sched_id = -1;

	add_subscription(sub_tree);
	return sub_tree;
}

/*!
 * \brief Create a subscription tree based on a resource tree.
 *
 * Using the previously-determined valid resources in the provided resource tree,
 * a corresponding tree of ast_sip_subscriptions are created. The root of the
 * subscription tree is a real subscription, and the rest in the tree are
 * virtual subscriptions.
 *
 * \param handler The handler to use for leaf subscriptions
 * \param endpoint The endpoint that sent the SUBSCRIBE request
 * \param rdata The SUBSCRIBE content
 * \param resource The requested resource in the SUBSCRIBE request
 * \param generator The body generator to use in leaf subscriptions
 * \param tree The resource tree on which the subscription tree is based
 * \param dlg_status[out] The result of attempting to create a dialog.
 *
 * \retval NULL Could not create the subscription tree
 * \retval non-NULL The root of the created subscription tree
 */

static struct sip_subscription_tree *create_subscription_tree(const struct ast_sip_subscription_handler *handler,
		struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, const char *resource,
		struct ast_sip_pubsub_body_generator *generator, struct resource_tree *tree,
		pj_status_t *dlg_status)
{
	struct sip_subscription_tree *sub_tree;
	pjsip_dialog *dlg;
	struct subscription_persistence *persistence;

	sub_tree = allocate_subscription_tree(endpoint);
	if (!sub_tree) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		return NULL;
	}
	sub_tree->role = AST_SIP_NOTIFIER;

	dlg = ast_sip_create_dialog_uas(endpoint, rdata, dlg_status);
	if (!dlg) {
		if (*dlg_status != PJ_EEXISTS) {
			ast_log(LOG_WARNING, "Unable to create dialog for SIP subscription\n");
		}
		ao2_ref(sub_tree, -1);
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

	pjsip_evsub_create_uas(dlg, &pubsub_cb, rdata, 0, &sub_tree->evsub);
	subscription_setup_dialog(sub_tree, dlg);

	ast_sip_mod_data_set(dlg->pool, dlg->mod_data, pubsub_module.id, MOD_DATA_MSG,
			pjsip_msg_clone(dlg->pool, rdata->msg_info.msg));

	sub_tree->notification_batch_interval = tree->notification_batch_interval;

	sub_tree->root = create_virtual_subscriptions(handler, resource, generator, sub_tree, tree->root);
	if (AST_VECTOR_SIZE(&sub_tree->root->children) > 0) {
		sub_tree->is_list = 1;
	}

	return sub_tree;
}

static int generate_initial_notify(struct ast_sip_subscription *sub);
static int send_notify(struct sip_subscription_tree *sub_tree, unsigned int force_full_state);

/*! \brief Callback function to perform the actual recreation of a subscription */
static int subscription_persistence_recreate(void *obj, void *arg, int flags)
{
	struct subscription_persistence *persistence = obj;
	pj_pool_t *pool = arg;
	pjsip_rx_data rdata = { { 0, }, };
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct sip_subscription_tree *sub_tree;
	struct ast_sip_pubsub_body_generator *generator;
	int resp;
	char *resource;
	size_t resource_size;
	pjsip_sip_uri *request_uri;
	struct resource_tree tree;
	pjsip_expires_hdr *expires_header;
	struct ast_sip_subscription_handler *handler;

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

	request_uri = pjsip_uri_get_uri(rdata.msg_info.msg->line.req.uri);
	resource_size = pj_strlen(&request_uri->user) + 1;
	resource = alloca(resource_size);
	ast_copy_pj_str(resource, &request_uri->user, resource_size);

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
	if (!handler || !handler->notifier) {
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
		return 0;
	}

	generator = subscription_get_generator_from_rdata(&rdata, handler);
	if (!generator) {
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
		return 0;
	}

	ast_sip_mod_data_set(rdata.tp_info.pool, rdata.endpt_info.mod_data,
			pubsub_module.id, MOD_DATA_PERSISTENCE, persistence);

	memset(&tree, 0, sizeof(tree));
	resp = build_resource_tree(endpoint, handler, resource, &tree,
		ast_sip_pubsub_has_eventlist_support(&rdata));
	if (PJSIP_IS_STATUS_IN_CLASS(resp, 200)) {
		pj_status_t dlg_status;

		sub_tree = create_subscription_tree(handler, endpoint, &rdata, resource, generator, &tree, &dlg_status);
		if (!sub_tree) {
			ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
			ast_log(LOG_WARNING, "Failed to re-create subscription for %s\n", persistence->endpoint);
			return 0;
		}
		sub_tree->persistence = ao2_bump(persistence);
		subscription_persistence_update(sub_tree, &rdata);
		if (generate_initial_notify(sub_tree->root)) {
			pjsip_evsub_terminate(sub_tree->evsub, PJ_TRUE);
		}
		send_notify(sub_tree, 1);
	} else {
		ast_sorcery_delete(ast_sip_get_sorcery(), persistence);
	}
	resource_tree_destroy(&tree);

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

typedef int (*on_subscription_t)(struct sip_subscription_tree *sub, void *arg);

static int for_each_subscription(on_subscription_t on_subscription, void *arg)
{
	int num = 0;
	struct sip_subscription_tree *i;
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

static void sip_subscription_to_ami(struct sip_subscription_tree *sub_tree,
				    struct ast_str **buf)
{
	char str[256];
	struct ast_sip_endpoint_id_configuration *id = &sub_tree->endpoint->id;

	ast_str_append(buf, 0, "Role: %s\r\n",
		       sip_subscription_roles_map[sub_tree->role]);
	ast_str_append(buf, 0, "Endpoint: %s\r\n",
		       ast_sorcery_object_get_id(sub_tree->endpoint));

	ast_copy_pj_str(str, &sub_tree->dlg->call_id->id, sizeof(str));
	ast_str_append(buf, 0, "Callid: %s\r\n", str);

	ast_str_append(buf, 0, "State: %s\r\n", pjsip_evsub_get_state_name(sub_tree->evsub));

	ast_callerid_merge(str, sizeof(str),
			   S_COR(id->self.name.valid, id->self.name.str, NULL),
			   S_COR(id->self.number.valid, id->self.number.str, NULL),
			   "Unknown");

	ast_str_append(buf, 0, "Callerid: %s\r\n", str);

	/* XXX This needs to be done recursively for lists */
	if (sub_tree->root->handler->to_ami) {
		sub_tree->root->handler->to_ami(sub_tree->root, buf);
	}
}


void *ast_sip_subscription_get_header(const struct ast_sip_subscription *sub, const char *header)
{
	pjsip_dialog *dlg = sub->tree->dlg;
	pjsip_msg *msg = ast_sip_mod_data_get(dlg->mod_data, pubsub_module.id, MOD_DATA_MSG);
	pj_str_t name;

	pj_cstr(&name, header);

	return pjsip_msg_find_hdr_by_name(msg, &name, NULL);
}

struct ast_sip_subscription *ast_sip_create_subscription(const struct ast_sip_subscription_handler *handler,
		struct ast_sip_endpoint *endpoint, const char *resource)
{
	struct ast_sip_subscription *sub;
	pjsip_dialog *dlg;
	struct ast_sip_contact *contact;
	pj_str_t event;
	pjsip_tx_data *tdata;
	pjsip_evsub *evsub;
	struct sip_subscription_tree *sub_tree = NULL;

	sub_tree = allocate_subscription_tree(endpoint);
	if (!sub_tree) {
		return NULL;
	}

	sub = allocate_subscription(handler, resource, sub_tree);
	if (!sub) {
		ao2_cleanup(sub_tree);
		return NULL;
	}

	contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
	if (!contact || ast_strlen_zero(contact->uri)) {
		ast_log(LOG_WARNING, "No contacts configured for endpoint %s. Unable to create SIP subsription\n",
				ast_sorcery_object_get_id(endpoint));
		ao2_ref(sub_tree, -1);
		ao2_cleanup(contact);
		return NULL;
	}

	dlg = ast_sip_create_dialog_uac(endpoint, contact->uri, NULL);
	ao2_cleanup(contact);
	if (!dlg) {
		ast_log(LOG_WARNING, "Unable to create dialog for SIP subscription\n");
		ao2_ref(sub_tree, -1);
		return NULL;
	}

	pj_cstr(&event, handler->event_name);
	pjsip_evsub_create_uac(dlg, &pubsub_cb, &event, 0, &sub_tree->evsub);
	subscription_setup_dialog(sub_tree, dlg);

	evsub = sub_tree->evsub;

	if (pjsip_evsub_initiate(evsub, NULL, -1, &tdata) == PJ_SUCCESS) {
		pjsip_evsub_send_request(evsub, tdata);
	} else {
		/* pjsip_evsub_terminate will result in pubsub_on_evsub_state,
		 * being called and terminating the subscription. Therefore, we don't
		 * need to decrease the reference count of sub here.
		 */
		pjsip_evsub_terminate(evsub, PJ_TRUE);
		ao2_ref(sub_tree, -1);
		return NULL;
	}

	return sub;
}

struct ast_sip_endpoint *ast_sip_subscription_get_endpoint(struct ast_sip_subscription *sub)
{
	ast_assert(sub->tree->endpoint != NULL);
	return ao2_bump(sub->tree->endpoint);
}

struct ast_taskprocessor *ast_sip_subscription_get_serializer(struct ast_sip_subscription *sub)
{
	ast_assert(sub->tree->serializer != NULL);
	return sub->tree->serializer;
}

/*!
 * \brief Pre-allocate a buffer for the transmission
 *
 * Typically, we let PJSIP do this step for us when we send a request. PJSIP's buffer
 * allocation algorithm is to allocate a buffer of PJSIP_MAX_PKT_LEN bytes and attempt
 * to write the packet to the allocated buffer. If the buffer is too small to hold the
 * packet, then we get told the message is too long to be sent.
 *
 * When dealing with SIP NOTIFY, especially with RLS, it is possible to exceed
 * PJSIP_MAX_PKT_LEN. Rather than accepting the limitation imposed on us by default,
 * we instead take the strategy of pre-allocating the buffer, testing for ourselves
 * if the message will fit, and resizing the buffer as required.
 *
 * RFC 3261 says that a SIP UDP request can be up to 65535 bytes long. We're capping
 * it at 64000 for a couple of reasons:
 * 1) Allocating more than 64K at a time is hard to justify
 * 2) If the message goes through proxies, those proxies will want to add Via and
 *    Record-Route headers, making the message even larger. Giving some space for
 *    those headers is a nice thing to do.
 *
 * RFC 3261 does not place an upper limit on the size of TCP requests, but we are
 * going to impose the same 64K limit as a memory savings.
 *
 * \param tdata The tdata onto which to allocate a buffer
 * \retval 0 Success
 * \retval -1 The message is too large
 */
static int allocate_tdata_buffer(pjsip_tx_data *tdata)
{
	int buf_size;
	int size = -1;
	char *buf;

	for (buf_size = PJSIP_MAX_PKT_LEN; size == -1 && buf_size < 64000; buf_size *= 2) {
		buf = pj_pool_alloc(tdata->pool, buf_size);
		size = pjsip_msg_print(tdata->msg, buf, buf_size);
	}

	if (size == -1) {
		return -1;
	}

	tdata->buf.start = buf;
	tdata->buf.cur = tdata->buf.start;
	tdata->buf.end = tdata->buf.start + buf_size;

	return 0;
}

static int sip_subscription_send_request(struct sip_subscription_tree *sub_tree, pjsip_tx_data *tdata)
{
#ifdef TEST_FRAMEWORK
	struct ast_sip_endpoint *endpoint = sub_tree->endpoint;
#endif
	int res;

	if (allocate_tdata_buffer(tdata)) {
		ast_log(LOG_ERROR, "SIP request %s is too large to send.\n", tdata->info);
		return -1;
	}

	res = pjsip_evsub_send_request(sub_tree->evsub, tdata) == PJ_SUCCESS ? 0 : -1;
	subscription_persistence_update(sub_tree, NULL);

	ast_test_suite_event_notify("SUBSCRIPTION_STATE_SET",
		"StateText: %s\r\n"
		"Endpoint: %s\r\n",
		pjsip_evsub_get_state_name(sub_tree->evsub),
		ast_sorcery_object_get_id(endpoint));

	return res;
}

/*!
 * \brief Add a resource XML element to an RLMI body
 *
 * Each resource element represents a subscribed resource in the list. This function currently
 * will unconditionally add an instance element to each created resource element. Instance
 * elements refer to later parts in the multipart body.
 *
 * \param pool PJLIB allocation pool
 * \param cid Content-ID header of the resource
 * \param resource_name Name of the resource
 * \param resource_uri URI of the resource
 * \param state State of the subscribed resource
 */
static void add_rlmi_resource(pj_pool_t *pool, pj_xml_node *rlmi, const pjsip_generic_string_hdr *cid,
		const char *resource_name, const pjsip_sip_uri *resource_uri, pjsip_evsub_state state)
{
	static pj_str_t cid_name = { "cid", 3 };
	pj_xml_node *resource;
	pj_xml_node *name;
	pj_xml_node *instance;
	pj_xml_attr *cid_attr;
	char id[6];
	char uri[PJSIP_MAX_URL_SIZE];

	/* This creates a string representing the Content-ID without the enclosing < > */
	const pj_str_t cid_stripped = {
		.ptr = cid->hvalue.ptr + 1,
		.slen = cid->hvalue.slen - 2,
	};

	resource = ast_sip_presence_xml_create_node(pool, rlmi, "resource");
	name = ast_sip_presence_xml_create_node(pool, resource, "name");
	instance = ast_sip_presence_xml_create_node(pool, resource, "instance");

	pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, resource_uri, uri, sizeof(uri));
	ast_sip_presence_xml_create_attr(pool, resource, "uri", uri);

	pj_strdup2(pool, &name->content, resource_name);

	ast_generate_random_string(id, sizeof(id));

	ast_sip_presence_xml_create_attr(pool, instance, "id", id);
	ast_sip_presence_xml_create_attr(pool, instance, "state",
			state == PJSIP_EVSUB_STATE_TERMINATED ? "terminated" : "active");

	/* Use the PJLIB-util XML library directly here since we are using a
	 * pj_str_t
	 */

	cid_attr = pj_xml_attr_new(pool, &cid_name, &cid_stripped);
	pj_xml_add_attr(instance, cid_attr);
}

/*!
 * \brief A multipart body part and meta-information
 *
 * When creating a multipart body part, the end result (the
 * pjsip_multipart_part) is hard to inspect without undoing
 * a lot of what was done to create it. Therefore, we use this
 * structure to store meta-information about the body part.
 *
 * The main consumer of this is the creator of the RLMI body
 * part of a multipart resource list body.
 */
struct body_part {
	/*! Content-ID header for the body part */
	pjsip_generic_string_hdr *cid;
	/*! Subscribed resource represented in the body part */
	const char *resource;
	/*! URI for the subscribed body part */
	pjsip_sip_uri *uri;
	/*! Subscription state of the resource represented in the body part */
	pjsip_evsub_state state;
	/*! The actual body part that will be present in the multipart body */
	pjsip_multipart_part *part;
};

/*!
 * \brief Type declaration for container of body part structures
 */
AST_VECTOR(body_part_list, struct body_part *);

/*!
 * \brief Create a Content-ID header
 *
 * Content-ID headers are required by RFC2387 for multipart/related
 * bodies. They serve as identifiers for each part of the multipart body.
 *
 * \param pool PJLIB allocation pool
 * \param sub Subscription to a resource
 */
static pjsip_generic_string_hdr *generate_content_id_hdr(pj_pool_t *pool,
		const struct ast_sip_subscription *sub)
{
	static const pj_str_t cid_name = { "Content-ID", 10 };
	pjsip_generic_string_hdr *cid;
	char id[6];
	size_t alloc_size;
	pj_str_t cid_value;

	/* '<' + '@' + '>' = 3. pj_str_t does not require a null-terminator */
	alloc_size = sizeof(id) + pj_strlen(&sub->uri->host) + 3;
	cid_value.ptr = pj_pool_alloc(pool, alloc_size);
	cid_value.slen = sprintf(cid_value.ptr, "<%s@%.*s>",
			ast_generate_random_string(id, sizeof(id)),
			(int) pj_strlen(&sub->uri->host), pj_strbuf(&sub->uri->host));
	cid = pjsip_generic_string_hdr_create(pool, &cid_name, &cid_value);

	return cid;
}

static int rlmi_print_body(struct pjsip_msg_body *msg_body, char *buf, pj_size_t size)
{
	int num_printed;
	pj_xml_node *rlmi = msg_body->data;

	num_printed = pj_xml_print(rlmi, buf, size, PJ_TRUE);
	if (num_printed == AST_PJSIP_XML_PROLOG_LEN) {
		return -1;
	}

	return num_printed;
}

static void *rlmi_clone_data(pj_pool_t *pool, const void *data, unsigned len)
{
	const pj_xml_node *rlmi = data;

	return pj_xml_clone(pool, rlmi);
}

/*!
 * \brief Create an RLMI body part for a multipart resource list body
 *
 * RLMI (Resource list meta information) is a special body type that lists
 * the subscribed resources and tells subscribers the number of subscribed
 * resources and what other body parts are in the multipart body. The
 * RLMI body also has a version number that a subscriber can use to ensure
 * that the locally-stored state corresponds to server state.
 *
 * \param pool The allocation pool
 * \param sub The subscription representing the subscribed resource list
 * \param body_parts A container of body parts that RLMI will refer to
 * \param full_state Indicates whether this is a full or partial state notification
 * \return The multipart part representing the RLMI body
 */
static pjsip_multipart_part *build_rlmi_body(pj_pool_t *pool, struct ast_sip_subscription *sub,
		struct body_part_list *body_parts, unsigned int full_state)
{
	static const pj_str_t rlmi_type = { "application", 11 };
	static const pj_str_t rlmi_subtype = { "rlmi+xml", 8 };
	pj_xml_node *rlmi;
	pj_xml_node *name;
	pjsip_multipart_part *rlmi_part;
	char version_str[32];
	char uri[PJSIP_MAX_URL_SIZE];
	pjsip_generic_string_hdr *cid;
	int i;

	rlmi = ast_sip_presence_xml_create_node(pool, NULL, "list");
	ast_sip_presence_xml_create_attr(pool, rlmi, "xmlns", "urn:ietf:params:xml:ns:rlmi");

	ast_sip_subscription_get_local_uri(sub, uri, sizeof(uri));
	ast_sip_presence_xml_create_attr(pool, rlmi, "uri", uri);

	snprintf(version_str, sizeof(version_str), "%u", sub->version++);
	ast_sip_presence_xml_create_attr(pool, rlmi, "version", version_str);
	ast_sip_presence_xml_create_attr(pool, rlmi, "fullState", full_state ? "true" : "false");

	name = ast_sip_presence_xml_create_node(pool, rlmi, "name");
	pj_strdup2(pool, &name->content, ast_sip_subscription_get_resource_name(sub));

	for (i = 0; i < AST_VECTOR_SIZE(body_parts); ++i) {
		const struct body_part *part = AST_VECTOR_GET(body_parts, i);

		add_rlmi_resource(pool, rlmi, part->cid, part->resource, part->uri, part->state);
	}

	rlmi_part = pjsip_multipart_create_part(pool);

	rlmi_part->body = PJ_POOL_ZALLOC_T(pool, pjsip_msg_body);
	pj_strdup(pool, &rlmi_part->body->content_type.type, &rlmi_type);
	pj_strdup(pool, &rlmi_part->body->content_type.subtype, &rlmi_subtype);
	pj_list_init(&rlmi_part->body->content_type.param);

	rlmi_part->body->data = pj_xml_clone(pool, rlmi);
	rlmi_part->body->clone_data = rlmi_clone_data;
	rlmi_part->body->print_body = rlmi_print_body;

	cid = generate_content_id_hdr(pool, sub);
	pj_list_insert_before(&rlmi_part->hdr, cid);

	return rlmi_part;
}

static pjsip_msg_body *generate_notify_body(pj_pool_t *pool, struct ast_sip_subscription *root,
		unsigned int force_full_state);

/*!
 * \brief Destroy a list of body parts
 *
 * \param parts The container of parts to destroy
 */
static void free_body_parts(struct body_part_list *parts)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(parts); ++i) {
		struct body_part *part = AST_VECTOR_GET(parts, i);
		ast_free(part);
	}

	AST_VECTOR_FREE(parts);
}

/*!
 * \brief Allocate and initialize a body part structure
 *
 * \param pool PJLIB allocation pool
 * \param sub Subscription representing a subscribed resource
 */
static struct body_part *allocate_body_part(pj_pool_t *pool, const struct ast_sip_subscription *sub)
{
	struct body_part *bp;

	bp = ast_calloc(1, sizeof(*bp));
	if (!bp) {
		return NULL;
	}

	bp->cid = generate_content_id_hdr(pool, sub);
	bp->resource = sub->resource;
	bp->state = sub->subscription_state;
	bp->uri = sub->uri;

	return bp;
}

/*!
 * \brief Create a multipart body part for a subscribed resource
 *
 * \param pool PJLIB allocation pool
 * \param sub The subscription representing a subscribed resource
 * \param parts A vector of parts to append the created part to.
 * \param use_full_state Unused locally, but may be passed to other functions
 */
static void build_body_part(pj_pool_t *pool, struct ast_sip_subscription *sub,
		struct body_part_list *parts, unsigned int use_full_state)
{
	struct body_part *bp;
	pjsip_msg_body *body;

	bp = allocate_body_part(pool, sub);
	if (!bp) {
		return;
	}

	body = generate_notify_body(pool, sub, use_full_state);
	if (!body) {
		/* Partial state was requested and the resource has not changed state */
		ast_free(bp);
		return;
	}

	bp->part = pjsip_multipart_create_part(pool);
	bp->part->body = body;
	pj_list_insert_before(&bp->part->hdr, bp->cid);

	AST_VECTOR_APPEND(parts, bp);
}

/*!
 * \brief Create and initialize the PJSIP multipart body structure for a resource list subscription
 *
 * \param pool
 * \return The multipart message body
 */
static pjsip_msg_body *create_multipart_body(pj_pool_t *pool)
{
	pjsip_media_type media_type;
	pjsip_param *media_type_param;
	char boundary[6];
	pj_str_t pj_boundary;

	pjsip_media_type_init2(&media_type, "multipart", "related");

	media_type_param = pj_pool_alloc(pool, sizeof(*media_type_param));
	pj_list_init(media_type_param);

	pj_strdup2(pool, &media_type_param->name, "type");
	pj_strdup2(pool, &media_type_param->value, "\"application/rlmi+xml\"");

	pj_list_insert_before(&media_type.param, media_type_param);

	pj_cstr(&pj_boundary, ast_generate_random_string(boundary, sizeof(boundary)));
	return pjsip_multipart_create(pool, &media_type, &pj_boundary);
}

/*!
 * \brief Create a resource list body for NOTIFY requests
 *
 * Resource list bodies are multipart/related bodies. The first part of the multipart body
 * is an RLMI body that describes the rest of the parts to come. The other parts of the body
 * convey state of individual subscribed resources.
 *
 * \param pool PJLIB allocation pool
 * \param sub Subscription details from which to generate body
 * \param force_full_state If true, ignore resource list settings and send a full state notification
 * \return The generated multipart/related body
 */
static pjsip_msg_body *generate_list_body(pj_pool_t *pool, struct ast_sip_subscription *sub,
		unsigned int force_full_state)
{
	int i;
	pjsip_multipart_part *rlmi_part;
	pjsip_msg_body *multipart;
	struct body_part_list body_parts;
	unsigned int use_full_state = force_full_state ? 1 : sub->full_state;

	if (AST_VECTOR_INIT(&body_parts, AST_VECTOR_SIZE(&sub->children))) {
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&sub->children); ++i) {
		build_body_part(pool, AST_VECTOR_GET(&sub->children, i), &body_parts, use_full_state);
	}

	/* This can happen if issuing partial state and no children of the list have changed state */
	if (AST_VECTOR_SIZE(&body_parts) == 0) {
		return NULL;
	}

	multipart = create_multipart_body(pool);

	rlmi_part = build_rlmi_body(pool, sub, &body_parts, use_full_state);
	if (!rlmi_part) {
		return NULL;
	}
	pjsip_multipart_add_part(pool, multipart, rlmi_part);

	for (i = 0; i < AST_VECTOR_SIZE(&body_parts); ++i) {
		pjsip_multipart_add_part(pool, multipart, AST_VECTOR_GET(&body_parts, i)->part);
	}

	free_body_parts(&body_parts);
	return multipart;
}

/*!
 * \brief Create the body for a NOTIFY request.
 *
 * \param pool The pool used for allocations
 * \param root The root of the subscription tree
 * \param force_full_state If true, ignore resource list settings and send a full state notification
 */
static pjsip_msg_body *generate_notify_body(pj_pool_t *pool, struct ast_sip_subscription *root,
		unsigned int force_full_state)
{
	pjsip_msg_body *body;

	if (AST_VECTOR_SIZE(&root->children) == 0) {
		if (force_full_state || root->body_changed) {
			/* Not a list. We've already generated the body and saved it on the subscription.
			 * Use that directly.
			 */
			pj_str_t type;
			pj_str_t subtype;
			pj_str_t text;

			pj_cstr(&type, ast_sip_subscription_get_body_type(root));
			pj_cstr(&subtype, ast_sip_subscription_get_body_subtype(root));
			pj_cstr(&text, ast_str_buffer(root->body_text));

			body = pjsip_msg_body_create(pool, &type, &subtype, &text);
			root->body_changed = 0;
		} else {
			body = NULL;
		}
	} else {
		body = generate_list_body(pool, root, force_full_state);
	}

	return body;
}

/*!
 * \brief Shortcut method to create a Require: eventlist header
 */
static pjsip_require_hdr *create_require_eventlist(pj_pool_t *pool)
{
	pjsip_require_hdr *require;

	require = pjsip_require_hdr_create(pool);
	pj_strdup2(pool, &require->values[0], "eventlist");
	require->count = 1;

	return require;
}

/*!
 * \brief Send a NOTIFY request to a subscriber
 *
 * \param sub_tree The subscription tree representing the subscription
 * \param force_full_state If true, ignore resource list settings and send full resource list state.
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int send_notify(struct sip_subscription_tree *sub_tree, unsigned int force_full_state)
{
	pjsip_evsub *evsub = sub_tree->evsub;
	pjsip_tx_data *tdata;

	if (ast_shutting_down()
		&& sub_tree->root->subscription_state == PJSIP_EVSUB_STATE_TERMINATED
		&& sub_tree->persistence) {
		return 0;
	}

	if (pjsip_evsub_notify(evsub, sub_tree->root->subscription_state,
				NULL, NULL, &tdata) != PJ_SUCCESS) {
		return -1;
	}

	tdata->msg->body = generate_notify_body(tdata->pool, sub_tree->root, force_full_state);
	if (!tdata->msg->body) {
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}

	if (sub_tree->is_list) {
		pjsip_require_hdr *require = create_require_eventlist(tdata->pool);
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) require);
	}

	if (sip_subscription_send_request(sub_tree, tdata)) {
		return -1;
	}

	sub_tree->send_scheduled_notify = 0;

	return 0;
}

static int serialized_send_notify(void *userdata)
{
	struct sip_subscription_tree *sub_tree = userdata;

	/* It's possible that between when the notification was scheduled
	 * and now, that a new SUBSCRIBE arrived, requiring full state to be
	 * sent out in an immediate NOTIFY. If that has happened, we need to
	 * bail out here instead of sending the batched NOTIFY.
	 */
	if (!sub_tree->send_scheduled_notify) {
		ao2_cleanup(sub_tree);
		return 0;
	}

	send_notify(sub_tree, 0);
	ast_test_suite_event_notify("SUBSCRIPTION_STATE_CHANGED",
			"Resource: %s",
			sub_tree->root->resource);
	sub_tree->notify_sched_id = -1;
	ao2_cleanup(sub_tree);
	return 0;
}

static int sched_cb(const void *data)
{
	struct sip_subscription_tree *sub_tree = (struct sip_subscription_tree *) data;

	/* We don't need to bump the refcount of sub_tree since we bumped it when scheduling this task */
	ast_sip_push_task(sub_tree->serializer, serialized_send_notify, sub_tree);
	return 0;
}

static int schedule_notification(struct sip_subscription_tree *sub_tree)
{
	/* There's already a notification scheduled */
	if (sub_tree->notify_sched_id > -1) {
		return 0;
	}

	sub_tree->notify_sched_id = ast_sched_add(sched, sub_tree->notification_batch_interval, sched_cb, ao2_bump(sub_tree));
	if (sub_tree->notify_sched_id < 0) {
		return -1;
	}

	sub_tree->send_scheduled_notify = 1;
	return 0;
}

int ast_sip_subscription_notify(struct ast_sip_subscription *sub, struct ast_sip_body_data *notify_data,
		int terminate)
{
	if (ast_sip_pubsub_generate_body_content(ast_sip_subscription_get_body_type(sub),
				ast_sip_subscription_get_body_subtype(sub), notify_data, &sub->body_text)) {
		return -1;
	}

	sub->body_changed = 1;
	if (terminate) {
		sub->subscription_state = PJSIP_EVSUB_STATE_TERMINATED;
	}

	if (sub->tree->notification_batch_interval) {
		return schedule_notification(sub->tree);
	} else {
		int res;
		/* See the note in pubsub_on_rx_refresh() for why sub->tree is refbumped here */
		ao2_ref(sub->tree, +1);
		res = send_notify(sub->tree, 0);
		ast_test_suite_event_notify(terminate ? "SUBSCRIPTION_TERMINATED" : "SUBSCRIPTION_STATE_CHANGED",
				"Resource: %s",
				sub->tree->root->resource);
		ao2_ref(sub->tree, -1);

		return res;
	}
}

void ast_sip_subscription_get_local_uri(struct ast_sip_subscription *sub, char *buf, size_t size)
{
	pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, sub->uri, buf, size);
}

void ast_sip_subscription_get_remote_uri(struct ast_sip_subscription *sub, char *buf, size_t size)
{
	pjsip_dialog *dlg = sub->tree->dlg;
	ast_copy_pj_str(buf, &dlg->remote.info_str, size);
}

const char *ast_sip_subscription_get_resource_name(struct ast_sip_subscription *sub)
{
	return sub->resource;
}

static int sip_subscription_accept(struct sip_subscription_tree *sub_tree, pjsip_rx_data *rdata, int response)
{
	pjsip_hdr res_hdr;

	/* If this is a persistence recreation the subscription has already been accepted */
	if (ast_sip_mod_data_get(rdata->endpt_info.mod_data, pubsub_module.id, MOD_DATA_PERSISTENCE)) {
		return 0;
	}

	pj_list_init(&res_hdr);
	if (sub_tree->is_list) {
		/* If subscribing to a list, our response has to have a Require: eventlist header in it */
		pj_list_insert_before(&res_hdr, create_require_eventlist(rdata->tp_info.pool));
	}

	return pjsip_evsub_accept(sub_tree->evsub, rdata, response, &res_hdr) == PJ_SUCCESS ? 0 : -1;
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
	ao2_find(subscription->datastores, name, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
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
	pj_str_t accept[AST_SIP_MAX_ACCEPT] = { {0, }, };
	struct ast_sip_subscription_handler *existing;
	int i = 0;

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

static int generate_initial_notify(struct ast_sip_subscription *sub)
{
	void *notify_data;
	int res;
	struct ast_sip_body_data data = {
		.body_type = sub->handler->body_type,
	};

	if (AST_VECTOR_SIZE(&sub->children) > 0) {
		int i;

		for (i = 0; i < AST_VECTOR_SIZE(&sub->children); ++i) {
			if (generate_initial_notify(AST_VECTOR_GET(&sub->children, i))) {
				return -1;
			}
		}

		return 0;
	}

	if (sub->handler->notifier->subscription_established(sub)) {
		return -1;
	}

	notify_data = sub->handler->notifier->get_notify_data(sub);
	if (!notify_data) {
		return -1;
	}

	data.body_data = notify_data;

	res = ast_sip_pubsub_generate_body_content(ast_sip_subscription_get_body_type(sub),
			ast_sip_subscription_get_body_subtype(sub), &data, &sub->body_text);

	ao2_cleanup(notify_data);

	return res;
}

static pj_bool_t pubsub_on_rx_subscribe_request(pjsip_rx_data *rdata)
{
	pjsip_expires_hdr *expires_header;
	struct ast_sip_subscription_handler *handler;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct sip_subscription_tree *sub_tree;
	struct ast_sip_pubsub_body_generator *generator;
	char *resource;
	pjsip_uri *request_uri;
	pjsip_sip_uri *request_uri_sip;
	size_t resource_size;
	int resp;
	struct resource_tree tree;
	pj_status_t dlg_status;

	endpoint = ast_pjsip_rdata_get_endpoint(rdata);
	ast_assert(endpoint != NULL);

	if (!endpoint->subscription.allow) {
		ast_log(LOG_WARNING, "Subscriptions not permitted for endpoint %s.\n", ast_sorcery_object_get_id(endpoint));
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 603, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	request_uri = rdata->msg_info.msg->line.req.uri;

	if (!PJSIP_URI_SCHEME_IS_SIP(request_uri) && !PJSIP_URI_SCHEME_IS_SIPS(request_uri)) {
		char uri_str[PJSIP_MAX_URL_SIZE];

		pjsip_uri_print(PJSIP_URI_IN_REQ_URI, request_uri, uri_str, sizeof(uri_str));
		ast_log(LOG_WARNING, "Request URI '%s' is not a sip: or sips: URI.\n", uri_str);
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 416, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	request_uri_sip = pjsip_uri_get_uri(request_uri);
	resource_size = pj_strlen(&request_uri_sip->user) + 1;
	resource = alloca(resource_size);
	ast_copy_pj_str(resource, &request_uri_sip->user, resource_size);

	expires_header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, rdata->msg_info.msg->hdr.next);

	if (expires_header) {
		if (expires_header->ivalue == 0) {
			ast_log(LOG_WARNING, "Subscription request from endpoint %s rejected. Expiration of 0 is invalid\n",
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

	memset(&tree, 0, sizeof(tree));
	resp = build_resource_tree(endpoint, handler, resource, &tree,
		ast_sip_pubsub_has_eventlist_support(rdata));
	if (!PJSIP_IS_STATUS_IN_CLASS(resp, 200)) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, resp, NULL, NULL, NULL);
		resource_tree_destroy(&tree);
		return PJ_TRUE;
	}

	sub_tree = create_subscription_tree(handler, endpoint, rdata, resource, generator, &tree, &dlg_status);
	if (!sub_tree) {
		if (dlg_status != PJ_EEXISTS) {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		}
	} else {
		sub_tree->persistence = subscription_persistence_create(sub_tree);
		subscription_persistence_update(sub_tree, rdata);
		sip_subscription_accept(sub_tree, rdata, resp);
		if (generate_initial_notify(sub_tree->root)) {
			pjsip_evsub_terminate(sub_tree->evsub, PJ_TRUE);
		}
		send_notify(sub_tree, 1);
		ast_test_suite_event_notify("SUBSCRIPTION_ESTABLISHED",
				"Resource: %s",
				sub_tree->root->resource);
	}

	resource_tree_destroy(&tree);
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

/*! \brief Internal destructor for publications */
static void publication_destroy_fn(void *obj)
{
	struct ast_sip_publication *publication = obj;

	ast_debug(3, "Destroying SIP publication\n");

	ao2_cleanup(publication->datastores);
	ao2_cleanup(publication->endpoint);
}

static struct ast_sip_publication *sip_create_publication(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata,
	const char *resource, const char *event_configuration_name)
{
	struct ast_sip_publication *publication;
	pjsip_expires_hdr *expires_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL);
	size_t resource_len = strlen(resource) + 1, event_configuration_name_len = strlen(event_configuration_name) + 1;
	char *dst;

	ast_assert(endpoint != NULL);

	if (!(publication = ao2_alloc(sizeof(*publication) + resource_len + event_configuration_name_len, publication_destroy_fn))) {
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
	dst = publication->data;
	publication->resource = strcpy(dst, resource);
	dst += resource_len;
	publication->event_configuration_name = strcpy(dst, event_configuration_name);

	return publication;
}

static int sip_publication_respond(struct ast_sip_publication *pub, int status_code,
		pjsip_rx_data *rdata)
{
	pj_status_t status;
	pjsip_tx_data *tdata;
	pjsip_transaction *tsx;

	if (pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, status_code, NULL, &tdata) != PJ_SUCCESS) {
		return -1;
	}

	if (PJSIP_IS_STATUS_IN_CLASS(status_code, 200)) {
		RAII_VAR(char *, entity_tag, NULL, ast_free_ptr);
		RAII_VAR(char *, expires, NULL, ast_free_ptr);

		if ((ast_asprintf(&entity_tag, "%d", pub->entity_tag) < 0) ||
			(ast_asprintf(&expires, "%d", pub->expires) < 0)) {
			pjsip_tx_data_dec_ref(tdata);
			return -1;
		}

		ast_sip_add_header(tdata, "SIP-ETag", entity_tag);
		ast_sip_add_header(tdata, "Expires", expires);
	}

	if ((status = pjsip_tsx_create_uas(&pubsub_module, rdata, &tsx)) != PJ_SUCCESS) {
		return -1;
	}

	pjsip_tsx_recv_msg(tsx, rdata);

	if (pjsip_tsx_send_msg(tsx, tdata) != PJ_SUCCESS) {
		return -1;
	}

	return 0;
}

static struct ast_sip_publication *publish_request_initial(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata,
	struct ast_sip_publish_handler *handler)
{
	struct ast_sip_publication *publication;
	char *resource_name;
	size_t resource_size;
	RAII_VAR(struct ast_sip_publication_resource *, resource, NULL, ao2_cleanup);
	struct ast_variable *event_configuration_name = NULL;
	pjsip_uri *request_uri;
	pjsip_sip_uri *request_uri_sip;
	int resp;

	request_uri = rdata->msg_info.msg->line.req.uri;

	if (!PJSIP_URI_SCHEME_IS_SIP(request_uri) && !PJSIP_URI_SCHEME_IS_SIPS(request_uri)) {
		char uri_str[PJSIP_MAX_URL_SIZE];

		pjsip_uri_print(PJSIP_URI_IN_REQ_URI, request_uri, uri_str, sizeof(uri_str));
		ast_log(LOG_WARNING, "Request URI '%s' is not a sip: or sips: URI.\n", uri_str);
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 416, NULL, NULL, NULL);
		return NULL;
	}

	request_uri_sip = pjsip_uri_get_uri(request_uri);
	resource_size = pj_strlen(&request_uri_sip->user) + 1;
	resource_name = alloca(resource_size);
	ast_copy_pj_str(resource_name, &request_uri_sip->user, resource_size);

	resource = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "inbound-publication", resource_name);
	if (!resource) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 404, NULL, NULL, NULL);
		return NULL;
	}

	if (!ast_strlen_zero(resource->endpoint) && strcmp(resource->endpoint, ast_sorcery_object_get_id(endpoint))) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		return NULL;
	}

	for (event_configuration_name = resource->events; event_configuration_name; event_configuration_name = event_configuration_name->next) {
		if (!strcmp(event_configuration_name->name, handler->event_name)) {
			break;
		}
	}

	if (!event_configuration_name) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 404, NULL, NULL, NULL);
		return NULL;
	}

	resp = handler->new_publication(endpoint, resource_name, event_configuration_name->value);

	if (!PJSIP_IS_STATUS_IN_CLASS(resp, 200)) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, resp, NULL, NULL, NULL);
		return NULL;
	}

	publication = sip_create_publication(endpoint, rdata, S_OR(resource_name, ""), event_configuration_name->value);

	if (!publication) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 503, NULL, NULL, NULL);
		return NULL;
	}

	publication->handler = handler;
	if (publication->handler->publication_state_change(publication, rdata->msg_info.msg->body,
			AST_SIP_PUBLISH_STATE_INITIALIZED)) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		ao2_cleanup(publication);
		return NULL;
	}

	sip_publication_respond(publication, resp, rdata);

	return publication;
}

static int publish_expire_callback(void *data)
{
	RAII_VAR(struct ast_sip_publication *, publication, data, ao2_cleanup);

	if (publication->handler->publish_expire) {
		publication->handler->publish_expire(publication);
	}

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
	int expires = 0, entity_id, response = 0;

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
			if (handler->publication_state_change(publication, rdata->msg_info.msg->body,
						AST_SIP_PUBLISH_STATE_ACTIVE)) {
				/* If an error occurs we want to terminate the publication */
				expires = 0;
			}
			response = 200;
			break;
		case SIP_PUBLISH_REMOVE:
			handler->publication_state_change(publication, rdata->msg_info.msg->body,
					AST_SIP_PUBLISH_STATE_TERMINATED);
			response = 200;
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

	if (response) {
		sip_publication_respond(publication, response, rdata);
	}

	return PJ_TRUE;
}

struct ast_sip_endpoint *ast_sip_publication_get_endpoint(struct ast_sip_publication *pub)
{
	return pub->endpoint;
}

const char *ast_sip_publication_get_resource(const struct ast_sip_publication *pub)
{
	return pub->resource;
}

const char *ast_sip_publication_get_event_configuration(const struct ast_sip_publication *pub)
{
	return pub->event_configuration_name;
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
	struct sip_subscription_tree *sub_tree;

	if (pjsip_evsub_get_state(evsub) != PJSIP_EVSUB_STATE_TERMINATED) {
		return;
	}

	sub_tree = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);
	if (!sub_tree) {
		return;
	}

	ao2_cleanup(sub_tree);

	pjsip_evsub_set_mod_data(evsub, pubsub_module.id, NULL);
}

static void set_state_terminated(struct ast_sip_subscription *sub)
{
	int i;

	sub->subscription_state = PJSIP_EVSUB_STATE_TERMINATED;
	for (i = 0; i < AST_VECTOR_SIZE(&sub->children); ++i) {
		set_state_terminated(AST_VECTOR_GET(&sub->children, i));
	}
}

static void pubsub_on_rx_refresh(pjsip_evsub *evsub, pjsip_rx_data *rdata,
		int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body)
{
	struct sip_subscription_tree *sub_tree;

	sub_tree = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);
	if (!sub_tree) {
		return;
	}

	/* If sending a NOTIFY to terminate a subscription, then pubsub_on_evsub_state()
	 * will be called when we send the NOTIFY, and that will result in dropping the
	 * refcount of sub_tree by one, and possibly destroying the sub_tree. We need to
	 * hold a reference to the sub_tree until this function returns so that we don't
	 * try to read from or write to freed memory by accident
	 */
	ao2_ref(sub_tree, +1);

	if (pjsip_evsub_get_state(evsub) == PJSIP_EVSUB_STATE_TERMINATED) {
		set_state_terminated(sub_tree->root);
	}

	if (send_notify(sub_tree, 1)) {
		*p_st_code = 500;
	}

	ast_test_suite_event_notify(sub_tree->root->subscription_state == PJSIP_EVSUB_STATE_TERMINATED ?
			"SUBSCRIPTION_TERMINATED" : "SUBSCRIPTION_REFRESHED",
			"Resource: %s", sub_tree->root->resource);

	if (sub_tree->is_list) {
		pj_list_insert_before(res_hdr, create_require_eventlist(rdata->tp_info.pool));
	}

	ao2_ref(sub_tree, -1);
}

static void pubsub_on_rx_notify(pjsip_evsub *evsub, pjsip_rx_data *rdata, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);

	if (!sub) {
		return;
	}

	sub->handler->subscriber->state_change(sub, rdata->msg_info.msg->body,
			pjsip_evsub_get_state(evsub));
}

static int serialized_pubsub_on_client_refresh(void *userdata)
{
	struct sip_subscription_tree *sub_tree = userdata;
	pjsip_tx_data *tdata;

	if (pjsip_evsub_initiate(sub_tree->evsub, NULL, -1, &tdata) == PJ_SUCCESS) {
		pjsip_evsub_send_request(sub_tree->evsub, tdata);
	} else {
		pjsip_evsub_terminate(sub_tree->evsub, PJ_TRUE);
	}
	ao2_cleanup(sub_tree);
	return 0;
}

static void pubsub_on_client_refresh(pjsip_evsub *evsub)
{
	struct sip_subscription_tree *sub_tree = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);

	ao2_ref(sub_tree, +1);
	ast_sip_push_task(sub_tree->serializer, serialized_pubsub_on_client_refresh, sub_tree);
}

static int serialized_pubsub_on_server_timeout(void *userdata)
{
	struct sip_subscription_tree *sub_tree = userdata;

	set_state_terminated(sub_tree->root);
	send_notify(sub_tree, 1);
	ast_test_suite_event_notify("SUBSCRIPTION_TERMINATED",
			"Resource: %s",
			sub_tree->root->resource);

	ao2_cleanup(sub_tree);
	return 0;
}

static void pubsub_on_server_timeout(pjsip_evsub *evsub)
{
	struct sip_subscription_tree *sub_tree = pjsip_evsub_get_mod_data(evsub, pubsub_module.id);

	if (!sub_tree) {
		/* if a subscription has been terminated and the subscription
		   timeout/expires is less than the time it takes for all pending
		   transactions to end then the subscription timer will not have
		   been canceled yet and sub will be null, so do nothing since
		   the subscription has already been terminated. */
		return;
	}

	ao2_ref(sub_tree, +1);
	ast_sip_push_task(sub_tree->serializer, serialized_pubsub_on_server_timeout, sub_tree);
}

static int ami_subscription_detail(struct sip_subscription_tree *sub_tree,
				   struct ast_sip_ami *ami,
				   const char *event)
{
	struct ast_str *buf;

	buf = ast_sip_create_ami_event(event, ami);
	if (!buf) {
		return -1;
	}

	sip_subscription_to_ami(sub_tree, &buf);
	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ast_free(buf);
	return 0;
}

static int ami_subscription_detail_inbound(struct sip_subscription_tree *sub_tree, void *arg)
{
	return sub_tree->role == AST_SIP_NOTIFIER ? ami_subscription_detail(
		sub_tree, arg, "InboundSubscriptionDetail") : 0;
}

static int ami_subscription_detail_outbound(struct sip_subscription_tree *sub_tree, void *arg)
{
	return sub_tree->role == AST_SIP_SUBSCRIBER ? ami_subscription_detail(
		sub_tree, arg, "OutboundSubscriptionDetail") : 0;
}

static int ami_show_subscriptions_inbound(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	int num;

	astman_send_listack(s, m, "Following are Events for "
			    "each inbound Subscription", "start");

	num = for_each_subscription(ami_subscription_detail_inbound, &ami);

	astman_send_list_complete_start(s, m, "InboundSubscriptionDetailComplete", num);
	astman_send_list_complete_end(s);
	return 0;
}

static int ami_show_subscriptions_outbound(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	int num;

	astman_send_listack(s, m, "Following are Events for "
			    "each outbound Subscription", "start");

	num = for_each_subscription(ami_subscription_detail_outbound, &ami);

	astman_send_list_complete_start(s, m, "OutboundSubscriptionDetailComplete", num);
	astman_send_list_complete_end(s);
	return 0;
}

static int format_ami_resource_lists(void *obj, void *arg, int flags)
{
	struct resource_list *list = obj;
	struct ast_sip_ami *ami = arg;
	struct ast_str *buf;

	buf = ast_sip_create_ami_event("ResourceListDetail", ami);
	if (!buf) {
		return CMP_STOP;
	}

	if (ast_sip_sorcery_object_to_ami(list, &buf)) {
		ast_free(buf);
		return CMP_STOP;
	}
	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));

	ast_free(buf);
	return 0;
}

static int ami_show_resource_lists(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	int num;
	struct ao2_container *lists;

	lists = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "resource_list",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	if (!lists || !(num = ao2_container_count(lists))) {
		astman_send_error(s, m, "No resource lists found\n");
		return 0;
	}

	astman_send_listack(s, m, "A listing of resource lists follows, "
			    "presented as ResourceListDetail events", "start");

	ao2_callback(lists, OBJ_NODATA, format_ami_resource_lists, &ami);

	astman_send_list_complete_start(s, m, "ResourceListDetailComplete", num);
	astman_send_list_complete_end(s);
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

#define RESOURCE_LIST_INIT_SIZE 4

static void resource_list_destructor(void *obj)
{
	struct resource_list *list = obj;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&list->items); ++i) {
		ast_free((char *) AST_VECTOR_GET(&list->items, i));
	}

	AST_VECTOR_FREE(&list->items);
}

static void *resource_list_alloc(const char *name)
{
	struct resource_list *list;

	list = ast_sorcery_generic_alloc(sizeof(*list), resource_list_destructor);
	if (!list) {
		return NULL;
	}

	if (AST_VECTOR_INIT(&list->items, RESOURCE_LIST_INIT_SIZE)) {
		ao2_cleanup(list);
		return NULL;
	}

	return list;
}

static int item_in_vector(const struct resource_list *list, const char *item)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&list->items); ++i) {
		if (!strcmp(item, AST_VECTOR_GET(&list->items, i))) {
			return 1;
		}
	}

	return 0;
}

static int list_item_handler(const struct aco_option *opt,
		struct ast_variable *var, void *obj)
{
	struct resource_list *list = obj;
	char *items = ast_strdupa(var->value);
	char *item;

	while ((item = strsep(&items, ","))) {
		if (item_in_vector(list, item)) {
			ast_log(LOG_WARNING, "Ignoring duplicated list item '%s'\n", item);
			continue;
		}
		if (AST_VECTOR_APPEND(&list->items, ast_strdup(item))) {
			return -1;
		}
	}

	return 0;
}

static int list_item_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct resource_list *list = obj;
	int i;
	struct ast_str *str = ast_str_create(32);

	for (i = 0; i < AST_VECTOR_SIZE(&list->items); ++i) {
		ast_str_append(&str, 0, "%s,", AST_VECTOR_GET(&list->items, i));
	}

	/* Chop off trailing comma */
	ast_str_truncate(str, -1);
	*buf = ast_strdup(ast_str_buffer(str));
	ast_free(str);
	return 0;
}

static int resource_list_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	struct resource_list *list = obj;

	if (ast_strlen_zero(list->event)) {
		ast_log(LOG_WARNING, "Resource list '%s' has no event set\n",
				ast_sorcery_object_get_id(list));
		return -1;
	}

	if (AST_VECTOR_SIZE(&list->items) == 0) {
		ast_log(LOG_WARNING, "Resource list '%s' has no list items\n",
				ast_sorcery_object_get_id(list));
		return -1;
	}

	return 0;
}

static int apply_list_configuration(struct ast_sorcery *sorcery)
{
	ast_sorcery_apply_default(sorcery, "resource_list", "config",
			"pjsip.conf,criteria=type=resource_list");
	if (ast_sorcery_object_register(sorcery, "resource_list", resource_list_alloc,
				NULL, resource_list_apply_handler)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "resource_list", "type", "",
			OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "resource_list", "event", "",
			OPT_CHAR_ARRAY_T, 1, CHARFLDSET(struct resource_list, event));
	ast_sorcery_object_field_register(sorcery, "resource_list", "full_state", "no",
			OPT_BOOL_T, 1, FLDSET(struct resource_list, full_state));
	ast_sorcery_object_field_register(sorcery, "resource_list", "notification_batch_interval",
			"0", OPT_UINT_T, 0, FLDSET(struct resource_list, notification_batch_interval));
	ast_sorcery_object_field_register_custom(sorcery, "resource_list", "list_item",
			"", list_item_handler, list_item_to_str, NULL, 0, 0);

	ast_sorcery_reload_object(sorcery, "resource_list");

	return 0;
}

#ifdef TEST_FRAMEWORK

/*!
 * \brief "bad" resources
 *
 * These are resources that the test handler will reject subscriptions to.
 */
const char *bad_resources[] = {
	"coconut",
	"cilantro",
	"olive",
	"cheese",
};

/*!
 * \brief new_subscribe callback for unit tests
 *
 * Will give a 200 OK response to any resource except the "bad" ones.
 */
static int test_new_subscribe(struct ast_sip_endpoint *endpoint, const char *resource)
{
	int i;

	for (i = 0; i < ARRAY_LEN(bad_resources); ++i) {
		if (!strcmp(resource, bad_resources[i])) {
			return 400;
		}
	}

	return 200;
}

/*!
 * \brief Subscription notifier for unit tests.
 *
 * Since unit tests are only concerned with building a resource tree,
 * only the new_subscribe callback needs to be defined.
 */
struct ast_sip_notifier test_notifier = {
	.new_subscribe = test_new_subscribe,
};

/*!
 * \brief Subscription handler for unit tests.
 */
struct ast_sip_subscription_handler test_handler = {
	.event_name = "test",
	.notifier = &test_notifier,
};

/*!
 * \brief Set properties on an allocated resource list
 *
 * \param list The list to set details on.
 * \param event The list's event.
 * \param resources Array of resources to add to the list.
 * \param num_resources Number of resources in the array.
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int populate_list(struct resource_list *list, const char *event, const char **resources, size_t num_resources)
{
	int i;

	ast_copy_string(list->event, event, sizeof(list->event));

	for (i = 0; i < num_resources; ++i) {
		if (AST_VECTOR_APPEND(&list->items, ast_strdup(resources[i]))) {
			return -1;
		}
	}
	return 0;
}

/*!
 * \brief RAII callback to destroy a resource list
 */
static void cleanup_resource_list(struct resource_list *list)
{
	if (!list) {
		return;
	}

	ast_sorcery_delete(ast_sip_get_sorcery(), list);
	ao2_cleanup(list);
}

/*!
 * \brief allocate a resource list, store it in sorcery, and set its details
 *
 * \param test The unit test. Used for logging status messages.
 * \param list_name The name of the list to create.
 * \param event The event the list services
 * \param resources Array of resources to apply to the list
 * \param num_resources The number of resources in the array
 * \retval NULL Failed to allocate or populate list
 * \retval non-NULL The created list
 */
static struct resource_list *create_resource_list(struct ast_test *test,
		const char *list_name, const char *event, const char **resources, size_t num_resources)
{
	struct resource_list *list;

	list = ast_sorcery_alloc(ast_sip_get_sorcery(), "resource_list", list_name);
	if (!list) {
		ast_test_status_update(test, "Could not allocate resource list in sorcery\n");
		return NULL;
	}

	if (ast_sorcery_create(ast_sip_get_sorcery(), list)) {
		ast_test_status_update(test, "Could not store the resource list in sorcery\n");
		ao2_cleanup(list);
		return NULL;
	}

	if (populate_list(list, event, resources, num_resources)) {
		ast_test_status_update(test, "Could not add resources to the resource list\n");
		cleanup_resource_list(list);
		return NULL;
	}

	return list;
}

/*!
 * \brief Check the integrity of a tree node against a set of resources.
 *
 * The tree node's resources must be in the same order as the resources in
 * the supplied resources array. Because of this constraint, tests can misrepresent
 * the size of the resources array as being smaller than it really is if resources
 * at the end of the array should not be present in the tree node.
 *
 * \param test The unit test. Used for printing status messages.
 * \param node The constructed tree node whose integrity is under question.
 * \param resources Array of expected resource values
 * \param num_resources The number of resources to check in the array.
 */
static int check_node(struct ast_test *test, struct tree_node *node,
		const char **resources, size_t num_resources)
{
	int i;

	if (AST_VECTOR_SIZE(&node->children) != num_resources) {
		ast_test_status_update(test, "Unexpected number of resources in tree. Expected %zu, got %zu\n",
				num_resources, AST_VECTOR_SIZE(&node->children));
		return -1;
	}

	for (i = 0; i < num_resources; ++i) {
		if (strcmp(resources[i], AST_VECTOR_GET(&node->children, i)->resource)) {
			ast_test_status_update(test, "Mismatched resources. Expected '%s' but got '%s'\n",
					resources[i], AST_VECTOR_GET(&node->children, i)->resource);
			return -1;
		}
	}

	return 0;
}

/*!
 * \brief RAII_VAR callback to destroy an allocated resource tree
 */
static void test_resource_tree_destroy(struct resource_tree *tree)
{
	resource_tree_destroy(tree);
	ast_free(tree);
}

static int ineligible_configuration(void)
{
	struct ast_config *config;
	struct ast_flags flags = {0,};
	const char *value;

	config = ast_config_load("sorcery.conf", flags);
	if (!config) {
		return 1;
	}

	value = ast_variable_retrieve(config, "res_pjsip_pubsub", "resource_list");
	if (ast_strlen_zero(value)) {
		ast_config_destroy(config);
		return 1;
	}

	if (strcasecmp(value, "memory") && strcasecmp(value, "astdb")) {
		ast_config_destroy(config);
		return 1;
	}

	return 0;
}

AST_TEST_DEFINE(resource_tree)
{
	RAII_VAR(struct resource_list *, list, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_tree *, tree, NULL, test_resource_tree_destroy);
	const char *resources[] = {
		"huey",
		"dewey",
		"louie",
	};
	int resp;

	switch (cmd) {
	case TEST_INIT:
		info->name = "resource_tree";
		info->category = "/res/res_pjsip_pubsub/";
		info->summary = "Basic resource tree integrity check";
		info->description =
			"Create a resource list and ensure that our attempt to build a tree works as expected.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ineligible_configuration()) {
		ast_test_status_update(test, "Ineligible configuration for this test. Please add a "
				"'res_pjsip_pubsub' section to sorcery.conf, and set 'resource_list=memory'\n");
		return AST_TEST_NOT_RUN;
	}

	list = create_resource_list(test, "foo", "test", resources, ARRAY_LEN(resources));
	if (!list) {
		return AST_TEST_FAIL;
	}

	tree = ast_calloc(1, sizeof(*tree));
	resp = build_resource_tree(NULL, &test_handler, "foo", tree, 1);
	if (resp != 200) {
		ast_test_status_update(test, "Unexpected response %d when building resource tree\n", resp);
		return AST_TEST_FAIL;
	}

	if (!tree->root) {
		ast_test_status_update(test, "Resource tree has no root\n");
		return AST_TEST_FAIL;
	}

	if (check_node(test, tree->root, resources, ARRAY_LEN(resources))) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(complex_resource_tree)
{
	RAII_VAR(struct resource_list *, list_1, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_list *, list_2, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_tree *, tree, NULL, test_resource_tree_destroy);
	const char *resources_1[] = {
		"huey",
		"dewey",
		"louie",
		"dwarves",
	};
	const char *resources_2[] = {
		"happy",
		"grumpy",
		"doc",
		"bashful",
		"dopey",
		"sneezy",
		"sleepy",
	};
	int resp;
	struct tree_node *node;

	switch (cmd) {
	case TEST_INIT:
		info->name = "complex_resource_tree";
		info->category = "/res/res_pjsip_pubsub/";
		info->summary = "Complex resource tree integrity check";
		info->description =
			"Create a complex resource list and ensure that our attempt to build a tree works as expected.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ineligible_configuration()) {
		ast_test_status_update(test, "Ineligible configuration for this test. Please add a "
				"'res_pjsip_pubsub' section to sorcery.conf, and set 'resource_list=memory'\n");
		return AST_TEST_NOT_RUN;
	}

	list_1 = create_resource_list(test, "foo", "test", resources_1, ARRAY_LEN(resources_1));
	if (!list_1) {
		return AST_TEST_FAIL;
	}

	list_2 = create_resource_list(test, "dwarves", "test", resources_2, ARRAY_LEN(resources_2));
	if (!list_2) {
		return AST_TEST_FAIL;
	}

	tree = ast_calloc(1, sizeof(*tree));
	resp = build_resource_tree(NULL, &test_handler, "foo", tree, 1);
	if (resp != 200) {
		ast_test_status_update(test, "Unexpected response %d when building resource tree\n", resp);
		return AST_TEST_FAIL;
	}

	if (!tree->root) {
		ast_test_status_update(test, "Resource tree has no root\n");
		return AST_TEST_FAIL;
	}

	node = tree->root;
	if (check_node(test, node, resources_1, ARRAY_LEN(resources_1))) {
		return AST_TEST_FAIL;
	}

	/* The embedded list is at index 3 in the root node's children */
	node = AST_VECTOR_GET(&node->children, 3);
	if (check_node(test, node, resources_2, ARRAY_LEN(resources_2))) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bad_resource)
{
	RAII_VAR(struct resource_list *, list, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_tree *, tree, NULL, test_resource_tree_destroy);
	const char *resources[] = {
		"huey",
		"dewey",
		"louie",
		"coconut", /* A "bad" resource */
	};
	int resp;

	switch (cmd) {
	case TEST_INIT:
		info->name = "bad_resource";
		info->category = "/res/res_pjsip_pubsub/";
		info->summary = "Ensure bad resources do not end up in the tree";
		info->description =
			"Create a resource list with a single bad resource. Ensure the bad resource does not end up in the tree.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ineligible_configuration()) {
		ast_test_status_update(test, "Ineligible configuration for this test. Please add a "
				"'res_pjsip_pubsub' section to sorcery.conf, and set 'resource_list=memory'\n");
		return AST_TEST_NOT_RUN;
	}

	list = create_resource_list(test, "foo", "test", resources, ARRAY_LEN(resources));
	if (!list) {
		return AST_TEST_FAIL;
	}

	tree = ast_calloc(1, sizeof(*tree));
	resp = build_resource_tree(NULL, &test_handler, "foo", tree, 1);
	if (resp != 200) {
		ast_test_status_update(test, "Unexpected response %d when building resource tree\n", resp);
		return AST_TEST_FAIL;
	}

	if (!tree->root) {
		ast_test_status_update(test, "Resource tree has no root\n");
		return AST_TEST_FAIL;
	}

	/* We check against all but the final resource since we expect it not to be in the tree */
	if (check_node(test, tree->root, resources, ARRAY_LEN(resources) - 1)) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;

}

AST_TEST_DEFINE(bad_branch)
{
	RAII_VAR(struct resource_list *, list_1, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_list *, list_2, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_tree *, tree, NULL, test_resource_tree_destroy);
	const char *resources_1[] = {
		"huey",
		"dewey",
		"louie",
		"gross",
	};
	/* This list has nothing but bad resources */
	const char *resources_2[] = {
		"coconut",
		"cilantro",
		"olive",
		"cheese",
	};
	int resp;

	switch (cmd) {
	case TEST_INIT:
		info->name = "bad_branch";
		info->category = "/res/res_pjsip_pubsub/";
		info->summary = "Ensure bad branches are pruned from the tree";
		info->description =
			"Create a resource list that makes a tree with an entire branch of bad resources.\n"
			"Ensure the bad branch is pruned from the tree.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ineligible_configuration()) {
		ast_test_status_update(test, "Ineligible configuration for this test. Please add a "
				"'res_pjsip_pubsub' section to sorcery.conf, and set 'resource_list=memory'\n");
		return AST_TEST_NOT_RUN;
	}

	list_1 = create_resource_list(test, "foo", "test", resources_1, ARRAY_LEN(resources_1));
	if (!list_1) {
		return AST_TEST_FAIL;
	}
	list_2 = create_resource_list(test, "gross", "test", resources_2, ARRAY_LEN(resources_2));
	if (!list_2) {
		return AST_TEST_FAIL;
	}

	tree = ast_calloc(1, sizeof(*tree));
	resp = build_resource_tree(NULL, &test_handler, "foo", tree, 1);
	if (resp != 200) {
		ast_test_status_update(test, "Unexpected response %d when building resource tree\n", resp);
		return AST_TEST_FAIL;
	}

	if (!tree->root) {
		ast_test_status_update(test, "Resource tree has no root\n");
		return AST_TEST_FAIL;
	}

	/* We check against all but the final resource of the list since the entire branch should
	 * be pruned from the tree
	 */
	if (check_node(test, tree->root, resources_1, ARRAY_LEN(resources_1) - 1)) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;

}

AST_TEST_DEFINE(duplicate_resource)
{
	RAII_VAR(struct resource_list *, list_1, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_list *, list_2, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_tree *, tree, NULL, test_resource_tree_destroy);
	const char *resources_1[] = {
		"huey",
		"ducks",
		"dewey",
		"louie",
	};
	const char *resources_2[] = {
		"donald",
		"daisy",
		"scrooge",
		"dewey",
		"louie",
		"huey",
	};
	int resp;
	struct tree_node *node;

	switch (cmd) {
	case TEST_INIT:
		info->name = "duplicate_resource";
		info->category = "/res/res_pjsip_pubsub/";
		info->summary = "Ensure duplicated resources do not end up in the tree";
		info->description =
			"Create a resource list with a single duplicated resource. Ensure the duplicated resource does not end up in the tree.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ineligible_configuration()) {
		ast_test_status_update(test, "Ineligible configuration for this test. Please add a "
				"'res_pjsip_pubsub' section to sorcery.conf, and set 'resource_list=memory'\n");
		return AST_TEST_NOT_RUN;
	}

	list_1 = create_resource_list(test, "foo", "test", resources_1, ARRAY_LEN(resources_1));
	if (!list_1) {
		return AST_TEST_FAIL;
	}

	list_2 = create_resource_list(test, "ducks", "test", resources_2, ARRAY_LEN(resources_2));
	if (!list_2) {
		return AST_TEST_FAIL;
	}

	tree = ast_calloc(1, sizeof(*tree));
	resp = build_resource_tree(NULL, &test_handler, "foo", tree, 1);
	if (resp != 200) {
		ast_test_status_update(test, "Unexpected response %d when building resource tree\n", resp);
		return AST_TEST_FAIL;
	}

	if (!tree->root) {
		ast_test_status_update(test, "Resource tree has no root\n");
		return AST_TEST_FAIL;
	}

	node = tree->root;
	/* This node should have "huey" and "ducks". "dewey" and "louie" should not
	 * be present since they were found in the "ducks" list.
	 */
	if (check_node(test, node, resources_1, ARRAY_LEN(resources_1) - 2)) {
		return AST_TEST_FAIL;
	}

	/* This node should have "donald", "daisy", "scrooge", "dewey", and "louie".
	 * "huey" is not here since that was already encountered in the parent list
	 */
	node = AST_VECTOR_GET(&node->children, 1);
	if (check_node(test, node, resources_2, ARRAY_LEN(resources_2) - 1)) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(loop)
{
	RAII_VAR(struct resource_list *, list_1, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_list *, list_2, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_tree *, tree, NULL, test_resource_tree_destroy);
	const char *resources_1[] = {
		"derp",
	};
	const char *resources_2[] = {
		"herp",
	};
	int resp;

	switch (cmd) {
	case TEST_INIT:
		info->name = "loop";
		info->category = "/res/res_pjsip_pubsub/";
		info->summary = "Test that loops are properly detected.";
		info->description =
			"Create two resource lists that refer to each other. Ensure that attempting to build a tree\n"
			"results in an empty tree.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ineligible_configuration()) {
		ast_test_status_update(test, "Ineligible configuration for this test. Please add a "
				"'res_pjsip_pubsub' section to sorcery.conf, and set 'resource_list=memory'\n");
		return AST_TEST_NOT_RUN;
	}

	list_1 = create_resource_list(test, "herp", "test", resources_1, ARRAY_LEN(resources_1));
	if (!list_1) {
		return AST_TEST_FAIL;
	}
	list_2 = create_resource_list(test, "derp", "test", resources_2, ARRAY_LEN(resources_2));
	if (!list_2) {
		return AST_TEST_FAIL;
	}

	tree = ast_calloc(1, sizeof(*tree));
	resp = build_resource_tree(NULL, &test_handler, "herp", tree, 1);
	if (resp == 200) {
		ast_test_status_update(test, "Unexpected response %d when building resource tree\n", resp);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bad_event)
{
	RAII_VAR(struct resource_list *, list, NULL, cleanup_resource_list);
	RAII_VAR(struct resource_tree *, tree, NULL, test_resource_tree_destroy);
	const char *resources[] = {
		"huey",
		"dewey",
		"louie",
	};
	int resp;

	switch (cmd) {
	case TEST_INIT:
		info->name = "bad_event";
		info->category = "/res/res_pjsip_pubsub/";
		info->summary = "Ensure that list with wrong event specified is not retrieved";
		info->description =
			"Create a simple resource list for event 'tsetse'. Ensure that trying to retrieve the list for event 'test' fails.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ineligible_configuration()) {
		ast_test_status_update(test, "Ineligible configuration for this test. Please add a "
				"'res_pjsip_pubsub' section to sorcery.conf, and set 'resource_list=memory'\n");
		return AST_TEST_NOT_RUN;
	}

	list = create_resource_list(test, "foo", "tsetse", resources, ARRAY_LEN(resources));
	if (!list) {
		return AST_TEST_FAIL;
	}

	tree = ast_calloc(1, sizeof(*tree));
	/* Since the test_handler is for event "test", this should not build a list, but
	 * instead result in a single resource being created, called "foo"
	 */
	resp = build_resource_tree(NULL, &test_handler, "foo", tree, 1);
	if (resp != 200) {
		ast_test_status_update(test, "Unexpected response %d when building resource tree\n", resp);
		return AST_TEST_FAIL;
	}

	if (!tree->root) {
		ast_test_status_update(test, "Resource tree has no root\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(tree->root->resource, "foo")) {
		ast_test_status_update(test, "Unexpected resource %s found in tree\n", tree->root->resource);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

#endif

static int resource_endpoint_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_publication_resource *resource = obj;

	ast_free(resource->endpoint);
	resource->endpoint = ast_strdup(var->value);

	return 0;
}

static int resource_event_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_publication_resource *resource = obj;
	/* The event configuration name starts with 'event_' so skip past it to get the real name */
	const char *event = var->name + 6;
	struct ast_variable *item;

	if (ast_strlen_zero(event) || ast_strlen_zero(var->value)) {
		return -1;
	}

	item = ast_variable_new(event, var->value, "");
	if (!item) {
		return -1;
	}

	if (resource->events) {
		item->next = resource->events;
	}
	resource->events = item;

	return 0;
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

	if (apply_list_configuration(sorcery)) {
		ast_sip_unregister_service(&pubsub_module);
		ast_sched_context_destroy(sched);
	}

	ast_sorcery_apply_default(sorcery, "inbound-publication", "config", "pjsip.conf,criteria=type=inbound-publication");
	if (ast_sorcery_object_register(sorcery, "inbound-publication", publication_resource_alloc,
		NULL, NULL)) {
		ast_log(LOG_ERROR, "Could not register subscription persistence object support\n");
		ast_sip_unregister_service(&pubsub_module);
		ast_sched_context_destroy(sched);
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_sorcery_object_field_register(sorcery, "inbound-publication", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "inbound-publication", "endpoint", "",
		resource_endpoint_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_fields_register(sorcery, "inbound-publication", "^event_", resource_event_handler, NULL);
	ast_sorcery_reload_object(sorcery, "inbound-publication");

	if (ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		ast_sip_push_task(NULL, subscription_persistence_load, NULL);
	} else {
		stasis_subscribe_pool(ast_manager_get_topic(), subscription_persistence_event_cb, NULL);
	}

	ast_manager_register_xml(AMI_SHOW_SUBSCRIPTIONS_INBOUND, EVENT_FLAG_SYSTEM,
				 ami_show_subscriptions_inbound);
	ast_manager_register_xml(AMI_SHOW_SUBSCRIPTIONS_OUTBOUND, EVENT_FLAG_SYSTEM,
				 ami_show_subscriptions_outbound);
	ast_manager_register_xml("PJSIPShowResourceLists", EVENT_FLAG_SYSTEM,
			ami_show_resource_lists);

	AST_TEST_REGISTER(resource_tree);
	AST_TEST_REGISTER(complex_resource_tree);
	AST_TEST_REGISTER(bad_resource);
	AST_TEST_REGISTER(bad_branch);
	AST_TEST_REGISTER(duplicate_resource);
	AST_TEST_REGISTER(loop);
	AST_TEST_REGISTER(bad_event);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_manager_unregister(AMI_SHOW_SUBSCRIPTIONS_OUTBOUND);
	ast_manager_unregister(AMI_SHOW_SUBSCRIPTIONS_INBOUND);
	ast_manager_unregister("PJSIPShowResourceLists");

	if (sched) {
		ast_sched_context_destroy(sched);
	}

	AST_TEST_UNREGISTER(resource_tree);
	AST_TEST_UNREGISTER(complex_resource_tree);
	AST_TEST_UNREGISTER(bad_resource);
	AST_TEST_UNREGISTER(bad_branch);
	AST_TEST_UNREGISTER(duplicate_resource);
	AST_TEST_UNREGISTER(loop);
	AST_TEST_UNREGISTER(bad_event);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP event resource",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
