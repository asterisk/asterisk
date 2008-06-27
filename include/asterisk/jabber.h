/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matt O'Gorman <mogorman@digium.com>
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
 * \brief AJI - The Asterisk Jabber Interface
 * \arg \ref AJI_intro
 * \ref res_jabber.c
 * \author Matt O'Gorman <mogorman@digium.com>
 * \extref IKSEMEL http://iksemel.jabberstudio.org
 *
 * \page AJI_intro AJI - The Asterisk Jabber Interface
 * 
 * The Asterisk Jabber Interface, AJI, publishes an API for
 * modules to use jabber communication. res_jabber.c implements
 * a Jabber client and a component that can connect as a service
 * to Jabber servers.
 *
 * \section External dependencies
 * AJI use the IKSEMEL library found at http://iksemel.jabberstudio.org/
 *
 * \section Files
 * - res_jabber.c
 * - jabber.h
 * - chan_gtalk.c
 *
 */

#ifndef _ASTERISK_JABBER_H
#define _ASTERISK_JABBER_H

#ifdef HAVE_OPENSSL

#include <openssl/ssl.h>
#include <openssl/err.h>
#define TRY_SECURE 2
#define SECURE 4

#endif /* HAVE_OPENSSL */
/* file is read by blocks with this size */
#define NET_IO_BUF_SIZE 4096
/* Return value for timeout connection expiration */
#define IKS_NET_EXPIRED 12

#include <iksemel.h>
#include "asterisk/astobj.h"
#include "asterisk/linkedlists.h"

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
#define AJI_MAX_JIDLEN 3071
#define AJI_MAX_RESJIDLEN 1023

enum aji_state {
	AJI_DISCONNECTING,
	AJI_DISCONNECTED,
	AJI_CONNECTING,
	AJI_CONNECTED
};

enum {
	AJI_AUTOPRUNE = (1 << 0),
	AJI_AUTOREGISTER = (1 << 1)
};

enum aji_btype {
	AJI_USER=0,
	AJI_TRANS=1,
	AJI_UTRANS=2
};

struct aji_version {
	char version[50];
	int jingle;
	struct aji_capabilities *parent;
	struct aji_version *next;
};

struct aji_capabilities {
	char node[200];
	struct aji_version *versions;
	struct aji_capabilities *next;
};

struct aji_resource {
	int status;
	char resource[AJI_MAX_RESJIDLEN];
	char *description;
	struct aji_version *cap;
	int priority;
	struct aji_resource *next;
};

struct aji_message {
	char *from;
	char *message;
	char id[25];
	time_t arrived;
	AST_LIST_ENTRY(aji_message) list;
};

struct aji_buddy {
	ASTOBJ_COMPONENTS_FULL(struct aji_buddy, AJI_MAX_JIDLEN, 1);
	char channel[160];
	struct aji_resource *resources;
	enum aji_btype btype;
	struct ast_flags flags;
};

struct aji_buddy_container {
	ASTOBJ_CONTAINER_COMPONENTS(struct aji_buddy);
};

struct aji_transport_container {
	ASTOBJ_CONTAINER_COMPONENTS(struct aji_transport);
};

struct aji_client {
	ASTOBJ_COMPONENTS(struct aji_client);
	char password[160];
	char user[AJI_MAX_JIDLEN];
	char serverhost[AJI_MAX_RESJIDLEN];
	char statusmessage[256];
	char name_space[256];
	char sid[10]; /* Session ID */
	char mid[6]; /* Message ID */
	iksid *jid;
	iksparser *p;
	iksfilter *f;
	ikstack *stack;
#ifdef HAVE_OPENSSL
	SSL_CTX *ssl_context;
	SSL *ssl_session;
	SSL_METHOD *ssl_method;
	unsigned int stream_flags;
#endif /* HAVE_OPENSSL */
	enum aji_state state;
	int port;
	int debug;
	int usetls;
	int forcessl;
	int usesasl;
	int keepalive;
	int allowguest;
	int timeout;
	int message_timeout;
	int authorized;
	struct ast_flags flags;
	int component; /* 0 client,  1 component */
	struct aji_buddy_container buddies;
	AST_LIST_HEAD(messages,aji_message) messages;
	void *jingle;
	pthread_t thread;
	int priority;
	enum ikshowtype status;
};

struct aji_client_container{
	ASTOBJ_CONTAINER_COMPONENTS(struct aji_client);
};

/* !Send XML stanza over the established XMPP connection */
int ast_aji_send(struct aji_client *client, iks *x);
/*! Send jabber chat message from connected client to jabber URI */
int ast_aji_send_chat(struct aji_client *client, const char *address, const char *message);
/*! Disconnect jabber client */
int ast_aji_disconnect(struct aji_client *client);
int ast_aji_check_roster(void);
void ast_aji_increment_mid(char *mid);
/*! Open Chat session */
int ast_aji_create_chat(struct aji_client *client,char *room, char *server, char *topic);
/*! Invite to opened Chat session */
int ast_aji_invite_chat(struct aji_client *client, char *user, char *room, char *message);
/*! Join existing Chat session */
int ast_aji_join_chat(struct aji_client *client,char *room);
struct aji_client *ast_aji_get_client(const char *name);
struct aji_client_container *ast_aji_get_clients(void);

#endif
