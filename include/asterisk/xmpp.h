/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
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

/*! \file
 * \brief XMPP Interface
 * \author Joshua Colp <jcolp@digium.com>
 * IKSEMEL http://iksemel.jabberstudio.org
 */

#ifndef _ASTERISK_XMPP_H
#define _ASTERISK_XMPP_H

#ifdef HAVE_OPENSSL

#include <openssl/ssl.h>
#include <openssl/err.h>
#define TRY_SECURE 2
#define SECURE 4

#endif /* HAVE_OPENSSL */

/* file is read by blocks with this size */
#define NET_IO_BUF_SIZE 16384

/* Return value for timeout connection expiration */
#define IKS_NET_EXPIRED 12

#include <iksemel.h>

#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/linkedlists.h"
#include "asterisk/stringfields.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis.h"

/*
 * As per RFC 3920 - section 3.1, the maximum length for a full Jabber ID
 * is 3071 bytes.
 * The ABNF syntax for jid :
 * jid = [node "@" ] domain [ "/" resource ]
 * Each allowable portion of a JID (node identifier, domain identifier,
 * and resource identifier) MUST NOT be more than 1023 bytes in length,
 * resulting in a maximum total size (including the '@' and '/' separators)
 * of 3071 bytes.
 */
#define XMPP_MAX_JIDLEN 3071

/*! \brief Maximum size of a resource JID */
#define XMPP_MAX_RESJIDLEN 1023

/*! \brief Maximum size of an attribute */
#define XMPP_MAX_ATTRLEN   256

/*! \brief Client connection states */
enum xmpp_state {
	XMPP_STATE_DISCONNECTING,   /*!< Client is disconnecting */
	XMPP_STATE_DISCONNECTED,    /*!< Client is disconnected */
	XMPP_STATE_CONNECTING,      /*!< Client is connecting */
	XMPP_STATE_REQUEST_TLS,     /*!< Client should request TLS */
	XMPP_STATE_REQUESTED_TLS,   /*!< Client has requested TLS */
	XMPP_STATE_AUTHENTICATE,    /*!< Client needs to authenticate */
	XMPP_STATE_AUTHENTICATING,  /*!< Client is authenticating */
	XMPP_STATE_ROSTER,          /*!< Client is currently getting the roster */
	XMPP_STATE_CONNECTED,       /*!< Client is fully connected */
};

/*! \brief Resource capabilities */
struct ast_xmpp_capabilities {
	char node[200];        /*!< Node string from the capabilities stanza in presence notification */
	char version[50];      /*!< Version string from the capabilities stanza in presence notification */
	unsigned int jingle:1; /*!< Set if the resource supports Jingle */
	unsigned int google:1; /*!< Set if the resource supports Google Talk */
};

/*! \brief XMPP Resource */
struct ast_xmpp_resource {
	char resource[XMPP_MAX_RESJIDLEN]; /*!< JID of the resource */
	int status;                        /*!< Current status of the resource */
	char *description;                 /*!< Description of the resource */
	int priority;                      /*!< Priority, used for deciding what resource to use */
	struct ast_xmpp_capabilities caps; /*!< Capabilities of the resource */
};

/*! \brief XMPP Message */
struct ast_xmpp_message {
	char *from;                            /*!< Who the message is from */
	char *message;                         /*!< Message contents */
	char id[25];                           /*!< Identifier for the message */
	struct timeval arrived;                /*!< When the message arrived */
	AST_LIST_ENTRY(ast_xmpp_message) list; /*!< Linked list information */
};

struct ast_endpoint;

/*! \brief XMPP Buddy */
struct ast_xmpp_buddy {
	char id[XMPP_MAX_JIDLEN];        /*!< JID of the buddy */
	struct ao2_container *resources; /*!< Resources for the buddy */
	unsigned int subscribe:1;        /*!< Need to subscribe to get their status */
};

/*! \brief XMPP Client Connection */
struct ast_xmpp_client {
	AST_DECLARE_STRING_FIELDS(
		/*! Name of the client configuration */
		AST_STRING_FIELD(name);
		);
	/*! Message ID */
	char mid[6];
	iksid *jid;
	iksparser *parser;
	iksfilter *filter;
	ikstack *stack;
#ifdef HAVE_OPENSSL
	SSL_CTX *ssl_context;
	SSL *ssl_session;
	const SSL_METHOD *ssl_method;
	unsigned int stream_flags;
#endif /* HAVE_OPENSSL */
	enum xmpp_state state;
	struct ao2_container *buddies;
	AST_LIST_HEAD(, ast_xmpp_message) messages;
	pthread_t thread;
	int timeout;
	/*! Reconnect this client */
	unsigned int reconnect:1;
	/*! If distributing event information the MWI subscription */
	struct stasis_subscription *mwi_sub;
	/*! If distributing event information the device state subscription */
	struct stasis_subscription *device_state_sub;
	/*! The endpoint associated with this client */
	struct ast_endpoint *endpoint;
};

/*!
 * \brief Find an XMPP client connection using a given name
 *
 * \param name Name of the client connection
 *
 * \retval non-NULL on success
 * \retval NULL on failure
 *
 * \note This will return the client connection with the reference count incremented by one.
 */
struct ast_xmpp_client *ast_xmpp_client_find(const char *name);

/*!
 * \brief Disconnect an XMPP client connection
 *
 * \param client Pointer to the client
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_xmpp_client_disconnect(struct ast_xmpp_client *client);

/*!
 * \brief Release XMPP client connection reference
 *
 * \param client Pointer to the client
 */
void ast_xmpp_client_unref(struct ast_xmpp_client *client);

/*!
 * \brief Lock an XMPP client connection
 *
 * \param client Pointer to the client
 */
void ast_xmpp_client_lock(struct ast_xmpp_client *client);

/*!
 * \brief Unlock an XMPP client connection
 *
 * \param client Pointer to the client
 */
void ast_xmpp_client_unlock(struct ast_xmpp_client *client);

/*!
 * \brief Send an XML stanza out using an established XMPP client connection
 *
 * \param client Pointer to the client
 * \param stanza Pointer to the Iksemel stanza
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_xmpp_client_send(struct ast_xmpp_client *client, iks *stanza);

/*!
 * \brief Send a message to a given user using an established XMPP client connection
 *
 * \param client Pointer to the client
 * \param user User the message should be sent to
 * \param message The message to send
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_xmpp_client_send_message(struct ast_xmpp_client *client, const char *user, const char *message);

/*!
 * \brief Invite a user to an XMPP multi-user chatroom
 *
 * \param client Pointer to the client
 * \param user JID of the user
 * \param room Name of the chatroom
 * \param message Message to send with the invitation
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_xmpp_chatroom_invite(struct ast_xmpp_client *client, const char *user, const char *room, const char *message);

/*!
 * \brief Join an XMPP multi-user chatroom
 *
 * \param client Pointer to the client
 * \param room Name of the chatroom
 * \param nickname Nickname to use
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_xmpp_chatroom_join(struct ast_xmpp_client *client, const char *room, const char *nickname);

/*!
 * \brief Send a message to an XMPP multi-user chatroom
 *
 * \param client Pointer to the client
 * \param nickname Nickname to use
 * \param address Address of the room
 * \param message Message itself
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_xmpp_chatroom_send(struct ast_xmpp_client *client, const char *nickname, const char *address, const char *message);

/*!
 * \brief Leave an XMPP multi-user chatroom
 *
 * \param client Pointer to the client
 * \param room Name of the chatroom
 * \param nickname Nickname being used
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_xmpp_chatroom_leave(struct ast_xmpp_client *client, const char *room, const char *nickname);

/*!
 * \brief Helper function which increments the message identifier
 *
 * \param mid Pointer to a string containing the message identifier
 */
void ast_xmpp_increment_mid(char *mid);

#endif
