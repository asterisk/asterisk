/*
 * Copyright (C) 2004-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be 
 * used and copied only in accordance with the terms of this license. 
 * The text of the license may generally be found in the root 
 * directory of this installation in the COPYING file.  It 
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must 
 * maintain this copyright notice.
 *
 *****************************************************************************/
/**
 * @file ooCalls.h 
 * This file contains call management functions. 
 */
#ifndef _OOCALLS_H_
#define _OOCALLS_H_

#include "ooLogChan.h"
#include "ooCapability.h"
#include <regex.h>

#ifdef __cplusplus
extern "C" {
#endif


/** 
 * @defgroup callmgmt  Call Management
 * @{
 */
/* Flag mask values */
/* DISABLEGK is used to selectively disable gatekeeper use. For incoming calls
   DISABLEGK can be set in onReceivedSetup callback by application.
   Very useful in pbx applications where gk is used only when call is
   to or from outside pbx domian. For outgoing calls, ooMakeCallNoGk
   disables use of gk for specific call.
*/


#define OO_M_ENDSESSION_BUILT   ASN1UINTCNT(0x00800000)
#define OO_M_RELEASE_BUILT      ASN1UINTCNT(0x00400000)
#define OO_M_FASTSTARTANSWERED  ASN1UINTCNT(0x04000000)

#define OO_M_ENDPOINTCREATED    ASN1UINTCNT(0x00010000)
#define OO_M_GKROUTED           ASN1UINTCNT(0x00200000)
#define OO_M_AUTOANSWER         ASN1UINTCNT(0x00100000)
#define OO_M_TUNNELING	        ASN1UINTCNT(0x08000000)
#define OO_M_MEDIAWAITFORCONN   ASN1UINTCNT(0x20000000)
#define OO_M_FASTSTART	        ASN1UINTCNT(0x02000000)
#define OO_M_DISABLEGK          ASN1UINTCNT(0x01000000)
#define OO_M_MANUALRINGBACK     ASN1UINTCNT(0x10000000)

#define OO_M_TRYBEMASTER	ASN1UINTCNT(0x00000010)
#define OO_M_AUDIOSESSION	ASN1UINTCNT(0x00000100)
#define OO_M_DATASESSION	ASN1UINTCNT(0x00000200)
#define OO_M_T38SUPPORTED	ASN1UINTCNT(0x00000400)

/** 
 * Call states.
 */
typedef enum {
   OO_CALL_CREATED,               /*!< Call created. */
   OO_CALL_WAITING_ADMISSION,     /*!< Call waiting for admission by GK */
   OO_CALL_CONNECTING,            /*!< Call in process of connecting */
   OO_CALL_CONNECTED,             /*!< Call currently connected. */
   OO_CALL_PAUSED,                /*!< Call Paused for hold/transfer. */
   OO_CALL_CLEAR,                 /*!< Call marked for clearing */
   OO_CALL_CLEAR_RELEASERECVD,    /*!< Release command received. */
   OO_CALL_CLEAR_RELEASESENT,     /*!< Release sent */
   OO_CALL_CLEARED,               /*!< Call cleared */
   OO_CALL_REMOVED		  /* call removed */
} OOCallState;

/** 
 * H.245 session states.
 */
typedef enum {
   OO_H245SESSION_IDLE,
   OO_H245SESSION_PAUSED,
   OO_H245SESSION_ACTIVE,
   OO_H245SESSION_ENDSENT, 
   OO_H245SESSION_ENDRECVD,
   OO_H245SESSION_CLOSED
} OOH245SessionState;

/**
 * Structure to store local and remote media endpoint info for a 
 * given media type.
 */
typedef struct OOMediaInfo{
   char   dir[15]; /* transmit/receive*/
   int   cap;
   int   lMediaPort;
   int   lMediaCntrlPort;
   char  lMediaIP[2+8*4+7];
   struct OOMediaInfo *next;
} OOMediaInfo;

#define ooMediaInfo OOMediaInfo

struct OOAliases;

/**
 * Structure to hold information on a forwarded call.
 */
typedef struct OOCallFwdData {
   char ip[20];
   int port;
   struct OOAliases *aliases;
   OOBOOL fwdedByRemote; /*Set when we are being fwded by remote*/
} OOCallFwdData;      

/**
 * Structure to store information on an H.323 channel (H.225 or H.245) for 
 * a particular call.
 */
typedef struct OOH323Channel {
   OOSOCKET sock;      /*!< Socket connection for the channel */
   int      port;      /*!< Port assigned to the channel */
   DList    outQueue;  /*!< Output message queue */
} OOH323Channel;

/**
 * Structure to store information on fast start response (H.225) to 
 * reply same answer in CALL PROCEEDING, ALERTING & CONNECT.
 */
typedef struct EXTERN FastStartResponse {
   ASN1UINT n;
   ASN1DynOctStr *elem;
} FastStartResponse;

typedef struct OOH323Regex {
   regex_t regex;
   int inuse;
   ast_mutex_t lock;
} OOH323Regex;


/**
 * This structure is used to maintain all information on an active call. 
 * A list of these structures is maintained within the global endpoint 
 * structure.
 */
typedef struct OOH323CallData {
   OOCTXT               *pctxt;
   OOCTXT               *msgctxt;
   pthread_t		callThread;
   ast_cond_t		gkWait;
   ast_mutex_t		GkLock;
   ast_mutex_t		Lock;
   OOBOOL 		Monitor;
   OOBOOL		fsSent;
   OOSOCKET		CmdChan;
   OOSOCKET		cmdSock;
   ast_mutex_t*		CmdChanLock;
   char                 callToken[20]; /* ex: ooh323c_call_1 */
   char                 callType[10]; /* incoming/outgoing */
   OOCallMode           callMode;
   int			transfercap;
   ASN1USINT            callReference;
   char                 ourCallerId[256];
   H225CallIdentifier   callIdentifier;/* The call identifier for the active 
                                          call. */
   char                 *callingPartyNumber;
   char                 *calledPartyNumber; 
   H225ConferenceIdentifier confIdentifier;
   ASN1UINT             flags;
   OOCallState          callState;
   OOCallClearReason    callEndReason;
   int			q931cause;
   ASN1UINT		h225version;
   unsigned             h245ConnectionAttempts;
   OOH245SessionState   h245SessionState;
   int                  dtmfmode;
   int			dtmfcodec;
   OOMediaInfo          *mediaInfo;
   OOCallFwdData        *pCallFwdData;
   char                 localIP[2+8*4+7];/* Local IP address */
   int			versionIP; /* IP Address family 6 or 4 */
   OOH323Channel*       pH225Channel;
   OOH323Channel*       pH245Channel;
   OOSOCKET             *h245listener;
   int                  *h245listenport;
   char                 remoteIP[2+8*4+7];/* Remote IP address */
   int                  remotePort;
   int                  remoteH245Port;
   char                 *remoteDisplayName;
   struct OOAliases     *remoteAliases;
   struct OOAliases     *ourAliases; /*aliases used in the call for us */
   OOMasterSlaveState   masterSlaveState;   /*!< Master-Slave state */
   OOMSAckStatus	msAckStatus; /* Master-Slave ack's status */
   ASN1UINT             statusDeterminationNumber;
   OOCapExchangeState   localTermCapState;
   OOCapExchangeState   remoteTermCapState;
   struct ooH323EpCapability* ourCaps;
   struct ooH323EpCapability* remoteCaps; /* TODO: once we start using jointCaps, get rid of remoteCaps*/
   struct ooH323EpCapability* jointCaps;
   int                  jointDtmfMode;
   DList                remoteFastStartOLCs;
   ASN1UINT8            remoteTermCapSeqNo;
   ASN1UINT8            localTermCapSeqNo;
   OOCapPrefs           capPrefs;   
   OOLogicalChannel*    logicalChans; 
   int                  noOfLogicalChannels;
   int                  logicalChanNoBase;
   int                  logicalChanNoMax;
   int                  logicalChanNoCur;
   unsigned             nextSessionID; /* Note by default 1 is audio session, 2 is video and 3 is data, from 3 onwards master decides*/
   DList                timerList;
   ASN1UINT             msdRetries;
   ASN1UINT8		requestSequence;
   ASN1UINT		reqFlags;
   ASN1UINT		t38sides;
   int			T38FarMaxDatagram;
   int			T38Version;
   H235TimeStamp	alertingTime, connectTime, endTime; /* time data for gatekeeper */
   FastStartResponse    *pFastStartRes; /* fast start response */
   struct OOH323Regex*		rtpMask;
   char			rtpMaskStr[120];
   char			lastDTMF;
   ASN1UINT		nextDTMFstamp;
   int			rtdrInterval, rtdrCount;	/* roundTripDelay interval and unreplied count */
   ASN1UINT		rtdrSend, rtdrRecv;		/* last sended/replied RTD request */
   void                 *usrData; /*!<User can set this to user specific data*/
   struct OOH323CallData* next;
   struct OOH323CallData* prev;
} OOH323CallData;

#define ooCallData OOH323CallData

/**
 * This callback function is triggered when a new call structure is 
 * created inside the stack for an incoming or outgoing call.
 *
 * @param call H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure.
 */
typedef int (*cb_OnNewCallCreated)(OOH323CallData* call);

/**
 * This callback function is triggered when a Q.931 alerting message is 
 * received for an outgoing call or when a Q.931 alerting message is sent 
 * for an incoming call.
 *
 * @param call H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure.
 */
typedef int (*cb_OnAlerting)(OOH323CallData * call);

/**
 * This callback function is triggered when there is an incoming call. 
 * In the case where a gatekeeper is in use, the call must first be 
 * admitted by the gatekeeper before this callback is triggered.
 *
 * @param call H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure.
 */
typedef int (*cb_OnIncomingCall)(OOH323CallData* call );

/**
 * This callback function is triggered after a Q.931 setup message 
 * is sent for an outgoing call.
 *
 * @param call H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure.
 */
typedef int (*cb_OnOutgoingCall)(OOH323CallData* call );

/**
 * This callback function is triggered when a Q.931 connect message is 
 * sent in case of incoming call.  In case of outgoing call, this is invoked 
 * when a Q.931 connect message is received. It is not invoked until after 
 * fast start and H.245 tunneling messages within the connect message are 
 * processed.
 *
 * @param call H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure.
 */
typedef int (*cb_OnCallEstablished)(struct OOH323CallData* call);

/**
 * This callback function is triggered when a call is cleared.
 *
 * @param call H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure.
 */
typedef int (*cb_OnCallCleared)(struct OOH323CallData* call);

/**
 * This callback function is triggered when master-slave determination 
 * and capabilities negotiation procedures are successfully completed 
 * for a call.
 *
 * @param call H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure.
 */
typedef int (*cb_OpenLogicalChannels)(struct OOH323CallData* call);

/**
 * This callback function is triggered when a call is forwarded by
 * a remote endpoint to another remote destination.
 * @param call Associated H.323 call data structure
 * @return 0 if callback was successful, non-zero error code if failure
 */
typedef int (*cb_OnCallForwarded)(struct OOH323CallData* call);

/**
 * This callback function is triggered when dtmf is received over Q.931(keypad)
 * or H.245(alphanumeric) or H.245(signal). This is not triggered when rfc
 * 2833 based dtmf is received.
 */
typedef int (*cb_OnReceivedDTMF)
   (struct OOH323CallData *call, const char *dtmf);

/**
 * This callback function is triggered when dtmf is received over Q.931(keypad)
 * or H.245(alphanumeric) or H.245(signal). This is not triggered when rfc
 * 2833 based dtmf is received.
 */
typedef void (*cb_OnModeChanged)
   (struct OOH323CallData *call, int isT38Mode);

/**
 * This structure holds all of the H.323 signaling callback function 
 * addresses.
 * @see ooH323EpSetH323Callbacks
 */
typedef struct OOH323CALLBACKS {
   cb_OnAlerting onNewCallCreated;
   cb_OnAlerting onAlerting;
   cb_OnAlerting onProgress;
   cb_OnIncomingCall onIncomingCall;
   cb_OnOutgoingCall onOutgoingCall;
   cb_OnCallEstablished onCallEstablished;
   cb_OnCallForwarded onCallForwarded;
   cb_OnCallCleared onCallCleared;
   cb_OpenLogicalChannels openLogicalChannels;
   cb_OnReceivedDTMF onReceivedDTMF;
   cb_OnModeChanged onModeChanged;
} OOH323CALLBACKS;

/**
 * This function is used to create a new call entry.
 * @param type         Type of the call (incoming/outgoing)
 * @param callToken    Call Token, an uniques identifier for the call
 *
 * @return             Pointer to a newly created call
 */
EXTERN OOH323CallData* ooCreateCall(char *type, char *callToken);

/**
 * This function is used to add a call to the list of existing calls.
 * @param call         Pointer to the call to be added.
 * @return             OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooAddCallToList (OOH323CallData *call);

/**
 * This function is used to set the caller ID for a call.
 *
 * @param call          Handle to the call
 * @param callerid      caller ID value
 * @return              OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooCallSetCallerId
(OOH323CallData* call, const char* callerid);

/**
 * This function is used to set calling party number for a particular call.
 * @param call          Handle to the call.
 * @param number        Calling Party number value.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallSetCallingPartyNumber
(OOH323CallData *call, const char *number);

/**
 * This function is used to retrieve calling party number of a particular call.
 * @param call          Handle to the call.
 * @param buffer        Handle to the buffer in which value will be returned.
 * @param len           Length of the supplied buffer.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallGetCallingPartyNumber
(OOH323CallData *call, char *buffer, int len);

/**
 * This function is used to retrieve called party number of a particular call.
 * @param call          Handle to the call.
 * @param buffer        Handle to the buffer in which value will be returned.
 * @param len           Length of the supplied buffer.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallGetCalledPartyNumber
(OOH323CallData *call, char *buffer, int len);

/**
 * This function is used to set called party number for a particular call.
 * @param call          Handle to the call.
 * @param number        Called Party number value.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallSetCalledPartyNumber
(OOH323CallData *call, const char *number);

/**
 * This function is used to clear the local aliases used by this call.
 * @param call          Handle to the call.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallClearAliases(OOH323CallData *call);

/**
 * This function is used to add an H323ID alias to be used by local endpoint 
 * for a particular call.
 * @param call          Handle to the call
 * @param h323id        H323ID to add for the local endpoint for the call.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallAddAliasH323ID(OOH323CallData *call, const char* h323id);

/**
 * This function is used to add an dialedDigits alias to be used by local 
 * endpoint for a particular call.
 * @param call          Handle to the call
 * @param dialedDigits  DialedDigits to add for the local endpoint for call.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallAddAliasDialedDigits
                             (OOH323CallData *call, const char* dialedDigits);

/**
 * This function is used to add an email-id alias to be used by local 
 * endpoint for a particular call.
 * @param call          Handle to the call
 * @param email         Email-id to add for the local endpoint for call.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallAddAliasEmailID(OOH323CallData *call, const char* email);


/**
 * This function is used to add an email-id alias to be used by local 
 * endpoint for a particular call.
 * @param call          Handle to the call
 * @param url           URL-id to add for the local endpoint for call.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallAddAliasURLID(OOH323CallData *call, const char* url);


/**
 * This is a helper function used by other call related add aliases functions 
 * to add a particular alias. This function is not supposed to be called 
 * directly.
 * @param call          Handle to the call
 * @param aliasType     Type of alias being added
 * @param value         Alias value
 * @param local         Whether alias is for local party or remote party
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
int ooCallAddAlias
   (OOH323CallData *call, int aliasType, const char *value, OOBOOL local);


/**
 * This function is used to add an dialed digits alias for the remote endpoint 
 * involved in a particular call.
 * @param call          Handle to the call
 * @param dialedDigits  dialedDigits alias to add for the remote endpoint.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallAddRemoteAliasDialedDigits
   (OOH323CallData *call, const char* dialedDigits);

/**
 * This function is used to add an H323ID alias for the remote endpoint 
 * involved in a particular call.
 * @param call          Handle to the call
 * @param h323id        H323ID to add for the remote endpoint.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCallAddRemoteAliasH323ID(OOH323CallData *call, const char* h323id);


/**
 * This function is used to add G7231 capability for the call. The 
 * "ooCallAdd...Capability" functions allow to override the global endpoint 
 * capabilities and use specific capabilities for specific calls.
 * @param call                 Call for which capability has to be added.
 * @param cap                  Capability to be added.
 * @param txframes             Number of frames per packet for transmission. 
 * @param rxframes             Number of frames per packet for reception.
 * @param silenceSuppression   Indicates support for silenceSuppression.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooCallAddG7231Capability(OOH323CallData *call, int cap, int txframes, 
                            int rxframes, OOBOOL silenceSuppression, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel);

/**
 * This function is used to add G728 capability for the call. The 
 * "ooCallAdd...Capability" functions allow to override the global endpoint 
 * capabilities and use specific capabilities for specific calls.
 * @param call                 Call for which capability has to be added.
 * @param cap                  Capability to be added.
 * @param txframes             Number of frames per packet for transmission. 
 * @param rxframes             Number of frames per packet for reception.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooCallAddG728Capability(OOH323CallData *call, int cap, int txframes, 
                            int rxframes, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel);

/**
 * This function is used to add G729 capability for the call. The 
 * "ooCallAdd...Capability" functions allow to override the global endpoint 
 * capabilities and use specific capabilities for specific calls.
 * @param call                 Call for which capability has to be added.
 * @param cap                  Capability to be added.
 * @param txframes             Number of frames per packet for transmission. 
 * @param rxframes             Number of frames per packet for reception.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooCallAddG729Capability(OOH323CallData *call, int cap, int txframes, 
                            int rxframes, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel);

/**
 * This function is used to add G711 capability for the call. The 
 * "ooCallAdd...Capability" functions allow to override the global endpoint 
 * capabilities and use specific capabilities for specific calls.
 * @param call                 Call for which capability has to be added.
 * @param cap                  Capability to be added.
 * @param txframes             Number of frames per packet for transmission. 
 * @param rxframes             Number of frames per packet for reception.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooCallAddG711Capability(OOH323CallData *call, int cap, int txframes, 
                            int rxframes, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel);


/**
 * This function is used to add GSM capability for the call. The 
 * "ooCallAdd...Capability" functions allow to override the global endpoint 
 * capabilities and use specific capabilities for specific calls.
 * @param call                 Call for which capability has to be added.
 * @param cap                  Type of GSM capability to be added.
 * @param framesPerPkt         Number of GSM frames pre packet. 
 * @param comfortNoise         Comfort noise spec for the capability. 
 * @param scrambled            Scrambled enabled/disabled for the capability.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooCallAddGSMCapability(OOH323CallData* call, int cap, 
                                  ASN1USINT framesPerPkt, OOBOOL comfortNoise, 
                                  OOBOOL scrambled, int dir,
                                  cb_StartReceiveChannel startReceiveChannel,
                                  cb_StartTransmitChannel startTransmitChannel,
                                  cb_StopReceiveChannel stopReceiveChannel,
                                  cb_StopTransmitChannel stopTransmitChannel);




/**
 * This function is used to add H263 video capability for the call. The 
 * "ooCallAdd...Capability" functions allow to override the global endpoint 
 * capabilities and use specific capabilities for specific calls.
 * @param call                 Call for which capability has to be added.
 * @param cap                  Capability type - OO_H263VIDEO
 * @param sqcifMPI             Minimum picture interval for encoding/decoding 
 *                             of SQCIF pictures.
 * @param qcifMPI              Minimum picture interval for encoding/decoding 
 *                             of QCIF pictures.
 * @param cifMPI               Minimum picture interval for encoding/decoding 
 *                             of CIF pictures.
 * @param cif4MPI              Minimum picture interval for encoding/decoding 
 *                             of CIF4 pictures.
 * @param cif16MPI             Minimum picture interval for encoding/decoding 
 *                             of CIF16 pictures.
 * @param maxBitRate           Maximum bit rate in units of 100 bits/s at
 *                             which a transmitter can transmit video or a 
 *                             receiver can receive video.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooCallAddH263VideoCapability(OOH323CallData *call, int cap, 
                                 unsigned sqcifMPI, unsigned qcifMPI, 
                                 unsigned cifMPI, unsigned cif4MPI, 
                                 unsigned cif16MPI, unsigned maxBitRate, 
                                 int dir, 
                                 cb_StartReceiveChannel startReceiveChannel,
                                 cb_StartTransmitChannel startTransmitChannel,
                                 cb_StopReceiveChannel stopReceiveChannel,
                                 cb_StopTransmitChannel stopTransmitChannel);


/**
 * This function is used to enable rfc 2833 capability for the call. By default
 * the stack uses the dtmf settings for the endpoint. But if you want to
 * enable/disable dtmf for a specific call, then you can override end-point
 * settings using this function
 * @param call                  Call for which rfc2833 has to be enabled.
 * @param dynamicRTPPayloadType dynamicRTPPayloadType to be used.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallEnableDTMFRFC2833
          (OOH323CallData *call, int dynamicRTPPayloadType);


/**
 * This function is used to disable rfc 2833 capability for the call. 
 * By default the stack uses the dtmf settings for the endpoint. But if you 
 * want to enable/disable dtmf for a specific call, then you can override 
 * end-point settings using this function
 * @param call                  Call for which rfc2833 has to be disabled.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallDisableDTMFRFC2833(OOH323CallData *call);


/**
 * This function is used to enable H.245(alphanumeric) dtmf support for the 
 * call. By default the stack uses the dtmf settings for the endpoint. But if 
 * you want to enable H.245(alphanumeric) dtmf for a specific call, then you 
 * can override end-point settings using this function
 * @param call                  Call for which H.245(alphanumeric) dtmf support
 *                              has to be enabled.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallEnableDTMFH245Alphanumeric(OOH323CallData *call);

/**
 * This function is used to disable H.245(alphanumeric) dtmf support for the 
 * call. By default the stack uses the dtmf settings for the endpoint. But if 
 * you want to disable H.245(alphanumeric) dtmf for a specific call, then you 
 * can override end-point settings using this function
 * @param call                  Call for which H.245(alphanumeric) dtmf support
 *                              has to be disabled.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallDisableDTMFH245Alphanumeric(OOH323CallData *call);

/**
 * This function is used to enable H.245(signal) dtmf support for the call. 
 * By default the stack uses the dtmf settings for the endpoint. But if you 
 * want to enable H.245(signal) dtmf for a specific call, then you can override
 * end-point settings using this function
 * @param call                  Call for which H.245(signal) dtmf support
 *                              has to be enabled.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallEnableDTMFH245Signal(OOH323CallData *call);


/**
 * This function is used to disable H.245(signal) dtmf support for the call. 
 * By default the stack uses the dtmf settings for the endpoint. But if you 
 * want to disable H.245(signal) dtmf for a specific call, then you can 
 * override end-point settings using this function
 * @param call                  Call for which H.245(signal) dtmf support
 *                              has to be disabled.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallDisableDTMFH245Signal(OOH323CallData *call);


/**
 * This function is used to enable Q.931(keypad) dtmf support for the call.
 * By default the stack uses the dtmf settings for the endpoint. But if you 
 * want to enable Q.931(keypad) dtmf support for a specific call, then you can
 * override end-point settings using this function
 * @param call                  Call for which Q.931(keypad) dtmf support
 *                              has to be enabled.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallEnableDTMFQ931Keypad(OOH323CallData *call);

/**
 * This function is used to disable Q.931(keypad) dtmf support for the call.
 * By default the stack uses the dtmf settings for the endpoint. But if you 
 * want to disable Q.931(keypad) dtmf support for a specific call, then you can
 * override end-point settings using this function
 * @param call                  Call for which Q.931(keypad) dtmf support
 *                              has to be disabled.
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallDisableDTMFQ931Keypad(OOH323CallData *call);


/**
 * This function is used to find a call by using the unique token for the call.
 * @param callToken      The unique token for the call.
 *
 * @return               Pointer to the call if found, NULL otherwise.
 */
EXTERN OOH323CallData* ooFindCallByToken(const char *callToken);

/**
 * This function is used to end a call. Based on what stage of clearance the
 * call is it takes appropriate action.
 *
 * @param call   Handle to the call which has to be cleared.
 * @return       OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooEndCall(OOH323CallData *call);

/**
 * This function is used to remove a call from the list of existing calls.
 * 
 * @param call          Pointer to the call to be removed.
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooRemoveCallFromList (OOH323CallData *call);

/**
 * This function is used to clean up a call. It closes all associated sockets, 
 * removes the call from the global list and frees up associated memory.
 *
 * @param call          Pointer to the call to be cleared.
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCleanCall(OOH323CallData *call);

/**
 * This function is used to check whether a specified session in specified 
 * direction is active for the call.
 * @param call       Handle to call for which session has to be queried.
 * @param sessionID  Session id to identify the type of session(1 for audio, 
 *                   2 for voice and 3 for data)
 * @param dir        Direction of the session(transmit/receive)
 *
 * @return           1, if session active. 0, otherwise. 
 */
EXTERN ASN1BOOL ooIsSessionEstablished
(OOH323CallData *call, int sessionID, char* dir);

/**
 * This function can be used by an application to specify media endpoint 
 * information for different types of media. The stack by default uses local IP
 * and port for media. An application can provide mediainfo if it wants to 
 * override default.
 * @param call      Handle to the call
 * @param mediaInfo Structure which defines the media endpoint to be 
 *                  used.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooAddMediaInfo(OOH323CallData *call, OOMediaInfo mediaInfo);

/**
 * This function is used to generate a media session id for the new media 
 * session for the call.
 * @param call       Handle to the call.
 * @param type       Type of media session.
 * @param dir        Direction of session
 *
 * @return           Generated session id.
 */
EXTERN unsigned ooCallGenerateSessionID
                    (OOH323CallData *call, OOCapType type, char *dir);

/**
 * This is an handler for H245 connection retry timer. When remote end is not 
 * yet listening for H245 connections, this timer provides a wait and retry
 * mechanism to establish H245 connection.
 * @param data      Timer callback data.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooCallH245ConnectionRetryTimerExpired(void *data);

/**
 * This function is used to retrieve the description text for a reason
 * code.
 *
 * @param code     Reason code.
 * @return         The text description string.
 */
EXTERN const char* ooGetReasonCodeText (OOUINT32 code);

/**
 * This function is used to retrieve the description text for a call
 * state.
 *
 * @param callState Call state.
 * @return         The text description string.
 */
EXTERN const char* ooGetCallStateText (OOCallState callState);

/** 
 * @} 
 */

int isRunning(char *callToken);

int ooCallAddG726Capability(struct OOH323CallData *call, int cap, int txframes,
                            int rxframes, OOBOOL silenceSuppression, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel);
int ooCallAddAMRNBCapability(struct OOH323CallData *call, int cap, int txframes,
                            int rxframes, OOBOOL silenceSuppression, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel);
int ooCallAddSpeexCapability(struct OOH323CallData *call, int cap, int txframes,
                            int rxframes, OOBOOL silenceSuppression, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel);
int ooCallEnableDTMFCISCO(struct OOH323CallData *call, int dynamicRTPPayloadType);
int ooCallDisableDTMFCISCO(struct OOH323CallData *call);

#ifdef __cplusplus
}
#endif

#endif
