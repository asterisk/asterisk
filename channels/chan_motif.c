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
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \brief Motif Jingle Channel Driver
 *
 * Iksemel http://iksemel.jabberstudio.org/
 *
 * \ingroup channel_drivers
 */

/*! \li \ref chan_motif.c uses the configuration file \ref motif.conf
 * \addtogroup configuration_file
 */

/*! \page motif.conf motif.conf
 * \verbinclude motif.conf.sample
 */

/*** MODULEINFO
	<depend>iksemel</depend>
	<depend>res_xmpp</depend>
	<use type="external">openssl</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <iksemel.h>
#include <pthread.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config_options.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include "asterisk/stringfields.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astobj.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/xmpp.h"
#include "asterisk/endpoints.h"
#include "asterisk/stasis_channels.h"

/*** DOCUMENTATION
	<configInfo name="chan_motif" language="en_US">
		<synopsis>Jingle Channel Driver</synopsis>
		<description>
			<para><emphasis>Transports</emphasis></para>
			<para>There are three different transports and protocol derivatives
			supported by <literal>chan_motif</literal>. They are in order of
			preference: Jingle using ICE-UDP, Google Jingle, and Google-V1.</para>
			<para>Jingle as defined in XEP-0166 supports the widest range of
			features. It is referred to as <literal>ice-udp</literal>. This is
			the specification that Jingle clients implement.</para>
			<para>Google Jingle follows the Jingle specification for signaling
			but uses a custom transport for media. It is supported by the
			Google Talk Plug-in in Gmail and by some other Jingle clients. It
			is referred to as <literal>google</literal> in this file.</para>
			<para>Google-V1 is the original Google Talk signaling protocol
			which uses an initial preliminary version of Jingle. It also uses
			the same custom transport as Google Jingle for media. It is
			supported by Google Voice, some other Jingle clients, and the
			Windows Google Talk client. It is referred to as <literal>google-v1</literal>
			in this file.</para>
			<para>Incoming sessions will automatically switch to the correct
			transport once it has been determined.</para>
			<para>Outgoing sessions are capable of determining if the target
			is capable of Jingle or a Google transport if the target is in the
			roster. Unfortunately it is not possible to differentiate between
			a Google Jingle or Google-V1 capable resource until a session
			initiate attempt occurs. If a resource is determined to use a
			Google transport it will initially use Google Jingle but will fall
			back to Google-V1 if required.</para>
			<para>If an outgoing session attempt fails due to failure to
			support the given transport <literal>chan_motif</literal> will
			fall back in preference order listed previously until all
			transports have been exhausted.</para>
			<para><emphasis>Dialing and Resource Selection Strategy</emphasis></para>
			<para>Placing a call through an endpoint can be accomplished using the
			following dial string:</para>
			<para><literal>Motif/[endpoint name]/[target]</literal></para>
			<para>When placing an outgoing call through an endpoint the requested
			target is searched for in the roster list. If present the first Jingle
			or Google Jingle capable resource is specifically targeted. Since the
			capabilities of the resource are known the outgoing session initiation
			will disregard the configured transport and use the determined one.</para>
			<para>If the target is not found in the roster the target will be used
			as-is and a session will be initiated using the transport specified
			in this configuration file. If no transport has been specified the
			endpoint defaults to <literal>ice-udp</literal>.</para>
			<para><emphasis>Video Support</emphasis></para>
			<para>Support for video does not need to be explicitly enabled.
			Configuring any video codec on your endpoint will automatically enable
			it.</para>
			<para><emphasis>DTMF</emphasis></para>
			<para>The only supported method for DTMF is RFC2833. This is always
			enabled on audio streams and negotiated if possible.</para>
			<para><emphasis>Incoming Calls</emphasis></para>
			<para>Incoming calls will first look for the extension matching the
			name of the endpoint in the configured context. If no such extension
			exists the call will automatically fall back to the <literal>s</literal> extension.</para>
			<para><emphasis>CallerID</emphasis></para>
			<para>The incoming caller id number is populated with the username of
			the caller and the name is populated with the full identity of the
			caller. If you would like to perform authentication or filtering
			of incoming calls it is recommended that you use these fields to do so.</para>
			<para>Outgoing caller id can <emphasis>not</emphasis> be set.</para>
			<warning>
				<para>Multiple endpoints using the
				same connection is <emphasis>NOT</emphasis> supported. Doing so
				may result in broken calls.</para>
			</warning>
		</description>
		<configFile name="motif.conf">
			<configObject name="endpoint">
				<synopsis>The configuration for an endpoint.</synopsis>
				<configOption name="context">
					<synopsis>Default dialplan context that incoming sessions will be routed to</synopsis>
				</configOption>
				<configOption name="callgroup">
					<synopsis>A callgroup to assign to this endpoint.</synopsis>
				</configOption>
				<configOption name="pickupgroup">
					<synopsis>A pickup group to assign to this endpoint.</synopsis>
				</configOption>
				<configOption name="language">
					<synopsis>The default language for this endpoint.</synopsis>
				</configOption>
				<configOption name="musicclass">
					<synopsis>Default music on hold class for this endpoint.</synopsis>
				</configOption>
				<configOption name="parkinglot">
					<synopsis>Default parking lot for this endpoint.</synopsis>
				</configOption>
				<configOption name="accountcode">
					<synopsis>Accout code for CDR purposes</synopsis>
				</configOption>
				<configOption name="allow">
					<synopsis>Codecs to allow</synopsis>
				</configOption>
				<configOption name="disallow">
					<synopsis>Codecs to disallow</synopsis>
				</configOption>
				<configOption name="connection">
					<synopsis>Connection to accept traffic on and on which to send traffic out</synopsis>
				</configOption>
				<configOption name="transport">
					<synopsis>The transport to use for the endpoint.</synopsis>
					<description>
						<para>The default outbound transport for this endpoint. Inbound
						messages are inferred. Allowed transports are <literal>ice-udp</literal>,
						<literal>google</literal>, or <literal>google-v1</literal>. Note
						that <literal>chan_motif</literal> will fall back to transport
						preference order if the transport value chosen here fails.</para>
						<enumlist>
							<enum name="ice-udp">
								<para>The Jingle protocol, as defined in XEP 0166.</para>
							</enum>
							<enum name="google">
								<para>The Google Jingle protocol, which follows the Jingle
								specification for signaling but uses a custom transport for
								media.</para>
							</enum>
							<enum name="google-v1">
								<para>Google-V1 is the original Google Talk signaling
								protocol which uses an initial preliminary version of Jingle.
								It also uses the same custom transport as <literal>google</literal> for media.</para>
							</enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="maxicecandidates">
					<synopsis>Maximum number of ICE candidates to offer</synopsis>
				</configOption>
				<configOption name="maxpayloads">
					<synopsis>Maximum number of pyaloads to offer</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

/*! \brief Default maximum number of ICE candidates we will offer */
#define DEFAULT_MAX_ICE_CANDIDATES "10"

/*! \brief Default maximum number of payloads we will offer */
#define DEFAULT_MAX_PAYLOADS "30"

/*! \brief Number of buckets for endpoints */
#define ENDPOINT_BUCKETS 37

/*! \brief Number of buckets for sessions, on a per-endpoint basis */
#define SESSION_BUCKETS 37

/*! \brief Namespace for Jingle itself */
#define JINGLE_NS "urn:xmpp:jingle:1"

/*! \brief Namespace for Jingle RTP sessions */
#define JINGLE_RTP_NS "urn:xmpp:jingle:apps:rtp:1"

/*! \brief Namespace for Jingle RTP info */
#define JINGLE_RTP_INFO_NS "urn:xmpp:jingle:apps:rtp:info:1"

/*! \brief Namespace for Jingle ICE-UDP */
#define JINGLE_ICE_UDP_NS "urn:xmpp:jingle:transports:ice-udp:1"

/*! \brief Namespace for Google Talk ICE-UDP */
#define GOOGLE_TRANSPORT_NS "http://www.google.com/transport/p2p"

/*! \brief Namespace for Google Talk Raw UDP */
#define GOOGLE_TRANSPORT_RAW_NS "http://www.google.com/transport/raw-udp"

/*! \brief Namespace for Google Session */
#define GOOGLE_SESSION_NS "http://www.google.com/session"

/*! \brief Namespace for Google Phone description */
#define GOOGLE_PHONE_NS "http://www.google.com/session/phone"

/*! \brief Namespace for Google Video description */
#define GOOGLE_VIDEO_NS "http://www.google.com/session/video"

/*! \brief Namespace for XMPP stanzas */
#define XMPP_STANZAS_NS "urn:ietf:params:xml:ns:xmpp-stanzas"

/*! \brief The various transport methods supported, from highest priority to lowest priority when doing fallback */
enum jingle_transport {
	JINGLE_TRANSPORT_ICE_UDP = 3,   /*!< XEP-0176 */
	JINGLE_TRANSPORT_GOOGLE_V2 = 2, /*!< https://developers.google.com/talk/call_signaling */
	JINGLE_TRANSPORT_GOOGLE_V1 = 1, /*!< Undocumented initial Google specification */
	JINGLE_TRANSPORT_NONE = 0,      /*!< No transport specified */
};

/*! \brief Endpoint state information */
struct jingle_endpoint_state {
	struct ao2_container *sessions; /*!< Active sessions to or from the endpoint */
};

/*! \brief Endpoint which contains configuration information and active sessions */
struct jingle_endpoint {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);              /*!< Name of the endpoint */
		AST_STRING_FIELD(context);           /*!< Context to place incoming calls into */
		AST_STRING_FIELD(accountcode);       /*!< Account code */
		AST_STRING_FIELD(language);          /*!< Default language for prompts */
		AST_STRING_FIELD(musicclass);        /*!< Configured music on hold class */
		AST_STRING_FIELD(parkinglot);        /*!< Configured parking lot */
		);
	struct ast_xmpp_client *connection;     /*!< Connection to use for traffic */
	iksrule *rule;                          /*!< Active matching rule */
	unsigned int maxicecandidates;          /*!< Maximum number of ICE candidates we will offer */
	unsigned int maxpayloads;               /*!< Maximum number of payloads we will offer */
	struct ast_codec_pref prefs;            /*!< Codec preferences */
	struct ast_format_cap *cap;             /*!< Formats to use */
	ast_group_t callgroup;                  /*!< Call group */
	ast_group_t pickupgroup;                /*!< Pickup group */
	enum jingle_transport transport;        /*!< Default transport to use on outgoing sessions */
	struct jingle_endpoint_state *state;    /*!< Endpoint state information */
};

/*! \brief Session which contains information about an active session */
struct jingle_session {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(sid);        /*!< Session identifier */
		AST_STRING_FIELD(audio_name); /*!< Name of the audio content */
		AST_STRING_FIELD(video_name); /*!< Name of the video content */
		);
	struct jingle_endpoint_state *state;  /*!< Endpoint we are associated with */
	struct ast_xmpp_client *connection;   /*!< Connection to use for traffic */
	enum jingle_transport transport;      /*!< Transport type to use for this session */
	unsigned int maxicecandidates;        /*!< Maximum number of ICE candidates we will offer */
	unsigned int maxpayloads;             /*!< Maximum number of payloads we will offer */
	char remote_original[XMPP_MAX_JIDLEN];/*!< Identifier of the original remote party (remote may have changed due to redirect) */
	char remote[XMPP_MAX_JIDLEN];         /*!< Identifier of the remote party */
	iksrule *rule;                        /*!< Session matching rule */
	struct ast_codec_pref prefs;          /*!< Codec preferences */
	struct ast_channel *owner;            /*!< Master Channel */
	struct ast_rtp_instance *rtp;         /*!< RTP audio session */
	struct ast_rtp_instance *vrtp;        /*!< RTP video session */
	struct ast_format_cap *cap;           /*!< Local codec capabilities */
	struct ast_format_cap *jointcap;      /*!< Joint codec capabilities */
	struct ast_format_cap *peercap;       /*!< Peer codec capabilities */
	unsigned int outgoing:1;              /*!< Whether this is an outgoing leg or not */
	unsigned int gone:1;                  /*!< In the eyes of Jingle this session is already gone */
	struct ast_callid *callid;            /*!< Bound session call-id */
};

static const char desc[] = "Motif Jingle Channel";
static const char channel_type[] = "Motif";

struct jingle_config {
	struct ao2_container *endpoints; /*!< Configured endpoints */
};

static AO2_GLOBAL_OBJ_STATIC(globals);

static struct ast_sched_context *sched; /*!< Scheduling context for RTCP */

/* \brief Asterisk core interaction functions */
static struct ast_channel *jingle_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int jingle_sendtext(struct ast_channel *ast, const char *text);
static int jingle_digit_begin(struct ast_channel *ast, char digit);
static int jingle_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int jingle_call(struct ast_channel *ast, const char *dest, int timeout);
static int jingle_hangup(struct ast_channel *ast);
static int jingle_answer(struct ast_channel *ast);
static struct ast_frame *jingle_read(struct ast_channel *ast);
static int jingle_write(struct ast_channel *ast, struct ast_frame *f);
static int jingle_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int jingle_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static struct jingle_session *jingle_alloc(struct jingle_endpoint *endpoint, const char *from, const char *sid);

/*! \brief Action handlers */
static void jingle_action_session_initiate(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak);
static void jingle_action_transport_info(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak);
static void jingle_action_session_accept(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak);
static void jingle_action_session_info(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak);
static void jingle_action_session_terminate(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak);

/*! \brief PBX interface structure for channel registration */
static struct ast_channel_tech jingle_tech = {
	.type = "Motif",
	.description = "Motif Jingle Channel Driver",
	.requester = jingle_request,
	.send_text = jingle_sendtext,
	.send_digit_begin = jingle_digit_begin,
	.send_digit_end = jingle_digit_end,
	.call = jingle_call,
	.hangup = jingle_hangup,
	.answer = jingle_answer,
	.read = jingle_read,
	.write = jingle_write,
	.write_video = jingle_write,
	.exception = jingle_read,
	.indicate = jingle_indicate,
	.fixup = jingle_fixup,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER
};

/*! \brief Defined handlers for different Jingle actions */
static const struct jingle_action_handler {
	const char *action;
	void (*handler)(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak);
} jingle_action_handlers[] = {
	/* Jingle actions */
	{ "session-initiate", jingle_action_session_initiate, },
	{ "transport-info", jingle_action_transport_info, },
	{ "session-accept", jingle_action_session_accept, },
	{ "session-info", jingle_action_session_info, },
	{ "session-terminate", jingle_action_session_terminate, },
	/* Google-V1 actions */
	{ "initiate", jingle_action_session_initiate, },
	{ "candidates", jingle_action_transport_info, },
	{ "accept", jingle_action_session_accept, },
	{ "terminate", jingle_action_session_terminate, },
	{ "reject", jingle_action_session_terminate, },
};

/*! \brief Reason text <-> cause code mapping */
static const struct jingle_reason_mapping {
	const char *reason;
	int cause;
} jingle_reason_mappings[] = {
	{ "busy", AST_CAUSE_BUSY, },
	{ "cancel", AST_CAUSE_CALL_REJECTED, },
	{ "connectivity-error", AST_CAUSE_INTERWORKING, },
	{ "decline", AST_CAUSE_CALL_REJECTED, },
	{ "expired", AST_CAUSE_NO_USER_RESPONSE, },
	{ "failed-transport", AST_CAUSE_PROTOCOL_ERROR, },
	{ "failed-application", AST_CAUSE_SWITCH_CONGESTION, },
	{ "general-error", AST_CAUSE_CONGESTION, },
	{ "gone", AST_CAUSE_NORMAL_CLEARING, },
	{ "incompatible-parameters", AST_CAUSE_BEARERCAPABILITY_NOTAVAIL, },
	{ "media-error", AST_CAUSE_BEARERCAPABILITY_NOTAVAIL, },
	{ "security-error", AST_CAUSE_PROTOCOL_ERROR, },
	{ "success", AST_CAUSE_NORMAL_CLEARING, },
	{ "timeout", AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE, },
	{ "unsupported-applications", AST_CAUSE_BEARERCAPABILITY_NOTAVAIL, },
	{ "unsupported-transports", AST_CAUSE_FACILITY_NOT_IMPLEMENTED, },
};

/*! \brief Hashing function for Jingle sessions */
static int jingle_session_hash(const void *obj, const int flags)
{
	const struct jingle_session *session = obj;
	const char *sid = obj;

	return ast_str_hash(flags & OBJ_KEY ? sid : session->sid);
}

/*! \brief Comparator function for Jingle sessions */
static int jingle_session_cmp(void *obj, void *arg, int flags)
{
	struct jingle_session *session1 = obj, *session2 = arg;
	const char *sid = arg;

	return !strcmp(session1->sid, flags & OBJ_KEY ? sid : session2->sid) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Destructor for Jingle endpoint state */
static void jingle_endpoint_state_destructor(void *obj)
{
	struct jingle_endpoint_state *state = obj;

	ao2_ref(state->sessions, -1);
}

/*! \brief Destructor for Jingle endpoints */
static void jingle_endpoint_destructor(void *obj)
{
	struct jingle_endpoint *endpoint = obj;

	if (endpoint->rule) {
		iks_filter_remove_rule(endpoint->connection->filter, endpoint->rule);
	}

	if (endpoint->connection) {
		ast_xmpp_client_unref(endpoint->connection);
	}

	ast_format_cap_destroy(endpoint->cap);

	ao2_ref(endpoint->state, -1);

	ast_string_field_free_memory(endpoint);
}

/*! \brief Find function for Jingle endpoints */
static void *jingle_endpoint_find(struct ao2_container *tmp_container, const char *category)
{
	return ao2_find(tmp_container, category, OBJ_KEY);
}

/*! \brief Allocator function for Jingle endpoint state */
static struct jingle_endpoint_state *jingle_endpoint_state_create(void)
{
	struct jingle_endpoint_state *state;

	if (!(state = ao2_alloc(sizeof(*state), jingle_endpoint_state_destructor))) {
		return NULL;
	}

	if (!(state->sessions = ao2_container_alloc(SESSION_BUCKETS, jingle_session_hash, jingle_session_cmp))) {
		ao2_ref(state, -1);
		return NULL;
	}

	return state;
}

/*! \brief State find/create function */
static struct jingle_endpoint_state *jingle_endpoint_state_find_or_create(const char *category)
{
	RAII_VAR(struct jingle_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct jingle_endpoint *, endpoint, NULL, ao2_cleanup);

	if (!cfg || !cfg->endpoints || !(endpoint = jingle_endpoint_find(cfg->endpoints, category))) {
		return jingle_endpoint_state_create();
	}

	ao2_ref(endpoint->state, +1);
	return endpoint->state;
}

/*! \brief Allocator function for Jingle endpoints */
static void *jingle_endpoint_alloc(const char *cat)
{
	struct jingle_endpoint *endpoint;

	if (!(endpoint = ao2_alloc(sizeof(*endpoint), jingle_endpoint_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(endpoint, 512)) {
		ao2_ref(endpoint, -1);
		return NULL;
	}

	if (!(endpoint->state = jingle_endpoint_state_find_or_create(cat))) {
		ao2_ref(endpoint, -1);
		return NULL;
	}

	ast_string_field_set(endpoint, name, cat);

	endpoint->cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK);
	endpoint->transport = JINGLE_TRANSPORT_ICE_UDP;

	return endpoint;
}

/*! \brief Hashing function for Jingle endpoints */
static int jingle_endpoint_hash(const void *obj, const int flags)
{
	const struct jingle_endpoint *endpoint = obj;
	const char *name = obj;

	return ast_str_hash(flags & OBJ_KEY ? name : endpoint->name);
}

/*! \brief Comparator function for Jingle endpoints */
static int jingle_endpoint_cmp(void *obj, void *arg, int flags)
{
	struct jingle_endpoint *endpoint1 = obj, *endpoint2 = arg;
	const char *name = arg;

	return !strcmp(endpoint1->name, flags & OBJ_KEY ? name : endpoint2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static struct aco_type endpoint_option = {
	.type = ACO_ITEM,
	.name = "endpoint",
	.category_match = ACO_BLACKLIST,
	.category = "^general$",
	.item_alloc = jingle_endpoint_alloc,
	.item_find = jingle_endpoint_find,
	.item_offset = offsetof(struct jingle_config, endpoints),
};

struct aco_type *endpoint_options[] = ACO_TYPES(&endpoint_option);

struct aco_file jingle_conf = {
	.filename = "motif.conf",
	.types = ACO_TYPES(&endpoint_option),
};

/*! \brief Destructor for Jingle sessions */
static void jingle_session_destructor(void *obj)
{
	struct jingle_session *session = obj;

	if (session->rule) {
		iks_filter_remove_rule(session->connection->filter, session->rule);
	}

	if (session->connection) {
		ast_xmpp_client_unref(session->connection);
	}

	if (session->rtp) {
		ast_rtp_instance_stop(session->rtp);
		ast_rtp_instance_destroy(session->rtp);
	}

	if (session->vrtp) {
		ast_rtp_instance_stop(session->vrtp);
		ast_rtp_instance_destroy(session->vrtp);
	}

	ast_format_cap_destroy(session->cap);
	ast_format_cap_destroy(session->jointcap);
	ast_format_cap_destroy(session->peercap);

	if (session->callid) {
		ast_callid_unref(session->callid);
	}

	ast_string_field_free_memory(session);
}

/*! \brief Destructor called when module configuration goes away */
static void jingle_config_destructor(void *obj)
{
	struct jingle_config *cfg = obj;
	ao2_cleanup(cfg->endpoints);
}

/*! \brief Allocator called when module configuration should appear */
static void *jingle_config_alloc(void)
{
	struct jingle_config *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), jingle_config_destructor))) {
		return NULL;
	}

	if (!(cfg->endpoints = ao2_container_alloc(ENDPOINT_BUCKETS, jingle_endpoint_hash, jingle_endpoint_cmp))) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

CONFIG_INFO_STANDARD(cfg_info, globals, jingle_config_alloc,
		     .files = ACO_FILES(&jingle_conf),
	);

/*! \brief Function called by RTP engine to get local RTP peer */
static enum ast_rtp_glue_result jingle_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct jingle_session *session = ast_channel_tech_pvt(chan);
	enum ast_rtp_glue_result res = AST_RTP_GLUE_RESULT_LOCAL;

	if (!session->rtp) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	ao2_ref(session->rtp, +1);
	*instance = session->rtp;

	return res;
}

/*! \brief Function called by RTP engine to get peer capabilities */
static void jingle_get_codec(struct ast_channel *chan, struct ast_format_cap *result)
{
}

/*! \brief Function called by RTP engine to change where the remote party should send media */
static int jingle_set_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance *rtp, struct ast_rtp_instance *vrtp, struct ast_rtp_instance *tpeer, const struct ast_format_cap *cap, int nat_active)
{
	return -1;
}

/*! \brief Local glue for interacting with the RTP engine core */
static struct ast_rtp_glue jingle_rtp_glue = {
	.type = "Motif",
	.get_rtp_info = jingle_get_rtp_peer,
	.get_codec = jingle_get_codec,
	.update_peer = jingle_set_rtp_peer,
};

/*! \brief Set the channel owner on the \ref jingle_session object and related objects */
static void jingle_set_owner(struct jingle_session *session, struct ast_channel *chan)
{
	session->owner = chan;
	if (session->rtp) {
		ast_rtp_instance_set_channel_id(session->rtp, session->owner ? ast_channel_uniqueid(session->owner) : "");
	}
	if (session->vrtp) {
		ast_rtp_instance_set_channel_id(session->vrtp, session->owner ? ast_channel_uniqueid(session->owner) : "");
	}
}

/*! \brief Internal helper function which enables video support on a sesson if possible */
static void jingle_enable_video(struct jingle_session *session)
{
	struct ast_sockaddr tmp;
	struct ast_rtp_engine_ice *ice;

	/* If video is already present don't do anything */
	if (session->vrtp) {
		return;
	}

	/* If there are no configured video codecs do not turn video support on, it just won't work */
	if (!ast_format_cap_has_type(session->cap, AST_FORMAT_TYPE_VIDEO)) {
		return;
	}

	ast_sockaddr_parse(&tmp, "0.0.0.0", 0);

	if (!(session->vrtp = ast_rtp_instance_new("asterisk", sched, &tmp, NULL))) {
		return;
	}

	ast_rtp_instance_set_prop(session->vrtp, AST_RTP_PROPERTY_RTCP, 1);
	ast_rtp_instance_set_channel_id(session->vrtp, ast_channel_uniqueid(session->owner));
	ast_channel_set_fd(session->owner, 2, ast_rtp_instance_fd(session->vrtp, 0));
	ast_channel_set_fd(session->owner, 3, ast_rtp_instance_fd(session->vrtp, 1));
	ast_rtp_codecs_packetization_set(ast_rtp_instance_get_codecs(session->vrtp), session->vrtp, &session->prefs);

	if (session->transport == JINGLE_TRANSPORT_GOOGLE_V2 && (ice = ast_rtp_instance_get_ice(session->vrtp))) {
		ice->stop(session->vrtp);
	}
}

/*! \brief Internal helper function used to allocate Jingle session on an endpoint */
static struct jingle_session *jingle_alloc(struct jingle_endpoint *endpoint, const char *from, const char *sid)
{
	struct jingle_session *session;
	struct ast_callid *callid;
	struct ast_sockaddr tmp;

	if (!(session = ao2_alloc(sizeof(*session), jingle_session_destructor))) {
		return NULL;
	}

	callid = ast_read_threadstorage_callid();
	session->callid = (callid ? callid : ast_create_callid());

	if (ast_string_field_init(session, 512)) {
		ao2_ref(session, -1);
		return NULL;
	}

	if (!ast_strlen_zero(from)) {
		ast_copy_string(session->remote_original, from, sizeof(session->remote_original));
		ast_copy_string(session->remote, from, sizeof(session->remote));
	}

	if (ast_strlen_zero(sid)) {
		ast_string_field_build(session, sid, "%08lx%08lx", (unsigned long)ast_random(), (unsigned long)ast_random());
		session->outgoing = 1;
		ast_string_field_set(session, audio_name, "audio");
		ast_string_field_set(session, video_name, "video");
	} else {
		ast_string_field_set(session, sid, sid);
	}

	ao2_ref(endpoint->state, +1);
	session->state = endpoint->state;
	ao2_ref(endpoint->connection, +1);
	session->connection = endpoint->connection;
	session->transport = endpoint->transport;

	if (!(session->cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK)) ||
	    !(session->jointcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK)) ||
	    !(session->peercap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK)) ||
	    !session->callid) {
		ao2_ref(session, -1);
		return NULL;
	}

	ast_format_cap_copy(session->cap, endpoint->cap);

	/* While we rely on res_xmpp for communication we still need a temporary ast_sockaddr to tell the RTP engine
	 * that we want IPv4 */
	ast_sockaddr_parse(&tmp, "0.0.0.0", 0);

	/* Sessions always carry audio, but video is optional so don't enable it here */
	if (!(session->rtp = ast_rtp_instance_new("asterisk", sched, &tmp, NULL))) {
		ao2_ref(session, -1);
		return NULL;
	}
	ast_rtp_instance_set_prop(session->rtp, AST_RTP_PROPERTY_RTCP, 1);
	ast_rtp_instance_set_prop(session->rtp, AST_RTP_PROPERTY_DTMF, 1);

	memcpy(&session->prefs, &endpoint->prefs, sizeof(session->prefs));

	session->maxicecandidates = endpoint->maxicecandidates;
	session->maxpayloads = endpoint->maxpayloads;

	return session;
}

/*! \brief Function called to create a new Jingle Asterisk channel */
static struct ast_channel *jingle_new(struct jingle_endpoint *endpoint, struct jingle_session *session, int state, const char *title, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *cid_name)
{
	struct ast_channel *chan;
	const char *str = S_OR(title, session->remote);
	struct ast_format tmpfmt;

	if (ast_format_cap_is_empty(session->cap)) {
		return NULL;
	}

	if (!(chan = ast_channel_alloc_with_endpoint(1, state, S_OR(title, ""), S_OR(cid_name, ""), "", "", "", assignedids, requestor, 0, endpoint->connection->endpoint, "Motif/%s-%04lx", str, (unsigned long)(ast_random() & 0xffff)))) {
		return NULL;
	}

	ast_channel_stage_snapshot(chan);

	ast_channel_tech_set(chan, &jingle_tech);
	ast_channel_tech_pvt_set(chan, session);
	jingle_set_owner(session, chan);

	ast_channel_callid_set(chan, session->callid);

	ast_format_cap_copy(ast_channel_nativeformats(chan), session->cap);
	ast_codec_choose(&session->prefs, session->cap, 1, &tmpfmt);

	if (session->rtp) {
		struct ast_rtp_engine_ice *ice;

		ast_channel_set_fd(chan, 0, ast_rtp_instance_fd(session->rtp, 0));
		ast_channel_set_fd(chan, 1, ast_rtp_instance_fd(session->rtp, 1));
		ast_rtp_codecs_packetization_set(ast_rtp_instance_get_codecs(session->rtp), session->rtp, &session->prefs);

		if (((session->transport == JINGLE_TRANSPORT_GOOGLE_V2) ||
		     (session->transport == JINGLE_TRANSPORT_GOOGLE_V1)) &&
		    (ice = ast_rtp_instance_get_ice(session->rtp))) {
			/* We stop built in ICE support because we need to fall back to old old old STUN support */
			ice->stop(session->rtp);
		}
	}

	if (state == AST_STATE_RING) {
		ast_channel_rings_set(chan, 1);
	}

	ast_channel_adsicpe_set(chan, AST_ADSI_UNAVAILABLE);

	ast_best_codec(ast_channel_nativeformats(chan), &tmpfmt);
	ast_format_copy(ast_channel_writeformat(chan), &tmpfmt);
	ast_format_copy(ast_channel_rawwriteformat(chan), &tmpfmt);
	ast_format_copy(ast_channel_readformat(chan), &tmpfmt);
	ast_format_copy(ast_channel_rawreadformat(chan), &tmpfmt);

	ao2_lock(endpoint);

	ast_channel_callgroup_set(chan, endpoint->callgroup);
	ast_channel_pickupgroup_set(chan, endpoint->pickupgroup);

	if (!ast_strlen_zero(endpoint->accountcode)) {
		ast_channel_accountcode_set(chan, endpoint->accountcode);
	}

	if (!ast_strlen_zero(endpoint->language)) {
		ast_channel_language_set(chan, endpoint->language);
	}

	if (!ast_strlen_zero(endpoint->musicclass)) {
		ast_channel_musicclass_set(chan, endpoint->musicclass);
	}

	ast_channel_context_set(chan, endpoint->context);
	if (ast_exists_extension(NULL, endpoint->context, endpoint->name, 1, NULL)) {
		ast_channel_exten_set(chan, endpoint->name);
	} else {
		ast_channel_exten_set(chan, "s");
	}
	ast_channel_priority_set(chan, 1);

	ao2_unlock(endpoint);

	ast_channel_stage_snapshot_done(chan);
	ast_channel_unlock(chan);

	return chan;
}

/*! \brief Internal helper function which sends a response */
static void jingle_send_response(struct ast_xmpp_client *connection, ikspak *pak)
{
	iks *response;

	if (!(response = iks_new("iq"))) {
		ast_log(LOG_ERROR, "Unable to allocate an IKS response stanza\n");
		return;
	}

	iks_insert_attrib(response, "type", "result");
	iks_insert_attrib(response, "from", connection->jid->full);
	iks_insert_attrib(response, "to", iks_find_attrib(pak->x, "from"));
	iks_insert_attrib(response, "id", iks_find_attrib(pak->x, "id"));

	ast_xmpp_client_send(connection, response);

	iks_delete(response);
}

/*! \brief Internal helper function which sends an error response */
static void jingle_send_error_response(struct ast_xmpp_client *connection, ikspak *pak, const char *type, const char *reasonstr, const char *reasonstr2)
{
	iks *response, *error = NULL, *reason = NULL, *reason2 = NULL;

	if (!(response = iks_new("iq")) ||
	    !(error = iks_new("error")) ||
	    !(reason = iks_new(reasonstr))) {
		ast_log(LOG_ERROR, "Unable to allocate IKS error response stanzas\n");
		goto end;
	}

	iks_insert_attrib(response, "type", "error");
	iks_insert_attrib(response, "from", connection->jid->full);
	iks_insert_attrib(response, "to", iks_find_attrib(pak->x, "from"));
	iks_insert_attrib(response, "id", iks_find_attrib(pak->x, "id"));

	iks_insert_attrib(error, "type", type);
	iks_insert_node(error, reason);

	if (!ast_strlen_zero(reasonstr2) && (reason2 = iks_new(reasonstr2))) {
		iks_insert_node(error, reason2);
	}

	iks_insert_node(response, error);

	ast_xmpp_client_send(connection, response);
end:
	iks_delete(reason2);
	iks_delete(reason);
	iks_delete(error);
	iks_delete(response);
}

/*! \brief Internal helper function which adds ICE-UDP candidates to a transport node */
static int jingle_add_ice_udp_candidates_to_transport(struct ast_rtp_instance *rtp, iks *transport, iks **candidates, unsigned int maximum)
{
	struct ast_rtp_engine_ice *ice;
	struct ao2_container *local_candidates;
	struct ao2_iterator it;
	struct ast_rtp_engine_ice_candidate *candidate;
	int i = 0, res = 0;

	if (!(ice = ast_rtp_instance_get_ice(rtp)) || !(local_candidates = ice->get_local_candidates(rtp))) {
		ast_log(LOG_ERROR, "Unable to add ICE-UDP candidates as ICE support not available or no candidates available\n");
		return -1;
	}

	iks_insert_attrib(transport, "xmlns", JINGLE_ICE_UDP_NS);
	iks_insert_attrib(transport, "pwd", ice->get_password(rtp));
	iks_insert_attrib(transport, "ufrag", ice->get_ufrag(rtp));

	it = ao2_iterator_init(local_candidates, 0);

	while ((candidate = ao2_iterator_next(&it)) && (i < maximum)) {
		iks *local_candidate;
		char tmp[30];

		if (!(local_candidate = iks_new("candidate"))) {
			res = -1;
			ast_log(LOG_ERROR, "Unable to allocate IKS candidate stanza for ICE-UDP transport\n");
			break;
		}

		snprintf(tmp, sizeof(tmp), "%u", candidate->id);
		iks_insert_attrib(local_candidate, "component", tmp);
		snprintf(tmp, sizeof(tmp), "%d", ast_str_hash(candidate->foundation));
		iks_insert_attrib(local_candidate, "foundation", tmp);
		iks_insert_attrib(local_candidate, "generation", "0");
		iks_insert_attrib(local_candidate, "network", "0");
		snprintf(tmp, sizeof(tmp), "%04lx", (unsigned long)(ast_random() & 0xffff));
		iks_insert_attrib(local_candidate, "id", tmp);
		iks_insert_attrib(local_candidate, "ip", ast_sockaddr_stringify_host(&candidate->address));
		iks_insert_attrib(local_candidate, "port", ast_sockaddr_stringify_port(&candidate->address));
		snprintf(tmp, sizeof(tmp), "%d", candidate->priority);
		iks_insert_attrib(local_candidate, "priority", tmp);
		iks_insert_attrib(local_candidate, "protocol", "udp");

		if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_HOST) {
			iks_insert_attrib(local_candidate, "type", "host");
		} else if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_SRFLX) {
			iks_insert_attrib(local_candidate, "type", "srflx");
		} else if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_RELAYED) {
			iks_insert_attrib(local_candidate, "type", "relay");
		}

		iks_insert_node(transport, local_candidate);
		candidates[i++] = local_candidate;
	}

	ao2_iterator_destroy(&it);
	ao2_ref(local_candidates, -1);

	return res;
}

/*! \brief Internal helper function which adds Google candidates to a transport node */
static int jingle_add_google_candidates_to_transport(struct ast_rtp_instance *rtp, iks *transport, iks **candidates, unsigned int video, enum jingle_transport transport_type, unsigned int maximum)
{
	struct ast_rtp_engine_ice *ice;
	struct ao2_container *local_candidates;
	struct ao2_iterator it;
	struct ast_rtp_engine_ice_candidate *candidate;
	int i = 0, res = 0;

	if (!(ice = ast_rtp_instance_get_ice(rtp)) || !(local_candidates = ice->get_local_candidates(rtp))) {
		ast_log(LOG_ERROR, "Unable to add Google ICE candidates as ICE support not available or no candidates available\n");
		return -1;
	}

	if (transport_type != JINGLE_TRANSPORT_GOOGLE_V1) {
		iks_insert_attrib(transport, "xmlns", GOOGLE_TRANSPORT_NS);
	}

	it = ao2_iterator_init(local_candidates, 0);

	while ((candidate = ao2_iterator_next(&it)) && (i < maximum)) {
		iks *local_candidate;
		/* In Google land a username is 16 bytes, explicitly */
		char ufrag[17] = "";

		if (!(local_candidate = iks_new("candidate"))) {
			res = -1;
			ast_log(LOG_ERROR, "Unable to allocate IKS candidate stanza for Google ICE transport\n");
			break;
		}

		if (candidate->id == 1) {
			iks_insert_attrib(local_candidate, "name", !video ? "rtp" : "video_rtp");
		} else if (candidate->id == 2) {
			iks_insert_attrib(local_candidate, "name", !video ? "rtcp" : "video_rtcp");
		} else {
			iks_delete(local_candidate);
			continue;
		}

		iks_insert_attrib(local_candidate, "address", ast_sockaddr_stringify_host(&candidate->address));
		iks_insert_attrib(local_candidate, "port", ast_sockaddr_stringify_port(&candidate->address));

		if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_HOST) {
			iks_insert_attrib(local_candidate, "preference", "0.95");
			iks_insert_attrib(local_candidate, "type", "local");
		} else if (candidate->type == AST_RTP_ICE_CANDIDATE_TYPE_SRFLX) {
			iks_insert_attrib(local_candidate, "preference", "0.9");
			iks_insert_attrib(local_candidate, "type", "stun");
		}

		iks_insert_attrib(local_candidate, "protocol", "udp");
		iks_insert_attrib(local_candidate, "network", "0");
		snprintf(ufrag, sizeof(ufrag), "%s", ice->get_ufrag(rtp));
		iks_insert_attrib(local_candidate, "username", ufrag);
		iks_insert_attrib(local_candidate, "generation", "0");

		if (transport_type == JINGLE_TRANSPORT_GOOGLE_V1) {
			iks_insert_attrib(local_candidate, "password", "");
			iks_insert_attrib(local_candidate, "foundation", "0");
			iks_insert_attrib(local_candidate, "component", "1");
		} else {
			iks_insert_attrib(local_candidate, "password", ice->get_password(rtp));
		}

		/* You may notice a lack of relay support up above - this is because we don't support it for use with
		 * the Google talk transport due to their arcane support. */

		iks_insert_node(transport, local_candidate);
		candidates[i++] = local_candidate;
	}

	ao2_iterator_destroy(&it);
	ao2_ref(local_candidates, -1);

	return res;
}

/*! \brief Internal function which sends a session-terminate message */
static void jingle_send_session_terminate(struct jingle_session *session, const char *reasontext)
{
	iks *iq = NULL, *jingle = NULL, *reason = NULL, *text = NULL;

	if (!(iq = iks_new("iq")) || !(jingle = iks_new(session->transport == JINGLE_TRANSPORT_GOOGLE_V1 ? "session" : "jingle")) ||
	    !(reason = iks_new("reason")) || !(text = iks_new(reasontext))) {
		ast_log(LOG_ERROR, "Failed to allocate stanzas for session-terminate message on session '%s'\n", session->sid);
		goto end;
	}

	iks_insert_attrib(iq, "to", session->remote);
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "id", session->connection->mid);
	ast_xmpp_increment_mid(session->connection->mid);

	if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
		iks_insert_attrib(jingle, "type", "terminate");
		iks_insert_attrib(jingle, "id", session->sid);
		iks_insert_attrib(jingle, "xmlns", GOOGLE_SESSION_NS);
		iks_insert_attrib(jingle, "initiator", session->outgoing ? session->connection->jid->full : session->remote);
	} else {
		iks_insert_attrib(jingle, "action", "session-terminate");
		iks_insert_attrib(jingle, "sid", session->sid);
		iks_insert_attrib(jingle, "xmlns", JINGLE_NS);
	}

	iks_insert_node(iq, jingle);
	iks_insert_node(jingle, reason);
	iks_insert_node(reason, text);

	ast_xmpp_client_send(session->connection, iq);

end:
	iks_delete(text);
	iks_delete(reason);
	iks_delete(jingle);
	iks_delete(iq);
}

/*! \brief Internal function which sends a session-info message */
static void jingle_send_session_info(struct jingle_session *session, const char *info)
{
	iks *iq = NULL, *jingle = NULL, *text = NULL;

	/* Google-V1 has no way to send informational messages so don't even bother trying */
	if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
		return;
	}

	if (!(iq = iks_new("iq")) || !(jingle = iks_new("jingle")) || !(text = iks_new(info))) {
		ast_log(LOG_ERROR, "Failed to allocate stanzas for session-info message on session '%s'\n", session->sid);
		goto end;
	}

	iks_insert_attrib(iq, "to", session->remote);
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "id", session->connection->mid);
	ast_xmpp_increment_mid(session->connection->mid);

	iks_insert_attrib(jingle, "action", "session-info");
	iks_insert_attrib(jingle, "sid", session->sid);
	iks_insert_attrib(jingle, "xmlns", JINGLE_NS);
	iks_insert_node(iq, jingle);
	iks_insert_node(jingle, text);

	ast_xmpp_client_send(session->connection, iq);

end:
	iks_delete(text);
	iks_delete(jingle);
	iks_delete(iq);
}

/*! \internal
 *
 * \brief Locks both pvt and pvt owner if owner is present.
 *
 * \note This function gives a ref to pvt->owner if it is present and locked.
 *       This reference must be decremented after pvt->owner is unlocked.
 *
 * \note This function will never give you up,
 * \note This function will never let you down.
 * \note This function will run around and desert you.
 *
 * \pre pvt is not locked
 * \post pvt is locked
 * \post pvt->owner is locked and its reference count is increased (if pvt->owner is not NULL)
 *
 * \returns a pointer to the locked and reffed pvt->owner channel if it exists.
 */
static struct ast_channel *jingle_session_lock_full(struct jingle_session *pvt)
{
	struct ast_channel *chan;

	/* Locking is simple when it is done right.  If you see a deadlock resulting
	 * in this function, it is not this function's fault, Your problem exists elsewhere.
	 * This function is perfect... seriously. */
	for (;;) {
		/* First, get the channel and grab a reference to it */
		ao2_lock(pvt);
		chan = pvt->owner;
		if (chan) {
			/* The channel can not go away while we hold the pvt lock.
			 * Give the channel a ref so it will not go away after we let
			 * the pvt lock go. */
			ast_channel_ref(chan);
		} else {
			/* no channel, return pvt locked */
			return NULL;
		}

		/* We had to hold the pvt lock while getting a ref to the owner channel
		 * but now we have to let this lock go in order to preserve proper
		 * locking order when grabbing the channel lock */
		ao2_unlock(pvt);

		/* Look, no deadlock avoidance, hooray! */
		ast_channel_lock(chan);
		ao2_lock(pvt);
		if (pvt->owner == chan) {
			/* done */
			break;
		}

		/* If the owner changed while everything was unlocked, no problem,
		 * just start over and everthing will work.  This is rare, do not be
		 * confused by this loop and think this it is an expensive operation.
		 * The majority of the calls to this function will never involve multiple
		 * executions of this loop. */
		ast_channel_unlock(chan);
		ast_channel_unref(chan);
		ao2_unlock(pvt);
	}

	/* If owner exists, it is locked and reffed */
	return pvt->owner;
}

/*! \brief Helper function which queues a hangup frame with cause code */
static void jingle_queue_hangup_with_cause(struct jingle_session *session, int cause)
{
	struct ast_channel *chan;

	if ((chan = jingle_session_lock_full(session))) {
		ast_debug(3, "Hanging up channel '%s' with cause '%d'\n", ast_channel_name(chan), cause);
		ast_queue_hangup_with_cause(chan, cause);
		ast_channel_unlock(chan);
		ast_channel_unref(chan);
	}
	ao2_unlock(session);
}

/*! \brief Internal function which sends a transport-info message */
static void jingle_send_transport_info(struct jingle_session *session, const char *from)
{
	iks *iq, *jingle = NULL, *audio = NULL, *audio_transport = NULL, *video = NULL, *video_transport = NULL;
	iks *audio_candidates[session->maxicecandidates], *video_candidates[session->maxicecandidates];
	int i, res = 0;

	if (!(iq = iks_new("iq")) ||
	    !(jingle = iks_new(session->transport == JINGLE_TRANSPORT_GOOGLE_V1 ? "session" : "jingle"))) {
		iks_delete(iq);
		jingle_queue_hangup_with_cause(session, AST_CAUSE_SWITCH_CONGESTION);
		ast_log(LOG_ERROR, "Failed to allocate stanzas for transport-info message, hanging up session '%s'\n", session->sid);
		return;
	}

	memset(audio_candidates, 0, sizeof(audio_candidates));
	memset(video_candidates, 0, sizeof(video_candidates));

	iks_insert_attrib(iq, "from", session->connection->jid->full);
	iks_insert_attrib(iq, "to", from);
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "id", session->connection->mid);
	ast_xmpp_increment_mid(session->connection->mid);

	if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
		iks_insert_attrib(jingle, "type", "candidates");
		iks_insert_attrib(jingle, "id", session->sid);
		iks_insert_attrib(jingle, "xmlns", GOOGLE_SESSION_NS);
		iks_insert_attrib(jingle, "initiator", session->outgoing ? session->connection->jid->full : from);
	} else {
		iks_insert_attrib(jingle, "action", "transport-info");
		iks_insert_attrib(jingle, "sid", session->sid);
		iks_insert_attrib(jingle, "xmlns", JINGLE_NS);
	}
	iks_insert_node(iq, jingle);

	if (session->rtp) {
		if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
			/* V1 protocol has the candidates directly in the session */
			res = jingle_add_google_candidates_to_transport(session->rtp, jingle, audio_candidates, 0, session->transport, session->maxicecandidates);
		} else if ((audio = iks_new("content")) && (audio_transport = iks_new("transport"))) {
			iks_insert_attrib(audio, "creator", session->outgoing ? "initiator" : "responder");
			iks_insert_attrib(audio, "name", session->audio_name);
			iks_insert_node(jingle, audio);
			iks_insert_node(audio, audio_transport);

			if (session->transport == JINGLE_TRANSPORT_ICE_UDP) {
				res = jingle_add_ice_udp_candidates_to_transport(session->rtp, audio_transport, audio_candidates, session->maxicecandidates);
			} else if (session->transport == JINGLE_TRANSPORT_GOOGLE_V2) {
				res = jingle_add_google_candidates_to_transport(session->rtp, audio_transport, audio_candidates, 0, session->transport,
										session->maxicecandidates);
			}
		} else {
			res = -1;
		}
	}

	if ((session->transport != JINGLE_TRANSPORT_GOOGLE_V1) && !res && session->vrtp) {
		if ((video = iks_new("content")) && (video_transport = iks_new("transport"))) {
			iks_insert_attrib(video, "creator", session->outgoing ? "initiator" : "responder");
			iks_insert_attrib(video, "name", session->video_name);
			iks_insert_node(jingle, video);
			iks_insert_node(video, video_transport);

			if (session->transport == JINGLE_TRANSPORT_ICE_UDP) {
				res = jingle_add_ice_udp_candidates_to_transport(session->vrtp, video_transport, video_candidates, session->maxicecandidates);
			} else if (session->transport == JINGLE_TRANSPORT_GOOGLE_V2) {
				res = jingle_add_google_candidates_to_transport(session->vrtp, video_transport, video_candidates, 1, session->transport,
										session->maxicecandidates);
			}
		} else {
			res = -1;
		}
	}

	if (!res) {
		ast_xmpp_client_send(session->connection, iq);
	} else {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_SWITCH_CONGESTION);
	}

	/* Clean up after ourselves */
	for (i = 0; i < session->maxicecandidates; i++) {
		iks_delete(video_candidates[i]);
		iks_delete(audio_candidates[i]);
	}

	iks_delete(video_transport);
	iks_delete(video);
	iks_delete(audio_transport);
	iks_delete(audio);
	iks_delete(jingle);
	iks_delete(iq);
}

/*! \brief Internal helper function which adds payloads to a description */
static int jingle_add_payloads_to_description(struct jingle_session *session, struct ast_rtp_instance *rtp, iks *description, iks **payloads, enum ast_format_type type)
{
	struct ast_format format;
	int x = 0, i = 0, res = 0;

	for (x = 0; (x < AST_CODEC_PREF_SIZE) && (i < (session->maxpayloads - 2)); x++) {
		int rtp_code;
		iks *payload;
		char tmp[32];

		if (!ast_codec_pref_index(&session->prefs, x, &format)) {
			break;
		}

		if (AST_FORMAT_GET_TYPE(format.id) != type) {
			continue;
		}

		if (!ast_format_cap_iscompatible(session->jointcap, &format)) {
			continue;
		}

		if (((rtp_code = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(rtp), 1, &format, 0)) == -1) ||
		    (!(payload = iks_new("payload-type")))) {
			return -1;
		}

		if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
			iks_insert_attrib(payload, "xmlns", GOOGLE_PHONE_NS);
		}

		snprintf(tmp, sizeof(tmp), "%d", rtp_code);
		iks_insert_attrib(payload, "id", tmp);
		iks_insert_attrib(payload, "name", ast_rtp_lookup_mime_subtype2(1, &format, 0, 0));
		iks_insert_attrib(payload, "channels", "1");

		if ((format.id == AST_FORMAT_G722) && ((session->transport == JINGLE_TRANSPORT_GOOGLE_V1) || (session->transport == JINGLE_TRANSPORT_GOOGLE_V2))) {
			iks_insert_attrib(payload, "clockrate", "16000");
		} else {
			snprintf(tmp, sizeof(tmp), "%u", ast_rtp_lookup_sample_rate2(1, &format, 0));
			iks_insert_attrib(payload, "clockrate", tmp);
		}

		if ((type == AST_FORMAT_TYPE_VIDEO) && (session->transport == JINGLE_TRANSPORT_GOOGLE_V2)) {
			iks *parameter;

			/* Google requires these parameters to be set, but alas we can not give accurate values so use some safe defaults */
			if ((parameter = iks_new("parameter"))) {
				iks_insert_attrib(parameter, "name", "width");
				iks_insert_attrib(parameter, "value", "640");
				iks_insert_node(payload, parameter);
			}
			if ((parameter = iks_new("parameter"))) {
				iks_insert_attrib(parameter, "name", "height");
				iks_insert_attrib(parameter, "value", "480");
				iks_insert_node(payload, parameter);
			}
			if ((parameter = iks_new("parameter"))) {
				iks_insert_attrib(parameter, "name", "framerate");
				iks_insert_attrib(parameter, "value", "30");
				iks_insert_node(payload, parameter);
			}
		}

		iks_insert_node(description, payload);
		payloads[i++] = payload;
	}
	/* If this is for audio and there is room for RFC2833 add it in */
	if ((type == AST_FORMAT_TYPE_AUDIO) && (i < session->maxpayloads)) {
		iks *payload;

		if ((payload = iks_new("payload-type"))) {
			if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
				iks_insert_attrib(payload, "xmlns", GOOGLE_PHONE_NS);
			}

			iks_insert_attrib(payload, "id", "101");
			iks_insert_attrib(payload, "name", "telephone-event");
			iks_insert_attrib(payload, "channels", "1");
			iks_insert_attrib(payload, "clockrate", "8000");
			iks_insert_node(description, payload);
			payloads[i++] = payload;
		}
	}

	return res;
}

/*! \brief Helper function which adds content to a description */
static int jingle_add_content(struct jingle_session *session, iks *jingle, iks *content, iks *description, iks *transport,
			      const char *name, enum ast_format_type type, struct ast_rtp_instance *rtp, iks **payloads)
{
	int res = 0;

	if (session->transport != JINGLE_TRANSPORT_GOOGLE_V1) {
		iks_insert_attrib(content, "creator", session->outgoing ? "initiator" : "responder");
		iks_insert_attrib(content, "name", name);
		iks_insert_node(jingle, content);

		iks_insert_attrib(description, "xmlns", JINGLE_RTP_NS);
		if (type == AST_FORMAT_TYPE_AUDIO) {
			iks_insert_attrib(description, "media", "audio");
		} else if (type == AST_FORMAT_TYPE_VIDEO) {
			iks_insert_attrib(description, "media", "video");
		} else {
			return -1;
		}
		iks_insert_node(content, description);
	} else {
		iks_insert_attrib(description, "xmlns", GOOGLE_PHONE_NS);
		iks_insert_node(jingle, description);
	}

	if (!(res = jingle_add_payloads_to_description(session, rtp, description, payloads, type))) {
		if (session->transport == JINGLE_TRANSPORT_ICE_UDP) {
			iks_insert_attrib(transport, "xmlns", JINGLE_ICE_UDP_NS);
			iks_insert_node(content, transport);
		} else if (session->transport == JINGLE_TRANSPORT_GOOGLE_V2) {
			iks_insert_attrib(transport, "xmlns", GOOGLE_TRANSPORT_NS);
			iks_insert_node(content, transport);
		}
	}

	return res;
}

/*! \brief Internal function which sends a complete session message */
static void jingle_send_session_action(struct jingle_session *session, const char *action)
{
	iks *iq, *jingle, *audio = NULL, *audio_description = NULL, *video = NULL, *video_description = NULL;
	iks *audio_payloads[session->maxpayloads], *video_payloads[session->maxpayloads];
	iks *audio_transport = NULL, *video_transport = NULL;
	int i, res = 0;

	if (!(iq = iks_new("iq")) ||
	    !(jingle = iks_new(session->transport == JINGLE_TRANSPORT_GOOGLE_V1 ? "session" : "jingle"))) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_SWITCH_CONGESTION);
		iks_delete(iq);
		return;
	}

	memset(audio_payloads, 0, sizeof(audio_payloads));
	memset(video_payloads, 0, sizeof(video_payloads));

	iks_insert_attrib(iq, "from", session->connection->jid->full);
	iks_insert_attrib(iq, "to", session->remote);
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "id", session->connection->mid);
	ast_xmpp_increment_mid(session->connection->mid);

	if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
		iks_insert_attrib(jingle, "type", action);
		iks_insert_attrib(jingle, "id", session->sid);
		iks_insert_attrib(jingle, "xmlns", GOOGLE_SESSION_NS);
	} else {
		iks_insert_attrib(jingle, "action", action);
		iks_insert_attrib(jingle, "sid", session->sid);
		iks_insert_attrib(jingle, "xmlns", JINGLE_NS);
	}

	if (!strcasecmp(action, "session-initiate") || !strcasecmp(action, "initiate") || !strcasecmp(action, "accept")) {
		iks_insert_attrib(jingle, "initiator", session->outgoing ? session->connection->jid->full : session->remote);
	}

	iks_insert_node(iq, jingle);

	if (session->rtp && (audio = iks_new("content")) && (audio_description = iks_new("description")) &&
	    (audio_transport = iks_new("transport"))) {
		res = jingle_add_content(session, jingle, audio, audio_description, audio_transport, session->audio_name,
					 AST_FORMAT_TYPE_AUDIO, session->rtp, audio_payloads);
	} else {
		ast_log(LOG_ERROR, "Failed to allocate audio content stanzas for session '%s', hanging up\n", session->sid);
		res = -1;
	}

	if ((session->transport != JINGLE_TRANSPORT_GOOGLE_V1) && !res && session->vrtp) {
		if ((video = iks_new("content")) && (video_description = iks_new("description")) &&
		    (video_transport = iks_new("transport"))) {
			res = jingle_add_content(session, jingle, video, video_description, video_transport, session->video_name,
						 AST_FORMAT_TYPE_VIDEO, session->vrtp, video_payloads);
		} else {
			ast_log(LOG_ERROR, "Failed to allocate video content stanzas for session '%s', hanging up\n", session->sid);
			res = -1;
		}
	}

	if (!res) {
		ast_xmpp_client_send(session->connection, iq);
	} else {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_SWITCH_CONGESTION);
	}

	iks_delete(video_transport);
	iks_delete(audio_transport);

	for (i = 0; i < session->maxpayloads; i++) {
		iks_delete(video_payloads[i]);
		iks_delete(audio_payloads[i]);
	}

	iks_delete(video_description);
	iks_delete(video);
	iks_delete(audio_description);
	iks_delete(audio);
	iks_delete(jingle);
	iks_delete(iq);
}

/*! \brief Internal function which sends a session-inititate message */
static void jingle_send_session_initiate(struct jingle_session *session)
{
	jingle_send_session_action(session, session->transport == JINGLE_TRANSPORT_GOOGLE_V1 ? "initiate" : "session-initiate");
}

/*! \brief Internal function which sends a session-accept message */
static void jingle_send_session_accept(struct jingle_session *session)
{
	jingle_send_session_action(session, session->transport == JINGLE_TRANSPORT_GOOGLE_V1 ? "accept" : "session-accept");
}

/*! \brief Callback for when a response is received for an outgoing session-initiate message */
static int jingle_outgoing_hook(void *data, ikspak *pak)
{
	struct jingle_session *session = data;
	iks *error = iks_find(pak->x, "error"), *redirect;

	/* In all cases this hook is done with */
	iks_filter_remove_rule(session->connection->filter, session->rule);
	session->rule = NULL;

	ast_callid_threadassoc_add(session->callid);

	/* If no error occurred they accepted our session-initiate message happily */
	if (!error) {
		struct ast_channel *chan;

		if ((chan = jingle_session_lock_full(session))) {
			ast_queue_control(chan, AST_CONTROL_PROCEEDING);
			ast_channel_unlock(chan);
			ast_channel_unref(chan);
		}
		ao2_unlock(session);

		jingle_send_transport_info(session, iks_find_attrib(pak->x, "from"));

		goto end;
	}

	/* Assume that because this is an error the session is gone, there is only one case where this is incorrect - a redirect */
	session->gone = 1;

	/* Map the error we received to an appropriate cause code and hang up the channel */
	if ((redirect = iks_find_with_attrib(error, "redirect", "xmlns", XMPP_STANZAS_NS))) {
		iks *to = iks_child(redirect);
		char *target;

		if (to && (target = iks_name(to)) && !ast_strlen_zero(target)) {
			/* Make the xmpp: go away if it is present */
			if (!strncmp(target, "xmpp:", 5)) {
				target += 5;
			}

			/* This is actually a fairly simple operation - we update the remote and send another session-initiate */
			ast_copy_string(session->remote, target, sizeof(session->remote));

			/* Add a new hook so we can get the status of redirected session */
			session->rule = iks_filter_add_rule(session->connection->filter, jingle_outgoing_hook, session,
							    IKS_RULE_ID, session->connection->mid, IKS_RULE_DONE);

			jingle_send_session_initiate(session);

			session->gone = 0;
		} else {
			jingle_queue_hangup_with_cause(session, AST_CAUSE_PROTOCOL_ERROR);
		}
	} else if (iks_find_with_attrib(error, "service-unavailable", "xmlns", XMPP_STANZAS_NS)) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_CONGESTION);
	} else if (iks_find_with_attrib(error, "resource-constraint", "xmlns", XMPP_STANZAS_NS)) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_REQUESTED_CHAN_UNAVAIL);
	} else if (iks_find_with_attrib(error, "bad-request", "xmlns", XMPP_STANZAS_NS)) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_PROTOCOL_ERROR);
	} else if (iks_find_with_attrib(error, "remote-server-not-found", "xmlns", XMPP_STANZAS_NS)) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_NO_ROUTE_DESTINATION);
	} else if (iks_find_with_attrib(error, "feature-not-implemented", "xmlns", XMPP_STANZAS_NS)) {
		/* Assume that this occurred because the remote side does not support our transport, so drop it down one and try again */
		session->transport--;

		/* If we still have a viable transport mechanism re-send the session-initiate */
		if (session->transport != JINGLE_TRANSPORT_NONE) {
			struct ast_rtp_engine_ice *ice;

			if (((session->transport == JINGLE_TRANSPORT_GOOGLE_V2) ||
			     (session->transport == JINGLE_TRANSPORT_GOOGLE_V1)) &&
			    (ice = ast_rtp_instance_get_ice(session->rtp))) {
				/* We stop built in ICE support because we need to fall back to old old old STUN support */
				ice->stop(session->rtp);
			}

			/* Re-send the message to the *original* target and not a redirected one */
			ast_copy_string(session->remote, session->remote_original, sizeof(session->remote));

			session->rule = iks_filter_add_rule(session->connection->filter, jingle_outgoing_hook, session,
							    IKS_RULE_ID, session->connection->mid, IKS_RULE_DONE);

			jingle_send_session_initiate(session);

			session->gone = 0;
		} else {
			/* Otherwise we have exhausted all transports */
			jingle_queue_hangup_with_cause(session, AST_CAUSE_FACILITY_NOT_IMPLEMENTED);
		}
	} else {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_PROTOCOL_ERROR);
	}

end:
	ast_callid_threadassoc_remove();

	return IKS_FILTER_EAT;
}

/*! \brief Function called by core when we should answer a Jingle session */
static int jingle_answer(struct ast_channel *ast)
{
	struct jingle_session *session = ast_channel_tech_pvt(ast);

	/* The channel has already been answered so we don't need to do anything */
	if (ast_channel_state(ast) == AST_STATE_UP) {
		return 0;
	}

	jingle_send_session_accept(session);

	return 0;
}

/*! \brief Function called by core to read any waiting frames */
static struct ast_frame *jingle_read(struct ast_channel *ast)
{
	struct jingle_session *session = ast_channel_tech_pvt(ast);
	struct ast_frame *frame = &ast_null_frame;

	switch (ast_channel_fdno(ast)) {
	case 0:
		if (session->rtp) {
			frame = ast_rtp_instance_read(session->rtp, 0);
		}
		break;
	case 1:
		if (session->rtp) {
			frame = ast_rtp_instance_read(session->rtp, 1);
		}
		break;
	case 2:
		if (session->vrtp) {
			frame = ast_rtp_instance_read(session->vrtp, 0);
		}
		break;
	case 3:
		if (session->vrtp) {
			frame = ast_rtp_instance_read(session->vrtp, 1);
		}
		break;
	default:
		break;
	}

	if (frame && frame->frametype == AST_FRAME_VOICE &&
	    !ast_format_cap_iscompatible(ast_channel_nativeformats(ast), &frame->subclass.format)) {
		if (!ast_format_cap_iscompatible(session->jointcap, &frame->subclass.format)) {
			ast_debug(1, "Bogus frame of format '%s' received from '%s'!\n",
				  ast_getformatname(&frame->subclass.format), ast_channel_name(ast));
			ast_frfree(frame);
			frame = &ast_null_frame;
		} else {
			ast_debug(1, "Oooh, format changed to %s\n",
				  ast_getformatname(&frame->subclass.format));
			ast_format_cap_remove_bytype(ast_channel_nativeformats(ast), AST_FORMAT_TYPE_AUDIO);
			ast_format_cap_add(ast_channel_nativeformats(ast), &frame->subclass.format);
			ast_set_read_format(ast, ast_channel_readformat(ast));
			ast_set_write_format(ast, ast_channel_writeformat(ast));
		}
	}

	return frame;
}

/*! \brief Function called by core to write frames */
static int jingle_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct jingle_session *session = ast_channel_tech_pvt(ast);
	int res = 0;
	char buf[256];

	switch (frame->frametype) {
	case AST_FRAME_VOICE:
		if (!(ast_format_cap_iscompatible(ast_channel_nativeformats(ast), &frame->subclass.format))) {
			ast_log(LOG_WARNING,
				"Asked to transmit frame type %s, while native formats is %s (read/write = %s/%s)\n",
				ast_getformatname(&frame->subclass.format),
				ast_getformatname_multiple(buf, sizeof(buf), ast_channel_nativeformats(ast)),
				ast_getformatname(ast_channel_readformat(ast)),
				ast_getformatname(ast_channel_writeformat(ast)));
			return 0;
		}
		if (session && session->rtp) {
			res = ast_rtp_instance_write(session->rtp, frame);
		}
		break;
	case AST_FRAME_VIDEO:
		if (session && session->vrtp) {
			res = ast_rtp_instance_write(session->vrtp, frame);
		}
		break;
	default:
		ast_log(LOG_WARNING, "Can't send %u type frames with Jingle write\n",
			frame->frametype);
		return 0;
	}

	return res;
}

/*! \brief Function called by core to change the underlying owner channel */
static int jingle_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct jingle_session *session = ast_channel_tech_pvt(newchan);

	ao2_lock(session);

	jingle_set_owner(session, newchan);

	ao2_unlock(session);

	return 0;
}

/*! \brief Function called by core to ask the channel to indicate some sort of condition */
static int jingle_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	struct jingle_session *session = ast_channel_tech_pvt(ast);
	int res = 0;

	switch (condition) {
	case AST_CONTROL_RINGING:
		if (ast_channel_state(ast) == AST_STATE_RING) {
			jingle_send_session_info(session, "ringing xmlns='urn:xmpp:jingle:apps:rtp:info:1'");
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_BUSY:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			ast_channel_hangupcause_set(ast, AST_CAUSE_BUSY);
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_CONGESTION:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			ast_channel_hangupcause_set(ast, AST_CAUSE_CONGESTION);
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_INCOMPLETE:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			ast_channel_hangupcause_set(ast, AST_CAUSE_CONGESTION);
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
		}
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_SRCUPDATE:
		if (session->rtp) {
			ast_rtp_instance_update_source(session->rtp);
		}
		break;
	case AST_CONTROL_SRCCHANGE:
		if (session->rtp) {
			ast_rtp_instance_change_source(session->rtp);
		}
		break;
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_UPDATE_RTP_PEER:
	case AST_CONTROL_CONNECTED_LINE:
		break;
	case AST_CONTROL_PVT_CAUSE_CODE:
	case AST_CONTROL_MASQUERADE_NOTIFY:
	case -1:
		res = -1;
		break;
	default:
		ast_log(LOG_NOTICE, "Don't know how to indicate condition '%d'\n", condition);
		res = -1;
	}

	return res;
}

/*! \brief Function called by core to send text to the remote party of the Jingle session */
static int jingle_sendtext(struct ast_channel *chan, const char *text)
{
	struct jingle_session *session = ast_channel_tech_pvt(chan);

	return ast_xmpp_client_send_message(session->connection, session->remote, text);
}

/*! \brief Function called by core to start a DTMF digit */
static int jingle_digit_begin(struct ast_channel *chan, char digit)
{
	struct jingle_session *session = ast_channel_tech_pvt(chan);

	if (session->rtp) {
		ast_rtp_instance_dtmf_begin(session->rtp, digit);
	}

	return 0;
}

/*! \brief Function called by core to stop a DTMF digit */
static int jingle_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct jingle_session *session = ast_channel_tech_pvt(ast);

	if (session->rtp) {
		ast_rtp_instance_dtmf_end_with_duration(session->rtp, digit, duration);
	}

	return 0;
}

/*! \brief Function called by core to actually start calling a remote party */
static int jingle_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct jingle_session *session = ast_channel_tech_pvt(ast);

	ast_setstate(ast, AST_STATE_RING);

	/* Since we have no idea of the remote capabilities use ours for now */
	ast_format_cap_copy(session->jointcap, session->cap);

	/* We set up a hook so we can know when our session-initiate message was accepted or rejected */
	session->rule = iks_filter_add_rule(session->connection->filter, jingle_outgoing_hook, session,
					    IKS_RULE_ID, session->connection->mid, IKS_RULE_DONE);

	jingle_send_session_initiate(session);

	return 0;
}

/*! \brief Function called by core to hang up a Jingle session */
static int jingle_hangup(struct ast_channel *ast)
{
	struct jingle_session *session = ast_channel_tech_pvt(ast);

	ao2_lock(session);

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && !session->gone) {
		int cause = (session->owner ? ast_channel_hangupcause(session->owner) : AST_CAUSE_CONGESTION);
		const char *reason = "success";
		int i;

		/* Get the appropriate reason and send a session-terminate */
		for (i = 0; i < ARRAY_LEN(jingle_reason_mappings); i++) {
			if (jingle_reason_mappings[i].cause == cause) {
				reason = jingle_reason_mappings[i].reason;
				break;
			}
		}

		jingle_send_session_terminate(session, reason);
	}

	ast_channel_tech_pvt_set(ast, NULL);
	jingle_set_owner(session, NULL);

	ao2_unlink(session->state->sessions, session);
	ao2_ref(session->state, -1);

	ao2_unlock(session);
	ao2_ref(session, -1);

	return 0;
}

/*! \brief Function called by core to create a new outgoing Jingle session */
static struct ast_channel *jingle_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	RAII_VAR(struct jingle_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct jingle_endpoint *, endpoint, NULL, ao2_cleanup);
	char *dialed, target[200] = "";
	struct ast_xmpp_buddy *buddy;
	struct jingle_session *session;
	struct ast_channel *chan;
	enum jingle_transport transport = JINGLE_TRANSPORT_NONE;
	struct ast_rtp_engine_ice *ice;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(name);
			     AST_APP_ARG(target);
		);

	/* We require at a minimum one audio format to be requested */
	if (!ast_format_cap_has_type(cap, AST_FORMAT_TYPE_AUDIO)) {
		ast_log(LOG_ERROR, "Motif channel driver requires an audio format when dialing a destination\n");
		*cause = AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
		return NULL;
	}

	if (ast_strlen_zero(data) || !(dialed = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Unable to create channel with empty destination.\n");
		*cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
		return NULL;
	}

	/* Parse the given dial string and validate the results */
	AST_NONSTANDARD_APP_ARGS(args, dialed, '/');

	if (ast_strlen_zero(args.name) || ast_strlen_zero(args.target)) {
		ast_log(LOG_ERROR, "Unable to determine endpoint name and target.\n");
		*cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
		return NULL;
	}

	if (!(endpoint = jingle_endpoint_find(cfg->endpoints, args.name))) {
		ast_log(LOG_ERROR, "Endpoint '%s' does not exist.\n", args.name);
		*cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
		return NULL;
	}

	ao2_lock(endpoint->state);

	/* If we don't have a connection for the endpoint we can't exactly start a session on it */
	if (!endpoint->connection) {
		ast_log(LOG_ERROR, "Unable to create Jingle session on endpoint '%s' as no valid connection exists\n", args.name);
		*cause = AST_CAUSE_SWITCH_CONGESTION;
		ao2_unlock(endpoint->state);
		return NULL;
	}

	/* Find the target in the roster so we can choose a resource */
	if ((buddy = ao2_find(endpoint->connection->buddies, args.target, OBJ_KEY))) {
		struct ao2_iterator res;
		struct ast_xmpp_resource *resource;

		/* Iterate through finding the first viable Jingle capable resource */
		res = ao2_iterator_init(buddy->resources, 0);
		while ((resource = ao2_iterator_next(&res))) {
			if (resource->caps.jingle) {
				snprintf(target, sizeof(target), "%s/%s", args.target, resource->resource);
				transport = JINGLE_TRANSPORT_ICE_UDP;
				break;
			} else if (resource->caps.google) {
				snprintf(target, sizeof(target), "%s/%s", args.target, resource->resource);
				transport = JINGLE_TRANSPORT_GOOGLE_V2;
				break;
			}
			ao2_ref(resource, -1);
		}
		ao2_iterator_destroy(&res);

		ao2_ref(buddy, -1);
	} else {
		/* If the target is NOT in the roster use the provided target as-is */
		ast_copy_string(target, args.target, sizeof(target));
	}

	ao2_unlock(endpoint->state);

	/* If no target was found we can't set up a session */
	if (ast_strlen_zero(target)) {
		ast_log(LOG_ERROR, "Unable to create Jingle session on endpoint '%s' as no capable resource for target '%s' was found\n", args.name, args.target);
		*cause = AST_CAUSE_SWITCH_CONGESTION;
		return NULL;
	}

	if (!(session = jingle_alloc(endpoint, target, NULL))) {
		ast_log(LOG_ERROR, "Unable to create Jingle session on endpoint '%s'\n", args.name);
		*cause = AST_CAUSE_SWITCH_CONGESTION;
		return NULL;
	}

	/* Update the transport if we learned what we should actually use */
	if (transport != JINGLE_TRANSPORT_NONE) {
		session->transport = transport;
		/* Note that for Google-V1 and Google-V2 we don't stop built-in ICE support, this will happen in jingle_new */
	}

	if (!(chan = jingle_new(endpoint, session, AST_STATE_DOWN, target, assignedids, requestor, NULL))) {
		ast_log(LOG_ERROR, "Unable to create Jingle channel on endpoint '%s'\n", args.name);
		*cause = AST_CAUSE_SWITCH_CONGESTION;
		ao2_ref(session, -1);
		return NULL;
	}

	/* If video was requested try to enable it on the session */
	if (ast_format_cap_has_type(cap, AST_FORMAT_TYPE_VIDEO)) {
		jingle_enable_video(session);
	}

	/* As this is outgoing set ourselves as controlling */
	if (session->rtp && (ice = ast_rtp_instance_get_ice(session->rtp))) {
		ice->ice_lite(session->rtp);
	}

	if (session->vrtp && (ice = ast_rtp_instance_get_ice(session->vrtp))) {
		ice->ice_lite(session->vrtp);
	}

	/* We purposely don't decrement the session here as there is a reference on the channel */
	ao2_link(endpoint->state->sessions, session);

	return chan;
}

/*! \brief Helper function which handles content descriptions */
static int jingle_interpret_description(struct jingle_session *session, iks *description, const char *name, struct ast_rtp_instance **rtp)
{
	char *media = iks_find_attrib(description, "media");
	struct ast_rtp_codecs codecs;
	iks *codec;
	int othercapability = 0;

	/* Google-V1 is always carrying audio, but just doesn't tell us so */
	if (session->transport == JINGLE_TRANSPORT_GOOGLE_V1) {
		media = "audio";
	} else if (ast_strlen_zero(media)) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
		ast_log(LOG_ERROR, "Received a content description on session '%s' without a name\n", session->sid);
		return -1;
	}

	/* Determine the type of media that is being carried and update the RTP instance, as well as the name */
	if (!strcasecmp(media, "audio")) {
		if (!ast_strlen_zero(name)) {
			ast_string_field_set(session, audio_name, name);
		}
		*rtp = session->rtp;
		ast_format_cap_remove_bytype(session->peercap, AST_FORMAT_TYPE_AUDIO);
		ast_format_cap_remove_bytype(session->jointcap, AST_FORMAT_TYPE_AUDIO);
	} else if (!strcasecmp(media, "video")) {
		if (!ast_strlen_zero(name)) {
			ast_string_field_set(session, video_name, name);
		}

		jingle_enable_video(session);
		*rtp = session->vrtp;

		/* If video is not present cancel this session */
		if (!session->vrtp) {
			jingle_queue_hangup_with_cause(session, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
			ast_log(LOG_ERROR, "Received a video content description on session '%s' but could not enable video\n", session->sid);
			return -1;
		}

		ast_format_cap_remove_bytype(session->peercap, AST_FORMAT_TYPE_VIDEO);
		ast_format_cap_remove_bytype(session->jointcap, AST_FORMAT_TYPE_VIDEO);
	} else {
		/* Unknown media type */
		jingle_queue_hangup_with_cause(session, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
		ast_log(LOG_ERROR, "Unsupported media type '%s' received in content description on session '%s'\n", media, session->sid);
		return -1;
	}

	if (ast_rtp_codecs_payloads_initialize(&codecs)) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
		ast_log(LOG_ERROR, "Could not initialize codecs for negotiation on session '%s'\n", session->sid);
		return -1;
	}

	/* Iterate the codecs updating the relevant RTP instance as we go */
	for (codec = iks_child(description); codec; codec = iks_next(codec)) {
		char *id = iks_find_attrib(codec, "id"), *name = iks_find_attrib(codec, "name");
		char *clockrate = iks_find_attrib(codec, "clockrate");
		int rtp_id, rtp_clockrate;

		if (!ast_strlen_zero(id) && !ast_strlen_zero(name) && (sscanf(id, "%30d", &rtp_id) == 1)) {
			ast_rtp_codecs_payloads_set_m_type(&codecs, NULL, rtp_id);

			if (!ast_strlen_zero(clockrate) && (sscanf(clockrate, "%30d", &rtp_clockrate) == 1)) {
				ast_rtp_codecs_payloads_set_rtpmap_type_rate(&codecs, NULL, rtp_id, media, name, 0, rtp_clockrate);
			} else {
				ast_rtp_codecs_payloads_set_rtpmap_type(&codecs, NULL, rtp_id, media, name, 0);
			}
		}
	}

	ast_rtp_codecs_payload_formats(&codecs, session->peercap, &othercapability);
	ast_format_cap_joint_append(session->cap, session->peercap, session->jointcap);

	if (ast_format_cap_is_empty(session->jointcap)) {
		/* We have no compatible codecs, so terminate the session appropriately */
		jingle_queue_hangup_with_cause(session, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
		ast_rtp_codecs_payloads_destroy(&codecs);
		return -1;
	}

	ast_rtp_codecs_payloads_copy(&codecs, ast_rtp_instance_get_codecs(*rtp), *rtp);
	ast_rtp_codecs_payloads_destroy(&codecs);

	return 0;
}

/*! \brief Helper function which handles ICE-UDP transport information */
static int jingle_interpret_ice_udp_transport(struct jingle_session *session, iks *transport, struct ast_rtp_instance *rtp)
{
	struct ast_rtp_engine_ice *ice = ast_rtp_instance_get_ice(rtp);
	char *ufrag = iks_find_attrib(transport, "ufrag"), *pwd = iks_find_attrib(transport, "pwd");
	iks *candidate;

	if (!ice) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_SWITCH_CONGESTION);
		ast_log(LOG_ERROR, "Received ICE-UDP transport information on session '%s' but ICE support not available\n", session->sid);
		return -1;
	}

	if (!ast_strlen_zero(ufrag) && !ast_strlen_zero(pwd)) {
		ice->set_authentication(rtp, ufrag, pwd);
	}

	for (candidate = iks_child(transport); candidate; candidate = iks_next(candidate)) {
		char *component = iks_find_attrib(candidate, "component"), *foundation = iks_find_attrib(candidate, "foundation");
		char *generation = iks_find_attrib(candidate, "generation"), *id = iks_find_attrib(candidate, "id");
		char *ip = iks_find_attrib(candidate, "ip"), *port = iks_find_attrib(candidate, "port");
		char *priority = iks_find_attrib(candidate, "priority"), *protocol = iks_find_attrib(candidate, "protocol");
		char *type = iks_find_attrib(candidate, "type");
		struct ast_rtp_engine_ice_candidate local_candidate = { 0, };
		int real_port;
		struct ast_sockaddr remote_address = { { 0, } };

		/* If this candidate is incomplete skip it */
		if (ast_strlen_zero(component) || ast_strlen_zero(foundation) || ast_strlen_zero(generation) || ast_strlen_zero(id) ||
		    ast_strlen_zero(ip) || ast_strlen_zero(port) || ast_strlen_zero(priority) ||
		    ast_strlen_zero(protocol) || ast_strlen_zero(type)) {
			jingle_queue_hangup_with_cause(session, AST_CAUSE_PROTOCOL_ERROR);
			ast_log(LOG_ERROR, "Incomplete ICE-UDP candidate received on session '%s'\n", session->sid);
			return -1;
		}

		if ((sscanf(component, "%30u", &local_candidate.id) != 1) ||
		    (sscanf(priority, "%30u", (unsigned *)&local_candidate.priority) != 1) ||
		    (sscanf(port, "%30d", &real_port) != 1)) {
			jingle_queue_hangup_with_cause(session, AST_CAUSE_PROTOCOL_ERROR);
			ast_log(LOG_ERROR, "Invalid ICE-UDP candidate information received on session '%s'\n", session->sid);
			return -1;
		}

		local_candidate.foundation = foundation;
		local_candidate.transport = protocol;

		ast_sockaddr_parse(&local_candidate.address, ip, PARSE_PORT_FORBID);

		/* We only support IPv4 right now */
		if (!ast_sockaddr_is_ipv4(&local_candidate.address)) {
			continue;
		}

		ast_sockaddr_set_port(&local_candidate.address, real_port);

		if (!strcasecmp(type, "host")) {
			local_candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_HOST;
		} else if (!strcasecmp(type, "srflx")) {
			local_candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_SRFLX;
		} else if (!strcasecmp(type, "relay")) {
			local_candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_RELAYED;
		} else {
			continue;
		}

		/* Worst case use the first viable address */
		ast_rtp_instance_get_remote_address(rtp, &remote_address);

		if (ast_sockaddr_is_ipv4(&local_candidate.address) && ast_sockaddr_isnull(&remote_address)) {
			ast_rtp_instance_set_remote_address(rtp, &local_candidate.address);
		}

		ice->add_remote_candidate(rtp, &local_candidate);
	}

	ice->start(rtp);

	return 0;
}

/*! \brief Helper function which handles Google transport information */
static int jingle_interpret_google_transport(struct jingle_session *session, iks *transport, struct ast_rtp_instance *rtp)
{
	struct ast_rtp_engine_ice *ice = ast_rtp_instance_get_ice(rtp);
	iks *candidate;

	if (!ice) {
		jingle_queue_hangup_with_cause(session, AST_CAUSE_SWITCH_CONGESTION);
		ast_log(LOG_ERROR, "Received Google transport information on session '%s' but ICE support not available\n", session->sid);
		return -1;
	}

	/* If this session has not transitioned to the Google transport do so now */
	if ((session->transport != JINGLE_TRANSPORT_GOOGLE_V2) &&
	    (session->transport != JINGLE_TRANSPORT_GOOGLE_V1)) {
		/* Stop built-in ICE support... we need to fall back to the old old old STUN */
		ice->stop(rtp);

		session->transport = JINGLE_TRANSPORT_GOOGLE_V2;
	}

	for (candidate = iks_child(transport); candidate; candidate = iks_next(candidate)) {
		char *address = iks_find_attrib(candidate, "address"), *port = iks_find_attrib(candidate, "port");
		char *username = iks_find_attrib(candidate, "username"), *name = iks_find_attrib(candidate, "name");
		char *protocol = iks_find_attrib(candidate, "protocol");
		int real_port;
		struct ast_sockaddr target = { { 0, } };
		/* In Google land the combined value is 32 bytes */
		char combined[33] = "";

		/* If this is NOT actually a candidate just skip it */
		if (strcasecmp(iks_name(candidate), "candidate") &&
		    strcasecmp(iks_name(candidate), "p:candidate") &&
		    strcasecmp(iks_name(candidate), "ses:candidate")) {
			continue;
		}

		/* If this candidate is incomplete skip it */
		if (ast_strlen_zero(address) || ast_strlen_zero(port) || ast_strlen_zero(username) ||
		    ast_strlen_zero(name)) {
			jingle_queue_hangup_with_cause(session, AST_CAUSE_PROTOCOL_ERROR);
			ast_log(LOG_ERROR, "Incomplete Google candidate received on session '%s'\n", session->sid);
			return -1;
		}

		/* We only support UDP so skip any other protocols */
		if (!ast_strlen_zero(protocol) && strcasecmp(protocol, "udp")) {
			continue;
		}

		/* We only permit audio and video, not RTCP */
		if (strcasecmp(name, "rtp") && strcasecmp(name, "video_rtp")) {
			continue;
		}

		/* Parse the target information so we can send a STUN request to the candidate */
		if (sscanf(port, "%30d", &real_port) != 1) {
			jingle_queue_hangup_with_cause(session, AST_CAUSE_PROTOCOL_ERROR);
			ast_log(LOG_ERROR, "Invalid Google candidate port '%s' received on session '%s'\n", port, session->sid);
			return -1;
		}
		ast_sockaddr_parse(&target, address, PARSE_PORT_FORBID);
		ast_sockaddr_set_port(&target, real_port);

		/* Per the STUN support Google talk uses combine the two usernames */
		snprintf(combined, sizeof(combined), "%s%s", username, ice->get_ufrag(rtp));

		/* This should appease the masses... we will actually change the remote address when we get their STUN packet */
		ast_rtp_instance_stun_request(rtp, &target, combined);
	}

	return 0;
}

/*!
 * \brief Helper function which locates content stanzas and interprets them
 *
 * \note The session *must not* be locked before calling this
 */
static int jingle_interpret_content(struct jingle_session *session, ikspak *pak)
{
	iks *content;
	unsigned int changed = 0;
	struct ast_channel *chan;

	/* Look at the content in the session initiation */
	for (content = iks_child(iks_child(pak->x)); content; content = iks_next(content)) {
		char *name;
		struct ast_rtp_instance *rtp = NULL;
		iks *description, *transport;

		/* Ignore specific parts if they are known not to be useful */
		if (!strcmp(iks_name(content), "conference-info")) {
			continue;
		}

		name = iks_find_attrib(content, "name");

		if (session->transport != JINGLE_TRANSPORT_GOOGLE_V1) {
			/* If this content stanza has no name consider it invalid and move on */
			if (ast_strlen_zero(name) && !(name = iks_find_attrib(content, "jin:name"))) {
				jingle_queue_hangup_with_cause(session, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
				ast_log(LOG_ERROR, "Received content without a name on session '%s'\n", session->sid);
				return -1;
			}

			/* Try to pre-populate which RTP instance this content is relevant to */
			if (!strcmp(session->audio_name, name)) {
				rtp = session->rtp;
			} else if (!strcmp(session->video_name, name)) {
				rtp = session->vrtp;
			}
		} else {
			/* Google-V1 has no concept of assocating things like the above does, so since we only support audio over it assume they want audio */
			rtp = session->rtp;
		}

		/* If description information is available use it */
		if ((description = iks_find_with_attrib(content, "description", "xmlns", JINGLE_RTP_NS)) ||
		    (description = iks_find_with_attrib(content, "rtp:description", "xmlns:rtp", JINGLE_RTP_NS)) ||
		    (description = iks_find_with_attrib(content, "pho:description", "xmlns:pho", GOOGLE_PHONE_NS)) ||
		    (description = iks_find_with_attrib(pak->query, "description", "xmlns", GOOGLE_PHONE_NS)) ||
		    (description = iks_find_with_attrib(pak->query, "pho:description", "xmlns:pho", GOOGLE_PHONE_NS)) ||
		    (description = iks_find_with_attrib(pak->query, "vid:description", "xmlns", GOOGLE_VIDEO_NS))) {
			/* If we failed to do something with the content description abort immediately */
			if (jingle_interpret_description(session, description, name, &rtp)) {
				return -1;
			}

			/* If we successfully interpret the description then the codecs need updating */
			changed = 1;
		}

		/* If we get past the description handling and we still don't know what RTP instance this is for... it is unknown content */
		if (!rtp) {
			ast_log(LOG_ERROR, "Received a content stanza but have no RTP instance for it on session '%s'\n", session->sid);
			jingle_queue_hangup_with_cause(session, AST_CAUSE_SWITCH_CONGESTION);
			return -1;
		}

		/* If ICE UDP transport information is available use it */
		if ((transport = iks_find_with_attrib(content, "transport", "xmlns", JINGLE_ICE_UDP_NS))) {
			if (jingle_interpret_ice_udp_transport(session, transport, rtp)) {
				return -1;
			}
		} else if ((transport = iks_find_with_attrib(content, "transport", "xmlns", GOOGLE_TRANSPORT_NS)) ||
			   (transport = iks_find_with_attrib(content, "p:transport", "xmlns:p", GOOGLE_TRANSPORT_NS)) ||
			   (transport = iks_find_with_attrib(pak->x, "session", "xmlns", GOOGLE_SESSION_NS)) ||
			   (transport = iks_find_with_attrib(pak->x, "ses:session", "xmlns:ses", GOOGLE_SESSION_NS))) {
			/* If Google transport support is available use it */
			if (jingle_interpret_google_transport(session, transport, rtp)) {
				return -1;
			}
		} else if (iks_find(content, "transport")) {
			/* If this is a transport we do not support terminate the session as it probably won't work out in the end */
			jingle_queue_hangup_with_cause(session, AST_CAUSE_FACILITY_NOT_IMPLEMENTED);
			ast_log(LOG_ERROR, "Unsupported transport type received on session '%s'\n", session->sid);
			return -1;
		}
	}

	if (!changed) {
		return 0;
	}

	if ((chan = jingle_session_lock_full(session))) {
		struct ast_format fmt;

		ast_format_cap_copy(ast_channel_nativeformats(chan), session->jointcap);
		ast_codec_choose(&session->prefs, session->jointcap, 1, &fmt);
		ast_set_read_format(chan, &fmt);
		ast_set_write_format(chan, &fmt);

		ast_channel_unlock(chan);
		ast_channel_unref(chan);
	}
	ao2_unlock(session);

	return 0;
}

/*! \brief Handler function for the 'session-initiate' action */
static void jingle_action_session_initiate(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak)
{
	char *sid;
	enum jingle_transport transport = JINGLE_TRANSPORT_NONE;
	struct ast_channel *chan;
	int res;

	if (session) {
		/* This is a duplicate session setup, so respond accordingly */
		jingle_send_error_response(endpoint->connection, pak, "result", "out-of-order", NULL);
		return;
	}

	/* Retrieve the session identifier from the message, note that this may alter the transport */
	if ((sid = iks_find_attrib(pak->query, "id"))) {
		/* The presence of the session identifier in the 'id' attribute tells us that this is Google-V1 as everything else uses 'sid' */
		transport = JINGLE_TRANSPORT_GOOGLE_V1;
	} else if (!(sid = iks_find_attrib(pak->query, "sid"))) {
		jingle_send_error_response(endpoint->connection, pak, "bad-request", NULL, NULL);
		return;
	}

	/* Create a new local session */
	if (!(session = jingle_alloc(endpoint, pak->from->full, sid))) {
		jingle_send_error_response(endpoint->connection, pak, "cancel", "service-unavailable xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'", NULL);
		return;
	}

	/* If we determined that the transport should change as a result of how we got the SID change it */
	if (transport != JINGLE_TRANSPORT_NONE) {
		session->transport = transport;
	}

	/* Create a new Asterisk channel using the above local session */
	if (!(chan = jingle_new(endpoint, session, AST_STATE_DOWN, pak->from->user, NULL, NULL, pak->from->full))) {
		ao2_ref(session, -1);
		jingle_send_error_response(endpoint->connection, pak, "cancel", "service-unavailable xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'", NULL);
		return;
	}

	ao2_link(endpoint->state->sessions, session);

	ast_channel_lock(chan);
	ast_setstate(chan, AST_STATE_RING);
	ast_channel_unlock(chan);
	res = ast_pbx_start(chan);

	switch (res) {
	case AST_PBX_FAILED:
		ast_log(LOG_WARNING, "Failed to start PBX :(\n");
		jingle_send_error_response(endpoint->connection, pak, "cancel", "service-unavailable xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'", NULL);
		session->gone = 1;
		ast_hangup(chan);
		break;
	case AST_PBX_CALL_LIMIT:
		ast_log(LOG_WARNING, "Failed to start PBX (call limit reached) \n");
		jingle_send_error_response(endpoint->connection, pak, "wait", "resource-constraint xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'", NULL);
		ast_hangup(chan);
		break;
	case AST_PBX_SUCCESS:
		jingle_send_response(endpoint->connection, pak);

		/* Only send a transport-info message if we successfully interpreted the available content */
		if (!jingle_interpret_content(session, pak)) {
			jingle_send_transport_info(session, iks_find_attrib(pak->x, "from"));
		}
		break;
	}
}

/*! \brief Handler function for the 'transport-info' action */
static void jingle_action_transport_info(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak)
{
	if (!session) {
		jingle_send_error_response(endpoint->connection, pak, "cancel", "item-not-found xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'",
					   "unknown-session xmlns='urn:xmpp:jingle:errors:1'");
		return;
	}

	jingle_interpret_content(session, pak);
	jingle_send_response(endpoint->connection, pak);
}

/*! \brief Handler function for the 'session-accept' action */
static void jingle_action_session_accept(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak)
{
	struct ast_channel *chan;

	if (!session) {
		jingle_send_error_response(endpoint->connection, pak, "cancel", "item-not-found xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'",
					   "unknown-session xmlns='urn:xmpp:jingle:errors:1'");
		return;
	}


	jingle_interpret_content(session, pak);

	if ((chan = jingle_session_lock_full(session))) {
		ast_queue_control(chan, AST_CONTROL_ANSWER);
		ast_channel_unlock(chan);
		ast_channel_unref(chan);
	}
	ao2_unlock(session);

	jingle_send_response(endpoint->connection, pak);
}

/*! \brief Handler function for the 'session-info' action */
static void jingle_action_session_info(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak)
{
	struct ast_channel *chan;

	if (!session) {
		jingle_send_error_response(endpoint->connection, pak, "cancel", "item-not-found xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'",
					   "unknown-session xmlns='urn:xmpp:jingle:errors:1'");
		return;
	}

	if (!(chan = jingle_session_lock_full(session))) {
		ao2_unlock(session);
		jingle_send_response(endpoint->connection, pak);
		return;
	}

	if (iks_find_with_attrib(pak->query, "ringing", "xmlns", JINGLE_RTP_INFO_NS)) {
		ast_queue_control(chan, AST_CONTROL_RINGING);
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_setstate(chan, AST_STATE_RINGING);
		}
	} else if (iks_find_with_attrib(pak->query, "hold", "xmlns", JINGLE_RTP_INFO_NS)) {
		ast_queue_hold(chan, NULL);
	} else if (iks_find_with_attrib(pak->query, "unhold", "xmlns", JINGLE_RTP_INFO_NS)) {
		ast_queue_unhold(chan);
	}

	ast_channel_unlock(chan);
	ast_channel_unref(chan);
	ao2_unlock(session);

	jingle_send_response(endpoint->connection, pak);
}

/*! \brief Handler function for the 'session-terminate' action */
static void jingle_action_session_terminate(struct jingle_endpoint *endpoint, struct jingle_session *session, ikspak *pak)
{
	struct ast_channel *chan;
	iks *reason, *text;
	int cause = AST_CAUSE_NORMAL;
	struct ast_control_pvt_cause_code *cause_code;
	int data_size = sizeof(*cause_code);

	if (!session) {
		jingle_send_error_response(endpoint->connection, pak, "cancel", "item-not-found xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'",
					   "unknown-session xmlns='urn:xmpp:jingle:errors:1'");
		return;
	}

	if (!(chan = jingle_session_lock_full(session))) {
		ao2_unlock(session);
		jingle_send_response(endpoint->connection, pak);
		return;
	}

	/* Pull the reason text from the session-terminate message and translate it into a cause code */
	if ((reason = iks_find(pak->query, "reason")) && (text = iks_child(reason))) {
		int i;

		/* Size of the string making up the cause code is "Motif " + text */
		data_size += 6 + strlen(iks_name(text));
		cause_code = ast_alloca(data_size);
		memset(cause_code, 0, data_size);

		/* Get the appropriate cause code mapping for this reason */
		for (i = 0; i < ARRAY_LEN(jingle_reason_mappings); i++) {
			if (!strcasecmp(jingle_reason_mappings[i].reason, iks_name(text))) {
				cause = jingle_reason_mappings[i].cause;
				break;
			}
		}

		/* Store the technology specific information */
		snprintf(cause_code->code, data_size - sizeof(*cause_code) + 1, "Motif %s", iks_name(text));
	} else {
		/* No technology specific information is available */
		cause_code = ast_alloca(data_size);
		memset(cause_code, 0, data_size);
	}

	ast_copy_string(cause_code->chan_name, ast_channel_name(chan), AST_CHANNEL_NAME);
	cause_code->ast_cause = cause;
	ast_queue_control_data(chan, AST_CONTROL_PVT_CAUSE_CODE, cause_code, data_size);
	ast_channel_hangupcause_hash_set(chan, cause_code, data_size);

	ast_debug(3, "Hanging up channel '%s' due to session terminate message with cause '%d'\n", ast_channel_name(chan), cause);
	ast_queue_hangup_with_cause(chan, cause);
	session->gone = 1;

	ast_channel_unlock(chan);
	ast_channel_unref(chan);
	ao2_unlock(session);

	jingle_send_response(endpoint->connection, pak);
}

/*! \brief Callback for when a Jingle action is received from an endpoint */
static int jingle_action_hook(void *data, ikspak *pak)
{
	char *action;
	const char *sid = NULL;
	struct jingle_session *session = NULL;
	struct jingle_endpoint *endpoint = data;
	int i, handled = 0;

	/* We accept both Jingle and Google-V1 */
	if (!(action = iks_find_attrib(pak->query, "action")) &&
	    !(action = iks_find_attrib(pak->query, "type"))) {
		/* This occurs if either receive a packet masquerading as Jingle or Google-V1 that is actually not OR we receive a response
		 * to a message that has no response hook. */
		return IKS_FILTER_EAT;
	}

	/* Bump the endpoint reference count up in case a reload occurs. Unfortunately the available synchronization between iksemel and us
	 * does not permit us to make this completely safe. */
	ao2_ref(endpoint, +1);

	/* If a Jingle session identifier is present use it */
	if (!(sid = iks_find_attrib(pak->query, "sid"))) {
		/* If a Google-V1 session identifier is present use it */
		sid = iks_find_attrib(pak->query, "id");
	}

	/* If a session identifier was present in the message attempt to find the session, it is up to the action handler whether
	 * this is required or not */
	if (!ast_strlen_zero(sid)) {
		session = ao2_find(endpoint->state->sessions, sid, OBJ_KEY);
	}

	/* If a session is present associate the callid with this thread */
	if (session) {
		ast_callid_threadassoc_add(session->callid);
	}

	/* Iterate through supported action handlers looking for one that is able to handle this */
	for (i = 0; i < ARRAY_LEN(jingle_action_handlers); i++) {
		if (!strcasecmp(jingle_action_handlers[i].action, action)) {
			jingle_action_handlers[i].handler(endpoint, session, pak);
			handled = 1;
			break;
		}
	}

	/* If no action handler is present for the action they sent us make it evident */
	if (!handled) {
		ast_log(LOG_NOTICE, "Received action '%s' for session '%s' that has no handler\n", action, sid);
	}

	/* If a session was successfully found for this message deref it now since the handler is done */
	if (session) {
		ast_callid_threadassoc_remove();
		ao2_ref(session, -1);
	}

	ao2_ref(endpoint, -1);

	return IKS_FILTER_EAT;
}

/*! \brief Custom handler for groups */
static int custom_group_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct jingle_endpoint *endpoint = obj;

	if (!strcasecmp(var->name, "callgroup")) {
		endpoint->callgroup = ast_get_group(var->value);
	} else if (!strcasecmp(var->name, "pickupgroup")) {
		endpoint->pickupgroup = ast_get_group(var->value);
	} else {
		return -1;
	}

	return 0;
}

/*! \brief Custom handler for connection */
static int custom_connection_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct jingle_endpoint *endpoint = obj;

	/* You might think... but Josh, shouldn't you do this in a prelink callback? Well I *could* but until the original is destroyed
	 * this will not actually get called, so even if the config turns out to be bogus this is harmless.
	 */
	if (!(endpoint->connection = ast_xmpp_client_find(var->value))) {
		ast_log(LOG_ERROR, "Connection '%s' configured on endpoint '%s' could not be found\n", var->value, endpoint->name);
		return -1;
	}

	if (!(endpoint->rule = iks_filter_add_rule(endpoint->connection->filter, jingle_action_hook, endpoint,
						   IKS_RULE_TYPE, IKS_PAK_IQ,
						   IKS_RULE_NS, JINGLE_NS,
						   IKS_RULE_NS, GOOGLE_SESSION_NS,
						   IKS_RULE_DONE))) {
		ast_log(LOG_ERROR, "Action hook could not be added to connection '%s' on endpoint '%s'\n", var->value, endpoint->name);
		return -1;
	}

	return 0;
}

/*! \brief Custom handler for transport */
static int custom_transport_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct jingle_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "ice-udp")) {
		endpoint->transport = JINGLE_TRANSPORT_ICE_UDP;
	} else if (!strcasecmp(var->value, "google")) {
		endpoint->transport = JINGLE_TRANSPORT_GOOGLE_V2;
	} else if (!strcasecmp(var->value, "google-v1")) {
		endpoint->transport = JINGLE_TRANSPORT_GOOGLE_V1;
	} else {
		ast_log(LOG_WARNING, "Unknown transport type '%s' on endpoint '%s', defaulting to 'ice-udp'\n", var->value, endpoint->name);
		endpoint->transport = JINGLE_TRANSPORT_ICE_UDP;
	}

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
	if (!(jingle_tech.capabilities = ast_format_cap_alloc(0))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (aco_info_init(&cfg_info)) {
		ast_log(LOG_ERROR, "Unable to intialize configuration for chan_motif.\n");
		goto end;
	}

	aco_option_register(&cfg_info, "context", ACO_EXACT, endpoint_options, "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct jingle_endpoint, context));
	aco_option_register_custom(&cfg_info, "callgroup", ACO_EXACT, endpoint_options, NULL, custom_group_handler, 0);
	aco_option_register_custom(&cfg_info, "pickupgroup", ACO_EXACT, endpoint_options, NULL, custom_group_handler, 0);
	aco_option_register(&cfg_info, "language", ACO_EXACT, endpoint_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct jingle_endpoint, language));
	aco_option_register(&cfg_info, "musicclass", ACO_EXACT, endpoint_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct jingle_endpoint, musicclass));
	aco_option_register(&cfg_info, "parkinglot", ACO_EXACT, endpoint_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct jingle_endpoint, parkinglot));
	aco_option_register(&cfg_info, "accountcode", ACO_EXACT, endpoint_options, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct jingle_endpoint, accountcode));
	aco_option_register(&cfg_info, "allow", ACO_EXACT, endpoint_options, "ulaw,alaw", OPT_CODEC_T, 1, FLDSET(struct jingle_endpoint, prefs, cap));
	aco_option_register(&cfg_info, "disallow", ACO_EXACT, endpoint_options, "all", OPT_CODEC_T, 0, FLDSET(struct jingle_endpoint, prefs, cap));
	aco_option_register_custom(&cfg_info, "connection", ACO_EXACT, endpoint_options, NULL, custom_connection_handler, 0);
	aco_option_register_custom(&cfg_info, "transport", ACO_EXACT, endpoint_options, NULL, custom_transport_handler, 0);
	aco_option_register(&cfg_info, "maxicecandidates", ACO_EXACT, endpoint_options, DEFAULT_MAX_ICE_CANDIDATES, OPT_UINT_T, PARSE_DEFAULT,
			    FLDSET(struct jingle_endpoint, maxicecandidates), DEFAULT_MAX_ICE_CANDIDATES);
	aco_option_register(&cfg_info, "maxpayloads", ACO_EXACT, endpoint_options, DEFAULT_MAX_PAYLOADS, OPT_UINT_T, PARSE_DEFAULT,
			    FLDSET(struct jingle_endpoint, maxpayloads), DEFAULT_MAX_PAYLOADS);

	ast_format_cap_add_all_by_type(jingle_tech.capabilities, AST_FORMAT_TYPE_AUDIO);

	if (aco_process_config(&cfg_info, 0)) {
		ast_log(LOG_ERROR, "Unable to read config file motif.conf. Module loaded but not running.\n");
		aco_info_destroy(&cfg_info);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Unable to create scheduler context.\n");
		goto end;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Unable to create scheduler context thread.\n");
		goto end;
	}

	ast_rtp_glue_register(&jingle_rtp_glue);

	if (ast_channel_register(&jingle_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", channel_type);
		goto end;
	}

	return 0;

end:
	ast_rtp_glue_unregister(&jingle_rtp_glue);

	if (sched) {
		ast_sched_context_destroy(sched);
	}

	aco_info_destroy(&cfg_info);

	return AST_MODULE_LOAD_FAILURE;
}

/*! \brief Reload module */
static int reload(void)
{
	return aco_process_config(&cfg_info, 1);
}

/*! \brief Unload the jingle channel from Asterisk */
static int unload_module(void)
{
	ast_channel_unregister(&jingle_tech);
	ast_format_cap_destroy(jingle_tech.capabilities);
	jingle_tech.capabilities = NULL;
	ast_rtp_glue_unregister(&jingle_rtp_glue);
	ast_sched_context_destroy(sched);
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Motif Jingle Channel Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	       );
