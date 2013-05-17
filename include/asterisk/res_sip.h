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

#ifndef _RES_SIP_H
#define _RES_SIP_H

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
/* Needed for pj_sockaddr */
#include <pjlib.h>

/* Forward declarations of PJSIP stuff */
struct pjsip_rx_data;
struct pjsip_module;
struct pjsip_tx_data;
struct pjsip_dialog;
struct pjsip_transport;
struct pjsip_tpfactory;
struct pjsip_tls_setting;
struct pjsip_tpselector;

/*!
 * \brief Structure for SIP transport information
 */
struct ast_sip_transport_state {
	/*! \brief Transport itself */
	struct pjsip_transport *transport;

	/*! \brief Transport factory */
	struct pjsip_tpfactory *factory;
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

/*! \brief Maximum number of ciphers supported for a TLS transport */
#define SIP_TLS_MAX_CIPHERS 64

/*
 * \brief Transport to bind to
 */
struct ast_sip_transport {
	/*! Sorcery object details */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Certificate of authority list file */
		AST_STRING_FIELD(ca_list_file);
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
	/*! Address and port to bind to */
	pj_sockaddr host;
	/*! Number of simultaneous asynchronous operations */
	unsigned int async_operations;
	/*! Optional external port for signaling */
	unsigned int external_signaling_port;
	/*! TLS settings */
	pjsip_tls_setting tls;
	/*! Configured TLS ciphers */
	pj_ssl_cipher ciphers[SIP_TLS_MAX_CIPHERS];
	/*! Optional local network information, used for NAT purposes */
	struct ast_ha *localnet;
	/*! DNS manager for refreshing the external address */
	struct ast_dnsmgr_entry *external_address_refresher;
	/*! Optional external address information */
	struct ast_sockaddr external_address;
	/*! Transport state information */
	struct ast_sip_transport_state *state;
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

/*!
 * \brief Contact associated with an address of record
 */
struct ast_sip_contact {
	/*! Sorcery object details, the id is the aor name plus a random string */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Full URI of the contact */
		AST_STRING_FIELD(uri);
	);
	/*! Absolute time that this contact is no longer valid after */
	struct timeval expiration_time;
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
	);
	/*! Minimum expiration time */
	unsigned int minimum_expiration;
	/*! Maximum expiration time */
	unsigned int maximum_expiration;
	/*! Default contact expiration if one is not provided in the contact */
	unsigned int default_expiration;
	/*! Maximum number of external contacts, 0 to disable */
	unsigned int max_contacts;
	/*! Whether to remove any existing contacts not related to an incoming REGISTER when it comes in */
	unsigned int remove_existing;
	/*! Any permanent configured contacts */
	struct ao2_container *permanent_contacts;
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
};

#define SIP_SORCERY_AUTH_TYPE "auth"

struct ast_sip_auth {
	/* Sorcery ID of the auth is its name */
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/* Identification for these credentials */
		AST_STRING_FIELD(realm);
		/* Authentication username */
		AST_STRING_FIELD(auth_user);
		/* Authentication password */
		AST_STRING_FIELD(auth_pass);
		/* Authentication credentials in MD5 format (hash of user:realm:pass) */
		AST_STRING_FIELD(md5_creds);
	);
	/* The time period (in seconds) that a nonce may be reused */
	unsigned int nonce_lifetime;
	/* Used to determine what to use when authenticating */
	enum ast_sip_auth_type type;
};

/*!
 * \brief Different methods by which incoming requests can be matched to endpoints
 */
enum ast_sip_endpoint_identifier_type {
	/*! Identify based on user name in From header */
	AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME = (1 << 0),
	/*! Identify based on source location of the SIP message */
	AST_SIP_ENDPOINT_IDENTIFY_BY_LOCATION = (1 << 1),
};

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
		/*! Optional external media address to use in SDP */
		AST_STRING_FIELD(external_media_address);
		/*! Configured voicemail boxes for this endpoint. Used for MWI */
		AST_STRING_FIELD(mailboxes);
	);
	/*! Identification information for this endpoint */
	struct ast_party_id id;
	/*! Domain to which this endpoint belongs */
	struct ast_sip_domain *domain;
	/*! Address of record for incoming registrations */
	struct ast_sip_aor *aor;
	/*! Codec preferences */
	struct ast_codec_pref prefs;
	/*! Configured codecs */
	struct ast_format_cap *codecs;
	/*! Names of inbound authentication credentials */
	const char **sip_inbound_auths;
	/*! Number of configured auths */
	size_t num_inbound_auths;
	/*! Names of outbound authentication credentials */
	const char **sip_outbound_auths;
	/*! Number of configured outbound auths */
	size_t num_outbound_auths;
	/*! DTMF mode to use with this endpoint */
	enum ast_sip_dtmf_mode dtmf;
	/*! Whether IPv6 RTP is enabled or not */
	unsigned int rtp_ipv6;
	/*! Whether symmetric RTP is enabled or not */
	unsigned int rtp_symmetric;
	/*! Whether ICE support is enabled or not */
	unsigned int ice_support;
	/*! Whether to use the "ptime" attribute received from the endpoint or not */
	unsigned int use_ptime;
	/*! Whether to force using the source IP address/port for sending responses */
	unsigned int force_rport;
	/*! Whether to rewrite the Contact header with the source IP address/port or not */
	unsigned int rewrite_contact;
	/*! Enabled SIP extensions */
	unsigned int extensions;
	/*! Minimum session expiration period, in seconds */
	unsigned int min_se;
	/*! Session expiration period, in seconds */
	unsigned int sess_expires;
	/*! List of outbound registrations */
	AST_LIST_HEAD_NOLOCK(, ast_sip_registration) registrations;
	/*! Frequency to send OPTIONS requests to endpoint. 0 is disabled. */
	unsigned int qualify_frequency;
	/*! Method(s) by which the endpoint should be identified. */
	enum ast_sip_endpoint_identifier_type ident_method;
	/*! Boolean indicating if direct_media is permissible */
	unsigned int direct_media;
	/*! When using direct media, which method should be used */
	enum ast_sip_session_refresh_method direct_media_method;
	/*! Take steps to mitigate glare for direct media */
	enum ast_sip_direct_media_glare_mitigation direct_media_glare_mitigation;
	/*! Do not attempt direct media session refreshes if a media NAT is detected */
	unsigned int disable_direct_media_on_nat;
	/*! Do we trust the endpoint with our outbound identity? */
	unsigned int trust_id_outbound;
	/*! Do we trust identity information that originates externally (e.g. P-Asserted-Identity header)? */
	unsigned int trust_id_inbound;
	/*! Do we send P-Asserted-Identity headers to this endpoint? */
	unsigned int send_pai;
	/*! Do we send Remote-Party-ID headers to this endpoint? */
	unsigned int send_rpid;
	/*! Should unsolicited MWI be aggregated into a single NOTIFY? */
	unsigned int aggregate_mwi;
};

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
	 * \param auths An array of IDs of auth sorcery objects
	 * \param num_auths The number of IDs in the array
	 * \param challenge The SIP response with authentication challenge(s)
	 * \param tsx The transaction in which the challenge was received
	 * \param new_request The new SIP request with challenge response(s)
	 * \retval 0 Successfully created new request
	 * \retval -1 Failed to create a new request
	 */
	int (*create_request_with_auth)(const char **auths, size_t num_auths, struct pjsip_rx_data *challenge,
			struct pjsip_transaction *tsx, struct pjsip_tx_data **new_request);
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
 * \param auth The authenticator to register
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
 * \brief Initialize transport support on a sorcery instance
 *
 * \param sorcery The sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_transport(struct ast_sorcery *sorcery);

/*!
 * \brief Initialize location support on a sorcery instance
 *
 * \param sorcery The sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_location(struct ast_sorcery *sorcery);

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
 * \brief Retrieve all contacts currently available for an AOR
 *
 * \param aor Pointer to the AOR
 *
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 */
struct ao2_container *ast_sip_location_retrieve_aor_contacts(const struct ast_sip_aor *aor);

/*!
 * \brief Retrieve the first bound contact from a list of AORs
 *
 * \param aor_list A comma-separated list of AOR names
 * \retval NULL if no contacts available
 * \retval non-NULL if contacts available
 */
struct ast_sip_contact *ast_sip_location_retrieve_contact_from_aor_list(const char *aor_list);

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
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_location_add_contact(struct ast_sip_aor *aor, const char *uri, struct timeval expiration_time);

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
 * \brief Initialize domain aliases support on a sorcery instance
 *
 * \param sorcery The sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_domain_alias(struct ast_sorcery *sorcery);

/*!
 * \brief Initialize authentication support on a sorcery instance
 *
 * \param sorcery The sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_auth(struct ast_sorcery *sorcery);

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
 * \brief Initialize the distributor module
 *
 * The distributor module is responsible for taking an incoming
 * SIP message and placing it into the threadpool. Once in the threadpool,
 * the distributor will perform endpoint lookups and authentication, and
 * then distribute the message up the stack to any further modules.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_sip_initialize_distributor(void);

/*!
 * \page Threading model for SIP
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
 * for other PSJIP events, such as transaction state changes due to timer
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
 * \note
 *
 * Do not make assumptions about individual threads based on a corresponding serializer.
 * In other words, just because several tasks use the same serializer when being pushed
 * to servants, it does not mean that the same thread is necessarily going to execute those
 * tasks, even though they are all guaranteed to be executed in sequence.
 */

/*!
 * \brief Create a new serializer for SIP tasks
 *
 * See \ref ast_threadpool_serializer for more information on serializers.
 * SIP creates serializers so that tasks operating on similar data will run
 * in sequence.
 *
 * \retval NULL Failure
 * \retval non-NULL Newly-created serializer
 */
struct ast_taskprocessor *ast_sip_create_serializer(void);

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
 * \brief Push a task to SIP servants and wait for it to complete
 *
 * Like \ref ast_sip_push_task except that it blocks until the task completes.
 *
 * \warning \b Never use this function in a SIP servant thread. This can potentially
 * cause a deadlock. If you are in a SIP servant thread, just call your function
 * in-line.
 *
 * \param serializer The SIP serializer to which the task belongs. May be NULL.
 * \param sip_task The task to execute
 * \param task_data The parameter to pass to the task when it executes
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_push_task_synchronous(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data);

/*!
 * \brief Determine if the current thread is a SIP servant thread
 *
 * \retval 0 This is not a SIP servant thread
 * \retval 1 This is a SIP servant thread
 */
int ast_sip_thread_is_servant(void);

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
 * \brief General purpose method for creating a dialog with an endpoint
 *
 * \param endpoint A pointer to the endpoint
 * \param aor_name Optional name of the AOR to target, may even be an explicit SIP URI
 * \param request_user Optional user to place into the target URI
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
 pjsip_dialog *ast_sip_create_dialog(const struct ast_sip_endpoint *endpoint, const char *aor_name, const char *request_user);

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
 * \param endpoint Optional. If specified, the request will be created out-of-dialog
 * to the endpoint.
 * \param uri Optional. If specified, the request will be sent to this URI rather
 * than one configured for the endpoint.
 * \param[out] tdata The newly-created request
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_create_request(const char *method, struct pjsip_dialog *dlg,
		struct ast_sip_endpoint *endpoint, const char *uri, pjsip_tx_data **tdata);

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
 * \param dlg Optional. If specified, the dialog on which the request should be sent
 * \param endpoint Optional. If specified, the request is sent out-of-dialog to the endpoint.
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_send_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg, struct ast_sip_endpoint *endpoint);

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
int ast_sip_create_request_with_auth(const char **auths, size_t num_auths, pjsip_rx_data *challenge,
		pjsip_transaction *tsx, pjsip_tx_data **new_request);

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
 * This will treat each part of the input array as part of a multipart body and
 * add each part to the SIP message.
 *
 * \param tdata The message to add the body to
 * \param bodies The parts of the body to add
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
 * \param body The string to append to the end of the current body
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
void ast_copy_pj_str(char *dest, pj_str_t *src, size_t size);

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
 * \brief Retrieve relevant SIP auth structures from sorcery
 *
 * \param auth_names The sorcery IDs of auths to retrieve
 * \param num_auths The number of auths to retrieve
 * \param[out] out The retrieved auths are stored here
 */
int ast_sip_retrieve_auths(const char *auth_names[], size_t num_auths, struct ast_sip_auth **out);

/*!
 * \brief Clean up retrieved auth structures from memory
 *
 * Call this function once you have completed operating on auths
 * retrieved from \ref ast_sip_retrieve_auths
 *
 * \param auths An array of auth structures to clean up
 * \param num_auths The number of auths in the array
 */
void ast_sip_cleanup_auths(struct ast_sip_auth *auths[], size_t num_auths);

#endif /* _RES_SIP_H */
