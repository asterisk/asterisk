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
 * \brief A resource for interfacing Asterisk directly as a client
 * or a component to a XMPP/Jabber compliant server.
 *
 * References:
 * - http://www.xmpp.org - The XMPP standards foundation
 *
 * \extref Iksemel http://code.google.com/p/iksemel/
 *
 * \todo If you unload this module, chan_gtalk/jingle will be dead. How do we handle that?
 * \todo Dialplan applications need RETURN variable, like JABBERSENDSTATUS
 *
 */

/*** MODULEINFO
	<depend>iksemel</depend>
	<use>openssl</use>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <iksemel.h>

#include "asterisk/channel.h"
#include "asterisk/jabber.h"
#include "asterisk/file.h"
#include "asterisk/config.h"
#include "asterisk/callerid.h"
#include "asterisk/lock.h"
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

/*** DOCUMENTATION
	<application name="JabberSend" language="en_US">
		<synopsis>
			Send a Jabber Message
		</synopsis>
		<syntax>
			<parameter name="Jabber" required="true">
				<para>Client or transport Asterisk uses to connect to Jabber.</para>
			</parameter>
			<parameter name="JID" required="true">
				<para>XMPP/Jabber JID (Name) of recipient.</para>
			</parameter>
			<parameter name="Message" required="true">
				<para>Message to be sent to the buddy.</para>
			</parameter>
		</syntax>
		<description>
			<para>Allows user to send a message to a receipent via XMPP.</para>
		</description>
	</application>
	<application name="JabberStatus" language="en_US">
		<synopsis>
			Retrieve the status of a jabber list member
		</synopsis>
		<syntax>
			<parameter name="Jabber" required="true">
				<para>Client or transport Asterisk users to connect to Jabber.</para>
			</parameter>
			<parameter name="JID" required="true">
				<para>XMPP/Jabber JID (Name) of recipient.</para>
			</parameter>
			<parameter name="Variable" required="true">
				<para>Variable to store the status of requested user.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application is deprecated. Please use the JABBER_STATUS() function instead.</para>
			<para>Retrieves the numeric status associated with the specified buddy <replaceable>JID</replaceable>.
			The return value in the <replaceable>Variable</replaceable>will be one of the following.</para>
			<enumlist>
				<enum name="1">
					<para>Online.</para>
				</enum>
				<enum name="2">
					<para>Chatty.</para>
				</enum>
				<enum name="3">
					<para>Away.</para>
				</enum>
				<enum name="4">
					<para>Extended Away.</para>
				</enum>
				<enum name="5">
					<para>Do Not Disturb.</para>
				</enum>
				<enum name="6">
					<para>Offline.</para>
				</enum>
				<enum name="7">
					<para>Not In Roster.</para>
				</enum>
			</enumlist>
		</description>
	</application>
	<function name="JABBER_STATUS" language="en_US">
		<synopsis>
			Retrieve the status of a jabber list member
		</synopsis>
		<syntax>
			<parameter name="sender" required="true">
				<para>XMPP/Jabber ID (Name) of sender.</para>
			</parameter>
			<parameter name="buddy" required="true">
				<para>XMPP/Jabber JID (Name) of recipient.</para>
			</parameter>
			<parameter name="resource">
				<para>Client or transport Asterisk users to connect to Jabber.</para>
			</parameter>
		</syntax>
		<description>
			<para>Retrieves the numeric status associated with the specified buddy <replaceable>JID</replaceable>.
			The return value will be one of the following.</para>
			<enumlist>
				<enum name="1">
					<para>Online.</para>
				</enum>
				<enum name="2">
					<para>Chatty.</para>
				</enum>
				<enum name="3">
					<para>Away.</para>
				</enum>
				<enum name="4">
					<para>Extended Away.</para>
				</enum>
				<enum name="5">
					<para>Do Not Disturb.</para>
				</enum>
				<enum name="6">
					<para>Offline.</para>
				</enum>
				<enum name="7">
					<para>Not In Roster.</para>
				</enum>
			</enumlist>
		</description>
	</function>
	<manager name="JabberSend" language="en_US">
		<synopsis>
			Sends a message to a Jabber Client.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Jabber" required="true">
				<para>Client or transport Asterisk uses to connect to JABBER.</para>
			</parameter>
			<parameter name="JID" required="true">
				<para>XMPP/Jabber JID (Name) of recipient.</para>
			</parameter>
			<parameter name="Message" required="true">
				<para>Message to be sent to the buddy.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sends a message to a Jabber Client.</para>
		</description>
	</manager>
 ***/

/*! \todo This should really be renamed to xmpp.conf. For backwards compatibility, we
 	need to read both files */
#define JABBER_CONFIG "jabber.conf"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/*-- Forward declarations */
static void aji_buddy_destroy(struct aji_buddy *obj);
static void aji_client_destroy(struct aji_client *obj);
static int aji_is_secure(struct aji_client *client);
#ifdef HAVE_OPENSSL
static int aji_start_tls(struct aji_client *client);
static int aji_tls_handshake(struct aji_client *client);
#endif
static int aji_io_recv(struct aji_client *client, char *buffer, size_t buf_len, int timeout);
static int aji_recv(struct aji_client *client, int timeout);
static int aji_send_header(struct aji_client *client, const char *to);
static int aji_send_raw(struct aji_client *client, const char *xmlstr);
static void aji_log_hook(void *data, const char *xmpp, size_t size, int is_incoming);
static int aji_start_sasl(struct aji_client *client, enum ikssasltype type, char *username, char *pass);
static int aji_act_hook(void *data, int type, iks *node);
static void aji_handle_iq(struct aji_client *client, iks *node);
static void aji_handle_message(struct aji_client *client, ikspak *pak);
static void aji_handle_presence(struct aji_client *client, ikspak *pak);
static void aji_handle_subscribe(struct aji_client *client, ikspak *pak);
static void *aji_recv_loop(void *data);
static int aji_initialize(struct aji_client *client);
static int aji_client_connect(void *data, ikspak *pak);
static void aji_set_presence(struct aji_client *client, char *to, char *from, int level, char *desc);
static char *aji_do_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *aji_do_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *aji_show_clients(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *aji_show_buddies(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *aji_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static int aji_create_client(char *label, struct ast_variable *var, int debug);
static int aji_create_buddy(char *label, struct aji_client *client);
static int aji_reload(int reload);
static int aji_load_config(int reload);
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

static struct ast_cli_entry aji_cli[] = {
	AST_CLI_DEFINE(aji_do_set_debug, "Enable/Disable Jabber debug"),
	AST_CLI_DEFINE(aji_do_reload, "Reload Jabber configuration"),
	AST_CLI_DEFINE(aji_show_clients, "Show state of clients and components"),
	AST_CLI_DEFINE(aji_show_buddies, "Show buddy lists of our clients"),
	AST_CLI_DEFINE(aji_test, "Shows roster, but is generally used for mog's debugging."),
};

static char *app_ajisend = "JabberSend";

static char *app_ajistatus = "JabberStatus";

struct aji_client_container clients;
struct aji_capabilities *capabilities = NULL;

/*! \brief Global flags, initialized to default values */
static struct ast_flags globalflags = { AJI_AUTOREGISTER };

/*!
 * \brief Deletes the aji_client data structure.
 * \param obj aji_client The structure we will delete.
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
			ast_free(tmp->from);
		if (tmp->message)
			ast_free(tmp->message);
	}
	AST_LIST_HEAD_DESTROY(&obj->messages);
	ast_free(obj);
}

/*!
 * \brief Deletes the aji_buddy data structure.
 * \param obj aji_buddy The structure we will delete.
 * \return void.
 */
static void aji_buddy_destroy(struct aji_buddy *obj)
{
	struct aji_resource *tmp;

	while ((tmp = obj->resources)) {
		obj->resources = obj->resources->next;
		ast_free(tmp->description);
		ast_free(tmp);
	}

	ast_free(obj);
}

/*!
 * \brief Find version in XML stream and populate our capabilities list
 * \param node the node attribute in the caps element we'll look for or add to 
 * our list
 * \param version the version attribute in the caps element we'll look for or 
 * add to our list
 * \param pak struct The XML stanza we're processing
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
				res = ast_malloc(sizeof(*res));
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
		list = ast_malloc(sizeof(*list));
		if(!list) {
			ast_log(LOG_ERROR, "Out of memory!\n");
			return NULL;
		}
		res = ast_malloc(sizeof(*res));
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
/*!
 * \brief Find the aji_resource we want
 * \param buddy aji_buddy A buddy
 * \param name 
 * \return aji_resource object
*/
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

/*!
 * \brief Jabber GTalk function
 * \param node iks
 * \return 1 on success, 0 on failure.
*/
static int gtalk_yuck(iks *node)
{
	if (iks_find_with_attrib(node, "c", "node", "http://www.google.com/xmpp/client/caps"))
		return 1;
	return 0;
}

/*!
 * \brief Setup the authentication struct
 * \param id iksid 
 * \param pass password
 * \param sid
 * \return x iks
*/
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
 * \param chan ast_channel
 * \param data
 * \return 0 on success, -1 on error
 */
static int aji_status_exec(struct ast_channel *chan, const char *data)
{
	struct aji_client *client = NULL;
	struct aji_buddy *buddy = NULL;
	struct aji_resource *r = NULL;
	char *s = NULL;
	int stat = 7;
	char status[2];
	static int deprecation_warning = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(sender);
		AST_APP_ARG(jid);
		AST_APP_ARG(variable);
	);
	AST_DECLARE_APP_ARGS(jid,
		AST_APP_ARG(screenname);
		AST_APP_ARG(resource);
	);

	if (deprecation_warning++ % 10 == 0)
		ast_log(LOG_WARNING, "JabberStatus is deprecated.  Please use the JABBER_STATUS dialplan function in the future.\n");

	if (!data) {
		ast_log(LOG_ERROR, "Usage: JabberStatus(<sender>,<jid>[/<resource>],<varname>\n");
		return 0;
	}
	s = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, s);

	if (args.argc != 3) {
		ast_log(LOG_ERROR, "JabberStatus() requires 3 arguments.\n");
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(jid, args.jid, '/');

	if (!(client = ast_aji_get_client(args.sender))) {
		ast_log(LOG_WARNING, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}
	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, jid.screenname);
	if (!buddy) {
		ast_log(LOG_WARNING, "Could not find buddy in list: '%s'\n", jid.screenname);
		return -1;
	}
	r = aji_find_resource(buddy, jid.resource);
	if (!r && buddy->resources) 
		r = buddy->resources;
	if (!r)
		ast_log(LOG_NOTICE, "Resource '%s' of buddy '%s' was not found\n", jid.resource, jid.screenname);
	else
		stat = r->status;
	snprintf(status, sizeof(status), "%d", stat);
	pbx_builtin_setvar_helper(chan, args.variable, status);
	return 0;
}

static int acf_jabberstatus_read(struct ast_channel *chan, const char *name, char *data, char *buf, size_t buflen)
{
	struct aji_client *client = NULL;
	struct aji_buddy *buddy = NULL;
	struct aji_resource *r = NULL;
	int stat = 7;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(sender);
		AST_APP_ARG(jid);
	);
	AST_DECLARE_APP_ARGS(jid,
		AST_APP_ARG(screenname);
		AST_APP_ARG(resource);
	);

	if (!data) {
		ast_log(LOG_ERROR, "Usage: JABBER_STATUS(<sender>,<jid>[/<resource>])\n");
		return 0;
	}
	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc != 2) {
		ast_log(LOG_ERROR, "JABBER_STATUS requires 2 arguments: sender and jid.\n");
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(jid, args.jid, '/');

	if (!(client = ast_aji_get_client(args.sender))) {
		ast_log(LOG_WARNING, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}
	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, jid.screenname);
	if (!buddy) {
		ast_log(LOG_WARNING, "Could not find buddy in list: '%s'\n", jid.screenname);
		return -1;
	}
	r = aji_find_resource(buddy, jid.resource);
	if (!r && buddy->resources) 
		r = buddy->resources;
	if (!r)
		ast_log(LOG_NOTICE, "Resource %s of buddy %s was not found.\n", jid.resource, jid.screenname);
	else
		stat = r->status;
	snprintf(buf, buflen, "%d", stat);
	return 0;
}

static struct ast_custom_function jabberstatus_function = {
	.name = "JABBER_STATUS",
	.read = acf_jabberstatus_read,
};

/*!
 * \brief Dial plan function to send a message.
 * \param chan ast_channel
 * \param data  Data is sender|reciever|message.
 * \return 0 on success,-1 on error.
 */
static int aji_send_exec(struct ast_channel *chan, const char *data)
{
	struct aji_client *client = NULL;
	char *s;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(sender);
		AST_APP_ARG(recipient);
		AST_APP_ARG(message);
	);

	if (!data) {
		ast_log(LOG_ERROR, "Usage:  JabberSend(<sender>,<recipient>,<message>)\n");
		return 0;
	}
	s = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, s);
	if (args.argc < 3) {
		ast_log(LOG_ERROR, "JabberSend requires 3 arguments: '%s'\n", data);
		return -1;
	}

	if (!(client = ast_aji_get_client(args.sender))) {
		ast_log(LOG_WARNING, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}
	if (strchr(args.recipient, '@') && !ast_strlen_zero(args.message))
		ast_aji_send_chat(client, args.recipient, args.message);
	return 0;
}

/*! 
 * \brief Tests whether the connection is secured or not
 * \return 0 if the connection is not secured
 */
static int aji_is_secure(struct aji_client *client)
{
#ifdef HAVE_OPENSSL
	return client->stream_flags & SECURE;
#else
	return 0;
#endif
}

#ifdef HAVE_OPENSSL
/*!
 * \brief Starts the TLS procedure
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return IKS_OK on success, an error code if sending failed, IKS_NET_TLSFAIL
 * if OpenSSL is not installed
 */
static int aji_start_tls(struct aji_client *client)
{
	int ret;

	/* This is sent not encrypted */
	ret = iks_send_raw(client->p, "<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>");
	if (ret)
		return ret;

	client->stream_flags |= TRY_SECURE;
	return IKS_OK;
}

/*! 
 * \brief TLS handshake, OpenSSL initialization
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return IKS_OK on success, IKS_NET_TLSFAIL on failure 
 */
static int aji_tls_handshake(struct aji_client *client)
{
	int ret;
	int sock;
	
	ast_debug(1, "Starting TLS handshake\n"); 

	/* Load encryption, hashing algorithms and error strings */
	SSL_library_init();
	SSL_load_error_strings();

	/* Choose an SSL/TLS protocol version, create SSL_CTX */
	client->ssl_method = SSLv3_method();
	client->ssl_context = SSL_CTX_new(client->ssl_method);                
	if (!client->ssl_context)
		return IKS_NET_TLSFAIL;

	/* Create new SSL session */
	client->ssl_session = SSL_new(client->ssl_context);
	if (!client->ssl_session)
		return IKS_NET_TLSFAIL;

	/* Enforce TLS on our XMPP connection */
	sock = iks_fd(client->p);
	ret = SSL_set_fd(client->ssl_session, sock);
	if (!ret)
		return IKS_NET_TLSFAIL;

	/* Perform SSL handshake */
	ret = SSL_connect(client->ssl_session);
	if (!ret)
		return IKS_NET_TLSFAIL;

	client->stream_flags &= (~TRY_SECURE);
	client->stream_flags |= SECURE;

	/* Sent over the established TLS connection */
	ret = aji_send_header(client, client->jid->server);
	if (ret != IKS_OK)
		return IKS_NET_TLSFAIL;

	ast_debug(1, "TLS started with server\n"); 

	return IKS_OK;
}
#endif /* HAVE_OPENSSL */

/*! 
 * \brief Secured or unsecured IO socket receiving function
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param buffer the reception buffer
 * \param buf_len the size of the buffer
 * \param timeout the select timer
 * \return the number of read bytes on success, 0 on timeout expiration, 
 * -1 on  error
 */
static int aji_io_recv(struct aji_client *client, char *buffer, size_t buf_len, int timeout)
{
	int sock;
	fd_set fds;
	struct timeval tv, *tvptr = NULL;
	int len, res;

#ifdef HAVE_OPENSSL
	if (aji_is_secure(client)) {
		sock = SSL_get_fd(client->ssl_session);
		if (sock < 0)
			return -1;		
	} else
#endif /* HAVE_OPENSSL */
		sock = iks_fd(client->p);	

	memset(&tv, 0, sizeof(struct timeval));
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	tv.tv_sec = timeout;

	/* NULL value for tvptr makes ast_select wait indefinitely */
	tvptr = (timeout != -1) ? &tv : NULL;

	/* ast_select emulates linux behaviour in terms of timeout handling */
	res = ast_select(sock + 1, &fds, NULL, NULL, tvptr);
	if (res > 0) {
#ifdef HAVE_OPENSSL
		if (aji_is_secure(client)) {
			len = SSL_read(client->ssl_session, buffer, buf_len);
		} else
#endif /* HAVE_OPENSSL */
			len = recv(sock, buffer, buf_len, 0);

		if (len > 0) {
			return len;
		} else if (len <= 0) {
			return -1;
		}
	}
	return res;
}

/*! 
 * \brief Tries to receive data from the Jabber server
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param timeout the timeout value
 * This function receives (encrypted or unencrypted) data from the XMPP server,
 * and passes it to the parser.
 * \return IKS_OK on success, IKS_NET_RWERR on IO error, IKS_NET_NOCONN, if no
 * connection available, IKS_NET_EXPIRED on timeout expiration
 */
static int aji_recv (struct aji_client *client, int timeout)
{
	int len, ret;
	char buf[NET_IO_BUF_SIZE - 1];
	char newbuf[NET_IO_BUF_SIZE - 1];
	int pos = 0;
	int newbufpos = 0;
	unsigned char c;

	memset(buf, 0, sizeof(buf));
	memset(newbuf, 0, sizeof(newbuf));

	while (1) {
		len = aji_io_recv(client, buf, NET_IO_BUF_SIZE - 2, timeout);
		if (len < 0) return IKS_NET_RWERR;
		if (len == 0) return IKS_NET_EXPIRED;
		buf[len] = '\0';

		/* our iksemel parser won't work as expected if we feed
		   it with XML packets that contain multiple whitespace 
		   characters between tags */
		while (pos < len) {
			c = buf[pos];
			/* if we stumble on the ending tag character,
			   we skip any whitespace that follows it*/
			if (c == '>') {
				while (isspace(buf[pos+1])) {
					pos++;
				}
			}
			newbuf[newbufpos] = c;
			newbufpos ++;
			pos++;
		}
		pos = 0;
		newbufpos = 0;

		/* Log the message here, because iksemel's logHook is 
		   unaccessible */
		aji_log_hook(client, buf, len, 1);

		/* let iksemel deal with the string length, 
		   and reset our buffer */
		ret = iks_parse(client->p, newbuf, 0, 0);
		memset(newbuf, 0, sizeof(newbuf));

		switch (ret) {
		case IKS_NOMEM:
			ast_log(LOG_WARNING, "Parsing failure: Out of memory.\n");
			break;
		case IKS_BADXML:
			ast_log(LOG_WARNING, "Parsing failure: Invalid XML.\n");
			break;
		case IKS_HOOK:
			ast_log(LOG_WARNING, "Parsing failure: Hook returned an error.\n");
			break;
		}
		if (ret != IKS_OK) {
			return ret;
		}
		ast_debug(3, "XML parsing successful\n");	
	}
	return IKS_OK;
}

/*! 
 * \brief Sends XMPP header to the server
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param to the target XMPP server
 * \return IKS_OK on success, any other value on failure
 */
static int aji_send_header(struct aji_client *client, const char *to)
{
	char *msg;
	int len, err;

	len = 91 + strlen(client->name_space) + 6 + strlen(to) + 16 + 1;
	msg = iks_malloc(len);
	if (!msg)
		return IKS_NOMEM;
	sprintf(msg, "<?xml version='1.0'?>"
		"<stream:stream xmlns:stream='http://etherx.jabber.org/streams' xmlns='"
		"%s' to='%s' version='1.0'>", client->name_space, to);
	err = aji_send_raw(client, msg);
	iks_free(msg);
	if (err != IKS_OK)
		return err;

	return IKS_OK;
}

/*! 
 * \brief Wraps raw sending
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param x the XMPP packet to send
 * \return IKS_OK on success, any other value on failure
 */
int ast_aji_send(struct aji_client *client, iks *x)
{
	return aji_send_raw(client, iks_string(iks_stack(x), x));
}

/*! 
 * \brief Sends an XML string over an XMPP connection
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param xmlstr the XML string to send
 * The XML data is sent whether the connection is secured or not. In the 
 * latter case, we just call iks_send_raw().
 * \return IKS_OK on success, any other value on failure
 */
static int aji_send_raw(struct aji_client *client, const char *xmlstr)
{
	int ret;
#ifdef HAVE_OPENSSL
	int len = strlen(xmlstr);

	if (aji_is_secure(client)) {
		ret = SSL_write(client->ssl_session, xmlstr, len);
		if (ret) {
			/* Log the message here, because iksemel's logHook is 
			   unaccessible */
			aji_log_hook(client, xmlstr, len, 0);
			return IKS_OK;
		}
	}
#endif
	/* If needed, data will be sent unencrypted, and logHook will 
	   be called inside iks_send_raw */
	ret = iks_send_raw(client->p, xmlstr);
	if (ret != IKS_OK)
		return ret;	

	return IKS_OK;
}

/*!
 * \brief the debug loop.
 * \param data void
 * \param xmpp xml data as string
 * \param size size of string
 * \param is_incoming direction of packet 1 for inbound 0 for outbound.
 */
static void aji_log_hook(void *data, const char *xmpp, size_t size, int is_incoming)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);

	if (!ast_strlen_zero(xmpp))
		manager_event(EVENT_FLAG_USER, "JabberEvent", "Account: %s\r\nPacket: %s\r\n", client->name, xmpp);

	if (client->debug) {
		if (is_incoming)
			ast_verbose("\nJABBER: %s INCOMING: %s\n", client->name, xmpp);
		else {
			if( strlen(xmpp) == 1) {
				if(option_debug > 2  && xmpp[0] == ' ') {
					ast_verbose("\nJABBER: Keep alive packet\n");
				}
			} else
				ast_verbose("\nJABBER: %s OUTGOING: %s\n", client->name, xmpp);
		}

	}
	ASTOBJ_UNREF(client, aji_client_destroy);
}

/*!
 * \brief A wrapper function for iks_start_sasl
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param type the SASL authentication type. Supported types are PLAIN and MD5
 * \param username
 * \param pass password.
 *
 * \return IKS_OK on success, IKSNET_NOTSUPP on failure.
 */
static int aji_start_sasl(struct aji_client *client, enum ikssasltype type, char *username, char *pass)
{
	iks *x = NULL;
	int len;
	char *s;
	char *base64;

	/* trigger SASL DIGEST-MD5 only over an unsecured connection.
	   iks_start_sasl is an iksemel API function and relies on GnuTLS,
	   whereas we use OpenSSL */
	if ((type & IKS_STREAM_SASL_MD5) && !aji_is_secure(client))
		return iks_start_sasl(client->p, IKS_SASL_DIGEST_MD5, username, pass); 
	if (!(type & IKS_STREAM_SASL_PLAIN)) {
		ast_log(LOG_ERROR, "Server does not support SASL PLAIN authentication\n");
		return IKS_NET_NOTSUPP;
	}

	x = iks_new("auth"); 
	if (!x) {
		ast_log(LOG_ERROR, "Out of memory.\n");
		return IKS_NET_NOTSUPP;
	}

	iks_insert_attrib(x, "xmlns", IKS_NS_XMPP_SASL);
	len = strlen(username) + strlen(pass) + 3;
	s = alloca(len);
	base64 = alloca((len + 2) * 4 / 3);
	iks_insert_attrib(x, "mechanism", "PLAIN");
	snprintf(s, len, "%c%s%c%s", 0, username, 0, pass);

	/* exclude the NULL training byte from the base64 encoding operation
	   as some XMPP servers will refuse it.
	   The format for authentication is [authzid]\0authcid\0password
	   not [authzid]\0authcid\0password\0 */
	ast_base64encode(base64, (const unsigned char *) s, len - 1, (len + 2) * 4 / 3);
	iks_insert_cdata(x, base64, 0);
	ast_aji_send(client, x);
	iks_delete(x);

	return IKS_OK;
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
	int features = 0;

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
			if (client->usetls && !aji_is_secure(client)) {
#ifndef HAVE_OPENSSL
				ast_log(LOG_ERROR, "OpenSSL not installed. You need to install OpenSSL on this system, or disable the TLS option in your configuration file\n");
				ASTOBJ_UNREF(client, aji_client_destroy);
				return IKS_HOOK;
#else
				if (aji_start_tls(client) == IKS_NET_TLSFAIL) {
					ast_log(LOG_ERROR, "Could not start TLS\n");
					ASTOBJ_UNREF(client, aji_client_destroy);
					return IKS_HOOK;		
				}
#endif
				break;
			}
			if (!client->usesasl) {
				iks_filter_add_rule(client->f, aji_client_connect, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, client->mid, IKS_RULE_DONE);
				auth = jabber_make_auth(client->jid, client->password, iks_find_attrib(node, "id"));
				if (auth) {
					iks_insert_attrib(auth, "id", client->mid);
					iks_insert_attrib(auth, "to", client->jid->server);
					ast_aji_increment_mid(client->mid);
					ast_aji_send(client, auth);
					iks_delete(auth);
				} else
					ast_log(LOG_ERROR, "Out of memory.\n");
			}
			break;

		case IKS_NODE_NORMAL:
#ifdef HAVE_OPENSSL
			if (client->stream_flags & TRY_SECURE) {
				if (!strcmp("proceed", iks_name(node))) {
					return aji_tls_handshake(client);
				}
			}
#endif
			if (!strcmp("stream:features", iks_name(node))) {
				features = iks_stream_features(node);
				if (client->usesasl) {
					if (client->usetls && !aji_is_secure(client))
						break;
					if (client->authorized) {
						if (features & IKS_STREAM_BIND) {
							iks_filter_add_rule(client->f, aji_client_connect, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_DONE);
							auth = iks_make_resource_bind(client->jid);
							if (auth) {
								iks_insert_attrib(auth, "id", client->mid);
								ast_aji_increment_mid(client->mid);
								ast_aji_send(client, auth);
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
								ast_aji_send(client, auth);
								iks_delete(auth);
							} else {
								ast_log(LOG_ERROR, "Out of memory.\n");
							}
						}
					} else {
						int ret;
						if (!client->jid->user) {
							ast_log(LOG_ERROR, "Malformed Jabber ID : %s (domain missing?)\n", client->jid->full);
							break;
						}

						ret = aji_start_sasl(client, features, client->jid->user, client->password);
						if (ret != IKS_OK) {
							ASTOBJ_UNREF(client, aji_client_destroy);
							return IKS_HOOK;
						}
						break;
					}
				}
			} else if (!strcmp("failure", iks_name(node))) {
				ast_log(LOG_ERROR, "JABBER: encryption failure. possible bad password.\n");
			} else if (!strcmp("success", iks_name(node))) {
				client->authorized = 1;
				aji_send_header(client, client->jid->server);
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
				if (asprintf(&handshake, "<handshake>%s</handshake>", shasum) >= 0) {
					aji_send_raw(client, handshake);
					ast_free(handshake);
					handshake = NULL;
				}
				client->state = AJI_CONNECTING;
				if(aji_recv(client, 1) == 2) /*XXX proper result for iksemel library on iks_recv of <handshake/> XXX*/
					client->state = AJI_CONNECTED;
				else
					ast_log(LOG_WARNING, "Jabber didn't seem to handshake, failed to authenticate.\n");
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
		ast_debug(1, "JABBER: I don't know what to do with paktype NONE.\n");
		break;
	case IKS_PAK_MESSAGE:
		aji_handle_message(client, pak);
		ast_debug(1, "JABBER: Handling paktype MESSAGE.\n");
		break;
	case IKS_PAK_PRESENCE:
		aji_handle_presence(client, pak);
		ast_debug(1, "JABBER: Handling paktype PRESENCE\n");
		break;
	case IKS_PAK_S10N:
		aji_handle_subscribe(client, pak);
		ast_debug(1, "JABBER: Handling paktype S10N\n");
		break;
	case IKS_PAK_IQ:
		ast_debug(1, "JABBER: Handling paktype IQ\n");
		aji_handle_iq(client, node);
		break;
	default:
		ast_debug(1, "JABBER: I don't know anything about paktype '%d'\n", pak->type);
		break;
	}
	
	iks_filter_packet(client->f, pak);

	if (node)
		iks_delete(node);

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_OK;
}
/*!
 * \brief Unknown
 * \param data void
 * \param pak ikspak
 * \return IKS_FILTER_EAT.
*/
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
			ast_aji_send(client, iq);

			iks_insert_attrib(presence, "from", client->jid->full);
			iks_insert_attrib(presence, "to", pak->from->partial);
			iks_insert_attrib(presence, "id", client->mid);
			ast_aji_increment_mid(client->mid);
			iks_insert_attrib(presence, "type", "subscribe");
			iks_insert_attrib(x, "xmlns", "vcard-temp:x:update");
			iks_insert_node(presence, x);
			ast_aji_send(client, presence); 
		}
	} else {
		ast_log(LOG_ERROR, "Out of memory.\n");
	}


	iks_delete(iq);
	iks_delete(presence);
	iks_delete(x);
	
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}
/*!
 * \brief register handler for incoming querys (IQ's)
 * \param data incoming aji_client request
 * \param pak ikspak
 * \return IKS_FILTER_EAT.
*/
static int aji_register_query_handler(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	struct aji_buddy *buddy = NULL; 
	char *node = NULL;
	iks *iq = NULL, *query = NULL;

	client = (struct aji_client *) data;

	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, pak->from->partial);
	if (!buddy) {
		iks  *error = NULL, *notacceptable = NULL;

		ast_log(LOG_ERROR, "Someone.... %s tried to register but they aren't allowed\n", pak->from->partial);
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(error);
		iks_delete(notacceptable);
	} else 	if (!(node = iks_find_attrib(pak->query, "node"))) {
		iks *instructions = NULL;
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(instructions);
	}
	iks_delete(iq);
	iks_delete(query);
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

/*!
 * \brief Handles stuff
 * \param data void
 * \param pak ikspak 
 * \return IKS_FILTER_EAT.
*/
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(iq);
		iks_delete(query);
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(iq);
		iks_delete(query);
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(iq);
		iks_delete(query);
		iks_delete(feature);
	}

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;

}
/*!
 * \brief Handle add extra info
 * \param data void
 * \param pak ikspak
 * \return IKS_FILTER_EAT
*/
static int aji_client_info_handler(void *data, ikspak *pak)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	struct aji_resource *resource = NULL;
	struct aji_buddy *buddy = ASTOBJ_CONTAINER_FIND(&client->buddies, pak->from->partial);

	resource = aji_find_resource(buddy, pak->from->resource);
	if (pak->subtype == IKS_TYPE_RESULT) {
		if (!resource) {
			ast_log(LOG_NOTICE, "JABBER: Received client info from %s when not requested.\n", pak->from->full);
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
			ast_aji_send(client, iq);
		} else
			ast_log(LOG_ERROR, "Out of Memory.\n");

		iks_delete(iq);
		iks_delete(query);
		iks_delete(ident);
		iks_delete(google);
		iks_delete(disco);
	} else if (pak->subtype == IKS_TYPE_ERROR) {
		ast_log(LOG_NOTICE, "User %s does not support discovery.\n", pak->from->full);
	}
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}
/*!
 * \brief Handler of the return info packet
 * \param data aji_client
 * \param pak ikspak
 * \return IKS_FILTER_EAT
*/
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(iq);
		iks_delete(query);
		iks_delete(identity);
		iks_delete(disco);
		iks_delete(reg);
		iks_delete(commands);
		iks_delete(gateway);
		iks_delete(version);
		iks_delete(vcard);
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(iq);
		iks_delete(query);
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
			ast_aji_send(client, iq);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(iq);
		iks_delete(query);
		iks_delete(feature);
	}

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

/*!
 * \brief Handles \verbatim <iq> \endverbatim tags.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param node iks 
 * \return void.
 */
static void aji_handle_iq(struct aji_client *client, iks *node)
{
	/*Nothing to see here */
}

/*!
 * \brief Handles presence packets.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param pak ikspak the node
 */
static void aji_handle_message(struct aji_client *client, ikspak *pak)
{
	struct aji_message *insert, *tmp;
	int flag = 0;
	
	if (!(insert = ast_calloc(1, sizeof(*insert))))
		return;
	time(&insert->arrived);
	if (iks_find_cdata(pak->x, "body"))
		insert->message = ast_strdup(iks_find_cdata(pak->x, "body"));
	if (pak->id)
		ast_copy_string(insert->id, pak->id, sizeof(insert->message));
	if (pak->from)
		insert->from = ast_strdup(pak->from->full);
	AST_LIST_LOCK(&client->messages);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&client->messages, tmp, list) {
		if (flag) {
			AST_LIST_REMOVE_CURRENT(list);
			if (tmp->from)
				ast_free(tmp->from);
			if (tmp->message)
				ast_free(tmp->message);
		} else if (difftime(time(NULL), tmp->arrived) >= client->message_timeout) {
			flag = 1;
			AST_LIST_REMOVE_CURRENT(list);
			if (tmp->from)
				ast_free(tmp->from);
			if (tmp->message)
				ast_free(tmp->message);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_INSERT_HEAD(&client->messages, insert, list);
	AST_LIST_UNLOCK(&client->messages);
}
/*!
 * \brief Check the presence info
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param pak ikspak
*/
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
		aji_set_presence(client, pak->from->full, iks_find_attrib(pak->x, "to"), client->status, client->statusmessage);
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
			if (tmp->description) ast_free(tmp->description);
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
				ast_free(found);
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
		found = ast_calloc(1, sizeof(*found));

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
		if(found->cap->jingle && option_debug > 4) {
			ast_debug(1,"Special case for google till they support discover.\n");
		}
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
				ast_aji_send(client, iq);
				
			} else
				ast_log(LOG_ERROR, "Out of memory.\n");
			
			iks_delete(query);
			iks_delete(iq);
		}
	}
	switch (pak->subtype) {
	case IKS_TYPE_AVAILABLE:
		ast_debug(3, "JABBER: I am available ^_* %i\n", pak->subtype);
		break;
	case IKS_TYPE_UNAVAILABLE:
		ast_debug(3, "JABBER: I am unavailable ^_* %i\n", pak->subtype);
		break;
	default:
		ast_debug(3, "JABBER: Ohh sexy and the wrong type: %i\n", pak->subtype);
	}
	switch (pak->show) {
	case IKS_SHOW_UNAVAILABLE:
		ast_debug(3, "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
		break;
	case IKS_SHOW_AVAILABLE:
		ast_debug(3, "JABBER: type is available\n");
		break;
	case IKS_SHOW_CHAT:
		ast_debug(3, "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
		break;
	case IKS_SHOW_AWAY:
		ast_debug(3, "JABBER: type is away\n");
		break;
	case IKS_SHOW_XA:
		ast_debug(3, "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
		break;
	case IKS_SHOW_DND:
		ast_debug(3, "JABBER: type: %i subtype %i\n", pak->subtype, pak->show);
		break;
	default:
		ast_debug(3, "JABBER: Kinky! how did that happen %i\n", pak->show);
	}
}

/*!
 * \brief handles subscription requests.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param pak ikspak iksemel packet.
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
		if (presence && status) {
			iks_insert_attrib(presence, "type", "subscribed");
			iks_insert_attrib(presence, "to", pak->from->full);
			iks_insert_attrib(presence, "from", client->jid->full);
			if (pak->id)
				iks_insert_attrib(presence, "id", pak->id);
			iks_insert_cdata(status, "Asterisk has approved subscription", 0);
			iks_insert_node(presence, status);
			ast_aji_send(client, presence);
		} else
			ast_log(LOG_ERROR, "Unable to allocate nodes\n");

		iks_delete(presence);
		iks_delete(status);

		if (client->component)
			aji_set_presence(client, pak->from->full, iks_find_attrib(pak->x, "to"), client->status, client->statusmessage);
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
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param address
 * \param message
 * \return 1.
 */
int ast_aji_send_chat(struct aji_client *client, const char *address, const char *message)
{
	int res = 0;
	iks *message_packet = NULL;
	if (client->state == AJI_CONNECTED) {
		message_packet = iks_make_msg(IKS_TYPE_CHAT, address, message);
		if (message_packet) {
			iks_insert_attrib(message_packet, "from", client->jid->full);
			res = ast_aji_send(client, message_packet);
		} else {
			ast_log(LOG_ERROR, "Out of memory.\n");
		}

		iks_delete(message_packet);
	} else
		ast_log(LOG_WARNING, "JABBER: Not connected can't send\n");
	return 1;
}

/*!
 * \brief create a chatroom.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param room name of room
 * \param server name of server
 * \param topic topic for the room.
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
		ast_aji_send(client, iq);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");

	iks_delete(iq);

	return res;
}

/*!
 * \brief join a chatroom.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param room room to join
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
		res = ast_aji_send(client, presence);
		iks_insert_cdata(priority, "5", 1);
		iks_insert_attrib(presence, "to", room);
		res = ast_aji_send(client, presence);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");
	
	iks_delete(presence);
	iks_delete(priority);
	
	return res;
}

/*!
 * \brief invite to a chatroom.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param user 
 * \param room
 * \param message
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
		res = ast_aji_send(client, invite);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");

	iks_delete(body);
	iks_delete(namespace);
	iks_delete(invite);
	
	return res;
}


/*!
 * \brief receive message loop.
 * \param data void
 * \return void.
 */
static void *aji_recv_loop(void *data)
{
	struct aji_client *client = ASTOBJ_REF((struct aji_client *) data);
	int res = IKS_HOOK;

	while(res != IKS_OK) {
		ast_debug(3, "JABBER: Connecting.\n");
		res = aji_reconnect(client);
		sleep(4);
	}

	do {
		if (res == IKS_NET_RWERR || client->timeout == 0) {
			while(res != IKS_OK) {
				ast_debug(3, "JABBER: reconnecting.\n");
				res = aji_reconnect(client);
				sleep(4);
			}
		}

		res = aji_recv(client, 1);
		
		if (client->state == AJI_DISCONNECTING) {
			ast_debug(2, "Ending our Jabber client's thread due to a disconnect\n");
			pthread_exit(NULL);
		}

		/* Decrease timeout if no data received */
		if (res == IKS_NET_EXPIRED)
			client->timeout--;

		if (res == IKS_HOOK) 
			ast_log(LOG_WARNING, "JABBER: Got hook event.\n");
		else if (res == IKS_NET_TLSFAIL)
			ast_log(LOG_ERROR, "JABBER:  Failure in TLS.\n");
		else if (client->timeout == 0 && client->state == AJI_CONNECTED) {
			res = client->keepalive ? aji_send_raw(client, " ") : IKS_OK;
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
 * \param mid char.
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

#if 0
/*!
 * \brief attempts to register to a transport.
 * \param aji_client struct, and xml packet.
 * \return IKS_FILTER_EAT.
 */
/*allows for registering to transport , was too sketch and is out for now. */
static int aji_register_transport(void *data, ikspak *pak)
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
		res = ast_aji_send(client, send);
	} else 
		ast_log(LOG_ERROR, "Out of memory.\n");

	if (send)
		iks_delete(send);
	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;

}
/*!
 * \brief attempts to register to a transport step 2.
 * \param aji_client struct, and xml packet.
 * \return IKS_FILTER_EAT.
 */
/* more of the same blob of code, too wonky for now*/
static int aji_register_transport2(void *data, ikspak *pak)
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
		res = ast_aji_send(client, regiq);
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
#endif

/*!
 * \brief goes through roster and prunes users not needed in list, or adds them accordingly.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return void.
 * \note The messages here should be configurable.
 */
static void aji_pruneregister(struct aji_client *client)
{
	int res = 0;
	iks *removeiq = iks_new("iq");
	iks *removequery = iks_new("query");
	iks *removeitem = iks_new("item");
	iks *send = iks_make_iq(IKS_TYPE_GET, "http://jabber.org/protocol/disco#items");
	if (!client || !removeiq || !removequery || !removeitem || !send) {
		ast_log(LOG_ERROR, "Out of memory.\n");
		goto safeout;
	}

	iks_insert_node(removeiq, removequery);
	iks_insert_node(removequery, removeitem);
	ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
		ASTOBJ_RDLOCK(iterator);
		/* For an aji_buddy, both AUTOPRUNE and AUTOREGISTER will never
		 * be called at the same time */
		if (ast_test_flag(&iterator->flags, AJI_AUTOPRUNE)) { /* If autoprune is set on jabber.conf */
			res = ast_aji_send(client, iks_make_s10n(IKS_TYPE_UNSUBSCRIBE, iterator->name,
								 "GoodBye. Your status is no longer needed by Asterisk the Open Source PBX"
								 " so I am no longer subscribing to your presence.\n"));
			res = ast_aji_send(client, iks_make_s10n(IKS_TYPE_UNSUBSCRIBED, iterator->name,
								 "GoodBye.  You are no longer in the Asterisk config file so I am removing"
								 " your access to my presence.\n"));
			iks_insert_attrib(removeiq, "from", client->jid->full); 
			iks_insert_attrib(removeiq, "type", "set"); 
			iks_insert_attrib(removequery, "xmlns", "jabber:iq:roster");
			iks_insert_attrib(removeitem, "jid", iterator->name);
			iks_insert_attrib(removeitem, "subscription", "remove");
			res = ast_aji_send(client, removeiq);
		} else if (ast_test_flag(&iterator->flags, AJI_AUTOREGISTER)) {
			res = ast_aji_send(client, iks_make_s10n(IKS_TYPE_SUBSCRIBE, iterator->name, 
								 "Greetings! I am the Asterisk Open Source PBX and I want to subscribe to your presence\n"));
			ast_clear_flag(&iterator->flags, AJI_AUTOREGISTER);
		}
		ASTOBJ_UNLOCK(iterator);
	});

 safeout:
	iks_delete(removeiq);
	iks_delete(removequery);
	iks_delete(removeitem);
	iks_delete(send);
	
	ASTOBJ_CONTAINER_PRUNE_MARKED(&client->buddies, aji_buddy_destroy);
}

/*!
 * \brief filters the roster packet we get back from server.
 * \param data void
 * \param pak ikspak iksemel packet.
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
					ast_clear_flag(&iterator->flags, AJI_AUTOPRUNE | AJI_AUTOREGISTER);
				}
			}
			x = iks_next(x);
		}
		if (!flag)
			ast_copy_flags(&iterator->flags, &client->flags, AJI_AUTOREGISTER);
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

			if (flag) {
				/* found buddy, don't create a new one */
				x = iks_next(x);
				continue;
			}
			
			buddy = ast_calloc(1, sizeof(*buddy));
			if (!buddy) {
				ast_log(LOG_WARNING, "Out of memory\n");
				return 0;
			}
			ASTOBJ_INIT(buddy);
			ASTOBJ_WRLOCK(buddy);
			ast_copy_string(buddy->name, iks_find_attrib(x, "jid"), sizeof(buddy->name));
			ast_clear_flag(&buddy->flags, AST_FLAGS_ALL);
			if(ast_test_flag(&client->flags, AJI_AUTOPRUNE)) {
				ast_set_flag(&buddy->flags, AJI_AUTOPRUNE);
				ASTOBJ_MARK(buddy);
			} else if (!iks_strcmp(iks_find_attrib(x, "subscription"), "none") || !iks_strcmp(iks_find_attrib(x, "subscription"), "from")) {
				/* subscribe to buddy's presence only 
				   if we really need to */
				ast_set_flag(&buddy->flags, AJI_AUTOREGISTER);
			}
			ASTOBJ_UNLOCK(buddy);
			if (buddy) {
				ASTOBJ_CONTAINER_LINK(&client->buddies, buddy);
				ASTOBJ_UNREF(buddy, aji_buddy_destroy);
			}
		}
		x = iks_next(x);
	}

	iks_delete(x);
	aji_pruneregister(client);

	ASTOBJ_UNREF(client, aji_client_destroy);
	return IKS_FILTER_EAT;
}

/*!
 * \brief reconnect to jabber server
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return res.
*/
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

	res = aji_initialize(client);

	return res;
}

/*!
 * \brief Get the roster of jabber users
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return 1.
*/
static int aji_get_roster(struct aji_client *client)
{
	iks *roster = NULL;
	roster = iks_make_iq(IKS_TYPE_GET, IKS_NS_ROSTER);

	if(roster) {
		iks_insert_attrib(roster, "id", "roster");
		aji_set_presence(client, NULL, client->jid->full, client->status, client->statusmessage);
		ast_aji_send(client, roster);
	}

	iks_delete(roster);
	
	return 1;
}

/*!
 * \brief connects as a client to jabber server.
 * \param data void
 * \param pak ikspak iksemel packet
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
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return 1.
 */
static int aji_initialize(struct aji_client *client)
{
	int connected = IKS_NET_NOCONN;

#ifdef HAVE_OPENSSL	
	/* reset stream flags */
	client->stream_flags = 0;
#endif
	/* If it's a component, connect to user, otherwise, connect to server */
	connected = iks_connect_via(client->p, S_OR(client->serverhost, client->jid->server), client->port, client->component ? client->user : client->jid->server);

	if (connected == IKS_NET_NOCONN) {
		ast_log(LOG_ERROR, "JABBER ERROR: No Connection\n");
		return IKS_HOOK;
	} else 	if (connected == IKS_NET_NODNS) {
		ast_log(LOG_ERROR, "JABBER ERROR: No DNS %s for client to  %s\n", client->name, S_OR(client->serverhost, client->jid->server));
		return IKS_HOOK;
	}

	return IKS_OK;
}

/*!
 * \brief disconnect from jabber server.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return 1.
 */
int ast_aji_disconnect(struct aji_client *client)
{
	if (client) {
		ast_verb(4, "JABBER: Disconnecting\n");
#ifdef HAVE_OPENSSL
		if (client->stream_flags & SECURE) {
			SSL_shutdown(client->ssl_session);
			SSL_CTX_free(client->ssl_context);
			SSL_free(client->ssl_session);
		}
#endif
		iks_disconnect(client->p);
		iks_parser_delete(client->p);
		ASTOBJ_UNREF(client, aji_client_destroy);
	}

	return 1;
}

/*!
 * \brief set presence of client.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param to user send it to
 * \param from user it came from
 * \param level
 * \param desc
 * \return void.
 */
static void aji_set_presence(struct aji_client *client, char *to, char *from, int level, char *desc)
{
	int res = 0;
	iks *presence = iks_make_pres(level, desc);
	iks *cnode = iks_new("c");
	iks *priority = iks_new("priority");
	char priorityS[10];

	if (presence && cnode && client && priority) {
		if(to)
			iks_insert_attrib(presence, "to", to);
		if(from)
			iks_insert_attrib(presence, "from", from);
		snprintf(priorityS, sizeof(priorityS), "%d", client->priority);
		iks_insert_cdata(priority, priorityS, strlen(priorityS));
		iks_insert_node(presence, priority);
		iks_insert_attrib(cnode, "node", "http://www.asterisk.org/xmpp/client/caps");
		iks_insert_attrib(cnode, "ver", "asterisk-xmpp");
		iks_insert_attrib(cnode, "ext", "voice-v1");
		iks_insert_attrib(cnode, "xmlns", "http://jabber.org/protocol/caps");
		iks_insert_node(presence, cnode);
		res = ast_aji_send(client, presence);
	} else
		ast_log(LOG_ERROR, "Out of memory.\n");

	iks_delete(cnode);
	iks_delete(presence);
	iks_delete(priority);
}

/*!
 * \brief Turn on/off console debugging.
 * \return CLI_SUCCESS.
 */
static char *aji_do_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "jabber set debug {on|off}";
		e->usage =
			"Usage: jabber set debug {on|off}\n"
			"       Enables/disables dumping of XMPP/Jabber packets for debugging purposes.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strncasecmp(a->argv[e->args - 1], "on", 2)) {
		ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
			ASTOBJ_RDLOCK(iterator); 
			iterator->debug = 1;
			ASTOBJ_UNLOCK(iterator);
		});
		ast_cli(a->fd, "Jabber Debugging Enabled.\n");
		return CLI_SUCCESS;
	} else if (!strncasecmp(a->argv[e->args - 1], "off", 3)) {
		ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
			ASTOBJ_RDLOCK(iterator); 
			iterator->debug = 0;
			ASTOBJ_UNLOCK(iterator);
		});
		ast_cli(a->fd, "Jabber Debugging Disabled.\n");
		return CLI_SUCCESS;
	}
	return CLI_SHOWUSAGE; /* defaults to invalid */
}

/*!
 * \brief Reload jabber module.
 * \return CLI_SUCCESS.
 */
static char *aji_do_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "jabber reload";
		e->usage =
			"Usage: jabber reload\n"
			"       Reloads the Jabber module.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	aji_reload(1);
	ast_cli(a->fd, "Jabber Reloaded.\n");
	return CLI_SUCCESS;
}

/*!
 * \brief Show client status.
 * \return CLI_SUCCESS.
 */
static char *aji_show_clients(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *status;
	int count = 0;
	
	switch (cmd) {
	case CLI_INIT:
		e->command = "jabber show connected";
		e->usage =
			"Usage: jabber show connected\n"
			"       Shows state of clients and components\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "Jabber Users and their status:\n");
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
		ast_cli(a->fd, "       User: %s     - %s\n", iterator->user, status);
		ASTOBJ_UNLOCK(iterator);
	});
	ast_cli(a->fd, "----\n");
	ast_cli(a->fd, "   Number of users: %d\n", count);
	return CLI_SUCCESS;
}

/*!
 * \brief Show buddy lists
 * \return CLI_SUCCESS.
 */
static char *aji_show_buddies(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct aji_resource *resource;
	struct aji_client *client;

	switch (cmd) {
	case CLI_INIT:
		e->command = "jabber show buddies";
		e->usage =
			"Usage: jabber show buddies\n"
			"       Shows buddy lists of our clients\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "Jabber buddy lists\n");
	ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
		ast_cli(a->fd,"Client: %s\n", iterator->user);
		client = iterator;
		ASTOBJ_CONTAINER_TRAVERSE(&client->buddies, 1, {
			ASTOBJ_RDLOCK(iterator);
			ast_cli(a->fd,"\tBuddy:\t%s\n", iterator->name);
			if (!iterator->resources)
				ast_cli(a->fd,"\t\tResource: None\n");	
			for (resource = iterator->resources; resource; resource = resource->next) {
				ast_cli(a->fd,"\t\tResource: %s\n", resource->resource);
				if(resource->cap) {
					ast_cli(a->fd,"\t\t\tnode: %s\n", resource->cap->parent->node);
					ast_cli(a->fd,"\t\t\tversion: %s\n", resource->cap->version);
					ast_cli(a->fd,"\t\t\tJingle capable: %s\n", resource->cap->jingle ? "yes" : "no");
				}
				ast_cli(a->fd,"\t\tStatus: %d\n", resource->status);
				ast_cli(a->fd,"\t\tPriority: %d\n", resource->priority);
			}
			ASTOBJ_UNLOCK(iterator);
		});
		iterator = client;
	});
	return CLI_SUCCESS;
}

/*!
 * \brief Send test message for debugging.
 * \return CLI_SUCCESS,CLI_FAILURE.
 */
static char *aji_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct aji_client *client;
	struct aji_resource *resource;
	const char *name = "asterisk";
	struct aji_message *tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "jabber test";
		e->usage =
			"Usage: jabber test [client]\n"
			"       Sends test message for debugging purposes.  A specific client\n"
			"       as configured in jabber.conf can be optionally specified.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3)
		return CLI_SHOWUSAGE;
	else if (a->argc == 3)
		name = a->argv[2];

	if (!(client = ASTOBJ_CONTAINER_FIND(&clients, name))) {
		ast_cli(a->fd, "Unable to find client '%s'!\n", name);
		return CLI_FAILURE;
	}

	/* XXX Does Matt really want everyone to use his personal address for tests? */ /* XXX yes he does */
	ast_aji_send_chat(client, "mogorman@astjab.org", "blahblah");
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

	return CLI_SUCCESS;
}

/*!
 * \brief creates aji_client structure.
 * \param label
 * \param var ast_variable
 * \param debug 
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
		client = ast_calloc(1, sizeof(*client));
		if (!client) {
			ast_log(LOG_ERROR, "Out of memory!\n");
			return 0;
		}
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
	ast_copy_flags(&client->flags, &globalflags, AST_FLAGS_ALL);
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
	client->priority = 0;
	client->status = IKS_SHOW_AVAILABLE;

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
			ast_set2_flag(&client->flags, ast_true(var->value), AJI_AUTOPRUNE);
		else if (!strcasecmp(var->name, "autoregister"))
			ast_set2_flag(&client->flags, ast_true(var->value), AJI_AUTOREGISTER);
		else if (!strcasecmp(var->name, "buddy"))
			aji_create_buddy((char *)var->value, client);
		else if (!strcasecmp(var->name, "priority"))
			client->priority = atoi(var->value);
		else if (!strcasecmp(var->name, "status")) {
			if (!strcasecmp(var->value, "unavailable"))
				client->status = IKS_SHOW_UNAVAILABLE;
			else
			if (!strcasecmp(var->value, "available")
			 || !strcasecmp(var->value, "online"))
				client->status = IKS_SHOW_AVAILABLE;
			else
			if (!strcasecmp(var->value, "chat")
			 || !strcasecmp(var->value, "chatty"))
				client->status = IKS_SHOW_CHAT;
			else
			if (!strcasecmp(var->value, "away"))
				client->status = IKS_SHOW_AWAY;
			else
			if (!strcasecmp(var->value, "xa")
			 || !strcasecmp(var->value, "xaway"))
				client->status = IKS_SHOW_XA;
			else
			if (!strcasecmp(var->value, "dnd"))
				client->status = IKS_SHOW_DND;
			else
			if (!strcasecmp(var->value, "invisible"))
			#ifdef IKS_SHOW_INVISIBLE
				client->status = IKS_SHOW_INVISIBLE;
			#else
			{
				ast_log(LOG_WARNING, "Your iksemel doesn't support invisible status: falling back to DND\n");
				client->status = IKS_SHOW_DND;
			}
			#endif
			else
				ast_log(LOG_WARNING, "Unknown presence status: %s\n", var->value);
		}
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

	ast_copy_string(client->name_space, (client->component) ? "jabber:component:accept" : "jabber:client", sizeof(client->name_space));
	client->p = iks_stream_new(client->name_space, client, aji_act_hook);
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
		if (asprintf(&resource, "%s/asterisk", client->user) >= 0) {
			client->jid = iks_id_new(client->stack, resource);
			ast_free(resource);
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
	iks_set_log_hook(client->p, aji_log_hook);
	ASTOBJ_UNLOCK(client);
	ASTOBJ_CONTAINER_LINK(&clients,client);
	return 1;
}

#if 0
/*!
 * \brief creates transport.
 * \param label, buddy to dump it into. 
 * \return 0.
 */
/* no connecting to transports today */
static int aji_create_transport(char *label, struct aji_client *client)
{
	char *server = NULL, *buddyname = NULL, *user = NULL, *pass = NULL;
	struct aji_buddy *buddy = NULL;

	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies,label);
	if (!buddy) {
		buddy = ast_calloc(1, sizeof(*buddy));
		if(!buddy) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return 0;
		}
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
#endif

/*!
 * \brief creates buddy.
 * \param label char.
 * \param client the configured XMPP client we use to connect to a XMPP server 
 * \return 1 on success, 0 on failure.
 */
static int aji_create_buddy(char *label, struct aji_client *client)
{
	struct aji_buddy *buddy = NULL;
	int flag = 0;
	buddy = ASTOBJ_CONTAINER_FIND(&client->buddies,label);
	if (!buddy) {
		flag = 1;
		buddy = ast_calloc(1, sizeof(*buddy));
		if(!buddy) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return 0;
		}
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

/*!< load config file. \return 1. */
static int aji_load_config(int reload)
{
	char *cat = NULL;
	int debug = 1;
	struct ast_config *cfg = NULL;
	struct ast_variable *var = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if ((cfg = ast_config_load(JABBER_CONFIG, config_flags)) == CONFIG_STATUS_FILEUNCHANGED)
		return -1;

	/* Reset flags to default value */
	ast_set_flag(&globalflags, AJI_AUTOREGISTER);

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", JABBER_CONFIG);
		return 0;
	}

	cat = ast_category_browse(cfg, NULL);
	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		if (!strcasecmp(var->name, "debug")) {
			debug = (ast_false(ast_variable_retrieve(cfg, "general", "debug"))) ? 0 : 1;
		} else if (!strcasecmp(var->name, "autoprune")) {
			ast_set2_flag(&globalflags, ast_true(var->value), AJI_AUTOPRUNE);
		} else if (!strcasecmp(var->name, "autoregister")) {
			ast_set2_flag(&globalflags, ast_true(var->value), AJI_AUTOREGISTER);
		}
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

/*! 
 * \brief  Send a Jabber Message via call from the Manager 
 * \param s mansession Manager session
 * \param m message Message to send
 * \return  0
*/
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
		ast_aji_send_chat(client, screenname, message);
		astman_append(s, "Response: Success\r\n");
	} else {
		astman_append(s, "Response: Error\r\n");
	}
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n",id);
	}
	astman_append(s, "\r\n");
	return 0;
}

/*! \brief Reload the jabber module */
static int aji_reload(int reload)
{
	int res;

	ASTOBJ_CONTAINER_MARKALL(&clients);
	if (!(res = aji_load_config(reload))) {
		ast_log(LOG_ERROR, "JABBER: Failed to load config.\n");
		return 0;
	} else if (res == -1)
		return 1;

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

/*! \brief Unload the jabber module */
static int unload_module(void)
{

	ast_cli_unregister_multiple(aji_cli, ARRAY_LEN(aji_cli));
	ast_unregister_application(app_ajisend);
	ast_unregister_application(app_ajistatus);
	ast_manager_unregister("JabberSend");
	ast_custom_function_unregister(&jabberstatus_function);
	
	ASTOBJ_CONTAINER_TRAVERSE(&clients, 1, {
		ASTOBJ_RDLOCK(iterator);
		ast_debug(3, "JABBER: Releasing and disconnecting client: %s\n", iterator->name);
		iterator->state = AJI_DISCONNECTING;
		ast_aji_disconnect(iterator);
		pthread_join(iterator->thread, NULL);
		ASTOBJ_UNLOCK(iterator);
	});

	ASTOBJ_CONTAINER_DESTROYALL(&clients, aji_client_destroy);
	ASTOBJ_CONTAINER_DESTROY(&clients);
	return 0;
}

/*! \brief Unload the jabber module */
static int load_module(void)
{
	ASTOBJ_CONTAINER_INIT(&clients);
	if(!aji_reload(0))
		return AST_MODULE_LOAD_DECLINE;
	ast_manager_register_xml("JabberSend", EVENT_FLAG_SYSTEM, manager_jabber_send);
	ast_register_application_xml(app_ajisend, aji_send_exec);
	ast_register_application_xml(app_ajistatus, aji_status_exec);
	ast_cli_register_multiple(aji_cli, ARRAY_LEN(aji_cli));
	ast_custom_function_register(&jabberstatus_function);

	return 0;
}

/*! \brief Wrapper for aji_reload */
static int reload(void)
{
	aji_reload(1);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "AJI - Asterisk Jabber Interface",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
