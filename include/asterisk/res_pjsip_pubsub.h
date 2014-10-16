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

#ifndef _RES_PJSIP_PUBSUB_H
#define _RES_PJSIP_PUBSUB_H

#include "asterisk/linkedlists.h"

/* Forward declarations */
struct pjsip_rx_data;
struct pjsip_tx_data;
struct pjsip_evsub;
struct ast_sip_endpoint;
struct ast_datastore;
struct ast_datastore_info;

/*!
 * \brief Opaque structure representing a publication
 */
struct ast_sip_publication;

enum ast_sip_publish_state {
    /*! Publication has just been initialized */
    AST_SIP_PUBLISH_STATE_INITIALIZED,
    /*! Publication is currently active */
    AST_SIP_PUBLISH_STATE_ACTIVE,
    /*! Publication has been terminated */
    AST_SIP_PUBLISH_STATE_TERMINATED,
};

/*!
 * \brief Callbacks that publication handlers will define
 */
struct ast_sip_publish_handler {
	/*! \brief The name of the event this handler deals with */
	const char *event_name;

	/*! \brief Publications */
	struct ao2_container *publications;

	/*!
	 * \brief Called when a PUBLISH to establish a new publication arrives.
	 *
	 * \param endpoint The endpoint from whom the PUBLISH arrived.
	 * \param resource The resource whose state is being published.
	 * \param event_configuration The name of the event type configuration to use for this resource.
	 * \return Response code for the incoming PUBLISH
	 */
	int (*new_publication)(struct ast_sip_endpoint *endpoint, const char *resource, const char *event_configuration);
	/*!
	 * \brief Called when a publication has reached its expiration.
	 */
	void (*publish_expire)(struct ast_sip_publication *pub);
	/*!
	 * \brief Published resource has changed states.
	 *
	 * The state parameter can be used to take further action. For instance,
	 * if the state is AST_SIP_PUBLISH_STATE_INITIALIZED, then this is the initial
	 * PUBLISH request. This is a good time to set up datastores on the publication
	 * or any other initial needs.
	 *
	 * AST_SIP_PUBLISH_STATE_TERMINATED is used when the remote end is terminating
	 * its publication. This is a good opportunity to free any resources associated with
	 * the publication.
	 *
	 * AST_SIP_PUBLISH_STATE_ACTIVE is used when a publication that modifies state
	 * arrives.
	 *
	 * \param pub The publication whose state has changed
	 * \param body The body of the inbound PUBLISH
	 * \param state The state of the publication
	 */
	int (*publication_state_change)(struct ast_sip_publication *pub, pjsip_msg_body *body,
			enum ast_sip_publish_state state);
	AST_LIST_ENTRY(ast_sip_publish_handler) next;
};

/*!
 * \brief Given a publication, get the associated endpoint
 *
 * \param pub The publication
 * \retval NULL Failure
 * \retval non-NULL The associated endpoint
 */
struct ast_sip_endpoint *ast_sip_publication_get_endpoint(struct ast_sip_publication *pub);

/*!
 * \brief Given a publication, get the resource the publication is to
 *
 * \param pub The publication
 * \return The resource
 */
const char *ast_sip_publication_get_resource(const struct ast_sip_publication *pub);

/*!
 * \brief Given a publication, get the configuration name for the event type in use
 *
 * \param pub The publication
 * \return The configuration name
 */
const char *ast_sip_publication_get_event_configuration(const struct ast_sip_publication *pub);

/*!
 * \brief Register a publish handler
 *
 * \retval 0 Handler was registered successfully
 * \retval non-zero Handler was not registered successfully
 */
int ast_sip_register_publish_handler(struct ast_sip_publish_handler *handler);

/*!
 * \brief Unregister a publish handler
 */
void ast_sip_unregister_publish_handler(struct ast_sip_publish_handler *handler);

/*!
 * \brief Add a datastore to a SIP publication
 *
 * Note that SIP uses reference counted datastores. The datastore passed into this function
 * must have been allocated using ao2_alloc() or there will be serious problems.
 *
 * \param publication The publication to add the datastore to
 * \param datastore The datastore to be added to the subscription
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_publication_add_datastore(struct ast_sip_publication *publication, struct ast_datastore *datastore);

/*!
 * \brief Retrieve a publication datastore
 *
 * The datastore retrieved will have its reference count incremented. When the caller is done
 * with the datastore, the reference counted needs to be decremented using ao2_ref().
 *
 * \param publication The publication from which to retrieve the datastore
 * \param name The name of the datastore to retrieve
 * \retval NULL Failed to find the specified datastore
 * \retval non-NULL The specified datastore
 */
struct ast_datastore *ast_sip_publication_get_datastore(struct ast_sip_publication *publication, const char *name);

/*!
 * \brief Remove a publication datastore from the publication
 *
 * This operation may cause the datastore's free() callback to be called if the reference
 * count reaches zero.
 *
 * \param publication The publication to remove the datastore from
 * \param name The name of the datastore to remove
 */
void ast_sip_publication_remove_datastore(struct ast_sip_publication *publication, const char *name);

/*!
 * \brief Opaque structure representing an RFC 3265 SIP subscription
 */
struct ast_sip_subscription;

/*!
 * \brief Role for the subscription that is being created
 */
enum ast_sip_subscription_role {
	/* Sending SUBSCRIBEs, receiving NOTIFYs */
	AST_SIP_SUBSCRIBER,
	/* Sending NOTIFYs, receiving SUBSCRIBEs */
	AST_SIP_NOTIFIER,
};

/*!
 * \brief Data for responses to SUBSCRIBEs and NOTIFIEs
 *
 * Some of PJSIP's evsub callbacks expect us to provide them
 * with data so that they can craft a response rather than have
 * us create our own response.
 *
 * Filling in the structure is optional, since the framework
 * will automatically respond with a 200 OK response if we do
 * not provide it with any additional data.
 */
struct ast_sip_subscription_response_data {
	/*! Status code of the response */
	int status_code;
	/*! Optional status text */
	const char *status_text;
	/*! Optional additional headers to add to the response */
	struct ast_variable *headers;
	/*! Optional body to add to the response */
	struct ast_sip_body *body;
};

#define AST_SIP_MAX_ACCEPT 32
enum ast_sip_subscription_notify_reason {
	/*! Initial NOTIFY for subscription */
	AST_SIP_SUBSCRIPTION_NOTIFY_REASON_STARTED,
	/*! Subscription has been renewed */
	AST_SIP_SUBSCRIPTION_NOTIFY_REASON_RENEWED,
	/*! Subscription is being terminated */
	AST_SIP_SUBSCRIPTION_NOTIFY_REASON_TERMINATED,
	/*! Other unspecified reason */
	AST_SIP_SUBSCRIPTION_NOTIFY_REASON_OTHER
};

/*! Type used for conveying mailbox state */
#define AST_SIP_EXTEN_STATE_DATA "ast_sip_exten_state_data"
/*! Type used for extension state/presence */
#define AST_SIP_MESSAGE_ACCUMULATOR "ast_sip_message_accumulator"

/*!
 * \brief Data used to create bodies for NOTIFY/PUBLISH requests.
 */
struct ast_sip_body_data {
	/*! The type of the data */
	const char *body_type;
	/*! The actual data from which the body is generated */
	void *body_data;
};

struct ast_sip_notifier {
	/*!
	 * \brief Default body type defined for the event package this notifier handles.
	 *
	 * Typically, a SUBSCRIBE request will contain one or more Accept headers that tell
	 * what format they expect the body of NOTIFY requests to use. However, every event
	 * package is required to define a default body format type to be used if a SUBSCRIBE
	 * request for the event contains no Accept header.
	 */
	const char *default_accept;
	/*!
	 * \brief Called when a SUBSCRIBE arrives attempting to establish a new subscription.
	 *
	 * The notifier is expected to return the response that should be sent to the
	 * SUBSCRIBE request.
	 *
	 * If a 200-class response is returned, then the notifier's notify_required
	 * callback will immediately be called into with a reason of
	 * AST_SIP_SUBSCRIPTION_NOTIFY_REASON_STARTED.
	 *
	 * \param endpoint The endpoint from which we received the SUBSCRIBE
	 * \param resource The name of the resource to which the subscription is being made
	 * \return The response code to send to the SUBSCRIBE.
	 */
	int (*new_subscribe)(struct ast_sip_endpoint *endpoint, const char *resource);
	/*!
	 * \brief Called when an inbound subscription has been accepted.
	 *
	 * This is a prime opportunity for notifiers to add any notifier-specific
	 * data to the subscription (such as datastores) that it needs to.
	 *
	 * \note There is no need to send a NOTIFY request when this callback
	 * is called
	 *
	 * \param sub The new subscription
	 * \retval 0 Success
	 * \retval -1 Failure
	 */
	int (*subscription_established)(struct ast_sip_subscription *sub);
	/*!
	 * \brief Supply data needed to create a NOTIFY body.
	 *
	 * The returned data must be an ao2 object. The caller of this function
	 * will be responsible for decrementing the refcount of the returned object
	 *
	 * \param sub The subscription
	 * \return An ao2 object that can be used to create a NOTIFY body.
	 */
	void *(*get_notify_data)(struct ast_sip_subscription *sub);
};

struct ast_sip_subscriber {
	/*!
	 * \brief A NOTIFY has been received.
	 *
	 * The body of the NOTIFY is provided so that it may be parsed and appropriate
	 * internal state change may be generated.
	 *
	 * The state can be used to determine if the subscription has been terminated
	 * by the far end or if this is just a typical resource state change.
	 *
	 * \param sub The subscription on which the NOTIFY arrived
	 * \param body The body of the NOTIFY
	 * \param state The subscription state
	 */
	void (*state_change)(struct ast_sip_subscription *sub, pjsip_msg_body *body, enum pjsip_evsub_state state);
};

struct ast_sip_subscription_handler {
	/*! The name of the event this subscriber deals with */
	const char *event_name;
	/*! Type of data used to generate NOTIFY bodies */
	const char *body_type;
	/*! The types of body this subscriber accepts. */
	const char *accept[AST_SIP_MAX_ACCEPT];
	/*!
	 * \brief Called when a subscription is to be destroyed
	 *
	 * The handler is not expected to send any sort of requests or responses
	 * during this callback. The handler MUST, however, begin the destruction
	 * process for the subscription during this callback.
	 */
	void (*subscription_shutdown)(struct ast_sip_subscription *subscription);
	/*!
	 * \brief Converts the subscriber to AMI
	 *
	 * \param sub The subscription
	 * \param buf The string to write AMI data
	 */
	void (*to_ami)(struct ast_sip_subscription *sub, struct ast_str **buf);
	/*! Subscriber callbacks for this handler */
	struct ast_sip_subscriber *subscriber;
	/*! Notifier callbacks for this handler */
	struct ast_sip_notifier *notifier;
	AST_LIST_ENTRY(ast_sip_subscription_handler) next;
};

/*!
 * \brief Create a new ast_sip_subscription structure
 *
 * When a subscriber wishes to create a subscription, it may call this function
 * to allocate resources and to send the initial SUBSCRIBE out.
 *
 * \param subscriber The subscriber that is making the request.
 * \param endpoint The endpoint to whome the SUBSCRIBE will be sent.
 * \param resource The resource to place in the SUBSCRIBE's Request-URI.
 */
struct ast_sip_subscription *ast_sip_create_subscription(const struct ast_sip_subscription_handler *handler,
		struct ast_sip_endpoint *endpoint, const char *resource);


/*!
 * \brief Get the endpoint that is associated with this subscription
 *
 * This function will increase the reference count of the endpoint. Be sure to
 * release the reference to it when you are finished with the endpoint.
 *
 * \retval NULL Could not get endpoint
 * \retval non-NULL The endpoint
 */
struct ast_sip_endpoint *ast_sip_subscription_get_endpoint(struct ast_sip_subscription *sub);

/*!
 * \brief Get the serializer for the subscription
 *
 * Tasks that originate outside of a SIP servant thread should get the serializer
 * and push the task to the serializer.
 *
 * \param sub The subscription
 * \retval NULL Failure
 * \retval non-NULL The subscription's serializer
 */
struct ast_taskprocessor *ast_sip_subscription_get_serializer(struct ast_sip_subscription *sub);

/*!
 * \brief Notify a SIP subscription of a state change.
 *
 * This tells the pubsub core that the state of a subscribed resource has changed.
 * The pubsub core will generate an appropriate NOTIFY request to send to the
 * subscriber.
 *
 * \param sub The subscription on which a state change is occurring.
 * \param notify_data Event package-specific data used to create the NOTIFY body.
 * \param terminate True if this NOTIFY is intended to terminate the subscription.
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_subscription_notify(struct ast_sip_subscription *sub, struct ast_sip_body_data *notify_data, int terminate);

/*!
 * \brief Retrieve the local URI for this subscription
 *
 * This is the local URI of the subscribed resource.
 *
 * \param sub The subscription
 * \param[out] buf The buffer into which to store the URI.
 * \param size The size of the buffer.
 */
void ast_sip_subscription_get_local_uri(struct ast_sip_subscription *sub, char *buf, size_t size);

/*!
 * \brief Retrive the remote URI for this subscription
 *
 * This is the remote URI as determined by the underlying SIP dialog.
 *
 * \param sub The subscription
 * \param[out] buf The buffer into which to store the URI.
 * \param size The size of the buffer.
 */
void ast_sip_subscription_get_remote_uri(struct ast_sip_subscription *sub, char *buf, size_t size);

/*!
 * \brief Get the name of the subscribed resource.
 */
const char *ast_sip_subscription_get_resource_name(struct ast_sip_subscription *sub);

/*!
 * \brief Get a header value for a subscription.
 *
 * For notifiers, the headers of the inbound SUBSCRIBE that started the dialog
 * are stored on the subscription. This method allows access to the header. The
 * return is the same as pjsip_msg_find_hdr_by_name(), meaning that it is dependent
 * on the header being searched for.
 *
 * \param sub The subscription to search in.
 * \param header The name of the header to search for.
 * \return The discovered header, or NULL if the header cannot be found.
 */
void *ast_sip_subscription_get_header(const struct ast_sip_subscription *sub, const char *header);

/*!
 * \brief Send a request created via a PJSIP evsub method
 *
 * Callers of this function should take care to do so within a SIP servant
 * thread.
 *
 * \param sub The subscription on which to send the request
 * \param tdata The request to send
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_subscription_send_request(struct ast_sip_subscription *sub, pjsip_tx_data *tdata);

/*!
 * \brief Alternative for ast_datastore_alloc()
 *
 * There are two major differences between this and ast_datastore_alloc()
 * 1) This allocates a refcounted object
 * 2) This will fill in a uid if one is not provided
 *
 * DO NOT call ast_datastore_free() on a datastore allocated in this
 * way since that function will attempt to free the datastore rather
 * than play nicely with its refcount.
 *
 * \param info Callbacks for datastore
 * \param uid Identifier for datastore
 * \retval NULL Failed to allocate datastore
 * \retval non-NULL Newly allocated datastore
 */
struct ast_datastore *ast_sip_subscription_alloc_datastore(const struct ast_datastore_info *info, const char *uid);

/*!
 * \brief Add a datastore to a SIP subscription
 *
 * Note that SIP uses reference counted datastores. The datastore passed into this function
 * must have been allocated using ao2_alloc() or there will be serious problems.
 *
 * \param subscription The ssubscription to add the datastore to
 * \param datastore The datastore to be added to the subscription
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_subscription_add_datastore(struct ast_sip_subscription *subscription, struct ast_datastore *datastore);

/*!
 * \brief Retrieve a subscription datastore
 *
 * The datastore retrieved will have its reference count incremented. When the caller is done
 * with the datastore, the reference counted needs to be decremented using ao2_ref().
 *
 * \param subscription The subscription from which to retrieve the datastore
 * \param name The name of the datastore to retrieve
 * \retval NULL Failed to find the specified datastore
 * \retval non-NULL The specified datastore
 */
struct ast_datastore *ast_sip_subscription_get_datastore(struct ast_sip_subscription *subscription, const char *name);

/*!
 * \brief Remove a subscription datastore from the subscription
 *
 * This operation may cause the datastore's free() callback to be called if the reference
 * count reaches zero.
 *
 * \param subscription The subscription to remove the datastore from
 * \param name The name of the datastore to remove
 */
void ast_sip_subscription_remove_datastore(struct ast_sip_subscription *subscription, const char *name);

/*!
 * \brief Register a subscription handler
 *
 * \retval 0 Handler was registered successfully
 * \retval non-zero Handler was not registered successfully
 */
int ast_sip_register_subscription_handler(struct ast_sip_subscription_handler *handler);

/*!
 * \brief Unregister a subscription handler
 */
void ast_sip_unregister_subscription_handler(struct ast_sip_subscription_handler *handler);

/*!
 * \brief Pubsub body generator
 *
 * A body generator is responsible for taking Asterisk content
 * and converting it into a body format to be placed in an outbound
 * SIP NOTIFY or PUBLISH request.
 */
struct ast_sip_pubsub_body_generator {
	/*!
	 * \brief Content type
	 * In "plain/text", "plain" is the type
	 */
	const char *type;
	/*!
	 * \brief Content subtype
	 * In "plain/text", "text" is the subtype
	 */
	const char *subtype;
	/*! Type of data the body generator takes as input */
	const char *body_type;
	/*!
	 * \brief allocate body structure.
	 *
	 * Body generators will have this method called when a NOTIFY
	 * or PUBLISH body needs to be created. The type returned depends on
	 * the type of content being produced for the body. The data parameter
	 * is provided by the subscription handler and will vary between different
	 * event types.
	 *
	 * \param data The subscription data provided by the event handler
	 * \retval non-NULL The allocated body
	 * \retval NULL Failure
	 */
	void *(*allocate_body)(void *data);
	/*!
	 * \brief Add content to the body of a SIP request
	 *
	 * The body of the request has already been allocated by the body generator's
	 * allocate_body callback.
	 *
	 * \param body The body of the SIP request. The type is determined by the
	 * content type.
	 * \param data The subscription data used to populate the body. The type is
	 * determined by the content type.
	 */
	int (*generate_body_content)(void *body, void *data);
	/*!
	 * \brief Convert the body to a string.
	 *
	 * \param body The request body.
	 * \param str The converted string form of the request body
	 */
	void (*to_string)(void *body, struct ast_str **str);
	/*!
	 * \brief Deallocate resources created for the body
	 *
	 * Optional callback to destroy resources allocated for the
	 * message body.
	 *
	 * \param body Body to be destroyed
	 */
	void (*destroy_body)(void *body);
	AST_LIST_ENTRY(ast_sip_pubsub_body_generator) list;
};

/*!
 * \brief Body supplement
 *
 * Body supplements provide additions to bodies not already
 * provided by body generators. This may include proprietary
 * extensions, optional content, or other nonstandard fare.
 */
struct ast_sip_pubsub_body_supplement {
	/*!
	 * \brief Content type
	 * In "plain/text", "plain" is the type
	 */
	const char *type;
	/*!
	 * \brief Content subtype
	 * In "plain/text", "text" is the subtype
	 */
	const char *subtype;
	/*!
	 * \brief Add additional content to a SIP request body.
	 *
	 * A body generator will have already allocated a body and populated
	 * it with base data for the event. The supplement's duty is, if desired,
	 * to extend the body to have optional data beyond what a base RFC specifies.
	 *
	 * \param body The body of the SIP request. The type is determined by the
	 * body generator that allocated the body.
	 * \param data The subscription data used to populate the body. The type is
	 * determined by the content type.
	 */
	int (*supplement_body)(void *body, void *data);
	AST_LIST_ENTRY(ast_sip_pubsub_body_supplement) list;
};

/*!
 * \since 13.0.0
 * \brief Generate body content for a PUBLISH or NOTIFY
 *
 * This function takes a pre-allocated body and calls into registered body
 * generators in order to fill in the body with appropriate details.
 * The primary body generator will be called first, followed by the
 * supplementary body generators
 *
 * \param content_type The content type of the body
 * \param content_subtype The content subtype of the body
 * \param data The data associated with body generation.
 * \param[out] str The string representation of the generated body
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_pubsub_generate_body_content(const char *content_type,
		const char *content_subtype, struct ast_sip_body_data *data, struct ast_str **str);

/*!
 * \since 13.0.0
 * \brief Register a body generator with the pubsub core.
 *
 * This may fail if an attempt is made to register a primary body supplement
 * for a given content type if a primary body supplement for that content type
 * has already been registered.
 *
 * \param generator Body generator to register
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_pubsub_register_body_generator(struct ast_sip_pubsub_body_generator *generator);

/*!
 * \since 13.0.0
 * \brief Unregister a body generator with the pubsub core.
 *
 * \param generator Body generator to unregister
 */
void ast_sip_pubsub_unregister_body_generator(struct ast_sip_pubsub_body_generator *generator);

/*!
 * \since 13.0.0
 * \brief Register a body generator with the pubsub core.
 *
 * This may fail if an attempt is made to register a primary body supplement
 * for a given content type if a primary body supplement for that content type
 * has already been registered.
 *
 * \param generator Body generator to register
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_pubsub_register_body_supplement(struct ast_sip_pubsub_body_supplement *supplement);

/*!
 * \since 13.0.0
 * \brief Unregister a body generator with the pubsub core.
 *
 * \param generator Body generator to unregister
 */
void ast_sip_pubsub_unregister_body_supplement(struct ast_sip_pubsub_body_supplement *supplement);

/*!
 * \since 13.0.0
 * \brief Get the body type used for this subscription
 */
const char *ast_sip_subscription_get_body_type(struct ast_sip_subscription *sub);

/*!
 * \since 13.0.0
 * \brief Get the body subtype used for this subscription
 */
const char *ast_sip_subscription_get_body_subtype(struct ast_sip_subscription *sub);

/*! \brief Determines whether the res_pjsip_pubsub module is loaded */
#define CHECK_PJSIP_PUBSUB_MODULE_LOADED()			\
	do {							\
		CHECK_PJSIP_MODULE_LOADED();			\
		if (!ast_module_check("res_pjsip_pubsub.so")) {	\
			return AST_MODULE_LOAD_DECLINE;		\
		}						\
	} while(0)

#endif /* RES_PJSIP_PUBSUB_H */
