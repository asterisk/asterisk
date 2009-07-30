/*
 * Copyright (C) 2005 by Page Iberica, S.A.
 * Copyright (C) 2005 by Objective Systems, Inc.
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
 * @file ooGkClient.h 
 * This file contains functions to support RAS protocol. 
 *
 *
 */
#ifndef _OOGKCLIENT_H_
#define _OOGKCLIENT_H_

#include "ooasn1.h"
#include "ootypes.h"
#include "H323-MESSAGES.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#ifdef MAKE_DLL
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* _WIN32 */
#endif /* EXTERN */


/*-------------------------------------------------------------------*/
/*  Public definitions                                               */
/*-------------------------------------------------------------------*/



#define MAX_IP_LEN 15
#define DEFAULT_GKPORT 1719
#define MULTICAST_GKADDRESS "224.0.1.41"
#define MULTICAST_GKPORT 1718
#define DEFAULT_BW_REQUEST  100000

/* Various timeouts in seconds */
#define DEFAULT_REG_TTL 300
#define DEFAULT_TTL_OFFSET 20
#define DEFAULT_ARQ_TIMEOUT 5
#define DEFAULT_DRQ_TIMEOUT 5
#define DEFAULT_GRQ_TIMEOUT 15
#define DEFAULT_RRQ_TIMEOUT 10

/* Number of retries before giving up */
#define OO_MAX_GRQ_RETRIES 3
#define OO_MAX_RRQ_RETRIES 3
#define OO_MAX_ARQ_RETRIES 3

/* Gk Client timers */
#define OO_GRQ_TIMER (1<<0)
#define OO_RRQ_TIMER (1<<1)
#define OO_REG_TIMER (1<<2)
#define OO_ARQ_TIMER (1<<3)
#define OO_DRQ_TIMER (1<<4)

/** 
 * @defgroup gkclient Gatekeeper client
 * @{
 */

struct OOH323CallData;
struct ooGkClient;
struct RasCallAdmissionInfo;

typedef struct ooGkClientTimerCb{
   int timerType;
   struct ooGkClient *pGkClient;
   struct RasCallAdmissionInfo *pAdmInfo;
}ooGkClientTimerCb;

enum RasGatekeeperMode {
   RasNoGatekeeper = 0,
   RasDiscoverGatekeeper = 1,
   RasUseSpecificGatekeeper = 2,
};

enum RasCallType{
   RasPointToPoint =0,
   RasOneToN,
   RasnToOne,
   RasnToN,
};


enum OOGkClientState {
   GkClientIdle = 0,
   GkClientDiscovered, /* Gk Discovery is complete */
   GkClientRegistered, /* registered with gk */
   GkClientUnregistered,
   GkClientGkErr,/*Gk is not responding, in discover mode can look for new GK*/
   GkClientFailed
};
   

typedef struct RasGatekeeperInfo
{
   ASN1BOOL willRespondToIRR;
   H225UUIEsRequested uuiesRequested;
   H225BandWidth bw;
   H225RegistrationConfirm_preGrantedARQ preGrantedARQ;
}RasGatekeeperInfo;

/**
 *  Call Admission Information
 */
typedef struct RasCallAdmissionInfo
{
   struct OOH323CallData *call;
   unsigned int retries;
   unsigned short requestSeqNum;
   ASN1USINT irrFrequency;
} RasCallAdmissionInfo;

struct OOAliases;

/**
 * NOTE- This functionality is not yet fully completed.
 * This is a callback function which is triggered when registration confirm 
 * message is received from the gatekeeper. The first parameter is the message
 * received. The second parameter provides updated list of aliases after the 
 * message was processed by the stack.
 * @param rcf  Handle to the received registration confirm message
 */
typedef int (*cb_OnReceivedRegistrationConfirm)
     (H225RegistrationConfirm *rcf, struct OOAliases *aliases);


/**
 * NOTE- This functionality is not yet fully completed.
 * This is a callback function which is triggered when unregistration confirm 
 * message is received. The first parameter is the message received. The second
 * parameter provides updated list of aliases after the message was processed 
 * by the stack.
 */
typedef int (*cb_OnReceivedUnregistrationConfirm)
     (H225UnregistrationConfirm *ucf, struct OOAliases *aliases);

/**
 * NOTE- This functionality is not yet fully completed.
 * This is a callback function which is triggered when unregistration request 
 * message is received. The first parameter is the message received. The second
 * parameter provides the list of aliases requested to be unregistered.
 */
typedef int (*cb_OnReceivedUnregistrationRequest)
     (H225UnregistrationRequest *urq, struct OOAliases *aliases);

typedef struct OOGKCLIENTCALLBACKS{
   cb_OnReceivedRegistrationConfirm onReceivedRegistrationConfirm;
   cb_OnReceivedUnregistrationConfirm onReceivedUnregistrationConfirm;
   cb_OnReceivedUnregistrationRequest onReceivedUnregistrationRequest;
}OOGKCLIENTCALLBACKS;

/**
 * Structure to store all the configuration information for the gatekeeper
 * client. Gatekeeper client is responsible for all the communication with
 * a gatekeeper.
 */
typedef struct ooGkClient{
   ASN1BOOL discoveryComplete;
   OOCTXT ctxt;
   OOCTXT msgCtxt;
   OOSOCKET rasSocket;
   int localRASPort;
   char localRASIP[20];
   char gkRasIP[20];
   char gkCallSignallingIP[20];
   RasGatekeeperInfo gkInfo;
   int gkRasPort;
   int gkCallSignallingPort;
   unsigned short requestSeqNum;
   enum RasGatekeeperMode gkMode; /* Current Gk mode */
   struct timeval registrationTime;
   H225GatekeeperIdentifier gkId;
   H225EndpointIdentifier endpointId;
   DList callsPendingList;
   DList callsAdmittedList;
   DList timerList;
   OOGKCLIENTCALLBACKS callbacks;
   ASN1UINT grqRetries;
   ASN1UINT rrqRetries;
   ASN1UINT grqTimeout;
   ASN1UINT rrqTimeout;
   ASN1UINT regTimeout;
   ASN1UINT arqTimeout;
   ASN1UINT drqTimeout;
   enum OOGkClientState  state;
} ooGkClient;

struct OOAliases;
struct OOH323CallData;

/**
 * This function is used to initialize the Gatekeeper client.If an application
 * wants to use gatekeeper services, it should call this function immediately
 * after initializing the H323 EndPoint.
 * @param eGkMode          Gatekeeper mode.
 * @param szGkAddr         Dotted gk ip address, if gk has to be specified.
 * @param iGkPort          Gk port.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 * 
 */
EXTERN int ooGkClientInit
   (enum RasGatekeeperMode eGkMode, char *szGkAddr, int iGkPort );

/**
 * This function is used to print the gatekeeper client configuration 
 * information to log.
 * @param pGkClient        Handle to gatekeeper client.
 */
EXTERN void ooGkClientPrintConfig(ooGkClient *pGkClient);

/**
 * This function is used to destroy Gatekeeper client. It releases all the 
 * associated memory.
 *
 * @return     OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientDestroy(void);

/**
 * This function is used to start the Gatekeeper client functionality.
 * @param pGkClient        Pointer to the Gatekeeper Client.
 * 
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientStart(ooGkClient *pGkClient);

/**
 * This function is invoked to set a gatekeeper mode. 
 * @param pGkClient Handle to gatekeeper client. 
 * @param eGkMode   Gatekeeper mode selected. One of the following: 
 *                    - RasNoGatekeeper (DEFAULT), No Gatekeeper.              
 *                    - RasDiscoverGatekeeper, to discover a gatekeeper 
 *                      automatically.
 *                    - RasUseSpecificGatekeeper, to use a specific gatekeeper.
 * @param szGkAddr  Gatekeeper address (only when using specific gatekeeper).
 * @param iGkPort   Gatekeeper RAS port
 *
 * @return         Completion status - OO_OK on success, OO_FAILED on failure
 */
EXTERN int ooGkClientSetGkMode(ooGkClient *pGkClient, 
                               enum RasGatekeeperMode eGkMode, char *szGkAddr, 
                               int iGkPort );

/**
 * This function is used to create a RAS channel for the gatekeeper.
 * @param pGkClient     Pointer to the Gatekeeper client for which RAS channel
 *                      has to be created.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientCreateChannel(ooGkClient *pGkClient);

/**
 * This function is used to close a RAS channel of the gatekeeper client.
 * @param pGkClient    Pointer to the gatekeeper client.
 *
 * @return             OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientCloseChannel(ooGkClient *pGkClient);


/**
 * This function is used to fill endpoint's vendor information into vendor
 * identifier.
 * @param pGkClient    Pointer to gatekeeper client.
 * @param psVendor     Pointer to vendor identifier to be filled.
 * 
 */
EXTERN void ooGkClientRasFillVendor
   (ooGkClient *pGkClient, H225VendorIdentifier *psVendor);


/**
 * This function is invoked to receive data on Gatekeeper client's RAS channel.
 * @param pGkClient    Handle to Gatekeeper client for which message has to be
 *                     received.
 *
 * @return             Completion status - OO_OK on success, OO_FAILED on 
 *                     failure
 */
EXTERN int ooGkClientReceive(ooGkClient *pGkClient);


/**
 * This function is used to handle a received RAS message by a gatekeeper 
 * client.
 * @param pGkClient   Handle to gatekeeper client.
 * @param pRasMsg     Handle to received Ras message.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientHandleRASMessage
   (ooGkClient *pGkClient, H225RasMessage *pRasMsg);


/**
 * This function is used to send a message on Gatekeeper clien't RAS channel.
 * @param pGkClient   Handle to the gatekeeper client.
 * @param pRasMsg     Handle to Ras message to be sent.
 *
 * @return            OO_OK, on success. OO_FAILED, otherwise. 
 */
EXTERN int ooGkClientSendMsg(ooGkClient *pGkClient, H225RasMessage *pRasMsg);


/**
 * This function is used to send Gatekeeper request message.
 * @param pGkClient  Handle to gatekeeper client for which GRQ message has to 
 *                   be sent.
 *
 * @return           OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientSendGRQ(ooGkClient *pGkClient);


/**
 * This function is used to handle a received gatekeeper reject message.
 * @param pGkClient          Handle to gatekeeper client.
 * @param pGatekeeperReject  Handle to received reject message.
 *
 * @return                   OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientHandleGatekeeperReject
   (ooGkClient *pGkClient, H225GatekeeperReject *pGatekeeperReject);

/**
 * This function is used to handle a received gatekeeper confirm message.
 * @param pGkClient          Handle to gatekeeper client.
 * @param pGatekeeperConfirm Handle to received confirmed message.
 *
 * @return                   OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientHandleGatekeeperConfirm
   (ooGkClient *pGkClient, H225GatekeeperConfirm *pGatekeeperConfirm);


/**
 * This function is used to send Registration request message.
 * @param pGkClient  Handle to gatekeeper client for which RRQ message has to 
 *                   be sent.
 * @param keepAlive  Indicates whether keepalive lightweight registration has 
 *                   to be sent.
 *
 * @return           OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientSendRRQ(ooGkClient *pGkClient, ASN1BOOL keepAlive);

/**
 * This function is used to handle a received registration confirm message.
 * @param pGkClient            Handle to gatekeeper client.
 * @param pRegistrationConfirm Handle to received confirmed message.
 *
 * @return                     OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientHandleRegistrationConfirm
   (ooGkClient *pGkClient, H225RegistrationConfirm *pRegistrationConfirm);

/**
 * This function is used to handle a received registration reject message.
 * @param pGkClient           Handle to gatekeeper client.
 * @param pRegistrationReject Handle to received reject message.
 *
 * @return                    OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientHandleRegistrationReject
   (ooGkClient *pGkClient, H225RegistrationReject *pRegistrationReject);


/**
 * This function is used to send UnRegistration request message.
 * @param pGkClient  Handle to gatekeeper client for which URQ message has to 
 *                   be sent.
 * @param aliases    List of aliases to be unregistered. NULL, if all the 
 *                   aliases have to be unregistered.
 *
 * @return           OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientSendURQ(ooGkClient *pGkClient, struct OOAliases *aliases);

/**
 * This function is used to handle a received Unregistration request message.
 * @param pGkClient              Handle to gatekeeper client.
 * @param punregistrationRequest Handle to received unregistration request.
 *
 * @return                   OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientHandleUnregistrationRequest
   (ooGkClient *pGkClient, H225UnregistrationRequest *punregistrationRequest);


/**
 * This function is used to send an unregistration confirm message to 
 * gatekeeper.
 * @param pGkClient        Handle to gatekeeper client.
 * @param reqNo            Request Sequence number for the confirm message.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientSendUnregistrationConfirm(ooGkClient *pGkClient, 
                                                               unsigned reqNo);

/**
 * This function is invoked to request bandwith admission for a call. 
 * @param pGkClient     Gatekeeper client to be used
 * @param call          Handle to the call.
 * @param retransmit    Indicates whether new call or retransmitting for 
 *                      existing call.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientSendAdmissionRequest
   (ooGkClient *pGkClient, struct OOH323CallData *call, ASN1BOOL retransmit);

/**
 * This function is used to handle a received Admission confirm message.
 * @param pGkClient         Handle to gatekeeper client.
 * @param pAdmissionConfirm Handle to received confirmed message.
 *
 * @return                  OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientHandleAdmissionConfirm
   (ooGkClient *pGkClient, H225AdmissionConfirm *pAdmissionConfirm);


/**
 * This function is used to handle a received Admission Reject message. It 
 * finds the associated call and marks it for cleaning with appropriate 
 * call end reason code.
 * @param pGkClient         Handle to Gatekeeper client.
 * @param pAdmissionReject  Handle to received admission reject message.
 *
 * @return                  OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientHandleAdmissionReject
   (ooGkClient *pGkClient, H225AdmissionReject *pAdmissionReject);

/**
 * This function is invoked to request call disengage to gatekeeper. 
 * @param pGkClient  Gatekeeper client to be used.
 * @param call       Call Handle
 *
 * @return           Completion status - OO_OK on success, OO_FAILED on failure
 */
EXTERN int ooGkClientSendDisengageRequest
   (ooGkClient *pGkClient, struct OOH323CallData *call);

/**
 * This function is used to handle a received disengage confirm message.
 * @param pGkClient            Handle to gatekeeper client.
 * @param pDCF                 Handle to received confirmed message.
 *
 * @return                     OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooGkClientHandleDisengageConfirm
   (ooGkClient *pGkClient, H225DisengageConfirm *pDCF);

/**
 * This function is used to handle an expired registration request timer.
 * @param pdata     Handle to callback data
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientRRQTimerExpired(void*pdata);

/**
 * This function is used to handle an expired gatekeeper request timer.
 * @param pdata     Handle to callback data
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientGRQTimerExpired(void* pdata);

/**
 * This function is used to handle an expired registration time-to-live timer.
 * @param pdata     Handle to callback data
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientREGTimerExpired(void *pdata);

/**
 * This function is used to handle an expired admission request timer.
 * @param pdata     Handle to callback data
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientARQTimerExpired(void* pdata);

/**
 * This function is used to clean call related data from gatekeeper client.
 * @param pGkClient  Handle to the gatekeeper client.
 * @param call       Handle to the call to be cleaned.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientCleanCall(ooGkClient *pGkClient, struct OOH323CallData *call);

/**
 * This function is used to handle gatekeeper client failure or gatekeeper 
 * failure which can be detected by unresponsiveness of gk.
 * @param pGkClient  Handle to gatekeeper client.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientHandleClientOrGkFailure(ooGkClient *pGkClient);

/**
 * This function is used to update the registration status of aliases.
 * @param pGkClient  Handle to the GK client.
 * @param pAddresses List of newly registered addresses. NULL means all
 *                   aliases have been successfully registered.
 * @param registered Indicates whether aliases are registered or unregistered.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGkClientUpdateRegisteredAliases
   (ooGkClient *pGkClient, H225_SeqOfH225AliasAddress *pAddresses, 
    OOBOOL registered);

/**
 * This function is used internally to set Gatekeeper Clients callbacks.
 * Note: This functionality is not yet fully supported
 * @param pGkClient  Handle to the GK client.
 * @param callbacks  Callback structure contatining various gatekeeper client
 *                   callbacks.
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
int ooGkClientSetCallbacks
   (ooGkClient *pGkClient, OOGKCLIENTCALLBACKS callbacks);
/** 
 * @} 
 */

#ifdef __cplusplus
}
#endif

#endif /* __GKCLIENT_H */
