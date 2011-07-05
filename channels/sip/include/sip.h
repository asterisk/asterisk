/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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

/*!
 * \file
 * \brief chan_sip header file
 */

#ifndef _SIP_H
#define _SIP_H

#include "asterisk.h"

#include "asterisk/stringfields.h"
#include "asterisk/linkedlists.h"
#include "asterisk/strings.h"
#include "asterisk/tcptls.h"
#include "asterisk/test.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/astobj.h"

#ifndef FALSE
#define FALSE    0
#endif

#ifndef TRUE
#define TRUE     1
#endif

/* Arguments for find_peer */
#define FINDUSERS (1 << 0)
#define FINDPEERS (1 << 1)
#define FINDALLDEVICES (FINDUSERS | FINDPEERS)

#define	SIPBUFSIZE      512             /*!< Buffer size for many operations */

#define XMIT_ERROR      -2

#define SIP_RESERVED    ";/?:@&=+$,# "  /*!< Reserved characters in the username part of the URI */

#define DEFAULT_DEFAULT_EXPIRY       120
#define DEFAULT_MIN_EXPIRY           60
#define DEFAULT_MAX_EXPIRY           3600
#define DEFAULT_MWI_EXPIRY           3600
#define DEFAULT_REGISTRATION_TIMEOUT 20
#define DEFAULT_MAX_FORWARDS         70

#define DEFAULT_AUTHLIMIT            100
#define DEFAULT_AUTHTIMEOUT          30

/* guard limit must be larger than guard secs */
/* guard min must be < 1000, and should be >= 250 */
#define EXPIRY_GUARD_SECS    15   /*!< How long before expiry do we reregister */
#define EXPIRY_GUARD_LIMIT   30   /*!< Below here, we use EXPIRY_GUARD_PCT instead of EXPIRY_GUARD_SECS */
#define EXPIRY_GUARD_MIN     500  /*!< This is the minimum guard time applied. If
                                   *   GUARD_PCT turns out to be lower than this, it
                                   *   will use this time instead.
                                   *   This is in milliseconds.
								   */
#define EXPIRY_GUARD_PCT     0.20 /*!< Percentage of expires timeout to use when
                                   *   below EXPIRY_GUARD_LIMIT */
#define DEFAULT_EXPIRY       900  /*!< Expire slowly */

#define DEFAULT_QUALIFY_GAP   100
#define DEFAULT_QUALIFY_PEERS 1

#define CALLERID_UNKNOWN          "Anonymous"
#define FROMDOMAIN_INVALID        "anonymous.invalid"

#define DEFAULT_MAXMS             2000        /*!< Qualification: Must be faster than 2 seconds by default */
#define DEFAULT_QUALIFYFREQ       60 * 1000   /*!< Qualification: How often to check for the host to be up */
#define DEFAULT_FREQ_NOTOK        10 * 1000   /*!< Qualification: How often to check, if the host is down... */

#define DEFAULT_RETRANS           1000        /*!< How frequently to retransmit Default: 2 * 500 ms in RFC 3261 */
#define DEFAULT_TIMER_T1          500         /*!< SIP timer T1 (according to RFC 3261) */
#define SIP_TRANS_TIMEOUT         64 * DEFAULT_TIMER_T1 /*!< SIP request timeout (rfc 3261) 64*T1
                                                         *  \todo Use known T1 for timeout (peerpoke)
                                                         */
#define DEFAULT_TRANS_TIMEOUT     -1     /*!< Use default SIP transaction timeout */
#define PROVIS_KEEPALIVE_TIMEOUT  60000  /*!< How long to wait before retransmitting a provisional response (rfc 3261 13.3.1.1) */
#define MAX_AUTHTRIES             3      /*!< Try authentication three times, then fail */

#define SIP_MAX_HEADERS           64     /*!< Max amount of SIP headers to read */
#define SIP_MAX_LINES             256    /*!< Max amount of lines in SIP attachment (like SDP) */
#define SIP_MIN_PACKET            4096   /*!< Initialize size of memory to allocate for packets */
#define MAX_HISTORY_ENTRIES		  50	 /*!< Max entires in the history list for a sip_pvt */

#define INITIAL_CSEQ              101    /*!< Our initial sip sequence number */

#define DEFAULT_MAX_SE            1800   /*!< Session-Timer Default Session-Expires period (RFC 4028) */
#define DEFAULT_MIN_SE            90     /*!< Session-Timer Default Min-SE period (RFC 4028) */

#define SDP_MAX_RTPMAP_CODECS     32     /*!< Maximum number of codecs allowed in received SDP */

#define RTP     1
#define NO_RTP  0

#define DEC_CALL_LIMIT   0
#define INC_CALL_LIMIT   1
#define DEC_CALL_RINGING 2
#define INC_CALL_RINGING 3

/*! Define SIP option tags, used in Require: and Supported: headers
 *  We need to be aware of these properties in the phones to use
 *  the replace: header. We should not do that without knowing
 *  that the other end supports it...
 *  This is nothing we can configure, we learn by the dialog
 *  Supported: header on the REGISTER (peer) or the INVITE
 *  (other devices)
 *  We are not using many of these today, but will in the future.
 *  This is documented in RFC 3261
 */
#define SUPPORTED       1
#define NOT_SUPPORTED   0

/* SIP options */
#define SIP_OPT_REPLACES      (1 << 0)
#define SIP_OPT_100REL        (1 << 1)
#define SIP_OPT_TIMER         (1 << 2)
#define SIP_OPT_EARLY_SESSION (1 << 3)
#define SIP_OPT_JOIN          (1 << 4)
#define SIP_OPT_PATH          (1 << 5)
#define SIP_OPT_PREF          (1 << 6)
#define SIP_OPT_PRECONDITION  (1 << 7)
#define SIP_OPT_PRIVACY       (1 << 8)
#define SIP_OPT_SDP_ANAT      (1 << 9)
#define SIP_OPT_SEC_AGREE     (1 << 10)
#define SIP_OPT_EVENTLIST     (1 << 11)
#define SIP_OPT_GRUU          (1 << 12)
#define SIP_OPT_TARGET_DIALOG (1 << 13)
#define SIP_OPT_NOREFERSUB    (1 << 14)
#define SIP_OPT_HISTINFO      (1 << 15)
#define SIP_OPT_RESPRIORITY   (1 << 16)
#define SIP_OPT_FROMCHANGE    (1 << 17)
#define SIP_OPT_RECLISTINV    (1 << 18)
#define SIP_OPT_RECLISTSUB    (1 << 19)
#define SIP_OPT_OUTBOUND      (1 << 20)
#define SIP_OPT_UNKNOWN       (1 << 21)

/*! \brief SIP Methods we support
 *  \todo This string should be set dynamically. We only support REFER and SUBSCRIBE if we have
 *  allowsubscribe and allowrefer on in sip.conf.
 */
#define ALLOWED_METHODS "INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, SUBSCRIBE, NOTIFY, INFO, PUBLISH"

/*! \brief Standard SIP unsecure port for UDP and TCP from RFC 3261. DO NOT CHANGE THIS */
#define STANDARD_SIP_PORT	5060
/*! \brief Standard SIP TLS port from RFC 3261. DO NOT CHANGE THIS */
#define STANDARD_TLS_PORT	5061

/*! \note in many SIP headers, absence of a port number implies port 5060,
 * and this is why we cannot change the above constant.
 * There is a limited number of places in asterisk where we could,
 * in principle, use a different "default" port number, but
 * we do not support this feature at the moment.
 * You can run Asterisk with SIP on a different port with a configuration
 * option. If you change this value in the source code, the signalling will be incorrect.
 *
 */

/*! \name DefaultValues Default values, set and reset in reload_config before reading configuration

   These are default values in the source. There are other recommended values in the
   sip.conf.sample for new installations. These may differ to keep backwards compatibility,
   yet encouraging new behaviour on new installations
 */
/*@{*/
#define DEFAULT_CONTEXT        "default"  /*!< The default context for [general] section as well as devices */
#define DEFAULT_MOHINTERPRET   "default"  /*!< The default music class */
#define DEFAULT_MOHSUGGEST     ""
#define DEFAULT_VMEXTEN        "asterisk" /*!< Default voicemail extension */
#define DEFAULT_CALLERID       "asterisk" /*!< Default caller ID */
#define DEFAULT_MWI_FROM       ""
#define DEFAULT_NOTIFYMIME     "application/simple-message-summary"
#define DEFAULT_ALLOWGUEST     TRUE
#define DEFAULT_RTPKEEPALIVE   0      /*!< Default RTPkeepalive setting */
#define DEFAULT_CALLCOUNTER    FALSE   /*!< Do not enable call counters by default */
#define DEFAULT_SRVLOOKUP      TRUE    /*!< Recommended setting is ON */
#define DEFAULT_COMPACTHEADERS FALSE  /*!< Send compact (one-character) SIP headers. Default off */
#define DEFAULT_TOS_SIP        0      /*!< Call signalling packets should be marked as DSCP CS3, but the default is 0 to be compatible with previous versions. */
#define DEFAULT_TOS_AUDIO      0      /*!< Audio packets should be marked as DSCP EF (Expedited Forwarding), but the default is 0 to be compatible with previous versions. */
#define DEFAULT_TOS_VIDEO      0      /*!< Video packets should be marked as DSCP AF41, but the default is 0 to be compatible with previous versions. */
#define DEFAULT_TOS_TEXT       0      /*!< Text packets should be marked as XXXX XXXX, but the default is 0 to be compatible with previous versions. */
#define DEFAULT_COS_SIP        4      /*!< Level 2 class of service for SIP signalling */
#define DEFAULT_COS_AUDIO      5      /*!< Level 2 class of service for audio media  */
#define DEFAULT_COS_VIDEO      6      /*!< Level 2 class of service for video media */
#define DEFAULT_COS_TEXT       5      /*!< Level 2 class of service for text media (T.140) */
#define DEFAULT_ALLOW_EXT_DOM  TRUE    /*!< Allow external domains */
#define DEFAULT_REALM          "asterisk" /*!< Realm for HTTP digest authentication */
#define DEFAULT_DOMAINSASREALM FALSE    /*!< Use the domain option to guess the realm for registration and invite requests */
#define DEFAULT_NOTIFYRINGING  TRUE     /*!< Notify devicestate system on ringing state */
#define DEFAULT_NOTIFYCID      DISABLED	/*!< Include CID with ringing notifications */
#define DEFAULT_PEDANTIC       TRUE     /*!< Follow SIP standards for dialog matching */
#define DEFAULT_AUTOCREATEPEER FALSE    /*!< Don't create peers automagically */
#define	DEFAULT_MATCHEXTERNADDRLOCALLY FALSE /*!< Match extern IP locally default setting */
#define DEFAULT_QUALIFY        FALSE    /*!< Don't monitor devices */
#define DEFAULT_CALLEVENTS     FALSE    /*!< Extra manager SIP call events */
#define DEFAULT_ALWAYSAUTHREJECT  TRUE  /*!< Don't reject authentication requests always */
#define DEFAULT_AUTH_OPTIONS  FALSE
#define DEFAULT_AUTH_MESSAGE  TRUE
#define DEFAULT_ACCEPT_OUTOFCALL_MESSAGE TRUE
#define DEFAULT_REGEXTENONQUALIFY FALSE
#define DEFAULT_LEGACY_USEROPTION_PARSING FALSE
#define DEFAULT_T1MIN             100   /*!< 100 MS for minimal roundtrip time */
#define DEFAULT_MAX_CALL_BITRATE (384)  /*!< Max bitrate for video */
#ifndef DEFAULT_USERAGENT
#define DEFAULT_USERAGENT  "Asterisk PBX"  /*!< Default Useragent: header unless re-defined in sip.conf */
#define DEFAULT_SDPSESSION "Asterisk PBX"  /*!< Default SDP session name, (s=) header unless re-defined in sip.conf */
#define DEFAULT_SDPOWNER   "root"          /*!< Default SDP username field in (o=) header unless re-defined in sip.conf */
#define DEFAULT_ENGINE     "asterisk"      /*!< Default RTP engine to use for sessions */
#endif
/*@}*/

/*! \name SIPflags
	Various flags for the flags field in the pvt structure
	Trying to sort these up (one or more of the following):
	D: Dialog
	P: Peer/user
	G: Global flag
	When flags are used by multiple structures, it is important that
	they have a common layout so it is easy to copy them.
*/
/*@{*/
#define SIP_OUTGOING        (1 << 0) /*!< D: Direction of the last transaction in this dialog */
#define SIP_OFFER_CC        (1 << 1) /*!< D: Offer CC on subsequent responses */
#define SIP_RINGING         (1 << 2) /*!< D: Have sent 180 ringing */
#define SIP_PROGRESS_SENT   (1 << 3) /*!< D: Have sent 183 message progress */
#define SIP_NEEDREINVITE    (1 << 4) /*!< D: Do we need to send another reinvite? */
#define SIP_PENDINGBYE      (1 << 5) /*!< D: Need to send bye after we ack? */
#define SIP_GOTREFER        (1 << 6) /*!< D: Got a refer? */
#define SIP_CALL_LIMIT      (1 << 7) /*!< D: Call limit enforced for this call */
#define SIP_INC_COUNT       (1 << 8) /*!< D: Did this dialog increment the counter of in-use calls? */
#define SIP_INC_RINGING     (1 << 9) /*!< D: Did this connection increment the counter of in-use calls? */
#define SIP_DEFER_BYE_ON_TRANSFER	(1 << 10) /*!< D: Do not hangup at first ast_hangup */

#define SIP_PROMISCREDIR    (1 << 11) /*!< DP: Promiscuous redirection */
#define SIP_TRUSTRPID       (1 << 12) /*!< DP: Trust RPID headers? */
#define SIP_USEREQPHONE     (1 << 13) /*!< DP: Add user=phone to numeric URI. Default off */
#define SIP_USECLIENTCODE   (1 << 14) /*!< DP: Trust X-ClientCode info message */

/* DTMF flags - see str2dtmfmode() and dtmfmode2str() */
#define SIP_DTMF            (7 << 15) /*!< DP: DTMF Support: five settings, uses three bits */
#define SIP_DTMF_RFC2833    (0 << 15) /*!< DP: DTMF Support: RTP DTMF - "rfc2833" */
#define SIP_DTMF_INBAND     (1 << 15) /*!< DP: DTMF Support: Inband audio, only for ULAW/ALAW - "inband" */
#define SIP_DTMF_INFO       (2 << 15) /*!< DP: DTMF Support: SIP Info messages - "info" */
#define SIP_DTMF_AUTO       (3 << 15) /*!< DP: DTMF Support: AUTO switch between rfc2833 and in-band DTMF */
#define SIP_DTMF_SHORTINFO  (4 << 15) /*!< DP: DTMF Support: SIP Info messages - "info" - short variant */

/* NAT settings */
#define SIP_NAT_FORCE_RPORT     (1 << 18) /*!< DP: Force rport even if not present in the request */
#define SIP_NAT_RPORT_PRESENT   (1 << 19) /*!< DP: rport was present in the request */

/* re-INVITE related settings */
#define SIP_REINVITE         (7 << 20) /*!< DP: four settings, uses three bits */
#define SIP_REINVITE_NONE    (0 << 20) /*!< DP: no reinvite allowed */
#define SIP_DIRECT_MEDIA     (1 << 20) /*!< DP: allow peers to be reinvited to send media directly p2p */
#define SIP_DIRECT_MEDIA_NAT (2 << 20) /*!< DP: allow media reinvite when new peer is behind NAT */
#define SIP_REINVITE_UPDATE  (4 << 20) /*!< DP: use UPDATE (RFC3311) when reinviting this peer */

/* "insecure" settings - see insecure2str() */
#define SIP_INSECURE         (3 << 23)    /*!< DP: three settings, uses two bits */
#define SIP_INSECURE_NONE    (0 << 23)    /*!< DP: secure mode */
#define SIP_INSECURE_PORT    (1 << 23)    /*!< DP: don't require matching port for incoming requests */
#define SIP_INSECURE_INVITE  (1 << 24)    /*!< DP: don't require authentication for incoming INVITEs */

/* Sending PROGRESS in-band settings */
#define SIP_PROG_INBAND        (3 << 25) /*!< DP: three settings, uses two bits */
#define SIP_PROG_INBAND_NEVER  (0 << 25)
#define SIP_PROG_INBAND_NO     (1 << 25)
#define SIP_PROG_INBAND_YES    (2 << 25)

#define SIP_SENDRPID         (3 << 29) /*!< DP: Remote Party-ID Support */
#define SIP_SENDRPID_NO      (0 << 29)
#define SIP_SENDRPID_PAI     (1 << 29) /*!< Use "P-Asserted-Identity" for rpid */
#define SIP_SENDRPID_RPID    (2 << 29) /*!< Use "Remote-Party-ID" for rpid */
#define SIP_G726_NONSTANDARD (1 << 31) /*!< DP: Use non-standard packing for G726-32 data */

/*! \brief Flags to copy from peer/user to dialog */
#define SIP_FLAGS_TO_COPY \
	(SIP_PROMISCREDIR | SIP_TRUSTRPID | SIP_SENDRPID | SIP_DTMF | SIP_REINVITE | \
	 SIP_PROG_INBAND | SIP_USECLIENTCODE | SIP_NAT_FORCE_RPORT | SIP_G726_NONSTANDARD | \
	 SIP_USEREQPHONE | SIP_INSECURE)
/*@}*/

/*! \name SIPflags2
	a second page of flags (for flags[1] */
/*@{*/
/* realtime flags */
#define SIP_PAGE2_RTCACHEFRIENDS		(1 <<  0)    /*!< GP: Should we keep RT objects in memory for extended time? */
#define SIP_PAGE2_RTAUTOCLEAR			(1 <<  1)    /*!< GP: Should we clean memory from peers after expiry? */
#define SIP_PAGE2_RPID_UPDATE			(1 <<  2)
#define SIP_PAGE2_Q850_REASON			(1 <<  3)    /*!< DP: Get/send cause code via Reason header */
#define SIP_PAGE2_SYMMETRICRTP			(1 <<  4)    /*!< GDP: Whether symmetric RTP is enabled or not */
#define SIP_PAGE2_STATECHANGEQUEUE		(1 <<  5)    /*!< D: Unsent state pending change exists */
#define SIP_PAGE2_CONNECTLINEUPDATE_PEND	(1 <<  6)
#define SIP_PAGE2_RPID_IMMEDIATE		(1 <<  7)
#define SIP_PAGE2_RPORT_PRESENT			(1 <<  8)   /*!< Was rport received in the Via header? */
#define SIP_PAGE2_PREFERRED_CODEC		(1 <<  9)   /*!< GDP: Only respond with single most preferred joint codec */
#define SIP_PAGE2_VIDEOSUPPORT			(1 << 10)   /*!< DP: Video supported if offered? */
#define SIP_PAGE2_TEXTSUPPORT			(1 << 11)   /*!< GDP: Global text enable */
#define SIP_PAGE2_ALLOWSUBSCRIBE		(1 << 12)   /*!< GP: Allow subscriptions from this peer? */
#define SIP_PAGE2_ALLOWOVERLAP			(1 << 13)   /*!< DP: Allow overlap dialing ? */
#define SIP_PAGE2_SUBSCRIBEMWIONLY		(1 << 14)   /*!< GP: Only issue MWI notification if subscribed to */
#define SIP_PAGE2_IGNORESDPVERSION		(1 << 15)   /*!< GDP: Ignore the SDP session version number we receive and treat all sessions as new */

#define SIP_PAGE2_T38SUPPORT			(3 << 16)    /*!< GDP: T.38 Fax Support */
#define SIP_PAGE2_T38SUPPORT_UDPTL		(1 << 16)    /*!< GDP: T.38 Fax Support (no error correction) */
#define SIP_PAGE2_T38SUPPORT_UDPTL_FEC		(2 << 16)    /*!< GDP: T.38 Fax Support (FEC error correction) */
#define SIP_PAGE2_T38SUPPORT_UDPTL_REDUNDANCY	(3 << 16)    /*!< GDP: T.38 Fax Support (redundancy error correction) */

#define SIP_PAGE2_CALL_ONHOLD			(3 << 18)  /*!< D: Call hold states: */
#define SIP_PAGE2_CALL_ONHOLD_ACTIVE		(1 << 18)  /*!< D: Active hold */
#define SIP_PAGE2_CALL_ONHOLD_ONEDIR		(2 << 18)  /*!< D: One directional hold */
#define SIP_PAGE2_CALL_ONHOLD_INACTIVE		(3 << 18)  /*!< D: Inactive hold */

#define SIP_PAGE2_RFC2833_COMPENSATE		(1 << 20)  /*!< DP: Compensate for buggy RFC2833 implementations */
#define SIP_PAGE2_BUGGY_MWI			(1 << 21)  /*!< DP: Buggy CISCO MWI fix */
#define SIP_PAGE2_DIALOG_ESTABLISHED		(1 << 22)  /*!< 29: Has a dialog been established? */

#define SIP_PAGE2_FAX_DETECT			(3 << 23)  /*!< DP: Fax Detection support */
#define SIP_PAGE2_FAX_DETECT_CNG		(1 << 23)  /*!< DP: Fax Detection support - detect CNG in audio */
#define SIP_PAGE2_FAX_DETECT_T38		(2 << 23)  /*!< DP: Fax Detection support - detect T.38 reinvite from peer */
#define SIP_PAGE2_FAX_DETECT_BOTH		(3 << 23)  /*!< DP: Fax Detection support - detect both */

#define SIP_PAGE2_REGISTERTRYING		(1 << 24)  /*!< DP: Send 100 Trying on REGISTER attempts */
#define SIP_PAGE2_UDPTL_DESTINATION		(1 << 25)  /*!< DP: Use source IP of RTP as destination if NAT is enabled */
#define SIP_PAGE2_VIDEOSUPPORT_ALWAYS		(1 << 26)  /*!< DP: Always set up video, even if endpoints don't support it */
#define SIP_PAGE2_HAVEPEERCONTEXT	(1 << 27)	/*< Are we associated with a configured peer context? */
#define SIP_PAGE2_USE_SRTP              (1 << 28)    /*!< DP: Whether we should offer (only)  SRTP */

#define SIP_PAGE2_FLAGS_TO_COPY \
	(SIP_PAGE2_ALLOWSUBSCRIBE | SIP_PAGE2_ALLOWOVERLAP | SIP_PAGE2_IGNORESDPVERSION | \
	SIP_PAGE2_VIDEOSUPPORT | SIP_PAGE2_T38SUPPORT | SIP_PAGE2_RFC2833_COMPENSATE | \
	SIP_PAGE2_BUGGY_MWI | SIP_PAGE2_TEXTSUPPORT | SIP_PAGE2_FAX_DETECT | \
	SIP_PAGE2_UDPTL_DESTINATION | SIP_PAGE2_VIDEOSUPPORT_ALWAYS | SIP_PAGE2_PREFERRED_CODEC | \
	SIP_PAGE2_RPID_IMMEDIATE | SIP_PAGE2_RPID_UPDATE | SIP_PAGE2_SYMMETRICRTP |\
	SIP_PAGE2_Q850_REASON | SIP_PAGE2_HAVEPEERCONTEXT | SIP_PAGE2_USE_SRTP)


#define SIP_PAGE3_SNOM_AOC               (1 << 0)  /*!< DPG: Allow snom aoc messages */

#define SIP_PAGE3_FLAGS_TO_COPY \
	(SIP_PAGE3_SNOM_AOC)

/*@}*/

/*----------------------------------------------------------*/
/*----                    ENUMS                         ----*/
/*----------------------------------------------------------*/

/*! \brief Authorization scheme for call transfers
 *
 * \note Not a bitfield flag, since there are plans for other modes,
 * like "only allow transfers for authenticated devices"
 */
enum transfermodes {
	TRANSFER_OPENFORALL,            /*!< Allow all SIP transfers */
	TRANSFER_CLOSED,                /*!< Allow no SIP transfers */
};

/*! \brief The result of a lot of functions */
enum sip_result {
	AST_SUCCESS = 0,		/*!< FALSE means success, funny enough */
	AST_FAILURE = -1,		/*!< Failure code */
};

/*! \brief States for the INVITE transaction, not the dialog
 *  \note this is for the INVITE that sets up the dialog
 */
enum invitestates {
	INV_NONE = 0,         /*!< No state at all, maybe not an INVITE dialog */
	INV_CALLING = 1,      /*!< Invite sent, no answer */
	INV_PROCEEDING = 2,   /*!< We got/sent 1xx message */
	INV_EARLY_MEDIA = 3,  /*!< We got 18x message with to-tag back */
	INV_COMPLETED = 4,    /*!< Got final response with error. Wait for ACK, then CONFIRMED */
	INV_CONFIRMED = 5,    /*!< Confirmed response - we've got an ack (Incoming calls only) */
	INV_TERMINATED = 6,   /*!< Transaction done - either successful (AST_STATE_UP) or failed, but done
				     The only way out of this is a BYE from one side */
	INV_CANCELLED = 7,    /*!< Transaction cancelled by client or server in non-terminated state */
};

/*! \brief When sending a SIP message, we can send with a few options, depending on
 * type of SIP request. UNRELIABLE is moslty used for responses to repeated requests,
 * where the original response would be sent RELIABLE in an INVITE transaction
 */
enum xmittype {
	XMIT_CRITICAL = 2,    /*!< Transmit critical SIP message reliably, with re-transmits.
	                       *   If it fails, it's critical and will cause a teardown of the session */
	XMIT_RELIABLE = 1,    /*!< Transmit SIP message reliably, with re-transmits */
	XMIT_UNRELIABLE = 0,  /*!< Transmit SIP message without bothering with re-transmits */
};

/*! \brief Results from the parse_register() function */
enum parse_register_result {
	PARSE_REGISTER_DENIED,
	PARSE_REGISTER_FAILED,
	PARSE_REGISTER_UPDATE,
	PARSE_REGISTER_QUERY,
};

/*! \brief Type of subscription, based on the packages we do support, see \ref subscription_types */
enum subscriptiontype {
	NONE = 0,
	XPIDF_XML,
	DIALOG_INFO_XML,
	CPIM_PIDF_XML,
	PIDF_XML,
	MWI_NOTIFICATION,
	CALL_COMPLETION,
};

/*! \brief The number of media types in enum \ref media_type below. */
#define OFFERED_MEDIA_COUNT	4

/*! \brief Media types generate different "dummy answers" for not accepting the offer of 
	a media stream. We need to add definitions for each RTP profile. Secure RTP is not
	the same as normal RTP and will require a new definition */
enum media_type {
	SDP_AUDIO,   /*!< RTP/AVP Audio */
	SDP_VIDEO,   /*!< RTP/AVP Video */
	SDP_IMAGE,   /*!< Image udptl, not TCP or RTP */
	SDP_TEXT,    /*!< RTP/AVP Realtime Text */
};

/*! \brief Authentication types - proxy or www authentication
 *  \note Endpoints, like Asterisk, should always use WWW authentication to
 *  allow multiple authentications in the same call - to the proxy and
 *  to the end point.
 */
enum sip_auth_type {
	PROXY_AUTH = 407,
	WWW_AUTH = 401,
};

/*! \brief Result from get_destination function */
enum sip_get_dest_result {
	SIP_GET_DEST_PICKUP_EXTEN_FOUND = 1,
	SIP_GET_DEST_EXTEN_FOUND = 0,
	SIP_GET_DEST_EXTEN_NOT_FOUND = -1,
	SIP_GET_DEST_REFUSED = -2,
	SIP_GET_DEST_INVALID_URI = -3,
};

/*! \brief Authentication result from check_auth* functions */
enum check_auth_result {
	AUTH_DONT_KNOW = -100,	/*!< no result, need to check further */
		/* XXX maybe this is the same as AUTH_NOT_FOUND */
	AUTH_SUCCESSFUL = 0,
	AUTH_CHALLENGE_SENT = 1,
	AUTH_SECRET_FAILED = -1,
	AUTH_USERNAME_MISMATCH = -2,
	AUTH_NOT_FOUND = -3,	/*!< returned by register_verify */
	AUTH_FAKE_AUTH = -4,
	AUTH_UNKNOWN_DOMAIN = -5,
	AUTH_PEER_NOT_DYNAMIC = -6,
	AUTH_ACL_FAILED = -7,
	AUTH_BAD_TRANSPORT = -8,
	AUTH_RTP_FAILED = 9,
};

/*! \brief States for outbound registrations (with register= lines in sip.conf */
enum sipregistrystate {
	REG_STATE_UNREGISTERED = 0,	/*!< We are not registered
		 *  \note Initial state. We should have a timeout scheduled for the initial
		 * (or next) registration transmission, calling sip_reregister
		 */

	REG_STATE_REGSENT,	/*!< Registration request sent
		 * \note sent initial request, waiting for an ack or a timeout to
		 * retransmit the initial request.
		*/

	REG_STATE_AUTHSENT,	/*!< We have tried to authenticate
		 * \note entered after transmit_register with auth info,
		 * waiting for an ack.
		 */

	REG_STATE_REGISTERED,	/*!< Registered and done */

	REG_STATE_REJECTED,	/*!< Registration rejected
		 * \note only used when the remote party has an expire larger than
		 * our max-expire. This is a final state from which we do not
		 * recover (not sure how correctly).
		 */

	REG_STATE_TIMEOUT,	/*!< Registration timed out
		* \note XXX unused */

	REG_STATE_NOAUTH,	/*!< We have no accepted credentials
		 * \note fatal - no chance to proceed */

	REG_STATE_FAILED,	/*!< Registration failed after several tries
		 * \note fatal - no chance to proceed */
};

/*! \brief Modes in which Asterisk can be configured to run SIP Session-Timers */
enum st_mode {
        SESSION_TIMER_MODE_INVALID = 0, /*!< Invalid value */
        SESSION_TIMER_MODE_ACCEPT,      /*!< Honor inbound Session-Timer requests */
        SESSION_TIMER_MODE_ORIGINATE,   /*!< Originate outbound and honor inbound requests */
        SESSION_TIMER_MODE_REFUSE       /*!< Ignore inbound Session-Timers requests */
};

/*! \brief The entity playing the refresher role for Session-Timers */
enum st_refresher {
        SESSION_TIMER_REFRESHER_AUTO,    /*!< Negotiated                      */
        SESSION_TIMER_REFRESHER_UAC,     /*!< Session is refreshed by the UAC */
        SESSION_TIMER_REFRESHER_UAS      /*!< Session is refreshed by the UAS */
};

/*! \brief Define some implemented SIP transports
	\note Asterisk does not support SCTP or UDP/DTLS
*/
enum sip_transport {
	SIP_TRANSPORT_UDP = 1,         /*!< Unreliable transport for SIP, needs retransmissions */
	SIP_TRANSPORT_TCP = 1 << 1,    /*!< Reliable, but unsecure */
	SIP_TRANSPORT_TLS = 1 << 2,    /*!< TCP/TLS - reliable and secure transport for signalling */
};

/*! \brief States whether a SIP message can create a dialog in Asterisk. */
enum can_create_dialog {
	CAN_NOT_CREATE_DIALOG,
	CAN_CREATE_DIALOG,
	CAN_CREATE_DIALOG_UNSUPPORTED_METHOD,
};

/*! \brief SIP Request methods known by Asterisk
 *
 * \note Do _NOT_ make any changes to this enum, or the array following it;
 * if you think you are doing the right thing, you are probably
 * not doing the right thing. If you think there are changes
 * needed, get someone else to review them first _before_
 * submitting a patch. If these two lists do not match properly
 * bad things will happen.
 */
enum sipmethod {
	SIP_UNKNOWN,    /*!< Unknown response */
	SIP_RESPONSE,   /*!< Not request, response to outbound request */
	SIP_REGISTER,   /*!< Registration to the mothership, tell us where you are located */
	SIP_OPTIONS,    /*!< Check capabilities of a device, used for "ping" too */
	SIP_NOTIFY,     /*!< Status update, Part of the event package standard, result of a SUBSCRIBE or a REFER */
	SIP_INVITE,     /*!< Set up a session */
	SIP_ACK,        /*!< End of a three-way handshake started with INVITE. */
	SIP_PRACK,      /*!< Reliable pre-call signalling. Not supported in Asterisk. */
	SIP_BYE,        /*!< End of a session */
	SIP_REFER,      /*!< Refer to another URI (transfer) */
	SIP_SUBSCRIBE,  /*!< Subscribe for updates (voicemail, session status, device status, presence) */
	SIP_MESSAGE,    /*!< Text messaging */
	SIP_UPDATE,     /*!< Update a dialog. We can send UPDATE; but not accept it */
	SIP_INFO,       /*!< Information updates during a session */
	SIP_CANCEL,     /*!< Cancel an INVITE */
	SIP_PUBLISH,    /*!< Not supported in Asterisk */
	SIP_PING,       /*!< Not supported at all, no standard but still implemented out there */
};

/*! \brief Settings for the 'notifycid' option, see sip.conf.sample for details. */
enum notifycid_setting {
	DISABLED       = 0,
	ENABLED        = 1,
	IGNORE_CONTEXT = 2,
};

/*! \brief Modes for SIP domain handling in the PBX */
enum domain_mode {
	SIP_DOMAIN_AUTO,      /*!< This domain is auto-configured */
	SIP_DOMAIN_CONFIG,    /*!< This domain is from configuration */
};

/*! \brief debugging state
 * We store separately the debugging requests from the config file
 * and requests from the CLI. Debugging is enabled if either is set
 * (which means that if sipdebug is set in the config file, we can
 * only turn it off by reloading the config).
 */
enum sip_debug_e {
	sip_debug_none = 0,
	sip_debug_config = 1,
	sip_debug_console = 2,
};

/*! \brief T38 States for a call */
enum t38state {
	T38_DISABLED = 0,     /*!< Not enabled */
	T38_LOCAL_REINVITE,   /*!< Offered from local - REINVITE */
	T38_PEER_REINVITE,    /*!< Offered from peer - REINVITE */
	T38_ENABLED,          /*!< Negotiated (enabled) */
	T38_REJECTED          /*!< Refused */
};

/*! \brief Parameters to know status of transfer */
enum referstatus {
	REFER_IDLE,           /*!< No REFER is in progress */
	REFER_SENT,           /*!< Sent REFER to transferee */
	REFER_RECEIVED,       /*!< Received REFER from transferrer */
	REFER_CONFIRMED,      /*!< Refer confirmed with a 100 TRYING (unused) */
	REFER_ACCEPTED,       /*!< Accepted by transferee */
	REFER_RINGING,        /*!< Target Ringing */
	REFER_200OK,          /*!< Answered by transfer target */
	REFER_FAILED,         /*!< REFER declined - go on */
	REFER_NOAUTH          /*!< We had no auth for REFER */
};

enum sip_peer_type {
	SIP_TYPE_PEER = (1 << 0),
	SIP_TYPE_USER = (1 << 1),
};

enum t38_action_flag {
	SDP_T38_NONE = 0, /*!< Do not modify T38 information at all */
	SDP_T38_INITIATE, /*!< Remote side has requested T38 with us */
	SDP_T38_ACCEPT,   /*!< Remote side accepted our T38 request */
};

enum sip_tcptls_alert {
	TCPTLS_ALERT_DATA,  /*!< \brief There is new data to be sent out */
	TCPTLS_ALERT_STOP,  /*!< \brief A request to stop the tcp_handler thread */
};


/*----------------------------------------------------------*/
/*----                    STRUCTS                       ----*/
/*----------------------------------------------------------*/

/*! \brief definition of a sip proxy server
 *
 * For outbound proxies, a sip_peer will contain a reference to a
 * dynamically allocated instance of a sip_proxy. A sip_pvt may also
 * contain a reference to a peer's outboundproxy, or it may contain
 * a reference to the sip_cfg.outboundproxy.
 */
struct sip_proxy {
	char name[MAXHOSTNAMELEN];      /*!< DNS name of domain/host or IP */
	struct ast_sockaddr ip;          /*!< Currently used IP address and port */
	int port;
	time_t last_dnsupdate;          /*!< When this was resolved */
	enum sip_transport transport;
	int force;                      /*!< If it's an outbound proxy, Force use of this outbound proxy for all outbound requests */
	/* Room for a SRV record chain based on the name */
};

/*! \brief argument for the 'show channels|subscriptions' callback. */
struct __show_chan_arg {
	int fd;
	int subscriptions;
	int numchans;   /* return value */
};

/*! \name GlobalSettings
	Global settings apply to the channel (often settings you can change in the general section
	of sip.conf
*/
/*@{*/
/*! \brief a place to store all global settings for the sip channel driver

	These are settings that will be possibly to apply on a group level later on.
	\note Do not add settings that only apply to the channel itself and can't
	      be applied to devices (trunks, services, phones)
*/
struct sip_settings {
	int peer_rtupdate;          /*!< G: Update database with registration data for peer? */
	int rtsave_sysname;         /*!< G: Save system name at registration? */
	int ignore_regexpire;       /*!< G: Ignore expiration of peer  */
	int rtautoclear;            /*!< Realtime ?? */
	int directrtpsetup;         /*!< Enable support for Direct RTP setup (no re-invites) */
	int pedanticsipchecking;    /*!< Extra checking ?  Default off */
	int autocreatepeer;         /*!< Auto creation of peers at registration? Default off. */
	int srvlookup;              /*!< SRV Lookup on or off. Default is on */
	int allowguest;             /*!< allow unauthenticated peers to connect? */
	int alwaysauthreject;       /*!< Send 401 Unauthorized for all failing requests */
	int auth_options_requests;  /*!< Authenticate OPTIONS requests */
	int auth_message_requests;  /*!< Authenticate MESSAGE requests */
	int accept_outofcall_message; /*!< Accept MESSAGE outside of a call */
	int compactheaders;         /*!< send compact sip headers */
	int allow_external_domains; /*!< Accept calls to external SIP domains? */
	int callevents;             /*!< Whether we send manager events or not */
	int regextenonqualify;      /*!< Whether to add/remove regexten when qualifying peers */
	int legacy_useroption_parsing; /*!< Whether to strip useroptions in URI via semicolons */
	int matchexternaddrlocally;   /*!< Match externaddr/externhost setting against localnet setting */
	char regcontext[AST_MAX_CONTEXT];  /*!< Context for auto-extensions */
	char messagecontext[AST_MAX_CONTEXT];  /*!< Default context for out of dialog msgs. */
	unsigned int disallowed_methods;   /*!< methods that we should never try to use */
	int notifyringing;          /*!< Send notifications on ringing */
	int notifyhold;             /*!< Send notifications on hold */
	enum notifycid_setting notifycid;  /*!< Send CID with ringing notifications */
	enum transfermodes allowtransfer;  /*!< SIP Refer restriction scheme */
	int allowsubscribe;         /*!< Flag for disabling ALL subscriptions, this is FALSE only if all peers are FALSE
	                                 the global setting is in globals_flags[1] */
	char realm[MAXHOSTNAMELEN]; /*!< Default realm */
	int domainsasrealm;         /*!< Use domains lists as realms */
	struct sip_proxy outboundproxy; /*!< Outbound proxy */
	char default_context[AST_MAX_CONTEXT];
	char default_subscribecontext[AST_MAX_CONTEXT];
	struct ast_ha *contact_ha;  /*! \brief Global list of addresses dynamic peers are not allowed to use */
	struct ast_format_cap *caps; /*!< Supported codecs */
	int tcp_enabled;
	int default_max_forwards;    /*!< Default max forwards (SIP Anti-loop) */
};

/*! \brief The SIP socket definition */
struct sip_socket {
	enum sip_transport type;  /*!< UDP, TCP or TLS */
	int fd;                   /*!< Filed descriptor, the actual socket */
	uint16_t port;
	struct ast_tcptls_session_instance *tcptls_session;  /* If tcp or tls, a socket manager */
};

/*! \brief sip_request: The data grabbed from the UDP socket
 *
 * \verbatim
 * Incoming messages: we first store the data from the socket in data[],
 * adding a trailing \0 to make string parsing routines happy.
 * Then call parse_request() and req.method = find_sip_method();
 * to initialize the other fields. The \r\n at the end of each line is
 * replaced by \0, so that data[] is not a conforming SIP message anymore.
 * After this processing, rlPart1 is set to non-NULL to remember
 * that we can run get_header() on this kind of packet.
 *
 * parse_request() splits the first line as follows:
 * Requests have in the first line      method uri SIP/2.0
 *      rlPart1 = method; rlPart2 = uri;
 * Responses have in the first line     SIP/2.0 NNN description
 *      rlPart1 = SIP/2.0; rlPart2 = NNN + description;
 *
 * For outgoing packets, we initialize the fields with init_req() or init_resp()
 * (which fills the first line to "METHOD uri SIP/2.0" or "SIP/2.0 code text"),
 * and then fill the rest with add_header() and add_line().
 * The \r\n at the end of the line are still there, so the get_header()
 * and similar functions don't work on these packets.
 * \endverbatim
 */
struct sip_request {
	ptrdiff_t rlPart1;      /*!< Offset of the SIP Method Name or "SIP/2.0" protocol version */
	ptrdiff_t rlPart2;      /*!< Offset of the Request URI or Response Status */
	int headers;            /*!< # of SIP Headers */
	int method;             /*!< Method of this request */
	int lines;              /*!< Body Content */
	unsigned int sdp_start; /*!< the line number where the SDP begins */
	unsigned int sdp_count; /*!< the number of lines of SDP */
	char debug;             /*!< print extra debugging if non zero */
	char has_to_tag;        /*!< non-zero if packet has To: tag */
	char ignore;            /*!< if non-zero This is a re-transmit, ignore it */
	char authenticated;     /*!< non-zero if this request was authenticated */
	ptrdiff_t header[SIP_MAX_HEADERS]; /*!< Array of offsets into the request string of each SIP header*/
	ptrdiff_t line[SIP_MAX_LINES];     /*!< Array of offsets into the request string of each SDP line*/
	struct ast_str *data;	
	struct ast_str *content;
	/* XXX Do we need to unref socket.ser when the request goes away? */
	struct sip_socket socket;          /*!< The socket used for this request */
	AST_LIST_ENTRY(sip_request) next;
};

/* \brief given a sip_request and an offset, return the char * that resides there
 *
 * It used to be that rlPart1, rlPart2, and the header and line arrays were character
 * pointers. They are now offsets into the ast_str portion of the sip_request structure.
 * To avoid adding a bunch of redundant pointer arithmetic to the code, this macro is
 * provided to retrieve the string at a particular offset within the request's buffer
 */
#define REQ_OFFSET_TO_STR(req,offset) (ast_str_buffer((req)->data) + ((req)->offset))

/*! \brief structure used in transfers */
struct sip_dual {
	struct ast_channel *chan1;   /*!< First channel involved */
	struct ast_channel *chan2;   /*!< Second channel involved */
	struct sip_request req;      /*!< Request that caused the transfer (REFER) */
	int seqno;                   /*!< Sequence number */
	const char *parkexten;
};

/*! \brief Parameters to the transmit_invite function */
struct sip_invite_param {
	int addsipheaders;          /*!< Add extra SIP headers */
	const char *uri_options;    /*!< URI options to add to the URI */
	const char *vxml_url;       /*!< VXML url for Cisco phones */
	char *auth;                 /*!< Authentication */
	char *authheader;           /*!< Auth header */
	enum sip_auth_type auth_type;  /*!< Authentication type */
	const char *replaces;       /*!< Replaces header for call transfers */
	int transfer;               /*!< Flag - is this Invite part of a SIP transfer? (invite/replaces) */
};

/*! \brief Structure to save routing information for a SIP session */
struct sip_route {
	struct sip_route *next;
	char hop[0];
};

/*! \brief Structure to store Via information */
struct sip_via {
	char *via;
	const char *protocol;
	const char *sent_by;
	const char *branch;
	const char *maddr;
	unsigned int port;
	unsigned char ttl;
};

/*! \brief Domain data structure.
	\note In the future, we will connect this to a configuration tree specific
	for this domain
*/
struct domain {
	char domain[MAXHOSTNAMELEN];       /*!< SIP domain we are responsible for */
	char context[AST_MAX_EXTENSION];   /*!< Incoming context for this domain */
	enum domain_mode mode;             /*!< How did we find this domain? */
	AST_LIST_ENTRY(domain) list;       /*!< List mechanics */
};

/*! \brief sip_history: Structure for saving transactions within a SIP dialog */
struct sip_history {
	AST_LIST_ENTRY(sip_history) list;
	char event[0];	/* actually more, depending on needs */
};

/*! \brief sip_auth: Credentials for authentication to other SIP services */
struct sip_auth {
	AST_LIST_ENTRY(sip_auth) node;
	char realm[AST_MAX_EXTENSION];  /*!< Realm in which these credentials are valid */
	char username[256];             /*!< Username */
	char secret[256];               /*!< Secret */
	char md5secret[256];            /*!< MD5Secret */
};

/*! \brief Container of SIP authentication credentials. */
struct sip_auth_container {
	AST_LIST_HEAD_NOLOCK(, sip_auth) list;
};

/*! \brief T.38 channel settings (at some point we need to make this alloc'ed */
struct t38properties {
	enum t38state state;            /*!< T.38 state */
	struct ast_control_t38_parameters our_parms;
	struct ast_control_t38_parameters their_parms;
};

/*! \brief generic struct to map between strings and integers.
 * Fill it with x-s pairs, terminate with an entry with s = NULL;
 * Then you can call map_x_s(...) to map an integer to a string,
 * and map_s_x() for the string -> integer mapping.
 */
struct _map_x_s {
	int x;
	const char *s;
};

/*! \brief Structure to handle SIP transfers. Dynamically allocated when needed
	\note OEJ: Should be moved to string fields */
struct sip_refer {
	char refer_to[AST_MAX_EXTENSION];             /*!< Place to store REFER-TO extension */
	char refer_to_domain[AST_MAX_EXTENSION];      /*!< Place to store REFER-TO domain */
	char refer_to_urioption[AST_MAX_EXTENSION];   /*!< Place to store REFER-TO uri options */
	char refer_to_context[AST_MAX_EXTENSION];     /*!< Place to store REFER-TO context */
	char referred_by[AST_MAX_EXTENSION];          /*!< Place to store REFERRED-BY extension */
	char referred_by_name[AST_MAX_EXTENSION];     /*!< Place to store REFERRED-BY extension */
	char refer_contact[AST_MAX_EXTENSION];        /*!< Place to store Contact info from a REFER extension */
	char replaces_callid[SIPBUFSIZE];             /*!< Replace info: callid */
	char replaces_callid_totag[SIPBUFSIZE/2];     /*!< Replace info: to-tag */
	char replaces_callid_fromtag[SIPBUFSIZE/2];   /*!< Replace info: from-tag */
	struct sip_pvt *refer_call;                   /*!< Call we are referring. This is just a reference to a
	                                               * dialog owned by someone else, so we should not destroy
	                                               * it when the sip_refer object goes.
	                                               */
	int attendedtransfer;                         /*!< Attended or blind transfer? */
	int localtransfer;                            /*!< Transfer to local domain? */
	enum referstatus status;                      /*!< REFER status */
};

/*! \brief Struct to handle custom SIP notify requests. Dynamically allocated when needed */
struct sip_notify {
	struct ast_variable *headers;
	struct ast_str *content;
};

/*! \brief Structure that encapsulates all attributes related to running
 *   SIP Session-Timers feature on a per dialog basis.
 */
struct sip_st_dlg {
	int st_active;                     /*!< Session-Timers on/off */
	int st_interval;                   /*!< Session-Timers negotiated session refresh interval */
	int st_schedid;                    /*!< Session-Timers ast_sched scheduler id */
	enum st_refresher st_ref;          /*!< Session-Timers session refresher */
	int st_expirys;                    /*!< Session-Timers number of expirys */
	int st_active_peer_ua;             /*!< Session-Timers on/off in peer UA */
	int st_cached_min_se;              /*!< Session-Timers cached Min-SE */
	int st_cached_max_se;              /*!< Session-Timers cached Session-Expires */
	enum st_mode st_cached_mode;       /*!< Session-Timers cached M.O. */
	enum st_refresher st_cached_ref;   /*!< Session-Timers cached refresher */
	unsigned char quit_flag:1;         /*!< Stop trying to lock; just quit */
};


/*! \brief Structure that encapsulates all attributes related to configuration
 *   of SIP Session-Timers feature on a per user/peer basis.
 */
struct sip_st_cfg {
	enum st_mode st_mode_oper;    /*!< Mode of operation for Session-Timers           */
	enum st_refresher st_ref;     /*!< Session-Timer refresher                        */
	int st_min_se;                /*!< Lowest threshold for session refresh interval  */
	int st_max_se;                /*!< Highest threshold for session refresh interval */
};

/*! \brief Structure for remembering offered media in an INVITE, to make sure we reply
	to all media streams. In theory. In practise, we try our best. */
struct offered_media {
	int offered;
	char codecs[128];
};

/*! \brief Structure used for each SIP dialog, ie. a call, a registration, a subscribe.
 * Created and initialized by sip_alloc(), the descriptor goes into the list of
 * descriptors (dialoglist).
 */
struct sip_pvt {
	struct sip_pvt *next;                   /*!< Next dialog in chain */
	enum invitestates invitestate;          /*!< Track state of SIP_INVITEs */
	int method;                             /*!< SIP method that opened this dialog */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(callid);       /*!< Global CallID */
		AST_STRING_FIELD(initviabranch); /*!< The branch ID from the topmost Via header in the initial request */
		AST_STRING_FIELD(initviasentby); /*!< The sent-by from the topmost Via header in the initial request */
		AST_STRING_FIELD(randdata);     /*!< Random data */
		AST_STRING_FIELD(accountcode);  /*!< Account code */
		AST_STRING_FIELD(realm);        /*!< Authorization realm */
		AST_STRING_FIELD(nonce);        /*!< Authorization nonce */
		AST_STRING_FIELD(opaque);       /*!< Opaque nonsense */
		AST_STRING_FIELD(qop);          /*!< Quality of Protection, since SIP wasn't complicated enough yet. */
		AST_STRING_FIELD(domain);       /*!< Authorization domain */
		AST_STRING_FIELD(from);         /*!< The From: header */
		AST_STRING_FIELD(useragent);    /*!< User agent in SIP request */
		AST_STRING_FIELD(exten);        /*!< Extension where to start */
		AST_STRING_FIELD(context);      /*!< Context for this call */
		AST_STRING_FIELD(messagecontext); /*!< Default context for outofcall messages. */
		AST_STRING_FIELD(subscribecontext); /*!< Subscribecontext */
		AST_STRING_FIELD(subscribeuri); /*!< Subscribecontext */
		AST_STRING_FIELD(fromdomain);   /*!< Domain to show in the from field */
		AST_STRING_FIELD(fromuser);     /*!< User to show in the user field */
		AST_STRING_FIELD(fromname);     /*!< Name to show in the user field */
		AST_STRING_FIELD(tohost);       /*!< Host we should put in the "to" field */
		AST_STRING_FIELD(todnid);       /*!< DNID of this call (overrides host) */
		AST_STRING_FIELD(language);     /*!< Default language for this call */
		AST_STRING_FIELD(mohinterpret); /*!< MOH class to use when put on hold */
		AST_STRING_FIELD(mohsuggest);   /*!< MOH class to suggest when putting a peer on hold */
		AST_STRING_FIELD(rdnis);        /*!< Referring DNIS */
		AST_STRING_FIELD(redircause);   /*!< Referring cause */
		AST_STRING_FIELD(theirtag);     /*!< Their tag */
		AST_STRING_FIELD(username);     /*!< [user] name */
		AST_STRING_FIELD(peername);     /*!< [peer] name, not set if [user] */
		AST_STRING_FIELD(authname);     /*!< Who we use for authentication */
		AST_STRING_FIELD(uri);          /*!< Original requested URI */
		AST_STRING_FIELD(okcontacturi); /*!< URI from the 200 OK on INVITE */
		AST_STRING_FIELD(peersecret);   /*!< Password */
		AST_STRING_FIELD(peermd5secret);
		AST_STRING_FIELD(cid_num);      /*!< Caller*ID number */
		AST_STRING_FIELD(cid_name);     /*!< Caller*ID name */
		AST_STRING_FIELD(cid_tag);      /*!< Caller*ID tag */
		AST_STRING_FIELD(mwi_from);     /*!< Name to place in the From header in outgoing NOTIFY requests */
		AST_STRING_FIELD(fullcontact);  /*!< The Contact: that the UA registers with us */
		                                /* we only store the part in <brackets> in this field. */
		AST_STRING_FIELD(our_contact);  /*!< Our contact header */
		AST_STRING_FIELD(url);          /*!< URL to be sent with next message to peer */
		AST_STRING_FIELD(parkinglot);   /*!< Parkinglot */
		AST_STRING_FIELD(engine);       /*!< RTP engine to use */
		AST_STRING_FIELD(dialstring);   /*!< The dialstring used to call this SIP endpoint */
		AST_STRING_FIELD(msg_body);     /*!< Text for a MESSAGE body */
	);
	char via[128];                          /*!< Via: header */
	int maxforwards;                        /*!< SIP Loop prevention */
	struct sip_socket socket;               /*!< The socket used for this dialog */
	unsigned int ocseq;                     /*!< Current outgoing seqno */
	unsigned int icseq;                     /*!< Current incoming seqno */
	unsigned int init_icseq;                /*!< Initial incoming seqno from first request */
	ast_group_t callgroup;                  /*!< Call group */
	ast_group_t pickupgroup;                /*!< Pickup group */
	int lastinvite;                         /*!< Last Cseq of invite */
	struct ast_flags flags[3];              /*!< SIP_ flags */

	/* boolean flags that don't belong in flags */
	unsigned short do_history:1;          /*!< Set if we want to record history */
	unsigned short alreadygone:1;         /*!< the peer has sent a message indicating termination of the dialog */
	unsigned short needdestroy:1;         /*!< this dialog needs to be destroyed by the monitor thread */
	unsigned short final_destruction_scheduled:1; /*!< final dialog destruction is scheduled. Keep dialog
	                                               *   around until then to handle retransmits. */
	unsigned short outgoing_call:1;       /*!< this is an outgoing call */
	unsigned short answered_elsewhere:1;  /*!< This call is cancelled due to answer on another channel */
	unsigned short novideo:1;             /*!< Didn't get video in invite, don't offer */
	unsigned short notext:1;              /*!< Text not supported  (?) */
	unsigned short session_modify:1;      /*!< Session modification request true/false  */
	unsigned short route_persistent:1;    /*!< Is this the "real" route? */
	unsigned short autoframing:1;         /*!< Whether to use our local configuration for frame sizes (off)
	                                       *   or respect the other endpoint's request for frame sizes (on)
	                                       *   for incoming calls
	                                       */
	unsigned short req_secure_signaling:1;/*!< Whether we are required to have secure signaling or not */
	char tag[11];                     /*!< Our tag for this session */
	int timer_t1;                     /*!< SIP timer T1, ms rtt */
	int timer_b;                      /*!< SIP timer B, ms */
	unsigned int sipoptions;          /*!< Supported SIP options on the other end */
	unsigned int reqsipoptions;       /*!< Required SIP options on the other end */
	struct ast_codec_pref prefs;      /*!< codec prefs */
	struct ast_format_cap *caps;             /*!< Special capability (codec) */
	struct ast_format_cap *jointcaps;        /*!< Supported capability at both ends (codecs) */
	struct ast_format_cap *peercaps;         /*!< Supported peer capability */
	struct ast_format_cap *redircaps;        /*!< Redirect codecs */
	struct ast_format_cap *prefcaps;         /*!< Preferred codec (outbound only) */
	int noncodeccapability;	          /*!< DTMF RFC2833 telephony-event */
	int jointnoncodeccapability;      /*!< Joint Non codec capability */
	int maxcallbitrate;               /*!< Maximum Call Bitrate for Video Calls */	
	int t38_maxdatagram;              /*!< T.38 FaxMaxDatagram override */
	int request_queue_sched_id;       /*!< Scheduler ID of any scheduled action to process queued requests */
	int provisional_keepalive_sched_id;   /*!< Scheduler ID for provisional responses that need to be sent out to avoid cancellation */
	const char *last_provisional;         /*!< The last successfully transmitted provisonal response message */
	int authtries;                        /*!< Times we've tried to authenticate */
	struct sip_proxy *outboundproxy;      /*!< Outbound proxy for this dialog. Use ref_proxy to set this instead of setting it directly*/
	struct t38properties t38;             /*!< T38 settings */
	struct ast_sockaddr udptlredirip;     /*!< Where our T.38 UDPTL should be going if not to us */
	struct ast_udptl *udptl;              /*!< T.38 UDPTL session */
	int callingpres;                      /*!< Calling presentation */
	int expiry;                         /*!< How long we take to expire */
	int sessionversion;                 /*!< SDP Session Version */
	int sessionid;                      /*!< SDP Session ID */
	long branch;                        /*!< The branch identifier of this session */
	long invite_branch;                 /*!< The branch used when we sent the initial INVITE */
	int64_t sessionversion_remote;      /*!< Remote UA's SDP Session Version */
	unsigned int portinuri:1;           /*!< Non zero if a port has been specified, will also disable srv lookups */
	struct ast_sockaddr sa;              /*!< Our peer */
	struct ast_sockaddr redirip;         /*!< Where our RTP should be going if not to us */
	struct ast_sockaddr vredirip;        /*!< Where our Video RTP should be going if not to us */
	struct ast_sockaddr tredirip;        /*!< Where our Text RTP should be going if not to us */
	time_t lastrtprx;                   /*!< Last RTP received */
	time_t lastrtptx;                   /*!< Last RTP sent */
	int rtptimeout;                     /*!< RTP timeout time */
	struct ast_ha *directmediaha;		/*!< Which IPs are allowed to interchange direct media with this peer - copied from sip_peer */
	struct ast_sockaddr recv;            /*!< Received as */
	struct ast_sockaddr ourip;           /*!< Our IP (as seen from the outside) */
	enum transfermodes allowtransfer;   /*!< REFER: restriction scheme */
	struct ast_channel *owner;          /*!< Who owns us (if we have an owner) */
	struct sip_route *route;            /*!< Head of linked list of routing steps (fm Record-Route) */
	struct sip_notify *notify;          /*!< Custom notify type */
	struct sip_auth_container *peerauth;/*!< Realm authentication credentials */
	int noncecount;                     /*!< Nonce-count */
	unsigned int stalenonce:1;          /*!< Marks the current nonce as responded too */
	char lastmsg[256];                  /*!< Last Message sent/received */
	int amaflags;                       /*!< AMA Flags */
	int pendinginvite;    /*!< Any pending INVITE or state NOTIFY (in subscribe pvt's) ? (seqno of this) */
	int glareinvite;      /*!< A invite received while a pending invite is already present is stored here.  Its seqno is the
	                           value. Since this glare invite's seqno is not the same as the pending invite's, it must be
	                           held in order to properly process acknowledgements for our 491 response. */
	struct sip_request initreq;         /*!< Latest request that opened a new transaction
	                                         within this dialog.
	                                         NOT the request that opened the dialog */

	int initid;                         /*!< Auto-congest ID if appropriate (scheduler) */
	int waitid;                         /*!< Wait ID for scheduler after 491 or other delays */
	int autokillid;                     /*!< Auto-kill ID (scheduler) */
	int t38id;                          /*!< T.38 Response ID */
	struct sip_refer *refer;            /*!< REFER: SIP transfer data structure */
	enum subscriptiontype subscribed;   /*!< SUBSCRIBE: Is this dialog a subscription?  */
	int stateid;                        /*!< SUBSCRIBE: ID for devicestate subscriptions */
	int laststate;                      /*!< SUBSCRIBE: Last known extension state */
	int dialogver;                      /*!< SUBSCRIBE: Version for subscription dialog-info */

	struct ast_dsp *dsp;                /*!< Inband DTMF or Fax CNG tone Detection dsp */

	struct sip_peer *relatedpeer;       /*!< If this dialog is related to a peer, which one
	                                         Used in peerpoke, mwi subscriptions */
	struct sip_registry *registry;      /*!< If this is a REGISTER dialog, to which registry */
	struct ast_rtp_instance *rtp;       /*!< RTP Session */
	struct ast_rtp_instance *vrtp;      /*!< Video RTP session */
	struct ast_rtp_instance *trtp;      /*!< Text RTP session */
	struct sip_pkt *packets;            /*!< Packets scheduled for re-transmission */
	struct sip_history_head *history;   /*!< History of this SIP dialog */
	size_t history_entries;             /*!< Number of entires in the history */
	struct ast_variable *chanvars;      /*!< Channel variables to set for inbound call */
	AST_LIST_HEAD_NOLOCK(request_queue, sip_request) request_queue; /*!< Requests that arrived but could not be processed immediately */
	struct sip_invite_param *options;   /*!< Options for INVITE */
	struct sip_st_dlg *stimer;          /*!< SIP Session-Timers */
	struct sip_srtp *srtp;              /*!< Structure to hold Secure RTP session data for audio */
	struct sip_srtp *vsrtp;             /*!< Structure to hold Secure RTP session data for video */
	struct sip_srtp *tsrtp;             /*!< Structure to hold Secure RTP session data for text */

	int red;                            /*!< T.140 RTP Redundancy */
	int hangupcause;                    /*!< Storage of hangupcause copied from our owner before we disconnect from the AST channel (only used at hangup) */

	struct sip_subscription_mwi *mwi;   /*!< If this is a subscription MWI dialog, to which subscription */
	/*! The SIP methods supported by this peer. We get this information from the Allow header of the first
	 * message we receive from an endpoint during a dialog.
	 */
	unsigned int allowed_methods;
	/*! Some peers are not trustworthy with their Allow headers, and so we need to override their wicked
	 * ways through configuration. This is a copy of the peer's disallowed_methods, so that we can apply them
	 * to the sip_pvt at various stages of dialog establishment
	 */
	unsigned int disallowed_methods;
	/*! When receiving an SDP offer, it is important to take note of what media types were offered.
	 * By doing this, even if we don't want to answer a particular media stream with something meaningful, we can
	 * still put an m= line in our answer with the port set to 0.
	 *
	 * The reason for the length being 4 (OFFERED_MEDIA_COUNT) is that in this branch of Asterisk, the only media types supported are
	 * image, audio, text, and video. Therefore we need to keep track of which types of media were offered.
	 * Note that secure RTP defines new types of SDP media.
	 *
	 * If we wanted to be 100% correct, we would keep a list of all media streams offered. That way we could respond
	 * even to unknown media types, and we could respond to multiple streams of the same type. Such large-scale changes
	 * are not a good idea for released branches, though, so we're compromising by just making sure that for the common cases:
	 * audio and video, audio and T.38, and audio and text, we give the appropriate response to both media streams.
	 *
	 * The large-scale changes would be a good idea for implementing during an SDP rewrite.
	 */
	struct offered_media offered_media[OFFERED_MEDIA_COUNT];
	struct ast_cc_config_params *cc_params;
	struct sip_epa_entry *epa_entry;
	int fromdomainport;                 /*!< Domain port to show in from field */
};

/*! \brief sip packet - raw format for outbound packets that are sent or scheduled for transmission
 * Packets are linked in a list, whose head is in the struct sip_pvt they belong to.
 * Each packet holds a reference to the parent struct sip_pvt.
 * This structure is allocated in __sip_reliable_xmit() and only for packets that
 * require retransmissions.
 */
struct sip_pkt {
	struct sip_pkt *next;     /*!< Next packet in linked list */
	int retrans;              /*!< Retransmission number */
	int method;               /*!< SIP method for this packet */
	int seqno;                /*!< Sequence number */
	char is_resp;             /*!< 1 if this is a response packet (e.g. 200 OK), 0 if it is a request */
	char is_fatal;            /*!< non-zero if there is a fatal error */
	int response_code;        /*!< If this is a response, the response code */
	struct sip_pvt *owner;    /*!< Owner AST call */
	int retransid;            /*!< Retransmission ID */
	int timer_a;              /*!< SIP timer A, retransmission timer */
	int timer_t1;             /*!< SIP Timer T1, estimated RTT or 500 ms */
	struct timeval time_sent;  /*!< When pkt was sent */
	int64_t retrans_stop_time; /*!< Time in ms after 'now' that retransmission must stop */
	int retrans_stop;         /*!< Timeout is reached, stop retransmission  */
	struct ast_str *data;
};

/*!
 * \brief A peer's mailbox
 *
 * We could use STRINGFIELDS here, but for only two strings, it seems like
 * too much effort ...
 */
struct sip_mailbox {
	/*! Associated MWI subscription */
	struct ast_event_sub *event_sub;
	AST_LIST_ENTRY(sip_mailbox) entry;
	unsigned int delme:1;
	char *context;
	char mailbox[2];
};

/*! \brief Structure for SIP peer data, we place calls to peers if registered  or fixed IP address (host)
*/
/* XXX field 'name' must be first otherwise sip_addrcmp() will fail, as will astobj2 hashing of the structure */
struct sip_peer {
	char name[80];                          /*!< the unique name of this object */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(secret);       /*!< Password for inbound auth */
		AST_STRING_FIELD(md5secret);    /*!< Password in MD5 */
		AST_STRING_FIELD(description);	/*!< Description of this peer */
		AST_STRING_FIELD(remotesecret); /*!< Remote secret (trunks, remote devices) */
		AST_STRING_FIELD(context);      /*!< Default context for incoming calls */
		AST_STRING_FIELD(messagecontext); /*!< Default context for outofcall messages. */
		AST_STRING_FIELD(subscribecontext); /*!< Default context for subscriptions */
		AST_STRING_FIELD(username);     /*!< Temporary username until registration */
		AST_STRING_FIELD(accountcode);  /*!< Account code */
		AST_STRING_FIELD(tohost);       /*!< If not dynamic, IP address */
		AST_STRING_FIELD(regexten);     /*!< Extension to register (if regcontext is used) */
		AST_STRING_FIELD(fromuser);     /*!< From: user when calling this peer */
		AST_STRING_FIELD(fromdomain);   /*!< From: domain when calling this peer */
		AST_STRING_FIELD(fullcontact);  /*!< Contact registered with us (not in sip.conf) */
		AST_STRING_FIELD(cid_num);      /*!< Caller ID num */
		AST_STRING_FIELD(cid_name);     /*!< Caller ID name */
		AST_STRING_FIELD(cid_tag);      /*!< Caller ID tag */
		AST_STRING_FIELD(vmexten);      /*!< Dialplan extension for MWI notify message*/
		AST_STRING_FIELD(language);     /*!<  Default language for prompts */
		AST_STRING_FIELD(mohinterpret); /*!<  Music on Hold class */
		AST_STRING_FIELD(mohsuggest);   /*!<  Music on Hold class */
		AST_STRING_FIELD(parkinglot);   /*!<  Parkinglot */
		AST_STRING_FIELD(useragent);    /*!<  User agent in SIP request (saved from registration) */
		AST_STRING_FIELD(mwi_from);     /*!< Name to place in From header for outgoing NOTIFY requests */
		AST_STRING_FIELD(engine);       /*!<  RTP Engine to use */
		AST_STRING_FIELD(unsolicited_mailbox);  /*!< Mailbox to store received unsolicited MWI NOTIFY messages information in */
		);
	struct sip_socket socket;       /*!< Socket used for this peer */
	enum sip_transport default_outbound_transport;   /*!< Peer Registration may change the default outbound transport.
	                                                     If register expires, default should be reset. to this value */
	/* things that don't belong in flags */
	unsigned short transports:3;    /*!< Transports (enum sip_transport) that are acceptable for this peer */
	unsigned short is_realtime:1;   /*!< this is a 'realtime' peer */
	unsigned short rt_fromcontact:1;/*!< copy fromcontact from realtime */
	unsigned short host_dynamic:1;  /*!< Dynamic Peers register with Asterisk */
	unsigned short selfdestruct:1;  /*!< Automatic peers need to destruct themselves */
	unsigned short the_mark:1;      /*!< moved out of ASTOBJ into struct proper; That which bears the_mark should be deleted! */
	unsigned short autoframing:1;   /*!< Whether to use our local configuration for frame sizes (off)
	                                 *   or respect the other endpoint's request for frame sizes (on)
	                                 *   for incoming calls
	                                 */
	unsigned short deprecated_username:1; /*!< If it's a realtime peer, are they using the deprecated "username" instead of "defaultuser" */
	struct sip_auth_container *auth;/*!< Realm authentication credentials */
	int amaflags;                   /*!< AMA Flags (for billing) */
	int callingpres;                /*!< Calling id presentation */
	int inUse;                      /*!< Number of calls in use */
	int inRinging;                  /*!< Number of calls ringing */
	int onHold;                     /*!< Peer has someone on hold */
	int call_limit;                 /*!< Limit of concurrent calls */
	int t38_maxdatagram;            /*!< T.38 FaxMaxDatagram override */
	int busy_level;                 /*!< Level of active channels where we signal busy */
	int maxforwards;                /*!< SIP Loop prevention */
	enum transfermodes allowtransfer;   /*! SIP Refer restriction scheme */
	struct ast_codec_pref prefs;    /*!<  codec prefs */
	int lastmsgssent;
	unsigned int sipoptions;        /*!<  Supported SIP options */
	struct ast_flags flags[3];      /*!<  SIP_ flags */

	/*! Mailboxes that this peer cares about */
	AST_LIST_HEAD_NOLOCK(, sip_mailbox) mailboxes;

	int maxcallbitrate;             /*!<  Maximum Bitrate for a video call */
	int expire;                     /*!<  When to expire this peer registration */
	struct ast_format_cap *caps;            /*!<  Codec capability */
	int rtptimeout;                 /*!<  RTP timeout */
	int rtpholdtimeout;             /*!<  RTP Hold Timeout */
	int rtpkeepalive;               /*!<  Send RTP packets for keepalive */
	ast_group_t callgroup;          /*!<  Call group */
	ast_group_t pickupgroup;        /*!<  Pickup group */
	struct sip_proxy *outboundproxy;/*!< Outbound proxy for this peer */
	struct ast_dnsmgr_entry *dnsmgr;/*!<  DNS refresh manager for peer */
	struct ast_sockaddr addr;        /*!<  IP address of peer */
	unsigned int portinuri:1;       /*!< Whether the port should be included in the URI */
	struct sip_pvt *call;           /*!<  Call pointer */
	int pokeexpire;                 /*!<  Qualification: When to expire poke (qualify= checking) */
	int lastms;                     /*!<  Qualification: How long last response took (in ms), or -1 for no response */
	int maxms;                      /*!<  Qualification: Max ms we will accept for the host to be up, 0 to not monitor */
	int qualifyfreq;                /*!<  Qualification: Qualification: How often to check for the host to be up */
	struct timeval ps;              /*!<  Qualification: Time for sending SIP OPTION in sip_pke_peer() */
	struct ast_sockaddr defaddr;     /*!<  Default IP address, used until registration */
	struct ast_ha *ha;              /*!<  Access control list */
	struct ast_ha *contactha;       /*!<  Restrict what IPs are allowed in the Contact header (for registration) */
	struct ast_ha *directmediaha;   /*!<  Restrict what IPs are allowed to interchange direct media with */
	struct ast_variable *chanvars;  /*!<  Variables to set for channel created by user */
	struct sip_pvt *mwipvt;         /*!<  Subscription for MWI */
	struct sip_st_cfg stimer;       /*!<  SIP Session-Timers */
	int timer_t1;                   /*!<  The maximum T1 value for the peer */
	int timer_b;                    /*!<  The maximum timer B (transaction timeouts) */
	int fromdomainport;             /*!<  The From: domain port */

	/*XXX Seems like we suddenly have two flags with the same content. Why? To be continued... */
	enum sip_peer_type type; /*!< Distinguish between "user" and "peer" types. This is used solely for CLI and manager commands */
	unsigned int disallowed_methods;
	struct ast_cc_config_params *cc_params;
};

/*!
 * \brief Registrations with other SIP proxies
 *
 * Created by sip_register(), the entry is linked in the 'regl' list,
 * and never deleted (other than at 'sip reload' or module unload times).
 * The entry always has a pending timeout, either waiting for an ACK to
 * the REGISTER message (in which case we have to retransmit the request),
 * or waiting for the next REGISTER message to be sent (either the initial one,
 * or once the previously completed registration one expires).
 * The registration can be in one of many states, though at the moment
 * the handling is a bit mixed.
 *
 * \todo Convert this to astobj2
 */
struct sip_registry {
	ASTOBJ_COMPONENTS_FULL(struct sip_registry,1,1);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(callid);     /*!< Global Call-ID */
		AST_STRING_FIELD(realm);      /*!< Authorization realm */
		AST_STRING_FIELD(nonce);      /*!< Authorization nonce */
		AST_STRING_FIELD(opaque);     /*!< Opaque nonsense */
		AST_STRING_FIELD(qop);        /*!< Quality of Protection, since SIP wasn't complicated enough yet. */
		AST_STRING_FIELD(authdomain); /*!< Authorization domain */
		AST_STRING_FIELD(regdomain);  /*!< Registration doamin */
		AST_STRING_FIELD(username);   /*!< Who we are registering as */
		AST_STRING_FIELD(authuser);   /*!< Who we *authenticate* as */
		AST_STRING_FIELD(hostname);   /*!< Domain or host we register to */
		AST_STRING_FIELD(secret);     /*!< Password in clear text */
		AST_STRING_FIELD(md5secret);  /*!< Password in md5 */
		AST_STRING_FIELD(callback);   /*!< Contact extension */
		AST_STRING_FIELD(peername);   /*!< Peer registering to */
	);
	enum sip_transport transport;   /*!< Transport for this registration UDP, TCP or TLS */
	int portno;                     /*!< Optional port override */
	int regdomainport;              /*!< Port override for domainport */
	int expire;                     /*!< Sched ID of expiration */
	int configured_expiry;          /*!< Configured value to use for the Expires header */
	int expiry;             /*!< Negotiated value used for the Expires header */
	int regattempts;        /*!< Number of attempts (since the last success) */
	int timeout;            /*!< sched id of sip_reg_timeout */
	int refresh;            /*!< How often to refresh */
	struct sip_pvt *call;   /*!< create a sip_pvt structure for each outbound "registration dialog" in progress */
	enum sipregistrystate regstate; /*!< Registration state (see above) */
	struct timeval regtime;         /*!< Last successful registration time */
	int callid_valid;       /*!< 0 means we haven't chosen callid for this registry yet. */
	unsigned int ocseq;     /*!< Sequence number we got to for REGISTERs for this registry */
	struct ast_dnsmgr_entry *dnsmgr;  /*!<  DNS refresh manager for register */
	struct ast_sockaddr us;  /*!< Who the server thinks we are */
	int noncecount;         /*!< Nonce-count */
	char lastmsg[256];      /*!< Last Message sent/received */
};

struct tcptls_packet {
	AST_LIST_ENTRY(tcptls_packet) entry;
	struct ast_str *data;
	size_t len;
};
/*! \brief Definition of a thread that handles a socket */
struct sip_threadinfo {
	int stop;
	int alert_pipe[2];          /*! Used to alert tcptls thread when packet is ready to be written */
	pthread_t threadid;
	struct ast_tcptls_session_instance *tcptls_session;
	enum sip_transport type;    /*!< We keep a copy of the type here so we can display it in the connection list */
	AST_LIST_HEAD_NOLOCK(, tcptls_packet) packet_q;
};

/*!
 * \brief Definition of an MWI subscription to another server
 * 
 * \todo Convert this to astobj2.
 */
struct sip_subscription_mwi {
	ASTOBJ_COMPONENTS_FULL(struct sip_subscription_mwi,1,1);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(username);     /*!< Who we are sending the subscription as */
		AST_STRING_FIELD(authuser);     /*!< Who we *authenticate* as */
		AST_STRING_FIELD(hostname);     /*!< Domain or host we subscribe to */
		AST_STRING_FIELD(secret);       /*!< Password in clear text */
		AST_STRING_FIELD(mailbox);      /*!< Mailbox store to put MWI into */
		);
	enum sip_transport transport;    /*!< Transport to use */
	int portno;                      /*!< Optional port override */
	int resub;                       /*!< Sched ID of resubscription */
	unsigned int subscribed:1;       /*!< Whether we are currently subscribed or not */
	struct sip_pvt *call;            /*!< Outbound subscription dialog */
	struct ast_dnsmgr_entry *dnsmgr; /*!< DNS refresh manager for subscription */
	struct ast_sockaddr us;           /*!< Who the server thinks we are */
};

/*!
 * SIP PUBLISH support!
 * PUBLISH support was added to chan_sip due to its use in the call-completion
 * event package. In order to suspend and unsuspend monitoring of a called party,
 * a PUBLISH message must be sent. Rather than try to hack in PUBLISH transmission
 * and reception solely for the purposes of handling call-completion-related messages,
 * an effort has been made to create a generic framework for handling PUBLISH messages.
 *
 * There are two main components to the effort, the event publication agent (EPA) and
 * the event state compositor (ESC). Both of these terms appear in RFC 3903, and the
 * implementation in Asterisk conforms to the defintions there. An EPA is a UAC that
 * transmits PUBLISH requests. An ESC is a UAS that receives PUBLISH requests and
 * acts appropriately based on the content of those requests.
 *
 * ESC:
 * The main structure in chan_sip is the event_state_compositor. There is an
 * event_state_compositor structure for each event package supported (as of Nov 2009
 * this is only the call-completion package). The structure contains data which is
 * intrinsic to the event package itself, such as the name of the package and a set
 * of callbacks for handling incoming PUBLISH requests. In addition, the
 * event_state_compositor struct contains an ao2_container of sip_esc_entries.
 *
 * A sip_esc_entry corresponds to an entity which has sent a PUBLISH to Asterisk. We are
 * able to match the incoming PUBLISH to a sip_esc_entry using the Sip-If-Match header
 * of the message. Of course, if none is present, then a new sip_esc_entry will be created.
 *
 * Once it is determined what type of PUBLISH request has come in (from RFC 3903, it may
 * be an initial, modify, refresh, or remove), then the event package-specific callbacks
 * may be called. If your event package doesn't need to take any specific action for a
 * specific PUBLISH type, it is perfectly safe to not define the callback at all. The callback
 * only needs to take care of application-specific information. If there is a problem, it is
 * up to the callback to take care of sending an appropriate 4xx or 5xx response code. In such
 * a case, the callback should return -1. This will tell the function that called the handler
 * that an appropriate error response has been sent. If the callback returns 0, however, then
 * the caller of the callback will generate a new entity tag and send a 200 OK response.
 *
 * ESC entries are reference-counted, however as an implementor of a specific event package,
 * this should be transparent, since the reference counts are handled by the general ESC
 * framework.
 *
 * EPA:
 * The event publication agent in chan_sip is structured quite a bit differently than the
 * ESC. With an ESC, an appropriate entry has to be found based on the contents of an incoming
 * PUBLISH message. With an EPA, the application interested in sending the PUBLISH can maintain
 * a reference to the appropriate EPA entry instead. Similarly, when matching a PUBLISH response
 * to an appropriate EPA entry, the sip_pvt can maintain a reference to the corresponding
 * EPA entry. The result of this train of thought is that there is no compelling reason to
 * maintain a container of these entries.
 *
 * Instead, there is only the sip_epa_entry structure. Every sip_epa_entry has an entity tag
 * that it maintains so that subsequent PUBLISH requests will be identifiable by the ESC on
 * the far end. In addition, there is a static_data field which contains information that is
 * common to all sip_epa_entries for a specific event package. This static data includes the
 * name of the event package and callbacks for handling specific responses for outgoing PUBLISHes.
 * Also, there is a field for pointing to instance-specific data. This can include the current
 * published state or other identifying information that is specific to an instance of an EPA
 * entry of a particular event package.
 *
 * When an application wishes to send a PUBLISH request, it simply will call create_epa_entry,
 * followed by transmit_publish in order to send the PUBLISH. That's all that is necessary.
 * Like with ESC entries, sip_epa_entries are reference counted. Unlike ESC entries, though,
 * sip_epa_entries reference counts have to be maintained to some degree by the application making
 * use of the sip_epa_entry. The application will acquire a reference to the EPA entry when it
 * calls create_epa_entry. When the application has finished using the EPA entry (which may not
 * be until after several PUBLISH transactions have taken place) it must use ao2_ref to decrease
 * the reference count by 1.
 */

/*!
 * \brief The states that can be represented in a SIP call-completion PUBLISH
 */
enum sip_cc_publish_state {
	/*! Closed, i.e. unavailable */
	CC_CLOSED,
	/*! Open, i.e. available */
	CC_OPEN,
};

/*!
 * \brief The states that can be represented in a SIP call-completion NOTIFY
 */
enum sip_cc_notify_state {
	/*! Queued, i.e. unavailable */
	CC_QUEUED,
	/*! Ready, i.e. available */
	CC_READY,
};

/*!
 * \brief The types of PUBLISH messages defined in RFC 3903
 */
enum sip_publish_type {
	/*!
	 * \brief Unknown
	 *
	 * \details
	 * This actually is not defined in RFC 3903. We use this as a constant
	 * to indicate that an incoming PUBLISH does not fit into any of the
	 * other categories and is thus invalid.
	 */
	SIP_PUBLISH_UNKNOWN,
	/*!
	 * \brief Initial
	 *
	 * \details
	 * The first PUBLISH sent. This will contain a non-zero Expires header
	 * as well as a body that indicates the current state of the endpoint
	 * that has sent the message. The initial PUBLISH is the only type
	 * of PUBLISH to not contain a Sip-If-Match header in it.
	 */
	SIP_PUBLISH_INITIAL,
	/*!
	 * \brief Refresh
	 *
	 * \details
	 * Used to keep a published state from expiring. This will contain a
	 * non-zero Expires header but no body since its purpose is not to
	 * update state.
	 */
	SIP_PUBLISH_REFRESH,
	/*!
	 * \brief Modify
	 *
	 * \details
	 * Used to change state from its previous value. This will contain
	 * a body updating the published state. May or may not contain an
	 * Expires header.
	 */
	SIP_PUBLISH_MODIFY,
	/*!
	 * \brief Remove
	 * 
	 * \details
	 * Used to remove published state from an ESC. This will contain
	 * an Expires header set to 0 and likely no body.
	 */
	SIP_PUBLISH_REMOVE,
};

/*!
 * Data which is the same for all instances of an EPA for a
 * particular event package
 */
struct epa_static_data {
	/*! The event type */
	enum subscriptiontype event;
	/*!
	 * The name of the event as it would
	 * appear in a SIP message
	 */
	const char *name;
	/*!
	 * The callback called when a 200 OK is received on an outbound PUBLISH
	 */
	void (*handle_ok)(struct sip_pvt *, struct sip_request *, struct sip_epa_entry *);
	/*!
	 * The callback called when an error response is received on an outbound PUBLISH
	 */
	void (*handle_error)(struct sip_pvt *, const int resp, struct sip_request *, struct sip_epa_entry *);
	/*!
	 * Destructor to call to clean up instance data
	 */
	void (*destructor)(void *instance_data);
};

/*!
 * \brief backend for an event publication agent
 */
struct epa_backend {
	const struct epa_static_data *static_data;
	AST_LIST_ENTRY(epa_backend) next;
};

struct sip_epa_entry {
	/*!
	 * When we are going to send a publish, we need to
	 * know the type of PUBLISH to send.
	 */
	enum sip_publish_type publish_type;
	/*!
	 * When we send a PUBLISH, we have to be
	 * sure to include the entity tag that we
	 * received in the previous response.
	 */
	char entity_tag[SIPBUFSIZE];
	/*!
	 * The destination to which this EPA should send
	 * PUBLISHes. This may be the name of a SIP peer
	 * or a hostname.
	 */
	char destination[SIPBUFSIZE];
	/*!
	 * The body of the most recently-sent PUBLISH message.
	 * This is useful for situations such as authentication,
	 * in which we must send a message identical to the
	 * one previously sent
	 */
	char body[SIPBUFSIZE];
	/*!
	 * Every event package has some constant data and
	 * callbacks that all instances will share. This
	 * data resides in this field.
	 */
	const struct epa_static_data *static_data;
	/*!
	 * In addition to the static data that all instances
	 * of sip_epa_entry will have, each instance will
	 * require its own instance-specific data.
	 */
	void *instance_data;
};

/*!
 * \brief Instance data for a Call completion EPA entry
 */
struct cc_epa_entry {
	/*!
	 * The core ID of the CC transaction
	 * for which this EPA entry belongs. This
	 * essentially acts as a unique identifier
	 * for the entry and is used in the hash
	 * and comparison functions
	 */
	int core_id;
	/*!
	 * We keep the last known state of the
	 * device in question handy in case
	 * it needs to be known by a third party.
	 * Also, in the case where for some reason
	 * we get asked to transmit state that we
	 * already sent, we can just ignore the
	 * request.
	 */
	enum sip_cc_publish_state current_state;
};

struct event_state_compositor;

/*!
 * \brief common ESC items for all event types
 *
 * The entity_id field serves as a means by which
 * A specific entry may be found.
 */
struct sip_esc_entry {
	/*!
	 * The name of the party who
	 * sent us the PUBLISH. This will more
	 * than likely correspond to a peer name.
	 *
	 * This field's utility isn't really that
	 * great. It's mainly just a user-recognizable
	 * handle that can be printed in debug messages.
	 */
	const char *device_name;
	/*!
	 * The event package for which this esc_entry
	 * exists. Most of the time this isn't really
	 * necessary since you'll have easy access to the
	 * ESC which contains this entry. However, in
	 * some circumstances, we won't have the ESC
	 * available.
	 */
	const char *event;
	/*!
	 * The entity ID used when corresponding
	 * with the EPA on the other side. As the
	 * ESC, we generate an entity ID for each
	 * received PUBLISH and store it in this
	 * structure.
	 */
	char entity_tag[30];
	/*!
	 * The ID for the scheduler. We schedule
	 * destruction of a sip_esc_entry when we
	 * receive a PUBLISH. The destruction is
	 * scheduled for the duration received in
	 * the Expires header.
	 */
	int sched_id;
	/*!
	 * Each ESC entry will be for a specific
	 * event type. Those entries will need to
	 * carry data which is intrinsic to the
	 * ESC entry but which is specific to
	 * the event package
	 */
	void *event_specific_data;
};

typedef int (* const esc_publish_callback)(struct sip_pvt *, struct sip_request *, struct event_state_compositor *, struct sip_esc_entry *);

/*!
 * \brief Callbacks for SIP ESCs
 *
 * \details
 * The names of the callbacks are self-explanatory. The
 * corresponding handler is called whenever the specific
 * type of PUBLISH is received.
 */
struct sip_esc_publish_callbacks {
	const esc_publish_callback initial_handler;
	const esc_publish_callback refresh_handler;
	const esc_publish_callback modify_handler;
	const esc_publish_callback remove_handler;
};

struct sip_cc_agent_pvt {
	int offer_timer_id;
	/* A copy of the original call's Call-ID.
	 * We use this as a search key when attempting
	 * to find a particular sip_pvt.
	 */
	char original_callid[SIPBUFSIZE];
	/* A copy of the exten called originally.
	 * We use this to set the proper extension
	 * to dial during the recall since the incoming
	 * request URI is one that was generated just
	 * for the recall
	 */
	char original_exten[SIPBUFSIZE];
	/* A reference to the dialog which we will
	 * be sending a NOTIFY on when it comes time
	 * to send one
	 */
	struct sip_pvt *subscribe_pvt;
	/* When we send a NOTIFY, we include a URI
	 * that should be used by the caller when he
	 * wishes to send a PUBLISH or INVITE to us.
	 * We store that URI here.
	 */
	char notify_uri[SIPBUFSIZE];
	/* When we advertise call completion to a caller,
	 * we provide a URI for the caller to use when
	 * he sends us a SUBSCRIBE. We store it for matching
	 * purposes when we receive the SUBSCRIBE from the
	 * caller.
	 */
	char subscribe_uri[SIPBUFSIZE];
	char is_available;
};

struct sip_monitor_instance {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(subscribe_uri);
		AST_STRING_FIELD(notify_uri);
		AST_STRING_FIELD(peername);
		AST_STRING_FIELD(device_name);
	);
	int core_id;
	struct sip_pvt *subscription_pvt;
	struct sip_epa_entry *suspension_entry;
};

/*!
 * \brief uri parameters
 *
 */

struct uriparams {
	char *transport;
	char *user;
	char *method;
	char *ttl;
	char *maddr;
	int lr;
};

struct contact {
	AST_LIST_ENTRY(contact) list;
	char *name;
	char *user;
	char *pass;
	char *domain;
	struct uriparams params;
	char *headers;
	char *expires;
	char *q;
};

AST_LIST_HEAD_NOLOCK(contactliststruct, contact);

/*! \brief List of well-known SIP options. If we get this in a require,
   we should check the list and answer accordingly. */
static const struct cfsip_options {
	int id;             /*!< Bitmap ID */
	int supported;      /*!< Supported by Asterisk ? */
	char * const text;  /*!< Text id, as in standard */
} sip_options[] = {	/* XXX used in 3 places */
	/* RFC3262: PRACK 100% reliability */
	{ SIP_OPT_100REL,	NOT_SUPPORTED,	"100rel" },
	/* RFC3959: SIP Early session support */
	{ SIP_OPT_EARLY_SESSION, NOT_SUPPORTED,	"early-session" },
	/* SIMPLE events:  RFC4662 */
	{ SIP_OPT_EVENTLIST,	NOT_SUPPORTED,	"eventlist" },
	/* RFC 4916- Connected line ID updates */
	{ SIP_OPT_FROMCHANGE,	NOT_SUPPORTED,	"from-change" },
	/* GRUU: Globally Routable User Agent URI's */
	{ SIP_OPT_GRUU,		NOT_SUPPORTED,	"gruu" },
	/* RFC4244 History info */
	{ SIP_OPT_HISTINFO,	NOT_SUPPORTED,	"histinfo" },
	/* RFC3911: SIP Join header support */
	{ SIP_OPT_JOIN,		NOT_SUPPORTED,	"join" },
	/* Disable the REFER subscription, RFC 4488 */
	{ SIP_OPT_NOREFERSUB,	NOT_SUPPORTED,	"norefersub" },
	/* SIP outbound - the final NAT battle - draft-sip-outbound */
	{ SIP_OPT_OUTBOUND,	NOT_SUPPORTED,	"outbound" },
	/* RFC3327: Path support */
	{ SIP_OPT_PATH,		NOT_SUPPORTED,	"path" },
	/* RFC3840: Callee preferences */
	{ SIP_OPT_PREF,		NOT_SUPPORTED,	"pref" },
	/* RFC3312: Precondition support */
	{ SIP_OPT_PRECONDITION,	NOT_SUPPORTED,	"precondition" },
	/* RFC3323: Privacy with proxies*/
	{ SIP_OPT_PRIVACY,	NOT_SUPPORTED,	"privacy" },
	/* RFC-ietf-sip-uri-list-conferencing-02.txt conference invite lists */
	{ SIP_OPT_RECLISTINV,	NOT_SUPPORTED,	"recipient-list-invite" },
	/* RFC-ietf-sip-uri-list-subscribe-02.txt - subscription lists */
	{ SIP_OPT_RECLISTSUB,	NOT_SUPPORTED,	"recipient-list-subscribe" },
	/* RFC3891: Replaces: header for transfer */
	{ SIP_OPT_REPLACES,	SUPPORTED,	"replaces" },
	/* One version of Polycom firmware has the wrong label */
	{ SIP_OPT_REPLACES,	SUPPORTED,	"replace" },
	/* RFC4412 Resource priorities */
	{ SIP_OPT_RESPRIORITY,	NOT_SUPPORTED,	"resource-priority" },
	/* RFC3329: Security agreement mechanism */
	{ SIP_OPT_SEC_AGREE,	NOT_SUPPORTED,	"sec_agree" },
	/* RFC4092: Usage of the SDP ANAT Semantics in the SIP */
	{ SIP_OPT_SDP_ANAT,	NOT_SUPPORTED,	"sdp-anat" },
	/* RFC4028: SIP Session-Timers */
	{ SIP_OPT_TIMER,	SUPPORTED,	"timer" },
	/* RFC4538: Target-dialog */
	{ SIP_OPT_TARGET_DIALOG,NOT_SUPPORTED,	"tdialog" },
};

#endif
