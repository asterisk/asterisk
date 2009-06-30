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
 * @file ooh245.h 
 * This file contains functions to support H245 negotiations. 
 */
#ifndef _OOH245HDR_H_
#define _OOH245HDR_H_

#include "ooasn1.h"
#include "ooCapability.h"
#include "oochannels.h"
#include "ootrace.h"

#include "ooq931.h"
#include "MULTIMEDIA-SYSTEM-CONTROL.h"

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

struct OOH323CallData;

/** 
 * @defgroup h245 H.245 Message Handling
 * @{
 */
/**
 * Defines the H.245 message structure. All request/response
 * and command messages are represented using this structure.
 */
typedef struct H245Message {
   H245MultimediaSystemControlMessage h245Msg;
   ASN1UINT msgType;
   ASN1INT  logicalChannelNo;
} H245Message;

/**
 * Creates an outgoing H245 message of the type specified by the type
 * argument for the Application context. 
 *
 * @param msg       A pointer to pointer to message which will be assigned to 
 *                  allocated memory.
 * @param type      Type of the message to be created.
 *                  (Request/Response/Command/Indication)
 *
 * @return          Completion status of operation: 0 (OO_OK) = success,
 *                  negative return value is error.         
 */
EXTERN int ooCreateH245Message(H245Message **msg, int type);

/**
 * Frees up the memory used by the H245 message.
 *
 * @param call      Handle to the call
 * @param pmsg      Pointer to an H245 message structure.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure         
 */
EXTERN int ooFreeH245Message(struct OOH323CallData *call, H245Message *pmsg);

/**
 * This function is used to enqueue an H.245 message into an outgoing queue for
 * the call.
 * @param call      Pointer to call for which message has to be enqueued.
 * @param msg       Pointer to the H.245 message to be sent.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooSendH245Msg(struct OOH323CallData *call, H245Message *msg);

/**
 * This function is used to retrieve an H.245 message enqueued in the outgoing 
 * queue. 
 * @param call      Pointer to the call for which message has to be retrieved.
 * @param msgbuf    Pointer to a buffer in which the message will be returned.
 * @param len       Pointer to an int variable which will contain length of
 *                  the message data after returning.
 * @param msgType   Pointer to an int variable, which will contain message type
 *                  on return from the function.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooGetOutgoingH245Msgbuf(struct OOH323CallData *call, 
                                   ASN1OCTET *msgbuf, 
                                   int *len, int *msgType);

/**
 * This function is used to send out a terminal capability set message. 
 *
 * @param call      Pointer to a call for which TerminalCapabilitySet message
 *                  will be sent.
 * 
 * @return          OO_OK, on success. OO_FAILED, on failure.  
 */
EXTERN int ooSendTermCapMsg(struct OOH323CallData *call);

/**
 * This function is used to generate a random status determination number
 * for MSD procedure.
 *
 * @return          Generated status determination number.
 */
EXTERN ASN1UINT ooGenerateStatusDeterminationNumber();

/**
 * This fuction is used to handle received MasterSlaveDetermination procedure
 * messages. 
 * @param call       Pointer to the call for which a message is received.
 * @param pmsg       Pointer to MSD message
 * @param msgType    Message type indicating whether received message is MSD, 
 *                   MSDAck, MSDReject etc...
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooHandleMasterSlave
(struct OOH323CallData *call, void * pmsg, int msgType);

/**
 * This function is used to send MSD message.
 * @param call       Pointer to call for which MasterSlaveDetermination has to
 *                   be sent.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooSendMasterSlaveDetermination(struct OOH323CallData *call);

/**
 * This function is used to send a MasterSlaveDeterminationAck message.
 * @param call        Pointer to call for which MasterSlaveDeterminationAck has
 *                    to be sent.
 * @param status      Result of the determination process(Master/Slave as it 
 *                    applies to remote endpoint)
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */ 
EXTERN int ooSendMasterSlaveDeterminationAck
(struct OOH323CallData* call, char * status);

/**
 * This function is used to send a MasterSlaveDeterminationReject message.
 * @param call        Pointer to call for which message is to be sent.
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */ 
EXTERN int ooSendMasterSlaveDeterminationReject (struct OOH323CallData* call);


/**
 * This function is used to handle MasterSlaveReject message. If number of
 * retries is less than max allowed, then it restarts the 
 * MasterSlaveDetermination procedure.
 * @param call        Handle to the call for which MasterSlaveReject is 
 *                    received.
 * @param reject      Poinetr to the received reject message.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure. 
 */
EXTERN int ooHandleMasterSlaveReject
   (struct OOH323CallData *call, H245MasterSlaveDeterminationReject* reject);

/**
 * This function is used to handle received OpenLogicalChannel message.
 * @param call        Pointer to call for which OpenLogicalChannel message is
 *                    received.
 * @param olc         Pointer to the received OpenLogicalChannel message.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooHandleOpenLogicalChannel
                   (struct OOH323CallData* call, H245OpenLogicalChannel *olc);

/**
 * This is a helper function used to handle a received OpenLogicalChannel 
 * message. It builds an OpenLogicalChannelAck message and sends it.
 *
 * @param call        Pointer to cll for which OLC was received.
 * @param olc         The received OpenLogicalChannel message.
 * 
 * @return            OO_OK, on success. OO_FAILED, on failure.         
 */
EXTERN int ooHandleOpenLogicalChannel_helper
(struct OOH323CallData *call, H245OpenLogicalChannel*olc);

/**
 * This function is used to build and send OpenLogicalChannelReject message.
 * @param call        Pointer to call for which OLCReject has to be sent.
 * @param channelNum  LogicalChannelNumber to be rejected.
 * @param cause       Cause of rejection.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
int ooSendOpenLogicalChannelReject
   (struct OOH323CallData *call, ASN1UINT channelNum, ASN1UINT cause);

/**
 * This function is used to handle a received OpenLogicalChannelAck message.
 * @param call         Pointer to call for which OLCAck is received
 * @param olcAck       Pointer to received olcAck message.
 *
 * @return             OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedOpenLogicalChannelAck(struct OOH323CallData *call, 
                                            H245OpenLogicalChannelAck *olcAck);


/**
 * This function is used to handle the received OpenLogicalChannelReject 
 * message.
 * @param call         Handle to the call for which the message is received.
 * @param olcRejected  Pointer to received OpenLogicalChannelReject message.
 *
 * @return             OO_OK, on success. OO_FAILED, on failure.
 */
int ooOnReceivedOpenLogicalChannelRejected(struct OOH323CallData *call, 
                                    H245OpenLogicalChannelReject *olcRejected);
/**
 * This message is used to send an EndSession command. It builds a EndSession 
 * command message and queues it into the calls outgoing queue.
 * @param call          Pointer to call for which EndSession command has to be
 *                      sent.
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooSendEndSessionCommand(struct OOH323CallData *call);

/**
 * This function is used to handle a received H245Command message. 
 * @param call          Pointer to call for which an H245Command is received.
 * @param command       Pointer to a command message.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooHandleH245Command
(struct OOH323CallData *call, H245CommandMessage *command);


/**
 * This function is used to handle a received UserInput Indication message.
 * It extracts the dtmf received through user-input message and calls endpoints
 * onReceivedDTMF callback function, if such a function is registered by the 
 * endpoint.
 * @param call         Handle to the call for which user-input indication
 *                     message is received.
 * @param indication   Handle to the received user-input indication message.
 *
 * @return             OO_OK, on success; OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedUserInputIndication
   (OOH323CallData *call, H245UserInputIndication *indication);

/**
 * This function is called on receiving a TreminalCapabilitySetAck message.
 * If the MasterSlaveDetermination process is also over, this function 
 * initiates the process of opening logical channels.
 * @param call          Pointer to call for which TCSAck is received.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedTerminalCapabilitySetAck(struct OOH323CallData* call);

/**
 * This function is called to close all the open logical channels. It sends
 * CloseLogicalChannel message for all the forward channels and sends 
 * RequestCloseLogicalChannel message for all the reverse channels.
 * @param call          Pointer to call for which logical channels have to be 
 *                      closed.
 *
 * @return              OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCloseAllLogicalChannels(struct OOH323CallData *call);


/**
 * This function is used to send out a CloseLogicalChannel message for a particular
 * logical channel.
 * @param call           Pointer to a call, to which logical channel to be closed belongs.
 * @param logicalChan    Pointer to the logical channel to be closed.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooSendCloseLogicalChannel
(struct OOH323CallData *call, ooLogicalChannel *logicalChan);

/**
 * This function is used to process a received closeLogicalChannel request. It closes the
 * logical channel and removes the logical channel entry from the list. It also, sends
 * closeLogicalChannelAck message to the remote endpoint.
 * @param call           Pointer to call for which CloseLogicalChannel message is received.
 * @param clc            Pointer to received CloseLogicalChannel message.
 * 
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedCloseLogicalChannel(struct OOH323CallData *call, 
                                           H245CloseLogicalChannel* clc);

/**
 * This function is used to process a received CloseLogicalChannelAck message. It closes the
 * channel and removes it from the list of active logical channels.
 * @param call           Pointer to call for which CLCAck message is received.
 * @param clcAck         Pointer to the received CloseLogicalChannelAck message.
 * 
 * @return               OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooOnReceivedCloseChannelAck(struct OOH323CallData* call, 
                                           H245CloseLogicalChannelAck* clcAck);

/**
 * This function is used to handle received H245 message. Based on the type of message received,
 * it calls helper functions to process those messages.
 * @param call           Pointer to call for which a message is received.
 * @param pmsg           Pointer to the received H245 message.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooHandleH245Message
(struct OOH323CallData *call, H245Message * pmsg);

/**
 * This function is used to process received TCS message. It builds TCSAck message and queues it
 * into the calls outgoing queue. Also, starts Logical channel opening procedure if TCS and MSD
 * procedures have finished.
 * @param call           Pointer to call for which TCS is received.
 * @param pmsg           Pointer to the received message.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedTerminalCapabilitySet
(struct OOH323CallData *call, H245Message *pmsg);

/**
 * This function is used to send a TCSAck message to remote endpoint.
 * @param call           Pointer to call on which TCSAck has to be sent.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH245AcknowledgeTerminalCapabilitySet(struct OOH323CallData *call);

/**
 * This function is used to start OpenLogicalChannel procedure for all the
 * channels to be opened for the call.
 * @param call            Pointer to call for which logical channels have to be opened.
 *
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */ 
EXTERN int ooOpenLogicalChannels(struct OOH323CallData *call);

/**
 * This function is used to send OpenLogicalChannel message for audio/video 
 * channel.
 * @param call            Pointer to call for which  channel has to be opened.
 * @param capType         Type of media channel.
 *
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOpenLogicalChannel(struct OOH323CallData *call, 
                                enum OOCapType capType);

/**
 * This function is used to build and send OpenLogicalChannel message using
 *  capability passed as parameter.
 * @param call            Pointer to call for which OpenLogicalChannel message 
 *                        has to be built.
 * @param epCap           Pointer to capability
 * 
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOpenChannel
(struct OOH323CallData* call, ooH323EpCapability *epCap);

/**
 * This function is used to send dtmf digits as user input indication message
 * contating alphanumeric string.
 * @param call            Handle to the call for which dtmf has to be sent.
 * @param data            DTMF data
 *
 * @return                OO_OK, on success; OO_FAILED, on failure.
 */
EXTERN int ooSendH245UserInputIndication_alphanumeric
   (OOH323CallData *call, const char *data);

/**
 * This function is used to send dtmf digits as user input indication message
 * contating dtmf signal type.
 * @param call            Handle to the call for which dtmf has to be sent.
 * @param data            DTMF data
 *
 * @return                OO_OK, on success; OO_FAILED, on failure.
 */
EXTERN int ooSendH245UserInputIndication_signal
   (OOH323CallData *call, const char *data);

/**
 * This function is used to request a remote end point to close a logical
 * channel. 
 * @param call            Pointer to call for which the logical channel has to
 *                        be closed.
 * @param logicalChan     Pointer to the logical channel structure which needs
 *                        to be closed.
 *
 * @return                OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooSendRequestCloseLogicalChannel(struct OOH323CallData *call, 
                                            ooLogicalChannel *logicalChan);

/**
 * This function is used to send a RequestChannelCloseRelease message when the
 * corresponding timer has expired.
 * @param call            Handle to the call
 * @param channelNum      Channel number.
 *
 * @return                OO_OK, on success. OO_FAILED, otherwise.
 */
int ooSendRequestChannelCloseRelease
(struct OOH323CallData *call, int channelNum);

/**
 * This function handles the received RequestChannelClose message, verifies
 * that the requested channel is forward channel. It sends an acknowledgement
 * for the message followed by CloseLogicalChannel message.
 * @param call             Pointer to the call for which RequestChannelClose is
 *                         received.
 * @param rclc             Pointer to the received message.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedRequestChannelClose(struct OOH323CallData *call, 
                                           H245RequestChannelClose *rclc);

/**
 * This function is used to handle a received RequestChannelCloseReject 
 * response message.
 * @param call             Handle to the call.
 * @param rccReject        Pointer to the received reject response message.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
int ooOnReceivedRequestChannelCloseReject
   (struct OOH323CallData *call, H245RequestChannelCloseReject *rccReject);

/**
 * This function is used to handle a received RequestChannelCloseAck 
 * response message.
 * @param call             Handle to the call.
 * @param rccAck           Pointer to the received ack response message.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
int ooOnReceivedRequestChannelCloseAck
   (struct OOH323CallData *call, H245RequestChannelCloseAck *rccAck);

/**
 * Builds an OLC for faststart with an audio/video capability passed as 
 * parameter.
 * @param call             Handle to call for which OLC has to be built.
 * @param olc              Pointer to an OLC structure which will be populated.
 * @param epCap            Pointer to the capability which will be used to 
 *                         build OLC.
 * @param pctxt            Pointer to an OOCTXT structure which will be used 
 *                         to allocate additional memory for OLC.
 * @param dir              Direction of OLC
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooBuildFastStartOLC(struct OOH323CallData *call, 
                                          H245OpenLogicalChannel *olc, 
                                          ooH323EpCapability *epCap, 
                                          OOCTXT*pctxt, int dir);

/**
 * Prepares a faststart response olc from the olc received in SETUP message.
 * This function just changes the mediaChannel and mediaControl channel part
 * of the olc received in SETUP.
 * @param call             Handle to call for which OLC has to be built.
 * @param olc              Pointer to an received OLC structure.
 * @param epCap            Pointer to the capability which will be used for 
 *                         this channel.
 * @param pctxt            Pointer to an OOCTXT structure which will be used 
 *                         to allocate additional memory for OLC.
 * @param dir              Direction of channel OORX, OOTX etc.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooPrepareFastStartResponseOLC
   (OOH323CallData *call, H245OpenLogicalChannel *olc, 
    ooH323EpCapability *epCap, OOCTXT*pctxt, int dir);

/**
 * This function is used to encode an H245 message and return encoded data
 * into the buffer passed as a parameter to the function.
 * @param call            Handle to the call
 * @param ph245Msg        Handle to the message to be encoded.
 * @param msgbuf          buffer in which encoded message will be returned.
 * @param size            Size of the buffer.
 *
 * @return                OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooEncodeH245Message
(struct OOH323CallData *call, H245Message *ph245Msg, char *msgbuf, int size);

/**
 * This function is used to send a master-slave determination release message.
 * @param call             Handle to call, for which MSDRelease message has
 *                         to be sent.
 *
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
int ooSendMasterSlaveDeterminationRelease(struct OOH323CallData * call);

/**
 * This function is used to send a terminal capability set reject message
 * to the remote endpoint.
 * @param call             Handle to the call for which reject message has to 
 *                         be sent.
 * @param seqNo            Sequence number of the TCS message to be rejected.
 * @param cause            Cause for rejecting a TCS message.
 *
 * @return                 OO_OK, on success; OO_FAILED, otherwise.
 */
int ooSendTerminalCapabilitySetReject
    (struct OOH323CallData *call, int seqNo, ASN1UINT cause);

/**
 * This function is used to send a TerminalCapabilitySetRelease message after
 * capability exchange timer has expired.
 * @param call            Handle to call for which release message has to be 
 *                        sent.
 *
 * @return                OO_OK, on success; OO_FAILED, on failure.
 */
int ooSendTerminalCapabilitySetRelease(struct OOH323CallData * call);


/**
 * This is an helper function used to extract ip address and port info from 
 * H245TransportAddress structure.
 * @param call           Handle to associated call.
 * @param h245Address    Handle to H245TransportAddress structure from which 
 *                       information has to be extracted.
 * @param ip             Pointer to buffer in which ip address will be 
 *                       returned. Make sure that buffer has sufficient length.
 * @param port           Pointer to integer in which port number will be 
 *                       returned.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
int ooGetIpPortFromH245TransportAddress
   (OOH323CallData *call, H245TransportAddress *h245Address, char *ip, 
    int *port);

/**
 * This is a callback function for handling an expired master-slave 
 * determination timer.
 * @param data             Callback data registered at the time of creation of 
 *                         the timer.
 *
 * @return                 OO_OK, on success. OO_FAILED, otherwise.
 */
int ooMSDTimerExpired(void *data);

/**
 * This is a callback function for handling an expired capability exchange 
 * timer.
 * @param data             Callback data registered at the time of creation of 
 *                         the timer.
 *
 * @return                 OO_OK, on success. OO_FAILED, otherwise.
 */
int ooTCSTimerExpired(void *data);

/**
 * This is a callback function for handling an expired OpenLogicalChannel 
 * timer.
 * @param pdata            Callback data registered at the time of creation of 
 *                         the timer.
 *
 * @return                 OO_OK, on success. OO_FAILED, otherwise.
 */
int ooOpenLogicalChannelTimerExpired(void *pdata);

/**
 * This is a callback function for handling an expired CloseLogicalChannel 
 * timer.
 * @param pdata            Callback data registered at the time of creation of 
 *                         the timer.
 *
 * @return                 OO_OK, on success. OO_FAILED, otherwise.
 */
int ooCloseLogicalChannelTimerExpired(void *pdata);

/**
 * This is a callback function for handling an expired RequestChannelClose 
 * timer.
 * @param pdata            Callback data registered at the time of creation of 
 *                         the timer.
 *
 * @return                 OO_OK, on success. OO_FAILED, otherwise.
 */
int ooRequestChannelCloseTimerExpired(void *pdata);

/**
 * This is a callback function for handling an expired EndSession timer.
 * @param pdata            Callback data registered at the time of creation of 
 *                         the timer.
 *
 * @return                 OO_OK, on success. OO_FAILED, otherwise.
 */
int ooSessionTimerExpired(void *pdata);
/** 
 * @} 
 */
#ifdef __cplusplus
}
#endif

#endif
