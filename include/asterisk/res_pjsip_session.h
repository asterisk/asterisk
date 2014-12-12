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

#ifndef _RES_PJSIP_SESSION_H
#define _RES_PJSIP_SESSION_H

/* Needed for pj_timer_entry definition */
#include "pjlib.h"
#include "asterisk/linkedlists.h"
/* Needed for AST_MAX_EXTENSION constant */
#include "asterisk/channel.h"
/* Needed for ast_sockaddr struct */
#include "asterisk/netsock2.h"
/* Needed for ast_sdp_srtp struct */
#include "asterisk/sdp_srtp.h"

/* Forward declarations */
struct ast_sip_endpoint;
struct ast_sip_transport;
struct pjsip_inv_session;
struct ast_channel;
struct ast_datastore;
struct ast_datastore_info;
struct ao2_container;
struct pjsip_tx_data;
struct pjsip_rx_data;
struct ast_party_id;
struct pjmedia_sdp_media;
struct pjmedia_sdp_session;
struct ast_dsp;
struct ast_udptl;

/*! \brief T.38 states for a session */
enum ast_sip_session_t38state {
	T38_DISABLED = 0,   /*!< Not enabled */
	T38_LOCAL_REINVITE, /*!< Offered from local - REINVITE */
	T38_PEER_REINVITE,  /*!< Offered from peer - REINVITE */
	T38_ENABLED,        /*!< Negotiated (enabled) */
	T38_REJECTED,       /*!< Refused */
	T38_MAX_ENUM,       /*!< Not an actual state; used as max value in the enum */
};

struct ast_sip_session_sdp_handler;

/*!
 * \brief A structure containing SIP session media information
 */
struct ast_sip_session_media {
	union {
		/*! \brief RTP instance itself */
		struct ast_rtp_instance *rtp;
		/*! \brief UDPTL instance itself */
		struct ast_udptl *udptl;
	};
	/*! \brief Direct media address */
	struct ast_sockaddr direct_media_addr;
	/*! \brief SDP handler that setup the RTP */
	struct ast_sip_session_sdp_handler *handler;
	/*! \brief Holds SRTP information */
	struct ast_sdp_srtp *srtp;
	/*! \brief What type of encryption is in use on this stream */
	enum ast_sip_session_media_encryption encryption;
	/*! \brief The media transport in use for this stream */
	pj_str_t transport;
	/*! \brief Stream is on hold by remote side */
	unsigned int remotely_held:1;
	/*! \brief Stream is on hold by local side */
	unsigned int locally_held:1;
	/*! \brief Stream type this session media handles */
	char stream_type[1];
};

/*!
 * \brief Opaque structure representing a request that could not be sent
 * due to an outstanding INVITE transaction
 */
struct ast_sip_session_delayed_request;

/*! \brief Opaque struct controlling the suspension of the session's serializer. */
struct ast_sip_session_suspender;

/*!
 * \brief A structure describing a SIP session
 *
 * For the sake of brevity, a "SIP session" in Asterisk is referring to
 * a dialog initiated by an INVITE. While "session" is typically interpreted
 * to refer to the negotiated media within a SIP dialog, we have opted
 * to use the term "SIP session" to refer to the INVITE dialog itself.
 */
struct ast_sip_session {
	/*! Dialplan extension where incoming call is destined */
	char exten[AST_MAX_EXTENSION];
	/*! The endpoint with which Asterisk is communicating */
	struct ast_sip_endpoint *endpoint;
	/*! The contact associated with this session */
	struct ast_sip_contact *contact;
	/*! The PJSIP details of the session, which includes the dialog */
	struct pjsip_inv_session *inv_session;
	/*! The Asterisk channel associated with the session */
	struct ast_channel *channel;
	/*! Registered session supplements */
	AST_LIST_HEAD(, ast_sip_session_supplement) supplements;
	/*! Datastores added to the session by supplements to the session */
	struct ao2_container *datastores;
	/*! Media streams */
	struct ao2_container *media;
	/*! Serializer for tasks relating to this SIP session */
	struct ast_taskprocessor *serializer;
	/*! Non-null if the session serializer is suspended or being suspended. */
	struct ast_sip_session_suspender *suspended;
	/*! Requests that could not be sent due to current inv_session state */
	AST_LIST_HEAD_NOLOCK(, ast_sip_session_delayed_request) delayed_requests;
	/*! When we need to reschedule a reinvite, we use this structure to do it */
	pj_timer_entry rescheduled_reinvite;
	/*! Format capabilities pertaining to direct media */
	struct ast_format_cap *direct_media_cap;
	/*! When we need to forcefully end the session */
	pj_timer_entry scheduled_termination;
	/*! Identity of endpoint this session deals with */
	struct ast_party_id id;
	/*! Requested capabilities */
	struct ast_format_cap *req_caps;
	/*! Optional DSP, used only for inband DTMF detection if configured */
	struct ast_dsp *dsp;
	/*! Whether the termination of the session should be deferred */
	unsigned int defer_terminate:1;
	/*! Deferred incoming re-invite */
	pjsip_rx_data *deferred_reinvite;
	/*! Current T.38 state */
	enum ast_sip_session_t38state t38state;
};

typedef int (*ast_sip_session_request_creation_cb)(struct ast_sip_session *session, pjsip_tx_data *tdata);
typedef int (*ast_sip_session_response_cb)(struct ast_sip_session *session, pjsip_rx_data *rdata);
typedef int (*ast_sip_session_sdp_creation_cb)(struct ast_sip_session *session, pjmedia_sdp_session *sdp);

/*!
 * \brief Describes when a supplement should be called into on incoming responses.
 *
 * In most cases, session supplements will not need to worry about this because in most cases,
 * the correct value will be automatically applied. However, there are rare circumstances
 * when a supplement will want to specify when it should be called.
 *
 * The values below are listed in chronological order.
 */
enum ast_sip_session_response_priority {
	/*!
	 * When processing 3XX responses, the supplement is called into before
	 * the redirecting information is processed.
	 */
	AST_SIP_SESSION_BEFORE_REDIRECTING = (1 << 0),
	/*!
	 * For responses to INVITE transactions, the supplement is called into
	 * before media is negotiated.
	 *
	 * This priority is applied by default to any session supplement that
	 * does not specify a response priority.
	 */
	AST_SIP_SESSION_BEFORE_MEDIA = (1 << 1),
	/*!
	 * For INVITE transactions, the supplement is called into after media
	 * is negotiated.
	 */
	AST_SIP_SESSION_AFTER_MEDIA = (1 << 2),
};

/*!
 * \brief A supplement to SIP message processing
 *
 * These can be registered by any module in order to add
 * processing to incoming and outgoing SIP requests and responses
 */
struct ast_sip_session_supplement {
	/*! Method on which to call the callbacks. If NULL, call on all methods */
	const char *method;
	/*! Priority for this supplement. Lower numbers are visited before higher numbers */
	enum ast_sip_supplement_priority priority;
	/*!
	 * \brief Notification that the session has begun
	 * This method will always be called from a SIP servant thread.
	 */
	void (*session_begin)(struct ast_sip_session *session);
	/*! 
	 * \brief Notification that the session has ended
	 *
	 * This method may or may not be called from a SIP servant thread. Do
	 * not make assumptions about being able to call PJSIP methods from within
	 * this method.
	 */
	void (*session_end)(struct ast_sip_session *session);
	/*!
	 * \brief Notification that the session is being destroyed
	 */
	void (*session_destroy)(struct ast_sip_session *session);
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
	int (*incoming_request)(struct ast_sip_session *session, struct pjsip_rx_data *rdata);
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
	void (*incoming_response)(struct ast_sip_session *session, struct pjsip_rx_data *rdata);
	/*!
	 * \brief Called on an outgoing SIP request
	 * This method is always called from a SIP servant thread.
	 */
	void (*outgoing_request)(struct ast_sip_session *session, struct pjsip_tx_data *tdata);
	/*! 
	 * \brief Called on an outgoing SIP response
	 * This method is always called from a SIP servant thread.
	 */
	void (*outgoing_response)(struct ast_sip_session *session, struct pjsip_tx_data *tdata);
	/*! Next item in the list */
	AST_LIST_ENTRY(ast_sip_session_supplement) next;
	/*!
	 * Determines when the supplement is processed when handling a response.
	 * Defaults to AST_SIP_SESSION_BEFORE_MEDIA
	 */
	enum ast_sip_session_response_priority response_priority;
};

enum ast_sip_session_sdp_stream_defer {
	/*! The stream was not handled by this handler. If there are other registered handlers for this stream type, they will be called. */
	AST_SIP_SESSION_SDP_DEFER_NOT_HANDLED,
	/*! There was an error encountered. No further operations will take place and the current negotiation will be abandoned. */
	AST_SIP_SESSION_SDP_DEFER_ERROR,
	/*! Re-invite is not needed */
	AST_SIP_SESSION_SDP_DEFER_NOT_NEEDED,
	/*! Re-invite should be deferred and will be resumed later. No further operations will take place. */
	AST_SIP_SESSION_SDP_DEFER_NEEDED,
};

/*!
 * \brief A handler for SDPs in SIP sessions
 *
 * An SDP handler is registered by a module that is interested in being the
 * responsible party for specific types of SDP streams.
 */
struct ast_sip_session_sdp_handler {
	/*! An identifier for this handler */
	const char *id;
	/*!
	 * \brief Determine whether a stream requires that the re-invite be deferred.
	 * If a stream can not be immediately negotiated the re-invite can be deferred and
	 * resumed at a later time. It is up to the handler which caused deferral to occur
	 * to resume it.
	 *
	 * \param session The session for which the media is being re-invited
	 * \param session_media The media being reinvited
	 * \param sdp The entire SDP. Useful for getting "global" information, such as connections or attributes
	 * \param stream PJSIP incoming SDP media lines to parse by handler.
	 *
	 * \return enum ast_sip_session_defer_stream
	 *
	 * \note This is optional, if not implemented the stream is assumed to not be deferred.
	 */
	enum ast_sip_session_sdp_stream_defer (*defer_incoming_sdp_stream)(struct ast_sip_session *session, struct ast_sip_session_media *session_media, const struct pjmedia_sdp_session *sdp, const struct pjmedia_sdp_media *stream);
	/*!
	 * \brief Set session details based on a stream in an incoming SDP offer or answer
	 * \param session The session for which the media is being negotiated
	 * \param session_media The media to be setup for this session
	 * \param sdp The entire SDP. Useful for getting "global" information, such as connections or attributes
	 * \param stream The stream on which to operate
	 * \retval 0 The stream was not handled by this handler. If there are other registered handlers for this stream type, they will be called.
	 * \retval <0 There was an error encountered. No further operation will take place and the current negotiation will be abandoned.
	 * \retval >0 The stream was handled by this handler. No further handler of this stream type will be called.
	 */
	int (*negotiate_incoming_sdp_stream)(struct ast_sip_session *session, struct ast_sip_session_media *session_media, const struct pjmedia_sdp_session *sdp, const struct pjmedia_sdp_media *stream);
	/*!
	 * \brief Create an SDP media stream and add it to the outgoing SDP offer or answer
	 * \param session The session for which media is being added
	 * \param session_media The media to be setup for this session
	 * \param stream The stream on which to operate
	 * \retval 0 The stream was not handled by this handler. If there are other registered handlers for this stream type, they will be called.
	 * \retval <0 There was an error encountered. No further operation will take place and the current negotiation will be abandoned.
	 * \retval >0 The stream was handled by this handler. No further handler of this stream type will be called.
	 */
	int (*handle_incoming_sdp_stream)(struct ast_sip_session *session, struct ast_sip_session_media *session_media, const struct pjmedia_sdp_session *sdp, struct pjmedia_sdp_media *stream);
	/*!
	 * \brief Create an SDP media stream and add it to the outgoing SDP offer or answer
	 * \param session The session for which media is being added
	 * \param session_media The media to be setup for this session
	 * \param sdp The entire SDP as currently built
	 * \retval 0 This handler has no stream to add. If there are other registered handlers for this stream type, they will be called.
	 * \retval <0 There was an error encountered. No further operation will take place and the current SDP negotiation will be abandoned.
	 * \retval >0 The handler has a stream to be added to the SDP. No further handler of this stream type will be called.
	 */
	int (*create_outgoing_sdp_stream)(struct ast_sip_session *session, struct ast_sip_session_media *session_media, struct pjmedia_sdp_session *sdp);
	/*!
	 * \brief Update media stream with external address if applicable
	 * \param tdata The outgoing message itself
	 * \param stream The stream on which to operate
	 * \param transport The transport the SDP is going out on
	 */
	void (*change_outgoing_sdp_stream_media_address)(struct pjsip_tx_data *tdata, struct pjmedia_sdp_media *stream, struct ast_sip_transport *transport);
	/*!
	 * \brief Apply a negotiated SDP media stream
	 * \param session The session for which media is being applied
	 * \param session_media The media to be setup for this session
	 * \param local The entire local negotiated SDP
	 * \param local_stream The local stream which to apply
	 * \param remote The entire remote negotiated SDP
	 * \param remote_stream The remote stream which to apply
	 * \retval 0 The stream was not applied by this handler. If there are other registered handlers for this stream type, they will be called.
	 * \retval <0 There was an error encountered. No further operation will take place and the current application will be abandoned.
	 * \retval >0 The stream was handled by this handler. No further handler of this stream type will be called.
	 */
	int (*apply_negotiated_sdp_stream)(struct ast_sip_session *session, struct ast_sip_session_media *session_media, const struct pjmedia_sdp_session *local, const struct pjmedia_sdp_media *local_stream,
		const struct pjmedia_sdp_session *remote, const struct pjmedia_sdp_media *remote_stream);
	/*!
	 * \brief Destroy a session_media created by this handler
	 * \param session The session for which media is being destroyed
	 * \param session_media The media to destroy
	 */
	void (*stream_destroy)(struct ast_sip_session_media *session_media);
	/*! Next item in the list. */
	AST_LIST_ENTRY(ast_sip_session_sdp_handler) next;
};

/*!
 * \brief A structure which contains a channel implementation and session
 */
struct ast_sip_channel_pvt {
	/*! \brief Pointer to channel specific implementation information, must be ao2 object */
	void *pvt;
	/*! \brief Pointer to session */
	struct ast_sip_session *session;
};

/*!
 * \brief Allocate a new SIP channel pvt structure
 *
 * \param pvt Pointer to channel specific implementation
 * \param session Pointer to SIP session
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
struct ast_sip_channel_pvt *ast_sip_channel_pvt_alloc(void *pvt, struct ast_sip_session *session);

/*!
 * \brief Allocate a new SIP session
 *
 * This will take care of allocating the datastores container on the session as well
 * as placing all registered supplements onto the session.
 *
 * The endpoint that is passed in will have its reference count increased by one since
 * the session will be keeping a reference to the endpoint. The session will relinquish
 * this reference when the session is destroyed.
 *
 * \param endpoint The endpoint that this session communicates with
 * \param contact The contact associated with this session
 * \param inv_session The PJSIP INVITE session data
 */
struct ast_sip_session *ast_sip_session_alloc(struct ast_sip_endpoint *endpoint,
	struct ast_sip_contact *contact, pjsip_inv_session *inv);

/*!
 * \brief Request and wait for the session serializer to be suspended.
 * \since 12.7.0
 *
 * \param session Which session to suspend the serializer.
 *
 * \note No channel locks can be held while calling without risk of deadlock.
 *
 * \return Nothing
 */
void ast_sip_session_suspend(struct ast_sip_session *session);

/*!
 * \brief Request the session serializer be unsuspended.
 * \since 12.7.0
 *
 * \param session Which session to unsuspend the serializer.
 *
 * \return Nothing
 */
void ast_sip_session_unsuspend(struct ast_sip_session *session);

/*!
 * \brief Create a new outgoing SIP session
 *
 * The endpoint that is passed in will have its reference count increased by one since
 * the session will be keeping a reference to the endpoint. The session will relinquish
 * this reference when the session is destroyed.
 *
 * \param endpoint The endpoint that this session uses for settings
 * \param contact The contact that this session will communicate with
 * \param location Name of the location to call, be it named location or explicit URI. Overrides contact if present.
 * \param request_user Optional request user to place in the request URI if permitted
 * \param req_caps The requested capabilities
 */
struct ast_sip_session *ast_sip_session_create_outgoing(struct ast_sip_endpoint *endpoint,
	struct ast_sip_contact *contact, const char *location, const char *request_user,
	struct ast_format_cap *req_caps);

/*!
 * \brief Terminate a session and, if possible, send the provided response code
 *
 * \param session The session to terminate
 * \param response The response code to use for termination if possible
 */
void ast_sip_session_terminate(struct ast_sip_session *session, int response);

/*!
 * \brief Defer local termination of a session until remote side terminates, or an amount of time passes
 *
 * \param session The session to defer termination on
 */
void ast_sip_session_defer_termination(struct ast_sip_session *session);

/*!
 * \brief Register an SDP handler
 *
 * An SDP handler is responsible for parsing incoming SDP streams and ensuring that
 * Asterisk can cope with the contents. Similarly, the SDP handler will be
 * responsible for constructing outgoing SDP streams.
 *
 * Multiple handlers for the same stream type may be registered. They will be
 * visited in the order they were registered. Handlers will be visited for each
 * stream type until one claims to have handled the stream.
 *
 * \param handler The SDP handler to register
 * \param stream_type The type of media stream for which to call the handler
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_session_register_sdp_handler(struct ast_sip_session_sdp_handler *handler, const char *stream_type);

/*!
 * \brief Unregister an SDP handler
 *
 * \param handler The SDP handler to unregister
 * \param stream_type Stream type for which the SDP handler was registered
 */
void ast_sip_session_unregister_sdp_handler(struct ast_sip_session_sdp_handler *handler, const char *stream_type);

/*!
 * \brief Register a supplement to SIP session processing
 *
 * This allows for someone to insert themselves in the processing of SIP
 * requests and responses. This, for example could allow for a module to
 * set channel data based on headers in an incoming message. Similarly,
 * a module could reject an incoming request if desired.
 *
 * \param supplement The supplement to register
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_session_register_supplement(struct ast_sip_session_supplement *supplement);

/*!
 * \brief Unregister a an supplement to SIP session processing
 *
 * \param supplement The supplement to unregister
 */
void ast_sip_session_unregister_supplement(struct ast_sip_session_supplement *supplement);

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
struct ast_datastore *ast_sip_session_alloc_datastore(const struct ast_datastore_info *info, const char *uid);

/*!
 * \brief Add a datastore to a SIP session
 *
 * Note that SIP uses reference counted datastores. The datastore passed into this function
 * must have been allocated using ao2_alloc() or there will be serious problems.
 *
 * \param session The session to add the datastore to
 * \param datastore The datastore to be added to the session
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_session_add_datastore(struct ast_sip_session *session, struct ast_datastore *datastore);

/*!
 * \brief Retrieve a session datastore
 *
 * The datastore retrieved will have its reference count incremented. When the caller is done
 * with the datastore, the reference counted needs to be decremented using ao2_ref().
 *
 * \param session The session from which to retrieve the datastore
 * \param name The name of the datastore to retrieve
 * \retval NULL Failed to find the specified datastore
 * \retval non-NULL The specified datastore
 */
struct ast_datastore *ast_sip_session_get_datastore(struct ast_sip_session *session, const char *name);

/*!
 * \brief Remove a session datastore from the session
 *
 * This operation may cause the datastore's free() callback to be called if the reference
 * count reaches zero.
 *
 * \param session The session to remove the datastore from
 * \param name The name of the datastore to remove
 */
void ast_sip_session_remove_datastore(struct ast_sip_session *session, const char *name);

/*!
 * \brief Send a reinvite or UPDATE on a session
 *
 * This method will inspect the session in order to construct an appropriate
 * session refresh request. As with any outgoing request in res_pjsip_session,
 * this will call into registered supplements in case they wish to add anything.
 *
 * Note: The on_request_creation callback may or may not be called in the same
 * thread where this function is called. Request creation may need to be delayed
 * due to the current INVITE transaction state.
 * 
 * \param session The session on which the reinvite will be sent
 * \param on_request_creation Callback called when request is created
 * \param on_sdp_creation Callback called when SDP is created
 * \param on_response Callback called when response for request is received
 * \param method The method that should be used when constructing the session refresh
 * \param generate_new_sdp Boolean to indicate if a new SDP should be created
 * \retval 0 Successfully sent refresh
 * \retval -1 Failure to send refresh
 */
int ast_sip_session_refresh(struct ast_sip_session *session,
		ast_sip_session_request_creation_cb on_request_creation,
		ast_sip_session_sdp_creation_cb on_sdp_creation,
		ast_sip_session_response_cb on_response,
		enum ast_sip_session_refresh_method method,
		int generate_new_sdp);

/*!
 * \brief Send a SIP response
 *
 * This will send the SIP response specified in tdata and
 * call into any registered supplements' outgoing_response callback.
 *
 * \param session The session on which to send the response.
 * \param tdata The response to send
 */
void ast_sip_session_send_response(struct ast_sip_session *session, pjsip_tx_data *tdata);

/*!
 * \brief Send a SIP request
 *
 * This will send the SIP request specified in tdata and
 * call into any registered supplements' outgoing_request callback.
 *
 * \param session The session to which to send the request
 * \param tdata The request to send
 */
void ast_sip_session_send_request(struct ast_sip_session *session, pjsip_tx_data *tdata);

/*!
 * \brief Creates an INVITE request.
 *
 * \param session Starting session for the INVITE
 * \param tdata The created request.
 */
int ast_sip_session_create_invite(struct ast_sip_session *session, pjsip_tx_data **tdata);

/*!
 * \brief Send a SIP request and get called back when a response is received
 *
 * This will send the request out exactly the same as ast_sip_send_request() does.
 * The difference is that when a response arrives, the specified callback will be
 * called into
 *
 * \param session The session on which to send the request
 * \param tdata The request to send
 * \param on_response Callback to be called when a response is received
 */
void ast_sip_session_send_request_with_cb(struct ast_sip_session *session, pjsip_tx_data *tdata,
		ast_sip_session_response_cb on_response);

/*!
 * \brief Retrieves a session from a dialog
 *
 * \param dlg The dialog to retrieve the session from
 *
 * \retval non-NULL if session exists
 * \retval NULL if no session
 *
 * \note The reference count of the session is increased when returned
 *
 * \note This function *must* be called with the dialog locked
 */
struct ast_sip_session *ast_sip_dialog_get_session(pjsip_dialog *dlg);

/*!
 * \brief Resumes processing of a deferred incoming re-invite
 *
 * \param session The session which has a pending incoming re-invite
 *
 * \note When resuming a re-invite it is given to the pjsip stack as if it
 *       had just been received from a transport, this means that the deferral
 *       callback will be called again.
 */
void ast_sip_session_resume_reinvite(struct ast_sip_session *session);

/*! \brief Determines whether the res_pjsip_session module is loaded */
#define CHECK_PJSIP_SESSION_MODULE_LOADED()				\
	do {								\
		CHECK_PJSIP_MODULE_LOADED();				\
		if (!ast_module_check("res_pjsip_session.so")) {	\
			return AST_MODULE_LOAD_DECLINE;			\
		}							\
	} while(0)

#endif /* _RES_PJSIP_SESSION_H */
