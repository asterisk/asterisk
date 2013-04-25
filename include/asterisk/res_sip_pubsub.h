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

#ifndef _RES_SIP_PUBSUB_H
#define _RES_SIP_PUBSUB_H

#include "asterisk/linkedlists.h"

/* Forward declarations */
struct pjsip_rx_data;
struct pjsip_tx_data;
struct pjsip_evsub;
struct ast_sip_endpoint;
struct ast_datastore;
struct ast_datastore_info;

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

struct ast_sip_subscription_handler {
    /*! The name of the event this handler deals with */
    const char *event_name;
    /*! The types of body this handler accepts */
    const char *accept[AST_SIP_MAX_ACCEPT];
 
    /*!
     * \brief Called when a subscription is to be destroyed
     *
     * This is a subscriber and notifier callback.
     *
     * The handler is not expected to send any sort of requests or responses
     * during this callback. The handler MUST, however, begin the destruction
	 * process for the subscription during this callback.
     */
   void (*subscription_shutdown)(struct ast_sip_subscription *subscription);
 
    /*!
     * \brief Called when a SUBSCRIBE arrives in order to create a new subscription
     *
     * This is a notifier callback.
     *
     * If the notifier wishes to accept the subscription, then it can create
     * a new ast_sip_subscription to do so. 
     *
     * If the notifier chooses to create a new subscription, then it must accept
     * the incoming subscription using pjsip_evsub_accept() and it must also
     * send an initial NOTIFY with the current subscription state.
     *
     * \param endpoint The endpoint from which we received the SUBSCRIBE
     * \param rdata The SUBSCRIBE request
     * \retval NULL The SUBSCRIBE has not been accepted
     * \retval non-NULL The newly-created subscription
     */
    struct ast_sip_subscription *(*new_subscribe)(struct ast_sip_endpoint *endpoint,
            pjsip_rx_data *rdata);
 
    /*!
     * \brief Called when an endpoint renews a subscription.
     *
     * This is a notifier callback.
     *
     * Because of the way that the PJSIP evsub framework works, it will automatically
     * send a response to the SUBSCRIBE. However, the subscription handler must send
	 * a NOTIFY with the current subscription state when this callback is called.
	 *
	 * The response_data that is passed into this callback is used to craft what should
	 * be in the response to the incoming SUBSCRIBE. It is initialized with a 200 status
	 * code and all other parameters are empty.
     *
     * \param sub The subscription that is being renewed
     * \param rdata The SUBSCRIBE request in question
	 * \param[out] response_data Data pertaining to the SIP response that should be
	 * sent to the SUBSCRIBE
     */
    void (*resubscribe)(struct ast_sip_subscription *sub,
            pjsip_rx_data *rdata, struct ast_sip_subscription_response_data *response_data);
 
    /*!
     * \brief Called when a subscription times out.
     *
     * This is a notifier callback
     *
     * This indicates that the subscription has timed out. The subscription handler is
     * expected to send a NOTIFY that terminates the subscription.
     *
     * \param sub The subscription that has timed out
     */
    void (*subscription_timeout)(struct ast_sip_subscription *sub);
 
    /*!
     * \brief Called when a subscription is terminated via a SUBSCRIBE or NOTIFY request
     *
     * This is a notifier and subscriber callback.
     *
     * The PJSIP subscription framework will automatically send the response to the
     * request. If a notifier receives this callback, then the subscription handler
	 * is expected to send a final NOTIFY to terminate the subscription.
     *
     * \param sub The subscription being terminated
     * \param rdata The request that terminated the subscription
     */
    void (*subscription_terminated)(struct ast_sip_subscription *sub, pjsip_rx_data *rdata);
 
    /*!
     * \brief Called when a subscription handler's outbound NOTIFY receives a response
     *
     * This is a notifier callback.
     *
     * \param sub The subscription
     * \param rdata The NOTIFY response
     */
    void (*notify_response)(struct ast_sip_subscription *sub, pjsip_rx_data *rdata);
 
    /*!
     * \brief Called when a subscription handler receives an inbound NOTIFY
     *
     * This is a subscriber callback.
     *
     * Because of the way that the PJSIP evsub framework works, it will automatically
     * send a response to the NOTIFY. By default this will be a 200 OK response, but
     * this callback can change details of the response by returning response data
     * to use.
	 *
	 * The response_data that is passed into this callback is used to craft what should
	 * be in the response to the incoming SUBSCRIBE. It is initialized with a 200 status
	 * code and all other parameters are empty.
     *
     * \param sub The subscription
     * \param rdata The NOTIFY request
	 * \param[out] response_data Data pertaining to the SIP response that should be
	 * sent to the SUBSCRIBE
     */
    void (*notify_request)(struct ast_sip_subscription *sub,
            pjsip_rx_data *rdata, struct ast_sip_subscription_response_data *response_data);
 
    /*!
     * \brief Called when it is time for a subscriber to resubscribe
     *
     * This is a subscriber callback.
     *
     * The subscriber can reresh the subscription using the pjsip_evsub_initiate()
     * function.
     *
     * \param sub The subscription to refresh
     * \retval 0 Success
     * \retval non-zero Failure
     */
    int (*refresh_subscription)(struct ast_sip_subscription *sub);
	AST_LIST_ENTRY(ast_sip_subscription_handler) next;
};

/*!
 * \brief Create a new ast_sip_subscription structure
 *
 * In most cases the pubsub core will create a general purpose subscription
 * within PJSIP. However, PJSIP provides enhanced support for the following
 * event packages:
 *
 * presence
 * message-summary
 *
 * If either of these events are handled by the subscription handler, then
 * the special-purpose event subscriptions will be created within PJSIP,
 * and it will be expected that your subscription handler make use of the
 * special PJSIP APIs.
 *
 * \param handler The subsription handler for this subscription
 * \param role Whether we are acting as subscriber or notifier for this subscription
 * \param endpoint The endpoint involved in this subscription
 * \param rdata If acting as a notifier, the SUBSCRIBE request that triggered subscription creation
 */
struct ast_sip_subscription *ast_sip_create_subscription(const struct ast_sip_subscription_handler *handler,
        enum ast_sip_subscription_role role, struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata);
 
 
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
 * \brief Get the underlying PJSIP evsub structure
 *
 * This is useful when wishing to call PJSIP's API calls in order to
 * create SUBSCRIBEs, NOTIFIES, etc. as well as get subscription state
 *
 * This function, as well as all methods called on the pjsip_evsub should
 * be done in a SIP servant thread.
 *
 * \param sub The subscription
 * \retval NULL Failure
 * \retval non-NULL The underlying pjsip_evsub
 */
pjsip_evsub *ast_sip_subscription_get_evsub(struct ast_sip_subscription *sub);
 
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

#endif /* RES_SIP_PUBSUB_H */
