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

#include "asterisk.h"
#include "asterisk/lock.h"

#include "ooCalls.h"
#include "ooh323ep.h"

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

OOLogicalChannel* ooAddNewLogicalChannel(OOH323CallData *call, int channelNo,
                                         int sessionID, char *dir,
                                         ooH323EpCapability *epCap)
{
   OOLogicalChannel *pNewChannel=NULL, *pChannel=NULL;
   OOMediaInfo *pMediaInfo = NULL;
   OOTRACEDBGC5("Adding new media channel for cap %d dir %s (%s, %s)\n",
               epCap->cap, dir, call->callType, call->callToken);
   /* Create a new logical channel entry */
   pNewChannel = (OOLogicalChannel*)memAlloc(call->pctxt,
                                                     sizeof(OOLogicalChannel));
   if(!pNewChannel)
   {
      OOTRACEERR3("ERROR:Memory - ooAddNewLogicalChannel - pNewChannel "
                  "(%s, %s)\n", call->callType, call->callToken);
      return NULL;
   }

   memset(pNewChannel, 0, sizeof(OOLogicalChannel));
   pNewChannel->channelNo = channelNo;
   pNewChannel->sessionID = sessionID;
   pNewChannel->state = OO_LOGICALCHAN_IDLE;
   pNewChannel->type = epCap->capType;
   /*   strcpy(pNewChannel->type, type);*/
   strcpy(pNewChannel->dir, dir);

   pNewChannel->chanCap = epCap;
   OOTRACEDBGC4("Adding new channel with cap %d (%s, %s)\n", epCap->cap,
                call->callType, call->callToken);
   /* As per standards, media control port should be same for all
      proposed channels with same session ID. However, most applications
      use same media port for transmit and receive of audio streams. Infact,
      testing of OpenH323 based asterisk assumed that same ports are used.
      Hence we first search for existing media ports for same session and use
      them. This should take care of all cases.
   */
   if(call->mediaInfo)
   {
      pMediaInfo = call->mediaInfo;
      while(pMediaInfo)
      {
         if(!strcmp(pMediaInfo->dir, dir) &&
            (pMediaInfo->cap == epCap->cap))
         {
            break;
         }
         pMediaInfo = pMediaInfo->next;
      }
   }

   if(pMediaInfo)
   {
      OOTRACEDBGC3("Using configured media info (%s, %s)\n", call->callType,
                   call->callToken);
      pNewChannel->localRtpPort = pMediaInfo->lMediaRedirPort ? pMediaInfo->lMediaRedirPort : pMediaInfo->lMediaPort;
      /* check MediaRedirPort here because RedirCPort is RedirPort + 1 and can't be 0 ;) */
      pNewChannel->localRtcpPort = pMediaInfo->lMediaRedirPort ? pMediaInfo->lMediaRedirCPort : pMediaInfo->lMediaCntrlPort;
      /* If user application has not specified a specific ip and is using
         multihomed mode, substitute appropriate ip.
      */
      if(!strcmp(pMediaInfo->lMediaIP, "0.0.0.0") || !strcmp(pMediaInfo->lMediaIP, "::"))
         strcpy(pNewChannel->localIP, call->localIP);
      else
         strcpy(pNewChannel->localIP, pMediaInfo->lMediaIP);

      OOTRACEDBGC5("Configured media info (%s, %s) %s:%d\n", call->callType, call->callToken, pNewChannel->localIP, pNewChannel->localRtcpPort);
   }
   else{
      OOTRACEDBGC3("Using default media info (%s, %s)\n", call->callType,
                   call->callToken);
      pNewChannel->localRtpPort = ooGetNextPort (OORTP);

      /* Ensures that RTP port is an even one */
      if((pNewChannel->localRtpPort & 1) == 1)
        pNewChannel->localRtpPort = ooGetNextPort (OORTP);

      pNewChannel->localRtcpPort = ooGetNextPort (OORTP);
      strcpy(pNewChannel->localIP, call->localIP);
   }

   /* Add new channel to the list */
   pNewChannel->next = NULL;
   if(!call->logicalChans) {
      call->logicalChans = pNewChannel;
   }
   else{
      pChannel = call->logicalChans;
      while(pChannel->next)  pChannel = pChannel->next;
      pChannel->next = pNewChannel;
   }

   /* increment logical channels */
   call->noOfLogicalChannels++;
   OOTRACEINFO3("Created new logical channel entry (%s, %s)\n", call->callType,
                call->callToken);
   return pNewChannel;
}

OOLogicalChannel* ooFindLogicalChannelByLogicalChannelNo(OOH323CallData *call,
                                                         int ChannelNo)
{
   OOLogicalChannel *pLogicalChannel=NULL;
   if(!call->logicalChans)
   {
      OOTRACEWARN3("ERROR: No Open LogicalChannels - Failed "
                  "FindLogicalChannelByChannelNo (%s, %s\n", call->callType,
                   call->callToken);
      return NULL;
   }
   pLogicalChannel = call->logicalChans;
   while(pLogicalChannel)
   {
      if(pLogicalChannel->channelNo == ChannelNo)
         break;
      else
         pLogicalChannel = pLogicalChannel->next;
   }

   return pLogicalChannel;
}

OOLogicalChannel * ooFindLogicalChannelByOLC(OOH323CallData *call,
                               H245OpenLogicalChannel *olc)
{
   H245DataType * psDataType=NULL;
   H245H2250LogicalChannelParameters * pslcp=NULL;
   OOTRACEDBGC4("ooFindLogicalChannel by olc %d (%s, %s)\n",
            olc->forwardLogicalChannelNumber, call->callType, call->callToken);
   if(olc->m.reverseLogicalChannelParametersPresent)
   {
      OOTRACEDBGC3("Finding receive channel (%s,%s)\n", call->callType,
                                                       call->callToken);
      psDataType = &olc->reverseLogicalChannelParameters.dataType;
      /* Only H2250LogicalChannelParameters are supported */
      if(olc->reverseLogicalChannelParameters.multiplexParameters.t !=
         T_H245OpenLogicalChannel_reverseLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters){
         OOTRACEERR4("Error:Invalid olc %d received (%s, %s)\n",
           olc->forwardLogicalChannelNumber, call->callType, call->callToken);
         return NULL;
      }
      pslcp = olc->reverseLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;

      return ooFindLogicalChannel(call, pslcp->sessionID, "receive", psDataType);
   }
   else{
      OOTRACEDBGC3("Finding transmit channel (%s, %s)\n", call->callType,
                                                           call->callToken);
      psDataType = &olc->forwardLogicalChannelParameters.dataType;
      /* Only H2250LogicalChannelParameters are supported */
      if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
         T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
      {
         OOTRACEERR4("Error:Invalid olc %d received (%s, %s)\n",
           olc->forwardLogicalChannelNumber, call->callType, call->callToken);
         return NULL;
      }
      pslcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
      return ooFindLogicalChannel(call, pslcp->sessionID, "transmit", psDataType);
   }
}

OOLogicalChannel * ooFindLogicalChannel(OOH323CallData *call, int sessionID,
                                        char *dir, H245DataType * dataType)
{
   OOLogicalChannel * pChannel = NULL;
   pChannel = call->logicalChans;
   while(pChannel)
   {
      OOTRACEDBGC3("ooFindLogicalChannel, checking channel: %d:%s\n",
                    pChannel->sessionID, pChannel->dir);
      if(pChannel->sessionID == sessionID || pChannel->sessionID == 0)
      {
         if(!strcmp(pChannel->dir, dir))
         {
            OOTRACEDBGC3("ooFindLogicalChannel, comparing channel: %d:%s\n",
                          pChannel->channelNo, pChannel->dir);
            if(!strcmp(dir, "receive"))
            {
               if(ooCapabilityCheckCompatibility(call, pChannel->chanCap,
                                                 dataType, OORX)) {
                  return pChannel;
               }
            }
            else if(!strcmp(dir, "transmit"))
            {
               if(ooCapabilityCheckCompatibility(call, pChannel->chanCap,
                                                 dataType, OOTX)) {
                  return pChannel;
               }
            }
         }
      }
      pChannel = pChannel->next;
   }
   return NULL;
}

/* This function is used to get a logical channel with a particular session ID */
OOLogicalChannel* ooGetLogicalChannel
   (OOH323CallData *call, int sessionID, char *dir)
{
   OOLogicalChannel * pChannel = NULL;
   pChannel = call->logicalChans;
   while(pChannel)
   {
      if(pChannel->sessionID == sessionID && !strcmp(pChannel->dir, dir))
         return pChannel;
      else
         pChannel = pChannel->next;
   }
   return NULL;
}

/* function is to get channel with particular direction */

OOLogicalChannel* ooGetTransmitLogicalChannel
   (OOH323CallData *call)
{
   OOLogicalChannel * pChannel = NULL;
   pChannel = call->logicalChans;
   while(pChannel)
   {
      OOTRACEINFO6("Listing logical channel %d cap %d state %d for (%s, %s)\n",
		pChannel->channelNo, pChannel->chanCap->cap, pChannel->state,
		call->callType, call->callToken);
      if(!strcmp(pChannel->dir, "transmit") && pChannel->state != OO_LOGICALCHAN_IDLE &&
					       pChannel->state != OO_LOGICALCHAN_PROPOSEDFS)
         return pChannel;
      else
         pChannel = pChannel->next;
   }
   return NULL;
}


OOLogicalChannel* ooGetReceiveLogicalChannel
   (OOH323CallData *call)
{
   OOLogicalChannel * pChannel = NULL;
   pChannel = call->logicalChans;
   while (pChannel) {
      OOTRACEINFO6("Listing logical channel %d cap %d state %d for (%s, %s)\n",
		pChannel->channelNo, pChannel->chanCap->cap, pChannel->state,
		call->callType, call->callToken);
      if (!strcmp(pChannel->dir, "receive") && pChannel->state != OO_LOGICALCHAN_IDLE &&
					       pChannel->state != OO_LOGICALCHAN_PROPOSEDFS) {
         return pChannel;
      } else {
         pChannel = pChannel->next;
      }
   }
   return NULL;
}

int ooClearAllLogicalChannels(OOH323CallData *call)
{
   OOLogicalChannel * temp = NULL, *prev = NULL;

   OOTRACEINFO3("Clearing all logical channels (%s, %s)\n", call->callType,
                 call->callToken);

   temp = call->logicalChans;
   while(temp)
   {
      prev = temp;
      temp = temp->next;
      ooClearLogicalChannel(call, prev->channelNo);/* TODO: efficiency - This causes re-search
                                                      of logical channel in the list. Can be
                                                      easily improved.*/
   }
   call->logicalChans = NULL;
   return OO_OK;
}

int ooClearLogicalChannel(OOH323CallData *call, int channelNo)
{

   OOLogicalChannel *pLogicalChannel = NULL;
   ooH323EpCapability *epCap=NULL;

   OOTRACEDBGC4("Clearing logical channel number %d. (%s, %s)\n", channelNo,
                 call->callType, call->callToken);

   pLogicalChannel = ooFindLogicalChannelByLogicalChannelNo(call,channelNo);
   do { if(!pLogicalChannel)
   {
      OOTRACEWARN4("Logical Channel %d doesn't exist, in clearLogicalChannel."
                   " (%s, %s)\n",
                  channelNo, call->callType, call->callToken);
      return OO_OK;
   }

   epCap = (ooH323EpCapability*) pLogicalChannel->chanCap;
   if(!strcmp(pLogicalChannel->dir, "receive"))
   {
      if(epCap->stopReceiveChannel)
      {
         epCap->stopReceiveChannel(call, pLogicalChannel);
         OOTRACEINFO4("Stopped Receive channel %d (%s, %s)\n",
                                 channelNo, call->callType, call->callToken);
      }
      else{
         OOTRACEERR4("ERROR:No callback registered for stopReceiveChannel %d "
                     "(%s, %s)\n", channelNo, call->callType, call->callToken);
      }
   }
   else
   {
      if(pLogicalChannel->state == OO_LOGICALCHAN_ESTABLISHED)
      {
         if(epCap->stopTransmitChannel)
         {
            epCap->stopTransmitChannel(call, pLogicalChannel);
            OOTRACEINFO4("Stopped Transmit channel %d (%s, %s)\n",
                          channelNo, call->callType, call->callToken);
         }
         else{
            OOTRACEERR4("ERROR:No callback registered for stopTransmitChannel"
                        " %d (%s, %s)\n", channelNo, call->callType,
                        call->callToken);
         }
      }
   }
   ooRemoveLogicalChannel(call, channelNo);/* TODO: efficiency - This causes re-search of
                                                    of logical channel in the list. Can be
                                                    easily improved.*/
   } while ((pLogicalChannel = ooFindLogicalChannelByLogicalChannelNo(call, channelNo)));
   return OO_OK;
}

int ooRemoveLogicalChannel(OOH323CallData *call, int ChannelNo)
{
   OOLogicalChannel * temp = NULL, *prev=NULL;
   if(!call->logicalChans)
   {
      OOTRACEERR4("ERROR:Remove Logical Channel - Channel %d not found "
                  "Empty channel List(%s, %s)\n", ChannelNo, call->callType,
                  call->callToken);
      return OO_FAILED;
   }

   temp = call->logicalChans;
   while(temp)
   {
      if(temp->channelNo == ChannelNo)
      {
         if(!prev)   call->logicalChans = temp->next;
         else   prev->next = temp->next;
         memFreePtr(call->pctxt, temp->chanCap);
         memFreePtr(call->pctxt, temp);
         OOTRACEDBGC4("Removed logical channel %d (%s, %s)\n", ChannelNo,
                       call->callType, call->callToken);
         call->noOfLogicalChannels--;
         return OO_OK;
      }
      prev = temp;
      temp = temp->next;
   }

   OOTRACEERR4("ERROR:Remove Logical Channel - Channel %d not found "
                  "(%s, %s)\n", ChannelNo, call->callType, call->callToken);
   return OO_FAILED;
}

/*
Change the state of the channel as established and close all other
channels with same session IDs. This is useful for handling fastStart,
as the endpoint can open multiple logical channels for same sessionID.
Once the remote endpoint confirms it's selection, all other channels for
the same sessionID must be closed.
*/
int ooOnLogicalChannelEstablished
   (OOH323CallData *call, OOLogicalChannel * pChannel)
{
   OOLogicalChannel * temp = NULL, *prev=NULL;
   OOTRACEDBGC3("In ooOnLogicalChannelEstablished (%s, %s)\n",
                call->callType, call->callToken);
   pChannel->state = OO_LOGICALCHAN_ESTABLISHED;
   temp = call->logicalChans;
   while(temp)
   {
      if(temp->channelNo != pChannel->channelNo &&
         temp->sessionID == pChannel->sessionID &&
         !strcmp(temp->dir, pChannel->dir)        )
      {
         prev = temp;
         temp = temp->next;
         ooClearLogicalChannel(call, prev->channelNo);
      }
      else
         temp = temp->next;
   }
   return OO_OK;
}
