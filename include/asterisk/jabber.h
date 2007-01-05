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

#ifndef _ASTERISK_JABBER_H
#define _ASTERISK_JABBER_H

#include <iksemel.h>
#include "asterisk/astobj.h"
#include "asterisk/linkedlists.h"

enum aji_state {
	AJI_DISCONNECTED = 0,
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
	char resource[80];
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
	ASTOBJ_COMPONENTS(struct aji_buddy);
	char channel[160];
	struct aji_resource *resources;
	enum aji_btype btype;
	unsigned int flags;
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
	char user[160];
	char serverhost[160];
	char context[100];
	char statusmessage[256];
	char sid[10]; /* Session ID */
	char mid[6]; /* Message ID */
	iksid *jid;
	iksparser *p;
	iksfilter *f;
	ikstack *stack;
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
	unsigned int flags;
	int component; /* 0 client,  1 component */
	struct aji_buddy_container buddies;
	AST_LIST_HEAD(messages,aji_message) messages;
	void *jingle;
	pthread_t thread;
};

struct aji_client_container{
	ASTOBJ_CONTAINER_COMPONENTS(struct aji_client);
};

int ast_aji_send(struct aji_client *client, const char *address, const char *message);
int ast_aji_disconnect(struct aji_client *client);
int ast_aji_check_roster(void);
void ast_aji_increment_mid(char *mid);
int ast_aji_create_chat(struct aji_client *client,char *room, char *server, char *topic);
int ast_aji_invite_chat(struct aji_client *client, char *user, char *room, char *message);
int ast_aji_join_chat(struct aji_client *client,char *room);
struct aji_client *ast_aji_get_client(const char *name);
struct aji_client_container *ast_aji_get_clients(void);

#endif
