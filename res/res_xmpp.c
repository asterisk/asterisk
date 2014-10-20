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
 *
 * \brief XMPP client and component module.
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * Iksemel http://code.google.com/p/iksemel/
 *
 * A reference module for interfacting Asterisk directly as a client or component with
 * an XMPP/Jabber compliant server.
 *
 * This module is based upon the original res_jabber as done by Matt O'Gorman.
 *
 */

/*! \li \ref res_xmpp.c uses the configuration file \ref xmpp.conf and \ref jabber.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page xmpp.conf xmpp.conf
 * \verbinclude xmpp.conf.sample
 */

/*** MODULEINFO
	<depend>iksemel</depend>
	<use type="external">openssl</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <iksemel.h>

#include "asterisk/xmpp.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/app.h"
#include "asterisk/message.h"
#include "asterisk/manager.h"
#include "asterisk/cli.h"
#include "asterisk/config_options.h"

/*** DOCUMENTATION
	<application name="JabberSend" language="en_US" module="res_xmpp">
		<synopsis>
			Sends an XMPP message to a buddy.
		</synopsis>
		<syntax>
			<parameter name="account" required="true">
				<para>The local named account to listen on (specified in
				xmpp.conf)</para>
			</parameter>
			<parameter name="jid" required="true">
				<para>Jabber ID of the buddy to send the message to. It can be a
				bare JID (username@domain) or a full JID (username@domain/resource).</para>
			</parameter>
			<parameter name="message" required="true">
				<para>The message to send.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sends the content of <replaceable>message</replaceable> as text message
			from the given <replaceable>account</replaceable> to the buddy identified by
			<replaceable>jid</replaceable></para>
			<para>Example: JabberSend(asterisk,bob@domain.com,Hello world) sends "Hello world"
			to <replaceable>bob@domain.com</replaceable> as an XMPP message from the account
			<replaceable>asterisk</replaceable>, configured in xmpp.conf.</para>
		</description>
		<see-also>
			<ref type="function" module="res_xmpp">JABBER_STATUS</ref>
			<ref type="function" module="res_xmpp">JABBER_RECEIVE</ref>
		</see-also>
	</application>
	<function name="JABBER_RECEIVE" language="en_US" module="res_xmpp">
		<synopsis>
			Reads XMPP messages.
		</synopsis>
		<syntax>
			<parameter name="account" required="true">
				<para>The local named account to listen on (specified in
				xmpp.conf)</para>
			</parameter>
			<parameter name="jid" required="true">
				<para>Jabber ID of the buddy to receive message from. It can be a
				bare JID (username@domain) or a full JID (username@domain/resource).</para>
			</parameter>
			<parameter name="timeout">
				<para>In seconds, defaults to <literal>20</literal>.</para>
			</parameter>
		</syntax>
		<description>
			<para>Receives a text message on the given <replaceable>account</replaceable>
			from the buddy identified by <replaceable>jid</replaceable> and returns the contents.</para>
			<para>Example: ${JABBER_RECEIVE(asterisk,bob@domain.com)} returns an XMPP message
			sent from <replaceable>bob@domain.com</replaceable> (or nothing in case of a time out), to
			the <replaceable>asterisk</replaceable> XMPP account configured in xmpp.conf.</para>
		</description>
		<see-also>
			<ref type="function" module="res_xmpp">JABBER_STATUS</ref>
			<ref type="application" module="res_xmpp">JabberSend</ref>
		</see-also>
	</function>
	<function name="JABBER_STATUS" language="en_US" module="res_xmpp">
		<synopsis>
			Retrieves a buddy's status.
		</synopsis>
		<syntax>
			<parameter name="account" required="true">
				<para>The local named account to listen on (specified in
				xmpp.conf)</para>
			</parameter>
			<parameter name="jid" required="true">
				<para>Jabber ID of the buddy to receive message from. It can be a
				bare JID (username@domain) or a full JID (username@domain/resource).</para>
			</parameter>
		</syntax>
		<description>
			<para>Retrieves the numeric status associated with the buddy identified
			by <replaceable>jid</replaceable>.
			If the buddy does not exist in the buddylist, returns 7.</para>
			<para>Status will be 1-7.</para>
			<para>1=Online, 2=Chatty, 3=Away, 4=XAway, 5=DND, 6=Offline</para>
			<para>If not in roster variable will be set to 7.</para>
			<para>Example: ${JABBER_STATUS(asterisk,bob@domain.com)} returns 1 if
			<replaceable>bob@domain.com</replaceable> is online. <replaceable>asterisk</replaceable> is
			the associated XMPP account configured in xmpp.conf.</para>
		</description>
		<see-also>
			<ref type="function" module="res_xmpp">JABBER_RECEIVE</ref>
			<ref type="application" module="res_xmpp">JabberSend</ref>
		</see-also>
	</function>
	<application name="JabberSendGroup" language="en_US" module="res_xmpp">
		<synopsis>
			Send a Jabber Message to a specified chat room
		</synopsis>
		<syntax>
			<parameter name="Jabber" required="true">
				<para>Client or transport Asterisk uses to connect to Jabber.</para>
			</parameter>
			<parameter name="RoomJID" required="true">
				<para>XMPP/Jabber JID (Name) of chat room.</para>
			</parameter>
			<parameter name="Message" required="true">
				<para>Message to be sent to the chat room.</para>
			</parameter>
			<parameter name="Nickname" required="false">
				<para>The nickname Asterisk uses in the chat room.</para>
			</parameter>
		</syntax>
		<description>
			<para>Allows user to send a message to a chat room via XMPP.</para>
			<note><para>To be able to send messages to a chat room, a user must have previously joined it. Use the <replaceable>JabberJoin</replaceable> function to do so.</para></note>
		</description>
	</application>
	<application name="JabberJoin" language="en_US" module="res_xmpp">
		<synopsis>
			Join a chat room
		</synopsis>
		<syntax>
			<parameter name="Jabber" required="true">
				<para>Client or transport Asterisk uses to connect to Jabber.</para>
			</parameter>
			<parameter name="RoomJID" required="true">
				<para>XMPP/Jabber JID (Name) of chat room.</para>
			</parameter>
			<parameter name="Nickname" required="false">
				<para>The nickname Asterisk will use in the chat room.</para>
				<note><para>If a different nickname is supplied to an already joined room, the old nick will be changed to the new one.</para></note>
			</parameter>
		</syntax>
		<description>
			<para>Allows Asterisk to join a chat room.</para>
		</description>
	</application>
	<application name="JabberLeave" language="en_US" module="res_xmpp">
		<synopsis>
			Leave a chat room
		</synopsis>
		<syntax>
			<parameter name="Jabber" required="true">
				<para>Client or transport Asterisk uses to connect to Jabber.</para>
			</parameter>
			<parameter name="RoomJID" required="true">
				<para>XMPP/Jabber JID (Name) of chat room.</para>
			</parameter>
			<parameter name="Nickname" required="false">
				<para>The nickname Asterisk uses in the chat room.</para>
			</parameter>
		</syntax>
		<description>
			<para>Allows Asterisk to leave a chat room.</para>
		</description>
	</application>
	<application name="JabberStatus" language="en_US" module="res_xmpp">
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
	<manager name="JabberSend" language="en_US" module="res_xmpp">
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
	<info name="XMPPMessageToInfo" language="en_US" tech="XMPP">
		<para>Specifying a prefix of <literal>xmpp:</literal> will send the
		message as an XMPP chat message.</para>
	</info>
	<info name="XMPPMessageFromInfo" language="en_US" tech="XMPP">
		<para>Specifying a prefix of <literal>xmpp:</literal> will specify the
		account defined in <literal>xmpp.conf</literal> to send the message from.
		Note that this field is required for XMPP messages.</para>
	</info>
	<configInfo name="res_xmpp" language="en_US">
		<synopsis>XMPP Messaging</synopsis>
		<configFile name="xmpp.conf">
			<configObject name="global">
				<synopsis>Global configuration settings</synopsis>
				<configOption name="debug">
					<synopsis>Enable/disable XMPP message debugging</synopsis>
				</configOption>
				<configOption name="autoprune">
					<synopsis>Auto-remove users from buddy list.</synopsis>
					<description><para>Auto-remove users from buddy list. Depending on the setup
					(e.g., using your personal Gtalk account for a test) this could cause loss of
					the contact list.
					</para></description>
				</configOption>
				<configOption name="autoregister">
					<synopsis>Auto-register users from buddy list</synopsis>
				</configOption>
				<configOption name="collection_nodes">
					<synopsis>Enable support for XEP-0248 for use with distributed device state</synopsis>
				</configOption>
				<configOption name="pubsub_autocreate">
					<synopsis>Whether or not the PubSub server supports/is using auto-create for nodes</synopsis>
				</configOption>
				<configOption name="auth_policy">
					<synopsis>Whether to automatically accept or deny users' subscription requests</synopsis>
				</configOption>
			</configObject>
			<configObject name="client">
				<synopsis>Configuration options for an XMPP client</synopsis>
				<configOption name="username">
					<synopsis>XMPP username with optional resource</synopsis>
				</configOption>
				<configOption name="secret">
					<synopsis>XMPP password</synopsis>
				</configOption>
				<configOption name="serverhost">
					<synopsis>Route to server, e.g. talk.google.com</synopsis>
				</configOption>
				<configOption name="statusmessage">
					<synopsis>Custom status message</synopsis>
				</configOption>
				<configOption name="pubsub_node">
					<synopsis>Node for publishing events via PubSub</synopsis>
				</configOption>
				<configOption name="context">
					<synopsis>Dialplan context to send incoming messages to</synopsis>
				</configOption>
				<configOption name="priority">
					<synopsis>XMPP resource priority</synopsis>
				</configOption>
				<configOption name="port">
					<synopsis>XMPP server port</synopsis>
				</configOption>
				<configOption name="timeout">
					<synopsis>Timeout in seconds to hold incoming messages</synopsis>
					<description><para>Timeout (in seconds) on the message stack. Messages stored longer
					than this value will be deleted by Asterisk. This option applies to incoming messages only
					which are intended to be processed by the <literal>JABBER_RECEIVE</literal> dialplan function.
					</para></description>
				</configOption>
				<configOption name="debug">
					<synopsis>Enable debugging</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Connection is either a client or a component</synopsis>
				</configOption>
				<configOption name="distribute_events">
					<synopsis>Whether or not to distribute events using this connection</synopsis>
				</configOption>
				<configOption name="usetls">
					<synopsis>Whether to use TLS for the connection or not</synopsis>
				</configOption>
				<configOption name="usesasl">
					<synopsis>Whether to use SASL for the connection or not</synopsis>
				</configOption>
				<configOption name="forceoldssl">
					<synopsis>Force the use of old-style SSL for the connection</synopsis>
				</configOption>
				<configOption name="keepalive">
					<synopsis>If enabled, periodically send an XMPP message from this client with an empty message</synopsis>
				</configOption>
				<configOption name="autoprune">
					<synopsis>Auto-remove users from buddy list.</synopsis>
					<description><para>Auto-remove users from buddy list. Depending on the setup
					(e.g., using your personal Gtalk account for a test) this could cause loss of
					the contact list.
					</para></description>
				</configOption>
				<configOption name="autoregister">
					<synopsis>Auto-register users bfrom buddy list</synopsis>
				</configOption>
				<configOption name="auth_policy">
					<synopsis>Whether to automatically accept or deny users' subscription requests</synopsis>
				</configOption>
				<configOption name="sendtodialplan">
					<synopsis>Send incoming messages into the dialplan</synopsis>
				</configOption>
				<configOption name="status">
					<synopsis>Default XMPP status for the client</synopsis>
					<description><para>Can be one of the following XMPP statuses:</para>
						<enumlist>
							<enum name="chat"/>
							<enum name="available"/>
							<enum name="away"/>
							<enum name="xaway"/>
							<enum name="dnd"/>
						</enumlist>
					</description>
				</configOption>
				<configOption name="buddy">
					<synopsis>Manual addition of buddy to list</synopsis>
					<description><para>
					Manual addition of buddy to the buddy list. For distributed events, these budies are
					automatically added in the whitelist as 'owners' of the node(s).
					</para></description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

/*! \brief Supported general configuration flags */
enum {
	XMPP_AUTOPRUNE = (1 << 0),
	XMPP_AUTOREGISTER = (1 << 1),
	XMPP_AUTOACCEPT = (1 << 2),
	XMPP_DEBUG = (1 << 3),
	XMPP_USETLS = (1 << 4),
	XMPP_USESASL = (1 << 5),
	XMPP_FORCESSL = (1 << 6),
	XMPP_KEEPALIVE = (1 << 7),
	XMPP_COMPONENT = (1 << 8),
	XMPP_SEND_TO_DIALPLAN = (1 << 9),
	XMPP_DISTRIBUTE_EVENTS = (1 << 10),
};

/*! \brief Supported pubsub configuration flags */
enum {
	XMPP_XEP0248 = (1 << 0),
	XMPP_PUBSUB = (1 << 1),
	XMPP_PUBSUB_AUTOCREATE = (1 << 2),
};

/*! \brief Number of buckets for client connections */
#define CLIENT_BUCKETS 53

/*! \brief Number of buckets for buddies (per client) */
#define BUDDY_BUCKETS 53

/*! \brief Number of buckets for resources (per buddy) */
#define RESOURCE_BUCKETS 53

/*! \brief Namespace for TLS support */
#define XMPP_TLS_NS "urn:ietf:params:xml:ns:xmpp-tls"

/*! \brief Status for a disappearing buddy */
#define STATUS_DISAPPEAR 6

/*! \brief Global debug status */
static int debug;

/*! \brief XMPP Global Configuration */
struct ast_xmpp_global_config {
	struct ast_flags general; /*!< General configuration options */
	struct ast_flags pubsub;  /*!< Pubsub related configuration options */
};

/*! \brief XMPP Client Configuration */
struct ast_xmpp_client_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);        /*!< Name of the client connection */
		AST_STRING_FIELD(user);        /*!< Username to use for authentication */
		AST_STRING_FIELD(password);    /*!< Password to use for authentication */
		AST_STRING_FIELD(server);      /*!< Server hostname */
		AST_STRING_FIELD(statusmsg);   /*!< Status message for presence */
		AST_STRING_FIELD(pubsubnode);  /*!< Pubsub node */
		AST_STRING_FIELD(context);     /*!< Context for incoming messages */
		);
	int port;                       /*!< Port to use when connecting to server */
	int message_timeout;            /*!< Timeout for messages */
	int priority;                   /*!< Resource priority */
	struct ast_flags flags;         /*!< Various options that have been set */
	enum ikshowtype status;         /*!< Presence status */
	struct ast_xmpp_client *client; /*!< Pointer to the client */
	struct ao2_container *buddies;  /*!< Configured buddies */
};

struct xmpp_config {
	struct ast_xmpp_global_config *global; /*!< Global configuration options */
	struct ao2_container *clients;         /*!< Configured clients */
};

static AO2_GLOBAL_OBJ_STATIC(globals);

static int xmpp_client_request_tls(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node);
static int xmpp_client_requested_tls(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node);
static int xmpp_client_authenticate(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node);
static int xmpp_client_authenticating(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node);

static int xmpp_component_authenticate(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node);
static int xmpp_component_authenticating(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node);

/*! \brief Defined handlers for XMPP client states */
static const struct xmpp_state_handler {
	int state;
	int component;
	int (*handler)(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node);
} xmpp_state_handlers[] = {
	{ XMPP_STATE_REQUEST_TLS, 0, xmpp_client_request_tls, },
	{ XMPP_STATE_REQUESTED_TLS, 0, xmpp_client_requested_tls, },
	{ XMPP_STATE_AUTHENTICATE, 0, xmpp_client_authenticate, },
	{ XMPP_STATE_AUTHENTICATING, 0, xmpp_client_authenticating, },
	{ XMPP_STATE_AUTHENTICATE, 1, xmpp_component_authenticate, },
	{ XMPP_STATE_AUTHENTICATING, 1, xmpp_component_authenticating, },
};

static int xmpp_pak_message(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, iks *node, ikspak *pak);
static int xmpp_pak_presence(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, iks *node, ikspak *pak);
static int xmpp_pak_s10n(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, iks *node, ikspak *pak);

/*! \brief Defined handlers for different PAK types */
static const struct xmpp_pak_handler {
	int type;
	int (*handler)(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, iks *node, ikspak *pak);
} xmpp_pak_handlers[] = {
	{ IKS_PAK_MESSAGE, xmpp_pak_message, },
	{ IKS_PAK_PRESENCE, xmpp_pak_presence, },
	{ IKS_PAK_S10N, xmpp_pak_s10n, },
};

static const char *app_ajisend = "JabberSend";
static const char *app_ajisendgroup = "JabberSendGroup";
static const char *app_ajistatus = "JabberStatus";
static const char *app_ajijoin = "JabberJoin";
static const char *app_ajileave = "JabberLeave";

static ast_cond_t message_received_condition;
static ast_mutex_t messagelock;

static int xmpp_client_config_post_apply(void *obj, void *arg, int flags);

/*! \brief Destructor function for configuration */
static void ast_xmpp_client_config_destructor(void *obj)
{
	struct ast_xmpp_client_config *cfg = obj;
	ast_string_field_free_memory(cfg);
	ao2_cleanup(cfg->client);
	ao2_cleanup(cfg->buddies);
}

/*! \brief Destroy function for XMPP messages */
static void xmpp_message_destroy(struct ast_xmpp_message *message)
{
	if (message->from) {
		ast_free(message->from);
	}
	if (message->message) {
		ast_free(message->message);
	}

	ast_free(message);
}

/*! \brief Destructor callback function for XMPP client */
static void xmpp_client_destructor(void *obj)
{
	struct ast_xmpp_client *client = obj;
	struct ast_xmpp_message *message;

	ast_xmpp_client_disconnect(client);

	ast_endpoint_shutdown(client->endpoint);
	ao2_cleanup(client->endpoint);
	client->endpoint = NULL;

	if (client->filter) {
		iks_filter_delete(client->filter);
	}

	if (client->stack) {
		iks_stack_delete(client->stack);
	}

	ao2_cleanup(client->buddies);

	while ((message = AST_LIST_REMOVE_HEAD(&client->messages, list))) {
		xmpp_message_destroy(message);
	}
	AST_LIST_HEAD_DESTROY(&client->messages);
}

/*! \brief Hashing function for XMPP buddy */
static int xmpp_buddy_hash(const void *obj, const int flags)
{
	const struct ast_xmpp_buddy *buddy = obj;
	const char *id = obj;

	return ast_str_hash(flags & OBJ_KEY ? id : buddy->id);
}

/*! \brief Comparator function for XMPP buddy */
static int xmpp_buddy_cmp(void *obj, void *arg, int flags)
{
	struct ast_xmpp_buddy *buddy1 = obj, *buddy2 = arg;
	const char *id = arg;

	return !strcmp(buddy1->id, flags & OBJ_KEY ? id : buddy2->id) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Internal function which changes the XMPP client state */
static void xmpp_client_change_state(struct ast_xmpp_client *client, int state)
{
	if (state == client->state) {
		return;
	}
	client->state = state;
	if (client->state == XMPP_STATE_DISCONNECTED) {
		ast_endpoint_set_state(client->endpoint, AST_ENDPOINT_OFFLINE);
	} else if (client->state == XMPP_STATE_CONNECTED) {
		ast_endpoint_set_state(client->endpoint, AST_ENDPOINT_ONLINE);
	}
}

/*! \brief Allocator function for ast_xmpp_client */
static struct ast_xmpp_client *xmpp_client_alloc(const char *name)
{
	struct ast_xmpp_client *client;

	if (!(client = ao2_alloc(sizeof(*client), xmpp_client_destructor))) {
		return NULL;
	}

	AST_LIST_HEAD_INIT(&client->messages);
	client->thread = AST_PTHREADT_NULL;

	client->endpoint = ast_endpoint_create("XMPP", name);
	if (!client->endpoint) {
		ao2_ref(client, -1);
		return NULL;
	}

	if (!(client->buddies = ao2_container_alloc(BUDDY_BUCKETS, xmpp_buddy_hash, xmpp_buddy_cmp))) {
		ast_log(LOG_ERROR, "Could not initialize buddy container for '%s'\n", name);
		ao2_ref(client, -1);
		return NULL;
	}

	if (ast_string_field_init(client, 512)) {
		ast_log(LOG_ERROR, "Could not initialize stringfields for '%s'\n", name);
		ao2_ref(client, -1);
		return NULL;
	}

	if (!(client->stack = iks_stack_new(8192, 8192))) {
		ast_log(LOG_ERROR, "Could not create an Iksemel stack for '%s'\n", name);
		ao2_ref(client, -1);
		return NULL;
	}

	ast_string_field_set(client, name, name);

	client->timeout = 50;
	xmpp_client_change_state(client, XMPP_STATE_DISCONNECTED);
	ast_copy_string(client->mid, "aaaaa", sizeof(client->mid));

	return client;
}

/*! \brief Find function for configuration */
static void *xmpp_config_find(struct ao2_container *tmp_container, const char *category)
{
	return ao2_find(tmp_container, category, OBJ_KEY);
}

/*! \brief Look up existing client or create a new one */
static void *xmpp_client_find_or_create(const char *category)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, category))) {
		return xmpp_client_alloc(category);
	}

	ao2_ref(clientcfg->client, +1);
	return clientcfg->client;
}

/*! \brief Allocator function for configuration */
static void *ast_xmpp_client_config_alloc(const char *cat)
{
	struct ast_xmpp_client_config *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), ast_xmpp_client_config_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 512)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	if (!(cfg->client = xmpp_client_find_or_create(cat))) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	if (!(cfg->buddies = ao2_container_alloc(BUDDY_BUCKETS, xmpp_buddy_hash, xmpp_buddy_cmp))) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	ast_string_field_set(cfg, name, cat);

	return cfg;
}

/*! \brief Destructor for XMPP configuration */
static void xmpp_config_destructor(void *obj)
{
	struct xmpp_config *cfg = obj;
	ao2_cleanup(cfg->global);
	ao2_cleanup(cfg->clients);
}

/*! \brief Hashing function for configuration */
static int xmpp_config_hash(const void *obj, const int flags)
{
	const struct ast_xmpp_client_config *cfg = obj;
	const char *name = (flags & OBJ_KEY) ? obj : cfg->name;
	return ast_str_case_hash(name);
}

/*! \brief Comparator function for configuration */
static int xmpp_config_cmp(void *obj, void *arg, int flags)
{
	struct ast_xmpp_client_config *one = obj, *two = arg;
	const char *match = (flags & OBJ_KEY) ? arg : two->name;
	return strcasecmp(one->name, match) ? 0 : (CMP_MATCH | CMP_STOP);
}

/*! \brief Allocator for XMPP configuration */
static void *xmpp_config_alloc(void)
{
	struct xmpp_config *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), xmpp_config_destructor))) {
		return NULL;
	}

	if (!(cfg->global = ao2_alloc(sizeof(*cfg->global), NULL))) {
		goto error;
	}

	ast_set_flag(&cfg->global->general, XMPP_AUTOREGISTER | XMPP_AUTOACCEPT | XMPP_USETLS | XMPP_USESASL | XMPP_KEEPALIVE);

	if (!(cfg->clients = ao2_container_alloc(1, xmpp_config_hash, xmpp_config_cmp))) {
		goto error;
	}

	return cfg;
error:
	ao2_ref(cfg, -1);
	return NULL;
}

static int xmpp_config_prelink(void *newitem)
{
	struct ast_xmpp_client_config *clientcfg = newitem;
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, oldclientcfg, NULL, ao2_cleanup);

	if (ast_strlen_zero(clientcfg->user)) {
		ast_log(LOG_ERROR, "No user specified on client '%s'\n", clientcfg->name);
		return -1;
	} else if (ast_strlen_zero(clientcfg->password)) {
		ast_log(LOG_ERROR, "No password specified on client '%s'\n", clientcfg->name);
		return -1;
	} else if (ast_strlen_zero(clientcfg->server)) {
		ast_log(LOG_ERROR, "No server specified on client '%s'\n", clientcfg->name);
		return -1;
	}

	/* If this is a new connection force a reconnect */
	if (!cfg || !cfg->clients || !(oldclientcfg = xmpp_config_find(cfg->clients, clientcfg->name))) {
		clientcfg->client->reconnect = 1;
		return 0;
	}

	/* If any configuration options are changing that would require reconnecting set the bit so we will do so if possible */
	if (strcmp(clientcfg->user, oldclientcfg->user) ||
	    strcmp(clientcfg->password, oldclientcfg->password) ||
	    strcmp(clientcfg->server, oldclientcfg->server) ||
	    (clientcfg->port != oldclientcfg->port) ||
	    (ast_test_flag(&clientcfg->flags, XMPP_COMPONENT) != ast_test_flag(&oldclientcfg->flags, XMPP_COMPONENT)) ||
	    (clientcfg->priority != oldclientcfg->priority)) {
		clientcfg->client->reconnect = 1;
	} else {
		clientcfg->client->reconnect = 0;
	}

	return 0;
}

static void xmpp_config_post_apply(void)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);

	ao2_callback(cfg->clients, OBJ_NODATA | OBJ_MULTIPLE, xmpp_client_config_post_apply, NULL);
}

static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "global",
	.item_offset = offsetof(struct xmpp_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

struct aco_type *global_options[] = ACO_TYPES(&global_option);

static struct aco_type client_option = {
	.type = ACO_ITEM,
	.name = "client",
	.category_match = ACO_BLACKLIST,
	.category = "^(general)$",
	.item_alloc = ast_xmpp_client_config_alloc,
	.item_find = xmpp_config_find,
	.item_prelink = xmpp_config_prelink,
	.item_offset = offsetof(struct xmpp_config, clients),
};

struct aco_type *client_options[] = ACO_TYPES(&client_option);

struct aco_file res_xmpp_conf = {
	.filename = "xmpp.conf",
	.alias = "jabber.conf",
	.types = ACO_TYPES(&global_option, &client_option),
};

CONFIG_INFO_STANDARD(cfg_info, globals, xmpp_config_alloc,
		     .files = ACO_FILES(&res_xmpp_conf),
		     .post_apply_config = xmpp_config_post_apply,
	);

/*! \brief Destructor callback function for XMPP resource */
static void xmpp_resource_destructor(void *obj)
{
	struct ast_xmpp_resource *resource = obj;

	if (resource->description) {
		ast_free(resource->description);
	}
}

/*! \brief Hashing function for XMPP resource */
static int xmpp_resource_hash(const void *obj, const int flags)
{
	const struct ast_xmpp_resource *resource = obj;

	return flags & OBJ_KEY ? -1 : resource->priority;
}

/*! \brief Comparator function for XMPP resource */
static int xmpp_resource_cmp(void *obj, void *arg, int flags)
{
	struct ast_xmpp_resource *resource1 = obj;
	const char *resource = arg;

	return !strcmp(resource1->resource, resource) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Destructor callback function for XMPP buddy */
static void xmpp_buddy_destructor(void *obj)
{
	struct ast_xmpp_buddy *buddy = obj;

	if (buddy->resources) {
		ao2_ref(buddy->resources, -1);
	}
}

/*! \brief Helper function which returns whether an XMPP client connection is secure or not */
static int xmpp_is_secure(struct ast_xmpp_client *client)
{
#ifdef HAVE_OPENSSL
	return client->stream_flags & SECURE;
#else
	return 0;
#endif
}

struct ast_xmpp_client *ast_xmpp_client_find(const char *name)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, name))) {
		return NULL;
	}

	ao2_ref(clientcfg->client, +1);
	return clientcfg->client;
}

void ast_xmpp_client_unref(struct ast_xmpp_client *client)
{
	ao2_ref(client, -1);
}

void ast_xmpp_client_lock(struct ast_xmpp_client *client)
{
	ao2_lock(client);
}

void ast_xmpp_client_unlock(struct ast_xmpp_client *client)
{
	ao2_unlock(client);
}

/*! \brief Internal function used to send a message to a user or chatroom */
static int xmpp_client_send_message(struct ast_xmpp_client *client, int group, const char *nick, const char *address, const char *message)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	int res = 0;
	char from[XMPP_MAX_JIDLEN];
	iks *message_packet;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) ||
	    !(message_packet = iks_make_msg(group ? IKS_TYPE_GROUPCHAT : IKS_TYPE_CHAT, address, message))) {
		return -1;
	}

	if (!ast_strlen_zero(nick) && ast_test_flag(&clientcfg->flags, XMPP_COMPONENT)) {
		snprintf(from, sizeof(from), "%s@%s/%s", nick, client->jid->full, nick);
	} else {
		snprintf(from, sizeof(from), "%s", client->jid->full);
	}

	iks_insert_attrib(message_packet, "from", from);

	res = ast_xmpp_client_send(client, message_packet);

	iks_delete(message_packet);

	return res;
}

int ast_xmpp_client_send_message(struct ast_xmpp_client *client, const char *user, const char *message)
{
	return xmpp_client_send_message(client, 0, NULL, user, message);
}

int ast_xmpp_chatroom_invite(struct ast_xmpp_client *client, const char *user, const char *room, const char *message)
{
	int res = 0;
	iks *invite, *body = NULL, *namespace = NULL;

	if (!(invite = iks_new("message")) || !(body = iks_new("body")) || !(namespace = iks_new("x"))) {
		res = -1;
		goto done;
	}

	iks_insert_attrib(invite, "to", user);
	ast_xmpp_client_lock(client);
	iks_insert_attrib(invite, "id", client->mid);
	ast_xmpp_increment_mid(client->mid);
	ast_xmpp_client_unlock(client);
	iks_insert_cdata(body, message, 0);
	iks_insert_node(invite, body);
	iks_insert_attrib(namespace, "xmlns", "jabber:x:conference");
	iks_insert_attrib(namespace, "jid", room);
	iks_insert_node(invite, namespace);

	res = ast_xmpp_client_send(client, invite);

done:
	iks_delete(namespace);
	iks_delete(body);
	iks_delete(invite);

	return res;
}

static int xmpp_client_set_group_presence(struct ast_xmpp_client *client, const char *room, int level, const char *nick)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	int res = 0;
	iks *presence = NULL, *x = NULL;
	char from[XMPP_MAX_JIDLEN], roomid[XMPP_MAX_JIDLEN];

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) ||
	    !(presence = iks_make_pres(level, NULL)) || !(x = iks_new("x"))) {
		res = -1;
		goto done;
	}

	if (ast_test_flag(&clientcfg->flags, XMPP_COMPONENT)) {
		snprintf(from, sizeof(from), "%s@%s/%s", nick, client->jid->full, nick);
		snprintf(roomid, sizeof(roomid), "%s/%s", room, nick);
	} else {
		snprintf(from, sizeof(from), "%s", client->jid->full);
		snprintf(roomid, sizeof(roomid), "%s/%s", room, S_OR(nick, client->jid->user));
	}

	iks_insert_attrib(presence, "to", roomid);
	iks_insert_attrib(presence, "from", from);
	iks_insert_attrib(x, "xmlns", "http://jabber.org/protocol/muc");
	iks_insert_node(presence, x);

	res = ast_xmpp_client_send(client, presence);

done:
	iks_delete(x);
	iks_delete(presence);

	return res;
}

int ast_xmpp_chatroom_join(struct ast_xmpp_client *client, const char *room, const char *nickname)
{
	return xmpp_client_set_group_presence(client, room, IKS_SHOW_AVAILABLE, nickname);
}

int ast_xmpp_chatroom_send(struct ast_xmpp_client *client, const char *nickname, const char *address, const char *message)
{
	return xmpp_client_send_message(client, 1, nickname, address, message);
}

int ast_xmpp_chatroom_leave(struct ast_xmpp_client *client, const char *room, const char *nickname)
{
	return xmpp_client_set_group_presence(client, room, IKS_SHOW_UNAVAILABLE, nickname);
}

void ast_xmpp_increment_mid(char *mid)
{
	int i = 0;

	for (i = strlen(mid) - 1; i >= 0; i--) {
		if (mid[i] != 'z') {
			mid[i] = mid[i] + 1;
			i = 0;
		} else {
			mid[i] = 'a';
		}
	}
}

/*!
 * \brief Create an IQ packet
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param type the type of IQ packet to create
 * \return iks*
 */
static iks* xmpp_pubsub_iq_create(struct ast_xmpp_client *client, const char *type)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	iks *request;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) ||
	    !(request = iks_new("iq"))) {
		return NULL;
	}

	if (!ast_strlen_zero(clientcfg->pubsubnode)) {
		iks_insert_attrib(request, "to", clientcfg->pubsubnode);
	}

	iks_insert_attrib(request, "from", client->jid->full);
	iks_insert_attrib(request, "type", type);
	ast_xmpp_client_lock(client);
	ast_xmpp_increment_mid(client->mid);
	iks_insert_attrib(request, "id", client->mid);
	ast_xmpp_client_unlock(client);

	return request;
}

/*!
 * \brief Build the skeleton of a publish
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param node Name of the node that will be published to
 * \param event_type
 * \return iks *
 */
static iks* xmpp_pubsub_build_publish_skeleton(struct ast_xmpp_client *client, const char *node,
					       const char *event_type, unsigned int cachable)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	iks *request, *pubsub, *publish, *item;

	if (!cfg || !cfg->global || !(request = xmpp_pubsub_iq_create(client, "set"))) {
		return NULL;
	}

	pubsub = iks_insert(request, "pubsub");
	iks_insert_attrib(pubsub, "xmlns", "http://jabber.org/protocol/pubsub");
	publish = iks_insert(pubsub, "publish");
	iks_insert_attrib(publish, "node", ast_test_flag(&cfg->global->pubsub, XMPP_XEP0248) ? node : event_type);
	item = iks_insert(publish, "item");
	iks_insert_attrib(item, "id", node);

	if (cachable == AST_DEVSTATE_NOT_CACHABLE) {
		iks *options, *x, *field_form_type, *field_persist;

		options = iks_insert(pubsub, "publish-options");
		x = iks_insert(options, "x");
		iks_insert_attrib(x, "xmlns", "jabber:x:data");
		iks_insert_attrib(x, "type", "submit");
		field_form_type = iks_insert(x, "field");
		iks_insert_attrib(field_form_type, "var", "FORM_TYPE");
		iks_insert_attrib(field_form_type, "type", "hidden");
		iks_insert_cdata(iks_insert(field_form_type, "value"), "http://jabber.org/protocol/pubsub#publish-options", 0);
		field_persist = iks_insert(x, "field");
		iks_insert_attrib(field_persist, "var", "pubsub#persist_items");
		iks_insert_cdata(iks_insert(field_persist, "value"), "0", 1);
	}

	return item;

}

static iks* xmpp_pubsub_build_node_config(iks *pubsub, const char *node_type, const char *collection_name)
{
	iks *configure, *x, *field_owner, *field_node_type, *field_node_config,
		*field_deliver_payload, *field_persist_items, *field_access_model,
		*field_pubsub_collection;
	configure = iks_insert(pubsub, "configure");
	x = iks_insert(configure, "x");
	iks_insert_attrib(x, "xmlns", "jabber:x:data");
	iks_insert_attrib(x, "type", "submit");
	field_owner = iks_insert(x, "field");
	iks_insert_attrib(field_owner, "var", "FORM_TYPE");
	iks_insert_attrib(field_owner, "type", "hidden");
	iks_insert_cdata(iks_insert(field_owner, "value"),
			 "http://jabber.org/protocol/pubsub#owner", 39);
	if (node_type) {
		field_node_type = iks_insert(x, "field");
		iks_insert_attrib(field_node_type, "var", "pubsub#node_type");
		iks_insert_cdata(iks_insert(field_node_type, "value"), node_type, strlen(node_type));
	}
	field_node_config = iks_insert(x, "field");
	iks_insert_attrib(field_node_config, "var", "FORM_TYPE");
	iks_insert_attrib(field_node_config, "type", "hidden");
	iks_insert_cdata(iks_insert(field_node_config, "value"),
			 "http://jabber.org/protocol/pubsub#node_config", 45);
	field_deliver_payload = iks_insert(x, "field");
	iks_insert_attrib(field_deliver_payload, "var", "pubsub#deliver_payloads");
	iks_insert_cdata(iks_insert(field_deliver_payload, "value"), "1", 1);
	field_persist_items = iks_insert(x, "field");
	iks_insert_attrib(field_persist_items, "var", "pubsub#persist_items");
	iks_insert_cdata(iks_insert(field_persist_items, "value"), "1", 1);
	field_access_model = iks_insert(x, "field");
	iks_insert_attrib(field_access_model, "var", "pubsub#access_model");
	iks_insert_cdata(iks_insert(field_access_model, "value"), "whitelist", 9);
	if (node_type && !strcasecmp(node_type, "leaf")) {
		field_pubsub_collection = iks_insert(x, "field");
		iks_insert_attrib(field_pubsub_collection, "var", "pubsub#collection");
		iks_insert_cdata(iks_insert(field_pubsub_collection, "value"), collection_name,
				 strlen(collection_name));
	}
	return configure;
}

/*!
 * \brief Add Owner affiliations for pubsub node
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param node the name of the node to which to add affiliations
 * \return void
 */
static void xmpp_pubsub_create_affiliations(struct ast_xmpp_client *client, const char *node)
{
	iks *modify_affiliates = xmpp_pubsub_iq_create(client, "set");
	iks *pubsub, *affiliations, *affiliate;
	struct ao2_iterator i;
	struct ast_xmpp_buddy *buddy;

	if (!modify_affiliates) {
		ast_log(LOG_ERROR, "Could not create IQ for creating affiliations on client '%s'\n", client->name);
		return;
	}

	pubsub = iks_insert(modify_affiliates, "pubsub");
	iks_insert_attrib(pubsub, "xmlns", "http://jabber.org/protocol/pubsub#owner");
	affiliations = iks_insert(pubsub, "affiliations");
	iks_insert_attrib(affiliations, "node", node);

	i = ao2_iterator_init(client->buddies, 0);
	while ((buddy = ao2_iterator_next(&i))) {
		affiliate = iks_insert(affiliations, "affiliation");
		iks_insert_attrib(affiliate, "jid", buddy->id);
		iks_insert_attrib(affiliate, "affiliation", "owner");
		ao2_ref(buddy, -1);
	}
	ao2_iterator_destroy(&i);

	ast_xmpp_client_send(client, modify_affiliates);
	iks_delete(modify_affiliates);
}

/*!
 * \brief Create a pubsub node
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param node_type the type of node to create
 * \param name the name of the node to create
 * \param collection_name
 * \return void
 */
static void xmpp_pubsub_create_node(struct ast_xmpp_client *client, const char *node_type, const
				    char *name, const char *collection_name)
{
	iks *node, *pubsub, *create;

	if (!(node = xmpp_pubsub_iq_create(client, "set"))) {
		return;
	}

	pubsub = iks_insert(node, "pubsub");
	iks_insert_attrib(pubsub, "xmlns", "http://jabber.org/protocol/pubsub");
	create = iks_insert(pubsub, "create");
	iks_insert_attrib(create, "node", name);
	xmpp_pubsub_build_node_config(pubsub, node_type, collection_name);
	ast_xmpp_client_send(client, node);
	xmpp_pubsub_create_affiliations(client, name);
	iks_delete(node);
}

/*!
 * \brief Delete a PubSub node
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param node_name the name of the node to delete
 * return void
 */
static void xmpp_pubsub_delete_node(struct ast_xmpp_client *client, const char *node_name)
{
	iks *request, *pubsub, *delete;

	if (!(request = xmpp_pubsub_iq_create(client, "set"))) {
		return;
	}

	pubsub = iks_insert(request, "pubsub");
	iks_insert_attrib(pubsub, "xmlns", "http://jabber.org/protocol/pubsub#owner");
	delete = iks_insert(pubsub, "delete");
	iks_insert_attrib(delete, "node", node_name);
	ast_xmpp_client_send(client, request);

	iks_delete(request);
}

/*!
 * \brief Create a PubSub collection node.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param collection_name The name to use for this collection
 * \return void.
 */
static void xmpp_pubsub_create_collection(struct ast_xmpp_client *client, const char *collection_name)
{
	xmpp_pubsub_create_node(client, "collection", collection_name, NULL);
}


/*!
 * \brief Create a PubSub leaf node.
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param collection_name
 * \param leaf_name The name to use for this collection
 * \return void.
 */
static void xmpp_pubsub_create_leaf(struct ast_xmpp_client *client, const char *collection_name,
				    const char *leaf_name)
{
	xmpp_pubsub_create_node(client, "leaf", leaf_name, collection_name);
}

/*!
 * \brief Publish MWI to a PubSub node
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param mailbox The mailbox identifier
 * \param oldmsgs Old messages
 * \param newmsgs New Messages
 * \return void
 */
static void xmpp_pubsub_publish_mwi(struct ast_xmpp_client *client, const char *mailbox,
	const char *oldmsgs, const char *newmsgs)
{
	char eid_str[20];
	iks *mailbox_node, *request;

	request = xmpp_pubsub_build_publish_skeleton(client, mailbox, "message_waiting",
		AST_DEVSTATE_CACHABLE);
	if (!request) {
		return;
	}

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);
	mailbox_node = iks_insert(request, "mailbox");
	iks_insert_attrib(mailbox_node, "xmlns", "http://asterisk.org");
	iks_insert_attrib(mailbox_node, "eid", eid_str);
	iks_insert_cdata(iks_insert(mailbox_node, "NEWMSGS"), newmsgs, strlen(newmsgs));
	iks_insert_cdata(iks_insert(mailbox_node, "OLDMSGS"), oldmsgs, strlen(oldmsgs));

	ast_xmpp_client_send(client, iks_root(request));

	iks_delete(request);
}

/*!
 * \brief Publish device state to a PubSub node
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param device the name of the device whose state to publish
 * \param device_state the state to publish
 * \return void
 */
static void xmpp_pubsub_publish_device_state(struct ast_xmpp_client *client, const char *device,
					     const char *device_state, unsigned int cachable)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	iks *request, *state;
	char eid_str[20], cachable_str[2];

	if (!cfg || !cfg->global || !(request = xmpp_pubsub_build_publish_skeleton(client, device, "device_state", cachable))) {
		return;
	}

	if (ast_test_flag(&cfg->global->pubsub, XMPP_PUBSUB_AUTOCREATE)) {
		if (ast_test_flag(&cfg->global->pubsub, XMPP_XEP0248)) {
			xmpp_pubsub_create_node(client, "leaf", device, "device_state");
		} else {
			xmpp_pubsub_create_node(client, NULL, device, NULL);
		}
	}

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);
	state = iks_insert(request, "state");
	iks_insert_attrib(state, "xmlns", "http://asterisk.org");
	iks_insert_attrib(state, "eid", eid_str);
	snprintf(cachable_str, sizeof(cachable_str), "%u", cachable);
	iks_insert_attrib(state, "cachable", cachable_str);
	iks_insert_cdata(state, device_state, strlen(device_state));
	ast_xmpp_client_send(client, iks_root(request));
	iks_delete(request);
}

/*!
 * \brief Callback function for MWI events
 * \param ast_event
 * \param data void pointer to ast_client structure
 * \return void
 */
static void xmpp_pubsub_mwi_cb(void *data, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_xmpp_client *client = data;
	char oldmsgs[10], newmsgs[10];
	struct ast_mwi_state *mwi_state;

	if (!stasis_subscription_is_subscribed(sub) || ast_mwi_state_type() != stasis_message_type(msg)) {
		return;
	}

	mwi_state = stasis_message_data(msg);

	if (ast_eid_cmp(&ast_eid_default, &mwi_state->eid)) {
		/* If the event didn't originate from this server, don't send it back out. */
		return;
	}

	snprintf(oldmsgs, sizeof(oldmsgs), "%d", mwi_state->old_msgs);
	snprintf(newmsgs, sizeof(newmsgs), "%d", mwi_state->new_msgs);
	xmpp_pubsub_publish_mwi(client, mwi_state->uniqueid, oldmsgs, newmsgs);
}

/*!
 * \brief Callback function for device state events
 * \param ast_event
 * \param data void pointer to ast_client structure
 * \return void
 */
static void xmpp_pubsub_devstate_cb(void *data, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_xmpp_client *client = data;
	struct ast_device_state_message *dev_state;

	if (!stasis_subscription_is_subscribed(sub) || ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	dev_state = stasis_message_data(msg);
	if (!dev_state->eid || ast_eid_cmp(&ast_eid_default, dev_state->eid)) {
		/* If the event is aggregate or didn't originate from this server, don't send it out. */
		return;
	}

	xmpp_pubsub_publish_device_state(client, dev_state->device, ast_devstate_str(dev_state->state), dev_state->cachable);
}

/*!
 * \brief Unsubscribe from a PubSub node
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param node the name of the node to which to unsubscribe from
 * \return void
 */
static void xmpp_pubsub_unsubscribe(struct ast_xmpp_client *client, const char *node)
{
	iks *request = xmpp_pubsub_iq_create(client, "set");
	iks *pubsub, *unsubscribe;

	if (!request) {
		ast_log(LOG_ERROR, "Could not create IQ when creating pubsub unsubscription on client '%s'\n", client->name);
		return;
	}

	pubsub = iks_insert(request, "pubsub");
	iks_insert_attrib(pubsub, "xmlns", "http://jabber.org/protocol/pubsub");
	unsubscribe = iks_insert(pubsub, "unsubscribe");
	iks_insert_attrib(unsubscribe, "jid", client->jid->partial);
	iks_insert_attrib(unsubscribe, "node", node);

	ast_xmpp_client_send(client, request);
	iks_delete(request);
}

/*!
 * \brief Subscribe to a PubSub node
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param node the name of the node to which to subscribe
 * \return void
 */
static void xmpp_pubsub_subscribe(struct ast_xmpp_client *client, const char *node)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	iks *request = xmpp_pubsub_iq_create(client, "set");
	iks *pubsub, *subscribe;

	if (!cfg || !cfg->global || !request) {
		ast_log(LOG_ERROR, "Could not create IQ when creating pubsub subscription on client '%s'\n", client->name);
		return;
	}

	pubsub = iks_insert(request, "pubsub");
	iks_insert_attrib(pubsub, "xmlns", "http://jabber.org/protocol/pubsub");
	subscribe = iks_insert(pubsub, "subscribe");
	iks_insert_attrib(subscribe, "jid", client->jid->partial);
	iks_insert_attrib(subscribe, "node", node);
	if (ast_test_flag(&cfg->global->pubsub, XMPP_XEP0248)) {
		iks *options, *x, *sub_options, *sub_type, *sub_depth, *sub_expire;
		options = iks_insert(pubsub, "options");
		x = iks_insert(options, "x");
		iks_insert_attrib(x, "xmlns", "jabber:x:data");
		iks_insert_attrib(x, "type", "submit");
		sub_options = iks_insert(x, "field");
		iks_insert_attrib(sub_options, "var", "FORM_TYPE");
		iks_insert_attrib(sub_options, "type", "hidden");
		iks_insert_cdata(iks_insert(sub_options, "value"),
				 "http://jabber.org/protocol/pubsub#subscribe_options", 51);
		sub_type = iks_insert(x, "field");
		iks_insert_attrib(sub_type, "var", "pubsub#subscription_type");
		iks_insert_cdata(iks_insert(sub_type, "value"), "items", 5);
		sub_depth = iks_insert(x, "field");
		iks_insert_attrib(sub_depth, "var", "pubsub#subscription_depth");
		iks_insert_cdata(iks_insert(sub_depth, "value"), "all", 3);
		sub_expire = iks_insert(x, "field");
		iks_insert_attrib(sub_expire, "var", "pubsub#expire");
		iks_insert_cdata(iks_insert(sub_expire, "value"), "presence", 8);
	}
	ast_xmpp_client_send(client, request);
	iks_delete(request);
}

/*!
 * \brief Callback for handling PubSub events
 * \param data void pointer to ast_xmpp_client structure
 * \param pak A pak
 * \return IKS_FILTER_EAT
 */
static int xmpp_pubsub_handle_event(void *data, ikspak *pak)
{
	char *item_id, *device_state, *mailbox, *cachable_str;
	int oldmsgs, newmsgs;
	iks *item, *item_content;
	struct ast_eid pubsub_eid;
	unsigned int cachable = AST_DEVSTATE_CACHABLE;
	item = iks_find(iks_find(iks_find(pak->x, "event"), "items"), "item");
	if (!item) {
		ast_log(LOG_ERROR, "Could not parse incoming PubSub event\n");
		return IKS_FILTER_EAT;
	}
	item_id = iks_find_attrib(item, "id");
	item_content = iks_child(item);
	ast_str_to_eid(&pubsub_eid, iks_find_attrib(item_content, "eid"));
	if (!ast_eid_cmp(&ast_eid_default, &pubsub_eid)) {
		ast_debug(1, "Returning here, eid of incoming event matches ours!\n");
		return IKS_FILTER_EAT;
	}
	if (!strcasecmp(iks_name(item_content), "state")) {
		if ((cachable_str = iks_find_attrib(item_content, "cachable"))) {
			sscanf(cachable_str, "%30u", &cachable);
		}
		device_state = iks_find_cdata(item, "state");
		ast_publish_device_state_full(item_id,
						ast_devstate_val(device_state),
						cachable == AST_DEVSTATE_CACHABLE ? AST_DEVSTATE_CACHABLE : AST_DEVSTATE_NOT_CACHABLE,
						&pubsub_eid);
		return IKS_FILTER_EAT;
	} else if (!strcasecmp(iks_name(item_content), "mailbox")) {
		mailbox = strsep(&item_id, "@");
		sscanf(iks_find_cdata(item_content, "OLDMSGS"), "%10d", &oldmsgs);
		sscanf(iks_find_cdata(item_content, "NEWMSGS"), "%10d", &newmsgs);

		ast_publish_mwi_state_full(mailbox, item_id, newmsgs, oldmsgs, NULL, &pubsub_eid);

		return IKS_FILTER_EAT;
	} else {
		ast_debug(1, "Don't know how to handle PubSub event of type %s\n",
			  iks_name(item_content));
		return IKS_FILTER_EAT;
	}

	return IKS_FILTER_EAT;
}

static int xmpp_pubsub_handle_error(void *data, ikspak *pak)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	char *node_name, *error;
	int error_num;
	iks *orig_request, *orig_pubsub = iks_find(pak->x, "pubsub");
	struct ast_xmpp_client *client = data;

	if (!cfg || !cfg->global) {
		ast_log(LOG_ERROR, "No global configuration available\n");
		return IKS_FILTER_EAT;
	}

	if (!orig_pubsub) {
		ast_debug(1, "Error isn't a PubSub error, why are we here?\n");
		return IKS_FILTER_EAT;
	}

	orig_request = iks_child(orig_pubsub);
	error = iks_find_attrib(iks_find(pak->x, "error"), "code");
	node_name = iks_find_attrib(orig_request, "node");

	if (!sscanf(error, "%30d", &error_num)) {
		return IKS_FILTER_EAT;
	}

	if (error_num > 399 && error_num < 500 && error_num != 404) {
		ast_log(LOG_ERROR,
			"Error performing operation on PubSub node %s, %s.\n", node_name, error);
		return IKS_FILTER_EAT;
	} else if (error_num > 499 && error_num < 600) {
		ast_log(LOG_ERROR, "PubSub Server error, %s\n", error);
		return IKS_FILTER_EAT;
	}

	if (!strcasecmp(iks_name(orig_request), "publish")) {
		iks *request;

		if (ast_test_flag(&cfg->global->pubsub, XMPP_XEP0248)) {
			if (iks_find(iks_find(orig_request, "item"), "state")) {
				xmpp_pubsub_create_leaf(client, "device_state", node_name);
			} else if (iks_find(iks_find(orig_request, "item"), "mailbox")) {
				xmpp_pubsub_create_leaf(client, "message_waiting", node_name);
			}
		} else {
			xmpp_pubsub_create_node(client, NULL, node_name, NULL);
		}

		if ((request = xmpp_pubsub_iq_create(client, "set"))) {
			iks_insert_node(request, orig_pubsub);
			ast_xmpp_client_send(client, request);
			iks_delete(request);
		} else {
			ast_log(LOG_ERROR, "PubSub publish could not create IQ\n");
		}

		return IKS_FILTER_EAT;
	} else if (!strcasecmp(iks_name(orig_request), "subscribe")) {
		if (ast_test_flag(&cfg->global->pubsub, XMPP_XEP0248)) {
			xmpp_pubsub_create_collection(client, node_name);
		} else {
			xmpp_pubsub_create_node(client, NULL, node_name, NULL);
		}
	}

	return IKS_FILTER_EAT;
}

static int cached_devstate_cb(void *obj, void *arg, int flags)
{
	struct stasis_message *msg = obj;
	struct ast_xmpp_client *client = arg;
	xmpp_pubsub_devstate_cb(client, client->device_state_sub, msg);
	return 0;
}

/*!
 * \brief Initialize collections for event distribution
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \return void
 */
static void xmpp_init_event_distribution(struct ast_xmpp_client *client)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, cached, NULL, ao2_cleanup);

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name))) {
		return;
	}

	xmpp_pubsub_unsubscribe(client, "device_state");
	xmpp_pubsub_unsubscribe(client, "message_waiting");

	if (!(client->mwi_sub = stasis_subscribe(ast_mwi_topic_all(), xmpp_pubsub_mwi_cb, client))) {
		return;
	}

	if (!(client->device_state_sub = stasis_subscribe(ast_device_state_topic_all(), xmpp_pubsub_devstate_cb, client))) {
		client->mwi_sub = stasis_unsubscribe(client->mwi_sub);
		return;
	}

	cached = stasis_cache_dump(ast_device_state_cache(), NULL);
	ao2_callback(cached, OBJ_NODATA, cached_devstate_cb, client);

	xmpp_pubsub_subscribe(client, "device_state");
	xmpp_pubsub_subscribe(client, "message_waiting");
	iks_filter_add_rule(client->filter, xmpp_pubsub_handle_event, client, IKS_RULE_TYPE,
			    IKS_PAK_MESSAGE, IKS_RULE_FROM, clientcfg->pubsubnode, IKS_RULE_DONE);
	iks_filter_add_rule(client->filter, xmpp_pubsub_handle_error, client, IKS_RULE_TYPE,
			    IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_ERROR, IKS_RULE_DONE);

}

/*! \brief Internal astobj2 callback function which returns the first resource, which is the highest priority one */
static int xmpp_resource_immediate(void *obj, void *arg, int flags)
{
	return CMP_MATCH | CMP_STOP;
}

/*
 * \internal
 * \brief Dial plan function status(). puts the status of watched user
 * into a channel variable.
 * \param chan ast_channel
 * \param data
 * \retval 0 success
 * \retval -1 error
 */
static int xmpp_status_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_buddy *buddy;
	struct ast_xmpp_resource *resource;
	char *s = NULL, status[2];
	int stat = 7;
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

	if (deprecation_warning++ % 10 == 0) {
		ast_log(LOG_WARNING, "JabberStatus is deprecated.  Please use the JABBER_STATUS dialplan function in the future.\n");
	}

	if (ast_strlen_zero(data)) {
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
	if (jid.argc < 1 || jid.argc > 2) {
		ast_log(LOG_WARNING, "Wrong JID %s, exiting\n", args.jid);
		return -1;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, args.sender))) {
		ast_log(LOG_WARNING, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}

	if (!(buddy = ao2_find(clientcfg->client->buddies, jid.screenname, OBJ_KEY))) {
		ast_log(LOG_WARNING, "Could not find buddy in list: '%s'\n", jid.screenname);
		return -1;
	}

	if (ast_strlen_zero(jid.resource) || !(resource = ao2_callback(buddy->resources, 0, xmpp_resource_cmp, jid.resource))) {
		resource = ao2_callback(buddy->resources, OBJ_NODATA, xmpp_resource_immediate, NULL);
	}

	ao2_ref(buddy, -1);

	if (resource) {
		stat = resource->status;
		ao2_ref(resource, -1);
	} else {
		ast_log(LOG_NOTICE, "Resource '%s' of buddy '%s' was not found\n", jid.resource, jid.screenname);
	}

	snprintf(status, sizeof(status), "%d", stat);
	pbx_builtin_setvar_helper(chan, args.variable, status);

	return 0;
}

/*!
 * \internal
 * \brief Dial plan funtcion to retrieve the status of a buddy.
 * \param channel The associated ast_channel, if there is one
 * \param data The account, buddy JID, and optional timeout
 * timeout.
 * \retval 0 success
 * \retval -1 failure
 */
static int acf_jabberstatus_read(struct ast_channel *chan, const char *name, char *data, char *buf, size_t buflen)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_buddy *buddy;
	struct ast_xmpp_resource *resource;
	int stat = 7;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(sender);
			     AST_APP_ARG(jid);
		);
	AST_DECLARE_APP_ARGS(jid,
			     AST_APP_ARG(screenname);
			     AST_APP_ARG(resource);
		);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Usage: JABBER_STATUS(<sender>,<jid>[/<resource>])\n");
		return 0;
	}
	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc != 2) {
		ast_log(LOG_ERROR, "JABBER_STATUS requires 2 arguments: sender and jid.\n");
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(jid, args.jid, '/');
	if (jid.argc < 1 || jid.argc > 2) {
		ast_log(LOG_WARNING, "Wrong JID %s, exiting\n", args.jid);
		return -1;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, args.sender))) {
		ast_log(LOG_WARNING, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}

	if (!(buddy = ao2_find(clientcfg->client->buddies, jid.screenname, OBJ_KEY))) {
		ast_log(LOG_WARNING, "Could not find buddy in list: '%s'\n", jid.screenname);
		return -1;
	}

	if (ast_strlen_zero(jid.resource) || !(resource = ao2_callback(buddy->resources, 0, xmpp_resource_cmp, jid.resource))) {
		resource = ao2_callback(buddy->resources, OBJ_NODATA, xmpp_resource_immediate, NULL);
	}

	ao2_ref(buddy, -1);

	if (resource) {
		stat = resource->status;
		ao2_ref(resource, -1);
	} else {
		ast_log(LOG_NOTICE, "Resource %s of buddy %s was not found.\n", jid.resource, jid.screenname);
	}

	snprintf(buf, buflen, "%d", stat);

	return 0;
}

static struct ast_custom_function jabberstatus_function = {
	.name = "JABBER_STATUS",
	.read = acf_jabberstatus_read,
};

/*!
 * \brief Application to join a chat room
 * \param chan ast_channel
 * \param data  Data is sender|jid|nickname.
 * \retval 0 success
 * \retval -1 error
 */
static int xmpp_join_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	char *s, nick[XMPP_MAX_RESJIDLEN];
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(sender);
			     AST_APP_ARG(jid);
			     AST_APP_ARG(nick);
		);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires arguments (sender,jid[,nickname])\n", app_ajijoin);
		return -1;
	}
	s = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, s);
	if (args.argc < 2 || args.argc > 3) {
		ast_log(LOG_ERROR, "%s requires arguments (sender,jid[,nickname])\n", app_ajijoin);
		return -1;
	}

	if (strchr(args.jid, '/')) {
		ast_log(LOG_ERROR, "Invalid room name : resource must not be appended\n");
		return -1;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, args.sender))) {
		ast_log(LOG_ERROR, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}

	if (ast_strlen_zero(args.nick)) {
		if (ast_test_flag(&clientcfg->flags, XMPP_COMPONENT)) {
			snprintf(nick, sizeof(nick), "asterisk");
		} else {
			snprintf(nick, sizeof(nick), "%s", clientcfg->client->jid->user);
		}
	} else {
		snprintf(nick, sizeof(nick), "%s", args.nick);
	}

	if (!ast_strlen_zero(args.jid) && strchr(args.jid, '@')) {
		ast_xmpp_chatroom_join(clientcfg->client, args.jid, nick);
	} else {
		ast_log(LOG_ERROR, "Problem with specified jid of '%s'\n", args.jid);
	}

	return 0;
}

/*!
 * \brief Application to leave a chat room
 * \param chan ast_channel
 * \param data  Data is sender|jid|nickname.
 * \retval 0 success
 * \retval -1 error
 */
static int xmpp_leave_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	char *s, nick[XMPP_MAX_RESJIDLEN];
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(sender);
			     AST_APP_ARG(jid);
			     AST_APP_ARG(nick);
		);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires arguments (sender,jid[,nickname])\n", app_ajileave);
		return -1;
	}
	s = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, s);
	if (args.argc < 2 || args.argc > 3) {
		ast_log(LOG_ERROR, "%s requires arguments (sender,jid[,nickname])\n", app_ajileave);
		return -1;
	}

	if (strchr(args.jid, '/')) {
		ast_log(LOG_ERROR, "Invalid room name, resource must not be appended\n");
		return -1;
	}

	if (ast_strlen_zero(args.jid) || !strchr(args.jid, '@')) {
		ast_log(LOG_ERROR, "No jabber ID specified\n");
		return -1;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, args.sender))) {
		ast_log(LOG_ERROR, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}

	if (ast_strlen_zero(args.nick)) {
		if (ast_test_flag(&clientcfg->flags, XMPP_COMPONENT)) {
			snprintf(nick, sizeof(nick), "asterisk");
		} else {
			snprintf(nick, sizeof(nick), "%s", clientcfg->client->jid->user);
		}
	} else {
		snprintf(nick, sizeof(nick), "%s", args.nick);
	}

	ast_xmpp_chatroom_leave(clientcfg->client, args.jid, nick);

	return 0;
}

/*!
 * \internal
 * \brief Dial plan function to send a message.
 * \param chan ast_channel
 * \param data  Data is account,jid,message.
 * \retval 0 success
 * \retval -1 failure
 */
static int xmpp_send_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	char *s;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(sender);
			     AST_APP_ARG(recipient);
			     AST_APP_ARG(message);
		);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires arguments (account,jid,message)\n", app_ajisend);
		return -1;
	}
	s = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, s);

	if ((args.argc < 3) || ast_strlen_zero(args.message) || !strchr(args.recipient, '@')) {
		ast_log(LOG_WARNING, "%s requires arguments (account,jid,message)\n", app_ajisend);
		return -1;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, args.sender))) {
		ast_log(LOG_WARNING, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}

	ast_xmpp_client_send_message(clientcfg->client, args.recipient, args.message);

	return 0;
}

/*!
 * \brief Application to send a message to a groupchat.
 * \param chan ast_channel
 * \param data  Data is sender|groupchat|message.
 * \retval 0 success
 * \retval -1 error
 */
static int xmpp_sendgroup_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	char *s, nick[XMPP_MAX_RESJIDLEN];
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(sender);
			     AST_APP_ARG(groupchat);
			     AST_APP_ARG(message);
			     AST_APP_ARG(nick);
		);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires arguments (sender,groupchatid,message[,nickname])\n", app_ajisendgroup);
		return -1;
	}
	s = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, s);
	if ((args.argc < 3) || (args.argc > 4) || ast_strlen_zero(args.message) || !strchr(args.groupchat, '@')) {
		ast_log(LOG_ERROR, "%s requires arguments (sender,groupchatid,message[,nickname])\n", app_ajisendgroup);
		return -1;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, args.sender))) {
		ast_log(LOG_ERROR, "Could not find sender connection: '%s'\n", args.sender);
		return -1;
	}

	if (ast_strlen_zero(args.nick) || args.argc == 3) {
		if (ast_test_flag(&clientcfg->flags, XMPP_COMPONENT)) {
			snprintf(nick, sizeof(nick), "asterisk");
		} else {
			snprintf(nick, sizeof(nick), "%s", clientcfg->client->jid->user);
		}
	} else {
		snprintf(nick, sizeof(nick), "%s", args.nick);
	}

	ast_xmpp_chatroom_send(clientcfg->client, nick, args.groupchat, args.message);

	return 0;
}

/*!
 * \internal
 * \brief Dial plan function to receive a message.
 * \param channel The associated ast_channel, if there is one
 * \param data The account, JID, and optional timeout
 * timeout.
 * \retval 0 success
 * \retval -1 failure
 */
static int acf_jabberreceive_read(struct ast_channel *chan, const char *name, char *data, char *buf, size_t buflen)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	char *parse = NULL;
	int timeout, jidlen, resourcelen, found = 0;
	struct timeval start;
	long diff = 0;
	struct ast_xmpp_message *message;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(account);
			     AST_APP_ARG(jid);
			     AST_APP_ARG(timeout);
		);
	AST_DECLARE_APP_ARGS(jid,
			     AST_APP_ARG(screenname);
			     AST_APP_ARG(resource);
		);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires arguments (account,jid[,timeout])\n", name);
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 2 || args.argc > 3) {
		ast_log(LOG_WARNING, "%s requires arguments (account,jid[,timeout])\n", name);
		return -1;
	}

	parse = ast_strdupa(args.jid);
	AST_NONSTANDARD_APP_ARGS(jid, parse, '/');
	if (jid.argc < 1 || jid.argc > 2 || strlen(args.jid) > XMPP_MAX_JIDLEN) {
		ast_log(LOG_WARNING, "Invalid JID : %s\n", parse);
		return -1;
	}

	if (ast_strlen_zero(args.timeout)) {
		timeout = 20;
	} else {
		sscanf(args.timeout, "%d", &timeout);
		if (timeout <= 0) {
			ast_log(LOG_WARNING, "Invalid timeout specified: '%s'\n", args.timeout);
			return -1;
		}
	}

	jidlen = strlen(jid.screenname);
	resourcelen = ast_strlen_zero(jid.resource) ? 0 : strlen(jid.resource);

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, args.account))) {
		ast_log(LOG_WARNING, "Could not find client %s, exiting\n", args.account);
		return -1;
	}

	ast_debug(3, "Waiting for an XMPP message from %s\n", args.jid);

	start = ast_tvnow();

	if (chan && ast_autoservice_start(chan) < 0) {
		ast_log(LOG_WARNING, "Cannot start autoservice for channel %s\n", ast_channel_name(chan));
		return -1;
	}

	/* search the messages list, grab the first message that matches with
	 * the from JID we're expecting, and remove it from the messages list */
	while (diff < timeout) {
		struct timespec ts = { 0, };
		struct timeval wait;
		int res = 0;

		wait = ast_tvadd(start, ast_tv(timeout, 0));
		ts.tv_sec = wait.tv_sec;
		ts.tv_nsec = wait.tv_usec * 1000;

		/* wait up to timeout seconds for an incoming message */
		ast_mutex_lock(&messagelock);
		if (AST_LIST_EMPTY(&clientcfg->client->messages)) {
			res = ast_cond_timedwait(&message_received_condition, &messagelock, &ts);
		}
		ast_mutex_unlock(&messagelock);
		if (res == ETIMEDOUT) {
			ast_debug(3, "No message received from %s in %d seconds\n", args.jid, timeout);
			break;
		}

		AST_LIST_LOCK(&clientcfg->client->messages);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&clientcfg->client->messages, message, list) {
			if (jid.argc == 1) {
				/* no resource provided, compare bare JIDs */
				if (strncasecmp(jid.screenname, message->from, jidlen)) {
					continue;
				}
			} else {
				/* resource appended, compare bare JIDs and resources */
				char *resource = strchr(message->from, '/');
				if (!resource || strlen(resource) == 0) {
					ast_log(LOG_WARNING, "Remote JID has no resource : %s\n", message->from);
					if (strncasecmp(jid.screenname, message->from, jidlen)) {
						continue;
					}
				} else {
					resource ++;
					if (strncasecmp(jid.screenname, message->from, jidlen) || strncmp(jid.resource, resource, resourcelen)) {
						continue;
					}
				}
			}
			/* check if the message is not too old */
			if (ast_tvdiff_sec(ast_tvnow(), message->arrived) >= clientcfg->message_timeout) {
				ast_debug(3, "Found old message from %s, deleting it\n", message->from);
				AST_LIST_REMOVE_CURRENT(list);
				xmpp_message_destroy(message);
				continue;
			}
			found = 1;
			ast_copy_string(buf, message->message, buflen);
			AST_LIST_REMOVE_CURRENT(list);
			xmpp_message_destroy(message);
			break;
		}
		AST_LIST_TRAVERSE_SAFE_END;
		AST_LIST_UNLOCK(&clientcfg->client->messages);
		if (found) {
			break;
		}

		/* check timeout */
		diff = ast_tvdiff_ms(ast_tvnow(), start);
	}

	if (chan && ast_autoservice_stop(chan) < 0) {
		ast_log(LOG_WARNING, "Cannot stop autoservice for channel %s\n", ast_channel_name(chan));
	}

	/* return if we timed out */
	if (!found) {
		ast_log(LOG_NOTICE, "Timed out : no message received from %s\n", args.jid);
		return -1;
	}

	return 0;
}

static struct ast_custom_function jabberreceive_function = {
	.name = "JABBER_RECEIVE",
	.read = acf_jabberreceive_read,
};

/*!
 * \internal
 * \brief Delete old messages from a given JID
 * Messages stored during more than client->message_timeout are deleted
 * \param client Asterisk's XMPP client
 * \param from the JID we received messages from
 * \retval the number of deleted messages
 */
static int delete_old_messages(struct ast_xmpp_client *client, char *from)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	int deleted = 0, isold = 0;
	struct ast_xmpp_message *message = NULL;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name))) {
		return 0;
	}

	AST_LIST_LOCK(&client->messages);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&client->messages, message, list) {
		if (isold) {
			if (!from || !strncasecmp(from, message->from, strlen(from))) {
				AST_LIST_REMOVE_CURRENT(list);
				xmpp_message_destroy(message);
				deleted++;
			}
		} else if (ast_tvdiff_sec(ast_tvnow(), message->arrived) >= clientcfg->message_timeout) {
			isold = 1;
			if (!from || !strncasecmp(from, message->from, strlen(from))) {
				AST_LIST_REMOVE_CURRENT(list);
				xmpp_message_destroy(message);
				deleted++;
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&client->messages);

	return deleted;
}

static int xmpp_send_cb(const struct ast_msg *msg, const char *to, const char *from)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	char *sender, *dest;
	int res;

	sender = ast_strdupa(from);
	strsep(&sender, ":");
	dest = ast_strdupa(to);
	strsep(&dest, ":");

	if (ast_strlen_zero(sender)) {
		ast_log(LOG_ERROR, "MESSAGE(from) of '%s' invalid for XMPP\n", from);
		return -1;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, sender))) {
		ast_log(LOG_WARNING, "Could not finder account to send from as '%s'\n", sender);
		return -1;
	}

	ast_debug(1, "Sending message to '%s' from '%s'\n", dest, clientcfg->name);

	if ((res = ast_xmpp_client_send_message(clientcfg->client, dest, ast_msg_get_body(msg))) != IKS_OK) {
		ast_log(LOG_WARNING, "Failed to send XMPP message (%d).\n", res);
	}

	return res == IKS_OK ? 0 : -1;
}

static const struct ast_msg_tech msg_tech = {
	.name = "xmpp",
	.msg_send = xmpp_send_cb,
};

/*! \brief Internal function which creates a buddy on a client */
static struct ast_xmpp_buddy *xmpp_client_create_buddy(struct ao2_container *container, const char *id)
{
	struct ast_xmpp_buddy *buddy;

	if (!(buddy = ao2_alloc(sizeof(*buddy), xmpp_buddy_destructor))) {
		return NULL;
	}

	if (!(buddy->resources = ao2_container_alloc(RESOURCE_BUCKETS, xmpp_resource_hash, xmpp_resource_cmp))) {
		ao2_ref(buddy, -1);
		return NULL;
	}

	ast_copy_string(buddy->id, id, sizeof(buddy->id));

	/* Assume we need to subscribe to get their presence until proven otherwise */
	buddy->subscribe = 1;

	ao2_link(container, buddy);

	return buddy;
}

/*! \brief Helper function which unsubscribes a user and removes them from the roster */
static int xmpp_client_unsubscribe_user(struct ast_xmpp_client *client, const char *user)
{
	iks *iq, *query = NULL, *item = NULL;

	if (ast_xmpp_client_send(client, iks_make_s10n(IKS_TYPE_UNSUBSCRIBE, user,
						       "Goodbye. Your status is no longer required.\n"))) {
		return -1;
	}

	if (!(iq = iks_new("iq")) || !(query = iks_new("query")) || !(item = iks_new("item"))) {
		ast_log(LOG_WARNING, "Could not allocate memory for roster removal of '%s' from client '%s'\n",
			user, client->name);
		goto done;
	}

	iks_insert_attrib(iq, "from", client->jid->full);
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(query, "xmlns", "jabber:iq:roster");
	iks_insert_node(iq, query);
	iks_insert_attrib(item, "jid", user);
	iks_insert_attrib(item, "subscription", "remove");
	iks_insert_node(query, item);

	if (ast_xmpp_client_send(client, iq)) {
		ast_log(LOG_WARNING, "Could not send roster removal request of '%s' from client '%s'\n",
			user, client->name);
	}

done:
	iks_delete(item);
	iks_delete(query);
	iks_delete(iq);

	return 0;
}

/*! \brief Callback function which subscribes to a user if needed */
static int xmpp_client_subscribe_user(void *obj, void *arg, int flags)
{
	struct ast_xmpp_buddy *buddy = obj;
	struct ast_xmpp_client *client = arg;

	if (!buddy->subscribe) {
		return 0;
	}

	if (ast_xmpp_client_send(client, iks_make_s10n(IKS_TYPE_SUBSCRIBE, buddy->id,
						       "Greetings! I am the Asterisk Open Source PBX and I want to subscribe to your presence\n"))) {
		ast_log(LOG_WARNING, "Could not send subscription for '%s' on client '%s'\n",
			buddy->id, client->name);
	}

	buddy->subscribe = 0;

	return 0;
}

/*! \brief Hook function called when roster is received from server */
static int xmpp_roster_hook(void *data, ikspak *pak)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_client *client = data;
	iks *item;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name))) {
		return IKS_FILTER_EAT;
	}

	for (item = iks_child(pak->query); item; item = iks_next(item)) {
		struct ast_xmpp_buddy *buddy;

		if (iks_strcmp(iks_name(item), "item")) {
			continue;
		}

		if (!(buddy = ao2_find(client->buddies, iks_find_attrib(item, "jid"), OBJ_KEY))) {
			if (ast_test_flag(&clientcfg->flags, XMPP_AUTOPRUNE)) {
				/* The buddy has not been specified in the configuration file, we no longer
				 * want them on our buddy list or to receive their presence. */
				if (xmpp_client_unsubscribe_user(client, iks_find_attrib(item, "jid"))) {
					ast_log(LOG_ERROR, "Could not unsubscribe user '%s' on client '%s'\n",
						iks_find_attrib(item, "jid"), client->name);
				}
				continue;
			}

			if (!(buddy = xmpp_client_create_buddy(client->buddies, iks_find_attrib(item, "jid")))) {
				ast_log(LOG_ERROR, "Could not allocate buddy '%s' on client '%s'\n", iks_find_attrib(item, "jid"),
					client->name);
				continue;
			}
		}

		/* Determine if we need to subscribe to their presence or not */
		if (!iks_strcmp(iks_find_attrib(item, "subscription"), "none") ||
		    !iks_strcmp(iks_find_attrib(item, "subscription"), "from")) {
			buddy->subscribe = 1;
		} else {
			buddy->subscribe = 0;
		}

		ao2_ref(buddy, -1);
	}

	/* If autoregister is enabled we need to go through every buddy that we need to subscribe to and do so */
	if (ast_test_flag(&clientcfg->flags, XMPP_AUTOREGISTER)) {
		ao2_callback(client->buddies, OBJ_NODATA | OBJ_MULTIPLE, xmpp_client_subscribe_user, client);
	}

	xmpp_client_change_state(client, XMPP_STATE_CONNECTED);

	return IKS_FILTER_EAT;
}

/*! \brief Internal function which changes the presence status of an XMPP client */
static void xmpp_client_set_presence(struct ast_xmpp_client *client, const char *to, const char *from, int level, const char *desc)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	iks *presence = NULL, *cnode = NULL, *priority = NULL;
	char priorityS[10];

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) ||
	    !(presence = iks_make_pres(level, desc)) || !(cnode = iks_new("c")) || !(priority = iks_new("priority"))) {
		ast_log(LOG_ERROR, "Unable to allocate stanzas for setting presence status for client '%s'\n", client->name);
		goto done;
	}

	if (!ast_strlen_zero(to)) {
		iks_insert_attrib(presence, "to", to);
	}

	if (!ast_strlen_zero(from)) {
		iks_insert_attrib(presence, "from", from);
	}

	snprintf(priorityS, sizeof(priorityS), "%d", clientcfg->priority);
	iks_insert_cdata(priority, priorityS, strlen(priorityS));
	iks_insert_node(presence, priority);
	iks_insert_attrib(cnode, "node", "http://www.asterisk.org/xmpp/client/caps");
	iks_insert_attrib(cnode, "ver", "asterisk-xmpp");
	iks_insert_attrib(cnode, "ext", "voice-v1 video-v1 camera-v1");
	iks_insert_attrib(cnode, "xmlns", "http://jabber.org/protocol/caps");
	iks_insert_node(presence, cnode);
	ast_xmpp_client_send(client, presence);

done:
	iks_delete(cnode);
	iks_delete(presence);
	iks_delete(priority);
}

/*! \brief Hook function called when client receives a service discovery get message */
static int xmpp_client_service_discovery_get_hook(void *data, ikspak *pak)
{
	struct ast_xmpp_client *client = data;
	iks *iq, *disco = NULL, *ident = NULL, *google = NULL, *jingle = NULL, *ice = NULL, *rtp = NULL, *audio = NULL, *video = NULL, *query = NULL;

	if (!(iq = iks_new("iq")) || !(query = iks_new("query")) || !(ident = iks_new("identity")) || !(disco = iks_new("feature")) ||
	    !(google = iks_new("feature")) || !(jingle = iks_new("feature")) || !(ice = iks_new("feature")) || !(rtp = iks_new("feature")) ||
	    !(audio = iks_new("feature")) || !(video = iks_new("feature"))) {
		ast_log(LOG_ERROR, "Could not allocate memory for responding to service discovery request from '%s' on client '%s'\n",
			pak->from->full, client->name);
		goto end;
	}

	iks_insert_attrib(iq, "from", client->jid->full);

	if (pak->from) {
		iks_insert_attrib(iq, "to", pak->from->full);
	}

	iks_insert_attrib(iq, "type", "result");
	iks_insert_attrib(iq, "id", pak->id);
	iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#info");
	iks_insert_attrib(ident, "category", "client");
	iks_insert_attrib(ident, "type", "pc");
	iks_insert_attrib(ident, "name", "asterisk");
	iks_insert_attrib(disco, "var", "http://jabber.org/protocol/disco#info");

	iks_insert_attrib(google, "var", "http://www.google.com/xmpp/protocol/voice/v1");
	iks_insert_attrib(jingle, "var", "urn:xmpp:jingle:1");
	iks_insert_attrib(ice, "var", "urn:xmpp:jingle:transports:ice-udp:1");
	iks_insert_attrib(rtp, "var", "urn:xmpp:jingle:apps:rtp:1");
	iks_insert_attrib(audio, "var", "urn:xmpp:jingle:apps:rtp:audio");
	iks_insert_attrib(video, "var", "urn:xmpp:jingle:apps:rtp:video");
	iks_insert_node(iq, query);
	iks_insert_node(query, ident);
	iks_insert_node(query, google);
	iks_insert_node(query, disco);
	iks_insert_node(query, jingle);
	iks_insert_node(query, ice);
	iks_insert_node(query, rtp);
	iks_insert_node(query, audio);
	iks_insert_node(query, video);
	ast_xmpp_client_send(client, iq);

end:
	iks_delete(query);
	iks_delete(video);
	iks_delete(audio);
	iks_delete(rtp);
	iks_delete(ice);
	iks_delete(jingle);
	iks_delete(google);
	iks_delete(ident);
	iks_delete(disco);
	iks_delete(iq);

	return IKS_FILTER_EAT;
}

/*! \brief Hook function called when client receives a service discovery result message */
static int xmpp_client_service_discovery_result_hook(void *data, ikspak *pak)
{
	struct ast_xmpp_client *client = data;
	struct ast_xmpp_buddy *buddy;
	struct ast_xmpp_resource *resource;

	if (!(buddy = ao2_find(client->buddies, pak->from->partial, OBJ_KEY))) {
		return IKS_FILTER_EAT;
	}

	if (!(resource = ao2_callback(buddy->resources, 0, xmpp_resource_cmp, pak->from->resource))) {
		ao2_ref(buddy, -1);
		return IKS_FILTER_EAT;
	}

	ao2_lock(resource);

	if (iks_find_with_attrib(pak->query, "feature", "var", "urn:xmpp:jingle:1")) {
		resource->caps.jingle = 1;
	}

	ao2_unlock(resource);

	ao2_ref(resource, -1);
	ao2_ref(buddy, -1);

	return IKS_FILTER_EAT;
}

/*! \brief Hook function called when client finishes authenticating with the server */
static int xmpp_connect_hook(void *data, ikspak *pak)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_client *client = data;
	iks *roster;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name))) {
		return -1;
	}

	client->jid = (iks_find_cdata(pak->query, "jid")) ? iks_id_new(client->stack, iks_find_cdata(pak->query, "jid")) : client->jid;

	if (ast_test_flag(&clientcfg->flags, XMPP_DISTRIBUTE_EVENTS)) {
		xmpp_init_event_distribution(client);
	}

	if (!(roster = iks_make_iq(IKS_TYPE_GET, IKS_NS_ROSTER))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for roster request for client '%s'\n", client->name);
		return -1;
	}

	iks_filter_add_rule(client->filter, xmpp_client_service_discovery_get_hook, client, IKS_RULE_SUBTYPE, IKS_TYPE_GET, IKS_RULE_NS, "http://jabber.org/protocol/disco#info", IKS_RULE_DONE);
	iks_filter_add_rule(client->filter, xmpp_client_service_discovery_result_hook, client, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_NS, "http://jabber.org/protocol/disco#info", IKS_RULE_DONE);

	iks_insert_attrib(roster, "id", "roster");
	ast_xmpp_client_send(client, roster);

	iks_filter_remove_hook(client->filter, xmpp_connect_hook);
	iks_filter_add_rule(client->filter, xmpp_roster_hook, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, "roster", IKS_RULE_DONE);

	xmpp_client_set_presence(client, NULL, client->jid->full, clientcfg->status, clientcfg->statusmsg);
	xmpp_client_change_state(client, XMPP_STATE_ROSTER);

	return IKS_FILTER_EAT;
}

/*! \brief Logging hook function */
static void xmpp_log_hook(void *data, const char *xmpp, size_t size, int incoming)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_client *client = data;

	if (!debug && (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) || !ast_test_flag(&clientcfg->flags, XMPP_DEBUG))) {
		return;
	}

	if (!incoming) {
		ast_verbose("\n<--- XMPP sent to '%s' --->\n%s\n<------------->\n", client->name, xmpp);
	} else {
		ast_verbose("\n<--- XMPP received from '%s' --->\n%s\n<------------->\n", client->name, xmpp);
	}
}

/*! \brief Internal function which sends a raw message */
static int xmpp_client_send_raw_message(struct ast_xmpp_client *client, const char *message)
{
	int ret;
#ifdef HAVE_OPENSSL
	int len = strlen(message);

	if (xmpp_is_secure(client)) {
		ret = SSL_write(client->ssl_session, message, len);
		if (ret) {
			/* Log the message here, because iksemel's logHook is
			   unaccessible */
			xmpp_log_hook(client, message, len, 0);
			return IKS_OK;
		}
	}
#endif
	/* If needed, data will be sent unencrypted, and logHook will
	   be called inside iks_send_raw */
	ret = iks_send_raw(client->parser, message);
	if (ret != IKS_OK) {
		return ret;
	}

	return IKS_OK;
}

/*! \brief Helper function which sends an XMPP stream header to the server */
static int xmpp_send_stream_header(struct ast_xmpp_client *client, const struct ast_xmpp_client_config *cfg, const char *to)
{
	char *namespace = ast_test_flag(&cfg->flags, XMPP_COMPONENT) ? "jabber:component:accept" : "jabber:client";
	char msg[91 + strlen(namespace) + 6 + strlen(to) + 16 + 1];

	snprintf(msg, sizeof(msg), "<?xml version='1.0'?>"
		 "<stream:stream xmlns:stream='http://etherx.jabber.org/streams' xmlns='"
		 "%s' to='%s' version='1.0'>", namespace, to);

	return xmpp_client_send_raw_message(client, msg);
}

int ast_xmpp_client_send(struct ast_xmpp_client *client, iks *stanza)
{
	return xmpp_client_send_raw_message(client, iks_string(iks_stack(stanza), stanza));
}

/*! \brief Internal function called when we need to request TLS support */
static int xmpp_client_request_tls(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
	/* If the client connection is already secure we can jump straight to authenticating */
	if (xmpp_is_secure(client)) {
		xmpp_client_change_state(client, XMPP_STATE_AUTHENTICATE);
		return 0;
	}

#ifndef HAVE_OPENSSL
	ast_log(LOG_ERROR, "TLS connection for client '%s' cannot be established. OpenSSL is not available.\n", client->name);
	return -1;
#else
	if (iks_send_raw(client->parser, "<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>") == IKS_NET_TLSFAIL) {
		ast_log(LOG_ERROR, "TLS connection for client '%s' cannot be started.\n", client->name);
		return -1;
	}

	client->stream_flags |= TRY_SECURE;

	xmpp_client_change_state(client, XMPP_STATE_REQUESTED_TLS);

	return 0;
#endif
}

/*! \brief Internal function called when we receive a response to our TLS initiation request */
static int xmpp_client_requested_tls(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
#ifdef HAVE_OPENSSL
	int sock;
	long ssl_opts;
#endif

	if (!strcmp(iks_name(node), "success")) {
		/* TLS is up and working, we can move on to authenticating now */
		xmpp_client_change_state(client, XMPP_STATE_AUTHENTICATE);
		return 0;
	} else if (!strcmp(iks_name(node), "failure")) {
		/* TLS negotiation was a failure, close it on down! */
		return -1;
	} else if (strcmp(iks_name(node), "proceed")) {
		/* Ignore any other responses */
		return 0;
	}

#ifndef HAVE_OPENSSL
	ast_log(LOG_ERROR, "Somehow we managed to try to start TLS negotiation on client '%s' without OpenSSL support, disconnecting\n", client->name);
	return -1;
#else
	client->ssl_method = SSLv23_method();
	if (!(client->ssl_context = SSL_CTX_new((SSL_METHOD *) client->ssl_method))) {
		goto failure;
	}

	ssl_opts = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
	SSL_CTX_set_options(client->ssl_context, ssl_opts);

	if (!(client->ssl_session = SSL_new(client->ssl_context))) {
		goto failure;
	}

	sock = iks_fd(client->parser);
	if (!SSL_set_fd(client->ssl_session, sock)) {
		goto failure;
	}

	if (!SSL_connect(client->ssl_session)) {
		goto failure;
	}

	client->stream_flags &= (~TRY_SECURE);
	client->stream_flags |= SECURE;

	if (xmpp_send_stream_header(client, cfg, client->jid->server) != IKS_OK) {
		ast_log(LOG_ERROR, "TLS connection for client '%s' could not be established, failed to send stream header after negotiation\n",
			client->name);
		return -1;
	}

	ast_debug(1, "TLS connection for client '%s' started with server\n", client->name);

	xmpp_client_change_state(client, XMPP_STATE_AUTHENTICATE);

	return 0;

failure:
	ast_log(LOG_ERROR, "TLS connection for client '%s' cannot be established. OpenSSL initialization failed.\n", client->name);
	return -1;
#endif
}

/*! \brief Internal function called when we need to authenticate using non-SASL */
static int xmpp_client_authenticate_digest(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
	iks *iq = NULL, *query = NULL;
	char buf[41], sidpass[100];

	if (!(iq = iks_new("iq")) || !(query = iks_insert(iq, "query"))) {
		ast_log(LOG_ERROR, "Stanzas could not be allocated for authentication on client '%s'\n", client->name);
		iks_delete(iq);
		return -1;
	}

	iks_insert_attrib(iq, "type", "set");
	iks_insert_cdata(iks_insert(query, "username"), client->jid->user, 0);
	iks_insert_cdata(iks_insert(query, "resource"), client->jid->resource, 0);

	iks_insert_attrib(query, "xmlns", "jabber:iq:auth");
	snprintf(sidpass, sizeof(sidpass), "%s%s", iks_find_attrib(node, "id"), cfg->password);
	ast_sha1_hash(buf, sidpass);
	iks_insert_cdata(iks_insert(query, "digest"), buf, 0);

	ast_xmpp_client_lock(client);
	iks_filter_add_rule(client->filter, xmpp_connect_hook, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, client->mid, IKS_RULE_DONE);
	iks_insert_attrib(iq, "id", client->mid);
	ast_xmpp_increment_mid(client->mid);
	ast_xmpp_client_unlock(client);

	iks_insert_attrib(iq, "to", client->jid->server);

	ast_xmpp_client_send(client, iq);

	iks_delete(iq);

	xmpp_client_change_state(client, XMPP_STATE_AUTHENTICATING);

	return 0;
}

/*! \brief Internal function called when we need to authenticate using SASL */
static int xmpp_client_authenticate_sasl(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
	int features, len = strlen(client->jid->user) + strlen(cfg->password) + 3;
	iks *auth;
	char combined[len];
	char base64[(len + 2) * 4 / 3];

	if (strcmp(iks_name(node), "stream:features")) {
		/* Ignore anything beside stream features */
		return 0;
	}

	features = iks_stream_features(node);

	if ((features & IKS_STREAM_SASL_MD5) && !xmpp_is_secure(client)) {
		if (iks_start_sasl(client->parser, IKS_SASL_DIGEST_MD5, (char*)client->jid->user, (char*)cfg->password) != IKS_OK) {
			ast_log(LOG_ERROR, "Tried to authenticate client '%s' using SASL DIGEST-MD5 but could not\n", client->name);
			return -1;
		}

		xmpp_client_change_state(client, XMPP_STATE_AUTHENTICATING);
		return 0;
	}

	/* Our only other available option is plain so if they don't support it, bail out now */
	if (!(features & IKS_STREAM_SASL_PLAIN)) {
		ast_log(LOG_ERROR, "Tried to authenticate client '%s' using SASL PLAIN but server does not support it\n", client->name);
		return -1;
	}

	if (!(auth = iks_new("auth"))) {
		ast_log(LOG_ERROR, "Could not allocate memory for SASL PLAIN authentication for client '%s'\n", client->name);
		return -1;
	}

	iks_insert_attrib(auth, "xmlns", IKS_NS_XMPP_SASL);
	iks_insert_attrib(auth, "mechanism", "PLAIN");

	if (strchr(client->jid->user, '/')) {
		char *user = ast_strdupa(client->jid->user);

		snprintf(combined, sizeof(combined), "%c%s%c%s", 0, strsep(&user, "/"), 0, cfg->password);
	} else {
		snprintf(combined, sizeof(combined), "%c%s%c%s", 0, client->jid->user, 0, cfg->password);
	}

	ast_base64encode(base64, (const unsigned char *) combined, len - 1, (len + 2) * 4 / 3);
	iks_insert_cdata(auth, base64, 0);

	ast_xmpp_client_send(client, auth);

	iks_delete(auth);

	xmpp_client_change_state(client, XMPP_STATE_AUTHENTICATING);

	return 0;
}

/*! \brief Internal function called when we need to authenticate */
static int xmpp_client_authenticate(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
	return ast_test_flag(&cfg->flags, XMPP_USESASL) ? xmpp_client_authenticate_sasl(client, cfg, type, node) : xmpp_client_authenticate_digest(client, cfg, type, node);
}

/*! \brief Internal function called when we are authenticating */
static int xmpp_client_authenticating(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
	int features;

	if (!strcmp(iks_name(node), "success")) {
		/* Authentication was a success, yay! */
		xmpp_send_stream_header(client, cfg, client->jid->server);

		return 0;
	} else if (!strcmp(iks_name(node), "failure")) {
		/* Authentication was a bust, disconnect and reconnect later */
		return -1;
	} else if (strcmp(iks_name(node), "stream:features")) {
		/* Ignore any other responses */
		return 0;
	}

	features = iks_stream_features(node);

	if (features & IKS_STREAM_BIND) {
		iks *auth;

		if (!(auth = iks_make_resource_bind(client->jid))) {
			ast_log(LOG_ERROR, "Failed to allocate memory for stream bind on client '%s'\n", client->name);
			return -1;
		}

		ast_xmpp_client_lock(client);
		iks_insert_attrib(auth, "id", client->mid);
		ast_xmpp_increment_mid(client->mid);
		ast_xmpp_client_unlock(client);
		ast_xmpp_client_send(client, auth);

		iks_delete(auth);

		iks_filter_add_rule(client->filter, xmpp_connect_hook, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_DONE);
	}

	if (features & IKS_STREAM_SESSION) {
		iks *auth;

		if (!(auth = iks_make_session())) {
			ast_log(LOG_ERROR, "Failed to allocate memory for stream session on client '%s'\n", client->name);
			return -1;
		}

		iks_insert_attrib(auth, "id", "auth");
		ast_xmpp_client_lock(client);
		ast_xmpp_increment_mid(client->mid);
		ast_xmpp_client_unlock(client);
		ast_xmpp_client_send(client, auth);

		iks_delete(auth);

		iks_filter_add_rule(client->filter, xmpp_connect_hook, client, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, "auth", IKS_RULE_DONE);
	}

	return 0;
}

/*! \brief Internal function called when we should authenticate as a component */
static int xmpp_component_authenticate(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
	char secret[160], shasum[320], message[344];
	ikspak *pak = iks_packet(node);

	snprintf(secret, sizeof(secret), "%s%s", pak->id, cfg->password);
	ast_sha1_hash(shasum, secret);
	snprintf(message, sizeof(message), "<handshake>%s</handshake>", shasum);

	if (xmpp_client_send_raw_message(client, message) != IKS_OK) {
		ast_log(LOG_ERROR, "Unable to send handshake for component '%s'\n", client->name);
		return -1;
	}

	xmpp_client_change_state(client, XMPP_STATE_AUTHENTICATING);

	return 0;
}

/*! \brief Hook function called when component receives a service discovery get message */
static int xmpp_component_service_discovery_get_hook(void *data, ikspak *pak)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_client *client = data;
	iks *iq = NULL, *query = NULL, *identity = NULL, *disco = NULL, *reg = NULL, *commands = NULL, *gateway = NULL;
	iks *version = NULL, *vcard = NULL, *search = NULL, *item = NULL;
	char *node;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) ||
	    !(iq = iks_new("iq")) || !(query = iks_new("query")) || !(identity = iks_new("identity")) || !(disco = iks_new("feature")) ||
	    !(reg = iks_new("feature")) || !(commands = iks_new("feature")) || !(gateway = iks_new("feature")) || !(version = iks_new("feature")) ||
	    !(vcard = iks_new("feature")) || !(search = iks_new("search")) || !(item = iks_new("item"))) {
		ast_log(LOG_ERROR, "Failed to allocate stanzas for service discovery get response to '%s' on component '%s'\n",
			pak->from->partial, client->name);
		goto done;
	}

	iks_insert_attrib(iq, "from", clientcfg->user);
	iks_insert_attrib(iq, "to", pak->from->full);
	iks_insert_attrib(iq, "id", pak->id);
	iks_insert_attrib(iq, "type", "result");

	if (!(node = iks_find_attrib(pak->query, "node"))) {
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
	} else if (!strcasecmp(node, "http://jabber.org/protocol/commands")) {
		iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#items");
		iks_insert_attrib(query, "node", "http://jabber.org/protocol/commands");
		iks_insert_attrib(item, "node", "confirmaccount");
		iks_insert_attrib(item, "name", "Confirm account");
		iks_insert_attrib(item, "jid", clientcfg->user);

		iks_insert_node(iq, query);
		iks_insert_node(query, item);
	} else if (!strcasecmp(node, "confirmaccount")) {
		iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#info");
		iks_insert_attrib(commands, "var", "http://jabber.org/protocol/commands");

		iks_insert_node(iq, query);
		iks_insert_node(query, commands);
	} else {
		ast_debug(3, "Unsupported service discovery info request received with node '%s' on component '%s'\n",
			  node, client->name);
		goto done;
	}

	if (ast_xmpp_client_send(client, iq)) {
		ast_log(LOG_WARNING, "Could not send response to service discovery request on component '%s'\n",
			client->name);
	}

done:
	iks_delete(search);
	iks_delete(vcard);
	iks_delete(version);
	iks_delete(gateway);
	iks_delete(commands);
	iks_delete(reg);
	iks_delete(disco);
	iks_delete(identity);
	iks_delete(query);
	iks_delete(iq);

	return IKS_FILTER_EAT;
}

/*! \brief Hook function called when the component is queried about registration */
static int xmpp_component_register_get_hook(void *data, ikspak *pak)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_client *client = data;
	iks *iq = NULL, *query = NULL, *error = NULL, *notacceptable = NULL, *instructions = NULL;
	struct ast_xmpp_buddy *buddy;
	char *node;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) ||
	    !(iq = iks_new("iq")) || !(query = iks_new("query")) || !(error = iks_new("error")) || !(notacceptable = iks_new("not-acceptable")) ||
	    !(instructions = iks_new("instructions"))) {
		ast_log(LOG_ERROR, "Failed to allocate stanzas for register get response to '%s' on component '%s'\n",
			pak->from->partial, client->name);
		goto done;
	}

	iks_insert_attrib(iq, "from", clientcfg->user);
	iks_insert_attrib(iq, "to", pak->from->full);
	iks_insert_attrib(iq, "id", pak->id);
	iks_insert_attrib(iq, "type", "result");
	iks_insert_attrib(query, "xmlns", "jabber:iq:register");
	iks_insert_node(iq, query);

	if (!(buddy = ao2_find(client->buddies, pak->from->partial, OBJ_KEY))) {
		iks_insert_attrib(error, "code", "406");
		iks_insert_attrib(error, "type", "modify");
		iks_insert_attrib(notacceptable, "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas");

		iks_insert_node(iq, error);
		iks_insert_node(error, notacceptable);

		ast_log(LOG_ERROR, "Received register attempt from '%s' but buddy is not configured on component '%s'\n",
			pak->from->partial, client->name);
	} else if (!(node = iks_find_attrib(pak->query, "node"))) {
		iks_insert_cdata(instructions, "Welcome to Asterisk - the Open Source PBX.\n", 0);
		iks_insert_node(query, instructions);
		ao2_ref(buddy, -1);
	} else {
		ast_log(LOG_WARNING, "Received register get to component '%s' using unsupported node '%s' from '%s'\n",
			client->name, node, pak->from->partial);
		ao2_ref(buddy, -1);
		goto done;
	}

	if (ast_xmpp_client_send(client, iq)) {
		ast_log(LOG_WARNING, "Could not send response to '%s' for received register get on component '%s'\n",
			pak->from->partial, client->name);
	}

done:
	iks_delete(instructions);
	iks_delete(notacceptable);
	iks_delete(error);
	iks_delete(query);
	iks_delete(iq);

	return IKS_FILTER_EAT;
}

/*! \brief Hook function called when someone registers to the component */
static int xmpp_component_register_set_hook(void *data, ikspak *pak)
{
	struct ast_xmpp_client *client = data;
	iks *iq, *presence = NULL, *x = NULL;

	if (!(iq = iks_new("iq")) || !(presence = iks_new("presence")) || !(x = iks_new("x"))) {
		ast_log(LOG_ERROR, "Failed to allocate stanzas for register set response to '%s' on component '%s'\n",
			pak->from->partial, client->name);
		goto done;
	}

	iks_insert_attrib(iq, "from", client->jid->full);
	iks_insert_attrib(iq, "to", pak->from->full);
	iks_insert_attrib(iq, "id", pak->id);
	iks_insert_attrib(iq, "type", "result");

	if (ast_xmpp_client_send(client, iq)) {
		ast_log(LOG_WARNING, "Could not send response to '%s' for received register set on component '%s'\n",
			pak->from->partial, client->name);
		goto done;
	}

	iks_insert_attrib(presence, "from", client->jid->full);
	iks_insert_attrib(presence, "to", pak->from->partial);
	ast_xmpp_client_lock(client);
	iks_insert_attrib(presence, "id", client->mid);
	ast_xmpp_increment_mid(client->mid);
	ast_xmpp_client_unlock(client);
	iks_insert_attrib(presence, "type", "subscribe");
	iks_insert_attrib(x, "xmlns", "vcard-temp:x:update");

	iks_insert_node(presence, x);

	if (ast_xmpp_client_send(client, presence)) {
		ast_log(LOG_WARNING, "Could not send subscription to '%s' on component '%s'\n",
			pak->from->partial, client->name);
	}

done:
	iks_delete(x);
	iks_delete(presence);
	iks_delete(iq);

	return IKS_FILTER_EAT;
}

/*! \brief Hook function called when we receive a service discovery items request */
static int xmpp_component_service_discovery_items_hook(void *data, ikspak *pak)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_client *client = data;
	iks *iq = NULL, *query = NULL, *item = NULL, *feature = NULL;
	char *node;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name)) ||
	    !(iq = iks_new("iq")) || !(query = iks_new("query")) || !(item = iks_new("item")) || !(feature = iks_new("feature"))) {
		ast_log(LOG_ERROR, "Failed to allocate stanzas for service discovery items response to '%s' on component '%s'\n",
			pak->from->partial, client->name);
		goto done;
	}

	iks_insert_attrib(iq, "from", clientcfg->user);
	iks_insert_attrib(iq, "to", pak->from->full);
	iks_insert_attrib(iq, "id", pak->id);
	iks_insert_attrib(iq, "type", "result");
	iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#items");
	iks_insert_node(iq, query);

	if (!(node = iks_find_attrib(pak->query, "node"))) {
		iks_insert_attrib(item, "node", "http://jabber.org/protocol/commands");
		iks_insert_attrib(item, "name", "Asterisk Commands");
		iks_insert_attrib(item, "jid", clientcfg->user);

		iks_insert_node(query, item);
	} else if (!strcasecmp(node, "http://jabber.org/protocol/commands")) {
		iks_insert_attrib(query, "node", "http://jabber.org/protocol/commands");
	} else {
		ast_log(LOG_WARNING, "Received service discovery items request to component '%s' using unsupported node '%s' from '%s'\n",
			client->name, node, pak->from->partial);
		goto done;
	}

	if (ast_xmpp_client_send(client, iq)) {
		ast_log(LOG_WARNING, "Could not send response to service discovery items request from '%s' on component '%s'\n",
			pak->from->partial, client->name);
	}

done:
	iks_delete(feature);
	iks_delete(item);
	iks_delete(query);
	iks_delete(iq);

	return IKS_FILTER_EAT;
}

/*! \brief Internal function called when we authenticated as a component */
static int xmpp_component_authenticating(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, int type, iks *node)
{
	if (strcmp(iks_name(node), "handshake")) {
		ast_log(LOG_ERROR, "Failed to authenticate component '%s'\n", client->name);
		return -1;
	}

	iks_filter_add_rule(client->filter, xmpp_component_service_discovery_items_hook, client, IKS_RULE_NS, "http://jabber.org/protocol/disco#items", IKS_RULE_DONE);

	iks_filter_add_rule(client->filter, xmpp_component_service_discovery_get_hook, client, IKS_RULE_SUBTYPE, IKS_TYPE_GET, IKS_RULE_NS, "http://jabber.org/protocol/disco#info", IKS_RULE_DONE);

	/* This uses the client service discovery result hook on purpose, as the code is common between both */
	iks_filter_add_rule(client->filter, xmpp_client_service_discovery_result_hook, client, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_NS, "http://jabber.org/protocol/disco#info", IKS_RULE_DONE);

	iks_filter_add_rule(client->filter, xmpp_component_register_get_hook, client, IKS_RULE_SUBTYPE, IKS_TYPE_GET, IKS_RULE_NS, "jabber:iq:register", IKS_RULE_DONE);
	iks_filter_add_rule(client->filter, xmpp_component_register_set_hook, client, IKS_RULE_SUBTYPE, IKS_TYPE_SET, IKS_RULE_NS, "jabber:iq:register", IKS_RULE_DONE);

	xmpp_client_change_state(client, XMPP_STATE_CONNECTED);

	return 0;
}

/*! \brief Internal function called when a message is received */
static int xmpp_pak_message(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, iks *node, ikspak *pak)
{
	struct ast_xmpp_message *message;
	char *body;
	int deleted = 0;

	ast_debug(3, "XMPP client '%s' received a message\n", client->name);

	if (!(body = iks_find_cdata(pak->x, "body"))) {
		/* Message contains no body, ignore it. */
		return 0;
	}

	if (!(message = ast_calloc(1, sizeof(*message)))) {
		return -1;
	}

	message->arrived = ast_tvnow();

	message->message = ast_strdup(body);

	ast_copy_string(message->id, S_OR(pak->id, ""), sizeof(message->id));
	message->from = !ast_strlen_zero(pak->from->full) ? ast_strdup(pak->from->full) : NULL;

	if (ast_test_flag(&cfg->flags, XMPP_SEND_TO_DIALPLAN)) {
		struct ast_msg *msg;
		struct ast_xmpp_buddy *buddy;

		if ((msg = ast_msg_alloc())) {
			int res;

			ast_xmpp_client_lock(client);

			buddy = ao2_find(client->buddies, pak->from->partial, OBJ_KEY | OBJ_NOLOCK);

			res = ast_msg_set_to(msg, "xmpp:%s", cfg->user);
			res |= ast_msg_set_from(msg, "xmpp:%s", message->from);
			res |= ast_msg_set_body(msg, "%s", message->message);
			res |= ast_msg_set_context(msg, "%s", cfg->context);
			res |= ast_msg_set_tech(msg, "%s", "XMPP");
			res |= ast_msg_set_endpoint(msg, "%s", client->name);

			if (buddy) {
				res |= ast_msg_set_var(msg, "XMPP_BUDDY", buddy->id);
			}

			ao2_cleanup(buddy);

			ast_xmpp_client_unlock(client);

			if (res) {
				ast_msg_destroy(msg);
			} else {
				ast_msg_queue(msg);
			}
		}
	}

	/* remove old messages received from this JID
	 * and insert received message */
	deleted = delete_old_messages(client, pak->from->partial);
	ast_debug(3, "Deleted %d messages for client %s from JID %s\n", deleted, client->name, pak->from->partial);
	AST_LIST_LOCK(&client->messages);
	AST_LIST_INSERT_HEAD(&client->messages, message, list);
	AST_LIST_UNLOCK(&client->messages);

	/* wake up threads waiting for messages */
	ast_mutex_lock(&messagelock);
	ast_cond_broadcast(&message_received_condition);
	ast_mutex_unlock(&messagelock);

	return 0;
}

/*! \brief Helper function which sends a discovery information request to a user */
static int xmpp_client_send_disco_info_request(struct ast_xmpp_client *client, const char *to, const char *from)
{
	iks *iq, *query;
	int res;

	if (!(iq = iks_new("iq")) || !(query = iks_new("query"))) {
		iks_delete(iq);
		return -1;
	}

	iks_insert_attrib(iq, "type", "get");
	iks_insert_attrib(iq, "to", to);
	iks_insert_attrib(iq, "from", from);
	ast_xmpp_client_lock(client);
	iks_insert_attrib(iq, "id", client->mid);
	ast_xmpp_increment_mid(client->mid);
	ast_xmpp_client_unlock(client);
	iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#info");
	iks_insert_node(iq, query);

	res = ast_xmpp_client_send(client, iq);

	iks_delete(query);
	iks_delete(iq);

	return res;
}

/*! \brief Callback function which returns when the resource is available */
static int xmpp_resource_is_available(void *obj, void *arg, int flags)
{
	struct ast_xmpp_resource *resource = obj;

	return (resource->status == IKS_SHOW_AVAILABLE) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Helper function which sends a ping request to a server */
static int xmpp_ping_request(struct ast_xmpp_client *client, const char *to, const char *from)
{
	iks *iq, *ping;
	int res;
	
	ast_debug(2, "JABBER: Sending Keep-Alive Ping for client '%s'\n", client->name);

	if (!(iq = iks_new("iq")) || !(ping = iks_new("ping"))) {
		iks_delete(iq);
		return -1;
	}
	
	iks_insert_attrib(iq, "type", "get");
	iks_insert_attrib(iq, "to", to);
	iks_insert_attrib(iq, "from", from);
	
	ast_xmpp_client_lock(client);
	iks_insert_attrib(iq, "id", client->mid);
	ast_xmpp_increment_mid(client->mid);
	ast_xmpp_client_unlock(client);
	
	iks_insert_attrib(ping, "xmlns", "urn:xmpp:ping");
	iks_insert_node(iq, ping);
	
	res = ast_xmpp_client_send(client, iq);
	
	iks_delete(ping);
	iks_delete(iq);


	return res;
}

/*! \brief Internal function called when a presence message is received */
static int xmpp_pak_presence(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg, iks *node, ikspak *pak)
{
	struct ast_xmpp_buddy *buddy;
	struct ast_xmpp_resource *resource;
	char *type = iks_find_attrib(pak->x, "type");
	int status = pak->show ? pak->show : STATUS_DISAPPEAR;
	enum ast_device_state state = AST_DEVICE_UNAVAILABLE;

	/* If no resource is available this is a general buddy presence update, which we will ignore */
	if (!pak->from->resource) {
		return 0;
	}

	if (!(buddy = ao2_find(client->buddies, pak->from->partial, OBJ_KEY))) {
		/* Only output the message if it is not about us */
		if (strcmp(client->jid->partial, pak->from->partial)) {
			ast_log(LOG_WARNING, "Received presence information about '%s' despite not having them in roster on client '%s'\n",
				pak->from->partial, client->name);
		}
		return 0;
	}

	/* If this is a component presence probe request answer immediately with our presence status */
	if (ast_test_flag(&cfg->flags, XMPP_COMPONENT) && !ast_strlen_zero(type) && !strcasecmp(type, "probe")) {
		xmpp_client_set_presence(client, pak->from->full, iks_find_attrib(pak->x, "to"), cfg->status, cfg->statusmsg);
	}

	ao2_lock(buddy->resources);

	if (!(resource = ao2_callback(buddy->resources, OBJ_NOLOCK, xmpp_resource_cmp, pak->from->resource))) {
		/* Only create the new resource if it is not going away - in reality this should not happen */
		if (status != STATUS_DISAPPEAR) {
			if (!(resource = ao2_alloc(sizeof(*resource), xmpp_resource_destructor))) {
				ast_log(LOG_ERROR, "Could not allocate resource object for resource '%s' of buddy '%s' on client '%s'\n",
					pak->from->resource, buddy->id, client->name);
				ao2_unlock(buddy->resources);
				ao2_ref(buddy, -1);
				return 0;
			}

			ast_copy_string(resource->resource, pak->from->resource, sizeof(resource->resource));
		}
	} else {
		/* We unlink the resource in case the priority changes or in case they are going away */
		ao2_unlink_flags(buddy->resources, resource, OBJ_NOLOCK);
	}

	/* Only update the resource and add it back in if it is not going away */
	if (resource && (status != STATUS_DISAPPEAR)) {
		char *node, *ver;

		/* Try to get the XMPP spec node, and fall back to Google if not found */
		if (!(node = iks_find_attrib(iks_find(pak->x, "c"), "node"))) {
			node = iks_find_attrib(iks_find(pak->x, "caps:c"), "node");
		}

		if (!(ver = iks_find_attrib(iks_find(pak->x, "c"), "ver"))) {
			ver = iks_find_attrib(iks_find(pak->x, "caps:c"), "ver");
		}

		if (resource->description) {
			ast_free(resource->description);
		}

		if ((node && strcmp(resource->caps.node, node)) || (ver && strcmp(resource->caps.version, ver))) {
			/* For interoperability reasons, proceed even if the resource fails to provide node or version */
			if (node) {
				ast_copy_string(resource->caps.node, node, sizeof(resource->caps.node));
			}
			if (ver) {
				ast_copy_string(resource->caps.version, ver, sizeof(resource->caps.version));
			}

			/* Google Talk places the capabilities information directly in presence, so see if it is there */
			if (iks_find_with_attrib(pak->x, "c", "node", "http://www.google.com/xmpp/client/caps") ||
			    iks_find_with_attrib(pak->x, "caps:c", "node", "http://www.google.com/xmpp/client/caps") ||
			    iks_find_with_attrib(pak->x, "c", "node", "http://www.android.com/gtalk/client/caps") ||
			    iks_find_with_attrib(pak->x, "caps:c", "node", "http://www.android.com/gtalk/client/caps") ||
			    iks_find_with_attrib(pak->x, "c", "node", "http://mail.google.com/xmpp/client/caps") ||
			    iks_find_with_attrib(pak->x, "caps:c", "node", "http://mail.google.com/xmpp/client/caps")) {
				resource->caps.google = 1;
			}

			/* To discover if the buddy supports Jingle we need to query, so do so */
			if (xmpp_client_send_disco_info_request(client, pak->from->full, client->jid->full)) {
				ast_log(LOG_WARNING, "Could not send discovery information request to resource '%s' of buddy '%s' on client '%s', capabilities may be incomplete\n", resource->resource, buddy->id, client->name);
			}
		}

		resource->status = status;
		resource->description = ast_strdup(iks_find_cdata(pak->x, "status"));
		resource->priority = atoi((iks_find_cdata(pak->x, "priority")) ? iks_find_cdata(pak->x, "priority") : "0");

		ao2_link_flags(buddy->resources, resource, OBJ_NOLOCK);

		manager_event(EVENT_FLAG_USER, "JabberStatus",
			      "Account: %s\r\nJID: %s\r\nResource: %s\r\nStatus: %d\r\nPriority: %d"
			      "\r\nDescription: %s\r\n",
			      client->name, pak->from->partial, resource->resource, resource->status,
			      resource->priority, S_OR(resource->description, ""));

		ao2_ref(resource, -1);
	} else {
		/* This will get hit by presence coming in for an unknown resource, and also when a resource goes away */
		if (resource) {
			ao2_ref(resource, -1);
		}

		manager_event(EVENT_FLAG_USER, "JabberStatus",
			      "Account: %s\r\nJID: %s\r\nStatus: %u\r\n",
			      client->name, pak->from->partial, pak->show ? pak->show : IKS_SHOW_UNAVAILABLE);
	}

	/* Determine if at least one resource is available for device state purposes */
	if ((resource = ao2_callback(buddy->resources, OBJ_NOLOCK, xmpp_resource_is_available, NULL))) {
		state = AST_DEVICE_NOT_INUSE;
		ao2_ref(resource, -1);
	}

	ao2_unlock(buddy->resources);

	ao2_ref(buddy, -1);

	ast_devstate_changed(state, AST_DEVSTATE_CACHABLE, "XMPP/%s/%s", client->name, pak->from->partial);

	return 0;
}

/*! \brief Internal function called when a subscription message is received */
static int xmpp_pak_s10n(struct ast_xmpp_client *client, struct ast_xmpp_client_config *cfg,iks *node, ikspak *pak)
{
	struct ast_xmpp_buddy *buddy;

	switch (pak->subtype) {
	case IKS_TYPE_SUBSCRIBE:
		if (ast_test_flag(&cfg->flags, XMPP_AUTOREGISTER)) {
			iks *presence, *status = NULL;

			if ((presence = iks_new("presence")) && (status = iks_new("status"))) {
				iks_insert_attrib(presence, "type", "subscribed");
				iks_insert_attrib(presence, "to", pak->from->full);
				iks_insert_attrib(presence, "from", client->jid->full);

				if (pak->id) {
					iks_insert_attrib(presence, "id", pak->id);
				}

				iks_insert_cdata(status, "Asterisk has approved your subscription", 0);
				iks_insert_node(presence, status);

				if (ast_xmpp_client_send(client, presence)) {
					ast_log(LOG_ERROR, "Could not send subscription acceptance to '%s' from client '%s'\n",
						pak->from->partial, client->name);
				}
			} else {
				ast_log(LOG_ERROR, "Could not allocate presence stanzas for accepting subscription from '%s' to client '%s'\n",
					pak->from->partial, client->name);
			}

			iks_delete(status);
			iks_delete(presence);
		}

		if (ast_test_flag(&cfg->flags, XMPP_COMPONENT)) {
			xmpp_client_set_presence(client, pak->from->full, iks_find_attrib(pak->x, "to"), cfg->status, cfg->statusmsg);
		}
		/* This purposely flows through so we have the subscriber amongst our buddies */
	case IKS_TYPE_SUBSCRIBED:
		ao2_lock(client->buddies);

		if (!(buddy = ao2_find(client->buddies, pak->from->partial, OBJ_KEY | OBJ_NOLOCK))) {
			buddy = xmpp_client_create_buddy(client->buddies, pak->from->partial);
		}

		if (!buddy) {
			ast_log(LOG_WARNING, "Could not find or create buddy '%s' on client '%s'\n",
				pak->from->partial, client->name);
		} else {
			ao2_ref(buddy, -1);
		}

		ao2_unlock(client->buddies);

		break;
	default:
		break;
	}

	return 0;
}

/*! \brief Action hook for when things occur */
static int xmpp_action_hook(void *data, int type, iks *node)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	struct ast_xmpp_client *client = data;
	ikspak *pak;
	int i;

	if (!node) {
		ast_log(LOG_ERROR, "xmpp_action_hook was called without a packet\n");
		return IKS_HOOK;
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name))) {
		return IKS_HOOK;
	}

	/* If the client is disconnecting ignore everything */
	if (client->state == XMPP_STATE_DISCONNECTING) {
		return IKS_HOOK;
	}

	pak = iks_packet(node);

	/* work around iksemel's impossibility to recognize node names
	 * containing a colon. Set the namespace of the corresponding
	 * node accordingly. */
	if (iks_has_children(node) && strchr(iks_name(iks_child(node)), ':')) {
		char *node_ns = NULL;
		char attr[XMPP_MAX_ATTRLEN];
		char *node_name = iks_name(iks_child(node));
		char *aux = strchr(node_name, ':') + 1;
		snprintf(attr, strlen("xmlns:") + (strlen(node_name) - strlen(aux)), "xmlns:%s", node_name);
		node_ns = iks_find_attrib(iks_child(node), attr);
		if (node_ns) {
			pak->ns = node_ns;
			pak->query = iks_child(node);
		}
	}

	/* Process through any state handlers */
	for (i = 0; i < ARRAY_LEN(xmpp_state_handlers); i++) {
		if ((xmpp_state_handlers[i].state == client->state) && (xmpp_state_handlers[i].component == (ast_test_flag(&clientcfg->flags, XMPP_COMPONENT) ? 1 : 0))) {
			if (xmpp_state_handlers[i].handler(client, clientcfg, type, node)) {
				/* If the handler wants us to stop now, do so */
				return IKS_HOOK;
			}
			break;
		}
	}

	/* Process through any PAK handlers */
	for (i = 0; i < ARRAY_LEN(xmpp_pak_handlers); i++) {
		if (xmpp_pak_handlers[i].type == pak->type) {
			if (xmpp_pak_handlers[i].handler(client, clientcfg, node, pak)) {
				/* If the handler wants us to stop now, do so */
				return IKS_HOOK;
			}
			break;
		}
	}

	/* Send the packet through the filter in case any filters want to process it */
	iks_filter_packet(client->filter, pak);

	iks_delete(node);

	return IKS_OK;
}

int ast_xmpp_client_disconnect(struct ast_xmpp_client *client)
{
	if ((client->thread != AST_PTHREADT_NULL) && !pthread_equal(pthread_self(), client->thread)) {
		xmpp_client_change_state(client, XMPP_STATE_DISCONNECTING);
		pthread_join(client->thread, NULL);
		client->thread = AST_PTHREADT_NULL;
	}

	if (client->mwi_sub) {
		client->mwi_sub = stasis_unsubscribe(client->mwi_sub);
		xmpp_pubsub_unsubscribe(client, "message_waiting");
	}

	if (client->device_state_sub) {
		client->device_state_sub = stasis_unsubscribe(client->device_state_sub);
		xmpp_pubsub_unsubscribe(client, "device_state");
	}

#ifdef HAVE_OPENSSL
	if (client->stream_flags & SECURE) {
		SSL_shutdown(client->ssl_session);
		SSL_CTX_free(client->ssl_context);
		SSL_free(client->ssl_session);
	}

	client->stream_flags = 0;
#endif

	if (client->parser) {
		iks_disconnect(client->parser);
	}

	xmpp_client_change_state(client, XMPP_STATE_DISCONNECTED);

	return 0;
}

/*! \brief Internal function used to reconnect an XMPP client to its server */
static int xmpp_client_reconnect(struct ast_xmpp_client *client)
{
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	int res = IKS_NET_NOCONN;

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, client->name))) {
		return -1;
	}

	ast_xmpp_client_disconnect(client);

	client->timeout = 50;
	iks_parser_reset(client->parser);

	if (!client->filter && !(client->filter = iks_filter_new())) {
		ast_log(LOG_ERROR, "Could not create IKS filter for client connection '%s'\n", client->name);
		return -1;
	}

	/* If it's a component connect to user otherwise connect to server */
	res = iks_connect_via(client->parser, S_OR(clientcfg->server, client->jid->server), clientcfg->port,
			      ast_test_flag(&clientcfg->flags, XMPP_COMPONENT) ? clientcfg->user : client->jid->server);

	/* Set socket timeout options */
	setsockopt(iks_fd(client->parser), SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
	
	if (res == IKS_NET_NOCONN) {
		ast_log(LOG_ERROR, "No XMPP connection available when trying to connect client '%s'\n", client->name);
		return -1;
	} else if (res == IKS_NET_NODNS) {
		ast_log(LOG_ERROR, "No DNS available for XMPP connection when trying to connect client '%s'\n", client->name);
		return -1;
	}

	/* Depending on the configuration of the client we eiher jump to requesting TLS, or authenticating */
	xmpp_client_change_state(client, (ast_test_flag(&clientcfg->flags, XMPP_USETLS) ? XMPP_STATE_REQUEST_TLS : XMPP_STATE_AUTHENTICATE));

	return 0;
}

/*! \brief Internal function which polls on an XMPP client and receives data */
static int xmpp_io_recv(struct ast_xmpp_client *client, char *buffer, size_t buf_len, int timeout)
{
	struct pollfd pfd = { .events = POLLIN };
	int len, res;

#ifdef HAVE_OPENSSL
	if (xmpp_is_secure(client)) {
		pfd.fd = SSL_get_fd(client->ssl_session);
		if (pfd.fd < 0) {
			return -1;
		}
	} else
#endif /* HAVE_OPENSSL */
		pfd.fd = iks_fd(client->parser);

	res = ast_poll(&pfd, 1, timeout > 0 ? timeout * 1000 : -1);
	if (res > 0) {
#ifdef HAVE_OPENSSL
		if (xmpp_is_secure(client)) {
			len = SSL_read(client->ssl_session, buffer, buf_len);
		} else
#endif /* HAVE_OPENSSL */
			len = recv(pfd.fd, buffer, buf_len, 0);

		if (len > 0) {
			return len;
		} else if (len <= 0) {
			return -1;
		}
	}
	return res;
}

/*! \brief Internal function which receives data from the XMPP client connection */
static int xmpp_client_receive(struct ast_xmpp_client *client, unsigned int timeout)
{
	int len, ret, pos = 0, newbufpos = 0;
	char buf[NET_IO_BUF_SIZE - 1] = "";
	char newbuf[NET_IO_BUF_SIZE - 1] = "";
	unsigned char c;

	while (1) {
		len = xmpp_io_recv(client, buf, NET_IO_BUF_SIZE - 2, timeout);
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
			newbufpos++;
			pos++;
		}
		pos = 0;
		newbufpos = 0;

		/* Log the message here, because iksemel's logHook is
		   unaccessible */
		xmpp_log_hook(client, buf, len, 1);
		
		if(buf[0] == ' ') {
			ast_debug(1, "JABBER: Detected Google Keep Alive. "
				"Sending out Ping request for client '%s'\n", client->name);
			/* If we just send out the ping here then we will have socket
			 * read errors because the socket will timeout */
			xmpp_ping_request(client, client->jid->server, client->jid->full);
		}

		/* let iksemel deal with the string length,
		   and reset our buffer */
		ret = iks_parse(client->parser, newbuf, 0, 0);
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

/*! \brief XMPP client connection thread */
static void *xmpp_client_thread(void *data)
{
	struct ast_xmpp_client *client = data;
	int res = IKS_NET_RWERR;

	do {
		if (client->state == XMPP_STATE_DISCONNECTING) {
			ast_debug(1, "JABBER: Disconnecting client '%s'\n", client->name);
			break;
		}

		if (res == IKS_NET_RWERR || client->timeout == 0) {
			ast_debug(3, "Connecting client '%s'\n", client->name);
			if ((res = xmpp_client_reconnect(client)) != IKS_OK) {
				sleep(4);
				res = IKS_NET_RWERR;
			}
			continue;
		}

		res = xmpp_client_receive(client, 1);

		/* Decrease timeout if no data received, and delete
		 * old messages globally */
		if (res == IKS_NET_EXPIRED) {
			client->timeout--;
		}

		if (res == IKS_HOOK) {
			ast_debug(2, "JABBER: Got hook event.\n");
		} else if (res == IKS_NET_TLSFAIL) {
			ast_log(LOG_ERROR, "JABBER:  Failure in TLS.\n");
		} else if (!client->timeout && client->state == XMPP_STATE_CONNECTED) {
			RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
			RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);

			if (cfg && cfg->clients) {
				clientcfg = xmpp_config_find(cfg->clients, client->name);
			}

			if (clientcfg && ast_test_flag(&clientcfg->flags, XMPP_KEEPALIVE)) {
				res = xmpp_ping_request(client, client->jid->server, client->jid->full);
			} else {
				res = IKS_OK;
			}

			if (res == IKS_OK) {
				client->timeout = 50;
			} else {
				ast_log(LOG_WARNING, "JABBER: Network Timeout\n");
			}
		} else if (res == IKS_NET_RWERR) {
			ast_log(LOG_WARNING, "JABBER: socket read error\n");
		} else if (res == IKS_NET_NOSOCK) {
			ast_log(LOG_WARNING, "JABBER: No Socket\n");
		} else if (res == IKS_NET_NOCONN) {
			ast_log(LOG_WARNING, "JABBER: No Connection\n");
		} else if (res == IKS_NET_NODNS) {
			ast_log(LOG_WARNING, "JABBER: No DNS\n");
		} else if (res == IKS_NET_NOTSUPP) {
			ast_log(LOG_WARNING, "JABBER: Not Supported\n");
		} else if (res == IKS_NET_DROPPED) {
			ast_log(LOG_WARNING, "JABBER: Dropped?\n");
		} else if (res == IKS_NET_UNKNOWN) {
			ast_debug(5, "JABBER: Unknown\n");
		}

	} while (1);

	return NULL;
}

static int xmpp_client_config_merge_buddies(void *obj, void *arg, int flags)
{
	struct ast_xmpp_buddy *buddy1 = obj, *buddy2;
	struct ao2_container *buddies = arg;

	/* If the buddy does not already exist link it into the client buddies container */
	if (!(buddy2 = ao2_find(buddies, buddy1->id, OBJ_KEY))) {
		ao2_link(buddies, buddy1);
	} else {
		ao2_ref(buddy2, -1);
	}

	/* All buddies are unlinked from the configuration buddies container, always */
	return 1;
}

static int xmpp_client_config_post_apply(void *obj, void *arg, int flags)
{
	struct ast_xmpp_client_config *cfg = obj;

	/* Merge buddies as need be */
	ao2_callback(cfg->buddies, OBJ_MULTIPLE | OBJ_UNLINK, xmpp_client_config_merge_buddies, cfg->client->buddies);

	if (cfg->client->reconnect) {
		/* Disconnect the existing session since our role is changing, or we are starting up */
		ast_xmpp_client_disconnect(cfg->client);

		if (!(cfg->client->parser = iks_stream_new(ast_test_flag(&cfg->flags, XMPP_COMPONENT) ? "jabber:component:accept" : "jabber:client", cfg->client,
							   xmpp_action_hook))) {
			ast_log(LOG_ERROR, "Iksemel stream could not be created for client '%s' - client not active\n", cfg->name);
			return -1;
		}

		iks_set_log_hook(cfg->client->parser, xmpp_log_hook);

		/* Create a JID based on the given user, if no resource is given use the default */
		if (!strchr(cfg->user, '/') && !ast_test_flag(&cfg->flags, XMPP_COMPONENT)) {
			char resource[strlen(cfg->user) + strlen("/asterisk-xmpp") + 1];

			snprintf(resource, sizeof(resource), "%s/asterisk-xmpp", cfg->user);
			cfg->client->jid = iks_id_new(cfg->client->stack, resource);
		} else {
			cfg->client->jid = iks_id_new(cfg->client->stack, cfg->user);
		}

		if (!cfg->client->jid || ast_strlen_zero(cfg->client->jid->user)) {
			ast_log(LOG_ERROR, "Jabber identity '%s' could not be created for client '%s' - client not active\n", cfg->user, cfg->name);
			return -1;
		}

		ast_pthread_create_background(&cfg->client->thread, NULL, xmpp_client_thread, cfg->client);

		cfg->client->reconnect = 0;
	} else if (cfg->client->state == XMPP_STATE_CONNECTED) {
		/* If this client is connected update their presence status since it may have changed */
		xmpp_client_set_presence(cfg->client, NULL, cfg->client->jid->full, cfg->status, cfg->statusmsg);

		/* Subscribe to the status of any newly added buddies */
		if (ast_test_flag(&cfg->flags, XMPP_AUTOREGISTER)) {
			ao2_callback(cfg->client->buddies, OBJ_NODATA | OBJ_MULTIPLE, xmpp_client_subscribe_user, cfg->client);
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief  Send a Jabber Message via call from the Manager
 * \param s mansession Manager session
 * \param m message Message to send
 * \return  0
 */
static int manager_jabber_send(struct mansession *s, const struct message *m)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	const char *id = astman_get_header(m, "ActionID");
	const char *jabber = astman_get_header(m, "Jabber");
	const char *screenname = astman_get_header(m, "ScreenName");
	const char *message = astman_get_header(m, "Message");

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

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, jabber))) {
		astman_send_error(s, m, "Could not find Sender");
		return 0;
	}

	if (strchr(screenname, '@') && !ast_xmpp_client_send_message(clientcfg->client, screenname, message)) {
		astman_append(s, "Response: Success\r\n");
	} else {
		astman_append(s, "Response: Error\r\n");
	}

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	astman_append(s, "\r\n");

	return 0;
}

/*!
 * \brief Build the a node request
 * \param client the configured XMPP client we use to connect to a XMPP server
 * \param collection name of the collection for request
 * \return iks*
 */
static iks* xmpp_pubsub_build_node_request(struct ast_xmpp_client *client, const char *collection)
{
	iks *request = xmpp_pubsub_iq_create(client, "get"), *query;

	if (!request) {
		return NULL;
	}

	query = iks_insert(request, "query");
	iks_insert_attrib(query, "xmlns", "http://jabber.org/protocol/disco#items");

	if (collection) {
		iks_insert_attrib(query, "node", collection);
	}

	return request;
}

/*!
 * \brief Receive pubsub item lists
 * \param data pointer to ast_xmpp_client structure
 * \param pak response from pubsub diso#items query
 * \return IKS_FILTER_EAT
 */
static int xmpp_pubsub_receive_node_list(void *data, ikspak* pak)
{
	struct ast_xmpp_client *client = data;
	iks *item = NULL;

	if (iks_has_children(pak->query)) {
		item = iks_first_tag(pak->query);
		ast_verbose("Connection %s: %s\nNode name: %s\n", client->name, client->jid->partial,
			    iks_find_attrib(item, "node"));
		while ((item = iks_next_tag(item))) {
			ast_verbose("Node name: %s\n", iks_find_attrib(item, "node"));
		}
	}

	if (item) {
		iks_delete(item);
	}


	return IKS_FILTER_EAT;
}

/*!
* \brief Request item list from pubsub
* \param client the configured XMPP client we use to connect to a XMPP server
* \param collection name of the collection for request
* \return void
*/
static void xmpp_pubsub_request_nodes(struct ast_xmpp_client *client, const char *collection)
{
	iks *request = xmpp_pubsub_build_node_request(client, collection);

	if (!request) {
		ast_log(LOG_ERROR, "Could not request pubsub nodes on client '%s' - IQ could not be created\n", client->name);
		return;
	}

	iks_filter_add_rule(client->filter, xmpp_pubsub_receive_node_list, client, IKS_RULE_TYPE,
			    IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, client->mid,
			    IKS_RULE_DONE);
	ast_xmpp_client_send(client, request);
	iks_delete(request);

}

/*
 * \brief Method to expose PubSub node list via CLI.
 * \param e pointer to ast_cli_entry structure
 * \param cmd
 * \param a pointer to ast_cli_args structure
 * \return char *
 */
static char *xmpp_cli_list_pubsub_nodes(struct ast_cli_entry *e, int cmd, struct
					ast_cli_args *a)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	const char *name = NULL, *collection = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp list nodes";
		e->usage =
			"Usage: xmpp list nodes <connection> [collection]\n"
			"       Lists the user's nodes on the respective connection\n"
			"       ([connection] as configured in xmpp.conf.)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 5 || a->argc < 4) {
		return CLI_SHOWUSAGE;
	} else if (a->argc == 4 || a->argc == 5) {
		name = a->argv[3];
	}

	if (a->argc == 5) {
		collection = a->argv[4];
	}

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, name))) {
		ast_cli(a->fd, "Unable to find client '%s'!\n", name);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Listing pubsub nodes.\n");

	xmpp_pubsub_request_nodes(clientcfg->client, collection);

	return CLI_SUCCESS;
}

/*!
 * \brief Delete pubsub item lists
 * \param data pointer to ast_xmpp_client structure
 * \param pak response from pubsub diso#items query
 * \return IKS_FILTER_EAT
 */
static int xmpp_pubsub_delete_node_list(void *data, ikspak* pak)
{
	struct ast_xmpp_client *client = data;
	iks *item = NULL;

	if (iks_has_children(pak->query)) {
		item = iks_first_tag(pak->query);
		ast_log(LOG_WARNING, "Connection: %s  Node name: %s\n", client->jid->partial,
			iks_find_attrib(item, "node"));
		while ((item = iks_next_tag(item))) {
			xmpp_pubsub_delete_node(client, iks_find_attrib(item, "node"));
		}
	}

	if (item) {
		iks_delete(item);
	}

	return IKS_FILTER_EAT;
}

static void xmpp_pubsub_purge_nodes(struct ast_xmpp_client *client, const char* collection_name)
{
	iks *request = xmpp_pubsub_build_node_request(client, collection_name);
	ast_xmpp_client_send(client, request);
	iks_filter_add_rule(client->filter, xmpp_pubsub_delete_node_list, client, IKS_RULE_TYPE,
			    IKS_PAK_IQ, IKS_RULE_SUBTYPE, IKS_TYPE_RESULT, IKS_RULE_ID, client->mid,
			    IKS_RULE_DONE);
	ast_xmpp_client_send(client, request);
	iks_delete(request);
}

/*!
 * \brief Method to purge PubSub nodes via CLI.
 * \param e pointer to ast_cli_entry structure
 * \param cmd
 * \param a pointer to ast_cli_args structure
 * \return char *
 */
static char *xmpp_cli_purge_pubsub_nodes(struct ast_cli_entry *e, int cmd, struct
					 ast_cli_args *a)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	const char *name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp purge nodes";
		e->usage =
			"Usage: xmpp purge nodes <connection> <node>\n"
			"       Purges nodes on PubSub server\n"
			"       as configured in xmpp.conf.\n";
			return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}
	name = a->argv[3];

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, name))) {
		ast_cli(a->fd, "Unable to find client '%s'!\n", name);
		return CLI_FAILURE;
	}

	if (ast_test_flag(&cfg->global->pubsub, XMPP_XEP0248)) {
		xmpp_pubsub_purge_nodes(clientcfg->client, a->argv[4]);
	} else {
		xmpp_pubsub_delete_node(clientcfg->client, a->argv[4]);
	}

	return CLI_SUCCESS;
}

/*!
 * \brief Method to expose PubSub node deletion via CLI.
 * \param e pointer to ast_cli_entry structure
 * \param cmd
 * \param a pointer to ast_cli_args structure
 * \return char *
 */
static char *xmpp_cli_delete_pubsub_node(struct ast_cli_entry *e, int cmd, struct
					ast_cli_args *a)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	const char *name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp delete node";
		e->usage =
			"Usage: xmpp delete node <connection> <node>\n"
			"       Deletes a node on PubSub server\n"
			"       as configured in xmpp.conf.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}
	name = a->argv[3];

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, name))) {
		ast_cli(a->fd, "Unable to find client '%s'!\n", name);
		return CLI_FAILURE;
	}

	xmpp_pubsub_delete_node(clientcfg->client, a->argv[4]);

	return CLI_SUCCESS;
}

/*!
 * \brief Method to expose PubSub collection node creation via CLI.
 * \return char *.
 */
static char *xmpp_cli_create_collection(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	const char *name, *collection_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp create collection";
		e->usage =
			"Usage: xmpp create collection <connection> <collection>\n"
			"       Creates a PubSub collection node using the account\n"
			"       as configured in xmpp.conf.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}
	name = a->argv[3];
	collection_name = a->argv[4];

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, name))) {
		ast_cli(a->fd, "Unable to find client '%s'!\n", name);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Creating test PubSub node collection.\n");

	xmpp_pubsub_create_collection(clientcfg->client, collection_name);

	return CLI_SUCCESS;
}

/*!
 * \brief Method to expose PubSub leaf node creation via CLI.
 * \return char *.
 */
static char *xmpp_cli_create_leafnode(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct ast_xmpp_client_config *, clientcfg, NULL, ao2_cleanup);
	const char *name, *collection_name, *leaf_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp create leaf";
		e->usage =
			"Usage: xmpp create leaf <connection> <collection> <leaf>\n"
			"       Creates a PubSub leaf node using the account\n"
			"       as configured in xmpp.conf.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 6) {
		return CLI_SHOWUSAGE;
	}
	name = a->argv[3];
	collection_name = a->argv[4];
	leaf_name = a->argv[5];

	if (!cfg || !cfg->clients || !(clientcfg = xmpp_config_find(cfg->clients, name))) {
		ast_cli(a->fd, "Unable to find client '%s'!\n", name);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Creating test PubSub node collection.\n");

	xmpp_pubsub_create_leaf(clientcfg->client, collection_name, leaf_name);

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Turn on/off console debugging.
 * \return CLI_SUCCESS.
 */
static char *xmpp_do_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp set debug {on|off}";
		e->usage =
			"Usage: xmpp set debug {on|off}\n"
			"       Enables/disables dumping of XMPP/Jabber packets for debugging purposes.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	if (!strncasecmp(a->argv[e->args - 1], "on", 2)) {
		debug = 1;
		ast_cli(a->fd, "XMPP Debugging Enabled.\n");
		return CLI_SUCCESS;
	} else if (!strncasecmp(a->argv[e->args - 1], "off", 3)) {
		debug = 0;
		ast_cli(a->fd, "XMPP Debugging Disabled.\n");
		return CLI_SUCCESS;
	}
	return CLI_SHOWUSAGE; /* defaults to invalid */
}

/*!
 * \internal
 * \brief Show client status.
 * \return CLI_SUCCESS.
 */
static char *xmpp_show_clients(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	struct ao2_iterator i;
	struct ast_xmpp_client_config *clientcfg;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp show connections";
		e->usage =
			"Usage: xmpp show connections\n"
			"       Shows state of client and component connections\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!cfg || !cfg->clients) {
		return NULL;
	}

	ast_cli(a->fd, "Jabber Users and their status:\n");

	i = ao2_iterator_init(cfg->clients, 0);
	while ((clientcfg = ao2_iterator_next(&i))) {
		char *state;

		switch (clientcfg->client->state) {
		case XMPP_STATE_DISCONNECTING:
			state = "Disconnecting";
			break;
		case XMPP_STATE_DISCONNECTED:
			state = "Disconnected";
			break;
		case XMPP_STATE_CONNECTING:
			state = "Connecting";
			break;
		case XMPP_STATE_REQUEST_TLS:
			state = "Waiting to request TLS";
			break;
		case XMPP_STATE_REQUESTED_TLS:
			state = "Requested TLS";
			break;
		case XMPP_STATE_AUTHENTICATE:
			state = "Waiting to authenticate";
			break;
		case XMPP_STATE_AUTHENTICATING:
			state = "Authenticating";
			break;
		case XMPP_STATE_ROSTER:
			state = "Retrieving roster";
			break;
		case XMPP_STATE_CONNECTED:
			state = "Connected";
			break;
		default:
			state = "Unknown";
		}

		ast_cli(a->fd, "       [%s] %s     - %s\n", clientcfg->name, clientcfg->user, state);

		ao2_ref(clientcfg, -1);
	}
	ao2_iterator_destroy(&i);

	ast_cli(a->fd, "----\n");
	ast_cli(a->fd, "   Number of clients: %d\n", ao2_container_count(cfg->clients));

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Show buddy lists
 * \return CLI_SUCCESS.
 */
static char *xmpp_show_buddies(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct xmpp_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	struct ao2_iterator i;
	struct ast_xmpp_client_config *clientcfg;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmpp show buddies";
		e->usage =
			"Usage: xmpp show buddies\n"
			"       Shows buddy lists of our clients\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!cfg || !cfg->clients) {
		return NULL;
	}

	ast_cli(a->fd, "XMPP buddy lists\n");

	i = ao2_iterator_init(cfg->clients, 0);
	while ((clientcfg = ao2_iterator_next(&i))) {
		struct ao2_iterator bud;
		struct ast_xmpp_buddy *buddy;

		ast_cli(a->fd, "Client: %s\n", clientcfg->name);

		bud = ao2_iterator_init(clientcfg->client->buddies, 0);
		while ((buddy = ao2_iterator_next(&bud))) {
			struct ao2_iterator res;
			struct ast_xmpp_resource *resource;

			ast_cli(a->fd, "\tBuddy:\t%s\n", buddy->id);

			res = ao2_iterator_init(buddy->resources, 0);
			while ((resource = ao2_iterator_next(&res))) {
				ast_cli(a->fd, "\t\tResource: %s\n", resource->resource);
				ast_cli(a->fd, "\t\t\tnode: %s\n", resource->caps.node);
				ast_cli(a->fd, "\t\t\tversion: %s\n", resource->caps.version);
				ast_cli(a->fd, "\t\t\tGoogle Talk capable: %s\n", resource->caps.google ? "yes" : "no");
				ast_cli(a->fd, "\t\t\tJingle capable: %s\n", resource->caps.jingle ? "yes" : "no");

				ao2_ref(resource, -1);
			}
			ao2_iterator_destroy(&res);

			ao2_ref(buddy, -1);
		}
		ao2_iterator_destroy(&bud);

		ao2_ref(clientcfg, -1);
	}
	ao2_iterator_destroy(&i);

	return CLI_SUCCESS;
}

static struct ast_cli_entry xmpp_cli[] = {
	AST_CLI_DEFINE(xmpp_do_set_debug, "Enable/Disable Jabber debug"),
	AST_CLI_DEFINE(xmpp_show_clients, "Show state of clients and components"),
	AST_CLI_DEFINE(xmpp_show_buddies, "Show buddy lists of our clients"),
	AST_CLI_DEFINE(xmpp_cli_create_collection, "Creates a PubSub node collection."),
	AST_CLI_DEFINE(xmpp_cli_list_pubsub_nodes, "Lists PubSub nodes"),
	AST_CLI_DEFINE(xmpp_cli_create_leafnode, "Creates a PubSub leaf node"),
	AST_CLI_DEFINE(xmpp_cli_delete_pubsub_node, "Deletes a PubSub node"),
	AST_CLI_DEFINE(xmpp_cli_purge_pubsub_nodes, "Purges PubSub nodes"),
};

static int unload_module(void)
{
	ast_msg_tech_unregister(&msg_tech);
	ast_cli_unregister_multiple(xmpp_cli, ARRAY_LEN(xmpp_cli));
	ast_unregister_application(app_ajisend);
	ast_unregister_application(app_ajisendgroup);
	ast_unregister_application(app_ajistatus);
	ast_unregister_application(app_ajijoin);
	ast_unregister_application(app_ajileave);
	ast_manager_unregister("JabberSend");
	ast_custom_function_unregister(&jabberstatus_function);
	ast_custom_function_unregister(&jabberreceive_function);
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);

	ast_cond_destroy(&message_received_condition);
	ast_mutex_destroy(&messagelock);

	return 0;
}

static int global_bitfield_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_xmpp_global_config *global = obj;

	if (!strcasecmp(var->name, "debug")) {
		debug = ast_true(var->value);
	} else if (!strcasecmp(var->name, "autoprune")) {
		ast_set2_flag(&global->general, ast_true(var->value), XMPP_AUTOPRUNE);
	} else if (!strcasecmp(var->name, "autoregister")) {
		ast_set2_flag(&global->general, ast_true(var->value), XMPP_AUTOREGISTER);
	} else if (!strcasecmp(var->name, "auth_policy")) {
		ast_set2_flag(&global->general, !strcasecmp(var->value, "accept") ? 1 : 0, XMPP_AUTOACCEPT);
	} else if (!strcasecmp(var->name, "collection_nodes")) {
		ast_set2_flag(&global->pubsub, ast_true(var->value), XMPP_XEP0248);
	} else if (!strcasecmp(var->name, "pubsub_autocreate")) {
		ast_set2_flag(&global->pubsub, ast_true(var->value), XMPP_PUBSUB_AUTOCREATE);
	} else {
		return -1;
	}

	return 0;
}

static int client_bitfield_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_xmpp_client_config *cfg = obj;

	if (!strcasecmp(var->name, "debug")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_DEBUG);
	} else if (!strcasecmp(var->name, "type")) {
		ast_set2_flag(&cfg->flags, !strcasecmp(var->value, "component") ? 1 : 0, XMPP_COMPONENT);
	} else if (!strcasecmp(var->name, "distribute_events")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_DISTRIBUTE_EVENTS);
	} else if (!strcasecmp(var->name, "usetls")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_USETLS);
	} else if (!strcasecmp(var->name, "usesasl")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_USESASL);
	} else if (!strcasecmp(var->name, "forceoldssl")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_FORCESSL);
	} else if (!strcasecmp(var->name, "keepalive")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_KEEPALIVE);
	} else if (!strcasecmp(var->name, "autoprune")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_AUTOPRUNE);
	} else if (!strcasecmp(var->name, "autoregister")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_AUTOREGISTER);
	} else if (!strcasecmp(var->name, "auth_policy")) {
		ast_set2_flag(&cfg->flags, !strcasecmp(var->value, "accept") ? 1 : 0, XMPP_AUTOACCEPT);
	} else if (!strcasecmp(var->name, "sendtodialplan")) {
		ast_set2_flag(&cfg->flags, ast_true(var->value), XMPP_SEND_TO_DIALPLAN);
	} else {
		return -1;
	}

	return 0;
}

static int client_status_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_xmpp_client_config *cfg = obj;

	if (!strcasecmp(var->value, "unavailable")) {
		cfg->status = IKS_SHOW_UNAVAILABLE;
	} else if (!strcasecmp(var->value, "available") || !strcasecmp(var->value, "online")) {
		cfg->status = IKS_SHOW_AVAILABLE;
	} else if (!strcasecmp(var->value, "chat") || !strcasecmp(var->value, "chatty")) {
		cfg->status = IKS_SHOW_CHAT;
	} else if (!strcasecmp(var->value, "away")) {
		cfg->status = IKS_SHOW_AWAY;
	} else if (!strcasecmp(var->value, "xa") || !strcasecmp(var->value, "xaway")) {
		cfg->status = IKS_SHOW_XA;
	} else if (!strcasecmp(var->value, "dnd")) {
		cfg->status = IKS_SHOW_DND;
	} else if (!strcasecmp(var->value, "invisible")) {
#ifdef IKS_SHOW_INVISIBLE
		cfg->status = IKS_SHOW_INVISIBLE;
#else
		cfg->status = IKS_SHOW_DND;
#endif
	} else {
		return -1;
	}

	return 0;
}

static int client_buddy_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_xmpp_client_config *cfg = obj;
	struct ast_xmpp_buddy *buddy;

	if ((buddy = ao2_find(cfg->buddies, var->value, OBJ_KEY))) {
		ao2_ref(buddy, -1);
		return -1;
	}

	if (!(buddy = xmpp_client_create_buddy(cfg->buddies, var->value))) {
		return -1;
	}

	ao2_ref(buddy, -1);

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the 
 * configuration file or other non-critical problem return 
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (aco_info_init(&cfg_info)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	aco_option_register_custom(&cfg_info, "debug", ACO_EXACT, global_options, "no", global_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "autoprune", ACO_EXACT, global_options, "no", global_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "autoregister", ACO_EXACT, global_options, "yes", global_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "collection_nodes", ACO_EXACT, global_options, "no", global_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "pubsub_autocreate", ACO_EXACT, global_options, "no", global_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "auth_policy", ACO_EXACT, global_options, "accept", global_bitfield_handler, 0);

	aco_option_register(&cfg_info, "username", ACO_EXACT, client_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_xmpp_client_config, user));
	aco_option_register(&cfg_info, "secret", ACO_EXACT, client_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_xmpp_client_config, password));
	aco_option_register(&cfg_info, "serverhost", ACO_EXACT, client_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_xmpp_client_config, server));
	aco_option_register(&cfg_info, "statusmessage", ACO_EXACT, client_options, "Online and Available", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_xmpp_client_config, statusmsg));
	aco_option_register(&cfg_info, "pubsub_node", ACO_EXACT, client_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_xmpp_client_config, pubsubnode));
	aco_option_register(&cfg_info, "context", ACO_EXACT, client_options, "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_xmpp_client_config, context));
	aco_option_register(&cfg_info, "priority", ACO_EXACT, client_options, "1", OPT_UINT_T, 0, FLDSET(struct ast_xmpp_client_config, priority));
	aco_option_register(&cfg_info, "port", ACO_EXACT, client_options, "5222", OPT_UINT_T, 0, FLDSET(struct ast_xmpp_client_config, port));
	aco_option_register(&cfg_info, "timeout", ACO_EXACT, client_options, "5", OPT_UINT_T, 0, FLDSET(struct ast_xmpp_client_config, message_timeout));

	aco_option_register_custom(&cfg_info, "debug", ACO_EXACT, client_options, "no", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "type", ACO_EXACT, client_options, "client", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "distribute_events", ACO_EXACT, client_options, "no", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "usetls", ACO_EXACT, client_options, "yes", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "usesasl", ACO_EXACT, client_options, "yes", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "forceoldssl", ACO_EXACT, client_options, "no", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "keepalive", ACO_EXACT, client_options, "yes", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "autoprune", ACO_EXACT, client_options, "no", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "autoregister", ACO_EXACT, client_options, "yes", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "auth_policy", ACO_EXACT, client_options, "accept", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "sendtodialplan", ACO_EXACT, client_options, "no", client_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "status", ACO_EXACT, client_options, "available", client_status_handler, 0);
	aco_option_register_custom(&cfg_info, "buddy", ACO_EXACT, client_options, NULL, client_buddy_handler, 0);

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		aco_info_destroy(&cfg_info);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_manager_register_xml("JabberSend", EVENT_FLAG_SYSTEM, manager_jabber_send);

	ast_register_application_xml(app_ajisend, xmpp_send_exec);
	ast_register_application_xml(app_ajisendgroup, xmpp_sendgroup_exec);
	ast_register_application_xml(app_ajistatus, xmpp_status_exec);
	ast_register_application_xml(app_ajijoin, xmpp_join_exec);
	ast_register_application_xml(app_ajileave, xmpp_leave_exec);

	ast_cli_register_multiple(xmpp_cli, ARRAY_LEN(xmpp_cli));
	ast_custom_function_register(&jabberstatus_function);
	ast_custom_function_register(&jabberreceive_function);
	ast_msg_tech_register(&msg_tech);

	ast_mutex_init(&messagelock);
	ast_cond_init(&message_received_condition, NULL);

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Asterisk XMPP Interface",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	       );
