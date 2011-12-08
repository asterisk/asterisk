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
 * @file ooh323ep.h 
 * This file contains H323 endpoint related functions. 
 */
#ifndef OO_H323EP_H_
#define OO_H323EP_H_
#include "ooCapability.h"
#include "ooCalls.h"
#include "ooGkClient.h"
#include "ooports.h"
#include "ooq931.h"

#define DEFAULT_TRACEFILE "trace.log"
#define DEFAULT_TERMTYPE 60
#define DEFAULT_PRODUCTID  "ooh323"
#define DEFAULT_CALLERID   "objsyscall"
#define DEFAULT_T35COUNTRYCODE 184
#define DEFAULT_T35EXTENSION 0
#define DEFAULT_MANUFACTURERCODE 39
#define DEFAULT_H245CONNECTION_RETRYTIMEOUT 2
#define DEFAULT_CALLESTB_TIMEOUT 60
#define DEFAULT_MSD_TIMEOUT 30
#define DEFAULT_TCS_TIMEOUT 30
#define DEFAULT_LOGICALCHAN_TIMEOUT 30
#define DEFAULT_ENDSESSION_TIMEOUT 15
#define DEFAULT_H323PORT 1720

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

struct OOCapPrefs;
/** 
 * @defgroup h323ep  H323 Endpoint management functions
 * @{
 */
/* Default port ranges */
#define TCPPORTSSTART 12000  /*!< Starting TCP port number */
#define TCPPORTSEND   62230  /*!< Ending TCP port number   */
#define UDPPORTSSTART 13030  /*!< Starting UDP port number */
#define UDPPORTSEND   13230  /*!< Ending UDP port number   */
#define RTPPORTSSTART 14030  /*!< Starting RTP port number */
#define RTPPORTSEND   14230  /*!< Ending RTP port number   */



  
/**
 * This structure is used to define the port ranges to be used
 * by the application.
 */
typedef struct OOH323Ports {
   int start;    /*!< Starting port number. */
   int max;      /*!< Maximum port number.  */
   int current;  /*!< Current port number.  */
} OOH323Ports;

/** 
 * Structure to store all configuration information related to the
 * endpoint created by an application 
 */
typedef struct OOH323EndPoint {
   
   /** 
    * This context should be used for allocation of memory for
    * items within the endpoint structure.
    */
   OOCTXT ctxt;

   /** 
    * This context should be used for allocation of memory for
    * message structures.
    */
   OOCTXT msgctxt;

   char   traceFile[MAXFILENAME];
   FILE * fptraceFile;

   /** Range of port numbers to be used for TCP connections */
   OOH323Ports tcpPorts;

   /** Range of port numbers to be used for UDP connections */
   OOH323Ports udpPorts;

   /** Range of port numbers to be used for RTP connections */
   OOH323Ports rtpPorts;
  
   ASN1UINT  flags;

   int termType; /* 50 - Terminal entity with No MC, 
                    60 - Gateway entity with no MC, 
                    70 - Terminal Entity with MC, but no MP etc.*/
   int t35CountryCode;
   int t35Extension;
   int manufacturerCode;
   const char *productID;
   const char *versionID;
   const char *callerid;
   char callingPartyNumber[50];
   OOSOCKET *stackSocket;
   OOAliases *aliases;

   int callType;

   struct ooH323EpCapability *myCaps;
   OOCapPrefs     capPrefs;
   int noOfCaps;
   OOH225MsgCallbacks h225Callbacks;
   OOH323CALLBACKS h323Callbacks;
   char signallingIP[2+8*4+7];
   int listenPort;
   OOSOCKET *listener;
   OOH323CallData *callList;

   OOCallMode callMode; /* audio/audiorx/audiotx/video/fax */
   int dtmfmode;
   ASN1UINT callEstablishmentTimeout;
   ASN1UINT msdTimeout;
   ASN1UINT tcsTimeout;
   ASN1UINT logicalChannelTimeout;
   ASN1UINT sessionTimeout;
   int cmdPipe[2];
   struct ooGkClient *gkClient;

   OOInterface *ifList; /* interface list for the host we are running on*/
   OOBOOL isGateway;
   OOSOCKET cmdSock;
   OOBOOL v6Mode;
} OOH323EndPoint;

#define ooEndPoint OOH323EndPoint

/**
 * This function is the first function to be invoked before using stack. It
 * initializes the H323 Endpoint.
 * @param callMode       Type of calls to be made(audio/video/fax).
 *                       (OO_CALLMODE_AUDIO, OO_CALLMODE_VIDEO)
 * @param tracefile      Trace file name.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooH323EpInitialize
   (enum OOCallMode callMode, const char* tracefile);

/**
 * This function is used to represent the H.323 application endpoint as 
 * gateway, instead of an H.323 phone endpoint.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetAsGateway(void);

EXTERN void ooH323EpSetVersionInfo(int t35countrycode, int t35extensions, int manufacturer, 
				  char* vendor, char* version);

/**
 * This function is used to assign a local ip address to be used for call
 * signalling.
 * @param localip        Dotted IP address to be used for call signalling.
 * @param listenport     Port to be used for listening for incoming calls.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */ 
EXTERN int ooH323EpSetLocalAddress(const char* localip, int listenport);

/**
 * This function is used to set the range of tcp ports the application will
 * use for tcp transport.
 * @param base           Starting port number for the range
 * @param max            Ending port number for the range.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.    
 */
EXTERN int ooH323EpSetTCPPortRange(int base, int max);

/**
 * This function is used to set the range of udp ports the application will
 * use for udp transport.
 * @param base           Starting port number for the range
 * @param max            Ending port number for the range.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.    
 */
EXTERN int ooH323EpSetUDPPortRange(int base, int max);

/**
 * This function is used to set the range of rtp ports the application will
 * use for media streams.
 * @param base           Starting port number for the range
 * @param max            Ending port number for the range.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.    
 */
EXTERN int ooH323EpSetRTPPortRange(int base, int max);

/**
 * This function is used to set the trace level for the H.323 endpoint.
 * @param traceLevel     Level of tracing.
 *
 * @return               OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooH323EpSetTraceLevel(int traceLevel);

/**
 * This function is used to add the h323id alias for the endpoint.
 * @param h323id         H323-ID to be set as alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasH323ID(const char* h323id);

/**
 * This function is used to add the dialed digits alias for the
 * endpoint.
 * @param dialedDigits   Dialed-Digits to be set as alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasDialedDigits(const char* dialedDigits);

/**
 * This function is used to add the url alias for the endpoint.
 * @param url            URL to be set as an alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasURLID(const char* url);

/**
 * This function is used to add an email id as an alias for the endpoint.
 * @param email          Email id to be set as an alias.
 * 
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasEmailID(const char* email);

/**
 * This function is used to add an ip address as an alias.
 * @param ipaddress      IP address to be set as an alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasTransportID(const char* ipaddress);

/**
 * This function is used to clear all the aliases used by the 
 * H323 endpoint.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpClearAllAliases(void);

/**
 * This function is used to set the H225 message callbacks for the
 * endpoint.
 * @param h225Callbacks  Callback structure containing various callbacks.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetH225MsgCallbacks(OOH225MsgCallbacks h225Callbacks);

/**
 * This function is used to set high level H.323 callbacks for the endpoint.
 * Make sure all unused callbacks in the structure are set to NULL before 
 * calling this function.
 * @param h323Callbacks    Callback structure containing various high level 
 *                         callbacks.
 * @return                 OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooH323EpSetH323Callbacks(OOH323CALLBACKS h323Callbacks);


/**
 * This function is the last function to be invoked after done using the 
 * stack. It closes the H323 Endpoint for an application, releasing all 
 * the associated memory.
 *
 * @return          OO_OK on success
 *                  OO_FAILED on failure
 */
EXTERN int ooH323EpDestroy(void);


/**
 * This function is used to enable the auto answer feature for
 * incoming calls
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableAutoAnswer(void);

/**
 * This function is used to disable the auto answer feature for
 * incoming calls.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableAutoAnswer(void);

/**
 * This function is used to enable manual ringback. By default the stack sends 
 * alerting message automatically on behalf of the endpoint application. 
 * However, if endpoint application wants to do alerting user part first before
 * sending out alerting message, it can enable this feature.
 *
 * @return          OO_OK on success, OO_FAILED on failure
 */
EXTERN int ooH323EpEnableManualRingback(void);

/**
 * This function is used to disable manual ringback. By default the 
 * manual ringback feature is disabled, i.e, the stack sends alerting on behalf
 * of the application automatically.
 *
 * @return          OO_OK on success, OO_FAILED on failure
 */
EXTERN int ooH323EpDisableManualRingback(void);

/**
 * This function is used to enable MediaWaitForConnect.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableMediaWaitForConnect(void);

/**
 * This function is used to disable MediaWaitForConnect.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableMediaWaitForConnect(void);

/**
 * This function is used to enable faststart.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableFastStart(void);

/**
 * This function is used to disable faststart.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableFastStart(void);

/**
 * This function is used to enable tunneling.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableH245Tunneling(void);

/**
 * This function is used to disable tunneling.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableH245Tunneling(void);

/**
 * This function is used to setup/clear TryBeMaster flag
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpTryBeMaster(int);

/**
 * This function is used to enable GkRouted calls.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableGkRouted(void);

/**
 * This function is used to disable Gkrouted calls.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableGkRouted(void);

/**
 * This function is used to set the product ID.
 * @param productID  New value for the product id.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */ 
EXTERN int ooH323EpSetProductID (const char * productID);

/**
 * This function is used to set version id.
 * @param versionID  New value for the version id.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetVersionID (const char * versionID);

/**
 * This function is used to set callerid to be used for outbound
 * calls.
 * @param callerID  New value for the caller id.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetCallerID (const char * callerID);

/**
 * This function is used to set calling party number to be used for outbound
 * calls.Note, you can override it for a specific call by using 
 * ooCallSetCallingPartyNumber function.
 * @param number   e164 number to be used as calling party number.
 *
 * @return         OO_OK, on success; OO_FAILED, otherwise.
 */
EXTERN int ooH323EpSetCallingPartyNumber(const char * number);

/**
 * This function is used to print the current configuration information of 
 * the H323 endpoint to log file.
 */
void ooH323EpPrintConfig(void);


/**
 * This function is used to add G728 capability to the H323 endpoint.
 * @param cap                  Type of G728 capability to be added.
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
EXTERN int ooH323EpAddG728Capability
   (int cap, int txframes, int rxframes, int dir, 
    cb_StartReceiveChannel startReceiveChannel,
    cb_StartTransmitChannel startTransmitChannel,
    cb_StopReceiveChannel stopReceiveChannel,
    cb_StopTransmitChannel stopTransmitChannel);

/**
 * This function is used to add G729 capability to the H323 endpoint.
 * @param cap                  Type of G729 capability to be added.
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
EXTERN int ooH323EpAddG729Capability
   (int cap, int txframes, int rxframes, int dir, 
    cb_StartReceiveChannel startReceiveChannel,
    cb_StartTransmitChannel startTransmitChannel,
    cb_StopReceiveChannel stopReceiveChannel,
    cb_StopTransmitChannel stopTransmitChannel);


/**
 * This function is used to add G7231 capability to the H323 endpoint.
 * @param cap                  Type of G7231 capability to be added.
 * @param txframes             Number of frames per packet for transmission. 
 * @param rxframes             Number of frames per packet for reception.
 * @param silenceSuppression   Silence Suppression support
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooH323EpAddG7231Capability(int cap, int txframes, int rxframes, 
                              OOBOOL silenceSuppression, int dir,
                              cb_StartReceiveChannel startReceiveChannel,
                              cb_StartTransmitChannel startTransmitChannel,
                              cb_StopReceiveChannel stopReceiveChannel,
                              cb_StopTransmitChannel stopTransmitChannel);

/**
 * This function is used to add G711 capability to the H323 endpoint.
 * @param cap                  Type of G711 capability to be added.
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
EXTERN int ooH323EpAddG711Capability
   (int cap, int txframes, int rxframes, int dir, 
    cb_StartReceiveChannel startReceiveChannel,
    cb_StartTransmitChannel startTransmitChannel,
    cb_StopReceiveChannel stopReceiveChannel,
    cb_StopTransmitChannel stopTransmitChannel);


/**
 * This function is used to add a new GSM capability to the endpoint.
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
EXTERN int ooH323EpAddGSMCapability(int cap, ASN1USINT framesPerPkt, 
                             OOBOOL comfortNoise,OOBOOL scrambled,int dir, 
                             cb_StartReceiveChannel startReceiveChannel,
                             cb_StartTransmitChannel startTransmitChannel,
                             cb_StopReceiveChannel stopReceiveChannel,
                             cb_StopTransmitChannel stopTransmitChannel);
/**
 * This function is used to add H263 video capability to the H323 endpoint.
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
EXTERN int ooH323EpAddH263VideoCapability(int cap, unsigned sqcifMPI, 
                                 unsigned qcifMPI, unsigned cifMPI, 
                                 unsigned cif4MPI, unsigned cif16MPI, 
                                 unsigned maxBitRate, int dir, 
                                 cb_StartReceiveChannel startReceiveChannel,
                                 cb_StartTransmitChannel startTransmitChannel,
                                 cb_StopReceiveChannel stopReceiveChannel,
                                 cb_StopTransmitChannel stopTransmitChannel);

/**
 * This function is used to enable rfc 2833 support for the endpoint.
 * @param dynamicRTPPayloadType   Payload type value to use.
 *
 * @return                        OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooH323EpEnableDTMFRFC2833(int dynamicRTPPayloadType);

/**
 * This function is used to disable rfc 2833 support for the endpoint.
 *
 * @return                        OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooH323EpDisableDTMFRFC2833(void);

/**
 * This function is used to enable the H245(alphanumeric) dtmf capability for
 * the endpoint.
 * @return                        OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooH323EpEnableDTMFH245Alphanumeric(void);

/**
 * This function is used to disable the H245(alphanumeric) dtmf capability for
 * the endpoint.
 * @return                        OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooH323EpDisableDTMFH245Alphanumeric(void);

/**
 * This function is used to enable the H245(signal) dtmf capability for
 * the endpoint.
 * @return                        OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooH323EpEnableDTMFH245Signal(void);

/**
 * This function is used to disable the H245(signal) dtmf capability for
 * the endpoint.
 * @return                        OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooH323EpDisableDTMFH245Signal(void);

/**/
EXTERN int ooH323EpSetTermType(int value);
EXTERN int ooH323EpAddG726Capability(int cap, int txframes, int rxframes,
                              OOBOOL silenceSuppression, int dir,
                              cb_StartReceiveChannel startReceiveChannel,
                              cb_StartTransmitChannel startTransmitChannel,
                              cb_StopReceiveChannel stopReceiveChannel,
                              cb_StopTransmitChannel stopTransmitChannel);
EXTERN int ooH323EpAddAMRNBCapability(int cap, int txframes, int rxframes,
                              OOBOOL silenceSuppression, int dir,
                              cb_StartReceiveChannel startReceiveChannel,
                              cb_StartTransmitChannel startTransmitChannel,
                              cb_StopReceiveChannel stopReceiveChannel,
                              cb_StopTransmitChannel stopTransmitChannel);
EXTERN int ooH323EpAddAMRNBCapability(int cap, int txframes, int rxframes,
                              OOBOOL silenceSuppression, int dir,
                              cb_StartReceiveChannel startReceiveChannel,
                              cb_StartTransmitChannel startTransmitChannel,
                              cb_StopReceiveChannel stopReceiveChannel,
                              cb_StopTransmitChannel stopTransmitChannel);
EXTERN int ooH323EpAddSpeexCapability(int cap, int txframes, int rxframes,
                              OOBOOL silenceSuppression, int dir,
                              cb_StartReceiveChannel startReceiveChannel,
                              cb_StartTransmitChannel startTransmitChannel,
                              cb_StopReceiveChannel stopReceiveChannel,
                              cb_StopTransmitChannel stopTransmitChannel);
EXTERN int ooH323EpEnableDTMFCISCO(int dynamicRTPPayloadType);
EXTERN int ooH323EpDisableDTMFCISCO(void);

EXTERN int ooH323EpEnableDTMFQ931Keypad(void);
EXTERN int ooH323EpDisableDTMFQ931Keypad(void);

/**/

/**
 * This function is used to add callbacks to the gatekeeper client. If user
 * application wants to do some special processing of various gatekeeper client
 * events, that can be done through these callbacks.
 * @param gkClientCallbacks      Handle to the callback structure.Make sure all
 *                               the members of the structure are appropriately
 *                               initialized.
 *
 * @return                       OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetGkClientCallbacks(OOGKCLIENTCALLBACKS gkClientCallbacks);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif
