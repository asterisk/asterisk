/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief A resource for interfacing asterisk directly as a client
 * or a component to a jabber compliant server.
 *
 * \todo If you unload this module, chan_gtalk/jingle will be dead. How do we handle that?
 * \todo If you have TLS, you can't unload this module. See bug #9738. This needs to be fixed,
 *       but the bug is in the unmantained Iksemel library
 *
 */

/*** MODULEINFO
	<depend>iksemel</depend>
	<use>gnutls</use>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <iksemel.h>

#include "asterisk/channel.h"
#include "asterisk/jabber.h"
#include "asterisk/file.h"
#include "asterisk/config.h"
#include "asterisk/callerid.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/md5.h"
#include "asterisk/acl.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/astobj.h"
#include "asterisk/astdb.h"
#include "asterisk/manager.h"

#define JABBER_CONFIG "jabber.conf"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/*-- Forward declarations */
static int aji_highest_bit(int number);
static void aji_buddy_destroy(struct aji_buddy *obj);
static void aji_client_destroy(struct aji_client *obj);
static int aji_send_exec(struct ast_channel *chan, void *data);
static int aji_status_exec(struct ast_channel *chan, void *data);
static void aji_log_hook(void *data, const char *xmpp, size_t size, int is_incoming);
static int aji_act_hook(void *data, int type, iks *node);
static void aji_handle_iq(struct aji_client *client, iks *node);
static void aji_handle_message(struct aji_client *client, ikspak *pak);
static void aji_handle_presence(struct aji_client *client, ikspak *pak);
static void aji_handle_subscribe(struct aji_client *client, ikspak *pak);
static void *aji_recv_loop(void *data);
static int aji_component_initialize(struct aji_client *client);
static int aji_client_initialize(struct aji_client *client);
static int aji_client_connect(void *data, ikspak *pak);
static void aji_set_presence(struct aji_client *client, char *to, char *from, int level, char *desc);
static int aji_do_debug(int fd, int argc, char *argv[]);
static int aji_do_reload(int fd, int argc, char *argv[]);
static int aji_no_debug(int fd, int argc, char *argv[]);
static int aji_test(int fd, int argc, char *argv[]);
static int aji_show_clients(int fd, int argc, char *argv[]);
static int aji_create_client(char *label, struct ast_variable *var, int debug);
static int aji_create_buddy(char *label, struct aji_client *client);
static int aji_reload(void);
static int aji_load_config(void);
static void aji_pruneregister(struct aji_client *client);
static int aji_filter_roster(void *data, ikspak *pak);
static int aji_get_roster(struct aji_client *client);
static int aji_client_info_handler(void *data, ikspak *pak);
static int aji_dinfo_handler(void *data, ikspak *pak);
static int aji_ditems_handler(void *data, ikspak *pak);
static int aji_register_query_handler(void *data, ikspak *pak);
static int aji_register_approve_handler(void *data, ikspak *pak);
static int aji_reconnect(struct aji_client *client);
static iks *jabber_make_auth(iksid * id, const char *pass, const char *sid);
/* No transports in this version */
/*
static int aji_create_transport(char *label, struct aji_client *client);
static int aji_register_transport(void *data, ikspak *pak);
static int aji_register_transport2(void *data, ikspak *pak);
*/

static char debug_usage[] = 
"Usage: jabber debug\n" 
"       Enables dumping of Jabber packets for debugging purposes.\n";

static char no_debug_usage[] = 
"Usage: jabber debug off\n" 
"       Disables dumping of Jabber packets for debugging purposes.\n";

static char reload_usage[] = 
"Usage: jabber reload\n" 
"       Enables reloading of Jabber module.\n";

static char test_usage[] = 
"Usage: jabber test [client]\n" 
"       Sends test message for debugging purposes.  A specific client\n"
"       as configured in jabber.conf can be optionally specified.\n";

static struct ast_cli_entry aji_cli[] = {
	{ { "jabber", "debug", NULL},
	aji_do_debug, "Enable Jabber debugging",
	debug_usage },

	{ { "jabber", "reload", NULL},
	aji_do_reload, "Reload Jabber configuration",
	reload_usage },

	{ { "jabber", "show", "connected", NULL},
	aji_show_clients, "Show state of clients and components",
	debug_usage },

	{ { "jabber", "debug", "off", NULL},
	aji_no_debug, "Disable Jabber debug",
	no_debug_usage },

	{ { "jabber", "test", NULL},
	aji_test, "Shows roster, but is generally used for mog's debugging.",
	test_usage },
};

static char *app_ajisend = "JabberSend";

static char *ajisend_synopsis = "JabberSend(jabber,screenname,message)";

static char *ajisend_descrip =
"JabberSend(Jabber,ScreenName,Message)\n"
"  Jabber - Client or transport Asterisk uses to connect to Jabber\n" 
"  ScreenName - User Name to message.\n" 
"  Message - Message to be sent to the buddy\n";

static char *app_ajistatus = "JabberStatus";

static char *ajistatus_synopsis = "JabberStatus(Jabber,ScreenName,Variable)";

static char *ajistatus_descrip =
"JabberStatus(Jabber,ScreenName,Variable)\n"
"  Jabber - Client or transport Asterisk uses to connect to Jabber\n"
"  ScreenName - User Name to retrieve status from.\n"
"  Variable - Variable to store presence in will be 1-6.\n" 
"             In order, Online, Chatty, Away, XAway, DND, Offline\n" 
"             If not in roster variable will = 7\n";

struct aji_client_container clients;
struct aji_capabilities *capabilities = NULL;

/*! \brief Global flags, initialized to default values */
static struct ast_flags globalflags = { AJI_AUTOPRUNE | AJI_AUTOREGISTER };
static int tls_initialized = FALSE;

/*!
 * \brief Deletes the aji_client data structure.
 * \param obj is the structure we will delete.
 * \return void.
 */
static void aji_client_destroy(struct aji_client *obj)
{
	struct aji_message *tmp;
	ASTOBJ_CONTAINER_DESTROYALL(&obj->buddies, aji_buddy_destroy);
	ASTOBJ_CONTAINER_DESTROY(&obj->buddies);
	iks_filter_delete(obj->f);
	iks_parser_delete(obj->p);
	iks_stack_delete(obj->stack);
	AST_LIST_LOCK(&obj->messages);
	while ((tmp = AST_LIST_REMOVE_HEAD(&obj->messages, list))) {
		if (tmp->from)
			free(tmp->from);
		if (tmp->message)
			free(tmp->message);
	}
	AST_LIST_HEAD_DESTROY(&obj->messages);
	free(obj);
}

/*!
 * \brief Deletes the aji_buddy data structure.
 * \param obj is the structure we will delete.
 * \return void.
 */
static void aji_buddy_destroy(struct aji_buddy *obj)
{
	struct aji_resource *tmp;

	while ((tmp = obj->resources)) {
		obj->resources = obj->resources->next;
		free(tmp->description);
		free(tmp);
	}

	free(obj);
}

/*!
 * \brief Find version in XML stream and populate our capabilities list
 * \param node the node attribute in the caps element we'll look for or add to 
 * our list
 * \param version the version attribute in the caps element we'll look for or 
 * add to our list
 * \param pak the XML stanza we're processing
 * \return a pointer to the added or found aji_version structure
 */ 
static struct aji_version *aji_find_version(char *node, char *version, ikspak *pak)
{
	struct aji_capabilities *list = NULL;
	struct aji_version *res = NULL;

	list = capabilities;

	if(!node)
		node = pak->from->full;
	if(!version)
		version = "none supplied.";
	while(list) {
		if(!strcasecmp(list->node, node)) {
			res = list->versions;
			while(res) {
				 if(!strcasecmp(res->version, version))
					 return res;
				 res = res->next;
			}
			/* Specified version not found. Let's add it to 
			   this node in our capabilities list */
			if(!res) {
				res = (struct aji_version *)malloc(sizeof(struct aji_version));
				if(!res) {
					ast_log(LOG_ERROR, "Out of memory!\n");
					return NULL;
				}
				res->jingle = 0;
				res->parent = list;
				ast_copy_string(res->version, version, sizeof(res->version));
				res->next = list->versions;
				list->versions = res;
				return res;
			}
		}
		list = list->next;
	}
	/* Specified node not found. Let's add it our capabilities list */
	if(!list) {
		list = (struct aji_capabilities *)malloc(sizeof(struct aji_capabilities));
		if(!list) {
			ast_log(LOG_ERROR, "Out of memory!\n");
			return NULL;
		}
		res = (struct aji_version *)malloc(sizeof(struct aji_version));
		if(!res) {
			ast_log(LOG_ERROR, "Out of memory!\n");
			ast_free(list);
			return NULL;
		}
		ast_copy_string(list->node, node, sizeof(list->node));
		ast_copy_string(res->version, version, sizeof(res->version));
		res->jingle = 0;
		res->parent = list;
		res->next = NULL;
		list->versions = res;
		list->next = capabilities;
		capabilities = list;
	}
	return res;
}

static struct aji_resource *aji_find_resource(struct aji_buddy *buddy, char *name)
{
	struct aji_resource *res = NULL;
	if (!buddy || !name)
		return res;
	res = buddy->resources;
	while (res) {
		if (!strcasecmp(res->resource, name)) {
			break;
		}
		res = res->next;
	}
	return res;
}

static int gtalk_yuck(iks *node)
{
	if (iks_find_with_attrib(node, "c", "node", "http://www.google.com/xmpp/client/caps"))
		return 1;
	return 0;
}

/*!
 * \brief Detects the highest bit in a number.
 * \param Number you want to have evaluated.
 * \return the highest power of 2 that can go into the number.
 */
static int aji_highest_bit(int number)
{
	int x = sizeof(number) * 8 - 1;
	if (!number)
		return 0;
	for (; x > 0; x--) {
		if (number & (1 << x))
			break;
	}
	return (1 << x);
}

static iks *jabber_make_auth(iksid * id, const char *pass, const char *sid)
{
	iks *x, *y;
	x = iks_new("iq");
	iks_insert_attrib(x, "type", "set");
	y = iks_insert(x, "query");
	iks_insert_attrib(y, "xmlns", IKS_NS_AUTH);
	iks_insert_cdata(iks_insert(y, "username"), id->user, 0);
	iks_insert_cdata(iks_insert(y, "resource"), id->resource, 0);
	if (sid) {
		char buf[41];
		char sidpass[100];
		snprintf(sidpass, sizeof(sidpass), "%s%s", sid, pass);
		ast_sha1_hash(buf, sidpass);
		iks_insert_cdata(iks_insert(y, "digest"), buf, 0);
	} else {
		iks_insert_cdata(iks_insert(y, "password"), pass, 0);
	}
	return x;
}

/*!
 * \brief Dial plan function status(). puts the status of watched user 
   into a channel variable.
 * \param channel, and username,watched user, status var
 * \return 0.
 */
static int aji_status_exec(struct ast_channel *chan, void *data)
{
	struct aji_client *client = NULL;
	struct aji_buddy *buddy = NULL;
	struct aji_resource *r = NULL;
	char *s = NULL, *sender = NULL, *jid = NULL, *screenname = NULL, *resource = NULL, *variable = NULL;
	int stat = 7;
	char status[2];

	if (!data) {
		ast_log(LOG_ERROR, "This application requires arguments.\n");
		return 0;
	}
	s = ast_strdupa(data);
	if (s) {
		sender = strsep(&s, "|");
		if (sender && (sender[0] != '\0')) {
			jid = strsep(&s, "|");
			if (jid && (jid[0] != '\0')) {
				variable = s;
			} else {
				ast_log(LOG_ERROR, "Bad arguments\n");
				return -1;
			}
		}
	}

	if(!strchr(jid, '/')) {
		resource = NULL;
	} else {
		screenname = strsep(&jid, "/");
		resource = jid;
	}
	client = ast_aji_get_client(sender);
	if (!client) {
		ast_log(LOG_WARNING, "Could not find sender connection: %s\n", sender);
		return -1;
	}
	if(!&client->buddies) {
		ast_log(LOG_WARNING, "No buddies for connection : %s\n", sender);
		return -1;
	}
	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, resource ? screenname: jid);
	if (!buddy) {
		ast_log(LOG_WARNING, "Could not find buddy in list : %s\n", resource ? screenname : jid);
		return -1;
	}
	r = aji_find_resource(buddy, resource);
	if(!r && buddy->resources) 
		r = buddy->resources;
	if(!r)
		ast_log(LOG_NOTICE, "Resource %s of buddy %s not found \n", resource, screenname);
	else
		stat = r->status;
	sprintf(status, "%d", stat);
	pbx_builtin_setvar_helper(chan, variable, status);
	return 0;
}

/*!
 * \brief Dial plan function to send a message.
 * \param channel, and data, data is sender, reciever, message.
 * \return 0.
 */
static int aji_send_exec(struct ast_channel *chan, void *data)
{
	struct aji_client *client = NULL;

	char *s = NULL, *sender = NULL, *recipient = NULL, *message = NULL;

	if (!data) {
		ast_log(LOG_ERROR, "This application requires arguments.\n");
		return 0;
	}
	s = ast_strdupa(data);
	if (s) {
		sender = strsep(&s, "|");
		if (sender && (sender[0] != '\0')) {
			recipient = strsep(&s, "|");
			if (recipient && (recipient[0] != '\0')) {
				message = s;
			} else {
				ast_log(LOG_ERROR, "Bad arguments: %s\n", (char *) data);
				return -1;
			}
		}
	}
	if (!(client = ast_aji_get_client(sender))) {
		ast_log(LOG_WARNING, "Could not find sender connection: %s\n", sender);
		return -1;
	}
	if (strchr(recipient, '@') && message)
		ast_aji_send(client, recipient, message);
	return 0;
}

/*!
 * \brief the debug loop.
 * \param aji_client structure, xml data as string, size of string, direction of packet, 1 for inbound 0 for outbound.
 */
static void aji_log_hook(void *data, const char *xmpp, size_t size, int is_incoming)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	manager_event(EVENT_FLAG_USER, "JabberEvent", "Account: %s\r\nPacket: %s\r\n", client->name, xmpp);

	if (client->debug) {
		if (is_incoming)
			ast_verbose("\nJABBER: %s INCOMING: %s\n", client->name, xmpp);
		else {
			if( strlen(xmpp) == 1) {
				if(option_debug > 2  && xmpp[0] == ' ')
				ast_verbose("\nJABBER: Keep alive packet\n");
			} else
				ast_verbose("\nJABBER: %s OUTGOING: %s\n", client->name, xmpp);
		}

	}
	ASTOBJ_UNREF(client, aji_client_destroy);
}

/*!
 * \brief The action hook parses the inbound packets, constantly running.
 * \param data aji client structure 
 * \param type type of packet 
 * \param node the actual packet.
 * \return IKS_OK or IKS_HOOK .
 */
static int aji_act_hook(void *data, int type, iks *node)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	ikspak *pak = NULL;
	iks *auth = NULL;

	if(!node) {
		ast_log(LOG_ERROR, "aji_act_hook was called with out a packet\n"); /* most likely cause type is IKS_NODE_ERROR lost connection */
		ASTOBJ_UNREF(client, aji_client_destroy);
		return IKS_HOOK;
	}

	if (client->state == AJI_DISCONNECTING) {
		ASTOBJ_UNREF(client, aji_client_destroy);
		return IKS_HOOK;
	}

	pak = iks_packet(node);

	if (!client->component) { /*client */
		switch (type) {
		case IKS_NODE_START:
			if (client->usetls && !iks_is_secure(client->p)) {
				if (iks_has_tls()) {
					iks_start_tls(client->p);
					tls_initialized = TRUE;
				} else
					ast_log(LOG_ERROR, "gnuTLS not installed. You need to recompile the Iksemel library with gnuTLS support\n");
				break;
			}
			if (!client->usesasl) {
				iks_filter_add_rule(client->f, aji_client_connect, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, client->mid, IKS_RULE_DONE);
				auth = jabber_make_auth(client->jid, client->password, iks_find_attrib(node, "id"));
				if (auth) {
					iks_insert_attrib(auth, "id", client->mid);
					iks_insert_attrib(auth, "to", client->jid->server);
					ast_aji_increment_mid(client->mid);
					iks_send(client->p, auth);
					iks_delete(auth);
				} else
					ast_log(LOG_ERROR, "Out of memory.\n");
			}
			break;

		case IKS_NODE_NORMAL:
			if (!strcmp("stream:features", iks_name(node))) {
				int features = 0;
				features = iks_stream_features(node);
				if (client->usesasl) {
					if (client->usetls && !iks_is_secure(client->p))
						break;
					if (client->authorized) {
						if (features & IKS_STREAM_BIND) {
							iks_filter_add_rule (client->f, aji_client_connect, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_DONE);
							auth = iks_make_resource_bind(client->jid);
							if (auth) {
								iks_insert_attrib(auth, "id", client->mid);
								ast_aji_increment_mid(client->mid);
								iks_send(client->p, auth);
								iks_delete(auth);
							} else {
								ast_log(LOG_ERROR, "Out of memory.\n");
								break;
							}
						}
						if (features & IKS_STREAM_SESSION) {
							iks_filter_add_rule (client->f, aji_client_connect, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, "auth", IKS_RULE_DONE);
							auth = iks_make_session();
							if (auth) {
								iks_insert_attrib(auth, "id", "auth");
								ast_aji_increment_mid(client->mid);
								iks_send(client->p, auth);
								iks_delete(auth);
							} else {
								ast_log(LOG_ERROR, "Out of memory.\n");
							}
						}
					} else {
						if (!client->jid->user) {
							ast_log(LOG_ERROR, "Malformed Jabber ID : %s (domain missing?)\n", client->jid->full);
							break;
						}
						features = aji_highest_bit(features);
						if (features == IKS_STREAM_SASL_MD5)
							iks_start_sasl(client->p, IKS_SASL_DIGEST_MD5, client->jid->user, client->password);
						else {
							if (features == IKS_STREAM_SASL_PLAIN) {
								iks *x = NULL;
								x = iks_new("auth");
								if (x) {
									int len = strlen(client->jid->user) + strlen(client->password) + 3;
									/* XXX Check return values XXX */
									char *s = ast_malloc(80 + len);
									char *base64 = ast_malloc(80 + len * 2);
									iks_insert_attrib(x, "xmlns", IKS_NS_XMPP_SASL);
									iks_insert_attrib(x, "mechanism", "PLAIN");
									sprintf(s, "%c%s%c%s", 0, client->jid->user, 0, client->password);
										
									/* exclude the NULL training byte from the base64 encoding operation
									   as some XMPP servers will refuse it.
									   The format for authentication is [authzid]\0authcid\0password
									   not [authzid]\0authcid\0password\0 */
									ast_base64encode(base64, (const unsigned char *) s, len - 1, len * 2);
									iks_insert_cdata(x, base64, 0);
									iks_send(client->p, x);
									iks_delete(x);
									if (base64)
										free(base64);
									if (s)
										free(s);
								} else {
									ast_log(LOG_ERROR, "Out of memory.\n");
								}
							}
						}
					}
				}
			} else if (!strcmp("failure", iks_name(node))) {
				ast_log(LOG_ERROR, "JABBER: encryption failure. possible bad password.\n");
			} else if (!strcmp("success", iks_name(node))) {
				client->authorized = 1;
				iks_send_header(client->p, client->jid->server);
			}
			break;
		case IKS_NODE_ERROR: 
				ast_log(LOG_ERROR, "JABBER: Node Error\n");
				ASTOBJ_UNREF(client, aji_client_destroy);
				return IKS_HOOK;
				break;
		case IKS_NODE_STOP: 
				ast_log(LOG_WARNING, "JABBER: Disconnected\n");
				ASTOBJ_UNREF(client, aji_client_destroy);
				return IKS_HOOK;
				break;
		}
	} else if (client->state != AJI_CONNECTED && client->component) {
		switch (type) {
		case IKS_NODE_START:
			if (client->state == AJI_DISCONNECTED) {
				char secret[160], shasum[320], *handshake;

				sprintf(secret, "%s%s", pak->id, client->password);
				ast_sha1_hash(shasum, secret);
				handshake = NULL;
				if (asprintf(&handshake, "<handshake>%s</handshake>", shasum) > 0) {
					iks_send_raw(client->p, handshake);
					free(handshake);
					handshake = NULL;
				}
				client->state = AJI_CONNECTING;
				if(iks_recv(client->p,1) == 2) /*XXX proper result for iksemel library on iks_recv of <handshake/> XXX*/
					client->state = AJI_CONNECTED;
				else
					ast_log(LOG_WARNING,"Jabber didn't seem to handshake, failed to authenicate.\n");
				break;
			}
			break;

		case IKS_NODE_NORMAL:
			break;

		case IKS_NODE_ERROR:
			ast_log(LOG_ERROR, "JABBER: Node Error\n");
			ASTOBJ_UNREF(client, aji_client_destroy);
			return IKS_HOOK;

		case IKS_NODE_STOP:
			ast_log(LOG_WARNING, "JABBER: Disconnected\n");
			ASTOBJ_UNREF(client, aji_client_destroy);
			return IKS_HOOK;
		}
	}

	switch (pak->type) {
	case IKS_PAK_NONE:
		if (option_debug)
			ast_log(LOG_DEBUG, "JABBER: I Don't know what to do with you NONE\n");
		break;
	case IKS_PAK_MESSAGE:
		aji_handle_message(client, pak);
		if (option_debug)
			ast_log(LOG_DEBUG, "JABBER: I Don't know what to do with you MESSAGE\n");
		break;
	case IKS_PAK_PRESENCE:
		aji_handle_presence(client, pak);
		if (option_debug)
			ast_log(LOG_DEBUG, "JABBER: I Do know how to handle presence!!\n");
		break;
	case IKS_PAK_S10N:
		aji_handle_subscribe(client, pak);
		if (option_debug)
			ast_log(LOG_DEBUG, "JABBER: I Dont know S10N subscribe!!\n");
		break;
	case IKS_PAK_IQ:
		if (option_debug)
			ast_log(LOG_DEBUG, "JABBER: I Dont have an IQ!!!\n");
		aji_handle_iq(client, node);
		break;
	default:
		if (option_debug)
			ast_log(LOG_DEBUG, "JABBER: I Dont know %i\n", pak->type);
		break;
	}
	
	iks_filter_packet(client->f, pak);

	if (node)
		iks_delete(node);

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_OK;
}

static int aji_register_approve_handler(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	iks *iq = NULL, *presence = NULL, *x = NULL;

	iq = iks_new("iq");
	presence = iks_new("presence");
	x = iks_new("x");
	if (client && iq && presence && x) {
		if (!iks_find(pak->query, "remove")) {
			iks_insert_attrib(iq, "from", client->jid->full);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_send(client->p, iq);

			iks_insert_attrib(presence, "from", client->jid->full);
			iks_insert_attrib(presence, "to", pak->from->partial);
			iks_insert_attrib(presence, "id", client->mid);
			ast_aji_increment_mid(client->mid);
			iks_insert_attrib(presence, "type", "subscribe");
			iks_insert_attrib(x, "xmlns", "vcard-temp:x:update");
			iks_insert_node(presence, x);
			iks_send(client->p, presence); 
		}
	} else {
		ast_log(LOG_ERROR, "Out of memory.\n");
	}

	if (iq)
		iks_delete(iq);
	if(presence)
		iks_delete(presence);
	if (x)
		iks_delete(x);
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

static int aji_register_query_handler(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	struct aji_buddy *buddy = NULL; 
	char *node = NULL;

	client = (struct aji_client *) data;

	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, pak->from->partial);
	if (!buddy) {
		iks *iq = NULL, *query = NULL, *error = NULL, *notacceptable = NULL;
		ast_verbose("Someone.... %s tried to register but they aren't allowed\n", pak->from->partial);
		iq = iks_new("iq");
		query = iks_new("query");
		error = iks_new("error");
		notacceptable = iks_new("not-acceptable");
		if(iq && query && error && notacceptable) {
			iks_insert_attrib(iq, "type", "error");
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(query, "xmlns", "jabber:iq:register");
			iks_insert_attrib(error, "code" , "406");
			iks_insert_attrib(error, "type", "modify");
			iks_insert_attrib(notacceptable, "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas");
			iks_insert_node(iq, query);
			iks_insert_node(iq, error);
			iks_insert_node(error, notacceptable);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (error)
			iks_delete(error);
		if (notacceptable)
			iks_delete(notacceptable);
	} else 	if (!(node = iks_find_attrib(pak->query, "node"))) {
		iks *iq = NULL, *query = NULL, *instructions = NULL;
		char *explain = "Welcome to Asterisk - the Open Source PBX.\n";
		iq = iks_new("iq");
		query = iks_new("query");
		instructions = iks_new("instructions");
		if (iq && query && instructions && client) {
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(query, "xmlns", "jabber:iq:register");
			iks_insert_cdata(instructions, explain, 0);
			iks_insert_node(iq, query);
			iks_insert_node(query, instructions);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (instructions)
			iks_delete(instructions);
	}
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

static int aji_ditems_handler(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	char *node = NULL;

	if (!(node = iks_find_attrib(pak->query, "node"))) {
		iks *iq = NULL, *query = NULL, *item = NULL;
		iq = iks_new("iq");
		query = iks_new("query");
		item = iks_new("item");

		if (iq && query && item) {
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#items");
			iks_insert_attrib(item, "node", "http://jabber.org/protocol/commands");
			iks_insert_attrib(item, "name", "Million Dollar Asterisk Commands");
			iks_insert_attrib(item, "jid", client->user);

			iks_insert_node(iq, query);
			iks_insert_node(query, item);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (item)
			iks_delete(item);

	} else if (!strcasecmp(node, "http://jabber.org/protocol/commands")) {
		iks *iq, *query, *confirm;
		iq = iks_new("iq");
		query = iks_new("query");
		confirm = iks_new("item");
		if (iq && query && confirm && client) {
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#items");
			iks_insert_attrib(query, "node", "http://jabber.org/protocol/commands");
			iks_insert_attrib(confirm, "node", "confirmaccount");
			iks_insert_attrib(confirm, "name", "Confirm AIM account");
			iks_insert_attrib(confirm, "jid", "blog.astjab.org");

			iks_insert_node(iq, query);
			iks_insert_node(query, confirm);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (confirm)
			iks_delete(confirm);

	} else if (!strcasecmp(node, "confirmaccount")) {
		iks *iq = NULL, *query = NULL, *feature = NULL;

		iq = iks_new("iq");
		query = iks_new("query");
		feature = iks_new("feature");

		if (iq && query && feature && client) {
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#items");
			iks_insert_attrib(feature, "var", "http://jabber.org/protocol/commands");
			iks_insert_node(iq, query);
			iks_insert_node(query, feature);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (feature)
			iks_delete(feature);
	}

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;

}

static int aji_client_info_handler(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	struct aji_resource *resource = NULL;
	struct aji_buddy *buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, pak->from->partial);

	resource = aji_find_resource(buddy, pak->from->resource);
	if (pak->subtype == IKS_TYPE_RESULT) {
		if (!resource) {
			ast_log(LOG_NOTICE,"JABBER: Received client info from %s when not requested.\n", pak->from->full);
			ASTOBJ_UNREF(client, aji_client_destroy);
			return IKS_FILTER_EAT;
		}
		if (iks_find_with_attrib(pak->query, "feature", "var", "http://www.google.com/xmpp/protocol/voice/v1")) {
			resource->cap->jingle = 1;
		} else
			resource->cap->jingle = 0;
	} else if (pak->subtype == IKS_TYPE_GET) {
		iks *iq, *disco, *ident, *google, *query;
		iq = iks_new("iq");
		query = iks_new("query");
		ident = iks_new("identity");
		disco = iks_new("feature");
		google = iks_new("feature");
		if (iq && ident && disco && google) {
			iks_insert_attrib(iq, "from", client->jid->full);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#info");
			iks_insert_attrib(ident, "category", "client");
			iks_insert_attrib(ident, "type", "pc");
			iks_insert_attrib(ident, "name", "asterisk");
			iks_insert_attrib(disco, "var", "http://jabber.org/protocol/disco#info");
			iks_insert_attrib(google, "var", "http://www.google.com/xmpp/protocol/voice/v1");
			iks_insert_node(iq, query);
			iks_insert_node(query, ident);
			iks_insert_node(query, google);
			iks_insert_node(query, disco);
			iks_send(client->p, iq);
		} else
			ast_log(LOG_ERROR, "Out of Memory.\n");
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (ident)
			iks_delete(ident);
		if (google)
			iks_delete(google);
		if (disco)
			iks_delete(disco);
	} else if (pak->subtype == IKS_TYPE_ERROR) {
		ast_log(LOG_NOTICE, "User %s does not support discovery.\n", pak->from->full);
	}
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

static int aji_dinfo_handler(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	char *node = NULL;
	struct aji_resource *resource = NULL;
	struct aji_buddy *buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, pak->from->partial);

	resource = aji_find_resource(buddy, pak->from->resource);
	if (pak->subtype == IKS_TYPE_ERROR) {
		ast_log(LOG_WARNING, "Recieved error from a client, turn on jabber debug!\n");
		return IKS_FILTER_EAT;
	}
	if (pak->subtype == IKS_TYPE_RESULT) {
		if (!resource) {
			ast_log(LOG_NOTICE,"JABBER: Received client info from %s when not requested.\n", pak->from->full);
			ASTOBJ_UNREF(client, aji_client_destroy);
			return IKS_FILTER_EAT;
		}
		if (iks_find_with_attrib(pak->query, "feature", "var", "http://www.google.com/xmpp/protocol/voice/v1")) {
			resource->cap->jingle = 1;
		} else
			resource->cap->jingle = 0;
	} else if (pak->subtype == IKS_TYPE_GET && !(node = iks_find_attrib(pak->query, "node"))) {
		iks *iq, *query, *identity, *disco, *reg, *commands, *gateway, *version, *vcard, *search;

		iq = iks_new("iq");
		query = iks_new("query");
		identity = iks_new("identity");
		disco = iks_new("feature");
		reg = iks_new("feature");
		commands = iks_new("feature");
		gateway = iks_new("feature");
		version = iks_new("feature");
		vcard = iks_new("feature");
		search = iks_new("feature");

		if (iq && query && identity && disco && reg && commands && gateway && version && vcard && search && client) {
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#info");
			iks_insert_attrib(identity, "category", "gateway");
			iks_insert_attrib(identity, "type", "pstn");
			iks_insert_attrib(identity, "name", "Asterisk The Open Source PBX");
			iks_insert_attrib(disco, "var", "http://jabber.org/protocol/disco");
			iks_insert_attrib(reg, "var", "jabber:iq:register");
			iks_insert_attrib(commands, "var", "http://jabber.org/protocol/commands");
			iks_insert_attrib(gateway, "var", "jabber:iq:gateway");
			iks_insert_attrib(version, "var", "jabber:iq:version");
			iks_insert_attrib(vcard, "var", "vcard-temp");
			iks_insert_attrib(search, "var", "jabber:iq:search");

			iks_insert_node(iq, query);
			iks_insert_node(query, identity);
			iks_insert_node(query, disco);
			iks_insert_node(query, reg);
			iks_insert_node(query, commands);
			iks_insert_node(query, gateway);
			iks_insert_node(query, version);
			iks_insert_node(query, vcard);
			iks_insert_node(query, search);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (identity)
			iks_delete(identity);
		if (disco)
			iks_delete(disco);
		if (reg)
			iks_delete(reg);
		if (commands)
			iks_delete(commands);
		if (gateway)
			iks_delete(gateway);
		if (version)
			iks_delete(version);
		if (vcard)
			iks_delete(vcard);
		if (search)
			iks_delete(search);

	} else if (pak->subtype == IKS_TYPE_GET && !strcasecmp(node, "http://jabber.org/protocol/commands")) {
		iks *iq, *query, *confirm;
		iq = iks_new("iq");
		query = iks_new("query");
		confirm = iks_new("item");

		if (iq && query && confirm && client) {
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#items");
			iks_insert_attrib(query, "node", "http://jabber.org/protocol/commands");
			iks_insert_attrib(confirm, "node", "confirmaccount");
			iks_insert_attrib(confirm, "name", "Confirm AIM account");
			iks_insert_attrib(confirm, "jid", client->user);
			iks_insert_node(iq, query);
			iks_insert_node(query, confirm);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (confirm)
			iks_delete(confirm);

	} else if (pak->subtype == IKS_TYPE_GET && !strcasecmp(node, "confirmaccount")) {
		iks *iq, *query, *feature;

		iq = iks_new("iq");
		query = iks_new("query");
		feature = iks_new("feature");

		if (iq && query && feature && client) {
			iks_insert_attrib(iq, "from", client->user);
			iks_insert_attrib(iq, "to", pak->from->full);
			iks_insert_attrib(iq, "id", pak->id);
			iks_insert_attrib(iq, "type", "result");
			iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#info");
			iks_insert_attrib(feature, "var", "http://jabber.org/protocol/commands");
			iks_insert_node(iq, query);
			iks_insert_node(query, feature);
			iks_send(client->p, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (iq)
			iks_delete(iq);
		if (query)
			iks_delete(query);
		if (feature)
			iks_delete(feature);
	}

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

/*!
 * \brief Handles <iq> tags.
 * \param client structure and the iq node.
 * \return void.
 */
static void aji_handle_iq(struct aji_client *client, iks *node)
{
	/*Nothing to see here */
}

/*!
 * \brief Handles presence packets.
 * \param client structure and the node.
 * \return void.
 */
static void aji_handle_message(struct aji_client *client, ikspak *pak)
{
	struct aji_message *insert, *tmp;
	int flag = 0;
	
	if (!(insert = ast_calloc(1, sizeof(struct aji_message))))
		return;
	time(&insert->arrived);
	if (iks_find_cdata(pak->x, "body"))
		insert->message = ast_strdup(iks_find_cdata(pak->x, "body"));
	if(pak->id)
		ast_copy_string(insert->id, pak->id, sizeof(insert->message));
	if (pak->from)
		insert->from = ast_strdup(pak->from->full);
	AST_LIST_LOCK(&client->messages);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&client->messages, tmp, list) {
		if (flag) {
			AST_LIST_REMOVE_CURRENT(&client->messages, list);
			if (tmp->from)
				free(tmp->from);
			if (tmp->message)
				free(tmp->message);
		} else if (difftime(time(NULL), tmp->arrived) >= client->message_timeout) {
			flag = 1;
			AST_LIST_REMOVE_CURRENT(&client->messages, list);
			if (tmp->from)
				free(tmp->from);
			if (tmp->message)
				free(tmp->message);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_INSERT_HEAD(&client->messages, insert, list);
	AST_LIST_UNLOCK(&client->messages);
}

static void aji_handle_presence(struct aji_client *client, ikspak *pak)
{
	int status, priority;
	struct aji_buddy *buddy;
	struct aji_resource *tmp = NULL, *last = NULL, *found = NULL;
	char *ver, *node, *descrip, *type;
	
	if(client->state != AJI_CONNECTED)
		aji_create_buddy(pak->from->partial, client);

	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, pak->from->partial);
	if (!buddy && pak->from->partial) {
		/* allow our jid to be used to log in with another resource */
		if (!strcmp((const char *)pak->from->partial, (const char *)client->jid->partial))
			aji_create_buddy(pak->from->partial, client);
		else
			ast_log(LOG_NOTICE, "Got presence packet from %s, someone not in our roster!!!!\n", pak->from->partial);
		return;
	}
	type = iks_find_attrib(pak->x, "type");
	if(client->component && type &&!strcasecmp("probe", type)) {
		aji_set_presence(client, pak->from->full, iks_find_attrib(pak->x, "to"), 1, client->statusmessage);
		ast_verbose("what i was looking for \n");
	}
	ASTOBJ_WRLOCK(buddy);
	status = (pak->show) ? pak->show : 6;
	priority = atoi((iks_find_cdata(pak->x, "priority")) ? iks_find_cdata(pak->x, "priority") : "0");
	tmp = buddy->resources;
	descrip = ast_strdup(iks_find_cdata(pak->x,"status"));

	while (tmp && pak->from->resource) {
		if (!strcasecmp(tmp->resource, pak->from->resource)) {
			tmp->status = status;
			if (tmp->description) free(tmp->description);
			tmp->description = descrip;
			found = tmp;
			if (status == 6) {	/* Sign off Destroy resource */
				if (last && found->next) {
					last->next = found->next;
				} else if (!last) {
					if (found->next)
						buddy->resources = found->next;
					else
						buddy->resources = NULL;
				} else if (!found->next) {
					if (last)
						last->next = NULL;
					else
						buddy->resources = NULL;
				}
				free(found);
				found = NULL;
				break;
			}
			/* resource list is sorted by descending priority */
			if (tmp->priority != priority) {
				found->priority = priority;
				if (!last && !found->next)
					/* resource was found to be unique,
					   leave loop */
					break;
				/* search for resource in our list
				   and take it out for the moment */
				if (last)
					last->next = found->next;
				else
					buddy->resources = found->next;

				last = NULL;
				tmp = buddy->resources;
				if (!buddy->resources)
					buddy->resources = found;
				/* priority processing */
				while (tmp) {
					/* insert resource back according to 
					   its priority value */
					if (found->priority > tmp->priority) {
						if (last)
							/* insert within list */
							last->next = found;
						found->next = tmp;
						if (!last)
							/* insert on top */
							buddy->resources = found;
						break;
					}
					if (!tmp->next) {
						/* insert at the end of the list */
						tmp->next = found;
						found->next = NULL;
						break;
					}
					last = tmp;
					tmp = tmp->next;
				}
			}
			break;
		}
		last = tmp;
		tmp = tmp->next;
	}

	/* resource not found in our list, create it */
	if (!found && status != 6 && pak->from->resource) {
		found = (struct aji_resource *) malloc(sizeof(struct aji_resource));
		memset(found, 0, sizeof(struct aji_resource));

		if (!found) {
			ast_log(LOG_ERROR, "Out of memory!\n");
			return;
		}
		ast_copy_string(found->resource, pak->from->resource, sizeof(found->resource));
		found->status = status;
		found->description = descrip;
		found->priority = priority;
		found->next = NULL;
		last = NULL;
		tmp = buddy->resources;
		while (tmp) {
			if (found->priority > tmp->priority) {
				if (last)
					last->next = found;
				found->next = tmp;
				if (!last)
					buddy->resources = found;
				break;
			}
			if (!tmp->next) {
				tmp->next = found;
				break;
			}
			last = tmp;
			tmp = tmp->next;
		}
		if (!tmp)
			buddy->resources = found;
	}
	
	ASTOBJ_UNLOCK(buddy);
	ASTOBJ_UNREF(buddy, aji_buddy_destroy);

	node = iks_find_attrib(iks_find(pak->x, "c"), "node");
	ver = iks_find_attrib(iks_find(pak->x, "c"), "ver");

	/* handle gmail client's special caps:c tag */
	if (!node && !ver) {
		node = iks_find_attrib(iks_find(pak->x, "caps:c"), "node");
		ver = iks_find_attrib(iks_find(pak->x, "caps:c"), "ver");
	}

	/* retrieve capabilites of the new resource */
	if(status !=6 && found && !found->cap) {
		found->cap = aji_find_version(node, ver, pak);
		if(gtalk_yuck(pak->x)) /* gtalk should do discover */
			found->cap->jingle = 1;
		if(found->cap->jingle && option_debug > 4)
			ast_log(LOG_DEBUG,"Special case for google till they support discover.\n");
		else {
			iks *iq, *query;
			iq = iks_new("iq");
			query = iks_new("query");
			if(query && iq)  {
				iks_insert_attrib(iq, "type", "get");
				iks_insert_attrib(iq, "to", pak->from->full);
				iks_insert_attrib(iq,"from", client->jid->full);
				iks_insert_attrib(iq, "id", client->mid);
				ast_aji_increment_mid(client->mid);
				iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#info");
				iks_insert_node(iq, query);
				iks_send(client->p, iq);
				
			} else
				ast_log(LOG_ERROR, "Out of memory.\n");
			if(query)
				iks_delete(query);
			if(iq)
				iks_delete(iq);
		}
	}
	if (option_verbose > 4) {
		switch (pak->subtype) {
		case IKS_TYPE_AVAILABLE:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: I am available ^_* %i\n", pak->subtype);
			break;
		case IKS_TYPE_UNAVAILABLE:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: I am unavailable ^_* %i\n", pak->subtype);
			break;
		default:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: Ohh sexy and the wrong type: %i\n", pak->subtype);
		}
		switch (pak->show) {
		case IKS_SHOW_UNAVAILABLE:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
			break;
		case IKS_SHOW_AVAILABLE:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: type is available\n");
			break;
		case IKS_SHOW_CHAT:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
			break;
		case IKS_SHOW_AWAY:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: type is away\n");
			break;
		case IKS_SHOW_XA:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
			break;
		case IKS_SHOW_DND:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
			break;
		default:
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: Kinky! how did that happen %i\n", pak->show);
		}
	}
}

/*!
 * \brief handles subscription requests.
 * \param aji_client struct and xml packet.
 * \return void.
 */
static void aji_handle_subscribe(struct aji_client *client, ikspak *pak)
{
	iks *presence = NULL, *status = NULL;
	struct aji_buddy* buddy = NULL;

	switch (pak->subtype) { 
	case IKS_TYPE_SUBSCRIBE:
		presence = iks_new("presence");
		status = iks_new("status");
		if(presence && status) {
			iks_insert_attrib(presence, "type", "subscribed");
			iks_insert_attrib(presence, "to", pak->from->full);
			iks_insert_attrib(presence, "from", client->jid->full);
			if(pak->id)
				iks_insert_attrib(presence, "id", pak->id);
			iks_insert_cdata(status, "Asterisk has approved subscription", 0);
			iks_insert_node(presence, status);
			iks_send(client->p, presence);
		} else
			ast_log(LOG_ERROR, "Unable to allocate nodes\n");
		if(presence)
			iks_delete(presence);
		if(status)
			iks_delete(status);
		if(client->component)
			aji_set_presence(client, pak->from->full, iks_find_attrib(pak->x, "to"), 1, client->statusmessage);
	case IKS_TYPE_SUBSCRIBED:
		buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, pak->from->partial);
		if (!buddy && pak->from->partial) {
			aji_create_buddy(pak->from->partial, client);
		}
	default:
		if (option_verbose > 4) {
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: This is a subcription of type %i\n", pak->subtype);
		}
	}
}

/*!
 * \brief sends messages.
 * \param aji_client struct , reciever, message.
 * \return 1.
 */
int ast_aji_send(struct aji_client *client, const char *address, const char *message)
{
	int res = 0;
	iks *message_packet = NULL;
	if (client->state == AJI_CONNECTED) {
		message_packet = iks_make_msg(IKS_TYPE_CHAT, address, message);
		if (message_packet) {
			iks_insert_attrib(message_packet, "from", client->jid->full);
			res = iks_send(client->p, message_packet);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}
		if (message_packet)
			iks_delete(message_packet);
	} else
		ast_log(LOG_WARNING, "JABBER: Not connected can't send\n");
	return 1;
}

/*!
 * \brief create a chatroom.
 * \param aji_client struct , room, server, topic for the room.
 * \return 0.
 */
int ast_aji_create_chat(struct aji_client *client, char *room, char *server, char *topic)
{
	int res = 0;
	iks *iq = NULL;
	iq = iks_new("iq");

	if (iq && client) {
		iks_insert_attrib(iq, "type", "get");
		iks_insert_attrib(iq, "to", server);
		iks_insert_attrib(iq, "id", client->mid);
		ast_aji_increment_mid(client->mid);
		iks_send(client->p, iq);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");

	iks_delete(iq);

	return res;
}

/*!
 * \brief join a chatroom.
 * \param aji_client struct , room.
 * \return res.
 */
int ast_aji_join_chat(struct aji_client *client, char *room)
{
	int res = 0;
	iks *presence = NULL, *priority = NULL;
	presence = iks_new("presence");
	priority = iks_new("priority");
	if (presence && priority && client) {
		iks_insert_cdata(priority, "0", 1);
		iks_insert_attrib(presence, "to", room);
		iks_insert_node(presence, priority);
		res = iks_send(client->p, presence);
		iks_insert_cdata(priority, "5", 1);
		iks_insert_attrib(presence, "to", room);
		res = iks_send(client->p, presence);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");
	if (presence)
		iks_delete(presence);
	if (priority)
		iks_delete(priority);
	return res;
}

/*!
 * \brief invite to a chatroom.
 * \param aji_client struct ,user, room, message.
 * \return res.
 */
int ast_aji_invite_chat(struct aji_client *client, char *user, char *room, char *message)
{
	int res = 0;
	iks *invite, *body, *namespace;

	invite = iks_new("message");
	body = iks_new("body");
	namespace = iks_new("x");
	if (client && invite && body && namespace) {
		iks_insert_attrib(invite, "to", user);
		iks_insert_attrib(invite, "id", client->mid);
		ast_aji_increment_mid(client->mid);
		iks_insert_cdata(body, message, 0);
		iks_insert_attrib(namespace, "xmlns", "jabber:x:conference");
		iks_insert_attrib(namespace, "jid", room);
		iks_insert_node(invite, body);
		iks_insert_node(invite, namespace);
		res = iks_send(client->p, invite);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");
	if (body)
		iks_delete(body);
	if (namespace)
		iks_delete(namespace);
	if (invite)
		iks_delete(invite);
	return res;
}


/*!
 * \brief receive message loop.
 * \param aji_client struct.
 * \return void.
 */
static void *aji_recv_loop(void *data)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	int res = IKS_HOOK;
	do {
		if (res != IKS_OK) {
			while(res != IKS_OK) {
				if(option_verbose > 3)
					ast_verbose("JABBER: reconnecting.\n");
				res = aji_reconnect(client);
				sleep(4);
			}
		}

		res = iks_recv(client->p, 1);

		if (client->state == AJI_DISCONNECTING) {
			if (option_debug > 1)
				ast_log(LOG_DEBUG, "Ending our Jabber client's thread due to a disconnect\n");
			pthread_exit(NULL);
		}
		client->timeout--;
		if (res == IKS_HOOK) 
			ast_log(LOG_WARNING, "JABBER: Got hook event.\n");
		else if (res == IKS_NET_TLSFAIL)
			ast_log(LOG_WARNING, "JABBER:  Failure in TLS.\n");
		else if (client->timeout == 0 && client->state == AJI_CONNECTED) {
			res = client->keepalive ? iks_send_raw(client->p, " ") : IKS_OK;
			if(res == IKS_OK)
				client->timeout = 50;
			else
				ast_log(LOG_WARNING, "JABBER:  Network Timeout\n");
		} else if (res == IKS_NET_RWERR)
			ast_log(LOG_WARNING, "JABBER: socket read error\n");
	} while (client);
	ASTOBJ_UNREF(client, aji_client_destroy);
	return 0;
}

/*!
 * \brief increments the mid field for messages and other events.
 * \param message id.
 * \return void.
 */
void ast_aji_increment_mid(char *mid)
{
	int i = 0;

	for (i = strlen(mid) - 1; i >= 0; i--) {
		if (mid[i] != 'z') {
			mid[i] = mid[i] + 1;
			i = 0;
		} else
			mid[i] = 'a';
	}
}


/*!
 * \brief attempts to register to a transport.
 * \param aji_client struct, and xml packet.
 * \return IKS_FILTER_EAT.
 */
/*allows for registering to transport , was too sketch and is out for now. */
/*static int aji_register_transport(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	int res = 0;
	struct aji_buddy *buddy = NULL;
	iks *send = iks_make_iq(IKS_TYPE_GET, "jabber:iq:register");

	if (client && send) {
		ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
			ASTOBJ_RDLOCK(iterator); 
			if (iterator->btype == AJI_TRANS) {
				  buddy = iterator;
			}
			ASTOBJ_UNLOCK(iterator);
		});
		iks_filter_remove_hook(client->f, aji_register_transport);
		iks_filter_add_rule(client->f, aji_register_transport2, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_NS, IKS_NS_REGISTER, IKS_RULE_DONE);
		iks_insert_attrib(send, "to", buddy->host);
		iks_insert_attrib(send, "id", client->mid);
		ast_aji_increment_mid(client->mid);
		iks_insert_attrib(send, "from", client->user);
		res = iks_send(client->p, send);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");

	if (send)
		iks_delete(send);
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;

}
*/
/*!
 * \brief attempts to register to a transport step 2.
 * \param aji_client struct, and xml packet.
 * \return IKS_FILTER_EAT.
 */
/* more of the same blob of code, too wonky for now*/
/* static int aji_register_transport2(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	int res = 0;
	struct aji_buddy *buddy = NULL;

	iks *regiq = iks_new("iq");
	iks *regquery = iks_new("query");
	iks *reguser = iks_new("username");
	iks *regpass = iks_new("password");

	if (client && regquery && reguser && regpass && regiq) {
		ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
			ASTOBJ_RDLOCK(iterator);
			if (iterator->btype == AJI_TRANS)
				buddy = iterator; ASTOBJ_UNLOCK(iterator);
		});
		iks_filter_remove_hook(client->f, aji_register_transport2);
		iks_insert_attrib(regiq, "to", buddy->host);
		iks_insert_attrib(regiq, "type", "set");
		iks_insert_attrib(regiq, "id", client->mid);
		ast_aji_increment_mid(client->mid);
		iks_insert_attrib(regiq, "from", client->user);
		iks_insert_attrib(regquery, "xmlns", "jabber:iq:register");
		iks_insert_cdata(reguser, buddy->user, 0);
		iks_insert_cdata(regpass, buddy->pass, 0);
		iks_insert_node(regiq, regquery);
		iks_insert_node(regquery, reguser);
		iks_insert_node(regquery, regpass);
		res = iks_send(client->p, regiq);
	} else
		ast_log(LOG_ERROR, "Out of memory.\n");
	if (regiq)
		iks_delete(regiq);
	if (regquery)
		iks_delete(regquery);
	if (reguser)
		iks_delete(reguser);
	if (regpass)
		iks_delete(regpass);
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}
*/
/*!
 * \brief goes through roster and prunes users not needed in list, or adds them accordingly.
 * \param aji_client struct.
 * \return void.
 */
static void aji_pruneregister(struct aji_client *client)
{
	int res = 0;
	iks *removeiq = iks_new("iq");
	iks *removequery = iks_new("query");
	iks *removeitem = iks_new("item");
	iks *send = iks_make_iq(IKS_TYPE_GET, "http://jabber.org/protocol/disco#items");

	if (client && removeiq && removequery && removeitem && send) {
		iks_insert_node(removeiq, removequery);
		iks_insert_node(removequery, removeitem);
		ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
			ASTOBJ_RDLOCK(iterator);
			/* For an aji_buddy, both AUTOPRUNE and AUTOREGISTER will never
			 * be called at the same time */
			if (ast_test_flag(iterator, AJI_AUTOPRUNE)) {
				res = iks_send(client->p, iks_make_s10n(IKS_TYPE_UNSUBSCRIBE, iterator->name,
						"GoodBye your status is no longer needed by Asterisk the Open Source PBX"
						" so I am no longer subscribing to your presence.\n"));
				res = iks_send(client->p, iks_make_s10n(IKS_TYPE_UNSUBSCRIBED, iterator->name,
						"GoodBye you are no longer in the asterisk config file so I am removing"
						" your access to my presence.\n"));
				iks_insert_attrib(removeiq, "from", client->jid->full); 
				iks_insert_attrib(removeiq, "type", "set"); 
				iks_insert_attrib(removequery, "xmlns", "jabber:iq:roster");
				iks_insert_attrib(removeitem, "jid", iterator->name);
				iks_insert_attrib(removeitem, "subscription", "remove");
				res = iks_send(client->p, removeiq);
			} else if (ast_test_flag(iterator, AJI_AUTOREGISTER)) {
				res = iks_send(client->p, iks_make_s10n(IKS_TYPE_SUBSCRIBE, iterator->name, 
						"Greetings I am the Asterisk Open Source PBX and I want to subscribe to your presence\n"));
				ast_clear_flag(iterator, AJI_AUTOREGISTER);
			}
			ASTOBJ_UNLOCK(iterator);
		});
	} else
		ast_log(LOG_ERROR, "Out of memory.\n");
	if (removeiq)
		iks_delete(removeiq);
	if (removequery)
		iks_delete(removequery);
	if (removeitem)
		iks_delete(removeitem);
	if (send)
		iks_delete(send);
	ASTOBJ_CONTAINER_PRUNE_MARKED(&client->buddies, aji_buddy_destroy);
}

/*!
 * \brief filters the roster packet we get back from server.
 * \param aji_client struct, and xml packet.
 * \return IKS_FILTER_EAT.
 */
static int aji_filter_roster(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	int flag = 0;
	iks *x = NULL;
	struct aji_buddy *buddy;
	
	client->state = AJI_CONNECTED;
	ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
		ASTOBJ_RDLOCK(iterator);
		x = iks_child(pak->query);
		flag = 0;
		while (x) {
			if (!iks_strcmp(iks_name(x), "item")) {
				if (!strcasecmp(iterator->name, iks_find_attrib(x, "jid"))) {
					flag = 1;
					ast_clear_flag(iterator, AJI_AUTOPRUNE | AJI_AUTOREGISTER);
				}
			}
			x = iks_next(x);
		}
		if (!flag)
			ast_copy_flags(iterator, client, AJI_AUTOREGISTER);
		if (x)
			iks_delete(x);
		ASTOBJ_UNLOCK(iterator);
	});

	x = iks_child(pak->query);
	while (x) {
		flag = 0;
		if (iks_strcmp(iks_name(x), "item") == 0) {
			ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
				ASTOBJ_RDLOCK(iterator);
				if (!strcasecmp(iterator->name, iks_find_attrib(x, "jid")))
					flag = 1;
				ASTOBJ_UNLOCK(iterator);
			});

			if (!flag) {
				buddy = (struct aji_buddy *) malloc(sizeof(struct aji_buddy));
				if (!buddy) {
					ast_log(LOG_WARNING, "Out of memory\n");
					return 0;
				}
				memset(buddy, 0, sizeof(struct aji_buddy));
				ASTOBJ_INIT(buddy);
				ASTOBJ_WRLOCK(buddy);
				ast_copy_string(buddy->name, iks_find_attrib(x, "jid"), sizeof(buddy->name));
				ast_clear_flag(buddy, AST_FLAGS_ALL);
				if(ast_test_flag(client, AJI_AUTOPRUNE)) {
					ast_set_flag(buddy, AJI_AUTOPRUNE);
					buddy->objflags |= ASTOBJ_FLAG_MARKED;
				} else
					ast_set_flag(buddy, AJI_AUTOREGISTER);
				ASTOBJ_UNLOCK(buddy);
				if (buddy) {
					ASTOBJ_CONTAINER_LINK(&client->buddies, buddy);
					ASTOBJ_UNREF(buddy, aji_buddy_destroy);
				}
			}
		}
		x = iks_next(x);
	}
	if (x)
		iks_delete(x);
	aji_pruneregister(client);

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

static int aji_reconnect(struct aji_client *client)
{
	int res = 0;

	if (client->state)
		client->state = AJI_DISCONNECTED;
	client->timeout=50;
	if (client->p)
		iks_parser_reset(client->p);
	if (client->authorized)
		client->authorized = 0;

	if(client->component)
		res = aji_component_initialize(client);
	else
		res = aji_client_initialize(client);

	return res;
}

static int aji_get_roster(struct aji_client *client)
{
	iks *roster = NULL;
	roster = iks_make_iq(IKS_TYPE_GET, IKS_NS_ROSTER);
	if(roster) {
		iks_insert_attrib(roster, "id", "roster");
		aji_set_presence(client, NULL, client->jid->full, 1, client->statusmessage);
		iks_send(client->p, roster);
	}
	if (roster)
		iks_delete(roster);
	return 1;
}

/*!
 * \brief connects as a client to jabber server.
 * \param aji_client struct, and xml packet.
 * \return res.
 */
static int aji_client_connect(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	int res = 0;

	if (client) {
		if (client->state == AJI_DISCONNECTED) {
			iks_filter_add_rule(client->f, aji_filter_roster, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, "roster", IKS_RULE_DONE);
			client->state = AJI_CONNECTING;
			client->jid = (iks_find_cdata(pak->query, "jid")) ? iks_id_new(client->stack, iks_find_cdata(pak->query, "jid")) : client->jid;
			iks_filter_remove_hook(client->f, aji_client_connect);
			if(!client->component) /*client*/
				aji_get_roster(client);
		}
	} else
		ast_log(LOG_ERROR, "Out of memory.\n");

	ASTOBJ_UNREF(client, aji_client_destroy);
	return res;
}

/*!
 * \brief prepares client for connect.
 * \param aji_client struct.
 * \return 1.
 */
static int aji_client_initialize(struct aji_client *client)
{
	int connected = 0;

	connected = iks_connect_via(client->p, S_OR(client->serverhost, client->jid->server), client->port, client->jid->server);

	if (connected == IKS_NET_NOCONN) {
		ast_log(LOG_ERROR, "JABBER ERROR: No Connection\n");
		return IKS_HOOK;
	} else 	if (connected == IKS_NET_NODNS) {
		ast_log(LOG_ERROR, "JABBER ERROR: No DNS %s for client to  %s\n", client->name, S_OR(client->serverhost, client->jid->server));
		return IKS_HOOK;
	} else
		iks_recv(client->p, 30);
	return IKS_OK;
}

/*!
 * \brief prepares component for connect.
 * \param aji_client struct.
 * \return 1.
 */
static int aji_component_initialize(struct aji_client *client)
{
	int connected = 1;

	connected = iks_connect_via(client->p, S_OR(client->serverhost, client->jid->server), client->port, client->user);
	if (connected == IKS_NET_NOCONN) {
		ast_log(LOG_ERROR, "JABBER ERROR: No Connection\n");
		return IKS_HOOK;
	} else if (connected == IKS_NET_NODNS) {
		ast_log(LOG_ERROR, "JABBER ERROR: No DNS %s for client to  %s\n", client->name, S_OR(client->serverhost, client->jid->server));
		return IKS_HOOK;
	} else if (!connected) 
		iks_recv(client->p, 30);
	return IKS_OK;
}

/*!
 * \brief disconnect from jabber server.
 * \param aji_client struct.
 * \return 1.
 */
int ast_aji_disconnect(struct aji_client *client)
{
	if (client) {
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_3 "JABBER: Disconnecting\n");
		iks_disconnect(client->p);
		iks_parser_delete(client->p);
		ASTOBJ_UNREF(client, aji_client_destroy);
	}

	return 1;
}

/*!
 * \brief set presence of client.
 * \param aji_client struct, user to send it to, and from, level, description.
 * \return void.
 */
static void aji_set_presence(struct aji_client *client, char *to, char *from, int level, char *desc)
{
	int res = 0;
	iks *presence = iks_make_pres(level, desc);
	iks *cnode = iks_new("c");
	iks *priority = iks_new("priority");

	iks_insert_cdata(priority, "0", 1);
	if (presence && cnode && client) {
		if(to)
			iks_insert_attrib(presence, "to", to);
		if(from)
			iks_insert_attrib(presence, "from", from);
		iks_insert_attrib(cnode, "node", "http://www.asterisk.org/xmpp/client/caps");
		iks_insert_attrib(cnode, "ver", "asterisk-xmpp");
		iks_insert_attrib(cnode, "ext", "voice-v1");
		iks_insert_attrib(cnode, "xmlns", "http://jabber.org/protocol/caps");
		iks_insert_node(presence, cnode);
		res = iks_send(client->p, presence);
	} else
		ast_log(LOG_ERROR, "Out of memory.\n");
	if (cnode)
		iks_delete(cnode);
	if (presence)
		iks_delete(presence);
}

/*!
 * \brief turnon console debugging.
 * \param fd, number of args, args.
 * \return RESULT_SUCCESS.
 */
static int aji_do_debug(int fd, int argc, char *argv[])
{
	ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
		ASTOBJ_RDLOCK(iterator); 
		iterator->debug = 1;
		ASTOBJ_UNLOCK(iterator);
	});
	ast_cli(fd, "Jabber Debugging Enabled.\n");
	return RESULT_SUCCESS;
}

/*!
 * \brief reload jabber module.
 * \param fd, number of args, args.
 * \return RESULT_SUCCESS.
 */
static int aji_do_reload(int fd, int argc, char *argv[])
{
	aji_reload();
	ast_cli(fd, "Jabber Reloaded.\n");
	return RESULT_SUCCESS;
}

/*!
 * \brief turnoff console debugging.
 * \param fd, number of args, args.
 * \return RESULT_SUCCESS.
 */
static int aji_no_debug(int fd, int argc, char *argv[])
{
	ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
		ASTOBJ_RDLOCK(iterator);
		iterator->debug = 0;
		ASTOBJ_UNLOCK(iterator);
	});
	ast_cli(fd, "Jabber Debugging Disabled.\n");
	return RESULT_SUCCESS;
}

/*!
 * \brief show client status.
 * \param fd, number of args, args.
 * \return RESULT_SUCCESS.
 */
static int aji_show_clients(int fd, int argc, char *argv[])
{
	char *status;
	int count = 0;
	ast_cli(fd, "Jabber Users and their status:\n");
	ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
		ASTOBJ_RDLOCK(iterator);
		count++;
		switch (iterator->state) {
		case AJI_DISCONNECTED:
			status = "Disconnected";
			break;
		case AJI_CONNECTING:
			status = "Connecting";
			break;
		case AJI_CONNECTED:
			status = "Connected";
			break;
		default:
			status = "Unknown";
		}
		ast_cli(fd, "       User: %s     - %s\n", iterator->user, status);
		ASTOBJ_UNLOCK(iterator);
	});
	ast_cli(fd, "----\n");
	ast_cli(fd, "   Number of users: %d\n", count);
	return RESULT_SUCCESS;
}

/*!
 * \brief send test message for debugging.
 * \param fd, number of args, args.
 * \return RESULT_SUCCESS.
 */
static int aji_test(int fd, int argc, char *argv[])
{
	struct aji_client *client;
	struct aji_resource *resource;
	const char *name = "asterisk";
	struct aji_message *tmp;

	if (argc > 3)
		return RESULT_SHOWUSAGE;
	else if (argc == 3)
		name = argv[2];

	if (!(client = ASTOBJ_CONTAINER_FIND(&clients, name))) {
		ast_cli(fd, "Unable to find client '%s'!\n", name);
		return RESULT_FAILURE;
	}

	/* XXX Does Matt really want everyone to use his personal address for tests? */ /* XXX yes he does */
	ast_aji_send(client, "mogorman@astjab.org", "blahblah");
	ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
		ASTOBJ_RDLOCK(iterator);
		ast_verbose("User: %s\n", iterator->name);
		for (resource = iterator->resources; resource; resource = resource->next) {
			ast_verbose("Resource: %s\n", resource->resource);
			if(resource->cap) {
				ast_verbose("   client: %s\n", resource->cap->parent->node);
				ast_verbose("   version: %s\n", resource->cap->version);
				ast_verbose("   Jingle Capable: %d\n", resource->cap->jingle);
			}
			ast_verbose("	Priority: %d\n", resource->priority);
			ast_verbose("	Status: %d\n", resource->status); 
			ast_verbose("	Message: %s\n", S_OR(resource->description,"")); 
		}
		ASTOBJ_UNLOCK(iterator);
	});
	ast_verbose("\nOooh a working message stack!\n");
	AST_LIST_LOCK(&client->messages);
	AST_LIST_TRAVERSE(&client->messages, tmp, list) {
		ast_verbose("	Message from: %s with id %s @ %s	%s\n",tmp->from, S_OR(tmp->id,""), ctime(&tmp->arrived), S_OR(tmp->message, ""));
	}
	AST_LIST_UNLOCK(&client->messages);
	ASTOBJ_UNREF(client, aji_client_destroy);

	return RESULT_SUCCESS;
}

/*!
 * \brief creates aji_client structure.
 * \param label, ast_variable, debug, pruneregister, component/client, aji_client to dump into. 
 * \return 0.
 */
static int aji_create_client(char *label, struct ast_variable *var, int debug)
{
	char *resource;
	struct aji_client *client = NULL;
	int flag = 0;

	client = ASTOBJ_CONTAINER_FIND(&clients,label);
	if (!client) {
		flag = 1;
		client = (struct aji_client *) malloc(sizeof(struct aji_client));
		if (!client) {
			ast_log(LOG_ERROR, "Out of memory!\n");
			return 0;
		}
		memset(client, 0, sizeof(struct aji_client));
		ASTOBJ_INIT(client);
		ASTOBJ_WRLOCK(client);
		ASTOBJ_CONTAINER_INIT(&client->buddies);
	} else {
		ASTOBJ_WRLOCK(client);
		ASTOBJ_UNMARK(client);
	}
	ASTOBJ_CONTAINER_MARKALL(&client->buddies);
	ast_copy_string(client->name, label, sizeof(client->name));
	ast_copy_string(client->mid, "aaaaa", sizeof(client->mid));

	/* Set default values for the client object */
	client->debug = debug;
	ast_copy_flags(client, &globalflags, AST_FLAGS_ALL);
	client->port = 5222;
	client->usetls = 1;
	client->usesasl = 1;
	client->forcessl = 0;
	client->keepalive = 1;
	client->timeout = 50;
	client->message_timeout = 100;
	AST_LIST_HEAD_INIT(&client->messages);
	client->component = 0;
	ast_copy_string(client->statusmessage, "Online and Available", sizeof(client->statusmessage));

	if (flag) {
		client->authorized = 0;
		client->state = AJI_DISCONNECTED;
	}
	while (var) {
		if (!strcasecmp(var->name, "username"))
			ast_copy_string(client->user, var->value, sizeof(client->user));
		else if (!strcasecmp(var->name, "serverhost"))
			ast_copy_string(client->serverhost, var->value, sizeof(client->serverhost));
		else if (!strcasecmp(var->name, "secret"))
			ast_copy_string(client->password, var->value, sizeof(client->password));
		else if (!strcasecmp(var->name, "statusmessage"))
			ast_copy_string(client->statusmessage, var->value, sizeof(client->statusmessage));
		else if (!strcasecmp(var->name, "port"))
			client->port = atoi(var->value);
		else if (!strcasecmp(var->name, "timeout"))
			client->message_timeout = atoi(var->value);
		else if (!strcasecmp(var->name, "debug"))
			client->debug = (ast_false(var->value)) ? 0 : 1;
		else if (!strcasecmp(var->name, "type")) {
			if (!strcasecmp(var->value, "component"))
				client->component = 1;
		} else if (!strcasecmp(var->name, "usetls")) {
			client->usetls = (ast_false(var->value)) ? 0 : 1;
		} else if (!strcasecmp(var->name, "usesasl")) {
			client->usesasl = (ast_false(var->value)) ? 0 : 1;
		} else if (!strcasecmp(var->name, "forceoldssl"))
			client->forcessl = (ast_false(var->value)) ? 0 : 1;
		else if (!strcasecmp(var->name, "keepalive"))
			client->keepalive = (ast_false(var->value)) ? 0 : 1;
		else if (!strcasecmp(var->name, "autoprune"))
			ast_set2_flag(client, ast_true(var->value), AJI_AUTOPRUNE);
		else if (!strcasecmp(var->name, "autoregister"))
			ast_set2_flag(client, ast_true(var->value), AJI_AUTOREGISTER);
		else if (!strcasecmp(var->name, "buddy"))
				aji_create_buddy(var->value, client);
	/* no transport support in this version */
	/*	else if (!strcasecmp(var->name, "transport"))
				aji_create_transport(var->value, client);
	*/
		var = var->next;
	}
	if (!flag) {
		ASTOBJ_UNLOCK(client);
		ASTOBJ_UNREF(client, aji_client_destroy);
		return 1;
	}
	client->p = iks_stream_new(((client->component) ? "jabber:component:accept" : "jabber:client"), client, aji_act_hook);
	if (!client->p) {
		ast_log(LOG_ERROR, "Failed to create stream for client '%s'!\n", client->name);
		return 0;
	}
	client->stack = iks_stack_new(8192, 8192);
	if (!client->stack) {
		ast_log(LOG_ERROR, "Failed to allocate stack for client '%s'\n", client->name);
		return 0;
	}
	client->f = iks_filter_new();
	if (!client->f) {
		ast_log(LOG_ERROR, "Failed to create filter for client '%s'\n", client->name);
		return 0;
	}
	if (!strchr(client->user, '/') && !client->component) { /*client */
		resource = NULL;
		if (asprintf(&resource, "%s/asterisk", client->user) > 0) {
			client->jid = iks_id_new(client->stack, resource);
			free(resource);
		}
	} else
		client->jid = iks_id_new(client->stack, client->user);
	if (client->component) {
		iks_filter_add_rule(client->f, aji_dinfo_handler, client, IKS_RULE_NS, "http://jabber.org/protocol/disco#info", IKS_RULE_DONE);
		iks_filter_add_rule(client->f, aji_ditems_handler, client, IKS_RULE_NS, "http://jabber.org/protocol/disco#items", IKS_RULE_DONE);
		iks_filter_add_rule(client->f, aji_register_query_handler, client, IKS_RULE_SUBTYPE, IKS_TYPE_GET, IKS_RULE_NS, "jabber:iq:register", IKS_RULE_DONE);
		iks_filter_add_rule(client->f, aji_register_approve_handler, client, IKS_RULE_SUBTYPE, IKS_TYPE_SET, IKS_RULE_NS, "jabber:iq:register", IKS_RULE_DONE);
	} else {
		iks_filter_add_rule(client->f, aji_client_info_handler, client, IKS_RULE_NS, "http://jabber.org/protocol/disco#info", IKS_RULE_DONE);
	}
	if (!strchr(client->user, '/') && !client->component) { /*client */
		resource = NULL;
		if (asprintf(&resource, "%s/asterisk", client->user) > 0) {
			client->jid = iks_id_new(client->stack, resource);
			free(resource);
		}
	} else
		client->jid = iks_id_new(client->stack, client->user);
	iks_set_log_hook(client->p, aji_log_hook);
	ASTOBJ_UNLOCK(client);
	ASTOBJ_CONTAINER_LINK(&clients,client);
	return 1;
}

/*!
 * \brief creates transport.
 * \param label, buddy to dump it into. 
 * \return 0.
 */
/* no connecting to transports today */
/*
static int aji_create_transport(char *label, struct aji_client *client)
{
	char *server = NULL, *buddyname = NULL, *user = NULL, *pass = NULL;
	struct aji_buddy *buddy = NULL;

	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies,label);
	if (!buddy) {
		buddy = malloc(sizeof(struct aji_buddy));
		if(!buddy) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return 0;
		}
		memset(buddy, 0, sizeof(struct aji_buddy));
		ASTOBJ_INIT(buddy);
	}
	ASTOBJ_WRLOCK(buddy);
	server = label;
	if ((buddyname = strchr(label, ','))) {
		*buddyname = '\0';
		buddyname++;
		if (buddyname && buddyname[0] != '\0') {
			if ((user = strchr(buddyname, ','))) {
				*user = '\0';
				user++;
				if (user && user[0] != '\0') {
					if ((pass = strchr(user, ','))) {
						*pass = '\0';
						pass++;
						ast_copy_string(buddy->pass, pass, sizeof(buddy->pass));
						ast_copy_string(buddy->user, user, sizeof(buddy->user));
						ast_copy_string(buddy->name, buddyname, sizeof(buddy->name));
						ast_copy_string(buddy->server, server, sizeof(buddy->server));
						return 1;
					}
				}
			}
		}
	}
	ASTOBJ_UNLOCK(buddy);
	ASTOBJ_UNMARK(buddy);
	ASTOBJ_CONTAINER_LINK(&client->buddies, buddy);
	return 0;
}
*/

/*!
 * \brief creates buddy.
 * \param label, buddy to dump it into. 
 * \return 0.
 */
static int aji_create_buddy(char *label, struct aji_client *client)
{
	struct aji_buddy *buddy = NULL;
	int flag = 0;
	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies,label);
	if (!buddy) {
		flag = 1;
		buddy = malloc(sizeof(struct aji_buddy));
		if(!buddy) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return 0;
		}
		memset(buddy, 0, sizeof(struct aji_buddy));
		ASTOBJ_INIT(buddy);
	}
	ASTOBJ_WRLOCK(buddy);
	ast_copy_string(buddy->name, label, sizeof(buddy->name));
	ASTOBJ_UNLOCK(buddy);
	if(flag)
		ASTOBJ_CONTAINER_LINK(&client->buddies, buddy);
	else {
		ASTOBJ_UNMARK(buddy);
		ASTOBJ_UNREF(buddy, aji_buddy_destroy);
	}
	return 1;
}

/*!
 * \brief load config file.
 * \param void. 
 * \return 1.
 */
static int aji_load_config(void)
{
	char *cat = NULL;
	int debug = 1;
	struct ast_config *cfg = NULL;
	struct ast_variable *var = NULL;

	cfg = ast_config_load(JABBER_CONFIG);
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", JABBER_CONFIG);
		return 0;
	}

	cat = ast_category_browse(cfg, NULL);
	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		if (!strcasecmp(var->name, "debug"))
			debug = (ast_false(ast_variable_retrieve(cfg, "general", "debug"))) ? 0 : 1;
		else if (!strcasecmp(var->name, "autoprune"))
			ast_set2_flag(&globalflags, ast_true(var->value), AJI_AUTOPRUNE);
		else if (!strcasecmp(var->name, "autoregister"))
			ast_set2_flag(&globalflags, ast_true(var->value), AJI_AUTOREGISTER);
	}

	while (cat) {
		if (strcasecmp(cat, "general")) {
				var = ast_variable_browse(cfg, cat);
				aji_create_client(cat, var, debug);
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_config_destroy(cfg); /* or leak memory */
	return 1;
}

/*!
  * \brief grab a aji_client structure by label name or JID 
  * (without the resource string)
  * \param name label or JID 
  * \return aji_client.
 */
struct aji_client *ast_aji_get_client(const char *name)
{
	struct aji_client *client = NULL;
	char *aux = NULL;

	client = ASTOBJ_CONTAINER_FIND(&clients, name);
 	if (!client && strchr(name, '@')) {
 		ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
 			aux = ast_strdupa(iterator->user);
 			if (strchr(aux, '/')) {
 				/* strip resource for comparison */
 				aux = strsep(&aux, "/");
 			}
 			if (!strncasecmp(aux, name, strlen(aux))) {
 				client = iterator;
 			}				
 		});
 	}
 
	return client;
}

struct aji_client_container *ast_aji_get_clients(void)
{
	return &clients;
}

static char mandescr_jabber_send[] =
"Description: Sends a message to a Jabber Client.\n"
"Variables: \n"
"  Jabber:	Client or transport Asterisk uses to connect to JABBER.\n"
"  ScreenName:	User Name to message.\n"
"  Message:	Message to be sent to the buddy\n";

/*! \brief  Send a Jabber Message via call from the Manager */
static int manager_jabber_send(struct mansession *s, const struct message *m)
{
	struct aji_client *client = NULL;
	const char *id = astman_get_header(m,"ActionID");
	const char *jabber = astman_get_header(m,"Jabber");
	const char *screenname = astman_get_header(m,"ScreenName");
	const char *message = astman_get_header(m,"Message");

	if (ast_strlen_zero(jabber)) {
		astman_send_error(s, m, "No transport specified");
		return 0;
	}
	if (ast_strlen_zero(screenname)) {
		astman_send_error(s, m, "No ScreenName specified");
		return 0;
	}
	if (ast_strlen_zero(message)) {
		astman_send_error(s, m, "No Message specified");
		return 0;
	}

	astman_send_ack(s, m, "Attempting to send Jabber Message");
	client = ast_aji_get_client(jabber);
	if (!client) {
		astman_send_error(s, m, "Could not find Sender");
		return 0;
	}	
	if (strchr(screenname, '@') && message) {
		ast_aji_send(client, screenname, message);
		astman_append(s, "Response: Success\r\n");
	} else {
		astman_append(s, "Response: Failure\r\n");
	}
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}
	astman_append(s, "\r\n");
	return 0;
}


static int aji_reload()
{
	ASTOBJ_CONTAINER_MARKALL(&clients);
	if (!aji_load_config()) {
		ast_log(LOG_ERROR, "JABBER: Failed to load config.\n");
		return 0;
	}
	ASTOBJ_CONTAINER_PRUNE_MARKED(&clients, aji_client_destroy);
	ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
		ASTOBJ_RDLOCK(iterator);
		if(iterator->state == AJI_DISCONNECTED) {
			if (!iterator->thread)
				ast_pthread_create_background(&iterator->thread, NULL, aji_recv_loop, iterator);
		} else if (iterator->state == AJI_CONNECTING)
			aji_get_roster(iterator);
		ASTOBJ_UNLOCK(iterator);
	});
	
	return 1;
}

static int unload_module(void)
{

	/* Check if TLS is initialized. If that's the case, we can't unload this
	   module due to a bug in the iksemel library that will cause a crash or
	   a deadlock. We're trying to find a way to handle this, but in the meantime
	   we will simply refuse to die... 
	 */
	if (tls_initialized) {
		ast_log(LOG_ERROR, "Module can't be unloaded due to a bug in the Iksemel library when using TLS.\n");
		return 1;	/* You need a forced unload to get rid of this module */
	}

	ast_cli_unregister_multiple(aji_cli, sizeof(aji_cli) / sizeof(struct ast_cli_entry));
	ast_unregister_application(app_ajisend);
	ast_unregister_application(app_ajistatus);
	ast_manager_unregister("JabberSend");
	
	ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
		ASTOBJ_RDLOCK(iterator);
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "JABBER: Releasing and disconneing client: %s\n", iterator->name);
		iterator->state = AJI_DISCONNECTING;
		ast_aji_disconnect(iterator);
		pthread_join(iterator->thread, NULL);
		ASTOBJ_UNLOCK(iterator);
	});

	ASTOBJ_CONTAINER_DESTROYALL(&clients, aji_client_destroy);
	ASTOBJ_CONTAINER_DESTROY(&clients);
	return 0;
}

static int load_module(void)
{
	ASTOBJ_CONTAINER_INIT(&clients);
	if(!aji_reload())
		return AST_MODULE_LOAD_DECLINE;
	ast_manager_register2("JabberSend", EVENT_FLAG_SYSTEM, manager_jabber_send,
			"Sends a message to a Jabber Client", mandescr_jabber_send);
	ast_register_application(app_ajisend, aji_send_exec, ajisend_synopsis, ajisend_descrip);
	ast_register_application(app_ajistatus, aji_status_exec, ajistatus_synopsis, ajistatus_descrip);
	ast_cli_register_multiple(aji_cli, sizeof(aji_cli) / sizeof(struct ast_cli_entry));

	return 0;
}

static int reload(void)
{
	aji_reload();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "AJI - Asterisk Jabber Interface",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
