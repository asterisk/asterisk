/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

#ifndef _RES_PJSIP_OUTBOUND_PUBLISH_H
#define _RES_PJSIP_OUTBOUND_PUBLISH_H

#include "asterisk/linkedlists.h"

/* Forward declarations */
struct ast_datastore;
struct ast_datastore_info;

/*!
 * \brief Opaque structure representing outbound publish configuration
 */
struct ast_sip_outbound_publish;

/*!
 * \brief Opaque structure representing an outbound publish client
 */
struct ast_sip_outbound_publish_client;

/*!
 * \brief Callbacks that event publisher handlers will define
 */
struct ast_sip_event_publisher_handler {
	/*! \brief The name of the event this handler deals with */
	const char *event_name;

	/*!
	 * \brief Called when a publisher should start publishing.
	 *
	 * \param configuration The outbound publish configuration, event-specific configuration
	 *        is accessible using extended sorcery fields
	 * \param client The publish client that can be used to send PUBLISH messages.
	 * \retval 0 success
	 * \retval -1 failure
	 */
	int (*start_publishing)(struct ast_sip_outbound_publish *configuration,
		struct ast_sip_outbound_publish_client *client);

	/*!
	 * \brief Called when a publisher should stop publishing.
	 *
	 * \param client The publish client that was used to send PUBLISH messages.
	 * \retval 0 success
	 * \retval -1 failure
	 */
	int (*stop_publishing)(struct ast_sip_outbound_publish_client *client);

	AST_LIST_ENTRY(ast_sip_event_publisher_handler) next;
};

/*!
 * \brief Register an event publisher handler
 *
 * \retval 0 Handler was registered successfully
 * \retval non-zero Handler was not registered successfully
 */
int ast_sip_register_event_publisher_handler(struct ast_sip_event_publisher_handler *handler);

/*!
 * \brief Unregister a publish handler
 */
void ast_sip_unregister_event_publisher_handler(struct ast_sip_event_publisher_handler *handler);

/*!
 * \brief Find a publish client using its name
 *
 * \param name The name of the publish client
 *
 * \retval NULL failure
 * \retval non-NULL success
 *
 * \note The publish client is returned with its reference count increased and must be released using
 *       ao2_cleanup.
 */
struct ast_sip_outbound_publish_client *ast_sip_publish_client_get(const char *name);

/*!
 * \brief Get the From URI the client will use.
 * \since 14.0.0
 *
 * \param client The publication client to get the From URI
 *
 * \retval From-uri on success
 * \retval Empty-string on failure
 */
const char *ast_sip_publish_client_get_from_uri(struct ast_sip_outbound_publish_client *client);

/*!
 * \brief Get the From URI the client will use for a specific user.
 * \since 14.0.0
 *
 * \param client The publication client to get the From URI of a user
 * \param user The user to retrieve the From URI for
 * \param uri A buffer to place the URI into
 * \param size The size of the buffer
 *
 * \retval From-uri on success
 * \retval Empty-string on failure
 */
const char *ast_sip_publish_client_get_user_from_uri(struct ast_sip_outbound_publish_client *client, const char *user,
	char *uri, size_t size);

/*!
 * \brief Get the To URI the client will use.
 * \since 14.0.0
 *
 * \param client The publication client to get the To URI
 *
 * \retval From-uri on success
 * \retval Empty-string on failure
 */
const char *ast_sip_publish_client_get_to_uri(struct ast_sip_outbound_publish_client *client);

/*!
 * \brief Get the To URI the client will use for a specific user.
 * \since 14.0.0
 *
 * \param client The publication client to get the To URI of a user
 * \param user The user to retrieve the To URI for
 * \param uri A buffer to place the URI into
 * \param size The size of the buffer
 *
 * \retval To-uri on success
 * \retval Empty-string on failure
 */
const char *ast_sip_publish_client_get_user_to_uri(struct ast_sip_outbound_publish_client *client, const char *user,
	char *uri, size_t size);

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
struct ast_datastore *ast_sip_publish_client_alloc_datastore(const struct ast_datastore_info *info, const char *uid);

/*!
 * \brief Add a datastore to a SIP event publisher
 *
 * Note that SIP uses reference counted datastores. The datastore passed into this function
 * must have been allocated using ao2_alloc() or there will be serious problems.
 *
 * \param client The publication client to add the datastore to
 * \param datastore The datastore to be added to the subscription
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_publish_client_add_datastore(struct ast_sip_outbound_publish_client *client,
	struct ast_datastore *datastore);

/*!
 * \brief Retrieve an event publisher datastore
 *
 * The datastore retrieved will have its reference count incremented. When the caller is done
 * with the datastore, the reference counted needs to be decremented using ao2_ref().
 *
 * \param client The publication client from which to retrieve the datastore
 * \param name The name of the datastore to retrieve
 * \retval NULL Failed to find the specified datastore
 * \retval non-NULL The specified datastore
 */
struct ast_datastore *ast_sip_publish_client_get_datastore(struct ast_sip_outbound_publish_client *client,
	const char *name);

/*!
 * \brief Remove a publication datastore from an event publisher
 *
 * This operation may cause the datastore's free() callback to be called if the reference
 * count reaches zero.
 *
 * \param client The publication client to remove the datastore from
 * \param name The name of the datastore to remove
 */
void ast_sip_publish_client_remove_datastore(struct ast_sip_outbound_publish_client *client,
	const char *name);

/*!
 * \brief Send an outgoing PUBLISH message using a client
 *
 * \param client The publication client to send from
 * \param body An optional body to add to the PUBLISH
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_publish_client_send(struct ast_sip_outbound_publish_client *client,
	const struct ast_sip_body *body);

/*!
* \brief Send an outgoing PUBLISH message based on the user
*
* \param client The publication client to send from
* \param user The user to send to
* \param body An optional body to add to the PUBLISH
*
* \retval -1 failure
* \retval 0 success
*/
int ast_sip_publish_client_user_send(struct ast_sip_outbound_publish_client *client,
	const char *user, const struct ast_sip_body *body);

/*!
* \brief Remove the user from the client (stopping it from publishing)
*
* \param client The publication client
* \param user The user to remove
*/
void ast_sip_publish_client_remove(struct ast_sip_outbound_publish_client *client,
	const char *user);

#endif /* RES_PJSIP_OUTBOUND_PUBLISH_H */
