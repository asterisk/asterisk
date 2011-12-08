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
 * @file ooLogChan.h 
 * This file contains structures and functions for maintaining information
 * on logical channels within the stack.
 */
#ifndef _OOLOGCHAN_H_
#define _OOLOGCHAN_H_

#include "ootypes.h"
#include "ooCapability.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup logchan H.245 logical channel management
 * @{
 */
struct ooH323EpCapability;
struct OOH323CallData;

/** 
 * Logical channel states.
 */
typedef enum {
   OO_LOGICAL_CHAN_UNKNOWN, 
   OO_LOGICALCHAN_IDLE, 
   OO_LOGICALCHAN_PROPOSED, 
   OO_LOGICALCHAN_ESTABLISHED,
   OO_LOGICALCHAN_PROPOSEDFS
} OOLogicalChannelState;

/**
 * Structure to store information on logical channels for a call.
 */
typedef struct OOLogicalChannel {
   int  channelNo;
   int  sessionID;
   enum OOCapType type;
   char dir[10];  /* receive/transmit */
   char remoteIP[2+8*4+7];
   int  remoteMediaPort;
   int  remoteMediaControlPort;
   int  localRtpPort;
   int  localRtcpPort;
   char localIP[2+8*4+7];
   OOLogicalChannelState state;         
   struct ooH323EpCapability *chanCap;
   struct OOLogicalChannel *next;
} OOLogicalChannel;

#define ooLogicalChannel OOLogicalChannel

/**
 * This function is used to add a new logical channel entry into the list
 * of currently active logical channels.
 * @param call      Pointer to the call for which new logical channel 
 *                  entry has to be created.
 * @param channelNo Channel number for the new channel entry.
 * @param sessionID Session identifier for the new channel.
 * @param dir       Direction of the channel(transmit/receive)
 * @param epCap     Capability to be used for the new channel.
 *
 * @return          Pointer to logical channel, on success. NULL, on failure
 */
EXTERN ooLogicalChannel* ooAddNewLogicalChannel
   (struct OOH323CallData *call, int channelNo, int sessionID, 
    char *dir, ooH323EpCapability *epCap);

/**
 * This function is used to find a logical channel using the logical 
 * channel number as a key.
 * @param call          Pointer to the call for which logical channel is 
 *                      required.
 * @param channelNo     Forward Logical Channel number for the logical channel
 *
 * @return              Pointer to the logical channel if found, NULL 
 *                      otherwise.   
 */
EXTERN ooLogicalChannel* ooFindLogicalChannelByLogicalChannelNo
(struct OOH323CallData *call, int channelNo);

/**
 * This function is called when a new logical channel is established. It is 
 * particularly useful in case of faststart. When the remote endpoint selects 
 * one of the proposed alternatives, other channels for the same session type 
 * need to be closed. This function is used for that.
 *
 * @param call      Handle to the call which owns the logical channel.
 * @param pChannel  Handle to the newly established logical channel.
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnLogicalChannelEstablished
(struct OOH323CallData *call, OOLogicalChannel * pChannel);

/**
 * This function is used to retrieve a logical channel with a particular 
 * sessionID. Note that there can be two entries of logical channel, one in 
 * each direction. This function will return the first channel which has the 
 * same session ID.
 * @param call      Handle to the call which owns the channels to be searched.
 * @param sessionID Session id of the session which is to be searched for.
 * @param dir       Direction of the channel.(transmit/receive)
 *
 * @return          Returns a pointer to the logical channel if found, NULL 
 *                  otherwise.
 */
EXTERN ooLogicalChannel* ooGetLogicalChannel
(struct OOH323CallData *call, int sessionID, char *dir);

/**
 * This function is used to remove a logical channel from the list of 
 * channels within the call structure.
 * @param call              Pointer to the call from which logical channel has 
 *                          to be removed.
 * @param ChannelNo         Forward logical channel number of the channel to be
 *                          removed.
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooRemoveLogicalChannel (struct OOH323CallData *call, int ChannelNo);

/**
 * This function is used to cleanup a logical channel. It first stops media if
 * it is still active and then removes the channel from the list, freeing up 
 * all the associated memory.
 * @param call       Handle to the call which owns the logical channel.
 * @param channelNo  Channel number identifying the channel.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooClearLogicalChannel (struct OOH323CallData *call, int channelNo);

/**
 * This function is used to cleanup all the logical channels associated with 
 * the call.
 * @param call      Handle to the call which owns the channels.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooClearAllLogicalChannels (struct OOH323CallData *call);

/**
 * This function is used to find a logical channel from a received
 * H.245 Open Logical Channel (OLC) message.
 * @param call     Handle to the related call.
 * @param olc      Handle to the received OLC.
 *
 * @return         Returns the corresponding logical channel if found,
 *                 else returns NULL.
 */
EXTERN OOLogicalChannel * ooFindLogicalChannelByOLC
(struct OOH323CallData *call, H245OpenLogicalChannel *olc);

/**
 * This function is used to find a logical channel based on session Id,
 * direction of channel and datatype.
 * @param call       Handle to the call
 * @param sessionID  Session ID for the channel to be searched.
 * @param dir        Direction of the channel wrt local endpoint.
 *                   (transmit/receive)
 * @param dataType   Handle to the data type for the channel.
 *
 * @return           Logical channel, if found, NULL otherwise.
 */
EXTERN OOLogicalChannel * ooFindLogicalChannel
(struct OOH323CallData* call, int sessionID, char *dir, H245DataType* dataType);

EXTERN OOLogicalChannel* ooGetTransmitLogicalChannel(struct OOH323CallData *call);

/** 
 * @} 
 */

#ifdef __cplusplus
}
#endif

#endif
