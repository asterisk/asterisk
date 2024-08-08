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

#ifndef _RES_PJSIP_H
#define _RES_PJSIP_H

#include <pjsip.h>
/* Needed for SUBSCRIBE, NOTIFY, and PUBLISH method definitions */
#include <pjsip_simple.h>
#include <pjsip/sip_transaction.h>
#include <pj/timer.h>
/* Needed for pj_sockaddr */
#include <pjlib.h>

#include "asterisk/stringfields.h"
/* Needed for struct ast_sockaddr */
#include "asterisk/netsock2.h"
/* Needed for linked list macros */
#include "asterisk/linkedlists.h"
/* Needed for ast_party_id */
#include "asterisk/channel.h"
/* Needed for ast_sorcery */
#include "asterisk/sorcery.h"
/* Needed for ast_dnsmgr */
#include "asterisk/dnsmgr.h"
/* Needed for ast_endpoint */
#include "asterisk/endpoints.h"
/* Needed for ast_t38_ec_modes */
#include "asterisk/udptl.h"
/* Needed for ast_rtp_dtls_cfg struct */
#include "asterisk/rtp_engine.h"
/* Needed for AST_VECTOR macro */
#include "asterisk/vector.h"
/* Needed for ast_sip_for_each_channel_snapshot struct */
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stream.h"

#ifdef HAVE_PJSIP_TLS_TRANSPORT_RESTART
/* Needed for knowing if the cert or priv key files changed */
#include <sys/stat.h>
#endif

#define PJSIP_MINVERSION(m,n,p) (((m << 24) | (n << 16) | (p << 8)) >= PJ_VERSION_NUM)

#ifndef PJSIP_EXPIRES_NOT_SPECIFIED
/*
 * Added in pjproject 2.10.0. However define here if someone compiles against a
 * version of pjproject < 2.10.0.
 *
 * Usually defined in pjsip/include/pjsip/sip_msg.h (included as part of <pjsip.h>)
 */
#define PJSIP_EXPIRES_NOT_SPECIFIED	((pj_uint32_t)-1)
#endif

#define PJSTR_PRINTF_SPEC "%.*s"
#define PJSTR_PRINTF_VAR(_v) ((int)(_v).slen), ((_v).ptr)

#define AST_SIP_AUTH_MAX_REALM_LENGTH 255	/* From the auth/realm realtime column size */

/* ":12345" */
#define COLON_PORT_STRLEN 6
/*
 * "<ipaddr>:<port>"
 * PJ_INET6_ADDRSTRLEN includes the NULL terminator
 */
#define IP6ADDR_COLON_PORT_BUFLEN (PJ_INET6_ADDRSTRLEN + COLON_PORT_STRLEN)

/*!
 * \brief Fill a buffer with a pjsip transport's remote ip address and port
 *
 * \param _transport The pjsip_transport to use
 * \param _dest The destination buffer of at least IP6ADDR_COLON_PORT_BUFLEN bytes
 */
#define AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR(_transport, _dest) \
	snprintf(_dest, IP6ADDR_COLON_PORT_BUFLEN, \
		PJSTR_PRINTF_SPEC ":%d", \
		PJSTR_PRINTF_VAR(_transport->remote_name.host), \
		_transport->remote_name.port);

/* Forward declarations of PJSIP stuff */
struct pjsip_rx_data;
struct pjsip_module;
struct pjsip_tx_data;
struct pjsip_dialog;
struct pjsip_transport;
struct pjsip_tpfactory;
struct pjsip_tls_setting;
struct pjsip_tpselector;

/*! \brief Maximum number of ciphers supported for a TLS transport */
#define SIP_TLS_MAX_CIPHERS 64

/*! Maximum number of challenges before assuming that we are in a loop */
#define MAX_RX_CHALLENGES	10

AST_VECTOR(ast_sip_service_route_vector, char *);

static const pj_str_t AST_PJ_STR_EMPTY = { "", 0 };

/*!
 * \brief Structure for SIP transport information
 */
struct ast_sip_transport_state {
	/*! \brief Transport itself */
	struct pjsip_transport *transport;
	/*! \brief Transport factory */
	struct pjsip_tpfactory *factory;
	/*!
	 * Transport id
	 * \since 13.8.0
	 */
	char *id;
	/*!
	 * Transport type
	 * \since 13.8.0
	 */
	enum ast_transport type;
	/*!
	 * Address and port to bind to
	 * \since 13.8.0
	 */
	pj_sockaddr host;
	/*!
	 * TLS settings
	 * \since 13.8.0
	 */
	pjsip_tls_setting tls;
	/*!
	 * Configured TLS ciphers
	 * \since 13.8.0
	 */
	pj_ssl_cipher ciphers[SIP_TLS_MAX_CIPHERS];
	/*!
	 * Optional local network information, used for NAT purposes.
	 * "deny" (set) means that it's in the local network. Use the
	 * ast_sip_transport_is_nonlocal and ast_sip_transport_is_local
	 * macro's.
	 * \since 13.8.0
	 */
	struct ast_ha *localnet;
	/*!
	 * DNS manager for refreshing the external signaling address
	 * \since 13.8.0
	 */
	struct ast_dnsmgr_entry *external_signaling_address_refresher;
	/*!
	 * Optional external signaling address information
	 * \since 13.8.0
	 */
	struct ast_sockaddr external_signaling_address;
	/*!
	 * DNS manager for refreshing the external media address
	 * \since 13.18.0
	 */
	struct ast_dnsmgr_entry *external_media_address_refresher;
	/*!
	 * Optional external signaling address information
	 * \since 13.18.0
	 */
	struct ast_sockaddr external_media_address;
	/*!
	 * Set when this transport is a flow of signaling to a target
	 * \since 17.0.0
	 */
	int flow;
	/*!
	 * The P-Preferred-Identity to use on traffic using this transport
	 * \since 17.0.0
	 */
	char *preferred_identity;
	/*!
	 * The Service Routes to use on traffic using this transport
	 * \since 17.0.0
	 */
	struct ast_sip_service_route_vector *service_routes;
	/*!
	 * Disregard RFC5922 7.2, and allow wildcard certs (TLS only)
	 */
	int allow_wildcard_certs;
	/*!
	 * If true, fail if server certificate cannot verify (TLS only)
	 */
	int verify_server;
#ifdef HAVE_PJSIP_TLS_TRANSPORT_RESTART
	/*!
	 * The stats information for the certificate file, if configured
	 */
	struct stat cert_file_stat;
	/*!
	 * The stats information for the private key file, if configured
	 */
	struct stat privkey_file_stat;
#endif
};

#define ast_sip_transport_is_nonlocal(transport_state, addr) \
	(!transport_state->localnet || ast_apply_ha(transport_state->localnet, addr) == AST_SENSE_ALLOW)

#define ast_sip_transport_is_local(transport_state, addr) \
	(transport_state->localnet && ast_apply_ha(transport_state->localnet, addr) != AST_SENSE_ALLOW)

/*!
 * \brief Transport to bind to
 */
struct ast_sip_transport {
	/*! Sorcery object details */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Certificate of authority list file */
		AST_STRING_FIELD(ca_list_file);
		/*! Certificate of authority list path */
		AST_STRING_FIELD(ca_list_path);
		/*! Public certificate file */
		AST_STRING_FIELD(cert_file);
		/*! Optional private key of the certificate file */
		AST_STRING_FIELD(privkey_file);
		/*! Password to open the private key */
		AST_STRING_FIELD(password);
		/*! External signaling address */
		AST_STRING_FIELD(external_signaling_address);
		/*! External media address */
		AST_STRING_FIELD(external_media_address);
		/*! Optional domain to use for messages if provided could not be found */
		AST_STRING_FIELD(domain);
		);
	/*! Type of transport */
	enum ast_transport type;
	/*!
	 * \deprecated Moved to ast_sip_transport_state
	 * \version 13.8.0 deprecated
	 * Address and port to bind to
	 */
	pj_sockaddr host;
	/*! Number of simultaneous asynchronous operations */
	unsigned int async_operations;
	/*! Optional external port for signaling */
	unsigned int external_signaling_port;
	/*!
	 * \deprecated Moved to ast_sip_transport_state
	 * \version 13.7.1 deprecated
	 * TLS settings
	 */
	pjsip_tls_setting tls;
	/*!
	 * \deprecated Moved to ast_sip_transport_state
	 * \version 13.7.1 deprecated
	 * Configured TLS ciphers
	 */
	pj_ssl_cipher ciphers[SIP_TLS_MAX_CIPHERS];
	/*!
	 * \deprecated Moved to ast_sip_transport_state
	 * \version 13.7.1 deprecated
	 * Optional local network information, used for NAT purposes
	 */
	struct ast_ha *localnet;
	/*!
	 * \deprecated Moved to ast_sip_transport_state
	 * \version 13.7.1 deprecated
	 * DNS manager for refreshing the external address
	 */
	struct ast_dnsmgr_entry *external_address_refresher;
	/*!
	 * \deprecated Moved to ast_sip_transport_state
	 * \version 13.7.1 deprecated
	 * Optional external address information
	 */
	struct ast_sockaddr external_address;
	/*!
	 * \deprecated
	 * \version 13.7.1 deprecated
	 * Transport state information
	 */
	struct ast_sip_transport_state *state;
	/*! QOS DSCP TOS bits */
	unsigned int tos;
	/*! QOS COS value */
	unsigned int cos;
	/*! Write timeout */
	int write_timeout;
	/*! Allow reload */
	int allow_reload;
	/*! Automatically send requests out the same transport requests have come in on */
	int symmetric_transport;
	/*! This is a flow to another target */
	int flow;
	/*! Enable TCP keepalive */
	int tcp_keepalive_enable;
	/*! Time in seconds the connection needs to remain idle before TCP starts sending keepalive probes */
	int tcp_keepalive_idle_time;
	/*! The time in seconds between individual keepalive probes */
	int tcp_keepalive_interval_time;
	/*! The maximum number of keepalive probes TCP should send before dropping the connection */
	int tcp_keepalive_probe_count;
};

#define SIP_SORCERY_DOMAIN_ALIAS_TYPE "domain_alias"

/*!
 * Details about a SIP domain alias
 */
struct ast_sip_domain_alias {
	/*! Sorcery object details */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Domain to be aliased to */
		AST_STRING_FIELD(domain);
	);
};

/*!
 * \brief Structure for SIP nat hook information
 */
struct ast_sip_nat_hook {
	/*! Sorcery object details */
	SORCERY_OBJECT(details);
	/*! Callback for when a message is going outside of our local network */
	void (*outgoing_external_message)(struct pjsip_tx_data *tdata, struct ast_sip_transport *transport);
};

/*! \brief Structure which contains information about a transport */
struct ast_sip_request_transport_details {
	/*! \brief Type of transport */
	enum ast_transport type;
	/*! \brief Potential pointer to the transport itself, if UDP */
	pjsip_transport *transport;
	/*! \brief Potential pointer to the transport factory itself, if TCP/TLS */
	pjsip_tpfactory *factory;
	/*! \brief Local address for transport */
	pj_str_t local_address;
	/*! \brief Local port for transport */
	int local_port;
};

/*!
 * \brief The kind of security negotiation
 */
enum ast_sip_security_negotiation {
	/*! No security mechanism negotiation */
	AST_SIP_SECURITY_NEG_NONE = 0,
	/*! Use mediasec security mechanism negotiation */
	AST_SIP_SECURITY_NEG_MEDIASEC,
	/* Add RFC 3329 (sec-agree) mechanism negotiation in the future */
};

/*!
 * \brief The security mechanism type
 */
enum ast_sip_security_mechanism_type {
	AST_SIP_SECURITY_MECH_NONE = 0,
	/* Use msrp-tls as security mechanism */
	AST_SIP_SECURITY_MECH_MSRP_TLS,
	/* Use sdes-srtp as security mechanism */
	AST_SIP_SECURITY_MECH_SDES_SRTP,
	/* Use dtls-srtp as security mechanism */
	AST_SIP_SECURITY_MECH_DTLS_SRTP,
	/* Add RFC 3329 (sec-agree) mechanisms like tle, digest, ipsec-ike in the future */
};

/*!
 * \brief Structure representing a security mechanism as defined in RFC 3329
 */
struct ast_sip_security_mechanism {
	/* Used to determine which security mechanism to use. */
	enum ast_sip_security_mechanism_type type;
	/* The preference of this security mechanism. The higher the value, the more preferred. */
	float qvalue;
	/* Optional mechanism parameters. */
	struct ast_vector_string mechanism_parameters;
};

AST_VECTOR(ast_sip_security_mechanism_vector, struct ast_sip_security_mechanism *);

/*!
 * \brief Contact associated with an address of record
 */
struct ast_sip_contact {
	/*! Sorcery object details, the id is the aor name plus a random string */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Full URI of the contact */
		AST_STRING_FIELD(uri);
		/*! Outbound proxy to use for qualify */
		AST_STRING_FIELD(outbound_proxy);
		/*! Path information to place in Route headers */
		AST_STRING_FIELD(path);
		/*! Content of the User-Agent header in REGISTER request */
		AST_STRING_FIELD(user_agent);
		/*! The name of the aor this contact belongs to */
		AST_STRING_FIELD(aor);
		/*! Asterisk Server name */
		AST_STRING_FIELD(reg_server);
		/*! IP-address of the Via header in REGISTER request */
		AST_STRING_FIELD(via_addr);
		/*! Content of the Call-ID header in REGISTER request */
		AST_STRING_FIELD(call_id);
		/*! The name of the endpoint that added the contact */
		AST_STRING_FIELD(endpoint_name);
	);
	/*! Absolute time that this contact is no longer valid after */
	struct timeval expiration_time;
	/*! Frequency to send OPTIONS requests to contact. 0 is disabled. */
	unsigned int qualify_frequency;
	/*! If true authenticate the qualify challenge response if needed */
	int authenticate_qualify;
	/*! Qualify timeout. 0 is diabled. */
	double qualify_timeout;
	/*! Endpoint that added the contact, only available in observers */
	struct ast_sip_endpoint *endpoint;
	/*! Port of the Via header in REGISTER request */
	int via_port;
	/*! If true delete the contact on Asterisk restart/boot */
	int prune_on_boot;
};

/*!
 * \brief Status type for a contact.
 */
enum ast_sip_contact_status_type {
	/*! Frequency > 0, but no response from remote uri */
	UNAVAILABLE,
	/*! Frequency > 0, and got response from remote uri */
	AVAILABLE,
	/*! Default last status, and when a contact status object is not found */
	UNKNOWN,
	/*! Frequency == 0, has a contact, but don't know status (non-qualified) */
	CREATED,
	REMOVED,
};

/*!
 * \brief A contact's status.
 *
 * Maintains a contact's current status and round trip time if available.
 */
struct ast_sip_contact_status {
	AST_DECLARE_STRING_FIELDS(
		/*! The original contact's URI */
		AST_STRING_FIELD(uri);
		/*! The name of the aor this contact_status belongs to */
		AST_STRING_FIELD(aor);
	);
	/*! The round trip time in microseconds */
	int64_t rtt;
	/*!
	 * The security mechanism list of the contact (RFC 3329).
	 * Stores the values of Security-Server headers in 401/421/494 responses to an
	 * in-dialog request or successful outbound registration which will be used to
	 * set the Security-Verify headers of all subsequent requests to the contact.
	 */
	struct ast_sip_security_mechanism_vector security_mechanisms;
	/*! Current status for a contact (default - unavailable) */
	enum ast_sip_contact_status_type status;
	/*! Last status for a contact (default - unavailable) */
	enum ast_sip_contact_status_type last_status;
	/*! Name of the contact */
	char name[0];
};

/*!
 * \brief A SIP address of record
 */
struct ast_sip_aor {
	/*! Sorcery object details, the id is the AOR name */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Voicemail boxes for this AOR */
		AST_STRING_FIELD(mailboxes);
		/*! Outbound proxy for OPTIONS requests */
		AST_STRING_FIELD(outbound_proxy);
	);
	/*! Minimum expiration time */
	unsigned int minimum_expiration;
	/*! Maximum expiration time */
	unsigned int maximum_expiration;
	/*! Default contact expiration if one is not provided in the contact */
	unsigned int default_expiration;
	/*! Frequency to send OPTIONS requests to AOR contacts. 0 is disabled. */
	unsigned int qualify_frequency;
	/*! If true authenticate the qualify challenge response if needed */
	int authenticate_qualify;
	/*! Maximum number of external contacts, 0 to disable */
	unsigned int max_contacts;
	/*! Whether to remove any existing contacts not related to an incoming REGISTER when it comes in */
	unsigned int remove_existing;
	/*! Any permanent configured contacts */
	struct ao2_container *permanent_contacts;
	/*! Determines whether SIP Path headers are supported */
	unsigned int support_path;
	/*! Qualify timeout. 0 is diabled. */
	double qualify_timeout;
	/*! Voicemail extension to set in Message-Account */
	char *voicemail_extension;
	/*! Whether to remove unavailable contacts over max_contacts at all or first if remove_existing is enabled */
	unsigned int remove_unavailable;
};

/*!
 * \brief A wrapper for contact that adds the aor_id and
 * a consistent contact id.  Used by ast_sip_for_each_contact.
 */
struct ast_sip_contact_wrapper {
	/*! The id of the parent aor. */
	char *aor_id;
	/*! The id of contact in form of aor_id/contact_uri. */
	char *contact_id;
	/*! Pointer to the actual contact. */
	struct ast_sip_contact *contact;
};

/*!
 * \brief 100rel modes for SIP endpoints
 */
enum ast_sip_100rel_mode {
	/*! Do not support 100rel. (no) */
	AST_SIP_100REL_UNSUPPORTED = 0,
	/*! As UAC, indicate 100rel support in Supported header. (yes) */
	AST_SIP_100REL_SUPPORTED,
	/*! As UAS, send 1xx responses reliably, if the UAC indicated its support. Otherwise same as AST_SIP_100REL_SUPPORTED. (peer_supported) */
	AST_SIP_100REL_PEER_SUPPORTED,
	/*! Require the use of 100rel. (required) */
	AST_SIP_100REL_REQUIRED,
};

/*!
 * \brief DTMF modes for SIP endpoints
 */
enum ast_sip_dtmf_mode {
	/*! No DTMF to be used */
	AST_SIP_DTMF_NONE,
	/* XXX Should this be 2833 instead? */
	/*! Use RFC 4733 events for DTMF */
	AST_SIP_DTMF_RFC_4733,
	/*! Use DTMF in the audio stream */
	AST_SIP_DTMF_INBAND,
	/*! Use SIP INFO DTMF (blech) */
	AST_SIP_DTMF_INFO,
	/*! Use SIP 4733 if supported by the other side or INBAND if not */
	AST_SIP_DTMF_AUTO,
	/*! Use SIP 4733 if supported by the other side or INFO DTMF (blech) if not */
	AST_SIP_DTMF_AUTO_INFO,
};

/*!
 * \brief Methods of storing SIP digest authentication credentials.
 *
 * Note that both methods result in MD5 digest authentication being
 * used. The two methods simply alter how Asterisk determines the
 * credentials for a SIP authentication
 */
enum ast_sip_auth_type {
	/*! Credentials stored as a username and password combination */
	AST_SIP_AUTH_TYPE_USER_PASS,
	/*! Credentials stored as an MD5 sum */
	AST_SIP_AUTH_TYPE_MD5,
	/*! Google Oauth */
	AST_SIP_AUTH_TYPE_GOOGLE_OAUTH,
	/*! Credentials not stored this is a fake auth */
	AST_SIP_AUTH_TYPE_ARTIFICIAL
};

#define SIP_SORCERY_AUTH_TYPE "auth"

struct ast_sip_auth {
	/*! Sorcery ID of the auth is its name */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Identification for these credentials */
		AST_STRING_FIELD(realm);
		/*! Authentication username */
		AST_STRING_FIELD(auth_user);
		/*! Authentication password */
		AST_STRING_FIELD(auth_pass);
		/*! Authentication credentials in MD5 format (hash of user:realm:pass) */
		AST_STRING_FIELD(md5_creds);
		/*! Refresh token to use for OAuth authentication */
		AST_STRING_FIELD(refresh_token);
		/*! Client ID to use for OAuth authentication */
		AST_STRING_FIELD(oauth_clientid);
		/*! Secret to use for OAuth authentication */
		AST_STRING_FIELD(oauth_secret);
	);
	/*! The time period (in seconds) that a nonce may be reused */
	unsigned int nonce_lifetime;
	/*! Used to determine what to use when authenticating */
	enum ast_sip_auth_type type;
};

AST_VECTOR(ast_sip_auth_vector, const char *);

/*!
 * \brief Different methods by which incoming requests can be matched to endpoints
 */
enum ast_sip_endpoint_identifier_type {
	/*! Identify based on user name in From header */
	AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME = (1 << 0),
	/*! Identify based on user name in Auth header first, then From header */
	AST_SIP_ENDPOINT_IDENTIFY_BY_AUTH_USERNAME = (1 << 1),
	/*! Identify based on source IP address */
	AST_SIP_ENDPOINT_IDENTIFY_BY_IP = (1 << 2),
	/*! Identify based on arbitrary headers */
	AST_SIP_ENDPOINT_IDENTIFY_BY_HEADER = (1 << 3),
	/*! Identify based on request uri */
	AST_SIP_ENDPOINT_IDENTIFY_BY_REQUEST_URI = (1 << 4),
};
AST_VECTOR(ast_sip_identify_by_vector, enum ast_sip_endpoint_identifier_type);

enum ast_sip_session_refresh_method {
	/*! Use reinvite to negotiate direct media */
	AST_SIP_SESSION_REFRESH_METHOD_INVITE,
	/*! Use UPDATE to negotiate direct media */
	AST_SIP_SESSION_REFRESH_METHOD_UPDATE,
};

enum ast_sip_direct_media_glare_mitigation {
	/*! Take no special action to mitigate reinvite glare */
	AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE,
	/*! Do not send an initial direct media session refresh on outgoing call legs
	 * Subsequent session refreshes will be sent no matter the session direction
	 */
	AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_OUTGOING,
	/*! Do not send an initial direct media session refresh on incoming call legs
	 * Subsequent session refreshes will be sent no matter the session direction
	 */
	AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_INCOMING,
};

enum ast_sip_session_media_encryption {
	/*! Invalid media encryption configuration */
	AST_SIP_MEDIA_TRANSPORT_INVALID = 0,
	/*! Do not allow any encryption of session media */
	AST_SIP_MEDIA_ENCRYPT_NONE,
	/*! Offer SDES-encrypted session media */
	AST_SIP_MEDIA_ENCRYPT_SDES,
	/*! Offer encrypted session media with datagram TLS key exchange */
	AST_SIP_MEDIA_ENCRYPT_DTLS,
};

enum ast_sip_session_redirect {
	/*! User portion of the target URI should be used as the target in the dialplan */
	AST_SIP_REDIRECT_USER = 0,
	/*! Target URI should be used as the target in the dialplan */
	AST_SIP_REDIRECT_URI_CORE,
	/*! Target URI should be used as the target within chan_pjsip itself */
	AST_SIP_REDIRECT_URI_PJSIP,
};

/*!
 * \brief Incoming/Outgoing call offer/answer joint codec preference.
 *
 * The default is INTERSECT ALL LOCAL.
 */
enum ast_sip_call_codec_pref {
	/*! Two bits for merge */
	/*! Intersection of local and remote */
	AST_SIP_CALL_CODEC_PREF_INTERSECT =	1 << 0,
	/*! Union of local and remote */
	AST_SIP_CALL_CODEC_PREF_UNION =		1 << 1,

	/*! Two bits for filter */
	/*! No filter */
	AST_SIP_CALL_CODEC_PREF_ALL =	 	1 << 2,
	/*! Only the first */
	AST_SIP_CALL_CODEC_PREF_FIRST = 	1 << 3,

	/*! Two bits for preference and sort   */
	/*! Prefer, and order by local values */
	AST_SIP_CALL_CODEC_PREF_LOCAL = 	1 << 4,
	/*! Prefer, and order by remote values */
	AST_SIP_CALL_CODEC_PREF_REMOTE = 	1 << 5,
};

/*!
 * \brief Returns true if the preference is set in the parameter
 * \since 18.0.0
 *
 * \param __param A ast_flags struct with one or more of enum ast_sip_call_codec_pref set
 * \param __codec_pref The last component of one of the enum values
 * \retval 1 if the enum value is set
 * \retval 0 if not
 */
#define ast_sip_call_codec_pref_test(__param, __codec_pref) (!!(ast_test_flag( &__param, AST_SIP_CALL_CODEC_PREF_ ## __codec_pref )))

/*!
 * \brief Session timers options
 */
struct ast_sip_timer_options {
	/*! Minimum session expiration period, in seconds */
	unsigned int min_se;
	/*! Session expiration period, in seconds */
	unsigned int sess_expires;
};

/*!
 * \brief Endpoint configuration for SIP extensions.
 *
 * SIP extensions, in this case refers to features
 * indicated in Supported or Required headers.
 */
struct ast_sip_endpoint_extensions {
	/*! Enabled SIP extensions */
	unsigned int flags;
	/*! Timer options */
	struct ast_sip_timer_options timer;
};

/*!
 * \brief Endpoint configuration for unsolicited MWI
 */
struct ast_sip_mwi_configuration {
	AST_DECLARE_STRING_FIELDS(
		/*! Configured voicemail boxes for this endpoint. Used for MWI */
		AST_STRING_FIELD(mailboxes);
		/*! Username to use when sending MWI NOTIFYs to this endpoint */
		AST_STRING_FIELD(fromuser);
	);
	/*! Should mailbox states be combined into a single notification? */
	unsigned int aggregate;
	/*! Should a subscribe replace unsolicited notifies? */
	unsigned int subscribe_replaces_unsolicited;
	/*! Voicemail extension to set in Message-Account */
	char *voicemail_extension;
};

/*!
 * \brief Endpoint subscription configuration
 */
struct ast_sip_endpoint_subscription_configuration {
	/*! Indicates if endpoint is allowed to initiate subscriptions */
	unsigned int allow;
	/*! The minimum allowed expiration for subscriptions from endpoint */
	unsigned int minexpiry;
	/*! Message waiting configuration */
	struct ast_sip_mwi_configuration mwi;
	/*! Context for SUBSCRIBE requests */
	char context[AST_MAX_CONTEXT];
};

/*!
 * \brief NAT configuration options for endpoints
 */
struct ast_sip_endpoint_nat_configuration {
	/*! Whether to force using the source IP address/port for sending responses */
	unsigned int force_rport;
	/*! Whether to rewrite the Contact header with the source IP address/port or not */
	unsigned int rewrite_contact;
};

/*!
 * \brief Party identification options for endpoints
 *
 * This includes caller ID, connected line, and redirecting-related options
 */
struct ast_sip_endpoint_id_configuration {
	struct ast_party_id self;
	/*! Do we accept identification information from this endpoint */
	unsigned int trust_inbound;
	/*! Do we send private identification information to this endpoint? */
	unsigned int trust_outbound;
	/*! Do we send P-Asserted-Identity headers to this endpoint? */
	unsigned int send_pai;
	/*! Do we send Remote-Party-ID headers to this endpoint? */
	unsigned int send_rpid;
	/*! Do we send messages for connected line updates for unanswered incoming calls immediately to this endpoint? */
	unsigned int rpid_immediate;
	/*! Do we add Diversion headers to applicable outgoing requests/responses? */
	unsigned int send_diversion;
	/*! Do we accept connected line updates from this endpoint? */
	unsigned int trust_connected_line;
	/*! Do we send connected line updates to this endpoint? */
	unsigned int send_connected_line;
	/*! When performing connected line update, which method should be used */
	enum ast_sip_session_refresh_method refresh_method;
	/*! Do we add History-Info headers to applicable outgoing requests/responses? */
	unsigned int send_history_info;
};

/*!
 * \brief Call pickup configuration options for endpoints
 */
struct ast_sip_endpoint_pickup_configuration {
	/*! Call group */
	ast_group_t callgroup;
	/*! Pickup group */
	ast_group_t pickupgroup;
	/*! Named call group */
	struct ast_namedgroups *named_callgroups;
	/*! Named pickup group */
	struct ast_namedgroups *named_pickupgroups;
};

/*!
 * \brief Configuration for one-touch INFO recording
 */
struct ast_sip_info_recording_configuration {
	AST_DECLARE_STRING_FIELDS(
		/*! Feature to enact when one-touch recording INFO with Record: On is received */
		AST_STRING_FIELD(onfeature);
		/*! Feature to enact when one-touch recording INFO with Record: Off is received */
		AST_STRING_FIELD(offfeature);
	);
	/*! Is one-touch recording permitted? */
	unsigned int enabled;
};

/*!
 * \brief Endpoint configuration options for INFO packages
 */
struct ast_sip_endpoint_info_configuration {
	/*! Configuration for one-touch recording */
	struct ast_sip_info_recording_configuration recording;
};

/*!
 * \brief RTP configuration for SIP endpoints
 */
struct ast_sip_media_rtp_configuration {
	AST_DECLARE_STRING_FIELDS(
		/*! Configured RTP engine for this endpoint. */
		AST_STRING_FIELD(engine);
	);
	/*! Whether IPv6 RTP is enabled or not */
	unsigned int ipv6;
	/*! Whether symmetric RTP is enabled or not */
	unsigned int symmetric;
	/*! Whether ICE support is enabled or not */
	unsigned int ice_support;
	/*! Whether to use the "ptime" attribute received from the endpoint or not */
	unsigned int use_ptime;
	/*! Do we use AVPF exclusively for this endpoint? */
	unsigned int use_avpf;
	/*! Do we force AVP, AVPF, SAVP, or SAVPF even for DTLS media streams? */
	unsigned int force_avp;
	/*! Do we use the received media transport in our answer SDP */
	unsigned int use_received_transport;
	/*! \brief DTLS-SRTP configuration information */
	struct ast_rtp_dtls_cfg dtls_cfg;
	/*! Should SRTP use a 32 byte tag instead of an 80 byte tag? */
	unsigned int srtp_tag_32;
	/*! Do we use media encryption? what type? */
	enum ast_sip_session_media_encryption encryption;
	/*! Do we want to optimistically support encryption if possible? */
	unsigned int encryption_optimistic;
	/*! Number of seconds between RTP keepalive packets */
	unsigned int keepalive;
	/*! Number of seconds before terminating channel due to lack of RTP (when not on hold) */
	unsigned int timeout;
	/*! Number of seconds before terminating channel due to lack of RTP (when on hold) */
	unsigned int timeout_hold;
	/*! Follow forked media with a different To tag */
	unsigned int follow_early_media_fork;
	/*! Accept updated SDPs on non-100rel 18X and 2XX responses with the same To tag */
	unsigned int accept_multiple_sdp_answers;
};

/*!
 * \brief Direct media options for SIP endpoints
 */
struct ast_sip_direct_media_configuration {
	/*! Boolean indicating if direct_media is permissible */
	unsigned int enabled;
	/*! When using direct media, which method should be used */
	enum ast_sip_session_refresh_method method;
	/*! Take steps to mitigate glare for direct media */
	enum ast_sip_direct_media_glare_mitigation glare_mitigation;
	/*! Do not attempt direct media session refreshes if a media NAT is detected */
	unsigned int disable_on_nat;
};

struct ast_sip_t38_configuration {
	/*! Whether T.38 UDPTL support is enabled or not */
	unsigned int enabled;
	/*! Error correction setting for T.38 UDPTL */
	enum ast_t38_ec_modes error_correction;
	/*! Explicit T.38 max datagram value, may be 0 to indicate the remote side can be trusted */
	unsigned int maxdatagram;
	/*! Whether NAT Support is enabled for T.38 UDPTL sessions or not */
	unsigned int nat;
	/*! Whether to use IPv6 for UDPTL or not */
	unsigned int ipv6;
	/*! Bind the UDPTL instance to the media_address */
	unsigned int bind_udptl_to_media_address;
};

/*!
 * \brief Media configuration for SIP endpoints
 */
struct ast_sip_endpoint_media_configuration {
	AST_DECLARE_STRING_FIELDS(
		/*! Optional media address to use in SDP */
		AST_STRING_FIELD(address);
		/*! SDP origin username */
		AST_STRING_FIELD(sdpowner);
		/*! SDP session name */
		AST_STRING_FIELD(sdpsession);
	);
	/*! RTP media configuration */
	struct ast_sip_media_rtp_configuration rtp;
	/*! Direct media options */
	struct ast_sip_direct_media_configuration direct_media;
	/*! T.38 (FoIP) options */
	struct ast_sip_t38_configuration t38;
	/*! Configured codecs */
	struct ast_format_cap *codecs;
	/*! Capabilities in topology form */
	struct ast_stream_topology *topology;
	/*! DSCP TOS bits for audio streams */
	unsigned int tos_audio;
	/*! Priority for audio streams */
	unsigned int cos_audio;
	/*! DSCP TOS bits for video streams */
	unsigned int tos_video;
	/*! Priority for video streams */
	unsigned int cos_video;
	/*! Is g.726 packed in a non standard way */
	unsigned int g726_non_standard;
	/*! Bind the RTP instance to the media_address */
	unsigned int bind_rtp_to_media_address;
	/*! Use RTCP-MUX */
	unsigned int rtcp_mux;
	/*! Maximum number of audio streams to offer/accept */
	unsigned int max_audio_streams;
	/*! Maximum number of video streams to offer/accept */
	unsigned int max_video_streams;
	/*! Use BUNDLE */
	unsigned int bundle;
	/*! Enable webrtc settings and defaults */
	unsigned int webrtc;
	/*! Codec preference for an incoming offer */
	struct ast_flags incoming_call_offer_pref;
	/*! Codec preference for an outgoing offer */
	struct ast_flags outgoing_call_offer_pref;
	/*! Codec negotiation prefs for incoming offers */
	struct ast_stream_codec_negotiation_prefs codec_prefs_incoming_offer;
	/*! Codec negotiation prefs for outgoing offers */
	struct ast_stream_codec_negotiation_prefs codec_prefs_outgoing_offer;
	/*! Codec negotiation prefs for incoming answers */
	struct ast_stream_codec_negotiation_prefs codec_prefs_incoming_answer;
	/*! Codec negotiation prefs for outgoing answers */
	struct ast_stream_codec_negotiation_prefs codec_prefs_outgoing_answer;
};

/*!
 * \brief An entity with which Asterisk communicates
 */
struct ast_sip_endpoint {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Context to send incoming calls to */
		AST_STRING_FIELD(context);
		/*! Name of an explicit transport to use */
		AST_STRING_FIELD(transport);
		/*! Outbound proxy to use */
		AST_STRING_FIELD(outbound_proxy);
		/*! Explicit AORs to dial if none are specified */
		AST_STRING_FIELD(aors);
		/*! Musiconhold class to suggest that the other side use when placing on hold */
		AST_STRING_FIELD(mohsuggest);
		/*! Configured tone zone for this endpoint. */
		AST_STRING_FIELD(zone);
		/*! Configured language for this endpoint. */
		AST_STRING_FIELD(language);
		/*! Default username to place in From header */
		AST_STRING_FIELD(fromuser);
		/*! Domain to place in From header */
		AST_STRING_FIELD(fromdomain);
		/*! Context to route incoming MESSAGE requests to */
		AST_STRING_FIELD(message_context);
		/*! Accountcode to auto-set on channels */
		AST_STRING_FIELD(accountcode);
		/*! If set, we'll push incoming MWI NOTIFYs to stasis using this mailbox */
		AST_STRING_FIELD(incoming_mwi_mailbox);
		/*! STIR/SHAKEN profile to use */
		AST_STRING_FIELD(stir_shaken_profile);
		/*! Tenant ID for the endpoint */
		AST_STRING_FIELD(tenantid);
	);
	/*! Configuration for extensions */
	struct ast_sip_endpoint_extensions extensions;
	/*! Configuration relating to media */
	struct ast_sip_endpoint_media_configuration media;
	/*! SUBSCRIBE/NOTIFY configuration options */
	struct ast_sip_endpoint_subscription_configuration subscription;
	/*! NAT configuration */
	struct ast_sip_endpoint_nat_configuration nat;
	/*! Party identification options */
	struct ast_sip_endpoint_id_configuration id;
	/*! Configuration options for INFO packages */
	struct ast_sip_endpoint_info_configuration info;
	/*! Call pickup configuration */
	struct ast_sip_endpoint_pickup_configuration pickup;
	/*! Inbound authentication credentials */
	struct ast_sip_auth_vector inbound_auths;
	/*! Outbound authentication credentials */
	struct ast_sip_auth_vector outbound_auths;
	/*! DTMF mode to use with this endpoint */
	enum ast_sip_dtmf_mode dtmf;
	/*! Method(s) by which the endpoint should be identified. */
	enum ast_sip_endpoint_identifier_type ident_method;
	/*! Order of the method(s) by which the endpoint should be identified. */
	struct ast_sip_identify_by_vector ident_method_order;
	/*! Boolean indicating if ringing should be sent as inband progress */
	unsigned int inband_progress;
	/*! Pointer to the persistent Asterisk endpoint */
	struct ast_endpoint *persistent;
	/*! The number of channels at which busy device state is returned */
	unsigned int devicestate_busy_at;
	/*! Whether fax detection is enabled or not (CNG tone detection) */
	unsigned int faxdetect;
	/*! Determines if transfers (using REFER) are allowed by this endpoint */
	unsigned int allowtransfer;
	/*! Method used when handling redirects */
	enum ast_sip_session_redirect redirect_method;
	/*! Variables set on channel creation */
	struct ast_variable *channel_vars;
	/*! Whether to place a 'user=phone' parameter into the request URI if user is a number */
	unsigned int usereqphone;
	/*! Whether to pass through hold and unhold using re-invites with recvonly and sendrecv */
	unsigned int moh_passthrough;
	/*! Access control list */
	struct ast_acl_list *acl;
	/*! Restrict what IPs are allowed in the Contact header (for registration) */
	struct ast_acl_list *contact_acl;
	/*! The number of seconds into call to disable fax detection.  (0 = disabled) */
	unsigned int faxdetect_timeout;
	/*! Override the user on the outgoing Contact header with this value. */
	char *contact_user;
	/*! Whether to response SDP offer with single most preferred codec. */
	unsigned int preferred_codec_only;
	/*! Do we allow an asymmetric RTP codec? */
	unsigned int asymmetric_rtp_codec;
	/*! Do we allow overlap dialling? */
	unsigned int allow_overlap;
	/*! Whether to notifies all the progress details on blind transfer */
	unsigned int refer_blind_progress;
	/*! Whether to notifies dialog-info 'early' on INUSE && RINGING state */
	unsigned int notify_early_inuse_ringing;
	/*! Suppress Q.850 Reason headers on this endpoint */
	unsigned int suppress_q850_reason_headers;
	/*! Ignore 183 if no SDP is present */
	unsigned int ignore_183_without_sdp;
	/*! Type of security negotiation to use (RFC 3329). */
	enum ast_sip_security_negotiation security_negotiation;
	/*! Client security mechanisms (RFC 3329). */
	struct ast_sip_security_mechanism_vector security_mechanisms;
	/*! Set which STIR/SHAKEN behaviors we want on this endpoint */
	unsigned int stir_shaken;
	/*! Should we authenticate OPTIONS requests per RFC 3261? */
	unsigned int allow_unauthenticated_options;
	/*! The name of the geoloc profile to apply when Asterisk receives a call from this endpoint */
	AST_STRING_FIELD_EXTENDED(geoloc_incoming_call_profile);
	/*! The name of the geoloc profile to apply when Asterisk sends a call to this endpoint */
	AST_STRING_FIELD_EXTENDED(geoloc_outgoing_call_profile);
	/*! The context to use for overlap dialing, if different from the endpoint's context */
	AST_STRING_FIELD_EXTENDED(overlap_context);
	/*! 100rel mode to use with this endpoint */
	enum ast_sip_100rel_mode rel100;
	/*! Send Advice-of-Charge messages */
	unsigned int send_aoc;
};

/*! URI parameter for symmetric transport */
#define AST_SIP_X_AST_TXP "x-ast-txp"
#define AST_SIP_X_AST_TXP_LEN 9

/*! Common media types used throughout res_pjsip and pjproject */
extern pjsip_media_type pjsip_media_type_application_json;
extern pjsip_media_type pjsip_media_type_application_media_control_xml;
extern pjsip_media_type pjsip_media_type_application_pidf_xml;
extern pjsip_media_type pjsip_media_type_application_xpidf_xml;
extern pjsip_media_type pjsip_media_type_application_cpim_xpidf_xml;
extern pjsip_media_type pjsip_media_type_application_rlmi_xml;
extern pjsip_media_type pjsip_media_type_application_simple_message_summary;
extern pjsip_media_type pjsip_media_type_application_sdp;
extern pjsip_media_type pjsip_media_type_multipart_alternative;
extern pjsip_media_type pjsip_media_type_multipart_mixed;
extern pjsip_media_type pjsip_media_type_multipart_related;
extern pjsip_media_type pjsip_media_type_text_plain;

/*!
 * \brief Compare pjsip media types
 *
 * \param a the first media type
 * \param b the second media type
 * \retval 1 Media types are equal
 * \retval 0 Media types are not equal
 */
int ast_sip_are_media_types_equal(pjsip_media_type *a, pjsip_media_type *b);

/*!
 * \brief Check if a media type is in a list of others
 *
 * \param a pjsip_media_type to search for
 * \param ... one or more pointers to pjsip_media_types the last of which must be "SENTINEL"
 * \retval 1 Media types are equal
 * \retval 0 Media types are not equal
 */
int ast_sip_is_media_type_in(pjsip_media_type *a, ...) attribute_sentinel;

/*!
 * \brief Add security headers to transmission data
 *
 * \param security_mechanisms Vector of security mechanisms.
 * \param header_name The header name under which to add the security mechanisms.
 * One of Security-Client, Security-Server, Security-Verify.
 * \param add_qval If zero, don't add the q-value to the header.
 * \param tdata The transmission data.
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_add_security_headers(struct ast_sip_security_mechanism_vector *security_mechanisms,
		const char *header_name, int add_qval, pjsip_tx_data *tdata);

/*!
 * \brief Append to security mechanism vector from SIP header
 *
 * \param hdr The header of the security mechanisms.
 * \param security_mechanisms Vector of security mechanisms to append to.
 * Header name must be one of Security-Client, Security-Server, Security-Verify.
 */
void ast_sip_header_to_security_mechanism(const pjsip_generic_string_hdr *hdr,
		struct ast_sip_security_mechanism_vector *security_mechanisms);

/*!
 * \brief Initialize security mechanism vector from string of security mechanisms.
 *
 * \param security_mechanism Pointer to vector of security mechanisms to initialize.
 * \param value String of security mechanisms as defined in RFC 3329.
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_security_mechanism_vector_init(struct ast_sip_security_mechanism_vector *security_mechanism, const char *value);

/*!
 * \brief Removes all headers of a specific name and value from a pjsip_msg.
 *
 * \param msg PJSIP message from which to remove headers.
 * \param hdr_name Name of the header to remove.
 * \param value Optional string value of the header to remove.
 * If NULL, remove all headers of given hdr_name.
 */
void ast_sip_remove_headers_by_name_and_value(pjsip_msg *msg, const pj_str_t *hdr_name, const char* value);

/*!
 * \brief Duplicate a security mechanism.
 *
 * \param dst Security mechanism to duplicate to.
 * \param src Security mechanism to duplicate.
 */
void ast_sip_security_mechanisms_vector_copy(struct ast_sip_security_mechanism_vector *dst,
	const struct ast_sip_security_mechanism_vector *src);

/*!
 * \brief Free contents of a security mechanism vector.
 *
 * \param security_mechanisms Vector whose contents are to be freed
 */
void ast_sip_security_mechanisms_vector_destroy(struct ast_sip_security_mechanism_vector *security_mechanisms);

/*!
 * \brief Allocate a security mechanism from a string.
 *
 * \param security_mechanism Pointer-pointer to the security mechanism to allocate.
 * \param value The security mechanism string as defined in RFC 3329 (section 2.2)
 *				in the form <mechanism_name>;q=<q_value>;<mechanism_parameters>
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_str_to_security_mechanism(struct ast_sip_security_mechanism **security_mechanism, const char *value);

/*!
 * \brief Writes the security mechanisms of an endpoint into a buffer as a string and returns the buffer.
 *
 * \note The buffer must be freed by the caller.
 *
 * \param endpoint Pointer to endpoint.
 * \param add_qvalue If non-zero, the q-value is printed as well
 * \param buf The buffer to write the string into
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_security_mechanisms_to_str(const struct ast_sip_security_mechanism_vector *security_mechanisms, int add_qvalue, char **buf);

/*!
 * \brief Set the security negotiation based on a given string.
 *
 * \param security_negotiation Security negotiation enum to set.
 * \param val String that represents a security_negotiation value.
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_set_security_negotiation(enum ast_sip_security_negotiation *security_negotiation, const char *val);

/*!
 * \brief Initialize an auth vector with the configured values.
 *
 * \param vector Vector to initialize
 * \param auth_names Comma-separated list of names to set in the array
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_auth_vector_init(struct ast_sip_auth_vector *vector, const char *auth_names);

/*!
 * \brief Free contents of an auth vector.
 *
 * \param vector Vector whose contents are to be freed
 */
void ast_sip_auth_vector_destroy(struct ast_sip_auth_vector *vector);

/*!
 * \brief Possible returns from ast_sip_check_authentication
 */
enum ast_sip_check_auth_result {
    /*! Authentication needs to be challenged */
    AST_SIP_AUTHENTICATION_CHALLENGE,
    /*! Authentication succeeded */
    AST_SIP_AUTHENTICATION_SUCCESS,
    /*! Authentication failed */
    AST_SIP_AUTHENTICATION_FAILED,
    /*! Authentication encountered some internal error */
    AST_SIP_AUTHENTICATION_ERROR,
};

/*!
 * \brief An interchangeable way of handling digest authentication for SIP.
 *
 * An authenticator is responsible for filling in the callbacks provided below. Each is called from a publicly available
 * function in res_sip. The authenticator can use configuration or other local policy to determine whether authentication
 * should take place and what credentials should be used when challenging and authenticating a request.
 */
struct ast_sip_authenticator {
    /*!
     * \brief Check if a request requires authentication
     * See ast_sip_requires_authentication for more details
     */
    int (*requires_authentication)(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata);
	/*!
	 * \brief Check that an incoming request passes authentication.
	 *
	 * The tdata parameter is useful for adding information such as digest challenges.
	 *
	 * \param endpoint The endpoint sending the incoming request
	 * \param rdata The incoming request
	 * \param tdata Tentative outgoing request.
	 */
	enum ast_sip_check_auth_result (*check_authentication)(struct ast_sip_endpoint *endpoint,
			pjsip_rx_data *rdata, pjsip_tx_data *tdata);
};

/*!
 * \brief an interchangeable way of responding to authentication challenges
 *
 * An outbound authenticator takes incoming challenges and formulates a new SIP request with
 * credentials.
 */
struct ast_sip_outbound_authenticator {
	/*!
	 * \brief Create a new request with authentication credentials
	 *
	 * \param auths A vector of IDs of auth sorcery objects
	 * \param challenge The SIP response with authentication challenge(s)
	 * \param old_request The request that received the auth challenge(s)
	 * \param new_request The new SIP request with challenge response(s)
	 * \retval 0 Successfully created new request
	 * \retval -1 Failed to create a new request
	 */
	int (*create_request_with_auth)(const struct ast_sip_auth_vector *auths, struct pjsip_rx_data *challenge,
			struct pjsip_tx_data *old_request, struct pjsip_tx_data **new_request);
};

/*!
 * \brief An entity responsible for identifying the source of a SIP message
 */
struct ast_sip_endpoint_identifier {
    /*!
     * \brief Callback used to identify the source of a message.
     * See ast_sip_identify_endpoint for more details
     */
    struct ast_sip_endpoint *(*identify_endpoint)(pjsip_rx_data *rdata);
};

/*!
 * \brief Contact retrieval filtering flags
 */
enum ast_sip_contact_filter {
	/*! \brief Default filter flags */
	AST_SIP_CONTACT_FILTER_DEFAULT = 0,

	/*! \brief Return only reachable or unknown contacts */
	AST_SIP_CONTACT_FILTER_REACHABLE = (1 << 0),
};

/*!
 * \brief Adds a Date header to the tdata, formatted like:
 * Date: Wed, 01 Jan 2021 14:53:01 GMT
 * \since 16.19.0
 *
 * \note There is no checking done to see if the header already exists
 * before adding it. It's up to the caller of this function to determine
 * if that needs to be done or not.
 */
void ast_sip_add_date_header(pjsip_tx_data *tdata);

/*!
 * \brief Register a SIP service in Asterisk.
 *
 * This is more-or-less a wrapper around pjsip_endpt_register_module().
 * Registering a service makes it so that PJSIP will call into the
 * service at appropriate times. For more information about PJSIP module
 * callbacks, see the PJSIP documentation. Asterisk modules that call
 * this function will likely do so at module load time.
 *
 * \param module The module that is to be registered with PJSIP
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_register_service(pjsip_module *module);

/*!
 * This is the opposite of ast_sip_register_service().  Unregistering a
 * service means that PJSIP will no longer call into the module any more.
 * This will likely occur when an Asterisk module is unloaded.
 *
 * \param module The PJSIP module to unregister
 */
void ast_sip_unregister_service(pjsip_module *module);

/*!
 * \brief Register a SIP authenticator
 *
 * An authenticator has three main purposes:
 * 1) Determining if authentication should be performed on an incoming request
 * 2) Gathering credentials necessary for issuing an authentication challenge
 * 3) Authenticating a request that has credentials
 *
 * Asterisk provides a default authenticator, but it may be replaced by a
 * custom one if desired.
 *
 * \param auth The authenticator to register
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_register_authenticator(struct ast_sip_authenticator *auth);

/*!
 * \brief Unregister a SIP authenticator
 *
 * When there is no authenticator registered, requests cannot be challenged
 * or authenticated.
 *
 * \param auth The authenticator to unregister
 */
void ast_sip_unregister_authenticator(struct ast_sip_authenticator *auth);

 /*!
 * \brief Register an outbound SIP authenticator
 *
 * An outbound authenticator is responsible for creating responses to
 * authentication challenges by remote endpoints.
 *
 * \param outbound_auth The authenticator to register
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_register_outbound_authenticator(struct ast_sip_outbound_authenticator *outbound_auth);

/*!
 * \brief Unregister an outbound SIP authenticator
 *
 * When there is no outbound authenticator registered, authentication challenges
 * will be handled as any other final response would be.
 *
 * \param auth The authenticator to unregister
 */
void ast_sip_unregister_outbound_authenticator(struct ast_sip_outbound_authenticator *auth);

/*!
 * \brief Register a SIP endpoint identifier with a name.
 *
 * An endpoint identifier's purpose is to determine which endpoint a given SIP
 * message has come from.
 *
 * Multiple endpoint identifiers may be registered so that if an endpoint
 * cannot be identified by one identifier, it may be identified by another.
 *
 * \param identifier The SIP endpoint identifier to register
 * \param name The name of the endpoint identifier
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_register_endpoint_identifier_with_name(struct ast_sip_endpoint_identifier *identifier,
						   const char *name);

/*!
 * \brief Register a SIP endpoint identifier
 *
 * An endpoint identifier's purpose is to determine which endpoint a given SIP
 * message has come from.
 *
 * Multiple endpoint identifiers may be registered so that if an endpoint
 * cannot be identified by one identifier, it may be identified by another.
 *
 * Asterisk provides two endpoint identifiers. One identifies endpoints based
 * on the user part of the From header URI. The other identifies endpoints based
 * on the source IP address.
 *
 * If the order in which endpoint identifiers is run is important to you, then
 * be sure to load individual endpoint identifier modules in the order you wish
 * for them to be run in modules.conf
 *
 * \note endpoint identifiers registered using this method (no name specified)
 *       are placed at the front of the endpoint identifiers list ahead of any
 *       named identifiers.
 *
 * \param identifier The SIP endpoint identifier to register
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_register_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier);

/*!
 * \brief Unregister a SIP endpoint identifier
 *
 * This stops an endpoint identifier from being used.
 *
 * \param identifier The SIP endoint identifier to unregister
 */
void ast_sip_unregister_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier);

/*!
 * \brief Allocate a new SIP endpoint
 *
 * This will return an endpoint with its refcount increased by one. This reference
 * can be released using ao2_ref().
 *
 * \param name The name of the endpoint.
 * \retval NULL Endpoint allocation failed
 * \retval non-NULL The newly allocated endpoint
 */
void *ast_sip_endpoint_alloc(const char *name);

/*!
 * \brief Change state of a persistent endpoint.
 *
 * \param endpoint_name The SIP endpoint name to change state.
 * \param state The new state
 * \retval 0 Success
 * \retval -1 Endpoint not found
 */
int ast_sip_persistent_endpoint_update_state(const char *endpoint_name, enum ast_endpoint_state state);

/*!
 * \brief Publish the change of state for a contact.
 *
 * \param endpoint_name The SIP endpoint name.
 * \param contact_status The contact status.
 */
void ast_sip_persistent_endpoint_publish_contact_state(const char *endpoint_name, const struct ast_sip_contact_status *contact_status);

/*!
 * \brief Retrieve the current status for a contact.
 *
 * \param contact The contact.
 *
 * \retval non-NULL Success
 * \retval NULL Status information not found
 *
 * \note The returned contact status object is immutable.
 */
struct ast_sip_contact_status *ast_sip_get_contact_status(const struct ast_sip_contact *contact);

/*!
 * \brief Get a pointer to the PJSIP endpoint.
 *
 * This is useful when modules have specific information they need
 * to register with the PJSIP core.
 * \retval NULL endpoint has not been created yet.
 * \retval non-NULL PJSIP endpoint.
 */
pjsip_endpoint *ast_sip_get_pjsip_endpoint(void);

/*!
 * \brief Get a pointer to the SIP sorcery structure.
 *
 * \retval NULL sorcery has not been initialized
 * \retval non-NULL sorcery structure
 */
struct ast_sorcery *ast_sip_get_sorcery(void);

/*!
 * \brief Retrieve a named AOR
 *
 * \param aor_name Name of the AOR
 *
 * \retval NULL if not found
 * \retval non-NULL if found
 */
struct ast_sip_aor *ast_sip_location_retrieve_aor(const char *aor_name);

/*!
 * \brief Retrieve the first bound contact for an AOR
 *
 * \param aor Pointer to the AOR
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 */
struct ast_sip_contact *ast_sip_location_retrieve_first_aor_contact(const struct ast_sip_aor *aor);

/*!
 * \brief Retrieve the first bound contact for an AOR and filter based on flags
 * \since 13.16.0
 *
 * \param aor Pointer to the AOR
 * \param flags Filtering flags
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 */
struct ast_sip_contact *ast_sip_location_retrieve_first_aor_contact_filtered(const struct ast_sip_aor *aor,
	unsigned int flags);

/*!
 * \brief Retrieve all contacts currently available for an AOR
 *
 * \param aor Pointer to the AOR
 *
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 *
 * \warning
 * Since this function prunes expired contacts before returning, it holds a named write
 * lock on the aor.  If you already hold the lock, call ast_sip_location_retrieve_aor_contacts_nolock instead.
 */
struct ao2_container *ast_sip_location_retrieve_aor_contacts(const struct ast_sip_aor *aor);

/*!
 * \brief Retrieve all contacts currently available for an AOR and filter based on flags
 * \since 13.16.0
 *
 * \param aor Pointer to the AOR
 * \param flags Filtering flags
 *
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 *
 * \warning
 * Since this function prunes expired contacts before returning, it holds a named write
 * lock on the aor.  If you already hold the lock, call ast_sip_location_retrieve_aor_contacts_nolock instead.
 */
struct ao2_container *ast_sip_location_retrieve_aor_contacts_filtered(const struct ast_sip_aor *aor,
	unsigned int flags);

/*!
 * \brief Retrieve all contacts currently available for an AOR without locking the AOR
 * \since 13.9.0
 *
 * \param aor Pointer to the AOR
 *
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 *
 * \warning
 * This function should only be called if you already hold a named write lock on the aor.
 */
struct ao2_container *ast_sip_location_retrieve_aor_contacts_nolock(const struct ast_sip_aor *aor);

/*!
 * \brief Retrieve all contacts currently available for an AOR without locking the AOR and filter based on flags
 * \since 13.16.0
 *
 * \param aor Pointer to the AOR
 * \param flags Filtering flags
 *
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 *
 * \warning
 * This function should only be called if you already hold a named write lock on the aor.
 */
struct ao2_container *ast_sip_location_retrieve_aor_contacts_nolock_filtered(const struct ast_sip_aor *aor,
	unsigned int flags);

/*!
 * \brief Retrieve the first bound contact from a list of AORs
 *
 * \param aor_list A comma-separated list of AOR names
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 */
struct ast_sip_contact *ast_sip_location_retrieve_contact_from_aor_list(const char *aor_list);

/*!
 * \brief Retrieve all contacts from a list of AORs
 *
 * \param aor_list A comma-separated list of AOR names
 * \retval NULL if no contacts available
 * \retval non-NULL container (which must be freed) if contacts available
 */
struct ao2_container *ast_sip_location_retrieve_contacts_from_aor_list(const char *aor_list);

/*!
 * \brief Retrieve the first bound contact AND the AOR chosen from a list of AORs
 *
 * \param aor_list A comma-separated list of AOR names
 * \param aor The chosen AOR
 * \param contact The chosen contact
 */
 void ast_sip_location_retrieve_contact_and_aor_from_list(const char *aor_list, struct ast_sip_aor **aor,
	struct ast_sip_contact **contact);

/*!
 * \brief Retrieve the first bound contact AND the AOR chosen from a list of AORs and filter based on flags
 * \since 13.16.0
 *
 * \param aor_list A comma-separated list of AOR names
 * \param flags Filtering flags
 * \param aor The chosen AOR
 * \param contact The chosen contact
 */
void ast_sip_location_retrieve_contact_and_aor_from_list_filtered(const char *aor_list, unsigned int flags,
	struct ast_sip_aor **aor, struct ast_sip_contact **contact);

/*!
 * \brief Retrieve a named contact
 *
 * \param contact_name Name of the contact
 *
 * \retval NULL if not found
 * \retval non-NULL if found
 */
struct ast_sip_contact *ast_sip_location_retrieve_contact(const char *contact_name);

/*!
 * \brief Add a new contact to an AOR
 *
 * \param aor Pointer to the AOR
 * \param uri Full contact URI
 * \param expiration_time Optional expiration time of the contact
 * \param path_info Path information
 * \param user_agent User-Agent header from REGISTER request
 * \param via_addr
 * \param via_port
 * \param call_id
 * \param endpoint The endpoint that resulted in the contact being added
 *
 * \retval -1 failure
 * \retval 0 success
 *
 * \warning
 * This function holds a named write lock on the aor.  If you already hold the lock
 * you should call ast_sip_location_add_contact_nolock instead.
 */
int ast_sip_location_add_contact(struct ast_sip_aor *aor, const char *uri,
	struct timeval expiration_time, const char *path_info, const char *user_agent,
	const char *via_addr, int via_port, const char *call_id,
	struct ast_sip_endpoint *endpoint);

/*!
 * \brief Add a new contact to an AOR without locking the AOR
 * \since 13.9.0
 *
 * \param aor Pointer to the AOR
 * \param uri Full contact URI
 * \param expiration_time Optional expiration time of the contact
 * \param path_info Path information
 * \param user_agent User-Agent header from REGISTER request
 * \param via_addr
 * \param via_port
 * \param call_id
 * \param endpoint The endpoint that resulted in the contact being added
 *
 * \retval -1 failure
 * \retval 0 success
 *
 * \warning
 * This function should only be called if you already hold a named write lock on the aor.
 */
int ast_sip_location_add_contact_nolock(struct ast_sip_aor *aor, const char *uri,
	struct timeval expiration_time, const char *path_info, const char *user_agent,
	const char *via_addr, int via_port, const char *call_id,
	struct ast_sip_endpoint *endpoint);

/*!
 * \brief Create a new contact for an AOR without locking the AOR
 * \since 13.18.0
 *
 * \param aor Pointer to the AOR
 * \param uri Full contact URI
 * \param expiration_time Optional expiration time of the contact
 * \param path_info Path information
 * \param user_agent User-Agent header from REGISTER request
 * \param via_addr
 * \param via_port
 * \param call_id
 * \param prune_on_boot Non-zero if the contact cannot survive a restart/boot.
 * \param endpoint The endpoint that resulted in the contact being added
 *
 * \return The created contact or NULL on failure.
 *
 * \warning
 * This function should only be called if you already hold a named write lock on the aor.
 */
struct ast_sip_contact *ast_sip_location_create_contact(struct ast_sip_aor *aor,
	const char *uri, struct timeval expiration_time, const char *path_info,
	const char *user_agent, const char *via_addr, int via_port, const char *call_id,
	int prune_on_boot, struct ast_sip_endpoint *endpoint);

/*!
 * \brief Update a contact
 *
 * \param contact New contact object with details
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_location_update_contact(struct ast_sip_contact *contact);

/*!
* \brief Delete a contact
*
* \param contact Contact object to delete
*
* \retval -1 failure
* \retval 0 success
*/
int ast_sip_location_delete_contact(struct ast_sip_contact *contact);

/*!
 * \brief Prune the prune_on_boot contacts
 * \since 13.18.0
 */
void ast_sip_location_prune_boot_contacts(void);

/*!
 * \brief Callback called when an outbound request with authentication credentials is to be sent in dialog
 *
 * This callback will have the created request on it. The callback's purpose is to do any extra
 * housekeeping that needs to be done as well as to send the request out.
 *
 * This callback is only necessary if working with a PJSIP API that sits between the application
 * and the dialog layer.
 *
 * \param dlg The dialog to which the request belongs
 * \param tdata The created request to be sent out
 * \param user_data Data supplied with the callback
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
typedef int (*ast_sip_dialog_outbound_auth_cb)(pjsip_dialog *dlg, pjsip_tx_data *tdata, void *user_data);

/*!
 * \brief Set up outbound authentication on a SIP dialog
 *
 * This sets up the infrastructure so that all requests associated with a created dialog
 * can be re-sent with authentication credentials if the original request is challenged.
 *
 * \param dlg The dialog on which requests will be authenticated
 * \param endpoint The endpoint whom this dialog pertains to
 * \param cb Callback to call to send requests with authentication
 * \param user_data Data to be provided to the callback when it is called
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_dialog_setup_outbound_authentication(pjsip_dialog *dlg, const struct ast_sip_endpoint *endpoint,
		ast_sip_dialog_outbound_auth_cb cb, void *user_data);

/*!
 * \brief Retrieves a reference to the artificial auth.
 *
 * \retval The artificial auth
 */
struct ast_sip_auth *ast_sip_get_artificial_auth(void);

/*!
 * \brief Retrieves a reference to the artificial endpoint.
 *
 * \retval The artificial endpoint
 */
struct ast_sip_endpoint *ast_sip_get_artificial_endpoint(void);

/*! \defgroup pjsip_threading PJSIP Threading Model
 * @{
 * \page PJSIP PJSIP Threading Model
 *
 * There are three major types of threads that SIP will have to deal with:
 * \li Asterisk threads
 * \li PJSIP threads
 * \li SIP threadpool threads (a.k.a. "servants")
 *
 * \par Asterisk Threads
 *
 * Asterisk threads are those that originate from outside of SIP but within
 * Asterisk. The most common of these threads are PBX (channel) threads and
 * the autoservice thread. Most interaction with these threads will be through
 * channel technology callbacks. Within these threads, it is fine to handle
 * Asterisk data from outside of SIP, but any handling of SIP data should be
 * left to servants, \b especially if you wish to call into PJSIP for anything.
 * Asterisk threads are not registered with PJLIB, so attempting to call into
 * PJSIP will cause an assertion to be triggered, thus causing the program to
 * crash.
 *
 * \par PJSIP Threads
 *
 * PJSIP threads are those that originate from handling of PJSIP events, such
 * as an incoming SIP request or response, or a transaction timeout. The role
 * of these threads is to process information as quickly as possible so that
 * the next item on the SIP socket(s) can be serviced. On incoming messages,
 * Asterisk automatically will push the request to a servant thread. When your
 * module callback is called, processing will already be in a servant. However,
 * for other PJSIP events, such as transaction state changes due to timer
 * expirations, your module will be called into from a PJSIP thread. If you
 * are called into from a PJSIP thread, then you should push whatever processing
 * is needed to a servant as soon as possible. You can discern if you are currently
 * in a SIP servant thread using the \ref ast_sip_thread_is_servant function.
 *
 * \par Servants
 *
 * Servants are where the bulk of SIP work should be performed. These threads
 * exist in order to do the work that Asterisk threads and PJSIP threads hand
 * off to them. Servant threads register themselves with PJLIB, meaning that
 * they are capable of calling PJSIP and PJLIB functions if they wish.
 *
 * \par Serializer
 *
 * Tasks are handed off to servant threads using the API call \ref ast_sip_push_task.
 * The first parameter of this call is a serializer. If this pointer
 * is NULL, then the work will be handed off to whatever servant can currently handle
 * the task. If this pointer is non-NULL, then the task will not be executed until
 * previous tasks pushed with the same serializer have completed. For more information
 * on serializers and the benefits they provide, see \ref ast_threadpool_serializer
 *
 * \par Scheduler
 *
 * Some situations require that a task run periodically or at a future time.  Normally
 * the ast_sched functionality would be used but ast_sched only uses 1 thread for all
 * tasks and that thread isn't registered with PJLIB and therefore can't do any PJSIP
 * related work.
 *
 * ast_sip_sched uses ast_sched only as a scheduled queue.  When a task is ready to run,
 * it's pushed to a Serializer to be invoked asynchronously by a Servant.  This ensures
 * that the task is executed in a PJLIB registered thread and allows the ast_sched thread
 * to immediately continue processing the queue.  The Serializer used by ast_sip_sched
 * is one of your choosing or a random one from the res_pjsip pool if you don't choose one.
 *
 * \note
 *
 * Do not make assumptions about individual threads based on a corresponding serializer.
 * In other words, just because several tasks use the same serializer when being pushed
 * to servants, it does not mean that the same thread is necessarily going to execute those
 * tasks, even though they are all guaranteed to be executed in sequence.
 */

typedef int (*ast_sip_task)(void *user_data);

/*!
 * \brief Create a new serializer for SIP tasks
 * \since 13.8.0
 *
 * See \ref ast_threadpool_serializer for more information on serializers.
 * SIP creates serializers so that tasks operating on similar data will run
 * in sequence.
 *
 * \param name Name of the serializer. (must be unique)
 *
 * \retval NULL Failure
 * \retval non-NULL Newly-created serializer
 */
struct ast_taskprocessor *ast_sip_create_serializer(const char *name);

struct ast_serializer_shutdown_group;

/*!
 * \brief Create a new serializer for SIP tasks
 * \since 13.8.0
 *
 * See \ref ast_threadpool_serializer for more information on serializers.
 * SIP creates serializers so that tasks operating on similar data will run
 * in sequence.
 *
 * \param name Name of the serializer. (must be unique)
 * \param shutdown_group Group shutdown controller. (NULL if no group association)
 *
 * \retval NULL Failure
 * \retval non-NULL Newly-created serializer
 */
struct ast_taskprocessor *ast_sip_create_serializer_group(const char *name, struct ast_serializer_shutdown_group *shutdown_group);

/*!
 * \brief Determine the distributor serializer for the SIP message.
 * \since 13.10.0
 *
 * \param rdata The incoming message.
 *
 * \retval Calculated distributor serializer on success.
 * \retval NULL on error.
 */
struct ast_taskprocessor *ast_sip_get_distributor_serializer(pjsip_rx_data *rdata);

/*!
 * \brief Set a serializer on a SIP dialog so requests and responses are automatically serialized
 *
 * Passing a NULL serializer is a way to remove a serializer from a dialog.
 *
 * \param dlg The SIP dialog itself
 * \param serializer The serializer to use
 */
void ast_sip_dialog_set_serializer(pjsip_dialog *dlg, struct ast_taskprocessor *serializer);

/*!
 * \brief Set an endpoint on a SIP dialog so in-dialog requests do not undergo endpoint lookup.
 *
 * \param dlg The SIP dialog itself
 * \param endpoint The endpoint that this dialog is communicating with
 */
void ast_sip_dialog_set_endpoint(pjsip_dialog *dlg, struct ast_sip_endpoint *endpoint);

/*!
 * \brief Get the endpoint associated with this dialog
 *
 * This function increases the refcount of the endpoint by one. Release
 * the reference once you are finished with the endpoint.
 *
 * \param dlg The SIP dialog from which to retrieve the endpoint
 * \retval NULL No endpoint associated with this dialog
 * \retval non-NULL The endpoint.
 */
struct ast_sip_endpoint *ast_sip_dialog_get_endpoint(pjsip_dialog *dlg);

/*!
 * \brief Pushes a task to SIP servants
 *
 * This uses the serializer provided to determine how to push the task.
 * If the serializer is NULL, then the task will be pushed to the
 * servants directly. If the serializer is non-NULL, then the task will be
 * queued behind other tasks associated with the same serializer.
 *
 * \param serializer The serializer to which the task belongs. Can be NULL
 * \param sip_task The task to execute
 * \param task_data The parameter to pass to the task when it executes
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_push_task(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data);

/*!
 * \brief Push a task to SIP servants and wait for it to complete.
 *
 * Like \ref ast_sip_push_task except that it blocks until the task
 * completes.  If the current thread is a SIP servant thread then the
 * task executes immediately.  Otherwise, the specified serializer
 * executes the task and the current thread waits for it to complete.
 *
 * \note PJPROJECT callbacks tend to have locks already held when
 * called.
 *
 * \warning \b Never hold locks that may be acquired by a SIP servant
 * thread when calling this function.  Doing so may cause a deadlock
 * if all SIP servant threads are blocked waiting to acquire the lock
 * while the thread holding the lock is waiting for a free SIP servant
 * thread.
 *
 * \warning \b Use of this function in an ao2 destructor callback is a
 * bad idea.  You don't have control over which thread executes the
 * destructor.  Attempting to shift execution to another thread with
 * this function is likely to cause deadlock.
 *
 * \param serializer The SIP serializer to execute the task if the
 * current thread is not a SIP servant.  NULL if any of the default
 * serializers can be used.
 * \param sip_task The task to execute
 * \param task_data The parameter to pass to the task when it executes
 *
 * \note The sip_task() return value may need to be distinguished from
 * the failure to push the task.
 *
 * \return sip_task() return value on success.
 * \retval -1 Failure to push the task.
 */
int ast_sip_push_task_wait_servant(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data);

/*!
 * \brief Push a task to SIP servants and wait for it to complete.
 * \deprecated Replaced with ast_sip_push_task_wait_servant().
 */
int ast_sip_push_task_synchronous(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data);

/*!
 * \brief Push a task to the serializer and wait for it to complete.
 *
 * Like \ref ast_sip_push_task except that it blocks until the task is
 * completed by the specified serializer.  If the specified serializer
 * is the current thread then the task executes immediately.
 *
 * \note PJPROJECT callbacks tend to have locks already held when
 * called.
 *
 * \warning \b Never hold locks that may be acquired by a SIP servant
 * thread when calling this function.  Doing so may cause a deadlock
 * if all SIP servant threads are blocked waiting to acquire the lock
 * while the thread holding the lock is waiting for a free SIP servant
 * thread for the serializer to execute in.
 *
 * \warning \b Never hold locks that may be acquired by the serializer
 * when calling this function.  Doing so will cause a deadlock.
 *
 * \warning \b Never use this function in the pjsip monitor thread (It
 * is a SIP servant thread).  This is likely to cause a deadlock.
 *
 * \warning \b Use of this function in an ao2 destructor callback is a
 * bad idea.  You don't have control over which thread executes the
 * destructor.  Attempting to shift execution to another thread with
 * this function is likely to cause deadlock.
 *
 * \param serializer The SIP serializer to execute the task.  NULL if
 * any of the default serializers can be used.
 * \param sip_task The task to execute
 * \param task_data The parameter to pass to the task when it executes
 *
 * \note It is generally better to call
 * ast_sip_push_task_wait_servant() if you pass NULL for the
 * serializer parameter.
 *
 * \note The sip_task() return value may need to be distinguished from
 * the failure to push the task.
 *
 * \return sip_task() return value on success.
 * \retval -1 Failure to push the task.
 */
int ast_sip_push_task_wait_serializer(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data);

/*!
 * \brief Determine if the current thread is a SIP servant thread
 *
 * \retval 0 This is not a SIP servant thread
 * \retval 1 This is a SIP servant thread
 */
int ast_sip_thread_is_servant(void);

/*!
 * \brief Task flags for the res_pjsip scheduler
 *
 * The default is AST_SIP_SCHED_TASK_FIXED
 *                | AST_SIP_SCHED_TASK_DATA_NOT_AO2
 *                | AST_SIP_SCHED_TASK_DATA_NO_CLEANUP
 *                | AST_SIP_SCHED_TASK_PERIODIC
 */
enum ast_sip_scheduler_task_flags {
	/*!
	 * The defaults
	 */
	AST_SIP_SCHED_TASK_DEFAULTS = (0 << 0),

	/*!
	 * Run at a fixed interval.
	 * Stop scheduling if the callback returns <= 0.
	 * Any other value is ignored.
	 */
	AST_SIP_SCHED_TASK_FIXED = (0 << 0),
	/*!
	 * Run at a variable interval.
	 * Stop scheduling if the callback returns <= 0.
	 * Any other return value is used as the new interval.
	 */
	AST_SIP_SCHED_TASK_VARIABLE = (1 << 0),

	/*!
	 * Run just once.
	 * Return values are ignored.
	 */
	AST_SIP_SCHED_TASK_ONESHOT = (1 << 6),

	/*!
	 * The task data is not an AO2 object.
	 */
	AST_SIP_SCHED_TASK_DATA_NOT_AO2 = (0 << 1),
	/*!
	 * The task data is an AO2 object.
	 * A reference count will be held by the scheduler until
	 * after the task has run for the final time (if ever).
	 */
	AST_SIP_SCHED_TASK_DATA_AO2 = (1 << 1),

	/*!
	 * Don't take any cleanup action on the data
	 */
	AST_SIP_SCHED_TASK_DATA_NO_CLEANUP = (0 << 3),
	/*!
	 * If AST_SIP_SCHED_TASK_DATA_AO2 is set, decrement the reference count
	 * otherwise call ast_free on it.
	 */
	AST_SIP_SCHED_TASK_DATA_FREE = ( 1 << 3 ),

	/*!
	 * \brief The task is scheduled at multiples of interval
	 * \see Interval
	 */
	AST_SIP_SCHED_TASK_PERIODIC = (0 << 4),
	/*!
	 * \brief The next invocation of the task is at last finish + interval
	 * \see Interval
	 */
	AST_SIP_SCHED_TASK_DELAY = (1 << 4),
	/*!
	 * \brief The scheduled task's events are tracked in the debug log.
	 * \details
	 * Schedule events such as scheduling, running, rescheduling, canceling,
	 * and destroying are logged about the task.
	 */
	AST_SIP_SCHED_TASK_TRACK = (1 << 5),
};

/*!
 * \brief Scheduler task data structure
 */
struct ast_sip_sched_task;

/*!
 * \brief Schedule a task to run in the res_pjsip thread pool
 * \since 13.9.0
 *
 * \param serializer The serializer to use.  If NULL, don't use a serializer (see note below)
 * \param interval The invocation interval in milliseconds (see note below)
 * \param sip_task The task to invoke
 * \param name An optional name to associate with the task
 * \param task_data Optional data to pass to the task
 * \param flags One of enum ast_sip_scheduler_task_type
 *
 * \returns Pointer to \ref ast_sip_sched_task ao2 object which must be dereferenced when done.
 *
 * \par Serialization
 *
 * Specifying a serializer guarantees serialized execution but NOT specifying a serializer
 * may still result in tasks being effectively serialized if the thread pool is busy.
 * The point of the serializer BTW is not to prevent parallel executions of the SAME task.
 * That happens automatically (see below).  It's to prevent the task from running at the same
 * time as other work using the same serializer, whether or not it's being run by the scheduler.
 *
 * \par Interval
 *
 * The interval is used to calculate the next time the task should run.  There are two models.
 *
 * \ref AST_SIP_SCHED_TASK_PERIODIC specifies that the invocations of the task occur at the
 * specific interval.  That is, every \p interval milliseconds, regardless of how long the task
 * takes. If the task takes longer than \p interval, it will be scheduled at the next available
 * multiple of \p interval.  For example: If the task has an interval of 60 seconds and the task
 * takes 70 seconds, the next invocation will happen at 120 seconds.
 *
 * \ref AST_SIP_SCHED_TASK_DELAY specifies that the next invocation of the task should start
 * at \p interval milliseconds after the current invocation has finished.
 *
 */
struct ast_sip_sched_task *ast_sip_schedule_task(struct ast_taskprocessor *serializer,
	int interval, ast_sip_task sip_task, const char *name, void *task_data,
	enum ast_sip_scheduler_task_flags flags);

/*!
 * \brief Cancels the next invocation of a task
 * \since 13.9.0
 *
 * \param schtd The task structure pointer
 * \retval 0 Success
 * \retval -1 Failure
 * \note Only cancels future invocations not the currently running invocation.
 */
int ast_sip_sched_task_cancel(struct ast_sip_sched_task *schtd);

/*!
 * \brief Cancels the next invocation of a task by name
 * \since 13.9.0
 *
 * \param name The task name
 * \retval 0 Success
 * \retval -1 Failure
 * \note Only cancels future invocations not the currently running invocation.
 */
int ast_sip_sched_task_cancel_by_name(const char *name);

/*!
 * \brief Gets the last start and end times of the task
 * \since 13.9.0
 *
 * \param schtd The task structure pointer
 * \param[out] when_queued Pointer to a timeval structure to contain the time when queued
 * \param[out] last_start Pointer to a timeval structure to contain the time when last started
 * \param[out] last_end Pointer to a timeval structure to contain the time when last ended
 * \retval 0 Success
 * \retval -1 Failure
 * \note Any of the pointers can be NULL if you don't need them.
 */
int ast_sip_sched_task_get_times(struct ast_sip_sched_task *schtd,
	struct timeval *when_queued, struct timeval *last_start, struct timeval *last_end);

/*!
 * \brief Gets the queued, last start, last_end, time left, interval, next run
 * \since 16.15.0
 * \since 18.1.0
 *
 * \param schtd The task structure pointer
 * \param[out] when_queued Pointer to a timeval structure to contain the time when queued
 * \param[out] last_start Pointer to a timeval structure to contain the time when last started
 * \param[out] last_end Pointer to a timeval structure to contain the time when last ended
 * \param[out] interval Pointer to an int to contain the interval in ms
 * \param[out] time_left Pointer to an int to contain the ms left to the next run
 * \param[out] next_start Pointer to a timeval structure to contain the next run time
 * \retval 0 Success
 * \retval -1 Failure
 * \note Any of the pointers can be NULL if you don't need them.
 */
int ast_sip_sched_task_get_times2(struct ast_sip_sched_task *schtd,
	struct timeval *when_queued, struct timeval *last_start, struct timeval *last_end,
	int *interval, int *time_left, struct timeval *next_start);

/*!
 * \brief Gets the last start and end times of the task by name
 * \since 13.9.0
 *
 * \param name The task name
 * \param[out] when_queued Pointer to a timeval structure to contain the time when queued
 * \param[out] last_start Pointer to a timeval structure to contain the time when last started
 * \param[out] last_end Pointer to a timeval structure to contain the time when last ended
 * \retval 0 Success
 * \retval -1 Failure
 * \note Any of the pointers can be NULL if you don't need them.
 */
int ast_sip_sched_task_get_times_by_name(const char *name,
	struct timeval *when_queued, struct timeval *last_start, struct timeval *last_end);

/*!
 * \brief Gets the queued, last start, last_end, time left, interval, next run by task name
 * \since 16.15.0
 * \since 18.1.0
 *
 * \param name The task name
 * \param[out] when_queued Pointer to a timeval structure to contain the time when queued
 * \param[out] last_start Pointer to a timeval structure to contain the time when last started
 * \param[out] last_end Pointer to a timeval structure to contain the time when last ended
 * \param[out] interval Pointer to an int to contain the interval in ms
 * \param[out] time_left Pointer to an int to contain the ms left to the next run
 * \param[out] next_start Pointer to a timeval structure to contain the next run time
 * \retval 0 Success
 * \retval -1 Failure
 * \note Any of the pointers can be NULL if you don't need them.
 */
int ast_sip_sched_task_get_times_by_name2(const char *name,
	struct timeval *when_queued, struct timeval *last_start, struct timeval *last_end,
	int *interval, int *time_left, struct timeval *next_start);

/*!
 * \brief Gets the number of milliseconds until the next invocation
 * \since 13.9.0
 *
 * \param schtd The task structure pointer
 * \return The number of milliseconds until the next invocation or -1 if the task isn't scheduled
 */
int ast_sip_sched_task_get_next_run(struct ast_sip_sched_task *schtd);

/*!
 * \brief Gets the number of milliseconds until the next invocation
 * \since 13.9.0
 *
 * \param name The task name
 * \return The number of milliseconds until the next invocation or -1 if the task isn't scheduled
 */
int ast_sip_sched_task_get_next_run_by_name(const char *name);

/*!
 * \brief Checks if the task is currently running
 * \since 13.9.0
 *
 * \param schtd The task structure pointer
 * \retval 0 not running
 * \retval 1 running
 */
int ast_sip_sched_is_task_running(struct ast_sip_sched_task *schtd);

/*!
 * \brief Checks if the task is currently running
 * \since 13.9.0
 *
 * \param name The task name
 * \retval 0 not running or not found
 * \retval 1 running
 */
int ast_sip_sched_is_task_running_by_name(const char *name);

/*!
 * \brief Gets the task name
 * \since 13.9.0
 *
 * \param schtd The task structure pointer
 * \param name, maxlen
 * \retval 0 success
 * \retval 1 failure
 */
int ast_sip_sched_task_get_name(struct ast_sip_sched_task *schtd, char *name, size_t maxlen);

/*!
 *  @}
 */

/*!
 * \brief SIP body description
 *
 * This contains a type and subtype that will be added as
 * the "Content-Type" for the message as well as the body
 * text.
 */
struct ast_sip_body {
	/*! Type of the body, such as "application" */
	const char *type;
	/*! Subtype of the body, such as "sdp" */
	const char *subtype;
	/*! The text to go in the body */
	const char *body_text;
};

/*!
 * \brief General purpose method for creating a UAC dialog with an endpoint
 *
 * \param endpoint A pointer to the endpoint
 * \param aor_name Optional name of the AOR to target, may even be an explicit SIP URI
 * \param request_user Optional user to place into the target URI
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
pjsip_dialog *ast_sip_create_dialog_uac(const struct ast_sip_endpoint *endpoint, const char *aor_name, const char *request_user);

/*!
 * \brief General purpose method for creating a UAS dialog with an endpoint
 *
 * \deprecated This function is unsafe (due to the returned object not being locked nor
 *             having its reference incremented) and should no longer be used. Instead
 *             use ast_sip_create_dialog_uas_locked so a properly locked and referenced
 *             object is returned.
 *
 * \param endpoint A pointer to the endpoint
 * \param rdata The request that is starting the dialog
 * \param[out] status On failure, the reason for failure in creating the dialog
 */
pjsip_dialog *ast_sip_create_dialog_uas(const struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, pj_status_t *status);

/*!
 * \brief General purpose method for creating a UAS dialog with an endpoint
 *
 * This function creates and returns a locked, and referenced counted pjsip
 * dialog object. The caller is thus responsible for freeing the allocated
 * memory, decrementing the reference, and releasing the lock when done with
 * the returned object.
 *
 * \note The safest way to unlock the object, and decrement its reference is by
 *       calling pjsip_dlg_dec_lock. Alternatively, pjsip_dlg_dec_session can be
 *       used to decrement the reference only.
 *
 * The dialog is returned locked and with a reference in order to ensure that the
 * dialog object, and any of its associated objects (e.g. transaction) are not
 * untimely destroyed. For instance, that could happen when a transport error
 * occurs.
 *
 * As long as the caller maintains a reference to the dialog there should be no
 * worry that it might unknowingly be destroyed. However, once the caller unlocks
 * the dialog there is a danger that some of the dialog's internal objects could
 * be lost and/or compromised. For example, when the aforementioned transport error
 * occurs the dialog's associated transaction gets destroyed (see pjsip_dlg_on_tsx_state
 * in sip_dialog.c, and mod_inv_on_tsx_state in sip_inv.c).
 *
 * In this case and before using the dialog again the caller should re-lock the
 * dialog, check to make sure the dialog is still established, and the transaction
 * still exists and has not been destroyed.
 *
 * \param endpoint A pointer to the endpoint
 * \param rdata The request that is starting the dialog
 * \param[out] status On failure, the reason for failure in creating the dialog
 *
 * \retval A locked, and reference counted pjsip_dialog object.
 * \retval NULL on failure
 */
pjsip_dialog *ast_sip_create_dialog_uas_locked(const struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata, pj_status_t *status);

/*!
 * \brief General purpose method for creating an rdata structure using specific information
 * \since 13.15.0
 *
 * \param[out] rdata The rdata structure that will be populated
 * \param packet A SIP message
 * \param src_name The source IP address of the message
 * \param src_port The source port of the message
 * \param transport_type The type of transport the message was received on
 * \param local_name The local IP address the message was received on
 * \param local_port The local port the message was received on
 * \param contact_uri The contact URI of the message
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_sip_create_rdata_with_contact(pjsip_rx_data *rdata, char *packet,
	const char *src_name, int src_port, char *transport_type, const char *local_name,
	int local_port, const char *contact_uri);

/*!
 * \brief General purpose method for creating an rdata structure using specific information
 *
 * \param[out] rdata The rdata structure that will be populated
 * \param packet A SIP message
 * \param src_name The source IP address of the message
 * \param src_port The source port of the message
 * \param transport_type The type of transport the message was received on
 * \param local_name The local IP address the message was received on
 * \param local_port The local port the message was received on
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_sip_create_rdata(pjsip_rx_data *rdata, char *packet, const char *src_name,
	int src_port, char *transport_type, const char *local_name, int local_port);

/*!
 * \brief General purpose method for creating a SIP request
 *
 * Its typical use would be to create one-off requests such as an out of dialog
 * SIP MESSAGE.
 *
 * The request can either be in- or out-of-dialog. If in-dialog, the
 * dlg parameter MUST be present. If out-of-dialog the endpoint parameter
 * MUST be present. If both are present, then we will assume that the message
 * is to be sent in-dialog.
 *
 * The uri parameter can be specified if the request should be sent to an explicit
 * URI rather than one configured on the endpoint.
 *
 * \param method The method of the SIP request to send
 * \param dlg Optional. If specified, the dialog on which to request the message.
 * \param endpoint Optional. If specified, the request will be created out-of-dialog to the endpoint.
 * \param uri Optional. If specified, the request will be sent to this URI rather
 * than one configured for the endpoint.
 * \param contact The contact with which this request is associated for out-of-dialog requests.
 * \param[out] tdata The newly-created request
 *
 * The provided contact is attached to tdata with its reference bumped, but will
 * not survive for the entire lifetime of tdata since the contact is cleaned up
 * when all supplements have completed execution.
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_create_request(const char *method, struct pjsip_dialog *dlg,
		struct ast_sip_endpoint *endpoint, const char *uri,
		struct ast_sip_contact *contact, pjsip_tx_data **tdata);

/*!
 * \brief General purpose method for sending a SIP request
 *
 * This is a companion function for \ref ast_sip_create_request. The request
 * created there can be passed to this function, though any request may be
 * passed in.
 *
 * This will automatically set up handling outbound authentication challenges if
 * they arrive.
 *
 * \param tdata The request to send
 * \param dlg Optional. The dialog in which the request is sent.  Otherwise it is out-of-dialog.
 * \param endpoint Optional. If specified, the out-of-dialog request is sent to the endpoint.
 * \param token Data to be passed to the callback upon receipt of out-of-dialog response.
 * \param callback Callback to be called upon receipt of out-of-dialog response.
 *
 * \retval 0 Success
 * \retval -1 Failure (out-of-dialog callback will not be called.)
 */
int ast_sip_send_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg,
	struct ast_sip_endpoint *endpoint, void *token,
	void (*callback)(void *token, pjsip_event *e));

/*!
 * \brief General purpose method for sending an Out-Of-Dialog SIP request
 *
 * This is a companion function for \ref ast_sip_create_request. The request
 * created there can be passed to this function, though any request may be
 * passed in.
 *
 * This will automatically set up handling outbound authentication challenges if
 * they arrive.
 *
 * \param tdata The request to send
 * \param endpoint Optional. If specified, the out-of-dialog request is sent to the endpoint.
 * \param timeout If non-zero, after the timeout the transaction will be terminated
 * and the callback will be called with the PJSIP_EVENT_TIMER type.
 * \param token Data to be passed to the callback upon receipt of out-of-dialog response.
 * \param callback Callback to be called upon receipt of out-of-dialog response.
 *
 * \retval 0 Success
 * \retval -1 Failure (out-of-dialog callback will not be called.)
 *
 * \note Timeout processing:
 * There are 2 timers associated with this request, PJSIP timer_b which is
 * set globally in the "system" section of pjsip.conf, and the timeout specified
 * on this call.  The timer that expires first (before normal completion) will
 * cause the callback to be run with e->body.tsx_state.type = PJSIP_EVENT_TIMER.
 * The timer that expires second is simply ignored and the callback is not run again.
 */
int ast_sip_send_out_of_dialog_request(pjsip_tx_data *tdata,
	struct ast_sip_endpoint *endpoint, int timeout, void *token,
	void (*callback)(void *token, pjsip_event *e));

/*!
 * \brief General purpose method for creating a SIP response
 *
 * Its typical use would be to create responses for out of dialog
 * requests.
 *
 * \param rdata The rdata from the incoming request.
 * \param st_code The response code to transmit.
 * \param contact The contact with which this request is associated.
 * \param[out] p_tdata The newly-created response
 *
 * The provided contact is attached to tdata with its reference bumped, but will
 * not survive for the entire lifetime of tdata since the contact is cleaned up
 * when all supplements have completed execution.
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_create_response(const pjsip_rx_data *rdata, int st_code,
	struct ast_sip_contact *contact, pjsip_tx_data **p_tdata);

/*!
 * \brief Send a response to an out of dialog request
 *
 * Use this function sparingly, since this does not create a transaction
 * within PJSIP. This means that if the request is retransmitted, it is
 * your responsibility to detect this and not process the same request
 * twice, and to send the same response for each retransmission.
 *
 * \param res_addr The response address for this response
 * \param tdata The response to send
 * \param sip_endpoint The ast_sip_endpoint associated with this response
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_send_response(pjsip_response_addr *res_addr, pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint);

/*!
 * \brief Send a stateful response to an out of dialog request
 *
 * This creates a transaction within PJSIP, meaning that if the request
 * that we are responding to is retransmitted, we will not attempt to
 * re-handle the request.
 *
 * \param rdata The request that is being responded to
 * \param tdata The response to send
 * \param sip_endpoint The ast_sip_endpoint associated with this response
 *
 * \since 13.4.0
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_send_stateful_response(pjsip_rx_data *rdata, pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint);

/*!
 * \brief Determine if an incoming request requires authentication
 *
 * This calls into the registered authenticator's requires_authentication callback
 * in order to determine if the request requires authentication.
 *
 * If there is no registered authenticator, then authentication will be assumed
 * not to be required.
 *
 * \param endpoint The endpoint from which the request originates
 * \param rdata The incoming SIP request
 * \retval non-zero The request requires authentication
 * \retval 0 The request does not require authentication
 */
int ast_sip_requires_authentication(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata);

/*!
 * \brief Method to determine authentication status of an incoming request
 *
 * This will call into a registered authenticator. The registered authenticator will
 * do what is necessary to determine whether the incoming request passes authentication.
 * A tentative response is passed into this function so that if, say, a digest authentication
 * challenge should be sent in the ensuing response, it can be added to the response.
 *
 * \param endpoint The endpoint from the request was sent
 * \param rdata The request to potentially authenticate
 * \param tdata Tentative response to the request
 * \return The result of checking authentication.
 */
enum ast_sip_check_auth_result ast_sip_check_authentication(struct ast_sip_endpoint *endpoint,
		pjsip_rx_data *rdata, pjsip_tx_data *tdata);

/*!
 * \brief Create a response to an authentication challenge
 *
 * This will call into an outbound authenticator's create_request_with_auth callback
 * to create a new request with authentication credentials. See the create_request_with_auth
 * callback in the \ref ast_sip_outbound_authenticator structure for details about
 * the parameters and return values.
 */
int ast_sip_create_request_with_auth(const struct ast_sip_auth_vector *auths, pjsip_rx_data *challenge,
		pjsip_tx_data *tdata, pjsip_tx_data **new_request);

/*!
 * \brief Determine the endpoint that has sent a SIP message
 *
 * This will call into each of the registered endpoint identifiers'
 * identify_endpoint() callbacks until one returns a non-NULL endpoint.
 * This will return an ao2 object. Its reference count will need to be
 * decremented when completed using the endpoint.
 *
 * \param rdata The inbound SIP message to use when identifying the endpoint.
 * \retval NULL No matching endpoint
 * \retval non-NULL The matching endpoint
 */
struct ast_sip_endpoint *ast_sip_identify_endpoint(pjsip_rx_data *rdata);

/*!
 * \brief Get a specific header value from rdata
 *
 * \note The returned value does not need to be freed since it's from the rdata pool
 *
 * \param rdata The rdata
 * \param str The header to find
 *
 * \retval NULL on failure
 * \retval The header value on success
 */
char *ast_sip_rdata_get_header_value(pjsip_rx_data *rdata, const pj_str_t str);

/*!
 * \brief Set the outbound proxy for an outbound SIP message
 *
 * \param tdata The message to set the outbound proxy on
 * \param proxy SIP uri of the proxy
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_set_outbound_proxy(pjsip_tx_data *tdata, const char *proxy);

/*!
 * \brief Add a header to an outbound SIP message
 *
 * \param tdata The message to add the header to
 * \param name The header name
 * \param value The header value
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_add_header(pjsip_tx_data *tdata, const char *name, const char *value);

/*!
 * \brief Add a header to an outbound SIP message, returning a pointer to the header
 *
 * \param tdata The message to add the header to
 * \param name The header name
 * \param value The header value
 * \return The pjsip_generic_string_hdr * added.
 */
pjsip_generic_string_hdr *ast_sip_add_header2(pjsip_tx_data *tdata,
	const char *name, const char *value);

/*!
 * \brief Add a body to an outbound SIP message
 *
 * If this is called multiple times, the latest body will replace the current
 * body.
 *
 * \param tdata The message to add the body to
 * \param body The message body to add
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_add_body(pjsip_tx_data *tdata, const struct ast_sip_body *body);

/*!
 * \brief Add a multipart body to an outbound SIP message
 *
 * This will treat each part of the input vector as part of a multipart body and
 * add each part to the SIP message.
 *
 * \param tdata The message to add the body to
 * \param bodies The message bodies to add
 * \param num_bodies The parts of the body to add
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_add_body_multipart(pjsip_tx_data *tdata, const struct ast_sip_body *bodies[], int num_bodies);

/*!
 * \brief Append body data to a SIP message
 *
 * This acts mostly the same as ast_sip_add_body, except that rather than replacing
 * a body if it currently exists, it appends data to an existing body.
 *
 * \param tdata The message to append the body to
 * \param body_text The string to append to the end of the current body
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_append_body(pjsip_tx_data *tdata, const char *body_text);

/*!
 * \brief Copy a pj_str_t into a standard character buffer.
 *
 * pj_str_t is not NULL-terminated. Any place that expects a NULL-
 * terminated string needs to have the pj_str_t copied into a separate
 * buffer.
 *
 * This method copies the pj_str_t contents into the destination buffer
 * and NULL-terminates the buffer.
 *
 * \param dest The destination buffer
 * \param src The pj_str_t to copy
 * \param size The size of the destination buffer.
 */
void ast_copy_pj_str(char *dest, const pj_str_t *src, size_t size);

/*!
 * \brief Create and copy a pj_str_t into a standard character buffer.
 *
 * pj_str_t is not NULL-terminated. Any place that expects a NULL-
 * terminated string needs to have the pj_str_t copied into a separate
 * buffer.
 *
 * Copies the pj_str_t contents into a newly allocated buffer pointed to
 * by dest. NULL-terminates the buffer.
 *
 * \note Caller is responsible for freeing the allocated memory.
 *
 * \param[out] dest The destination buffer
 * \param src The pj_str_t to copy
 * \return Number of characters copied or negative value on error
 */
int ast_copy_pj_str2(char **dest, const pj_str_t *src);

/*!
 * \brief Get the looked-up endpoint on an out-of dialog request or response
 *
 * The function may ONLY be called on out-of-dialog requests or responses. For
 * in-dialog requests and responses, it is required that the user of the dialog
 * has the looked-up endpoint stored locally.
 *
 * This function should never return NULL if the message is out-of-dialog. It will
 * always return NULL if the message is in-dialog.
 *
 * This function will increase the reference count of the returned endpoint by one.
 * Release your reference using the ao2_ref function when finished.
 *
 * \param rdata Out-of-dialog request or response
 * \return The looked up endpoint
 */
struct ast_sip_endpoint *ast_pjsip_rdata_get_endpoint(pjsip_rx_data *rdata);

/*!
 * \brief Add 'user=phone' parameter to URI if enabled and user is a phone number.
 *
 * \param endpoint The endpoint to use for configuration
 * \param pool The memory pool to allocate the parameter from
 * \param uri The URI to check for user and to add parameter to
 */
void ast_sip_add_usereqphone(const struct ast_sip_endpoint *endpoint, pj_pool_t *pool, pjsip_uri *uri);

/*!
 * \brief Retrieve any endpoints available to sorcery.
 *
 * \retval Endpoints available to sorcery, NULL if no endpoints found.
 */
struct ao2_container *ast_sip_get_endpoints(void);

/*!
 * \brief Retrieve the default outbound endpoint.
 *
 * \retval The default outbound endpoint, NULL if not found.
 */
struct ast_sip_endpoint *ast_sip_default_outbound_endpoint(void);

/*!
 * \brief Retrieve relevant SIP auth structures from sorcery
 *
 * \param auths Vector of sorcery IDs of auth credentials to retrieve
 * \param[out] out The retrieved auths are stored here
 */
int ast_sip_retrieve_auths(const struct ast_sip_auth_vector *auths, struct ast_sip_auth **out);

/*!
 * \brief Clean up retrieved auth structures from memory
 *
 * Call this function once you have completed operating on auths
 * retrieved from \ref ast_sip_retrieve_auths
 *
 * \param auths An array of auth object pointers to clean up
 * \param num_auths The number of auths in the array
 */
void ast_sip_cleanup_auths(struct ast_sip_auth *auths[], size_t num_auths);

AST_VECTOR(ast_sip_auth_objects_vector, struct ast_sip_auth *);
/*!
 * \brief Retrieve relevant SIP auth structures from sorcery as a vector
 *
 * \param auth_ids Vector of sorcery IDs of auth credentials to retrieve
 * \param[out] auth_objects A pointer ast_sip_auth_objects_vector to hold the objects
 *
 * \retval 0 Success
 * \retval -1 Number of auth objects found is less than the number of names supplied.
 *
 * \warning The number of auth objects retrieved may be less than the
 * number of auth ids supplied if auth objects couldn't be found for
 * some of them.
 *
 * \note Since the ref count on all auth objects returned has been
 * bumped, you must call ast_sip_cleanup_auth_objects_vector() to decrement
 * the ref count on all of the auth objects in the vector,
 * then call AST_VECTOR_FREE() on the vector itself.
 *
 */
int ast_sip_retrieve_auths_vector(const struct ast_sip_auth_vector *auth_ids,
	struct ast_sip_auth_objects_vector *auth_objects);

/*!
 * \brief Clean up retrieved auth objects in vector
 *
 * Call this function once you have completed operating on auths
 * retrieved from \ref ast_sip_retrieve_auths_vector.  All
 * auth objects will have their reference counts decremented and
 * the vector size will be reset to 0.  You must still call
 * AST_VECTOR_FREE() on the vector itself.
 *
 * \param auth_objects A vector of auth structures to clean up
 */
#define ast_sip_cleanup_auth_objects_vector(auth_objects) AST_VECTOR_RESET(auth_objects, ao2_cleanup)

/*!
 * \brief Checks if the given content type matches type/subtype.
 *
 * Compares the pjsip_media_type with the passed type and subtype and
 * returns the result of that comparison.  The media type parameters are
 * ignored.
 *
 * \param content_type The pjsip_media_type structure to compare
 * \param type The media type to compare
 * \param subtype The media subtype to compare
 * \retval 0 No match
 * \retval -1 Match
 */
int ast_sip_is_content_type(pjsip_media_type *content_type, char *type, char *subtype);

/*!
 * \brief Send a security event notification for when an invalid endpoint is requested
 *
 * \param name Name of the endpoint requested
 * \param rdata Received message
 */
void ast_sip_report_invalid_endpoint(const char *name, pjsip_rx_data *rdata);

/*!
 * \brief Send a security event notification for when an ACL check fails
 *
 * \param endpoint Pointer to the endpoint in use
 * \param rdata Received message
 * \param name Name of the ACL
 */
void ast_sip_report_failed_acl(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, const char *name);

/*!
 * \brief Send a security event notification for when a challenge response has failed
 *
 * \param endpoint Pointer to the endpoint in use
 * \param rdata Received message
 */
void ast_sip_report_auth_failed_challenge_response(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata);

/*!
 * \brief Send a security event notification for when authentication succeeds
 *
 * \param endpoint Pointer to the endpoint in use
 * \param rdata Received message
 */
void ast_sip_report_auth_success(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata);

/*!
 * \brief Send a security event notification for when an authentication challenge is sent
 *
 * \param endpoint Pointer to the endpoint in use
 * \param rdata Received message
 * \param tdata Sent message
 */
void ast_sip_report_auth_challenge_sent(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, pjsip_tx_data *tdata);

/*!
 * \brief Send a security event notification for when a request is not supported
 *
 * \param endpoint Pointer to the endpoint in use
 * \param rdata Received message
 * \param req_type the type of request
 */
void ast_sip_report_req_no_support(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata,
				   const char* req_type);

/*!
 * \brief Send a security event notification for when a memory limit is hit.
 *
 * \param endpoint Pointer to the endpoint in use
 * \param rdata Received message
 */
void ast_sip_report_mem_limit(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata);

int ast_sip_add_global_request_header(const char *name, const char *value, int replace);
int ast_sip_add_global_response_header(const char *name, const char *value, int replace);

/*!
 * \brief Retrieves the value associated with the given key.
 *
 * \param ht the hash table/dictionary to search
 * \param key the key to find
 *
 * \retval the value associated with the key, NULL otherwise.
 */
void *ast_sip_dict_get(void *ht, const char *key);

/*!
 * \brief Using the dictionary stored in mod_data array at a given id,
 *        retrieve the value associated with the given key.
 *
 * \param mod_data a module data array
 * \param id the mod_data array index
 * \param key the key to find
 *
 * \retval the value associated with the key, NULL otherwise.
 */
#define ast_sip_mod_data_get(mod_data, id, key)		\
	ast_sip_dict_get(mod_data[id], key)

/*!
 * \brief Set the value for the given key.
 *
 * Note - if the hash table does not exist one is created first, the key/value
 * pair is set, and the hash table returned.
 *
 * \param pool the pool to allocate memory in
 * \param ht the hash table/dictionary in which to store the key/value pair
 * \param key the key to associate a value with
 * \param val the value to associate with a key
 *
 * \retval the given, or newly created, hash table.
 */
void *ast_sip_dict_set(pj_pool_t* pool, void *ht,
		       const char *key, void *val);

/*!
 * \brief Utilizing a mod_data array for a given id, set the value
 *        associated with the given key.
 *
 * For a given structure's mod_data array set the element indexed by id to
 * be a dictionary containing the key/val pair.
 *
 * \param pool a memory allocation pool
 * \param mod_data a module data array
 * \param id the mod_data array index
 * \param key the key to find
 * \param val the value to associate with a key
 */
#define ast_sip_mod_data_set(pool, mod_data, id, key, val)		\
	mod_data[id] = ast_sip_dict_set(pool, mod_data[id], key, val)

/*!
 * \brief For every contact on an AOR call the given 'on_contact' handler.
 *
 * \param aor the aor containing a list of contacts to iterate
 * \param on_contact callback on each contact on an AOR.  The object
 * received by the callback will be a ast_sip_contact_wrapper structure.
 * \param arg user data passed to handler
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_for_each_contact(const struct ast_sip_aor *aor,
		ao2_callback_fn on_contact, void *arg);

/*!
 * \brief Handler used to convert a contact to a string.
 *
 * \param object the ast_sip_aor_contact_pair containing a list of contacts to iterate and the contact
 * \param arg user data passed to handler
 * \param flags
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_contact_to_str(void *object, void *arg, int flags);

/*!
 * \brief For every aor in the comma separated aors string call the
 *        given 'on_aor' handler.
 *
 * \param aors a comma separated list of aors
 * \param on_aor callback for each aor
 * \param arg user data passed to handler
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_for_each_aor(const char *aors, ao2_callback_fn on_aor, void *arg);

/*!
 * \brief For every auth in the array call the given 'on_auth' handler.
 *
 * \param array an array of auths
 * \param on_auth callback for each auth
 * \param arg user data passed to handler
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_for_each_auth(const struct ast_sip_auth_vector *array,
			  ao2_callback_fn on_auth, void *arg);

/*!
 * \brief Converts the given auth type to a string
 *
 * \param type the auth type to convert
 * \retval a string representative of the auth type
 */
const char *ast_sip_auth_type_to_str(enum ast_sip_auth_type type);

/*!
 * \brief Converts an auths array to a string of comma separated values
 *
 * \param auths an auth array
 * \param buf the string buffer to write the object data
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_auths_to_str(const struct ast_sip_auth_vector *auths, char **buf);

/*!
 * \brief AMI variable container
 */
struct ast_sip_ami {
	/*! Manager session */
	struct mansession *s;
	/*! Manager message */
	const struct message *m;
	/*! Manager Action ID */
	const char *action_id;
	/*! user specified argument data */
	void *arg;
	/*! count of objects */
	int count;
};

/*!
 * \brief Creates a string to store AMI event data in.
 *
 * \param event the event to set
 * \param ami AMI session and message container
 * \retval an initialized ast_str or NULL on error.
 */
struct ast_str *ast_sip_create_ami_event(const char *event,
					 struct ast_sip_ami *ami);

/*!
 * \brief An entity responsible formatting endpoint information.
 */
struct ast_sip_endpoint_formatter {
	/*!
	 * \brief Callback used to format endpoint information over AMI.
	 */
	int (*format_ami)(const struct ast_sip_endpoint *endpoint,
			  struct ast_sip_ami *ami);
	AST_RWLIST_ENTRY(ast_sip_endpoint_formatter) next;
};

/*!
 * \brief Register an endpoint formatter.
 *
 * \param obj the formatter to register
 */
void ast_sip_register_endpoint_formatter(struct ast_sip_endpoint_formatter *obj);

/*!
 * \brief Unregister an endpoint formatter.
 *
 * \param obj the formatter to unregister
 */
void ast_sip_unregister_endpoint_formatter(struct ast_sip_endpoint_formatter *obj);

/*!
 * \brief Converts a sorcery object to a string of object properties.
 *
 * \param obj the sorcery object to convert
 * \param buf the string buffer to write the object data
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_sorcery_object_to_ami(const void *obj, struct ast_str **buf);

/*!
 * \brief Formats the endpoint and sends over AMI.
 *
 * \param endpoint the endpoint to format and send
 * \param ami AMI variable container
 * \param count the number of formatters operated on
 * \retval 0 Success, otherwise non-zero on error
 */
int ast_sip_format_endpoint_ami(struct ast_sip_endpoint *endpoint,
				struct ast_sip_ami *ami, int *count);

/*!
 * \brief Formats the contact and sends over AMI.
 *
 * \param obj a pointer an ast_sip_contact_wrapper structure
 * \param arg a pointer to an ast_sip_ami structure
 * \param flags ignored
 * \retval 0 Success, otherwise non-zero on error
 */
int ast_sip_format_contact_ami(void *obj, void *arg, int flags);

/*!
 * \brief Format auth details for AMI.
 *
 * \param auths an auth array
 * \param ami ami variable container
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_format_auths_ami(const struct ast_sip_auth_vector *auths,
			     struct ast_sip_ami *ami);

/*!
 * \brief Retrieve the endpoint snapshot for an endpoint
 *
 * \param endpoint The endpoint whose snapshot is to be retrieved.
 * \retval The endpoint snapshot
 */
struct ast_endpoint_snapshot *ast_sip_get_endpoint_snapshot(
	const struct ast_sip_endpoint *endpoint);

/*!
 * \brief Retrieve the device state for an endpoint.
 *
 * \param endpoint The endpoint whose state is to be retrieved.
 * \retval The device state.
 */
const char *ast_sip_get_device_state(const struct ast_sip_endpoint *endpoint);

/*!
 * \brief For every channel snapshot on an endpoint snapshot call the given
 *        'on_channel_snapshot' handler.
 *
 * \param endpoint_snapshot snapshot of an endpoint
 * \param on_channel_snapshot callback for each channel snapshot
 * \param arg user data passed to handler
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_for_each_channel_snapshot(const struct ast_endpoint_snapshot *endpoint_snapshot,
		ao2_callback_fn on_channel_snapshot,
				      void *arg);

/*!
 * \brief For every channel snapshot on an endpoint all the given
 *        'on_channel_snapshot' handler.
 *
 * \param endpoint endpoint
 * \param on_channel_snapshot callback for each channel snapshot
 * \param arg user data passed to handler
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_for_each_channel(const struct ast_sip_endpoint *endpoint,
		ao2_callback_fn on_channel_snapshot,
				      void *arg);

enum ast_sip_supplement_priority {
	/*! Top priority. Supplements with this priority are those that need to run before any others */
	AST_SIP_SUPPLEMENT_PRIORITY_FIRST = 0,
	/*! Channel creation priority.
	 * chan_pjsip creates a channel at this priority. If your supplement depends on being run before
	 * or after channel creation, then set your priority to be lower or higher than this value.
	 */
	AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL = 1000000,
	/*! Lowest priority. Supplements with this priority should be run after all other supplements */
	AST_SIP_SUPPLEMENT_PRIORITY_LAST = INT_MAX,
};

/*!
 * \brief A supplement to SIP message processing
 *
 * These can be registered by any module in order to add
 * processing to incoming and outgoing SIP out of dialog
 * requests and responses
 */
struct ast_sip_supplement {
	/*! Method on which to call the callbacks. If NULL, call on all methods */
	const char *method;
	/*! Priority for this supplement. Lower numbers are visited before higher numbers */
	enum ast_sip_supplement_priority priority;
	/*!
	 * \brief Called on incoming SIP request
	 * This method can indicate a failure in processing in its return. If there
	 * is a failure, it is required that this method sends a response to the request.
	 * This method is always called from a SIP servant thread.
	 *
	 * \note
	 * The following PJSIP methods will not work properly:
	 * pjsip_rdata_get_dlg()
	 * pjsip_rdata_get_tsx()
	 * The reason is that the rdata passed into this function is a cloned rdata structure,
	 * and its module data is not copied during the cloning operation.
	 * If you need to get the dialog, you can get it via session->inv_session->dlg.
	 *
	 * \note
	 * There is no guarantee that a channel will be present on the session when this is called.
	 */
	int (*incoming_request)(struct ast_sip_endpoint *endpoint, struct pjsip_rx_data *rdata);
	/*!
	 * \brief Called on an incoming SIP response
	 * This method is always called from a SIP servant thread.
	 *
	 * \note
	 * The following PJSIP methods will not work properly:
	 * pjsip_rdata_get_dlg()
	 * pjsip_rdata_get_tsx()
	 * The reason is that the rdata passed into this function is a cloned rdata structure,
	 * and its module data is not copied during the cloning operation.
	 * If you need to get the dialog, you can get it via session->inv_session->dlg.
	 *
	 * \note
	 * There is no guarantee that a channel will be present on the session when this is called.
	 */
	void (*incoming_response)(struct ast_sip_endpoint *endpoint, struct pjsip_rx_data *rdata);
	/*!
	 * \brief Called on an outgoing SIP request
	 * This method is always called from a SIP servant thread.
	 */
	void (*outgoing_request)(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, struct pjsip_tx_data *tdata);
	/*!
	 * \brief Called on an outgoing SIP response
	 * This method is always called from a SIP servant thread.
	 */
	void (*outgoing_response)(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, struct pjsip_tx_data *tdata);
	/*! Next item in the list */
	AST_LIST_ENTRY(ast_sip_supplement) next;
};

/*!
 * \brief Register a supplement to SIP out of dialog processing
 *
 * This allows for someone to insert themselves in the processing of out
 * of dialog SIP requests and responses. This, for example could allow for
 * a module to set channel data based on headers in an incoming message.
 * Similarly, a module could reject an incoming request if desired.
 *
 * \param supplement The supplement to register
 */
void ast_sip_register_supplement(struct ast_sip_supplement *supplement);

/*!
 * \brief Unregister a an supplement to SIP out of dialog processing
 *
 * \param supplement The supplement to unregister
 */
void ast_sip_unregister_supplement(struct ast_sip_supplement *supplement);

/*!
 * \brief Retrieve the global MWI taskprocessor high water alert trigger level.
 *
 * \since 13.12.0
 *
 * \retval the system MWI taskprocessor high water alert trigger level
 */
unsigned int ast_sip_get_mwi_tps_queue_high(void);

/*!
 * \brief Retrieve the global MWI taskprocessor low water clear alert level.
 *
 * \since 13.12.0
 *
 * \retval the system MWI taskprocessor low water clear alert level
 */
int ast_sip_get_mwi_tps_queue_low(void);

/*!
 * \brief Retrieve the global setting 'disable sending unsolicited mwi on startup'.
 * \since 13.12.0
 *
 * \retval non zero if disable.
 */
unsigned int ast_sip_get_mwi_disable_initial_unsolicited(void);

/*!
 * \brief Retrieve the global setting 'allow_sending_180_after_183'.
 *
 * \retval non zero if disable.
 */
unsigned int ast_sip_get_allow_sending_180_after_183(void);

/*!
 * \brief Retrieve the global setting 'use_callerid_contact'.
 * \since 13.24.0
 *
 * \retval non zero if CALLERID(num) is to be used as the default username in the contact
 */
unsigned int ast_sip_get_use_callerid_contact(void);

/*!
 * \brief Retrieve the global setting 'norefersub'.
 *
 * \retval non zero if norefersub is to be sent in "Supported" Headers
 */
unsigned int ast_sip_get_norefersub(void);

/*!
 * \brief Retrieve the global setting 'ignore_uri_user_options'.
 * \since 13.12.0
 *
 * \retval non zero if ignore the user field options.
 */
unsigned int ast_sip_get_ignore_uri_user_options(void);

/*!
 * \brief Retrieve the global setting 'send_contact_status_on_update_registration'.
 * \since 16.2.0
 *
 * \retval non zero if need to send AMI ContactStatus event when a contact is updated.
 */
unsigned int ast_sip_get_send_contact_status_on_update_registration(void);


/*!
 * \brief Truncate the URI user field options string if enabled.
 * \since 13.12.0
 *
 * \param str URI user field string to truncate if enabled
 *
 * \details
 * We need to be able to handle URI's looking like
 * "sip:1235557890;phone-context=national@x.x.x.x;user=phone"
 *
 * Where the URI user field is:
 * "1235557890;phone-context=national"
 *
 * When truncated the string will become:
 * "1235557890"
 */
#define AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(str)				\
	do {														\
		char *__semi = strchr((str), ';');						\
		if (__semi && ast_sip_get_ignore_uri_user_options()) {	\
			*__semi = '\0';										\
		}														\
	} while (0)

/*!
 * \brief Retrieve the system debug setting (yes|no|host).
 *
 * \note returned string needs to be de-allocated by caller.
 *
 * \retval the system debug setting.
 */
char *ast_sip_get_debug(void);

/*!
 * \brief Retrieve the global regcontext setting.
 *
 * \since 13.8.0
 *
 * \note returned string needs to be de-allocated by caller.
 *
 * \retval the global regcontext setting
 */
char *ast_sip_get_regcontext(void);

/*!
 * \brief Retrieve the global endpoint_identifier_order setting.
 *
 * Specifies the order by which endpoint identifiers should be regarded.
 *
 * \retval the global endpoint_identifier_order value
 */
char *ast_sip_get_endpoint_identifier_order(void);

/*!
 * \brief Retrieve the default voicemail extension.
 * \since 13.9.0
 *
 * \note returned string needs to be de-allocated by caller.
 *
 * \retval the default voicemail extension
 */
char *ast_sip_get_default_voicemail_extension(void);

/*!
 * \brief Retrieve the global default realm.
 *
 * This is the value placed in outbound challenges' realm if there
 * is no better option (such as an auth-configured realm).
 *
 * \param[out] realm The default realm
 * \param size The buffer size of realm
 */
void ast_sip_get_default_realm(char *realm, size_t size);

/*!
 * \brief Retrieve the global default from user.
 *
 * This is the value placed in outbound requests' From header if there
 * is no better option (such as an endpoint-configured from_user or
 * caller ID number).
 *
 * \param[out] from_user The default from user
 * \param size The buffer size of from_user
 */
void ast_sip_get_default_from_user(char *from_user, size_t size);

/*!
 * \brief Retrieve the system keep alive interval setting.
 *
 * \retval the keep alive interval.
 */
unsigned int ast_sip_get_keep_alive_interval(void);

/*!
 * \brief Retrieve the system contact expiration check interval setting.
 *
 * \retval the contact expiration check interval.
 */
unsigned int ast_sip_get_contact_expiration_check_interval(void);

/*!
 * \brief Retrieve the system setting 'disable multi domain'.
 * \since 13.9.0
 *
 * \retval non zero if disable multi domain.
 */
unsigned int ast_sip_get_disable_multi_domain(void);

/*!
 * \brief Retrieve the system max initial qualify time.
 *
 * \retval the maximum initial qualify time.
 */
unsigned int ast_sip_get_max_initial_qualify_time(void);

/*!
 * \brief translate ast_sip_contact_status_type to character string.
 *
 * \retval the character string equivalent.
 */

const char *ast_sip_get_contact_status_label(const enum ast_sip_contact_status_type status);
const char *ast_sip_get_contact_short_status_label(const enum ast_sip_contact_status_type status);

/*!
 * \brief Set a request to use the next value in the list of resolved addresses.
 *
 * \param tdata the tx data from the original request
 * \retval 0 No more addresses to try
 * \retval 1 The request was successfully re-intialized
 */
int ast_sip_failover_request(pjsip_tx_data *tdata);

/*!
 * \brief Retrieve the local host address in IP form
 *
 * \param af The address family to retrieve
 * \param addr A place to store the local host address
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \since 13.6.0
 */
int ast_sip_get_host_ip(int af, pj_sockaddr *addr);

/*!
 * \brief Retrieve the local host address in string form
 *
 * \param af The address family to retrieve
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 13.6.0
 *
 * \note An empty string may be returned if the address family is valid but no local address exists
 */
const char *ast_sip_get_host_ip_string(int af);

/*!
 * \brief Return the size of the SIP threadpool's task queue
 * \since 13.7.0
 */
long ast_sip_threadpool_queue_size(void);

/*!
 * \brief Retrieve the SIP threadpool object
 */
struct ast_threadpool *ast_sip_threadpool(void);

/*!
 * \brief Retrieve transport state
 * \since 13.7.1
 *
 * \param transport_id
 * \retval transport_state
 *
 * \note ao2_cleanup(...) or ao2_ref(...,  -1) must be called on the returned object
 */
struct ast_sip_transport_state *ast_sip_get_transport_state(const char *transport_id);

/*!
 * \brief Return the SIP URI of the Contact header
 * 
 * \param tdata
 * \retval Pointer to SIP URI of Contact
 * \retval NULL if Contact header not found or not a SIP(S) URI
 *
 * \note Do not free the returned object.
 */
pjsip_sip_uri *ast_sip_get_contact_sip_uri(pjsip_tx_data *tdata);

/*!
 * \brief Returns the transport state currently in use based on request transport details
 *
 * \param details
 * \retval transport_state
 *
 * \note ao2_cleanup(...) or ao2_ref(...,  -1) must be called on the returned object
 */
struct ast_sip_transport_state *ast_sip_find_transport_state_in_use(struct ast_sip_request_transport_details *details);

/*!
 * \brief Sets request transport details based on tdata
 *
 * \param details pre-allocated request transport details to set
 * \param tdata
 * \param use_ipv6 if non-zero, ipv6 transports will be considered
 * \retval 0 success
 * \retval -1 failure
 */
int ast_sip_set_request_transport_details(struct ast_sip_request_transport_details *details, pjsip_tx_data *tdata, int use_ipv6);

/*!
 * \brief Replace domain and port of SIP URI to point to (external) signaling address of this Asterisk instance
 *
 * \param uri
 * \param tdata
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note Uses domain and port in Contact header if it exists, otherwise the local URI of the dialog is used if the
 *       message is sent within the context of a dialog. Further, NAT settings are considered - i.e. if the target
 *       is not in the localnet, the external_signaling_address and port are used.
 */
int ast_sip_rewrite_uri_to_local(pjsip_sip_uri *uri, pjsip_tx_data *tdata);

/*!
 * \brief Retrieves all transport states
 * \since 13.7.1
 *
 * \retval ao2_container
 *
 * \note ao2_cleanup(...) or ao2_ref(...,  -1) must be called on the returned object
 */
struct ao2_container *ast_sip_get_transport_states(void);

/*!
 * \brief Sets pjsip_tpselector from ast_sip_transport
 * \since 13.8.0
 *
 * \param transport The transport to be used
 * \param selector The selector to be populated
 * \retval 0 success
 * \retval -1 failure
 *
 * \note The transport selector must be unreffed using ast_sip_tpselector_unref
 */
int ast_sip_set_tpselector_from_transport(const struct ast_sip_transport *transport, pjsip_tpselector *selector);

/*!
 * \brief Sets pjsip_tpselector from ast_sip_transport
 * \since 13.8.0
 *
 * \param transport_name The name of the transport to be used
 * \param selector The selector to be populated
 * \retval 0 success
 * \retval -1 failure
 *
 * \note The transport selector must be unreffed using ast_sip_tpselector_unref
 */
int ast_sip_set_tpselector_from_transport_name(const char *transport_name, pjsip_tpselector *selector);

/*!
 * \brief Unreference a pjsip_tpselector
 * \since 17.0.0
 *
 * \param selector The selector to be unreffed
 */
void ast_sip_tpselector_unref(pjsip_tpselector *selector);

/*!
 * \brief Sets the PJSIP transport on a child transport
 * \since 17.0.0
 *
 * \param transport_name The name of the transport to be updated
 * \param transport The PJSIP transport
 * \retval 0 success
 * \retval -1 failure
 */
int ast_sip_transport_state_set_transport(const char *transport_name, pjsip_transport *transport);

/*!
 * \brief Sets the P-Preferred-Identity on a child transport
 * \since 17.0.0
 *
 * \param transport_name The name of the transport to be set on
 * \param identity The P-Preferred-Identity to use on requests on this transport
 * \retval 0 success
 * \retval -1 failure
 */
int ast_sip_transport_state_set_preferred_identity(const char *transport_name, const char *identity);

/*!
 * \brief Sets the service routes on a child transport
 * \since 17.0.0
 *
 * \param transport_name The name of the transport to be set on
 * \param service_routes A vector of service routes
 * \retval 0 success
 * \retval -1 failure
 *
 * \note This assumes ownership of the service routes in both success and failure scenarios
 */
int ast_sip_transport_state_set_service_routes(const char *transport_name, struct ast_sip_service_route_vector *service_routes);

/*!
 * \brief Apply the configuration for a transport to an outgoing message
 * \since 17.0.0
 *
 * \param transport_name The name of the transport to apply configuration from
 * \param tdata The SIP message
 */
void ast_sip_message_apply_transport(const char *transport_name, pjsip_tx_data *tdata);

/*!
 * \brief Allocate a vector of service routes
 * \since 17.0.0
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
struct ast_sip_service_route_vector *ast_sip_service_route_vector_alloc(void);

/*!
 * \brief Destroy a vector of service routes
 * \since 17.0.0
 *
 * \param service_routes A vector of service routes
 */
void ast_sip_service_route_vector_destroy(struct ast_sip_service_route_vector *service_routes);

/*!
 * \brief Set the ID for a connected line update
 *
 * \retval -1 on failure, 0 on success
 */
int ast_sip_set_id_connected_line(struct pjsip_rx_data *rdata, struct ast_party_id *id);

/*!
 * \brief Set the ID from an INVITE
 *
 * \param rdata
 * \param id ID structure to fill
 * \param default_id Default ID structure with data to use (for non-trusted endpoints)
 * \param trust_inbound Whether or not the endpoint is trusted (controls whether PAI or RPID can be used)
 *
 * \retval -1 on failure, 0 on success
 */
int ast_sip_set_id_from_invite(struct pjsip_rx_data *rdata, struct ast_party_id *id, struct ast_party_id *default_id, int trust_inbound);

/*!
 * \brief Set name and number information on an identity header.
 *
 * \param pool Memory pool to use for string duplication
 * \param id_hdr A From, P-Asserted-Identity, or Remote-Party-ID header to modify
 * \param id The identity information to apply to the header
 */
void ast_sip_modify_id_header(pj_pool_t *pool, pjsip_fromto_hdr *id_hdr,
	const struct ast_party_id *id);

/*!
 * \brief Retrieves an endpoint and URI from the "to" string.
 *
 * This URI is used as the Request URI.
 *
 * Expects the given 'to' to be in one of the following formats:
 * Why we allow so many is a mystery.
 *
 * Basic:
 *
 *      endpoint        : We'll get URI from the default aor/contact
 *      endpoint/aor    : We'll get the URI from the specific aor/contact
 *      endpoint@domain : We toss the domain part and just use the endpoint
 *
 *   These all use the endpoint and specified URI:
 * \verbatim
        endpoint/<sip[s]:host>
        endpoint/<sip[s]:user@host>
        endpoint/"Bob" <sip[s]:host>
        endpoint/"Bob" <sip[s]:user@host>
        endpoint/sip[s]:host
        endpoint/sip[s]:user@host
        endpoint/host
        endpoint/user@host
   \endverbatim
 *
 *   These all use the default endpoint and specified URI:
 * \verbatim
        <sip[s]:host>
        <sip[s]:user@host>
        "Bob" <sip[s]:host>
        "Bob" <sip[s]:user@host>
        sip[s]:host
        sip[s]:user@host
   \endverbatim
 *
 *   These use the default endpoint and specified host:
 * \verbatim
        host
        user@host
   \endverbatim
 *
 *   This form is similar to a dialstring:
 * \verbatim
        PJSIP/user@endpoint
   \endverbatim
 *
 *   In this case, the user will be added to the endpoint contact's URI.
 *   If the contact URI already has a user, it will be replaced.
 *
 * The ones that have the sip[s] scheme are the easiest to parse.
 * The rest all have some issue.
 *
 *      endpoint vs host              : We have to test for endpoint first
 *      endpoint/aor vs endpoint/host : We have to test for aor first
 *                                      What if there's an aor with the same
 *                                      name as the host?
 *      endpoint@domain vs user@host  : We have to test for endpoint first.
 *                                      What if there's an endpoint with the
 *                                      same name as the user?
 *
 * \param to 'To' field with possible endpoint
 * \param get_default_outbound If nonzero, try to retrieve the default
 * 			       outbound endpoint if no endpoint was found.
 * 			       Otherwise, return NULL if no endpoint was found.
 * \param uri Pointer to a char* which will be set to the URI.
 *            Always must be ast_free'd by the caller - even if the return value is NULL!
 *
 * \note The logic below could probably be condensed but then it wouldn't be
 * as clear.
 */
struct ast_sip_endpoint *ast_sip_get_endpoint(const char *to, int get_default_outbound, char **uri);

/*!
 * \brief Replace the To URI in the tdata with the supplied one
 *
 * \param tdata the outbound message data structure
 * \param to URI to replace the To URI with. Must be a valid SIP URI.
 *
 * \retval 0: success, -1: failure
 */
int ast_sip_update_to_uri(pjsip_tx_data *tdata, const char *to);

/*!
 * \brief Overwrite fields in the outbound 'From' header
 *
 * The outbound 'From' header is created/added in ast_sip_create_request with
 * default data.  If available that data may be info specified in the 'from_user'
 * and 'from_domain' options found on the endpoint.  That information will be
 * overwritten with data in the given 'from' parameter.
 *
 * \param tdata the outbound message data structure
 * \param from info to copy into the header.
 *		  Can be either a SIP URI, or in the format user[@domain]
 *
 * \retval 0: success, -1: failure
 */
int ast_sip_update_from(pjsip_tx_data *tdata, char *from);

/*!
 * \brief Retrieve the unidentified request security event thresholds
 * \since 13.8.0
 *
 * \param count The maximum number of unidentified requests per source ip to accumulate before emitting a security event
 * \param period The period in seconds over which to accumulate unidentified requests
 * \param prune_interval The interval in seconds at which expired entries will be pruned
 */
void ast_sip_get_unidentified_request_thresholds(unsigned int *count, unsigned int *period,
	unsigned int *prune_interval);

/*!
 * \brief Get the transport name from an endpoint or request uri
 * \since 13.15.0
 *
 * \param endpoint
 * \param sip_uri
 * \param buf Buffer to receive transport name
 * \param buf_len Buffer length
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \note
 * If endpoint->transport is not NULL, it is returned in buf.
 * Otherwise if sip_uri has an 'x-ast-txp' parameter AND the sip_uri host is
 * an ip4 or ip6 address, its value is returned,
 */
int ast_sip_get_transport_name(const struct ast_sip_endpoint *endpoint,
	pjsip_sip_uri *sip_uri, char *buf, size_t buf_len);

/*!
 * \brief Sets pjsip_tpselector from an endpoint or uri
 * \since 13.15.0
 *
 * \param endpoint If endpoint->transport is set, it's used
 * \param sip_uri If sip_uri contains a x-ast-txp parameter, it's used
 * \param selector The selector to be populated
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_sip_set_tpselector_from_ep_or_uri(const struct ast_sip_endpoint *endpoint,
	pjsip_sip_uri *sip_uri, pjsip_tpselector *selector);

/*!
 * \brief Set the transport on a dialog
 * \since 13.15.0
 *
 * \param endpoint
 * \param dlg
 * \param selector (optional)
 *
 * \note
 * This API calls ast_sip_get_transport_name(endpoint, dlg->target) and if the result is
 * non-NULL, calls pjsip_dlg_set_transport.  If 'selector' is non-NULL, it is updated with
 * the selector used.
 *
 * \note
 * It is the responsibility of the caller to unref the passed in selector if one is provided.
 */
int ast_sip_dlg_set_transport(const struct ast_sip_endpoint *endpoint, pjsip_dialog *dlg,
	pjsip_tpselector *selector);

/*!
 * \brief Convert the DTMF mode enum value into a string
 * \since 13.18.0
 *
 * \param dtmf the dtmf mode
 * \param buf Buffer to receive dtmf mode string
 * \param buf_len Buffer length
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 */
int ast_sip_dtmf_to_str(const enum ast_sip_dtmf_mode dtmf,
	char *buf, size_t buf_len);

/*!
 * \brief Convert the DTMF mode name into an enum
 * \since 13.18.0
 *
 * \param dtmf_mode dtmf mode as a string
 *
 * \retval  >= 0 The enum value
 * \retval -1 Failure
 *
 */
int ast_sip_str_to_dtmf(const char *dtmf_mode);

/*!
 * \brief Convert the call codec preference flags to a string
 * \since 18.0.0
 *
 * \param pref the call codec preference setting
 *
 * \returns a constant string with either the setting value or 'unknown'
 * \note Don't try to free the string!
 *
 */
const char *ast_sip_call_codec_pref_to_str(struct ast_flags pref);

/*!
 * \brief Convert a call codec preference string to preference flags
 * \since 18.0.0
 *
 * \param pref A pointer to an ast_flags structure to receive the preference flags
 * \param pref_str The call codec preference setting string
 * \param is_outgoing Is for outgoing calls?
 *
 * \retval 0 The string was parsed successfully
 * \retval -1 The string option was invalid
 */
int ast_sip_call_codec_str_to_pref(struct ast_flags *pref, const char *pref_str, int is_outgoing);

/*!
 * \brief Transport shutdown monitor callback.
 * \since 13.18.0
 *
 * \param data User data to know what to do when transport shuts down.
 *
 * \note The callback does not need to care that data is an ao2 object.
 */
typedef void (*ast_transport_monitor_shutdown_cb)(void *data);

/*!
 * \brief Transport shutdown monitor data matcher
 * \since 13.20.0
 *
 * \param a User data to compare.
 * \param b User data to compare.
 *
 * \retval 1 The data objects match
 * \retval 0 The data objects don't match
 */
typedef int (*ast_transport_monitor_data_matcher)(void *a, void *b);

enum ast_transport_monitor_reg {
	/*! \brief Successfully registered the transport monitor */
	AST_TRANSPORT_MONITOR_REG_SUCCESS,
	/*! \brief Replaced the already existing transport monitor with new one. */
	AST_TRANSPORT_MONITOR_REG_REPLACED,
	/*!
	 * \brief Transport not found to monitor.
	 * \note Transport is either already shutdown or is not reliable.
	 */
	AST_TRANSPORT_MONITOR_REG_NOT_FOUND,
	/*! \brief Error while registering transport monitor. */
	AST_TRANSPORT_MONITOR_REG_FAILED,
};

/*!
 * \brief Register a reliable transport shutdown monitor callback.
 * \deprecated Replaced with ast_sip_transport_monitor_register_key().
 * \since 13.20.0
 *
 * \param transport Transport to monitor for shutdown.
 * \param cb Who to call when transport is shutdown.
 * \param ao2_data Data to pass with the callback.
 *
 * \note The data object passed will have its reference count automatically
 * incremented by this call and automatically decremented after the callback
 * runs or when the callback is unregistered.
 *
 * There is no checking for duplicate registrations.
 *
 * \return enum ast_transport_monitor_reg
 */
enum ast_transport_monitor_reg ast_sip_transport_monitor_register(pjsip_transport *transport,
	ast_transport_monitor_shutdown_cb cb, void *ao2_data);

/*!
 * \brief Register a reliable transport shutdown monitor callback.
 *
 * \param transport_key Key for the transport to monitor for shutdown.
 *                      Create the key with AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR.
 * \param cb Who to call when transport is shutdown.
 * \param ao2_data Data to pass with the callback.
 *
 * \note The data object passed will have its reference count automatically
 * incremented by this call and automatically decremented after the callback
 * runs or when the callback is unregistered.
 *
 * There is no checking for duplicate registrations.
 *
 * \return enum ast_transport_monitor_reg
 */
enum ast_transport_monitor_reg ast_sip_transport_monitor_register_key(
	const char *transport_key, ast_transport_monitor_shutdown_cb cb,
	void *ao2_data);

/*!
 * \brief Register a reliable transport shutdown monitor callback replacing any duplicate.
 * \deprecated Replaced with ast_sip_transport_monitor_register_replace_key().
 * \since 13.26.0
 * \since 16.3.0
 *
 * \param transport Transport to monitor for shutdown.
 * \param cb Who to call when transport is shutdown.
 * \param ao2_data Data to pass with the callback.
 * \param matches Matcher function that returns true if data matches a previously
 *                registered data object
 *
 * \note The data object passed will have its reference count automatically
 * incremented by this call and automatically decremented after the callback
 * runs or when the callback is unregistered.
 *
 * This function checks for duplicates, and overwrites/replaces the old monitor
 * with the given one.
 *
 * \return enum ast_transport_monitor_reg
 */
enum ast_transport_monitor_reg ast_sip_transport_monitor_register_replace(pjsip_transport *transport,
	ast_transport_monitor_shutdown_cb cb, void *ao2_data, ast_transport_monitor_data_matcher matches);

/*!
 * \brief Register a reliable transport shutdown monitor callback replacing any duplicate.
 *
 * \param transport_key Key for the transport to monitor for shutdown.
 *                      Create the key with AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR.
 * \param cb Who to call when transport is shutdown.
 * \param ao2_data Data to pass with the callback.
 * \param matches Matcher function that returns true if data matches a previously
 *                registered data object
 *
 * \note The data object passed will have its reference count automatically
 * incremented by this call and automatically decremented after the callback
 * runs or when the callback is unregistered.
 *
 * This function checks for duplicates, and overwrites/replaces the old monitor
 * with the given one.
 *
 * \return enum ast_transport_monitor_reg
 */
enum ast_transport_monitor_reg ast_sip_transport_monitor_register_replace_key(
	const char *transport_key, ast_transport_monitor_shutdown_cb cb,
	void *ao2_data, ast_transport_monitor_data_matcher matches);

/*!
 * \brief Unregister a reliable transport shutdown monitor
 * \deprecated Replaced with ast_sip_transport_monitor_unregister_key().
 * \since 13.20.0
 *
 * \param transport Transport to monitor for shutdown.
 * \param cb The callback that was used for the original register.
 * \param data Data to pass to the matcher. May be NULL and does NOT need to be an ao2 object.
 *             If NULL, all monitors with the provided callback are unregistered.
 * \param matches Matcher function that returns true if data matches the previously
 *                registered data object.  If NULL, a simple pointer comparison is done.
 *
 * \note The data object passed into the original register will have its reference count
 * automatically decremented.
 */
void ast_sip_transport_monitor_unregister(pjsip_transport *transport,
	ast_transport_monitor_shutdown_cb cb, void *data, ast_transport_monitor_data_matcher matches);

/*!
 * \brief Unregister a reliable transport shutdown monitor
 *
 * \param transport_key Key for the transport to monitor for shutdown.
 *                      Create the key with AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR.
 * \param cb The callback that was used for the original register.
 * \param data Data to pass to the matcher. May be NULL and does NOT need to be an ao2 object.
 *             If NULL, all monitors with the provided callback are unregistered.
 * \param matches Matcher function that returns true if data matches the previously
 *                registered data object.  If NULL, a simple pointer comparison is done.
 *
 * \note The data object passed into the original register will have its reference count
 * automatically decremented.
 */
void ast_sip_transport_monitor_unregister_key(const char *transport_key,
	ast_transport_monitor_shutdown_cb cb, void *data, ast_transport_monitor_data_matcher matches);

/*!
 * \brief Unregister a transport shutdown monitor from all reliable transports
 * \since 13.20.0
 *
 * \param cb The callback that was used for the original register.
 * \param data Data to pass to the matcher. May be NULL and does NOT need to be an ao2 object.
 *             If NULL, all monitors with the provided callback are unregistered.
 * \param matches Matcher function that returns true if ao2_data matches the previously
 *                registered data object.  If NULL, a simple pointer comparison is done.
 *
 * \note The data object passed into the original register will have its reference count
 * automatically decremented.
 */
void ast_sip_transport_monitor_unregister_all(ast_transport_monitor_shutdown_cb cb,
	void *data, ast_transport_monitor_data_matcher matches);

/*! Transport state notification registration element.  */
struct ast_sip_tpmgr_state_callback {
	/*! PJPROJECT transport state notification callback */
	pjsip_tp_state_callback cb;
	AST_LIST_ENTRY(ast_sip_tpmgr_state_callback) node;
};

/*!
 * \brief Register a transport state notification callback element.
 * \since 13.18.0
 *
 * \param element What we are registering.
 */
void ast_sip_transport_state_register(struct ast_sip_tpmgr_state_callback *element);

/*!
 * \brief Unregister a transport state notification callback element.
 * \since 13.18.0
 *
 * \param element What we are unregistering.
 */
void ast_sip_transport_state_unregister(struct ast_sip_tpmgr_state_callback *element);

/*!
 * \brief Check whether a pjsip_uri is SIP/SIPS or not
 * \since 16.28.0
 *
 * \param uri The pjsip_uri to check
 *
 * \retval 1 if true
 * \retval 0 if false
 */
int ast_sip_is_uri_sip_sips(pjsip_uri *uri);

/*!
 * \brief Check whether a pjsip_uri is allowed or not
 * \since 16.28.0
 *
 * \param uri The pjsip_uri to check
 *
 * \retval 1 if allowed
 * \retval 0 if not allowed
 */
int ast_sip_is_allowed_uri(pjsip_uri *uri);

/*!
 * \brief Get the user portion of the pjsip_uri
 * \since 16.28.0
 *
 * \param uri The pjsip_uri to get the user from
 *
 * \note This function will check what kind of URI it receives and return
 * the user based off of that
 *
 * \return User string or empty string if not present
 */
const pj_str_t *ast_sip_pjsip_uri_get_username(pjsip_uri *uri);

/*!
 * \brief Get the host portion of the pjsip_uri
 * \since 16.28.0
 *
 * \param uri The pjsip_uri to get the host from
 *
 * \note This function will check what kind of URI it receives and return
 * the host based off of that
 *
 * \return Host string or empty string if not present
 */
const pj_str_t *ast_sip_pjsip_uri_get_hostname(pjsip_uri *uri);

/*!
 * \brief Find an 'other' SIP/SIPS URI parameter by name
 * \since 16.28.0
 *
 * A convenience function to find a named parameter from a SIP/SIPS URI. This
 * function will not find the following standard SIP/SIPS URI parameters which
 * are stored separately by PJSIP:
 *
 * \li `user`
 * \li `method`
 * \li `transport`
 * \li `ttl`
 * \li `lr`
 * \li `maddr`
 *
 * \param uri The pjsip_uri to get the parameter from
 * \param param_str The name of the parameter to find
 *
 * \note This function will check what kind of URI it receives and return
 * the parameter based off of that
 *
 * \return Find parameter or NULL if not present
 */
struct pjsip_param *ast_sip_pjsip_uri_get_other_param(pjsip_uri *uri, const pj_str_t *param_str);

/*!
 * \brief Retrieve the system setting 'all_codecs_on_empty_reinvite'.
 *
 * \retval non zero if we should return all codecs on empty re-INVITE
 */
unsigned int ast_sip_get_all_codecs_on_empty_reinvite(void);


/*!
 * \brief Convert SIP hangup causes to Asterisk hangup causes
 *
 * \param cause SIP cause
 *
 * \retval matched cause code from causes.h
 */
const int ast_sip_hangup_sip2cause(int cause);

/*!
 * \brief Convert name to SIP response code
 *
 * \param name SIP response code name matching one of the
 *             enum names defined in "enum pjsip_status_code"
 *             defined in sip_msg.h.  May be specified with or
 *             without the PJSIP_SC_ prefix.
 *
 * \retval SIP response code
 * \retval -1 if matching code not found
 */
int ast_sip_str2rc(const char *name);

#endif /* _RES_PJSIP_H */
