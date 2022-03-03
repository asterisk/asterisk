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
 * @file ooq931.h
 * This file contains functions to support call signalling.
 */

#ifndef _OOQ931HDR_H_
#define _OOQ931HDR_H_

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
#endif /* MAKE_DLL */
#endif /* EXTERN */

/**
 * @defgroup q931 Q.931/H.2250 Message Handling
 * @{
 */
/* Maximum length of the Calling/Called party number number */
#define OO_MAX_NUMBER_LENGTH 50

/* Maximum value for a call token identifier */
#define OO_MAX_CALL_TOKEN 999999

/* Q.931 packet must be at least 5 bytes long */
#define Q931_E_TOOSHORT         (-1001)
/* callReference field must be 2 bytes long */
#define Q931_E_INVCALLREF       (-1002)
/* invalid length of message */
#define Q931_E_INVLENGTH        (-1003)

enum Q931MsgTypes {
   Q931NationalEscapeMsg  = 0x00,
   Q931AlertingMsg        = 0x01,
   Q931CallProceedingMsg  = 0x02,
   Q931ConnectMsg         = 0x07,
   Q931ConnectAckMsg      = 0x0f,
   Q931ProgressMsg        = 0x03,
   Q931SetupMsg           = 0x05,
   Q931SetupAckMsg        = 0x0d,
   Q931ResumeMsg          = 0x26,
   Q931ResumeAckMsg       = 0x2e,
   Q931ResumeRejectMsg    = 0x22,
   Q931SuspendMsg         = 0x25,
   Q931SuspendAckMsg      = 0x2d,
   Q931SuspendRejectMsg   = 0x21,
   Q931UserInformationMsg = 0x20,
   Q931DisconnectMsg      = 0x45,
   Q931ReleaseMsg         = 0x4d,
   Q931ReleaseCompleteMsg = 0x5a,
   Q931RestartMsg         = 0x46,
   Q931RestartAckMsg      = 0x4e,
   Q931SegmentMsg         = 0x60,
   Q931CongestionCtrlMsg  = 0x79,
   Q931InformationMsg     = 0x7b,
   Q931NotifyMsg          = 0x6e,
   Q931StatusMsg          = 0x7d,
   Q931StatusEnquiryMsg   = 0x75,
   Q931FacilityMsg        = 0x62
};

enum Q931IECodes {
   Q931BearerCapabilityIE   = 0x04,
   Q931CauseIE              = 0x08,
   Q931FacilityIE           = 0x1c,
   Q931ProgressIndicatorIE  = 0x1e,
   Q931CallStateIE          = 0x14,
   Q931DisplayIE            = 0x28,
   Q931SignalIE             = 0x34,
   Q931CallingPartyNumberIE = 0x6c,
   Q931CalledPartyNumberIE  = 0x70,
   Q931RedirectingNumberIE  = 0x74,
   Q931UserUserIE           = 0x7e,
   Q931KeypadIE             = 0x2c
};

enum Q931InformationTransferCapability {
   Q931TransferSpeech,
   Q931TransferUnrestrictedDigital = 8,
   Q931TransferRestrictedDigital = 9,
   Q931Transfer3_1kHzAudio = 16,
   Q931TrasnferUnrestrictedDigitalWithTones = 17,
   Q931TransferVideo = 24
};

enum Q931CauseValues {
   Q931UnallocatedNumber           = 0x01,
   Q931NoRouteToNetwork            = 0x02,
   Q931NoRouteToDestination        = 0x03,
   Q931ChannelUnacceptable         = 0x06,
   Q931NormalCallClearing          = 0x10,
   Q931UserBusy                    = 0x11,
   Q931NoResponse                  = 0x12,
   Q931NoAnswer                    = 0x13,
   Q931SubscriberAbsent            = 0x14,
   Q931CallRejected                = 0x15,
   Q931NumberChanged               = 0x16,
   Q931Redirection                 = 0x17,
   Q931DestinationOutOfOrder       = 0x1b,
   Q931InvalidNumberFormat         = 0x1c,
   Q931NormalUnspecified           = 0x1f,
   Q931StatusEnquiryResponse       = 0x1e,
   Q931NoCircuitChannelAvailable   = 0x22,
   Q931NetworkOutOfOrder           = 0x26,
   Q931TemporaryFailure            = 0x29,
   Q931Congestion                  = 0x2a,
   Q931RequestedCircuitUnAvailable = 0x2c,
   Q931ResourcesUnavailable        = 0x2f,
   Q931IncompatibleDestination     = 0x58,
   Q931ProtocolErrorUnspecified    = 0x6f,
   Q931RecoveryOnTimerExpiry       = 0x66,
   Q931InvalidCallReference        = 0x51,
   Q931ErrorInCauseIE              = 0
};

enum Q931SignalInfo {
   Q931SignalDialToneOn,
   Q931SignalRingBackToneOn,
   Q931SignalInterceptToneOn,
   Q931SignalNetworkCongestionToneOn,
   Q931SignalBusyToneOn,
   Q931SignalConfirmToneOn,
   Q931SignalAnswerToneOn,
   Q931SignalCallWaitingTone,
   Q931SignalOffhookWarningTone,
   Q931SignalPreemptionToneOn,
   Q931SignalTonesOff = 0x3f,
   Q931SignalAlertingPattern0 = 0x40,
   Q931SignalAlertingPattern1,
   Q931SignalAlertingPattern2,
   Q931SignalAlertingPattern3,
   Q931SignalAlertingPattern4,
   Q931SignalAlertingPattern5,
   Q931SignalAlertingPattern6,
   Q931SignalAlertingPattern7,
   Q931SignalAlretingOff = 0x4f,
   Q931SignalErrorInIE = 0x100
};

enum Q931NumberingPlanCodes {
   Q931UnknownPlan          = 0x00,
   Q931ISDNPlan             = 0x01,
   Q931DataPlan             = 0x03,
   Q931TelexPlan            = 0x04,
   Q931NationalStandardPlan = 0x08,
   Q931PrivatePlan          = 0x09,
   Q931ReservedPlan         = 0x0f
};

enum Q931TypeOfNumberCodes {
   Q931UnknownType          = 0x00,
   Q931InternationalType    = 0x01,
   Q931NationalType         = 0x02,
   Q931NetworkSpecificType  = 0x03,
   Q931SubscriberType       = 0x04,
   Q931AbbreviatedType      = 0x06,
   Q931ReservedType         = 0x07
};

enum Q931CodingStandard{
  Q931CCITTStd = 0,
  Q931ReservedInternationalStd,
  Q931NationalStd,
  Q931NetworkStd
};

enum Q931TransferMode {
  Q931TransferCircuitMode = 0,   /* 00 */
  Q931TransferPacketMode  = 2   /* 10 */
};

enum Q931TransferRate{
  Q931TransferRatePacketMode = 0x00,  /* 00000 */
  Q931TransferRate64Kbps     = 0x10,  /* 10000 */
  Q931TransferRate128kbps    = 0x11,  /* 10001 */
  Q931TransferRate384kbps    = 0x13,  /* 10011 */
  Q931TransferRate1536kbps   = 0x15,  /* 10101 */
  Q931TransferRate1920kbps   = 0x17   /* 10111 */
};

enum Q931UserInfoLayer1Protocol{
  Q931UserInfoLayer1CCITTStdRate = 1,
  Q931UserInfoLayer1G711ULaw,
  Q931UserInfoLayer1G711ALaw,
  Q931UserInfoLayer1G721ADPCM,
  Q931UserInfoLayer1G722G725,
  Q931UserInfoLayer1H261,
  Q931UserInfoLayer1NonCCITTStdRate,
  Q931UserInfoLayer1CCITTStdRateV120,
  Q931UserInfoLayer1X31
};

/*
  Structure to build store outgoing encoded UUIE
  The different fields in the structure have octet lengths
  as specified in the spec.
*/
typedef struct Q931InformationElement {
   int discriminator;
   int offset;
   int length;
   ASN1OCTET data[1];
} Q931InformationElement;

/**
 * Q.931 message structure. Contains context for memory allocation,
 * protocol discriminator, call reference, meesage type and list of
 * user-user information elements (IEs).
 */
typedef struct Q931Message {
   ASN1UINT protocolDiscriminator;
   ASN1UINT callReference;
   ASN1BOOL fromDestination;
   ASN1UINT messageType;      /* Q931MsgTypes */
   ASN1UINT tunneledMsgType;  /* The H245 message this message is tunneling*/
   ASN1INT  logicalChannelNo; /* channel number associated with tunneled */
                              /* message, 0 if no channel */
   DList ies;
   Q931InformationElement *bearerCapabilityIE;
   Q931InformationElement *callingPartyNumberIE;
   Q931InformationElement *calledPartyNumberIE;
   Q931InformationElement *causeIE;
   Q931InformationElement *keypadIE;
   Q931InformationElement *callstateIE;
   H225H323_UserInformation *userInfo;
} Q931Message;

/**
 * This structure is used to hold an H.323 alias address.
 */
typedef struct OOAliases {
   int type;           /*!< H.225 AliasAddress choice option (t value) */
   char *value;        /*!< H.225 AliasAddress value */
   OOBOOL registered;
   struct OOAliases *next;
} OOAliases;

#define ooAliases OOAliases

struct OOH323CallData;

/*
 * These are message callbacks which can be used by user applications
 * to perform application specific things on receiving a particular
 * message or before sending a particular message. For ex. user application
 * can change values of some parameters of setup message before it is actually
 * sent out.
 */
/**
 * This callback is triggered when an H.225 SETUP message is received by
 * the application.
 * @param call  The call the message is associated with.
 * @param pmsg  Q.931 message structure.
 * @return OO_OK if message processing successful or OO_FAILED if not.
 */
typedef int (*cb_OnReceivedSetup)
   (struct OOH323CallData *call, struct Q931Message *pmsg);

/**
 * This callback is triggered when an H.225 CONNECT message is received by
 * the application.
 * @param call  The call the message is associated with.
 * @param pmsg  Q.931 message structure.
 * @return OO_OK if message processing successful or OO_FAILED if not.
 */
typedef int (*cb_OnReceivedConnect)
   (struct OOH323CallData *call, struct Q931Message *pmsg);

/**
 * This callback is triggered after an H.225 SETUP message has been
 * constructed and is ready to be sent out.  It provides the application
 * with an opportunity to add additional non-standard information.
 * @param call  The call the message is associated with.
 * @param pmsg  Q.931 message structure.
 * @return OO_OK if message processing successful or OO_FAILED if not.
 */
typedef int (*cb_OnBuiltSetup)
   (struct OOH323CallData *call, struct Q931Message *pmsg);

/**
 * This callback is triggered after an H.225 CONNECT message has been
 * constructed and is ready to be sent out.  It provides the application
 * with an opportunity to add additional non-standard information.
 * @param call  The call the message is associated with.
 * @param pmsg  Q.931 message structure.
 * @return OO_OK if message processing successful or OO_FAILED if not.
 */
typedef int (*cb_OnBuiltConnect)
   (struct OOH323CallData *call, struct Q931Message *pmsg);

/**
 * This structure holds the various callback functions that are
 * triggered when H.225 messages are received or constructed.
 * @see ooH323EpSetH225MsgCallbacks
 */
typedef struct OOH225MsgCallbacks {
   cb_OnReceivedSetup onReceivedSetup;
   cb_OnReceivedConnect onReceivedConnect;
   cb_OnBuiltSetup onBuiltSetup;
   cb_OnBuiltConnect onBuiltConnect;
} OOH225MsgCallbacks;

/**
 * This function is invoked to decode a Q931 message.
 *
 * @param call     Handle to call which owns the message.
 * @param msg      Pointer to the Q931 message
 * @param length   Length of the encoded data
 * @param data     Pointer to the data to be decoded
 *
 * @return         Completion status - 0 on success, -1 on failure
 */
EXTERN int ooQ931Decode
(struct OOH323CallData *call, Q931Message* msg, int length, ASN1OCTET *data, int docallbacks);

/**
 * This function is used to decode the UUIE of the message from the list of
 * ies. It decodes the User-User ie and populates the userInfo field of the
 * message.
 * @param q931Msg    Pointer to the message whose User-User ie has to be
 *                   decoded.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooDecodeUUIE(OOCTXT* pctxt, Q931Message *q931Msg);

/**
 * This function is used to encode the UUIE field of the Q931 message.
 * It encodes UUIE and adds the encoded data to the list of ies.
 * @param q931msg        Pointer to the Q931 message whose UUIE field has to be
 *                       encoded.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooEncodeUUIE(OOCTXT* pctxt, Q931Message *q931msg);

/**
 * This function is invoked to retrieve an IE element from a Q931 message.
 *
 * @param q931msg  Pointer to the Q931 message
 * @param ieCode   IE code for the IE element to be retrieved
 *
 * @return         Pointer to a Q931InformationElement contating
 *                 the IE element.
 */
EXTERN Q931InformationElement* ooQ931GetIE (const Q931Message* q931msg,
                                            int ieCode);

/**
 * This function is invoked to print a Q931 message.
 *
 * @param q931msg  Pointer to the Q931 message
 *
 * @return         - none
 */
EXTERN void ooQ931Print (const Q931Message* q931msg);


/**
 * This function is invoked to create an outgoing Q931 message.
 *
 * @param msg      Reference to the pointer of type Q931 message.
 * @param msgType  Type of Q931 message to be created
 *
 * @return         Completion status - 0 on success, -1 on failure
 */
EXTERN int ooCreateQ931Message(OOCTXT* pctxt, Q931Message **msg, int msgType);

/**
 * This function is invoked to generate a unique call reference number.
 *
 * @return         - call reference number
 */
EXTERN ASN1USINT ooGenerateCallReference(void);


/**
 * This function is used to generate a unique call identifier for the call.
 * @param callid      Pointer to the callid structure, which will be populated
 *                    with the generated callid.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGenerateCallIdentifier(H225CallIdentifier *callid);

/**
 * This function is invoked to release the memory used up by a Q931 message
 *
 * @param q931Msg  Pointer to a Q931 message which has to be freed.
 *
 * @return         Completion status - 0 on success, -1 on failure
 */
EXTERN int ooFreeQ931Message(OOCTXT* pctxt, Q931Message *q931Msg);

/**
 * This function is invoked to retrive the outgoing message buffer for
 * Q931 message
 *
 * @param call     Pointer to call for which outgoing Q931 message has to be
 *                 retrieved.
 * @param msgbuf   Pointer to a buffer in which retrieved message will
 *                 be returned.
 * @param len      Pointer to int in which length of the buffer will
 *                 be returned.
 * @param msgType  Pointer to integer in which message type of the ougoing
 *                 message is returned.
 *
 * @return         Completion status - 0 on success, -1 on failure
 */
EXTERN int ooGetOutgoingQ931Msgbuf
(struct OOH323CallData *call, ASN1OCTET * msgbuf, int* len, int *msgType);

/**
 * This function is invoked to send a ReleaseComplete message for
 * the currently active call.
 *
 * @param call    Pointer to the call for which ReleaseComplete message have
 *                to be sent.
 *
 * @return         Completion status - 0 on success, -1 on failure
 */
EXTERN int ooSendReleaseComplete(struct OOH323CallData *call);

/**
 * This function is invoked to send a call proceeding message in response to
 * received setup message.
 *
 * @param call    Pointer to the call for which CallProceeding message have to
 *                be sent.
 *
 * @return        Completion status - 0 on success, -1 on failure
 */
EXTERN int ooSendCallProceeding(struct OOH323CallData *call);

/**
 * This function is invoked to send alerting message in response to received
 * setup message.
 *
 * @param call     Pointer to the call for which Alerting message have to be
 *                 sent.
 *
 * @return         Completion status - 0 on success, -1 on failure
 */
EXTERN int ooSendAlerting(struct OOH323CallData *call);

EXTERN int ooSendProgress(struct OOH323CallData *call);

EXTERN int ooSendStatus(struct OOH323CallData *call);

EXTERN int ooSendStatusInquiry(struct OOH323CallData *call);

/**
 * This function is invoked to send Facility message.
 *
 * @param call     Pointer to the call for which Facility message have to be
 *                 sent.
 *
 * @return         Completion status - 0 on success, -1 on failure
 */
EXTERN int ooSendFacility(struct OOH323CallData *call);


/**
 * This function is used to send dtmf data as Q931 keypad information element
 * as part of information message.
 * @param call     Pointer to the call for dtmf data has to be sent.
 * @param data     Dtmf data to be sent.
 *
 * @return         OO_OK, on success; OO_FAILED, on failure.
 */
EXTERN int ooQ931SendDTMFAsKeyPadIE
          (struct OOH323CallData *call, const char* data);

/**
 * This function is invoked to send a Connect message in response to received
 * setup message.
 *
 * @param call      Pointer to the call for which connect message has to be
 *                  sent.
 *
 * @return          Completion status - 0 on success, -1 on failure
 */
EXTERN int ooSendConnect(struct OOH323CallData *call);

/**
 * This function is used to send a SETUP message for outgoing call. It first
 * creates an H.225 TCP connection with the remote end point and then sends
 * SETUP message over this connection.
 * @param dest      Destination - IP:Port/alias.
 * @param callToken Unique token for the new call.
 * @param opts      Call specific options. If passed a non-null value, these
 *                  options will override global endpoint settings.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooH323MakeCall(char *dest, char *callToken, ooCallOptions *opts);

/**
 * Helper function used to make a call once it is approved by the Gk.
 * In case of no gk, this function is directly called to make a call.
 * @param call        Handle to the new call.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure
 */
int ooH323CallAdmitted( struct OOH323CallData *call);

/**
 * This function is used to handle a call forward request sent to local
 * endpoint by remote endpoint.
 * @param call        Handle to the call which is being forwarded
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323HandleCallFwdRequest(struct OOH323CallData *call);

/**
 * This function is used for forwarding/redirecting a call to third party.
 * @param callToken   callToken for the call which has to be redirected.
 * @param dest        Address to which call has to be forwarded. Can be
 *                    IP:Port or alias.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323ForwardCall(char* callToken, char *dest);

/**
 * This function is used to hangup a currently active call. It sets the call
 * state to CLEARING and initiates closing of all logical channels.
 * @param callToken Unique token of the call to be hanged.
 * @param reason    Reason for ending call.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323HangCall(char * callToken, OOCallClearReason reason, int q931);


/**
 * Function to accept a call by sending connect. This function is used
 * as a helper function to ooSendConnect.
 * @param call      Pointer to the call for which connect has to be sent
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooAcceptCall(struct OOH323CallData *call);

/*
 * An helper function to ooMakeCall.
 * @param call      Pointer to the new call.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323MakeCall_helper(struct OOH323CallData *call);

/**
 * This function is used to parse the destination
 * @param call      Handle to related call.
 * @param dest      Destination string to be parsed.
 * @param parsedIP  Pointer to buffer in which parsed ip:port will be returned.
 * @param len       Length of the buffer passed.
 * @param aliasList Aliases List in which new aliases will be added.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
int ooParseDestination
   (struct OOH323CallData *call, char *dest, char *parsedIP, unsigned len,
    OOAliases** aliasList);

/**
 * This function is used to generate a new call token
 * @param callToken Handle to the buffer in which new call token will be
 *                  returned
 * @param size      size of the buffer
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
int ooGenerateCallToken (char *callToken, size_t size);


/**
 * This function sends an encoded H.245 message buffer as a tunneled
 * H.245 Facility message.
 * @param call             Pointer to the call for which H.245 message has to
 *                         be tunneled.
 * @param msgbuf           Pointer to the encoded H.245 message to be tunneled.
 *
 * @param h245Len          Length of the encoded H.245 message buffer.
 * @param h245MsgType      Type of the H245 message
 * @param associatedChan   The logical channel number with which the tunneled
 *                         message is associated. In case of no channel, this
 *                         value should be 0.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooSendAsTunneledMessage
(struct OOH323CallData *call, ASN1OCTET* msgbuf,
 int h245Len, int h245MsgType, int associatedChan);


/**
 * This function is used to encode an H.225 message.
 * @param call            Handle to the call.
 * @param pq931Msg        Pointer to the message to be encoded.
 * @param msgbuf          Pointer to the buffer in which encoded message will
 *                        be returned.
 * @param size            Size of the buffer passed.
 *
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */
int ooEncodeH225Message(struct OOH323CallData *call, Q931Message *pq931Msg,
                        char *msgbuf, int size);

/**
 * This is a callback function which is called when there is no CONNECT
 * response from the remote endpoint after the SETUP has been sent and timeout
 * period has passed.
 * @param data            The callback data registered at the time of timer
 *                        creation.
 *
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */
int ooCallEstbTimerExpired(void *data);



/**
 * This function is used to add a keypad IE to a Q931 message for sending dtmf.
 * @param pmsg            Q931 message to which keypad ie has to be
 *                        added.
 * @param data            DTMF data to be sent.
 *
 * @return                OO_OK on success, OO_FAILED, on failure.
 */
EXTERN int ooQ931SetKeypadIE(OOCTXT* pctxt, Q931Message *pmsg, const char* data);

/**
 * This function is used to add a bearer capability IE to a Q931 message.
 * @param pmsg            Q931 message to which bearer capability ie has to be
 *                        added.
 * @param codingStandard  Coding standard to be used.
 * @param capability      Information transfer capability
 * @param transferMode    Information transfer mode.(circuit/packet modes).
 * @param transferRate    Information transfer rate.
 * @param userInfoLayer1  User information layer 1 protocol.
 *
 * @return                OO_OK on success, OO_FAILED, on failure.
 */
EXTERN int ooSetBearerCapabilityIE
   (OOCTXT* pctxt, Q931Message *pmsg, enum Q931CodingStandard codingStandard,
    enum Q931InformationTransferCapability capability,
    enum Q931TransferMode transferMode, enum Q931TransferRate transferRate,
    enum Q931UserInfoLayer1Protocol userInfoLayer1);

/**
 * This function is used to add a called party number ie to a q931 message.
 * @param pmsg            Q931 message to which CalledPartyNumber IE has to be
 *                        added.
 * @param number          Number for called party.
 * @param plan            Numbering Plan used
 * @param type            Type of number
 *
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooQ931SetCalledPartyNumberIE
   (OOCTXT *pctxt, Q931Message *pmsg, const char *number, unsigned plan, unsigned type);


/**
 * This function is used to add a CallingPartyNumber ie to a q931 message.
 * @param pmsg            Q931 message to which CallingPartyNumber IE has to be
 *                        added.
 * @param number          Number for calling party.
 * @param plan            Numbering Plan used
 * @param type            Type of number
 * @param presentation    Presentation of the address is allowed or restricted.
 * @param screening       Whether address was provided by endpoint or screened
 *                        by gatekeeper.
 *
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooQ931SetCallingPartyNumberIE
   (OOCTXT* pctxt, Q931Message *pmsg, const char *number, unsigned plan, unsigned type,
    unsigned presentation, unsigned screening);

/**
 * This function is used to set a cause ie for a q931 message.
 * @param pmsg        Valid Q931 Message
 * @param cause       Q931 Cause Value
 * @param coding      coding standard used. 0 for ITU-T standard coding
 * @param location    location. 0 for user.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooQ931SetCauseIE
   (OOCTXT *pctxt, Q931Message *pmsg,enum Q931CauseValues cause, unsigned coding,
    unsigned location);

EXTERN int ooQ931SetCallStateIE
    (OOCTXT *pctxt, Q931Message *pmsg, unsigned char callstate);

/**
 * This function is used to convert a call clear reason to cause and
 * reason code. It is used when local user is endoing the call and
 * sending releaseComplete.
 * @param clearReason   Reason for ending call.
 * @param cause         Pointer to Q931CauseVaules enum in which cause
 *                      will be returned.
 * @param reasonCode    Pointer to unsigned int in which reasonCode will
 *                      be returned.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooQ931GetCauseAndReasonCodeFromCallClearReason
   (OOCallClearReason clearReason, enum Q931CauseValues *cause,
    unsigned *reasonCode);

/**
 * This function is used to convert a cause value and reason code received
 * in ReleaseComplete message from remote endpoint into a CallClearReason.
 * @param cause         cause value received.
 * @param reasonCode    reasonCode received.
 *
 * @return              Returns a CallClearReason.
 */
EXTERN OOCallClearReason ooGetCallClearReasonFromCauseAndReasonCode
   (enum Q931CauseValues cause, unsigned reasonCode);

/**
 * This function is used to retrieve the description text for a
 * message type.
 *
 * @param msgType  Message type.
 * @return         The text description string.
 */
EXTERN const char* ooGetMsgTypeText (int msgType);

/**
 * This function is used to retrieve the text description for a Q931 Cause
 * value in Cause IE.
 * @param val     Q931 Cause value
 * @return        The text description string
 */
EXTERN const char* ooGetQ931CauseValueText (int val);

EXTERN int ooH323NewCall(char *callToken);

EXTERN char* ooQ931GetMessageTypeName(int messageType, char* buf);
EXTERN char* ooQ931GetIEName(int number, char* buf);
EXTERN int ooSendTCSandMSD(struct OOH323CallData *call);
EXTERN int ooSendStartH245Facility(struct OOH323CallData *call);
EXTERN int ooSendFSUpdate(struct OOH323CallData *call);
EXTERN int ooHandleFastStartChannels(struct OOH323CallData *pCall);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif /* __Q931HDR_H */
