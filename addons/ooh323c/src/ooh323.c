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
#include "asterisk/time.h"
#include <time.h>

#include "ootypes.h"
#include "ooq931.h"
#include "ootrace.h"
#include "oochannels.h"
#include "ooh245.h"
#include "ooCalls.h"
#include "printHandler.h"
#include "ooh323.h"
#include "ooh323ep.h"
#include "ooGkClient.h"
#include "ooTimer.h"

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

int ooHandleFastStart(OOH323CallData *call, H225Facility_UUIE *facility);
int ooOnReceivedReleaseComplete(OOH323CallData *call, Q931Message *q931Msg);
int ooOnReceivedCallProceeding(OOH323CallData *call, Q931Message *q931Msg);
int ooOnReceivedAlerting(OOH323CallData *call, Q931Message *q931Msg);
int ooOnReceivedProgress(OOH323CallData *call, Q931Message *q931Msg);
int ooHandleDisplayIE(OOH323CallData *call, Q931Message *q931Msg);

int ooHandleDisplayIE(OOH323CallData *call, Q931Message *q931Msg) {
   Q931InformationElement* pDisplayIE;

   /* check for display ie */
   pDisplayIE = ooQ931GetIE(q931Msg, Q931DisplayIE);
   if(pDisplayIE) {
      if (call->remoteDisplayName)
	memFreePtr(call->pctxt, call->remoteDisplayName);
      call->remoteDisplayName = (char *) memAllocZ(call->pctxt, 
                                 pDisplayIE->length*sizeof(ASN1OCTET)+1);
      strncpy(call->remoteDisplayName, (char *)pDisplayIE->data, pDisplayIE->length*sizeof(ASN1OCTET));
   }

   return OO_OK;
}

int ooHandleFastStart(OOH323CallData *call, H225Facility_UUIE *facility)
{
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   ooLogicalChannel * pChannel = NULL;
   H245H2250LogicalChannelParameters * h2250lcp = NULL;  
   int i=0, ret=0;

   /* Handle fast-start */
   if(OO_TESTFLAG (call->flags, OO_M_FASTSTART))
   {
      if(facility->m.fastStartPresent)
      {
         /* For printing the decoded message to log, initialize handler. */
         initializePrintHandler(&printHandler, "FastStart Elements");

         /* Set print handler */
         setEventHandler (call->pctxt, &printHandler);

         for(i=0; i<(int)facility->fastStart.n; i++)
         {
            olc = NULL;

            olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt, 
                                              sizeof(H245OpenLogicalChannel));
            if(!olc)
            {
               OOTRACEERR3("ERROR:Memory - ooHandleFastStart - olc"
                           "(%s, %s)\n", call->callType, call->callToken);
               /*Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            memset(olc, 0, sizeof(H245OpenLogicalChannel));
            memcpy(msgbuf, facility->fastStart.elem[i].data, 
                                    facility->fastStart.elem[i].numocts);
            setPERBuffer(call->pctxt, msgbuf, 
                         facility->fastStart.elem[i].numocts, 1);
            ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
            if(ret != ASN_OK)
            {
               OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                           "(%s, %s)\n", call->callType, call->callToken);
               /* Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }

            dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

            pChannel = ooFindLogicalChannelByOLC(call, olc);
            if(!pChannel)
            {
               OOTRACEERR4("ERROR: Logical Channel %d not found, fast start. "
                           "(%s, %s)\n",
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               return OO_FAILED;
            }
            if(pChannel->channelNo != olc->forwardLogicalChannelNumber)
            {
               OOTRACEINFO5("Remote endpoint changed forwardLogicalChannel"
                            "Number from %d to %d (%s, %s)\n", 
                            pChannel->channelNo, 
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               pChannel->channelNo = olc->forwardLogicalChannelNumber;
            }
            if(!strcmp(pChannel->dir, "transmit"))
            {

               if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
                  T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
               {
                  OOTRACEERR4("ERROR:Unknown multiplex parameter type for "
                              "channel %d (%s, %s)\n", 
                              olc->forwardLogicalChannelNumber, call->callType,
                              call->callToken);
                  continue;
               }
            
               /* Extract the remote media endpoint address */
               h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
               if(!h2250lcp)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "forward Logical Channel Parameters found. "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               if(!h2250lcp->m.mediaChannelPresent)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "reverse media channel information found."
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               ret = ooGetIpPortFromH245TransportAddress(call, 
                                   &h2250lcp->mediaChannel, pChannel->remoteIP,
                                   &pChannel->remoteMediaPort);
               
               if(ret != OO_OK)
               {
               	if(call->callState < OO_CALL_CLEAR)
               	{
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               	}
                  OOTRACEERR3("ERROR:Unsupported media channel address type "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
       
               if(!pChannel->chanCap->startTransmitChannel)
               {
                  OOTRACEERR3("ERROR:No callback registered to start transmit "
                              "channel (%s, %s)\n",call->callType, 
                              call->callToken);
                  return OO_FAILED;
               }
               pChannel->chanCap->startTransmitChannel(call, pChannel);
            }
            /* Mark the current channel as established and close all other 
               logical channels with same session id and in same direction.
            */
            ooOnLogicalChannelEstablished(call, pChannel);
         }
         finishPrint();
         removeEventHandler(call->pctxt);
         OO_SETFLAG(call->flags, OO_M_FASTSTARTANSWERED);
      }
      
   }

   if(facility->m.h245AddressPresent)
   {
      if (OO_TESTFLAG (call->flags, OO_M_TUNNELING))
      {
         OO_CLRFLAG (call->flags, OO_M_TUNNELING);
         OOTRACEINFO3("Tunneling is disabled for call as H245 address is "
                      "provided in facility message (%s, %s)\n", 
                      call->callType, call->callToken);
      }
      ret = ooH323GetIpPortFromH225TransportAddress(call, 
                                  &facility->h245Address, call->remoteIP,
                                  &call->remoteH245Port);
      if(ret != OO_OK)
      {
         OOTRACEERR3("Error: Unknown H245 address type in received "
                     "CallProceeding message (%s, %s)", call->callType, 
                     call->callToken);
         /* Mark call for clearing */
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_INVALIDMESSAGE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      if(call->remoteH245Port != 0 && !call->pH245Channel) {
      /* Create an H.245 connection. 
      */
       if(ooCreateH245Connection(call)== OO_FAILED)
       {
         OOTRACEERR3("Error: H.245 channel creation failed (%s, %s)\n", 
                     call->callType, call->callToken);

         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
       }
      }
   } else if (OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
	ret =ooSendTCSandMSD(call);
	if (ret != OO_OK)
		return ret;
   }
   return OO_OK;
}

int ooOnReceivedReleaseComplete(OOH323CallData *call, Q931Message *q931Msg)
{
   int ret = OO_OK;
   H225ReleaseComplete_UUIE * releaseComplete = NULL;
   ASN1UINT i;
   DListNode *pNode = NULL;
   OOTimer *pTimer = NULL;
   unsigned reasonCode=T_H225ReleaseCompleteReason_undefinedReason;
   enum Q931CauseValues cause= Q931ErrorInCauseIE;

   if(q931Msg->causeIE)
   {
      cause = q931Msg->causeIE->data[1];
      /* Get rid of the extension bit.For more info, check ooQ931SetCauseIE */
      cause = cause & 0x7f; 
      OOTRACEDBGA4("Cause of Release Complete is %x. (%s, %s)\n", cause, 
                                             call->callType, call->callToken);
   }

   /* Remove session timer, if active*/
   for(i = 0; i<call->timerList.count; i++)
   {
      pNode = dListFindByIndex(&call->timerList, i);
      pTimer = (OOTimer*)pNode->data;
      if(((ooTimerCallback*)pTimer->cbData)->timerType & 
                                                   OO_SESSION_TIMER)
      {
         memFreePtr(call->pctxt, pTimer->cbData);
         ooTimerDelete(call->pctxt, &call->timerList, pTimer);
         OOTRACEDBGC3("Deleted Session Timer. (%s, %s)\n", 
                       call->callType, call->callToken);
         break;
      }
   }

 
   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:No User-User IE in received ReleaseComplete message "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   releaseComplete = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.releaseComplete;
   if(!releaseComplete)
   {
      OOTRACEWARN3("WARN: ReleaseComplete UUIE not found in received "
                  "ReleaseComplete message - %s "
                  "%s\n", call->callType, call->callToken);
   }
   else{

      if(releaseComplete->m.reasonPresent)
      {
         OOTRACEINFO4("Release complete reason code %d. (%s, %s)\n", 
                   releaseComplete->reason.t, call->callType, call->callToken);
         reasonCode = releaseComplete->reason.t;
      }
   }

   if(call->callEndReason == OO_REASON_UNKNOWN)
      call->callEndReason = ooGetCallClearReasonFromCauseAndReasonCode(cause, 
                                                                   reasonCode);
   call->q931cause = cause;
#if 0
   if (q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
       q931Msg->userInfo->h323_uu_pdu.h245Tunneling          &&
       OO_TESTFLAG (call->flags, OO_M_TUNNELING) )
   {
      OOTRACEDBGB3("Handling tunneled messages in ReleaseComplete. (%s, %s)\n",
                   call->callType, call->callToken);
      ret = ooHandleTunneledH245Messages
                    (call, &q931Msg->userInfo->h323_uu_pdu);
      OOTRACEDBGB3("Finished handling tunneled messages in ReleaseComplete."
                   " (%s, %s)\n", call->callType, call->callToken);
   }
#endif
   if(call->h245SessionState != OO_H245SESSION_IDLE && 
      call->h245SessionState != OO_H245SESSION_CLOSED)
   {
      ooCloseH245Connection(call);
   }

   if(call->callState != OO_CALL_CLEAR_RELEASESENT)
   {
      if(gH323ep.gkClient && !OO_TESTFLAG(call->flags, OO_M_DISABLEGK))
      {
         if(gH323ep.gkClient->state == GkClientRegistered){
            OOTRACEDBGA3("Sending DRQ after received ReleaseComplete."
                         "(%s, %s)\n", call->callType, call->callToken);
            ooGkClientSendDisengageRequest(gH323ep.gkClient, call);
         }
      }
   }
   call->callState = OO_CALL_CLEARED;

   return ret;
}

int ooOnReceivedSetup(OOH323CallData *call, Q931Message *q931Msg)
{
   H225Setup_UUIE *setup=NULL;
   int i=0, ret=0;
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   H225TransportAddress_ipAddress_ip *ip = NULL;
   Q931InformationElement* pDisplayIE=NULL;
   OOAliases *pAlias=NULL;

   call->callReference = q931Msg->callReference;
 
   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:No User-User IE in received SETUP message (%s, %s)\n",
                  call->callType, call->callToken);
      return OO_FAILED;
   }
   setup = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.setup;
   if(!setup)
   {
      OOTRACEERR3("Error: Setup UUIE not found in received setup message - %s "
                  "%s\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   memcpy(call->callIdentifier.guid.data, setup->callIdentifier.guid.data, 
          setup->callIdentifier.guid.numocts);
   call->callIdentifier.guid.numocts = setup->callIdentifier.guid.numocts;
   
   memcpy(call->confIdentifier.data, setup->conferenceID.data,
          setup->conferenceID.numocts);
   call->confIdentifier.numocts = setup->conferenceID.numocts;

   /* check for display ie */
   pDisplayIE = ooQ931GetIE(q931Msg, Q931DisplayIE);
   if(pDisplayIE)
   {
      call->remoteDisplayName = (char *) memAlloc(call->pctxt, 
                                 pDisplayIE->length*sizeof(ASN1OCTET)+1);
      strcpy(call->remoteDisplayName, (char *)pDisplayIE->data);
   }
   /*Extract Remote Aliases, if present*/
   if(setup->m.sourceAddressPresent)
   {
      if(setup->sourceAddress.count>0)
      {
         ooH323RetrieveAliases(call, &setup->sourceAddress, 
                                                       &call->remoteAliases);
         pAlias = call->remoteAliases;
         while(pAlias)
         {
            if(pAlias->type ==  T_H225AliasAddress_dialedDigits)
            {
              if(!call->callingPartyNumber)
              {
                 call->callingPartyNumber = (char*)memAlloc(call->pctxt,
                                                    strlen(pAlias->value)*+1);
                 if(call->callingPartyNumber)
                 {
                     strcpy(call->callingPartyNumber, pAlias->value);
                 }
              }
              break;
           }
           pAlias = pAlias->next;
         }
      }
   }
   /* Extract, aliases used for us, if present. Also,
      Populate calledPartyNumber from dialedDigits, if not yet populated using
      calledPartyNumber Q931 IE. 
   */      
   if(setup->m.destinationAddressPresent)
   {
      if(setup->destinationAddress.count>0)
      {
         ooH323RetrieveAliases(call, &setup->destinationAddress, 
                                                       &call->ourAliases);
         pAlias = call->ourAliases;
         while(pAlias)
         {
            if(pAlias->type == T_H225AliasAddress_dialedDigits)
            {
              if(!call->calledPartyNumber)
              {
                 call->calledPartyNumber = (char*)memAlloc(call->pctxt,
                                                    strlen(pAlias->value)*+1);
                 if(call->calledPartyNumber)
                 {
                    strcpy(call->calledPartyNumber, pAlias->value);
                 }
              }
              break;
            }
            pAlias = pAlias->next; 
         }
      }
   }

   /* Check for tunneling */
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent)
   {
      /* Tunneling enabled only when tunneling is set to true and h245
         address is absent. In the presence of H.245 address in received
         SETUP message, tunneling is disabled, irrespective of tunneling
         flag in the setup message*/
      if(q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
         !setup->m.h245AddressPresent)
      {
         if(OO_TESTFLAG(gH323ep.flags, OO_M_TUNNELING))
         {
            OO_SETFLAG (call->flags, OO_M_TUNNELING);
            OOTRACEINFO3("Call has tunneling active (%s,%s)\n", call->callType,
                          call->callToken);
         }
         else
            OOTRACEINFO3("ERROR:Remote endpoint wants to use h245Tunneling, "
                        "local endpoint has it disabled (%s,%s)\n",
                        call->callType, call->callToken);
      }
      else {
         if(OO_TESTFLAG(gH323ep.flags, OO_M_TUNNELING))
         {
            OOTRACEINFO3("Tunneling disabled by remote endpoint. (%s, %s)\n",
                         call->callType, call->callToken);
         }
         OO_CLRFLAG (call->flags, OO_M_TUNNELING);
      }
   }
   else {
      if(OO_TESTFLAG(gH323ep.flags, OO_M_TUNNELING))
      {
         OOTRACEINFO3("Tunneling disabled by remote endpoint. (%s, %s)\n",
                       call->callType, call->callToken);
      }
      OO_CLRFLAG (call->flags, OO_M_TUNNELING);
   }
   
   /* Extract Remote IP address */
   if(!setup->m.sourceCallSignalAddressPresent)
   {
      OOTRACEWARN3("WARNING:Missing source call signal address in received "
                   "setup (%s, %s)\n", call->callType, call->callToken);
   }
   else{

      if(setup->sourceCallSignalAddress.t != T_H225TransportAddress_ipAddress)
      {
         OOTRACEERR3("ERROR: Source call signalling address type not ip "
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }

      ip = &setup->sourceCallSignalAddress.u.ipAddress->ip;
      sprintf(call->remoteIP, "%d.%d.%d.%d", ip->data[0], ip->data[1], 
                                             ip->data[2], ip->data[3]);
      call->remotePort =  setup->sourceCallSignalAddress.u.ipAddress->port;
   }
   
   /* check for fast start */
   
   if(setup->m.fastStartPresent)
   {
      if(!OO_TESTFLAG(gH323ep.flags, OO_M_FASTSTART))
      {
         OOTRACEINFO3("Local endpoint does not support fastStart. Ignoring "
                     "fastStart. (%s, %s)\n", call->callType, call->callToken);
         OO_CLRFLAG (call->flags, OO_M_FASTSTART);
      }
      else if(setup->fastStart.n == 0)
      {
         OOTRACEINFO3("Empty faststart element received. Ignoring fast start. "
                      "(%s, %s)\n", call->callType, call->callToken);
         OO_CLRFLAG (call->flags, OO_M_FASTSTART);
      }
      else{
         OO_SETFLAG (call->flags, OO_M_FASTSTART);
         OOTRACEINFO3("FastStart enabled for call(%s, %s)\n", call->callType,
                       call->callToken);
      }
   }

   if (OO_TESTFLAG (call->flags, OO_M_FASTSTART))
   {
      /* For printing the decoded message to log, initialize handler. */
      initializePrintHandler(&printHandler, "FastStart Elements");

      /* Set print handler */
      setEventHandler (call->pctxt, &printHandler);

      for(i=0; i<(int)setup->fastStart.n; i++)
      {
         olc = NULL;
         /*         memset(msgbuf, 0, sizeof(msgbuf));*/
         olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt, 
                                              sizeof(H245OpenLogicalChannel));
         if(!olc)
         {
            OOTRACEERR3("ERROR:Memory - ooOnReceivedSetup - olc (%s, %s)\n", 
                        call->callType, call->callToken);
            /*Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_LOCAL_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         memset(olc, 0, sizeof(H245OpenLogicalChannel));
         memcpy(msgbuf, setup->fastStart.elem[i].data, 
                setup->fastStart.elem[i].numocts);

         setPERBuffer(call->pctxt, msgbuf, 
                      setup->fastStart.elem[i].numocts, 1);
         ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
         if(ret != ASN_OK)
         {
            OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                        "(%s, %s)\n", call->callType, call->callToken);
            /* Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_INVALIDMESSAGE;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         /* For now, just add decoded fast start elemts to list. This list
            will be processed at the time of sending CONNECT message. */
         dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);
      }
      finishPrint();
      removeEventHandler(call->pctxt);
   }

   return OO_OK;
}



int ooOnReceivedCallProceeding(OOH323CallData *call, Q931Message *q931Msg)
{
   H225CallProceeding_UUIE *callProceeding=NULL;
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   ooLogicalChannel * pChannel = NULL;
   H245H2250LogicalChannelParameters * h2250lcp = NULL;  
   int i=0, ret=0;

   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:No User-User IE in received CallProceeding message."
                  " (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   callProceeding = 
             q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.callProceeding;
   if(callProceeding == NULL)
   {
      OOTRACEERR3("Error: Received CallProceeding message does not have "
                  "CallProceeding UUIE (%s, %s)\n", call->callType, 
                  call->callToken);
      /* Mark call for clearing */
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   /* Handle fast-start */
   if(OO_TESTFLAG (call->flags, OO_M_FASTSTART))
   {
      if(callProceeding->m.fastStartPresent)
      {
         /* For printing the decoded message to log, initialize handler. */
         initializePrintHandler(&printHandler, "FastStart Elements");

         /* Set print handler */
         setEventHandler (call->pctxt, &printHandler);

         for(i=0; i<(int)callProceeding->fastStart.n; i++)
         {
            olc = NULL;

            olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt, 
                                              sizeof(H245OpenLogicalChannel));
            if(!olc)
            {
               OOTRACEERR3("ERROR:Memory - ooOnReceivedCallProceeding - olc"
                           "(%s, %s)\n", call->callType, call->callToken);
               /*Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            memset(olc, 0, sizeof(H245OpenLogicalChannel));
            memcpy(msgbuf, callProceeding->fastStart.elem[i].data, 
                                    callProceeding->fastStart.elem[i].numocts);
            setPERBuffer(call->pctxt, msgbuf, 
                         callProceeding->fastStart.elem[i].numocts, 1);
            ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
            if(ret != ASN_OK)
            {
               OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                           "(%s, %s)\n", call->callType, call->callToken);
               /* Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }

            dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

            pChannel = ooFindLogicalChannelByOLC(call, olc);
            if(!pChannel)
            {
               OOTRACEERR4("ERROR: Logical Channel %d not found, fast start. "
                           "(%s, %s)\n",
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               return OO_FAILED;
            }
            if(pChannel->channelNo != olc->forwardLogicalChannelNumber)
            {
               OOTRACEINFO5("Remote endpoint changed forwardLogicalChannel"
                            "Number from %d to %d (%s, %s)\n", 
                            pChannel->channelNo, 
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               pChannel->channelNo = olc->forwardLogicalChannelNumber;
            }
            if(!strcmp(pChannel->dir, "transmit"))
            {
               if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
                  T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
               {
                  OOTRACEERR4("ERROR:Unknown multiplex parameter type for "
                              "channel %d (%s, %s)\n", 
                              olc->forwardLogicalChannelNumber, call->callType,
                              call->callToken);
                  continue;
               }
            
               /* Extract the remote media endpoint address */
               h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
               if(!h2250lcp)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "forward Logical Channel Parameters found. "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               if(!h2250lcp->m.mediaChannelPresent)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "reverse media channel information found."
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               ret = ooGetIpPortFromH245TransportAddress(call, 
                                   &h2250lcp->mediaChannel, pChannel->remoteIP,
                                   &pChannel->remoteMediaPort);
               
               if(ret != OO_OK)
               {
               	if(call->callState < OO_CALL_CLEAR)
               	{
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               	}
                  OOTRACEERR3("ERROR:Unsupported media channel address type "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
       
               if(!pChannel->chanCap->startTransmitChannel)
               {
                  OOTRACEERR3("ERROR:No callback registered to start transmit "
                              "channel (%s, %s)\n",call->callType, 
                              call->callToken);
                  return OO_FAILED;
               }
               pChannel->chanCap->startTransmitChannel(call, pChannel);
            }
            /* Mark the current channel as established and close all other 
               logical channels with same session id and in same direction.
            */
            ooOnLogicalChannelEstablished(call, pChannel);
         }
         finishPrint();
         removeEventHandler(call->pctxt);
         OO_SETFLAG(call->flags, OO_M_FASTSTARTANSWERED);
      }
      
   }

   /* Retrieve tunneling info/H.245 control channel address from the connect msg */
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent && 
      !q931Msg->userInfo->h323_uu_pdu.h245Tunneling) {
	if (OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
		OO_CLRFLAG (call->flags, OO_M_TUNNELING);
		OOTRACEINFO3("Tunneling is disabled for call due to remote reject tunneling"
			      " (%s, %s)\n", call->callType, call->callToken);
	}
   }
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
      q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
      OO_TESTFLAG (call->flags, OO_M_TUNNELING) &&
      callProceeding->m.h245AddressPresent) {
      OOTRACEINFO3("Tunneling and h245address provided."
                   "Using Tunneling for H.245 messages (%s, %s)\n", 
                   call->callType, call->callToken);
   }
   else if(callProceeding->m.h245AddressPresent)
   {
      if (OO_TESTFLAG (call->flags, OO_M_TUNNELING))
      {
         OO_CLRFLAG (call->flags, OO_M_TUNNELING);
         OOTRACEINFO3("Tunneling is disabled for call as H245 address is "
                      "provided in callProceeding message (%s, %s)\n", 
                      call->callType, call->callToken);
      }
      ret = ooH323GetIpPortFromH225TransportAddress(call, 
                                  &callProceeding->h245Address, call->remoteIP,
                                  &call->remoteH245Port);
      if(ret != OO_OK)
      {
         OOTRACEERR3("Error: Unknown H245 address type in received "
                     "CallProceeding message (%s, %s)", call->callType, 
                     call->callToken);
         /* Mark call for clearing */
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_INVALIDMESSAGE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      if(call->remoteH245Port != 0 && !call->pH245Channel) {
      /* Create an H.245 connection. 
      */
       if(ooCreateH245Connection(call)== OO_FAILED)
       {
         OOTRACEERR3("Error: H.245 channel creation failed (%s, %s)\n", 
                     call->callType, call->callToken);

         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
       }
      }
   }

   return OO_OK;
}


int ooOnReceivedAlerting(OOH323CallData *call, Q931Message *q931Msg)
{
   H225Alerting_UUIE *alerting=NULL;
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   ooLogicalChannel * pChannel = NULL;
   H245H2250LogicalChannelParameters * h2250lcp = NULL;  
   int i=0, ret=0;

   ooHandleDisplayIE(call, q931Msg);

   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:No User-User IE in received Alerting message."
                  " (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   alerting = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.alerting;
   if(alerting == NULL)
   {
      OOTRACEERR3("Error: Received Alerting message does not have "
                  "alerting UUIE (%s, %s)\n", call->callType, 
                  call->callToken);
      /* Mark call for clearing */
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }
   /*Handle fast-start */
   if(OO_TESTFLAG (call->flags, OO_M_FASTSTART) &&
      !OO_TESTFLAG(call->flags, OO_M_FASTSTARTANSWERED))
   {
      if(alerting->m.fastStartPresent)
      {
         /* For printing the decoded message to log, initialize handler. */
         initializePrintHandler(&printHandler, "FastStart Elements");

         /* Set print handler */
         setEventHandler (call->pctxt, &printHandler);

         for(i=0; i<(int)alerting->fastStart.n; i++)
         {
            olc = NULL;

            olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt, 
                                              sizeof(H245OpenLogicalChannel));
            if(!olc)
            {
               OOTRACEERR3("ERROR:Memory - ooOnReceivedAlerting - olc"
                           "(%s, %s)\n", call->callType, call->callToken);
               /*Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            memset(olc, 0, sizeof(H245OpenLogicalChannel));
            memcpy(msgbuf, alerting->fastStart.elem[i].data, 
                                    alerting->fastStart.elem[i].numocts);
            setPERBuffer(call->pctxt, msgbuf, 
                         alerting->fastStart.elem[i].numocts, 1);
            ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
            if(ret != ASN_OK)
            {
               OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                           "(%s, %s)\n", call->callType, call->callToken);
               /* Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }

            dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

            pChannel = ooFindLogicalChannelByOLC(call, olc);
            if(!pChannel)
            {
               OOTRACEERR4("ERROR: Logical Channel %d not found, fast start. "
                           "(%s, %s)\n",
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               return OO_FAILED;
            }
            if(pChannel->channelNo != olc->forwardLogicalChannelNumber)
            {
               OOTRACEINFO5("Remote endpoint changed forwardLogicalChannel"
                            "Number from %d to %d (%s, %s)\n", 
                            pChannel->channelNo, 
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               pChannel->channelNo = olc->forwardLogicalChannelNumber;
            }
            if(!strcmp(pChannel->dir, "transmit"))
            {
               if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
                  T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
               {
                  OOTRACEERR4("ERROR:Unknown multiplex parameter type for "
                              "channel %d (%s, %s)\n", 
                              olc->forwardLogicalChannelNumber, call->callType,
                              call->callToken);
                  continue;
               }
            
               /* Extract the remote media endpoint address */
               h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
               if(!h2250lcp)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "forward Logical Channel Parameters found. "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               if(!h2250lcp->m.mediaChannelPresent)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "reverse media channel information found."
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               ret = ooGetIpPortFromH245TransportAddress(call, 
                                   &h2250lcp->mediaChannel, pChannel->remoteIP,
                                   &pChannel->remoteMediaPort);
               
               if(ret != OO_OK)
               {
               	if(call->callState < OO_CALL_CLEAR)
               	{
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               	}
                  OOTRACEERR3("ERROR:Unsupported media channel address type "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
       
               if(!pChannel->chanCap->startTransmitChannel)
               {
                  OOTRACEERR3("ERROR:No callback registered to start transmit "
                              "channel (%s, %s)\n",call->callType, 
                              call->callToken);
                  return OO_FAILED;
               }
               pChannel->chanCap->startTransmitChannel(call, pChannel);
               /* Mark the current channel as established and close all other 
                  logical channels with same session id and in same direction.
               */
               ooOnLogicalChannelEstablished(call, pChannel);
	    }
         }
         finishPrint();
         removeEventHandler(call->pctxt);
         OO_SETFLAG(call->flags, OO_M_FASTSTARTANSWERED);
      } 

   }

   /* Retrieve tunneling info/H.245 control channel address from the connect msg */
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent && 
      !q931Msg->userInfo->h323_uu_pdu.h245Tunneling) {
	if (OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
		OO_CLRFLAG (call->flags, OO_M_TUNNELING);
		OOTRACEINFO3("Tunneling is disabled for call due to remote reject tunneling"
			      " (%s, %s)\n", call->callType, call->callToken);
	}
   }
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
      q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
	 OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
      if (alerting->m.h245AddressPresent) 
      	OOTRACEINFO3("Tunneling and h245address provided."
                     "Giving preference to Tunneling (%s, %s)\n", 
                   	call->callType, call->callToken);
	ret =ooSendTCSandMSD(call);
	if (ret != OO_OK)
		return ret;

   } else if(alerting->m.h245AddressPresent) {
      if (OO_TESTFLAG (call->flags, OO_M_TUNNELING))
      {
         OO_CLRFLAG (call->flags, OO_M_TUNNELING);
         OOTRACEINFO3("Tunneling is disabled for call as H245 address is "
                      "provided in Alerting message (%s, %s)\n", 
                      call->callType, call->callToken);
      }
      ret = ooH323GetIpPortFromH225TransportAddress(call, 
                                  &alerting->h245Address, call->remoteIP,
                                  &call->remoteH245Port);
      if(ret != OO_OK)
      {
         OOTRACEERR3("Error: Unknown H245 address type in received "
                     "Alerting message (%s, %s)", call->callType, 
                     call->callToken);
         /* Mark call for clearing */
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_INVALIDMESSAGE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      if(call->remoteH245Port != 0 && !call->pH245Channel) {
      /* Create an H.245 connection. 
      */
       if(ooCreateH245Connection(call)== OO_FAILED)
       {
         OOTRACEERR3("Error: H.245 channel creation failed (%s, %s)\n", 
                     call->callType, call->callToken);

         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
       }
      }
   }

   return OO_OK;
}

int ooOnReceivedProgress(OOH323CallData *call, Q931Message *q931Msg)
{
   H225Progress_UUIE *progress=NULL;
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   ooLogicalChannel * pChannel = NULL;
   H245H2250LogicalChannelParameters * h2250lcp = NULL;  
   int i=0, ret=0;

   ooHandleDisplayIE(call, q931Msg);

   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:No User-User IE in received Progress message."
                  " (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   progress = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.progress;
   if(progress == NULL)
   {
      OOTRACEERR3("Error: Received Progress message does not have "
                  "progress UUIE (%s, %s)\n", call->callType, 
                  call->callToken);
      /* Mark call for clearing */
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }
   /*Handle fast-start */
   if(OO_TESTFLAG (call->flags, OO_M_FASTSTART) &&
      !OO_TESTFLAG(call->flags, OO_M_FASTSTARTANSWERED))
   {
      if(progress->m.fastStartPresent)
      {
         /* For printing the decoded message to log, initialize handler. */
         initializePrintHandler(&printHandler, "FastStart Elements");

         /* Set print handler */
         setEventHandler (call->pctxt, &printHandler);

         for(i=0; i<(int)progress->fastStart.n; i++)
         {
            olc = NULL;

            olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt, 
                                              sizeof(H245OpenLogicalChannel));
            if(!olc)
            {
               OOTRACEERR3("ERROR:Memory - ooOnReceivedProgress - olc"
                           "(%s, %s)\n", call->callType, call->callToken);
               /*Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            memset(olc, 0, sizeof(H245OpenLogicalChannel));
            memcpy(msgbuf, progress->fastStart.elem[i].data, 
                                    progress->fastStart.elem[i].numocts);
            setPERBuffer(call->pctxt, msgbuf, 
                         progress->fastStart.elem[i].numocts, 1);
            ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
            if(ret != ASN_OK)
            {
               OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                           "(%s, %s)\n", call->callType, call->callToken);
               /* Mark call for clearing */
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }

            dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

            pChannel = ooFindLogicalChannelByOLC(call, olc);
            if(!pChannel)
            {
               OOTRACEERR4("ERROR: Logical Channel %d not found, fast start. "
                           "(%s, %s)\n",
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               return OO_FAILED;
            }
            if(pChannel->channelNo != olc->forwardLogicalChannelNumber)
            {
               OOTRACEINFO5("Remote endpoint changed forwardLogicalChannel"
                            "Number from %d to %d (%s, %s)\n", 
                            pChannel->channelNo, 
                            olc->forwardLogicalChannelNumber, call->callType, 
                            call->callToken);
               pChannel->channelNo = olc->forwardLogicalChannelNumber;
            }
            if(!strcmp(pChannel->dir, "transmit"))
            {
               if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
                  T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
               {
                  OOTRACEERR4("ERROR:Unknown multiplex parameter type for "
                              "channel %d (%s, %s)\n", 
                              olc->forwardLogicalChannelNumber, call->callType,
                              call->callToken);
                  continue;
               }
            
               /* Extract the remote media endpoint address */
               h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
               if(!h2250lcp)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "forward Logical Channel Parameters found. "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               if(!h2250lcp->m.mediaChannelPresent)
               {
                  OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                              "reverse media channel information found."
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               ret = ooGetIpPortFromH245TransportAddress(call, 
                                   &h2250lcp->mediaChannel, pChannel->remoteIP,
                                   &pChannel->remoteMediaPort);
               
               if(ret != OO_OK)
               {
               	if(call->callState < OO_CALL_CLEAR)
               	{
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               	}
                  OOTRACEERR3("ERROR:Unsupported media channel address type "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
       
               if(!pChannel->chanCap->startTransmitChannel)
               {
                  OOTRACEERR3("ERROR:No callback registered to start transmit "
                              "channel (%s, %s)\n",call->callType, 
                              call->callToken);
                  return OO_FAILED;
               }
               pChannel->chanCap->startTransmitChannel(call, pChannel);
            }
            /* Mark the current channel as established and close all other 
               logical channels with same session id and in same direction.
            */
            ooOnLogicalChannelEstablished(call, pChannel);
         }
         finishPrint();
         removeEventHandler(call->pctxt);
         OO_SETFLAG(call->flags, OO_M_FASTSTARTANSWERED);
      }
      
   }

   /* Retrieve the H.245 control channel address from the connect msg */
   /* Retrieve tunneling info/H.245 control channel address from the connect msg */
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent && 
      !q931Msg->userInfo->h323_uu_pdu.h245Tunneling) {
	if (OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
		OO_CLRFLAG (call->flags, OO_M_TUNNELING);
		OOTRACEINFO3("Tunneling is disabled for call due to remote reject tunneling"
			      " (%s, %s)\n", call->callType, call->callToken);
	}
   }
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
      q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
      OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
      if (progress->m.h245AddressPresent) 
      	OOTRACEINFO3("Tunneling and h245address provided."
                     "Giving preference to Tunneling (%s, %s)\n", 
                     call->callType, call->callToken);
	ret =ooSendTCSandMSD(call);
	if (ret != OO_OK)
		return ret;
   } else if(progress->m.h245AddressPresent) {
      if (OO_TESTFLAG (call->flags, OO_M_TUNNELING))
      {
         OO_CLRFLAG (call->flags, OO_M_TUNNELING);
         OOTRACEINFO3("Tunneling is disabled for call as H245 address is "
                      "provided in Progress message (%s, %s)\n", 
                      call->callType, call->callToken);
      }
      ret = ooH323GetIpPortFromH225TransportAddress(call, 
                                  &progress->h245Address, call->remoteIP,
                                  &call->remoteH245Port);
      if(ret != OO_OK)
      {
         OOTRACEERR3("Error: Unknown H245 address type in received "
                     "Progress message (%s, %s)", call->callType, 
                     call->callToken);
         /* Mark call for clearing */
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_INVALIDMESSAGE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      if(call->remoteH245Port != 0 && !call->pH245Channel) {
       /* Create an H.245 connection. 
      */
       if(ooCreateH245Connection(call)== OO_FAILED)
       {
         OOTRACEERR3("Error: H.245 channel creation failed (%s, %s)\n", 
                     call->callType, call->callToken);

         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
       }
      }
   }

   return OO_OK;
}
   

int ooOnReceivedSignalConnect(OOH323CallData* call, Q931Message *q931Msg)
{
   int ret, i;
   H225Connect_UUIE *connect;
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   ooLogicalChannel * pChannel = NULL;
   H245H2250LogicalChannelParameters * h2250lcp = NULL;  

   ooHandleDisplayIE(call, q931Msg);

   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("Error: UUIE not found in received H.225 Connect message"
                  " (%s, %s)\n", call->callType, call->callToken);
      /* Mark call for clearing */
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }
   /* Retrieve the connect message from the user-user IE & Q.931 header */
   connect = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.connect;
   if(connect == NULL)
   {
      OOTRACEERR3("Error: Received Connect message does not have Connect UUIE"
                  " (%s, %s)\n", call->callType, call->callToken);
      /* Mark call for clearing */
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   /*Handle fast-start */
   if(OO_TESTFLAG (call->flags, OO_M_FASTSTART) && 
      !OO_TESTFLAG (call->flags, OO_M_FASTSTARTANSWERED))
   {
      if(!connect->m.fastStartPresent)
      {
         OOTRACEINFO3("Remote endpoint has rejected fastStart. (%s, %s)\n",
                      call->callType, call->callToken);
         /* Clear all channels we might have created */
         ooClearAllLogicalChannels(call);
         OO_CLRFLAG (call->flags, OO_M_FASTSTART);
      }
   }

   if (connect->m.fastStartPresent && 
       !OO_TESTFLAG(call->flags, OO_M_FASTSTARTANSWERED))
   {
      /* For printing the decoded message to log, initialize handler. */
      initializePrintHandler(&printHandler, "FastStart Elements");

      /* Set print handler */
      setEventHandler (call->pctxt, &printHandler);

      for(i=0; i<(int)connect->fastStart.n; i++)
      {
         olc = NULL;
         /* memset(msgbuf, 0, sizeof(msgbuf));*/
         olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt, 
                                              sizeof(H245OpenLogicalChannel));
         if(!olc)
         {
            OOTRACEERR3("ERROR:Memory - ooOnReceivedSignalConnect - olc"
                        "(%s, %s)\n", call->callType, call->callToken);
            /*Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_LOCAL_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            finishPrint();
            removeEventHandler(call->pctxt);
            return OO_FAILED;
         }
         memset(olc, 0, sizeof(H245OpenLogicalChannel));
         memcpy(msgbuf, connect->fastStart.elem[i].data, 
                                           connect->fastStart.elem[i].numocts);
         setPERBuffer(call->pctxt, msgbuf, 
                      connect->fastStart.elem[i].numocts, 1);
         ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
         if(ret != ASN_OK)
         {
            OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                        "(%s, %s)\n", call->callType, call->callToken);
            /* Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_INVALIDMESSAGE;
               call->callState = OO_CALL_CLEAR;
            }
            finishPrint();
            removeEventHandler(call->pctxt);
            return OO_FAILED;
         }

         dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

         pChannel = ooFindLogicalChannelByOLC(call, olc);
         if(!pChannel)
         {
            OOTRACEERR4("ERROR: Logical Channel %d not found, fasts start "
                        "answered. (%s, %s)\n",
                         olc->forwardLogicalChannelNumber, call->callType, 
                         call->callToken);
            finishPrint();
            removeEventHandler(call->pctxt);
            return OO_FAILED;
         }
         if(pChannel->channelNo != olc->forwardLogicalChannelNumber)
         {
            OOTRACEINFO5("Remote endpoint changed forwardLogicalChannelNumber"
                         "from %d to %d (%s, %s)\n", pChannel->channelNo,
                          olc->forwardLogicalChannelNumber, call->callType, 
                          call->callToken);
            pChannel->channelNo = olc->forwardLogicalChannelNumber;
         }
         if(!strcmp(pChannel->dir, "transmit"))
         {
            if(olc->forwardLogicalChannelParameters.multiplexParameters.t != 
               T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
            {
               OOTRACEERR4("ERROR:Unknown multiplex parameter type for channel"
                           " %d (%s, %s)\n", olc->forwardLogicalChannelNumber, 
                           call->callType, call->callToken);
               continue;
            }
            
            /* Extract the remote media endpoint address */
            h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
            if(!h2250lcp)
            {
               OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                           "forward Logical Channel Parameters found. (%s, %s)"
                           "\n", call->callType, call->callToken);
               finishPrint();
               removeEventHandler(call->pctxt);
               return OO_FAILED;
            }
            if(!h2250lcp->m.mediaChannelPresent)
            {
               OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                           "reverse media channel information found. (%s, %s)"
                           "\n", call->callType, call->callToken);
               finishPrint();
               removeEventHandler(call->pctxt);
               return OO_FAILED;
            }

            ret = ooGetIpPortFromH245TransportAddress(call, 
                                   &h2250lcp->mediaChannel, pChannel->remoteIP,
                                   &pChannel->remoteMediaPort);
            if(ret != OO_OK)
            {
               	if(call->callState < OO_CALL_CLEAR)
               	{
                  call->callEndReason = OO_REASON_INVALIDMESSAGE;
                  call->callState = OO_CALL_CLEAR;
               	}
               OOTRACEERR3("ERROR:Unsupported media channel address type "
                           "(%s, %s)\n", call->callType, call->callToken);
               finishPrint();
               removeEventHandler(call->pctxt);
               return OO_FAILED;
            }
            if(!pChannel->chanCap->startTransmitChannel)
            {
               OOTRACEERR3("ERROR:No callback registered to start transmit "
                         "channel (%s, %s)\n",call->callType, call->callToken);
               finishPrint();
               removeEventHandler(call->pctxt);
               return OO_FAILED;
            }
            pChannel->chanCap->startTransmitChannel(call, pChannel);
         }
         /* Mark the current channel as established and close all other 
            logical channels with same session id and in same direction.
         */
         ooOnLogicalChannelEstablished(call, pChannel);
      }
      finishPrint();
      removeEventHandler(call->pctxt);
      OO_SETFLAG(call->flags, OO_M_FASTSTARTANSWERED);
   }

   /* Retrieve tunneling info/H.245 control channel address from the connect msg */
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent && 
      !q931Msg->userInfo->h323_uu_pdu.h245Tunneling) {
	if (OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
		OO_CLRFLAG (call->flags, OO_M_TUNNELING);
		OOTRACEINFO3("Tunneling is disabled for call due to remote reject tunneling"
			      " (%s, %s)\n", call->callType, call->callToken);
	}
   }
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
      q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
      OO_TESTFLAG (call->flags, OO_M_TUNNELING) &&
      connect->m.h245AddressPresent) {
      OOTRACEINFO3("Tunneling and h245address provided."
                   "Giving preference to Tunneling (%s, %s)\n", 
                   call->callType, call->callToken);
   }
   else if(connect->m.h245AddressPresent)
   {
      if (OO_TESTFLAG (call->flags, OO_M_TUNNELING))
      {
         OO_CLRFLAG (call->flags, OO_M_TUNNELING);
         OOTRACEINFO3("Tunneling is disabled for call as H245 address is "
                      "provided in connect message (%s, %s)\n", 
                      call->callType, call->callToken);
      }
      ret = ooH323GetIpPortFromH225TransportAddress(call, 
                 &connect->h245Address, call->remoteIP, &call->remoteH245Port);
      if(ret != OO_OK)
      {
         OOTRACEERR3("Error: Unknown H245 address type in received Connect "
                     "message (%s, %s)", call->callType, call->callToken);
         /* Mark call for clearing */
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_INVALIDMESSAGE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
   }

   if(call->remoteH245Port != 0 && !call->pH245Channel)
   {
       /* Create an H.245 connection. 
      */
      if(ooCreateH245Connection(call)== OO_FAILED)
      {
         OOTRACEERR3("Error: H.245 channel creation failed (%s, %s)\n", 
                     call->callType, call->callToken);

         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
   }

   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent)
   {
      if (!q931Msg->userInfo->h323_uu_pdu.h245Tunneling)
      {
         if (OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         {
            OO_CLRFLAG (call->flags, OO_M_TUNNELING);
            OOTRACEINFO3("Tunneling is disabled by remote endpoint.(%s, %s)\n",
                          call->callType, call->callToken);
         }
      }
   }
   if (OO_TESTFLAG(call->flags, OO_M_TUNNELING))
   {
      OOTRACEDBGB3("Handling tunneled messages in CONNECT. (%s, %s)\n",
                    call->callType, call->callToken);
      ret = ooHandleTunneledH245Messages
         (call, &q931Msg->userInfo->h323_uu_pdu);
      OOTRACEDBGB3("Finished tunneled messages in Connect. (%s, %s)\n",
                    call->callType, call->callToken);

      /*
        Send TCS as call established and no capability exchange has yet 
        started. This will be true only when separate h245 connection is not
        established and tunneling is being used.
      */
      if(call->localTermCapState == OO_LocalTermCapExchange_Idle)
      {
         /*Start terminal capability exchange and master slave determination */
         ret = ooSendTermCapMsg(call);
         if(ret != OO_OK)
         {
            OOTRACEERR3("ERROR:Sending Terminal capability message (%s, %s)\n",
                         call->callType, call->callToken);
            return ret;
         }
      }

   }
   call->callState = OO_CALL_CONNECTED;
   if (call->rtdrCount > 0 && call->rtdrInterval > 0) {
        return ooSendRoundTripDelayRequest(call);
   }
   return OO_OK;  
}

int ooHandleH2250Message(OOH323CallData *call, Q931Message *q931Msg)
{
   int ret=OO_OK;
   ASN1UINT i;
   DListNode *pNode = NULL;
   OOTimer *pTimer=NULL;
   int type = q931Msg->messageType;
   struct timeval tv;
   struct timespec ts;

/* checking of message validity for first/next messages of calls */

   if (!strcmp(call->callType, "incoming")) {
	if ((call->callState != OO_CALL_CREATED && type == Q931SetupMsg) ||
	    (call->callState == OO_CALL_CREATED && type != Q931SetupMsg)) {
		ooFreeQ931Message(call->msgctxt, q931Msg);
		return OO_FAILED;
	}
   }

   switch(type)
   {
      case Q931SetupMsg: /* SETUP message is received */
         OOTRACEINFO3("Received SETUP message (%s, %s)\n", call->callType,
                       call->callToken);
         ooOnReceivedSetup(call, q931Msg);

         /* H225 message callback */
         if(gH323ep.h225Callbacks.onReceivedSetup)
            ret = gH323ep.h225Callbacks.onReceivedSetup(call, q931Msg);

         /* Free up the mem used by the received message, as it's processing 
            is done. 
         */
	 if (ret == OO_OK) {

         ooFreeQ931Message(call->msgctxt, q931Msg);
         
         /* DISABLEGK is used to selectively disable gatekeeper use. For 
            incoming calls DISABLEGK can be set in onReceivedSetup callback by 
            application. Very useful in pbx applications where gk is used only 
            when call is to or from outside pbx domian
         */
         if(gH323ep.gkClient && !OO_TESTFLAG(call->flags, OO_M_DISABLEGK))
         {
            if(gH323ep.gkClient->state == GkClientRegistered)
            {
               call->callState = OO_CALL_WAITING_ADMISSION;
	       ast_mutex_lock(&call->Lock);
               ret = ooGkClientSendAdmissionRequest(gH323ep.gkClient, call, 
                                                    FALSE);
				tv = ast_tvnow();
                ts.tv_sec = tv.tv_sec + 24;
				ts.tv_nsec = tv.tv_usec * 1000;
                ast_cond_timedwait(&call->gkWait, &call->Lock, &ts);
                if (call->callState == OO_CALL_WAITING_ADMISSION)
			call->callState = OO_CALL_CLEAR;
                ast_mutex_unlock(&call->Lock);

            }
            else {
               /* TODO: Should send Release complete with reject reason */
               OOTRACEERR1("Error:Ignoring incoming call as not yet"
                           "registered with Gk\n");
	       call->callState = OO_CALL_CLEAR;
            }
         }
	 if (call->callState < OO_CALL_CLEAR) {
         	ooSendCallProceeding(call);/* Send call proceeding message*/
         	ret = ooH323CallAdmitted (call);
	  }

	 call->callState = OO_CALL_CONNECTING;

	 } /* end ret == OO_OK */
         break;


      case Q931CallProceedingMsg: /* CALL PROCEEDING message is received */
         OOTRACEINFO3("H.225 Call Proceeding message received (%s, %s)\n",
                      call->callType, call->callToken);
         ooOnReceivedCallProceeding(call, q931Msg);

         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;


      case Q931AlertingMsg:/* ALERTING message received */
         OOTRACEINFO3("H.225 Alerting message received (%s, %s)\n", 
                      call->callType, call->callToken);

	 call->alertingTime = (H235TimeStamp) time(NULL);
         ooOnReceivedAlerting(call, q931Msg);

         if(gH323ep.h323Callbacks.onAlerting && call->callState<OO_CALL_CLEAR)
            gH323ep.h323Callbacks.onAlerting(call);
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;


      case Q931ProgressMsg:/* PROGRESS message received */
         OOTRACEINFO3("H.225 Progress message received (%s, %s)\n", 
                      call->callType, call->callToken);

         ooOnReceivedProgress(call, q931Msg);

         if(gH323ep.h323Callbacks.onProgress && call->callState<OO_CALL_CLEAR)
            gH323ep.h323Callbacks.onProgress(call);
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;


      case Q931ConnectMsg:/* CONNECT message received */
         OOTRACEINFO3("H.225 Connect message received (%s, %s)\n",
                      call->callType, call->callToken);

	 call->connectTime = (H235TimeStamp) time(NULL);

         /* Disable call establishment timer */
         for(i = 0; i<call->timerList.count; i++)
         {
            pNode = dListFindByIndex(&call->timerList, i);
            pTimer = (OOTimer*)pNode->data;
            if(((ooTimerCallback*)pTimer->cbData)->timerType & 
                                                         OO_CALLESTB_TIMER)
            {
               memFreePtr(call->pctxt, pTimer->cbData);
               ooTimerDelete(call->pctxt, &call->timerList, pTimer);
               OOTRACEDBGC3("Deleted CallESTB timer. (%s, %s)\n", 
                                              call->callType, call->callToken);
               break;
            }
         }
         ret = ooOnReceivedSignalConnect(call, q931Msg);
         if(ret != OO_OK)
            OOTRACEERR3("Error:Invalid Connect message received. (%s, %s)\n",
                        call->callType, call->callToken);
         else{
             /* H225 message callback */
            if(gH323ep.h225Callbacks.onReceivedConnect)
               gH323ep.h225Callbacks.onReceivedConnect(call, q931Msg);

            if(gH323ep.h323Callbacks.onCallEstablished)
               gH323ep.h323Callbacks.onCallEstablished(call);
         }
         ooFreeQ931Message(call->msgctxt, q931Msg);

         if(gH323ep.gkClient && !OO_TESTFLAG(call->flags, OO_M_DISABLEGK)) {
            if(gH323ep.gkClient->state == GkClientRegistered) {
		ooGkClientSendIRR(gH323ep.gkClient, call);
	    }
	 }
         break;
      case Q931InformationMsg:
         OOTRACEINFO3("H.225 Information msg received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;


      case Q931ReleaseCompleteMsg:/* RELEASE COMPLETE message received */
         OOTRACEINFO3("H.225 Release Complete message received (%s, %s)\n",
                      call->callType, call->callToken);

	 call->endTime = (H235TimeStamp) time(NULL);

         ooOnReceivedReleaseComplete(call, q931Msg);
         
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;
      case Q931FacilityMsg: 
         OOTRACEINFO3("H.225 Facility message Received (%s, %s)\n",
                       call->callType, call->callToken);

         ooOnReceivedFacility(call, q931Msg); 
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;
      case Q931StatusMsg:
         OOTRACEINFO3("H.225 Status message received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;
      case Q931StatusEnquiryMsg:
         OOTRACEINFO3("H.225 Status Inquiry message Received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;
      case Q931SetupAckMsg:
         OOTRACEINFO3("H.225 Setup Ack message received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;
      case Q931NotifyMsg: 
         OOTRACEINFO3("H.225 Notify message Received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(call->msgctxt, q931Msg);
         break;
      default:
         OOTRACEWARN3("Invalid H.225 message type received (%s, %s)\n",
                      call->callType, call->callToken);
         ooFreeQ931Message(call->msgctxt, q931Msg);
   }
   return ret;
}

int ooOnReceivedFacility(OOH323CallData *call, Q931Message * pQ931Msg)
{
   H225H323_UU_PDU * pH323UUPdu = NULL;
   H225Facility_UUIE * facility = NULL;
   int ret;
   H225TransportAddress_ipAddress_ip *ip = NULL;
   OOTRACEDBGC3("Received Facility Message.(%s, %s)\n", call->callType, 
                                                        call->callToken);
   /* Get Reference to H323_UU_PDU */
   if(!pQ931Msg->userInfo)
   {
      OOTRACEERR3("Error: UserInfo not found in received H.225 Facility "
                  "message (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   pH323UUPdu = &pQ931Msg->userInfo->h323_uu_pdu;
   if(!pH323UUPdu)
   {
      OOTRACEERR1("ERROR: H225H323_UU_PDU absent in incoming facility "
                  "message\n");
      return OO_FAILED;
   }
   facility = pH323UUPdu->h323_message_body.u.facility;
   if(facility)
   {
      /* Depending on the reason of facility message handle the message */
      if(facility->reason.t == T_H225FacilityReason_transportedInformation)
      {
         if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         {
            OOTRACEDBGB3("Handling tunneled messages in Facility. (%s, %s)\n",
               call->callType, call->callToken);
            ooHandleTunneledH245Messages(call, pH323UUPdu);
            OOTRACEDBGB3("Finished handling tunneled messages in Facility."
                         "(%s, %s)\n",call->callType, call->callToken);
         }
         else
         {
            OOTRACEERR3("ERROR:Tunneled H.245 message received in facility. "
                        "Tunneling is disabled at local for this call (%s, %s)\n",
                        call->callType, call->callToken);
            return OO_FAILED;
         }
      }
      else if(facility->reason.t == T_H225FacilityReason_startH245)
      {
         OOTRACEINFO3("Remote wants to start a separate H.245 Channel "
                      "(%s, %s)\n", call->callType, call->callToken);
         /*start H.245 channel*/
         ret = ooHandleStartH245FacilityMessage(call, facility);
         if(ret != OO_OK)
         {
            OOTRACEERR3("ERROR: Handling startH245 facility message "
                        "(%s, %s)\n", call->callType, call->callToken);
            return ret;
         }
      }
      else if(facility->reason.t == T_H225FacilityReason_callForwarded)
      {
         OOTRACEINFO3("Call Forward Facility message received. (%s, %s)\n",
                      call->callType, call->callToken);
         if(!facility->m.alternativeAddressPresent && 
            !facility->m.alternativeAliasAddressPresent)
         {
            OOTRACEERR3("Error:No alternative address provided in call forward"
                       "facility message.(%s, %s)\n", call->callType, 
                        call->callToken);
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callState = OO_CALL_CLEAR;
               call->callEndReason = OO_REASON_INVALIDMESSAGE;
            }
            return OO_OK;
         }
         call->pCallFwdData = (OOCallFwdData *) memAlloc(call->pctxt, 
                                                        sizeof(OOCallFwdData));
         if(!call->pCallFwdData)
         {
            OOTRACEERR3("Error:Memory - ooOnReceivedFacility - pCallFwdData "
                        "(%s, %s)\n", call->callType, call->callToken);
            return OO_FAILED;
         }
         call->pCallFwdData->fwdedByRemote = TRUE;
         call->pCallFwdData->ip[0]='\0';
         call->pCallFwdData->aliases = NULL;
         if(facility->m.alternativeAddressPresent)
         {
            if(facility->alternativeAddress.t != 
                                           T_H225TransportAddress_ipAddress)
            {
               OOTRACEERR3("ERROR: Source call signalling address type not ip "
                           "(%s, %s)\n", call->callType, call->callToken);
           
               return OO_FAILED;
            }

            ip = &facility->alternativeAddress.u.ipAddress->ip;
            sprintf(call->pCallFwdData->ip, "%d.%d.%d.%d", ip->data[0], 
                                       ip->data[1], ip->data[2], ip->data[3]);
            call->pCallFwdData->port =  
                               facility->alternativeAddress.u.ipAddress->port;
         }

         if(facility->m.alternativeAliasAddressPresent)
         {
            ooH323RetrieveAliases(call, &facility->alternativeAliasAddress, 
                                  &call->pCallFwdData->aliases);
         }
         /* Now we have to clear the current call and make a new call to
            fwded location*/
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callState = OO_CALL_CLEAR;
            call->callEndReason = OO_REASON_REMOTE_FWDED;
         }
         else{
            OOTRACEERR3("Error:Can't forward call as it is being cleared."
                        " (%s, %s)\n", call->callType, call->callToken);
           return OO_OK;
         }
      }
      else if(facility->reason.t == T_H225FacilityReason_forwardedElements)
      {
         OOTRACEINFO3("Handling fast start in forwardedElem facility for "
                      "(%s, %s)\n", call->callType, call->callToken);
         /*start H.245 channel*/
         ret = ooHandleFastStart(call, facility);
         if(ret != OO_OK)
         {
            OOTRACEERR3("ERROR: Handling transportedInformation facility message "
                        "(%s, %s)\n", call->callType, call->callToken);
            return ret;
         }
      }
      else{
         OOTRACEINFO3("Unhandled Facility reason type received (%s, %s)\n", 
                       call->callType, call->callToken);
      }
   }
   else{ /* Empty facility message Check for tunneling */
      if (pH323UUPdu->h323_message_body.t == 
	  T_H225H323_UU_PDU_h323_message_body_empty) {
      OOTRACEDBGB3("Handling tunneled messages in empty Facility message."
                   " (%s, %s)\n", call->callType, call->callToken);
      ooHandleTunneledH245Messages(call, pH323UUPdu);
      OOTRACEDBGB3("Finished handling tunneled messages in empty Facility "
                   "message. (%s, %s)\n", call->callType, call->callToken);
      }
   }
   
   return OO_OK;
}

int ooHandleStartH245FacilityMessage
   (OOH323CallData *call, H225Facility_UUIE *facility)
{
   H225TransportAddress_ipAddress *ipAddress = NULL;
   int ret;
   
   /* Extract H245 address */
   if(!facility->m.h245AddressPresent)
   {
      OOTRACEERR3("ERROR: startH245 facility message received with no h245 "
                  "address (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   if(facility->h245Address.t != T_H225TransportAddress_ipAddress)
   {
      OOTRACEERR3("ERROR:Unknown H245 address type in received startH245 "
               "facility message (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   ipAddress = facility->h245Address.u.ipAddress;
   if(!ipAddress)
   {
      OOTRACEERR3("ERROR:Invalid startH245 facility message. No H245 ip "
                  "address found. (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   
   sprintf(call->remoteIP, "%d.%d.%d.%d", ipAddress->ip.data[0],
                                          ipAddress->ip.data[1],
                                          ipAddress->ip.data[2],
                                          ipAddress->ip.data[3]);
   call->remoteH245Port = ipAddress->port;

   /* disable tunneling for this call */
   OO_CLRFLAG (call->flags, OO_M_TUNNELING);

   /*Establish an H.245 connection */
   if (!call->pH245Channel) {
    ret = ooCreateH245Connection(call);
    if(ret != OO_OK)
    {
      OOTRACEERR3("ERROR: Failed to establish an H.245 connection with remote"
                  " endpoint (%s, %s)\n", call->callType, call->callToken);
      return ret;
    }
   } else {
     OOTRACEINFO3("INFO: H.245 connection already established with remote"
                  " endpoint (%s, %s)\n", call->callType, call->callToken);
   }
   return OO_OK;
}

int ooHandleTunneledH245Messages
   (OOH323CallData *call, H225H323_UU_PDU * pH323UUPdu)
{
   H245Message *pmsg;
   OOCTXT *pctxt = call->msgctxt;
   int ret=0,i=0;
   
   OOTRACEDBGC3("Checking for tunneled H.245 messages (%s, %s)\n", 
                 call->callType, call->callToken);

   /* Check whether there are tunneled messages */  
   if(pH323UUPdu->m.h245TunnelingPresent)
   {
      if(pH323UUPdu->h245Tunneling)
      {
         OOTRACEDBGB4("Total number of tunneled H245 messages are %d.(%s, %s)"
                      "\n", (int)pH323UUPdu->h245Control.n, call->callType, 
                      call->callToken);
         for(i=0; i< (int)pH323UUPdu->h245Control.n; i++)
         {
            OOTRACEDBGC5("Retrieving %d of %d tunneled H.245 messages."
                         "(%s, %s)\n",i+1, pH323UUPdu->h245Control.n,
                         call->callType, call->callToken);
            pmsg = (H245Message*)memAlloc(pctxt, sizeof(H245Message));
            if(!pmsg)
            {
               OOTRACEERR3("Error:Memory - ooHandleH245TunneledMessages - pmsg"
                          "(%s, %s)\n", call->callType, call->callToken);
               return OO_FAILED;
            }

            setPERBuffer(pctxt, 
                         (ASN1OCTET*)pH323UUPdu->h245Control.elem[i].data,
                         pH323UUPdu->h245Control.elem[i].numocts, 1);  

            initializePrintHandler(&printHandler, "Tunneled H.245 Message");
            memset(pmsg, 0, sizeof(H245Message));
            /* Set event handler */
            setEventHandler (pctxt, &printHandler);
            OOTRACEDBGC4("Decoding %d tunneled H245 message. (%s, %s)\n", 
                          i+1, call->callType, call->callToken);
            ret = asn1PD_H245MultimediaSystemControlMessage(pctxt, 
                                                            &(pmsg->h245Msg));
            if(ret != ASN_OK)
            {
               OOTRACEERR3("Error decoding H245 message (%s, %s)\n", 
                            call->callType, call->callToken);
               ooFreeH245Message(call,pmsg);
               return OO_FAILED;
            }
            finishPrint();
            removeEventHandler (pctxt);
            ooHandleH245Message(call, pmsg);
            memFreePtr(pctxt, pmsg);
            pmsg = NULL;
         }/* End of For loop */
      }/* End of if(h245Tunneling) */
   }
   return OO_OK;
}

int ooH323RetrieveAliases
   (OOH323CallData *call, H225_SeqOfH225AliasAddress *pAddresses, 
    OOAliases **aliasList)
{
   int i=0,j=0,k=0;
   DListNode* pNode=NULL;
   H225AliasAddress *pAliasAddress=NULL;
   OOAliases *newAlias=NULL;
   H225TransportAddress *pTransportAddrss=NULL;

   if(!pAddresses)
   {
      OOTRACEWARN3("Warn:No Aliases present (%s, %s)\n", call->callType, 
                    call->callToken);
      return OO_OK;
   }
   /* check for aliases */
   if(pAddresses->count<=0)
      return OO_OK;
   
   for(i=0; i<(int)pAddresses->count; i++)
   {
      pNode = dListFindByIndex (pAddresses, i);

      if(!pNode)
         continue;

      pAliasAddress = (H225AliasAddress*)pNode->data;

      if(!pAliasAddress)
         continue;

      newAlias = (OOAliases*)memAlloc(call->pctxt, sizeof(OOAliases));
      if(!newAlias)
      {
         OOTRACEERR3("ERROR:Memory - ooH323RetrieveAliases - newAlias "
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      memset(newAlias, 0, sizeof(OOAliases));
      switch(pAliasAddress->t)
      {
      case T_H225AliasAddress_dialedDigits:
         newAlias->type = T_H225AliasAddress_dialedDigits;
         newAlias->value = (char*) memAlloc(call->pctxt, 
                         strlen(pAliasAddress->u.dialedDigits)*sizeof(char)+1);
         if(!newAlias->value)
         {
            OOTRACEERR3("ERROR:Memory - ooH323RetrieveAliases - "
                        "newAlias->value(dialedDigits) (%s, %s)\n", 
                         call->callType, call->callToken);
            memFreePtr(call->pctxt, newAlias);  
            return OO_FAILED;
         }

         memcpy(newAlias->value, pAliasAddress->u.dialedDigits,
                           strlen(pAliasAddress->u.dialedDigits)*sizeof(char));
         newAlias->value[strlen(pAliasAddress->u.dialedDigits)*sizeof(char)]='\0';
         break;
      case T_H225AliasAddress_h323_ID:
         newAlias->type = T_H225AliasAddress_h323_ID;
         newAlias->value = (char*)memAlloc(call->pctxt, 
                         (pAliasAddress->u.h323_ID.nchars+1)*sizeof(char)+1);
         if(!newAlias->value)
         {
            OOTRACEERR3("ERROR:Memory - ooH323RetrieveAliases - "
                        "newAlias->value(h323id) (%s, %s)\n", call->callType, 
                         call->callToken);
            memFreePtr(call->pctxt, newAlias);  
            return OO_FAILED;
         }

         for(j=0, k=0; j<(int)pAliasAddress->u.h323_ID.nchars; j++)
         {
            if(pAliasAddress->u.h323_ID.data[j] < 256)
            {
               newAlias->value[k++] = (char) pAliasAddress->u.h323_ID.data[j];
            }
         }
         newAlias->value[k] = '\0';
         break;   
      case T_H225AliasAddress_url_ID:
         newAlias->type = T_H225AliasAddress_url_ID;
         newAlias->value = (char*)memAlloc(call->pctxt,
                              strlen(pAliasAddress->u.url_ID)*sizeof(char)+1);
         if(!newAlias->value)
         {
            OOTRACEERR3("ERROR:Memory - ooH323RetrieveAliases - "
                        "newAlias->value(urlid) (%s, %s)\n", call->callType, 
                         call->callToken);
            memFreePtr(call->pctxt, newAlias);  
            return OO_FAILED;
         }

         memcpy(newAlias->value, pAliasAddress->u.url_ID,
                               strlen(pAliasAddress->u.url_ID)*sizeof(char));
         newAlias->value[strlen(pAliasAddress->u.url_ID)*sizeof(char)]='\0';
         break;
      case T_H225AliasAddress_transportID:
         newAlias->type = T_H225AliasAddress_transportID;
         pTransportAddrss = pAliasAddress->u.transportID;
         if(pTransportAddrss->t != T_H225TransportAddress_ipAddress)
         {
            OOTRACEERR3("Error:Alias transportID not an IP address"
                        "(%s, %s)\n", call->callType, call->callToken);
            memFreePtr(call->pctxt, newAlias);
            break;
         }
         /* hopefully ip:port value can't exceed more than 30 
            characters */
         newAlias->value = (char*)memAlloc(call->pctxt, 
                                                      30*sizeof(char));
         sprintf(newAlias->value, "%d.%d.%d.%d:%d", 
                                  pTransportAddrss->u.ipAddress->ip.data[0],
                                  pTransportAddrss->u.ipAddress->ip.data[1],
                                  pTransportAddrss->u.ipAddress->ip.data[2],
                                  pTransportAddrss->u.ipAddress->ip.data[3],
                                  pTransportAddrss->u.ipAddress->port);
         break;
      case T_H225AliasAddress_email_ID:
         newAlias->type = T_H225AliasAddress_email_ID;
         newAlias->value = (char*)memAlloc(call->pctxt, 
                             strlen(pAliasAddress->u.email_ID)*sizeof(char)+1);
         if(!newAlias->value)
         {
            OOTRACEERR3("ERROR:Memory - ooH323RetrieveAliases - "
                        "newAlias->value(emailid) (%s, %s)\n", call->callType, 
                         call->callToken);
            memFreePtr(call->pctxt, newAlias);  
            return OO_FAILED;
         }

         memcpy(newAlias->value, pAliasAddress->u.email_ID,
                              strlen(pAliasAddress->u.email_ID)*sizeof(char));
         newAlias->value[strlen(pAliasAddress->u.email_ID)*sizeof(char)]='\0';
         break;
      default:
         OOTRACEERR3("Error:Unhandled Alias type (%s, %s)\n", 
                       call->callType, call->callToken);
         memFreePtr(call->pctxt, newAlias);
         continue;
      }

      newAlias->next = *aliasList;
      *aliasList = newAlias;

      newAlias = NULL;
     
     pAliasAddress = NULL;
     pNode = NULL;
   }/* endof: for */
   return OO_OK;
}


int ooPopulatePrefixList(OOCTXT *pctxt, OOAliases *pAliases,
                           H225_SeqOfH225SupportedPrefix *pPrefixList )
{
   H225SupportedPrefix *pPrefixEntry=NULL;
   OOAliases * pAlias=NULL;
   ASN1BOOL bValid=FALSE;

   dListInit(pPrefixList);
   if(pAliases)
   {
      pAlias = pAliases;
      while(pAlias)
      {
	 pPrefixEntry = NULL;
         switch(pAlias->type)
         {
         case T_H225AliasAddress_dialedDigits:
            pPrefixEntry = (H225SupportedPrefix *)memAlloc(pctxt, 
                                                     sizeof(H225SupportedPrefix));
            if(!pPrefixEntry) {
            	OOTRACEERR1("ERROR:Memory - ooPopulatePrefixList - pAliasEntry\n");
            	return OO_FAILED;
            }
            pPrefixEntry->prefix.t = T_H225AliasAddress_dialedDigits;
            pPrefixEntry->prefix.u.dialedDigits = (ASN1IA5String)memAlloc(pctxt,
                                                     strlen(pAlias->value)+1);
            if(!pPrefixEntry->prefix.u.dialedDigits) {
               OOTRACEERR1("ERROR:Memory - ooPopulatePrefixList - "
                           "dialedDigits\n");
               memFreePtr(pctxt, pPrefixEntry);
               return OO_FAILED;
            }
            strcpy(*(char**)&pPrefixEntry->prefix.u.dialedDigits, pAlias->value);
            bValid = TRUE;
            break;
         default:
            bValid = FALSE;                  
         }
         
         if(bValid)
            dListAppend( pctxt, pPrefixList, (void*)pPrefixEntry );
         
         pAlias = pAlias->next;
      }
   }
   return OO_OK;
}
int ooPopulateAliasList(OOCTXT *pctxt, OOAliases *pAliases,
                           H225_SeqOfH225AliasAddress *pAliasList, int pAliasType)
{
   H225AliasAddress *pAliasEntry=NULL;
   OOAliases * pAlias=NULL;
   ASN1BOOL bValid=FALSE;
   int i = 0;

   dListInit(pAliasList);
   if(pAliases)
   {
      pAlias = pAliases;
      while(pAlias)
      {
	 if (pAlias->value[0] == 0) {
	  pAlias = pAlias->next;
	  continue;
	 }
         pAliasEntry = (H225AliasAddress*)memAlloc(pctxt, 
                                                     sizeof(H225AliasAddress));
         if(!pAliasEntry)
         {
            OOTRACEERR1("ERROR:Memory - ooPopulateAliasList - pAliasEntry\n");
            return OO_FAILED;
         }

	 if (pAliasType && pAlias->type != pAliasType) {
		pAlias = pAlias->next;
		continue;
	 }
         switch(pAlias->type)
         {
            case T_H225AliasAddress_dialedDigits:
             pAliasEntry->t = T_H225AliasAddress_dialedDigits;
             pAliasEntry->u.dialedDigits = (ASN1IA5String)memAlloc(pctxt,
                                                     strlen(pAlias->value)+1);
             if(!pAliasEntry->u.dialedDigits)
             {
               OOTRACEERR1("ERROR:Memory - ooPopulateAliasList - "
                           "dialedDigits\n");
               memFreePtr(pctxt, pAliasEntry);
               return OO_FAILED;
             }
             strcpy(*(char**)&pAliasEntry->u.dialedDigits, pAlias->value);
             bValid = TRUE;
            break;
         case T_H225AliasAddress_h323_ID:
            pAliasEntry->t = T_H225AliasAddress_h323_ID;
            pAliasEntry->u.h323_ID.nchars = strlen(pAlias->value);
            pAliasEntry->u.h323_ID.data = (ASN116BITCHAR*)memAllocZ
                     (pctxt, strlen(pAlias->value)*sizeof(ASN116BITCHAR));
            
            if(!pAliasEntry->u.h323_ID.data)
            {
               OOTRACEERR1("ERROR:Memory - ooPopulateAliasList - h323_id\n");
               memFreePtr(pctxt, pAliasEntry);
               return OO_FAILED;
            }
            for(i=0; *(pAlias->value+i) != '\0'; i++)
               pAliasEntry->u.h323_ID.data[i] =(ASN116BITCHAR)pAlias->value[i];
            bValid = TRUE;
            break;
         case T_H225AliasAddress_url_ID:
            pAliasEntry->t = T_H225AliasAddress_url_ID;
            pAliasEntry->u.url_ID = (ASN1IA5String)memAlloc(pctxt, 
                                                     strlen(pAlias->value)+1);
            if(!pAliasEntry->u.url_ID)
            {
               OOTRACEERR1("ERROR:Memory - ooPopulateAliasList - url_id\n");
               memFreePtr(pctxt, pAliasEntry);               
               return OO_FAILED;
            }
            strcpy(*(char**)&pAliasEntry->u.url_ID, pAlias->value);
            bValid = TRUE;
            break;
         case T_H225AliasAddress_email_ID:
            pAliasEntry->t = T_H225AliasAddress_email_ID;
            pAliasEntry->u.email_ID = (ASN1IA5String)memAlloc(pctxt, 
                                                     strlen(pAlias->value)+1);
            if(!pAliasEntry->u.email_ID)
            {
               OOTRACEERR1("ERROR: Failed to allocate memory for EmailID "
                           "alias entry \n");
               return OO_FAILED;
            }
            strcpy(*(char**)&pAliasEntry->u.email_ID, pAlias->value);
            bValid = TRUE;
            break;
         default:
            OOTRACEERR1("ERROR: Unhandled alias type\n");
            bValid = FALSE;                  
         }
         
         if(bValid)
            dListAppend( pctxt, pAliasList, (void*)pAliasEntry );
         else
            memFreePtr(pctxt, pAliasEntry);
         
         pAlias = pAlias->next;
      }
   }
   return OO_OK;
}


OOAliases* ooH323GetAliasFromList(OOAliases *aliasList, int type, char *value)
{

   OOAliases *pAlias = NULL;

   if(!aliasList)
   {
      OOTRACEDBGC1("No alias List to search\n");
      return NULL;
   }

   pAlias = aliasList;

   while(pAlias)
   {
     if(type != 0 && value) { /* Search by type and value */
         if(pAlias->type == type && !strcmp(pAlias->value, value))
         {
            return pAlias;
         }
      }
      else if(type != 0 && !value) {/* search by type */
         if(pAlias->type == type)
            return pAlias;
      }
      else if(type == 0 && value) {/* search by value */
         if(!strcmp(pAlias->value, value))
            return pAlias;
      }
      else {
         OOTRACEDBGC1("No criteria to search the alias list\n");
         return NULL;
      }
      pAlias = pAlias->next;
   }

   return NULL;
}

OOAliases* ooH323AddAliasToList
(OOAliases **pAliasList, OOCTXT *pctxt, H225AliasAddress *pAliasAddress)
{
   int j=0,k=0;
   OOAliases *newAlias=NULL;
   H225TransportAddress *pTransportAddrss=NULL;
   
   newAlias = (OOAliases*) memAlloc(pctxt, sizeof(OOAliases));
   if(!newAlias)
   {
      OOTRACEERR1("Error: Failed to allocate memory for new alias to be added to the alias list\n");
      return NULL;
   }
   memset(newAlias, 0, sizeof(OOAliases));

   switch(pAliasAddress->t)
   {
   case T_H225AliasAddress_dialedDigits:
      newAlias->type = T_H225AliasAddress_dialedDigits;
      newAlias->value = (char*) memAlloc(pctxt, strlen(pAliasAddress->u.dialedDigits)*sizeof(char)+1);
      strcpy(newAlias->value, pAliasAddress->u.dialedDigits);
      break;
   case T_H225AliasAddress_h323_ID:
      newAlias->type = T_H225AliasAddress_h323_ID;
      newAlias->value = (char*)memAlloc(pctxt, 
                           (pAliasAddress->u.h323_ID.nchars+1)*sizeof(char)+1);

      for(j=0, k=0; j<(int)pAliasAddress->u.h323_ID.nchars; j++)
      {
         if(pAliasAddress->u.h323_ID.data[j] < 256)
         {
            newAlias->value[k++] = (char) pAliasAddress->u.h323_ID.data[j];
         }
      }
      newAlias->value[k] = '\0';
      break;   
   case T_H225AliasAddress_url_ID:
      newAlias->type = T_H225AliasAddress_url_ID;
      newAlias->value = (char*)memAlloc(pctxt,
                            strlen(pAliasAddress->u.url_ID)*sizeof(char)+1);

      strcpy(newAlias->value, pAliasAddress->u.url_ID);
      break;
   case T_H225AliasAddress_transportID:
      newAlias->type = T_H225AliasAddress_transportID;
      pTransportAddrss = pAliasAddress->u.transportID;
      if(pTransportAddrss->t != T_H225TransportAddress_ipAddress)
      {
         OOTRACEERR1("Error:Alias transportID not an IP address\n");
         memFreePtr(pctxt, newAlias);
         return NULL;
      }
      /* hopefully ip:port value can't exceed more than 30 
         characters */
      newAlias->value = (char*)memAlloc(pctxt, 
                                              30*sizeof(char));
      sprintf(newAlias->value, "%d.%d.%d.%d:%d", 
                               pTransportAddrss->u.ipAddress->ip.data[0],
                               pTransportAddrss->u.ipAddress->ip.data[1],
                               pTransportAddrss->u.ipAddress->ip.data[2],
                               pTransportAddrss->u.ipAddress->ip.data[3],
                               pTransportAddrss->u.ipAddress->port);
      break;
   case T_H225AliasAddress_email_ID:
      newAlias->type = T_H225AliasAddress_email_ID;
      newAlias->value = (char*)memAlloc(pctxt, 
                 strlen(pAliasAddress->u.email_ID)*sizeof(char)+1);

      strcpy(newAlias->value, pAliasAddress->u.email_ID);
      break;
   default:
      OOTRACEERR1("Error:Unhandled Alias type \n");
      memFreePtr(pctxt, newAlias);
      return NULL;

   }
   newAlias->next = *pAliasList;
   *pAliasList= newAlias;
   return newAlias;
}

int ooH323GetIpPortFromH225TransportAddress(struct OOH323CallData *call, 
   H225TransportAddress *h225Address, char *ip, int *port)
{
   if(h225Address->t != T_H225TransportAddress_ipAddress)
   {
      OOTRACEERR3("Error: Unknown H225 address type. (%s, %s)", call->callType,
                   call->callToken);
      return OO_FAILED;
   }
   sprintf(ip, "%d.%d.%d.%d", 
              h225Address->u.ipAddress->ip.data[0], 
              h225Address->u.ipAddress->ip.data[1],
              h225Address->u.ipAddress->ip.data[2],
              h225Address->u.ipAddress->ip.data[3]);
   *port = h225Address->u.ipAddress->port;
   return OO_OK;
}
