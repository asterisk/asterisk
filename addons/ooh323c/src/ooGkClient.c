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
 * @file ooGkClient.c 
 * This file contains functions to support RAS protocol. 
 *
 */
#include "asterisk.h"
#include "asterisk/lock.h"
#include "asterisk/netsock2.h"

#include "ooGkClient.h"
#include "ootypes.h"
#include "ootrace.h"
#include "ooports.h"
#include "ooasn1.h"
#include "oochannels.h"
#include "printHandler.h"
#include "ooCalls.h"
#include "H323-MESSAGES.h"
#include "ooDateTime.h"
#include "ooq931.h"
#include "ooh323.h"
#include "ooh323ep.h"
#include "ooTimer.h"
#include "ooSocket.h"
#include "ooUtils.h"

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

static ASN1OBJID gProtocolID = {
   6, { 0, 0, 8, 2250, 0, 4 }
};

int ooGkClientInit(enum RasGatekeeperMode eGkMode,
              char *szGkAddr, int iGkPort )
{
   ooGkClient *pGkClient=NULL;
   OOInterface *cur=NULL;
   pGkClient = (ooGkClient*)
                         memAlloc(&gH323ep.ctxt, sizeof(ooGkClient));
   if(!pGkClient)
   {
      OOTRACEERR1("Error: Failed to allocate memory to Gatekeeper Client.\n");
      return OO_FAILED;
   }
 
   memset(pGkClient, 0, sizeof(ooGkClient));
   ast_mutex_init(&pGkClient->Lock);
   gH323ep.gkClient = pGkClient;
   initContext(&(pGkClient->ctxt));
   initContext(&(pGkClient->msgCtxt));
   pGkClient->rrqRetries = 0;
   pGkClient->grqRetries = 0;

   strcpy(pGkClient->localRASIP, gH323ep.signallingIP);
#ifndef _WIN32
   if(!strcmp(pGkClient->localRASIP, "0.0.0.0") ||
      !strcmp(pGkClient->localRASIP, "127.0.0.1"))
   {
      if(!gH323ep.ifList)
      {
         if(ooSocketGetInterfaceList(&gH323ep.ctxt, &gH323ep.ifList)!= ASN_OK)
         {
            OOTRACEERR1("Error:Failed to retrieve interface addresses\n");
            return OO_FAILED;
         }
      }
      for(cur = gH323ep.ifList; cur; cur = cur->next)
      {
         if(!strcmp(cur->name, "lo") || !strcmp(cur->addr, "127.0.0.1"))
            continue;
         break;
      }
      if(cur)
      {
         OOTRACEINFO2("Using local RAS Ip address %s\n", cur->addr);
         strcpy(pGkClient->localRASIP, cur->addr);
      }
      else{
         OOTRACEERR1("Error:Failed to assign a local RAS IP address\n");
         return OO_FAILED;
      }
   }
#endif   
   if(OO_OK != ooGkClientSetGkMode(pGkClient, eGkMode, szGkAddr, iGkPort))
   {
      OOTRACEERR1("Error:Failed to set Gk mode\n");
      memReset(&gH323ep.ctxt);
      return OO_FAILED;
   }
  
   /* Create default parameter set */
   pGkClient->grqTimeout = DEFAULT_GRQ_TIMEOUT;
   pGkClient->rrqTimeout = DEFAULT_RRQ_TIMEOUT;
   pGkClient->regTimeout = DEFAULT_REG_TTL;
   pGkClient->arqTimeout = DEFAULT_ARQ_TIMEOUT;
   pGkClient->drqTimeout = DEFAULT_DRQ_TIMEOUT;
   dListInit(&pGkClient->callsPendingList);
   dListInit(&pGkClient->callsAdmittedList);
   dListInit(&pGkClient->timerList);
   pGkClient->state = GkClientIdle;
   return OO_OK;
}


int ooGkClientSetCallbacks
   (ooGkClient *pGkClient, OOGKCLIENTCALLBACKS callbacks)
{
   pGkClient->callbacks.onReceivedRegistrationConfirm = 
                                      callbacks.onReceivedRegistrationConfirm;
   pGkClient->callbacks.onReceivedUnregistrationConfirm = 
                                     callbacks.onReceivedUnregistrationConfirm;
   pGkClient->callbacks.onReceivedUnregistrationRequest = 
                                     callbacks.onReceivedUnregistrationRequest;
   return OO_OK;
}

int ooGkClientReInit(ooGkClient *pGkClient)
{

   ooGkClientCloseChannel(pGkClient);
   pGkClient->gkRasIP[0]='\0';
   pGkClient->gkCallSignallingIP[0]='\0';
   pGkClient->gkRasPort = 0;
   pGkClient->gkCallSignallingPort = 0;
   pGkClient->rrqRetries = 0;
   pGkClient->grqRetries = 0;
   pGkClient->requestSeqNum = 0;
   
   dListFreeAll(&pGkClient->ctxt, &pGkClient->callsPendingList);
   dListFreeAll(&pGkClient->ctxt, &pGkClient->callsAdmittedList);
   dListFreeAll(&pGkClient->ctxt, &pGkClient->timerList);
   pGkClient->state = GkClientIdle;
   return OO_OK;
}

void ooGkClientPrintConfig(ooGkClient *pGkClient)
{
   OOTRACEINFO1("Gatekeeper Client Configuration:\n");
   if(pGkClient->gkMode == RasUseSpecificGatekeeper)
   {
      OOTRACEINFO1("\tGatekeeper mode - UseSpecificGatekeeper\n");
      OOTRACEINFO3("\tGatekeeper To Use - %s:%d\n", pGkClient->gkRasIP, 
                                                    pGkClient->gkRasPort);
   }
   else if(pGkClient->gkMode == RasDiscoverGatekeeper) {
      OOTRACEINFO1("\tGatekeeper mode - RasDiscoverGatekeeper\n");
   }
   else {
      OOTRACEERR1("Invalid GatekeeperMode\n");
   }
}

int ooGkClientDestroy(void)
{
   if(gH323ep.gkClient)
   {
      if(gH323ep.gkClient->state == GkClientRegistered)
      {
         OOTRACEINFO1("Unregistering from Gatekeeper\n");
         if(ooGkClientSendURQ(gH323ep.gkClient, NULL)!=OO_OK)
            OOTRACEERR1("Error:Failed to send URQ to gatekeeper\n");
      }
      OOTRACEINFO1("Destroying Gatekeeper Client\n");
      ooGkClientCloseChannel(gH323ep.gkClient);
      freeContext(&gH323ep.gkClient->msgCtxt);
      freeContext(&gH323ep.gkClient->ctxt);
      ast_mutex_lock(&gH323ep.gkClient->Lock);
      ast_mutex_unlock(&gH323ep.gkClient->Lock);
      ast_mutex_destroy(&gH323ep.gkClient->Lock);
      memFreePtr(&gH323ep.ctxt, gH323ep.gkClient);
      gH323ep.gkClient = NULL;
   }
   return OO_OK;
}

int ooGkClientStart(ooGkClient *pGkClient)
{
   int iRet=0;
   iRet = ooGkClientCreateChannel(pGkClient);

   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error: GkClient Channel Creation failed\n");
      return OO_FAILED;
   }
   
   ast_mutex_lock(&pGkClient->Lock);
   pGkClient->discoveryComplete = FALSE;
   iRet = ooGkClientSendGRQ(pGkClient);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error:Failed to send GRQ message\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}
   

int ooGkClientSetGkMode(ooGkClient *pGkClient, enum RasGatekeeperMode eGkMode, 
                        char *szGkAddr, int iGkPort )
{
   pGkClient->gkMode = eGkMode;
   if(eGkMode == RasUseSpecificGatekeeper)
   {
      OOTRACEINFO1("Gatekeeper Mode - RasUseSpecificGatekeeper\n");
      if(szGkAddr)
      {
         if(strlen(szGkAddr)>MAX_IP_LEN)
         { 
            OOTRACEERR2("Error:Invalid IP address specified - %s\n", szGkAddr);
            return OO_FAILED;
         }
         strcpy(pGkClient->gkRasIP, szGkAddr);
      }
      if(iGkPort)
         pGkClient->gkRasPort = iGkPort;
      else
         pGkClient->gkRasPort = DEFAULT_GKPORT;

      OOTRACEINFO3("Gatekeeper IP:port set to - %s:%d\n", 
                    szGkAddr,  pGkClient->gkRasPort);
   }
   else if(eGkMode == RasDiscoverGatekeeper) {
      OOTRACEINFO1("Gatekeeper Mode - RasDiscoverGatekeeper\n");
   }
   else if(eGkMode == RasNoGatekeeper) {
      OOTRACEINFO1("Gatekeeper Mode - RasNoGatekeeper\n");
   }

   return OO_OK;
}

   
/**
 * Create the RAS channel (socket).
 *
 */

int ooGkClientCreateChannel(ooGkClient *pGkClient)
{
   int ret=0;
   OOIPADDR ipaddrs;
   /* Create socket */
   if((ret=ooSocketCreateUDP(&pGkClient->rasSocket, 4))!=ASN_OK)
   {
      OOTRACEERR1("Failed to create RAS socket\n");
      pGkClient->state = GkClientFailed;
      return OO_FAILED;
   }
   if(pGkClient->localRASPort)
   {
      inet_pton(AF_INET, pGkClient->localRASIP, &ipaddrs);
      if( (ret=ooSocketBind( pGkClient->rasSocket, ipaddrs, 
           pGkClient->localRASPort))!=ASN_OK ) 
      {
         OOTRACEERR1("ERROR:Failed to create RAS channel\n");
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }
   }
   else {
      ret = ooBindPort (OOUDP, pGkClient->rasSocket, pGkClient->localRASIP);
      if(ret == OO_FAILED)
      {
         OOTRACEERR1("ERROR: Failed to bind port to RAS socket\n");
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }
      pGkClient->localRASPort = ret;
   }
   /* Test Code NOTE- This doesn't work..:(( Have to fix this */
   /* If multihomed, get ip from socket */
   if(!strcmp(pGkClient->localRASIP, "0.0.0.0"))
   {
      OOTRACEDBGA1("Determining ip address for RAS channel "
                   "multihomed mode. \n");
      ret = ooSocketGetIpAndPort(pGkClient->rasSocket, pGkClient->localRASIP, 
                                 20, &pGkClient->localRASPort, NULL);
      if(ret != ASN_OK)
      {
         OOTRACEERR1("Error:Failed to retrieve local ip and port from "
                     "socket for RAS channel(multihomed).\n");
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }
      OOTRACEDBGA3("Using local ip %s and port %d for RAS channel"
                   "(multihomedMode).\n", pGkClient->localRASIP, 
                   pGkClient->localRASPort);
   }
   /* End of Test code */
   OOTRACEINFO1("H323 RAS channel creation - successful\n");
   return OO_OK;
}


int ooGkClientCloseChannel(ooGkClient *pGkClient)
{
   int ret;
   if(pGkClient->rasSocket != 0)
   {
      ret = ooSocketClose(pGkClient->rasSocket);
      if(ret != ASN_OK)
      {
         OOTRACEERR1("Error: failed to close RAS channel\n");
         pGkClient->rasSocket = 0;
         return OO_FAILED;
      }
      pGkClient->rasSocket = 0;
   }
   OOTRACEINFO1("Closed RAS channel\n");
   return OO_OK;
}


/**
 * Fill vendor data in RAS message structure.
 */

void ooGkClientFillVendor
   (ooGkClient *pGkClient, H225VendorIdentifier *pVendor )
{
   pVendor->vendor.t35CountryCode = gH323ep.t35CountryCode;
   pVendor->vendor.t35Extension = gH323ep.t35Extension;
   pVendor->vendor.manufacturerCode = gH323ep.manufacturerCode;
   pVendor->enterpriseNumber.numids=0;
   if(gH323ep.productID)
   {
      pVendor->m.productIdPresent = TRUE;
      pVendor->productId.numocts = ASN1MIN(strlen(gH323ep.productID), 
                                             sizeof(pVendor->productId.data));
      memcpy(pVendor->productId.data, gH323ep.productID, 
                                                  pVendor->productId.numocts);
   }
   if(gH323ep.versionID)
   {
      pVendor->m.versionIdPresent = 1;
      pVendor->versionId.numocts = ASN1MIN(strlen(gH323ep.versionID), 
                                             sizeof(pVendor->versionId.data));
      memcpy(pVendor->versionId.data, gH323ep.versionID, 
                                                 pVendor->versionId.numocts); 
   }
}   


int ooGkClientReceive(ooGkClient *pGkClient)
{
   ASN1OCTET recvBuf[1024];
   int recvLen;
   char remoteHost[32];
   int iFromPort=0;
   OOCTXT *pctxt=NULL;
   H225RasMessage *pRasMsg=NULL;
   int iRet=OO_OK;
   
   ast_mutex_lock(&pGkClient->Lock);
   pctxt = &pGkClient->msgCtxt;

   recvLen = ooSocketRecvFrom(pGkClient->rasSocket, recvBuf, 1024, remoteHost,
                              32, &iFromPort);
   if(recvLen <0)
   {
      OOTRACEERR1("Error:Failed to receive RAS message\n");
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   OOTRACEDBGA1("GkClient Received RAS Message\n");
  
   /* Verify the gk */
   if(pGkClient->discoveryComplete)
   {
      if((strncmp(pGkClient->gkRasIP, remoteHost,strlen(pGkClient->gkRasIP)))||
         (pGkClient->gkRasPort!= iFromPort) )
      {
         OOTRACEWARN3("WARN:Ignoring message received from unknown gatekeeper "
                      "%s:%d\n", remoteHost, iFromPort);
	 ast_mutex_unlock(&pGkClient->Lock);
         return OO_OK;
      }
   }

   if(ASN_OK != setPERBuffer (pctxt, recvBuf, recvLen, TRUE ) )
   {
      OOTRACEERR1("Error:Failed to set PER buffer for RAS message decoding\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }      
   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   if(!pRasMsg)
   {
      OOTRACEERR1("Error: Failed to allocate memory for RAS message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
#ifndef _COMPACT
   initializePrintHandler(&printHandler, "Received RAS Message");
   /* Add event handler to list */
   setEventHandler (pctxt, &printHandler);
#endif
   if(ASN_OK == asn1PD_H225RasMessage(pctxt, pRasMsg))
   {
#ifndef _COMPACT
      finishPrint();
      removeEventHandler(pctxt);
#endif
      iRet=ooGkClientHandleRASMessage( pGkClient, pRasMsg );
      if(iRet != OO_OK)
      {
         OOTRACEERR1("Error: Failed to handle received RAS message\n");
         //pGkClient->state = GkClientFailed;
      }
      memReset(pctxt);
   }
   else{
      OOTRACEERR1("ERROR:Failed to decode received RAS message- ignoring"
                  "received message.\n");
#ifndef _COMPACT
      removeEventHandler(pctxt);
#endif
      memReset(pctxt);
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   ast_mutex_unlock(&pGkClient->Lock);
   return iRet;
}




/**
 * Manage incoming RAS message.
 */

int ooGkClientHandleRASMessage(ooGkClient *pGkClient, H225RasMessage *pRasMsg)
{
   int iRet = OO_OK;   
   switch( pRasMsg->t)
   {
   case T_H225RasMessage_gatekeeperConfirm:
      OOTRACEINFO1("Gatekeeper Confirmed (GCF) message received.\n");
      iRet = ooGkClientHandleGatekeeperConfirm(pGkClient, 
                                          pRasMsg->u.gatekeeperConfirm);
      break;
   case T_H225RasMessage_gatekeeperReject: 
      OOTRACEINFO1("Gatekeeper Reject (GRJ) message received\n");
      iRet = ooGkClientHandleGatekeeperReject(pGkClient, 
                                              pRasMsg->u.gatekeeperReject);
      break;
   case T_H225RasMessage_registrationConfirm:   
      OOTRACEINFO1("Registration Confirm (RCF) message received\n");
      iRet = ooGkClientHandleRegistrationConfirm(pGkClient,  
                                              pRasMsg->u.registrationConfirm );
      break;
   case T_H225RasMessage_registrationReject:
      OOTRACEINFO1("Registration Reject (RRJ) message received.\n");
      iRet = ooGkClientHandleRegistrationReject(pGkClient, 
                                                pRasMsg->u.registrationReject);
      break;
   case T_H225RasMessage_infoRequest:  
      //ooRasSendIRR( psRasMsg->sMessage.u.infoRequest->requestSeqNum );
      break;
   case T_H225RasMessage_admissionConfirm:
      OOTRACEINFO1("Admission Confirm (ACF) message received\n");
      iRet = ooGkClientHandleAdmissionConfirm(pGkClient, 
                                              pRasMsg->u.admissionConfirm);
      break;
   case T_H225RasMessage_unregistrationRequest:
      OOTRACEINFO1("UnRegistration Request (URQ) message received.\n");
      iRet = ooGkClientHandleUnregistrationRequest(pGkClient, 
                                            pRasMsg->u.unregistrationRequest);
      break;
   case T_H225RasMessage_unregistrationConfirm:
      OOTRACEINFO1("UnRegistration Confirm (UCF) message received.\n");
      break;
   case T_H225RasMessage_unregistrationReject:
      OOTRACEINFO1("UnRegistration Reject (URJ) message received.\n");
      break;
   case T_H225RasMessage_admissionReject:
      OOTRACEINFO1("Admission Reject (ARJ) message received.\n");
      iRet = ooGkClientHandleAdmissionReject(pGkClient, 
                                                   pRasMsg->u.admissionReject);
      break;
   case T_H225RasMessage_disengageConfirm:
      iRet = ooGkClientHandleDisengageConfirm(pGkClient, 
                                              pRasMsg->u.disengageConfirm);
      break;
   case T_H225RasMessage_disengageReject:
   case T_H225RasMessage_bandwidthConfirm:
   case T_H225RasMessage_bandwidthReject:
   case T_H225RasMessage_locationRequest:
   case T_H225RasMessage_locationConfirm:
   case T_H225RasMessage_locationReject:
   case T_H225RasMessage_infoRequestResponse:
   case T_H225RasMessage_nonStandardMessage:
   case T_H225RasMessage_unknownMessageResponse:
   case T_H225RasMessage_requestInProgress:
   case T_H225RasMessage_resourcesAvailableIndicate:
   case T_H225RasMessage_resourcesAvailableConfirm:
   case T_H225RasMessage_infoRequestAck:
   case T_H225RasMessage_infoRequestNak:
   case T_H225RasMessage_serviceControlIndication:
   case T_H225RasMessage_serviceControlResponse:
   case T_H225RasMessage_admissionConfirmSequence:
   default:
      /* Unhandled RAS message */
      iRet= OO_OK;
   }

   return iRet;
}

#ifndef _COMPACT
void ooGkClientPrintMessage
   (ooGkClient *pGkClient, ASN1OCTET *msg, ASN1UINT len)
{
   OOCTXT ctxt;
   H225RasMessage rasMsg;
   int ret; 

   initContext(&ctxt);
   setPERBuffer(&ctxt, msg, len, TRUE);
   initializePrintHandler(&printHandler, "Sending RAS Message");
   setEventHandler(&ctxt, &printHandler);

   ret = asn1PD_H225RasMessage(&ctxt, &rasMsg);
   if(ret != ASN_OK)
   {
      OOTRACEERR1("Error: Failed to decode RAS message\n");
   }
   finishPrint();
   freeContext(&ctxt);
}
#endif
/**
 * Encode and send RAS message.
 */

int ooGkClientSendMsg(ooGkClient *pGkClient, H225RasMessage *pRasMsg)
{
   ASN1OCTET msgBuf[MAXMSGLEN];
   ASN1OCTET *msgPtr=NULL;
   int  iLen;

   OOCTXT *pctxt = &pGkClient->msgCtxt;

   setPERBuffer( pctxt, msgBuf, MAXMSGLEN, TRUE );
   if ( ASN_OK == asn1PE_H225RasMessage(pctxt, pRasMsg) )
   {
      OOTRACEDBGC1("Ras message encoding - successful\n");
   }
   else {
      OOTRACEERR1("Error: RAS message encoding failed\n");
      return OO_FAILED;
   }

   msgPtr = encodeGetMsgPtr( pctxt, &iLen );
   /* If gatekeeper specified or have been discovered */
   if(pGkClient->gkMode == RasUseSpecificGatekeeper || 
      pGkClient->discoveryComplete)
   {
      if(ASN_OK != ooSocketSendTo( pGkClient->rasSocket, msgPtr, iLen, 
                                   pGkClient->gkRasIP, pGkClient->gkRasPort))
      {
         OOTRACEERR1("Error sending RAS message\n");
         return OO_FAILED;
      }
   }
   else if(pGkClient->gkMode == RasDiscoverGatekeeper && 
           !pGkClient->discoveryComplete) { 
      if(ASN_OK != ooSocketSendTo(pGkClient->rasSocket, msgPtr, iLen, 
                                       MULTICAST_GKADDRESS, MULTICAST_GKPORT))
      {
         OOTRACEERR1("Error sending multicast RAS message\n" );
         return OO_FAILED;
      }
   }
   else {/* should never go here */ 
      OOTRACEERR1("Error: GkClient in invalid state.\n");
      return OO_FAILED;
   }
#ifndef _COMPACT
   ooGkClientPrintMessage(pGkClient, msgPtr, iLen);
#endif
   return OO_OK;
}



int ooGkClientSendGRQ(ooGkClient *pGkClient)
{
   int iRet;
   H225RasMessage *pRasMsg=NULL;
   H225GatekeeperRequest *pGkReq=NULL;
   H225TransportAddress_ipAddress *pRasAddress;
   OOCTXT *pctxt = &pGkClient->msgCtxt;
   ooGkClientTimerCb *cbData=NULL;

   ast_mutex_lock(&pGkClient->Lock);

   /* Allocate memory for RAS message */
   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   if(!pRasMsg)
   {
      OOTRACEERR1("Error: Memory allocation for GRQ RAS message failed\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   pGkReq = (H225GatekeeperRequest*)memAlloc(pctxt, 
                                                sizeof(H225GatekeeperRequest));
   if(!pGkReq)
   {
      OOTRACEERR1("Error:Memory allocation for GRQ failed\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memset(pGkReq, 0, sizeof(H225GatekeeperRequest));
   pRasMsg->t = T_H225RasMessage_gatekeeperRequest;
   pRasMsg->u.gatekeeperRequest = pGkReq;

   /* Populate message structure */
   pGkReq->requestSeqNum = pGkClient->requestSeqNum++;
   if ( !pGkReq->requestSeqNum )
      pGkReq->requestSeqNum = pGkClient->requestSeqNum++;

   pGkReq->protocolIdentifier = gProtocolID;
   pGkReq->m.nonStandardDataPresent=0;
   pGkReq->rasAddress.t=T_H225TransportAddress_ipAddress; /* IPv4 address */
   pRasAddress = (H225TransportAddress_ipAddress*)memAlloc(pctxt, 
                                      sizeof(H225TransportAddress_ipAddress));
   if(!pRasAddress)
   {
      OOTRACEERR1("Error: Memory allocation for Ras Address of GRQ message "
                  "failed\n");
      memReset(&pGkClient->msgCtxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

 
   inet_pton(AF_INET, pGkClient->localRASIP, pRasAddress->ip.data);

   pRasAddress->ip.numocts = 4;
   pRasAddress->port = pGkClient->localRASPort;
   pGkReq->rasAddress.u.ipAddress = pRasAddress;

   /* Pose as gateway or terminal as per config */
   if(gH323ep.isGateway)
      pGkReq->endpointType.m.gatewayPresent = TRUE;
   else
      pGkReq->endpointType.m.terminalPresent = TRUE;

   pGkReq->endpointType.m.nonStandardDataPresent=0;
   pGkReq->endpointType.m.vendorPresent=1;

   ooGkClientFillVendor(pGkClient, &pGkReq->endpointType.vendor);


   pGkReq->m.endpointAliasPresent=TRUE;
   if(OO_OK != ooPopulateAliasList(&pGkClient->msgCtxt, gH323ep.aliases, 
                                      &pGkReq->endpointAlias, 0))
   {
      OOTRACEERR1("Error Failed to fill alias information for GRQ message\n");
      memReset(&pGkClient->msgCtxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   iRet = ooGkClientSendMsg(pGkClient, pRasMsg);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error: Failed to send GRQ message\n");
      memReset(&pGkClient->msgCtxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   OOTRACEINFO1("Sent GRQ message\n");
   cbData = (ooGkClientTimerCb*) memAlloc
                               (&pGkClient->ctxt, sizeof(ooGkClientTimerCb));
   if(!cbData)
   {
      OOTRACEERR1("Error:Failed to allocate memory to GRQ timer callback\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   cbData->timerType = OO_GRQ_TIMER;
   cbData->pGkClient = pGkClient;
   if(!ooTimerCreate(&pGkClient->ctxt, &pGkClient->timerList, 
                     &ooGkClientGRQTimerExpired, pGkClient->grqTimeout, 
                     cbData, FALSE))      
   {
      OOTRACEERR1("Error:Unable to create GRQ timer.\n ");
      memFreePtr(&pGkClient->ctxt, cbData);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}

int ooGkClientHandleGatekeeperReject
   (ooGkClient *pGkClient, H225GatekeeperReject *pGatekeeperReject)
{
   unsigned int x=0;
   DListNode *pNode = NULL;
   OOTimer *pTimer = NULL;

   if(pGkClient->gkMode == RasUseSpecificGatekeeper)
   {
      /* delete the corresponding GRQ timer */
      for(x=0; x<pGkClient->timerList.count; x++)
      {
         pNode =  dListFindByIndex(&pGkClient->timerList, x);
         pTimer = (OOTimer*)pNode->data;
         if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_GRQ_TIMER)
         {
            memFreePtr(&pGkClient->ctxt, pTimer->cbData);
            ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, pTimer);
            OOTRACEDBGA1("Deleted GRQ Timer.\n");
            break;
         }
      }
 
      pGkClient->state = GkClientGkErr;
      switch(pGatekeeperReject->rejectReason.t)
      {
      case T_H225GatekeeperRejectReason_resourceUnavailable:
         OOTRACEERR1("Error: Gatekeeper Reject - Resource Unavailable\n");
         break;
      case T_H225GatekeeperRejectReason_terminalExcluded:
         OOTRACEERR1("Error: Gatekeeper Reject - Terminal Excluded\n");
         break;
      case T_H225GatekeeperRejectReason_invalidRevision:
         OOTRACEERR1("Error: Gatekeeper Reject - Invalid Revision\n");
         break;
      case T_H225GatekeeperRejectReason_undefinedReason:
         OOTRACEERR1("Error: Gatekeeper Reject - Undefined Reason\n");
         break;
      case T_H225GatekeeperRejectReason_securityDenial:
         OOTRACEERR1("Error: Gatekeeper Reject - Security Denial\n");
         break;
      case T_H225GatekeeperRejectReason_genericDataReason:
         OOTRACEERR1("Error: Gatekeeper Reject - Generic Data Reason\n");
         break;
      case T_H225GatekeeperRejectReason_neededFeatureNotSupported:
         OOTRACEERR1("Error: Gatekeeper Reject - Needed Feature Not "
                     "Supported\n");
         break;
      case T_H225GatekeeperRejectReason_securityError:
         OOTRACEERR1("Error:Gatekeeper Reject - Security Error\n");
         break;
      default:
         OOTRACEERR1("Error: Gatekeeper Reject - Invalid reason\n");
      }
      return OO_OK;
   }
   OOTRACEDBGB1("Gatekeeper Reject response received for multicast GRQ "
                "request\n");
   return OO_OK;

}

int ooGkClientHandleGatekeeperConfirm
   (ooGkClient *pGkClient, H225GatekeeperConfirm *pGatekeeperConfirm)
{
   int iRet=0;
   unsigned int x=0;
   DListNode *pNode = NULL;
   OOTimer *pTimer = NULL;
   H225TransportAddress_ipAddress *pRasAddress;

   if(pGkClient->discoveryComplete)
   {
      OOTRACEDBGB1("Ignoring GKConfirm as Gatekeeper has been discovered\n");
      return OO_OK;
   }

   if(pGatekeeperConfirm->m.gatekeeperIdentifierPresent) 
   {
      pGkClient->gkId.nchars = pGatekeeperConfirm->gatekeeperIdentifier.nchars;
      pGkClient->gkId.data = (ASN116BITCHAR*)memAlloc(&pGkClient->ctxt,
                              sizeof(ASN116BITCHAR)*pGkClient->gkId.nchars);
      if(!pGkClient->gkId.data)
      {
         OOTRACEERR1("Error:Failed to allocate memory for GK ID data\n");
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }

      memcpy(pGkClient->gkId.data, 
             pGatekeeperConfirm->gatekeeperIdentifier.data,
             sizeof(ASN116BITCHAR)* pGkClient->gkId.nchars);
   }
   else{
      OOTRACEINFO1("ERROR:No Gatekeeper ID present in received GKConfirmed "
                  "message\n");
      pGkClient->gkId.nchars = 0;
   }
   
   /* Extract Gatekeeper's RAS address */
   if(pGatekeeperConfirm->rasAddress.t != T_H225TransportAddress_ipAddress)
   {
      OOTRACEERR1("ERROR:Unsupported RAS address type in received Gk Confirm"
                  " message.\n");
      pGkClient->state = GkClientGkErr;
      return OO_FAILED;
   }
   pRasAddress =   pGatekeeperConfirm->rasAddress.u.ipAddress;
   sprintf(pGkClient->gkRasIP, "%d.%d.%d.%d", pRasAddress->ip.data[0],
                                              pRasAddress->ip.data[1],
                                              pRasAddress->ip.data[2], 
                                              pRasAddress->ip.data[3]);
   pGkClient->gkRasPort = pRasAddress->port;
   
   pGkClient->discoveryComplete = TRUE;
   pGkClient->state = GkClientDiscovered;
   OOTRACEINFO1("Gatekeeper Confirmed\n");


   /* Delete the corresponding GRQ timer */
   for(x=0; x<pGkClient->timerList.count; x++)
   {
      pNode =  dListFindByIndex(&pGkClient->timerList, x);
      pTimer = (OOTimer*)pNode->data;
      if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_GRQ_TIMER)
      {
         memFreePtr(&pGkClient->ctxt, pTimer->cbData);
         ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, pTimer);
         OOTRACEDBGA1("Deleted GRQ Timer.\n");
         break;
      }
   }

   iRet = ooGkClientSendRRQ(pGkClient, FALSE);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error:Failed to send initial RRQ\n");
      return OO_FAILED;
   }
   return OO_OK;
}

/**
 * Send RRQ.
 */

int ooGkClientSendRRQ(ooGkClient *pGkClient, ASN1BOOL keepAlive)
{
   int iRet;
   H225RasMessage *pRasMsg=NULL;
   H225RegistrationRequest *pRegReq=NULL;
   OOCTXT *pctxt=NULL;
   H225TransportAddress *pTransportAddress=NULL;
   H225TransportAddress_ipAddress *pIpAddress=NULL;
   ooGkClientTimerCb *cbData =NULL;
   H225SupportedProtocols *pProtocol = NULL;
   H225VoiceCaps *pVoiceCaps = NULL;

   ast_mutex_lock(&pGkClient->Lock);

   pctxt = &pGkClient->msgCtxt;

   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   if(!pRasMsg)
   {
      OOTRACEERR1("Error: Memory allocation for RRQ RAS message failed\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   pRegReq = (H225RegistrationRequest*)memAlloc(pctxt, 
                                          sizeof(H225RegistrationRequest));
   if(!pRegReq)
   {
      OOTRACEERR1("Error:Memory allocation for RRQ failed\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memset(pRegReq, 0, sizeof(H225RegistrationRequest));
   pRasMsg->t = T_H225RasMessage_registrationRequest;
   pRasMsg->u.registrationRequest = pRegReq;
   
   pRegReq->protocolIdentifier = gProtocolID;
   pRegReq->m.nonStandardDataPresent=0;
   /* Populate CallSignal Address List*/
   pTransportAddress = (H225TransportAddress*) memAlloc(pctxt, 
                                                 sizeof(H225TransportAddress));
   pIpAddress = (H225TransportAddress_ipAddress*) memAlloc(pctxt,
                                       sizeof(H225TransportAddress_ipAddress));
   if(!pTransportAddress || !pIpAddress)
   {
      OOTRACEERR1("Error:Failed to allocate memory for signalling address of "
                  "RRQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pTransportAddress->t = T_H225TransportAddress_ipAddress;
   pTransportAddress->u.ipAddress = pIpAddress;
   inet_pton(AF_INET, pGkClient->localRASIP, pIpAddress->ip.data);
   pIpAddress->ip.numocts = 4;
   pIpAddress->port = gH323ep.listenPort;
   
   dListInit(&pRegReq->callSignalAddress);
   dListAppend(pctxt, &pRegReq->callSignalAddress, 
                                       (void*)pTransportAddress);

   /* Populate RAS Address List*/
   pTransportAddress = NULL;
   pIpAddress = NULL;
   pTransportAddress = (H225TransportAddress*) memAlloc(pctxt, 
                                                 sizeof(H225TransportAddress));
   pIpAddress = (H225TransportAddress_ipAddress*) memAlloc(pctxt,
                                       sizeof(H225TransportAddress_ipAddress));
   if(!pTransportAddress || !pIpAddress)
   {
      OOTRACEERR1("Error:Failed to allocate memory for RAS address of "
                  "RRQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   pTransportAddress->t = T_H225TransportAddress_ipAddress;
   pTransportAddress->u.ipAddress = pIpAddress;
   
   inet_pton(AF_INET, pGkClient->localRASIP, pIpAddress->ip.data);

   pIpAddress->ip.numocts = 4;
   pIpAddress->port = pGkClient->localRASPort;
   
   dListInit(&pRegReq->rasAddress);
   dListAppend(pctxt, &pRegReq->rasAddress, 
                                       (void*)pTransportAddress);
   
   /* Pose as gateway or terminal as per config */
   if(gH323ep.isGateway)
      pRegReq->terminalType.m.gatewayPresent = TRUE;
   else
      pRegReq->terminalType.m.terminalPresent = TRUE;

   pRegReq->terminalType.m.vendorPresent=TRUE;
   ooGkClientFillVendor(pGkClient, &pRegReq->terminalType.vendor );

   if (gH323ep.isGateway) {
      pRegReq->terminalType.gateway.m.protocolPresent = TRUE;
      pProtocol = (H225SupportedProtocols*) memAlloc(pctxt,
                                       sizeof(H225SupportedProtocols));
      pVoiceCaps = (H225VoiceCaps*) memAlloc(pctxt, sizeof(H225VoiceCaps));
      if(!pProtocol || !pVoiceCaps) {
      	OOTRACEERR1("Error:Failed to allocate memory for protocol info of "
                  "RRQ message\n");
      	memReset(pctxt);
      	pGkClient->state = GkClientFailed;
      	ast_mutex_unlock(&pGkClient->Lock);
      	return OO_FAILED;
      }

      memset(pVoiceCaps, 0, sizeof(H225VoiceCaps));
      memset(pProtocol, 0, sizeof(H225SupportedProtocols));

      pVoiceCaps->m.supportedPrefixesPresent = TRUE;
      ooPopulatePrefixList(pctxt, gH323ep.aliases, &pVoiceCaps->supportedPrefixes);

      pProtocol->t = T_H225SupportedProtocols_voice;
      pProtocol->u.voice = pVoiceCaps;
   
      dListInit(&pRegReq->terminalType.gateway.protocol);
      dListAppend(pctxt, &pRegReq->terminalType.gateway.protocol, 
                                       (void*)pProtocol);

   }

   pRegReq->m.terminalAliasPresent=TRUE;
   if(OO_OK != ooPopulateAliasList(pctxt, gH323ep.aliases, 
                                     &pRegReq->terminalAlias, 0)) {
     OOTRACEERR1("Error filling alias for RRQ\n");
     memReset(pctxt); 
     pGkClient->state = GkClientFailed;
     ast_mutex_unlock(&pGkClient->Lock);
     return OO_FAILED;
   }
   
   if (pGkClient->gkId.nchars) {
    pRegReq->m.gatekeeperIdentifierPresent=TRUE;
    pRegReq->gatekeeperIdentifier.nchars = pGkClient->gkId.nchars;
    pRegReq->gatekeeperIdentifier.data = (ASN116BITCHAR*)memAlloc
                         (pctxt, pGkClient->gkId.nchars*sizeof(ASN116BITCHAR));
    if(!pRegReq->gatekeeperIdentifier.data)
    {
      OOTRACEERR1("Error: Failed to allocate memory for GKIdentifier in RRQ "
                   "message.\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
    }
    memcpy(pRegReq->gatekeeperIdentifier.data, pGkClient->gkId.data, 
                                pGkClient->gkId.nchars*sizeof(ASN116BITCHAR));
   }
   
   ooGkClientFillVendor(pGkClient, &pRegReq->endpointVendor);
   
   pRegReq->m.willSupplyUUIEsPresent=TRUE;
   pRegReq->willSupplyUUIEs=FALSE;

   pRegReq->requestSeqNum = pGkClient->requestSeqNum++;
   if(!pRegReq->requestSeqNum)
      pRegReq->requestSeqNum = pGkClient->requestSeqNum++;
   
   pRegReq->discoveryComplete= pGkClient->discoveryComplete;
   pRegReq->m.keepAlivePresent=TRUE;
   pRegReq->keepAlive= keepAlive;

   /*
    * Cisco Gatekeeper re-registration fix.  Thanks to Mike Tubby (mike@tubby.org) 28feb2007
    * Without this patch initial registration works, but re-registration fails!
    *
    * For light-weight re-registration, keepalive is set true
    * GK needs rasAddress, keepAlive, endpointIdentifier, gatekeeperIdentifier,
    * tokens, and timeToLive
    * GK will ignore all other params if KeepAlive is set.
    *
    */
   if(keepAlive) {
      /* KeepAlive, re-registration message...
         allocate storage for endpoint-identifier, and populate it from what the
         GK told us from the previous RCF. Only allocate on the first pass thru here */
      pRegReq->endpointIdentifier.data = 
           (ASN116BITCHAR*)memAlloc(pctxt, pGkClient->gkId.nchars*sizeof(ASN116BITCHAR));
      if (pRegReq->endpointIdentifier.data) {
         pRegReq->endpointIdentifier.nchars = pGkClient->endpointId.nchars;
         pRegReq->m.endpointIdentifierPresent = TRUE;
         memcpy(pRegReq->endpointIdentifier.data, pGkClient->endpointId.data, pGkClient->endpointId.nchars*sizeof(ASN116BITCHAR));
         OOTRACEINFO1("Sending RRQ for re-registration (with EndpointID)\n");
      }
      else {
         OOTRACEERR1("Error: Failed to allocate memory for EndpointIdentifier in RRQ \n");
         memReset(pctxt);
         pGkClient->state = GkClientFailed;
	 ast_mutex_unlock(&pGkClient->Lock);
         return OO_FAILED;
      }
   }

   pRegReq->m.timeToLivePresent = TRUE;
   pRegReq->timeToLive = pGkClient->regTimeout;

   iRet = ooGkClientSendMsg(pGkClient, pRasMsg);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error: Failed to send RRQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   OOTRACEINFO1("Sent RRQ message \n");
   /* Start RRQ Timer */
   cbData = (ooGkClientTimerCb*) memAlloc
                            (&pGkClient->ctxt, sizeof(ooGkClientTimerCb));
   if(!cbData)
   {
      OOTRACEERR1("Error:Failed to allocate memory to RRQ timer callback\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   cbData->timerType = OO_RRQ_TIMER;
   cbData->pGkClient = pGkClient;
   if(!ooTimerCreate(&pGkClient->ctxt, &pGkClient->timerList, 
                     &ooGkClientRRQTimerExpired, pGkClient->rrqTimeout, 
                     cbData, FALSE))      
   {
      OOTRACEERR1("Error:Unable to create GRQ timer.\n ");
      memFreePtr(&pGkClient->ctxt, cbData);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   
   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}



/**
 * Manage incoming RCF message.
 */

int ooGkClientHandleRegistrationConfirm
   (ooGkClient *pGkClient, H225RegistrationConfirm *pRegistrationConfirm)
{
   int i=0;
   unsigned int x=0;
   OOTimer *pTimer = NULL;
   DListNode *pNode = NULL;
   H225TransportAddress *pCallSigAddr=NULL;
   ooGkClientTimerCb *cbData;
   ASN1UINT regTTL=0;
   /* Extract Endpoint Id */
   pGkClient->endpointId.nchars = 
                              pRegistrationConfirm->endpointIdentifier.nchars;
   pGkClient->endpointId.data = (ASN116BITCHAR*)memAlloc(&pGkClient->ctxt,
                          sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);
   if(!pGkClient->endpointId.data)
   {
      OOTRACEERR1("Error:Failed to allocate memory for endpoint Id.\n");
      pGkClient->state = GkClientFailed;
      return OO_FAILED;
   }
   
   memcpy(pGkClient->endpointId.data, 
          pRegistrationConfirm->endpointIdentifier.data,
          sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);

   /* Extract GK Identifier */
   
   if(pRegistrationConfirm->m.gatekeeperIdentifierPresent && pGkClient->gkId.nchars == 0)
   {
      pGkClient->gkId.nchars = pRegistrationConfirm->gatekeeperIdentifier.nchars;
      pGkClient->gkId.data = (ASN116BITCHAR*)memAlloc(&pGkClient->ctxt,
                              sizeof(ASN116BITCHAR)*pGkClient->gkId.nchars);
      if(!pGkClient->gkId.data)
      {
         OOTRACEERR1("Error:Failed to allocate memory for GK ID data\n");
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }

      memcpy(pGkClient->gkId.data, 
             pRegistrationConfirm->gatekeeperIdentifier.data,
             sizeof(ASN116BITCHAR)* pGkClient->gkId.nchars);
   }

   /* Extract CallSignalling Address */
   for(i=0; i<(int)pRegistrationConfirm->callSignalAddress.count; i++)
   {
      pNode = dListFindByIndex(&pRegistrationConfirm->callSignalAddress, i);
      if(!pNode)
      {
         OOTRACEERR1("Error:Invalid Registration confirmed message\n");
         OOTRACEINFO1("Ignoring RCF, will retransmit RRQ after timeout\n");
         return OO_FAILED; 
      }
      pCallSigAddr = (H225TransportAddress*)pNode->data;
      if(pCallSigAddr->t != T_H225TransportAddress_ipAddress)
         continue;
      sprintf(pGkClient->gkCallSignallingIP, "%d.%d.%d.%d", 
                            pCallSigAddr->u.ipAddress->ip.data[0],
                            pCallSigAddr->u.ipAddress->ip.data[1],
                            pCallSigAddr->u.ipAddress->ip.data[2],
                            pCallSigAddr->u.ipAddress->ip.data[3]);
      pGkClient->gkCallSignallingPort = pCallSigAddr->u.ipAddress->port;
   }
   
   /* Update list of registered aliases*/
   if(pRegistrationConfirm->m.terminalAliasPresent)
   {
      ooGkClientUpdateRegisteredAliases(pGkClient, 
                                   &pRegistrationConfirm->terminalAlias, TRUE);
   }
   else{/* Everything registered*/
     ooGkClientUpdateRegisteredAliases(pGkClient, NULL, TRUE);
   }

   /* Is keepAlive supported */
   if(pRegistrationConfirm->m.timeToLivePresent)
   {
      pGkClient->regTimeout = pRegistrationConfirm->timeToLive;
      OOTRACEINFO2("Gatekeeper supports KeepAlive, Registration TTL is %d\n",
                    pRegistrationConfirm->timeToLive);

      if(pGkClient->regTimeout > DEFAULT_TTL_OFFSET)
         regTTL = pGkClient->regTimeout - DEFAULT_TTL_OFFSET;
      else {
         regTTL = pGkClient->regTimeout - 1; /* -1 due to some ops expire us few earlier */
	 if (regTTL <= 0)
		regTTL = 1;
      }

      cbData = (ooGkClientTimerCb*) memAlloc
                                (&pGkClient->ctxt, sizeof(ooGkClientTimerCb));
      if(!cbData)
      {
         OOTRACEERR1("Error:Failed to allocate memory for Regisration timer."
                     "\n");
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }
      cbData->timerType = OO_REG_TIMER;
      cbData->pGkClient = pGkClient;
      if(!ooTimerCreate(&pGkClient->ctxt, &pGkClient->timerList, 
                     &ooGkClientREGTimerExpired, regTTL, 
                     cbData, FALSE))
      {
         OOTRACEERR1("Error:Unable to create REG timer.\n ");
         memFreePtr(&pGkClient->ctxt, cbData);
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }    
      
   }
   else{
      pGkClient->regTimeout = 0;
      OOTRACEINFO1("Gatekeeper does not support KeepAlive.\n");
   }
   /* Extract Pre-Granted ARQ */
   if(pRegistrationConfirm->m.preGrantedARQPresent)
   {
      memcpy(&pGkClient->gkInfo.preGrantedARQ, 
             &pRegistrationConfirm->preGrantedARQ,
             sizeof(H225RegistrationConfirm_preGrantedARQ));
   }


   /* First delete the corresponding RRQ timer */
   pNode = NULL;
   for(x=0; x<pGkClient->timerList.count; x++)
   {
      pNode =  dListFindByIndex(&pGkClient->timerList, x);
      pTimer = (OOTimer*)pNode->data;
      if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_RRQ_TIMER)
      {
         memFreePtr(&pGkClient->ctxt, pTimer->cbData);
         ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, pTimer);
         OOTRACEDBGA1("Deleted RRQ Timer.\n");
      }
   }
   pGkClient->state = GkClientRegistered;
   if(pGkClient->callbacks.onReceivedRegistrationConfirm)
      pGkClient->callbacks.onReceivedRegistrationConfirm(pRegistrationConfirm,
                                                              gH323ep.aliases);
   return OO_OK;
}

int ooGkClientHandleRegistrationReject
   (ooGkClient *pGkClient, H225RegistrationReject *pRegistrationReject)
{
   int iRet=0;
   unsigned int x=0;
   DListNode *pNode = NULL;
   OOTimer *pTimer = NULL;
   /* First delete the corresponding RRQ timer */
   for(x=0; x<pGkClient->timerList.count; x++)
   {
      pNode =  dListFindByIndex(&pGkClient->timerList, x);
      pTimer = (OOTimer*)pNode->data;
      if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_RRQ_TIMER)
      {
         memFreePtr(&pGkClient->ctxt, pTimer->cbData);
         ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, pTimer);
         OOTRACEDBGA1("Deleted RRQ Timer.\n");
         break;
      }
   }

   switch(pRegistrationReject->rejectReason.t)
   {
   case T_H225RegistrationRejectReason_discoveryRequired:
      OOTRACEINFO1("RRQ Rejected - Discovery Required\n");

      pGkClient->discoveryComplete = FALSE;
      pGkClient->state = GkClientIdle;
      pGkClient->rrqRetries = 0;
      pGkClient->grqRetries = 0;
      if(OO_OK != ooGkClientSendGRQ(pGkClient))
      {
         OOTRACEERR1("Error:Failed to send GRQ message\n");
         return OO_FAILED;
      }
      return OO_OK;
   case T_H225RegistrationRejectReason_invalidRevision:
      OOTRACEERR1("RRQ Rejected - Invalid Revision\n");
      break;
   case T_H225RegistrationRejectReason_invalidCallSignalAddress:
      OOTRACEERR1("RRQ Rejected - Invalid CallSignalAddress\n");
      break;
   case T_H225RegistrationRejectReason_invalidRASAddress:
      OOTRACEERR1("RRQ Rejected - Invalid RAS Address\n");
      break;
   case T_H225RegistrationRejectReason_duplicateAlias:
      OOTRACEERR1("RRQ Rejected - Duplicate Alias\n");
      break;
   case T_H225RegistrationRejectReason_invalidTerminalType:
      OOTRACEERR1("RRQ Rejected - Invalid Terminal Type\n");
      break;
   case T_H225RegistrationRejectReason_undefinedReason:
      OOTRACEERR1("RRQ Rejected - Undefined Reason\n");
      break;
   case T_H225RegistrationRejectReason_transportNotSupported:
      OOTRACEERR1("RRQ Rejected - Transport Not supported\n");
      break;
   case T_H225RegistrationRejectReason_transportQOSNotSupported:
      OOTRACEERR1("RRQ Rejected - Transport QOS Not Supported\n");
      break;
   case T_H225RegistrationRejectReason_resourceUnavailable:
      OOTRACEERR1("RRQ Rejected - Resource Unavailable\n");
      break;
   case T_H225RegistrationRejectReason_invalidAlias:
      OOTRACEERR1("RRQ Rejected - Invalid Alias\n");
      break;
   case T_H225RegistrationRejectReason_securityDenial:
      OOTRACEERR1("RRQ Rejected - Security Denial\n");
      break;
   case T_H225RegistrationRejectReason_fullRegistrationRequired:
      OOTRACEINFO1("RRQ Rejected - Full Registration Required\n");
      pGkClient->state = GkClientDiscovered;
      pGkClient->rrqRetries = 0;
      iRet = ooGkClientSendRRQ(pGkClient, 0); /* No keepAlive */
      if(iRet != OO_OK){
         OOTRACEERR1("\nError: Full Registration transmission failed\n");
         return OO_FAILED;
      }
      return OO_OK;
   case T_H225RegistrationRejectReason_additiveRegistrationNotSupported:
      OOTRACEERR1("RRQ Rejected - Additive Registration Not Supported\n");
      break;
   case T_H225RegistrationRejectReason_invalidTerminalAliases:
      OOTRACEERR1("RRQ Rejected - Invalid Terminal Aliases\n");
      break;
   case T_H225RegistrationRejectReason_genericDataReason:
      OOTRACEERR1("RRQ Rejected - Generic Data Reason\n");
      break;
   case T_H225RegistrationRejectReason_neededFeatureNotSupported:
      OOTRACEERR1("RRQ Rejected - Needed Feature Not Supported\n");
      break;
   case T_H225RegistrationRejectReason_securityError:
      OOTRACEERR1("RRQ Rejected - Security Error\n");
      break;
   default:
      OOTRACEINFO1("RRQ Rejected - Invalid Reason\n");
   }
   pGkClient->state = GkClientGkErr;
   return OO_OK;
}


int ooGkClientSendURQ(ooGkClient *pGkClient, ooAliases *aliases)
{
   int iRet;
   H225RasMessage *pRasMsg=NULL;
   H225UnregistrationRequest *pUnregReq=NULL;
   OOCTXT *pctxt=NULL;
   H225TransportAddress *pTransportAddress=NULL;
   H225TransportAddress_ipAddress *pIpAddress=NULL;

   ast_mutex_lock(&pGkClient->Lock);
   pctxt = &pGkClient->msgCtxt;

   OOTRACEDBGA1("Building Unregistration Request message\n");

   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   if(!pRasMsg)
   {
      OOTRACEERR1("Error: Memory allocation for URQ RAS message failed\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   pUnregReq = (H225UnregistrationRequest*)memAlloc(pctxt, 
                                          sizeof(H225UnregistrationRequest));
   if(!pUnregReq)
   {
      OOTRACEERR1("Error:Memory allocation for URQ failed\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memset(pUnregReq, 0, sizeof(H225UnregistrationRequest));
   pRasMsg->t = T_H225RasMessage_unregistrationRequest;
   pRasMsg->u.unregistrationRequest = pUnregReq;

   pUnregReq->requestSeqNum = pGkClient->requestSeqNum++;
   if(!pUnregReq->requestSeqNum)
      pUnregReq->requestSeqNum = pGkClient->requestSeqNum++;

   

 /* Populate CallSignal Address List*/
   pTransportAddress = (H225TransportAddress*) memAlloc(pctxt, 
                                                 sizeof(H225TransportAddress));
   pIpAddress = (H225TransportAddress_ipAddress*) memAlloc(pctxt,
                                       sizeof(H225TransportAddress_ipAddress));
   if(!pTransportAddress || !pIpAddress)
   {
      OOTRACEERR1("Error:Failed to allocate memory for signalling address of "
                  "RRQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pTransportAddress->t = T_H225TransportAddress_ipAddress;
   pTransportAddress->u.ipAddress = pIpAddress;
   inet_pton(AF_INET, pGkClient->localRASIP, pIpAddress->ip.data);
   pIpAddress->ip.numocts = 4;
   pIpAddress->port = gH323ep.listenPort;
   
   dListInit(&pUnregReq->callSignalAddress);
   dListAppend(pctxt, &pUnregReq->callSignalAddress, 
                                       (void*)pTransportAddress);

   /* Populate Endpoint Identifier */
   pUnregReq->m.endpointIdentifierPresent = TRUE;
   pUnregReq->endpointIdentifier.nchars = pGkClient->endpointId.nchars;
   pUnregReq->endpointIdentifier.data = (ASN116BITCHAR*)memAlloc(pctxt,
                           sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);
   if(!pUnregReq->endpointIdentifier.data)
   {
      OOTRACEERR1("Error: Failed to allocate memory for EndPoint Id in URQ "
                  "message.\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memcpy((void*)pUnregReq->endpointIdentifier.data, 
          (void*)pGkClient->endpointId.data,
          sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);

   /* Populate gatekeeper identifier */
   if (pGkClient->gkId.nchars) {
    pUnregReq->m.gatekeeperIdentifierPresent = TRUE;
    pUnregReq->gatekeeperIdentifier.nchars = pGkClient->gkId.nchars;
    pUnregReq->gatekeeperIdentifier.data = (ASN116BITCHAR*)memAlloc(pctxt,
                                 sizeof(ASN116BITCHAR)*pGkClient->gkId.nchars);
    if(!pUnregReq->gatekeeperIdentifier.data)
    {
      OOTRACEERR1("Error:Failed to allocate memory for GKID of URQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
    }
    memcpy((void*)pUnregReq->gatekeeperIdentifier.data, 
          (void*)pGkClient->gkId.data, 
          sizeof(ASN116BITCHAR)*pGkClient->gkId.nchars);   
   }

   /* Check whether specific aliases are to be unregistered*/
   if(aliases)
   {
      pUnregReq->m.endpointAliasPresent = TRUE;
      ooPopulateAliasList(pctxt, aliases, &pUnregReq->endpointAlias, 0);
   }

  
   iRet = ooGkClientSendMsg(pGkClient, pRasMsg);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error:Failed to send UnregistrationRequest message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pGkClient->state = GkClientUnregistered;
   OOTRACEINFO1("Unregistration Request message sent.\n");

   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}               



int ooGkClientHandleUnregistrationRequest
   (ooGkClient *pGkClient, H225UnregistrationRequest * punregistrationRequest)
{
   int iRet=0, x;
   OOTimer *pTimer = NULL;
   DListNode *pNode = NULL;
 
   /* Lets first send unregistration confirm message back to gatekeeper*/
   ooGkClientSendUnregistrationConfirm(pGkClient, 
                                      punregistrationRequest->requestSeqNum);

   if(punregistrationRequest->m.endpointAliasPresent)
   {
      OOTRACEINFO1("Gatekeeper requested a list of aliases be unregistered\n");
      ooGkClientUpdateRegisteredAliases(pGkClient, 
                                &punregistrationRequest->endpointAlias, FALSE);
   }
   else{

      OOTRACEINFO1("Gatekeeper requested a all aliases to be unregistered\n");
      ooGkClientUpdateRegisteredAliases(pGkClient, NULL, FALSE);
      /* Send a fresh Registration request and if that fails, go back to
         Gatekeeper discovery.
      */
      OOTRACEINFO1("Sending fresh RRQ - as unregistration request received\n");
      pGkClient->rrqRetries = 0;
      pGkClient->state = GkClientDiscovered;


      /* delete the corresponding RRQ & REG timers */
	pNode = NULL;
	for(x=0; x<pGkClient->timerList.count; x++) {
		pNode =  dListFindByIndex(&pGkClient->timerList, x);
		pTimer = (OOTimer*)pNode->data;
		if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_RRQ_TIMER) {
         		memFreePtr(&pGkClient->ctxt, pTimer->cbData);
         		ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, pTimer);
         		OOTRACEDBGA1("Deleted RRQ Timer.\n");
      		}
		if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_REG_TIMER) {
         		memFreePtr(&pGkClient->ctxt, pTimer->cbData);
         		ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, pTimer);
         		OOTRACEDBGA1("Deleted REG Timer.\n");
      		}
 	}

      iRet = ooGkClientSendRRQ(pGkClient, 0); 
      if(iRet != OO_OK)
      {
         OOTRACEERR1("Error: Failed to send RRQ message\n");
         return OO_FAILED;
      }
   }


   if(pGkClient->callbacks.onReceivedUnregistrationRequest)
      pGkClient->callbacks.onReceivedUnregistrationRequest(
                                      punregistrationRequest, gH323ep.aliases);
   return OO_OK;
}

int ooGkClientSendUnregistrationConfirm(ooGkClient *pGkClient, unsigned reqNo)
{
   int iRet = OO_OK;
   OOCTXT *pctxt = &pGkClient->msgCtxt;   
   H225RasMessage *pRasMsg=NULL;
   H225UnregistrationConfirm *pUCF=NULL;

   ast_mutex_lock(&pGkClient->Lock);

   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   pUCF = (H225UnregistrationConfirm*)memAlloc(pctxt, 
                                           sizeof(H225UnregistrationConfirm));
   if(!pRasMsg || !pUCF)
   {
      OOTRACEERR1("Error: Memory allocation for UCF RAS message failed\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pRasMsg->t = T_H225RasMessage_unregistrationConfirm;
   pRasMsg->u.unregistrationConfirm = pUCF;
   memset(pUCF, 0, sizeof(H225UnregistrationConfirm));
   
   pUCF->requestSeqNum = reqNo;
   
   iRet = ooGkClientSendMsg(pGkClient, pRasMsg);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error:Failed to send UnregistrationConfirm message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   OOTRACEINFO1("Unregistration Confirm message sent for \n");
   memReset(pctxt);

   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}




int ooGkClientSendAdmissionRequest
   (ooGkClient *pGkClient, OOH323CallData *call, ASN1BOOL retransmit)
{
   int iRet = 0;
   unsigned int x;
   DListNode *pNode;
   ooGkClientTimerCb *cbData=NULL;
   H225RasMessage *pRasMsg=NULL;
   OOCTXT* pctxt;
   H225AdmissionRequest *pAdmReq=NULL;
   H225TransportAddress_ipAddress *pIpAddressLocal =NULL, *pIpAddressRemote=NULL;
   ooAliases *destAliases = NULL, *srcAliases=NULL;
   RasCallAdmissionInfo *pCallAdmInfo=NULL;
   pctxt = &pGkClient->msgCtxt;

   ast_mutex_lock(&pGkClient->Lock);

   OOTRACEDBGA3("Building Admission Request for call (%s, %s)\n", 
                 call->callType, call->callToken);   
   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   if(!pRasMsg)
   {
      OOTRACEERR3("Error:Memory - ooGkClientSendAdmissionRequest - "
                  "pRasMsg(%s, %s)\n", call->callType, call->callToken);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pRasMsg->t = T_H225RasMessage_admissionRequest;
   pAdmReq = (H225AdmissionRequest*) memAlloc(pctxt, 
                                                 sizeof(H225AdmissionRequest));
   if(!pAdmReq)
   {
      OOTRACEERR3("Error:Memory - ooGkClientSendAdmissionRequest - "
                  "pAdmReq(%s, %s)\n", call->callType, call->callToken);
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memset(pAdmReq, 0, sizeof(H225AdmissionRequest));
   pRasMsg->u.admissionRequest = pAdmReq;
   
   /* Populate call signalling addresses */
   pIpAddressLocal = (H225TransportAddress_ipAddress*)memAlloc(pctxt, 
                                     sizeof(H225TransportAddress_ipAddress));
   if(!ooUtilsIsStrEmpty(call->remoteIP))
      pIpAddressRemote = (H225TransportAddress_ipAddress*)memAlloc(pctxt, 
                                      sizeof(H225TransportAddress_ipAddress));

   if(!pIpAddressLocal || (!ooUtilsIsStrEmpty(call->remoteIP) && (!pIpAddressRemote)))
   {
      OOTRACEERR1("Error:Failed to allocate memory for Call Signalling "
                  "Addresses of ARQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   inet_pton(AF_INET, pGkClient->localRASIP, pIpAddressLocal->ip.data);

   pIpAddressLocal->ip.numocts = 4;
   pIpAddressLocal->port = gH323ep.listenPort;

   if(!ooUtilsIsStrEmpty(call->remoteIP))
   {
      inet_pton(AF_INET, call->remoteIP, pIpAddressRemote->ip.data);
      pIpAddressRemote->ip.numocts = 4;
      pIpAddressRemote->port = call->remotePort;
   }

   if(!strcmp(call->callType, "incoming"))
   {
      pAdmReq->m.destCallSignalAddressPresent = TRUE;
      pAdmReq->destCallSignalAddress.t = T_H225TransportAddress_ipAddress;
      pAdmReq->destCallSignalAddress.u.ipAddress = pIpAddressLocal;
      if(!ooUtilsIsStrEmpty(call->remoteIP))
      {
         pAdmReq->m.srcCallSignalAddressPresent = TRUE;
         pAdmReq->srcCallSignalAddress.t = T_H225TransportAddress_ipAddress;
         pAdmReq->srcCallSignalAddress.u.ipAddress = pIpAddressRemote;
      }
   }
   else {
      pAdmReq->m.srcCallSignalAddressPresent = TRUE;
      pAdmReq->srcCallSignalAddress.t = T_H225TransportAddress_ipAddress;
      pAdmReq->srcCallSignalAddress.u.ipAddress = pIpAddressLocal;
      if(!ooUtilsIsStrEmpty(call->remoteIP))
      {
         pAdmReq->m.destCallSignalAddressPresent = TRUE;
         pAdmReq->destCallSignalAddress.t = T_H225TransportAddress_ipAddress;
         pAdmReq->destCallSignalAddress.u.ipAddress = pIpAddressRemote;
      }
   }

   /* Populate seq number */
   pAdmReq->requestSeqNum = pGkClient->requestSeqNum++;
   if(!pAdmReq->requestSeqNum)
      pAdmReq->requestSeqNum = pGkClient->requestSeqNum++;

   /* Populate call type - For now only PointToPoint supported*/
   pAdmReq->callType.t = T_H225CallType_pointToPoint;
   
   /* Add call model to message*/
   pAdmReq->m.callModelPresent = 1;
   if(OO_TESTFLAG(call->flags, OO_M_GKROUTED))
      pAdmReq->callModel.t = T_H225CallModel_gatekeeperRouted;
   else
      pAdmReq->callModel.t = T_H225CallModel_direct;

   /* Populate Endpoint Identifier */
   pAdmReq->endpointIdentifier.nchars = pGkClient->endpointId.nchars;
   pAdmReq->endpointIdentifier.data = (ASN116BITCHAR*)memAlloc(pctxt,
                           sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);
   if(!pAdmReq->endpointIdentifier.data)
   {
      OOTRACEERR3("Error:Memory -  ooGkClientSendAdmissionRequest - "
                  "endpointIdentifier.data(%s, %s)\n", call->callType, 
                  call->callToken);
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memcpy((void*)pAdmReq->endpointIdentifier.data, 
          (void*)pGkClient->endpointId.data,
          sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);

   /* Get Destination And source aliases for call -  */
   if(!strcmp(call->callType, "incoming"))
   {
      if(call->ourAliases) 
         destAliases = call->ourAliases;
      else
         destAliases = gH323ep.aliases; 

      srcAliases = call->remoteAliases;
   }
   else {
      if(call->ourAliases) 
         srcAliases = call->ourAliases;
      else
         srcAliases = gH323ep.aliases; 

      destAliases = call->remoteAliases;
   }

   /* Populate destination info */
   if(destAliases)
   {
      pAdmReq->m.destinationInfoPresent = 1;
      if(OO_OK != ooPopulateAliasList(&pGkClient->msgCtxt, destAliases,
                      &pAdmReq->destinationInfo, T_H225AliasAddress_dialedDigits))
      {
         OOTRACEERR1("Error:Failed to populate destination aliases - "
                    "ARQ message\n");
         pGkClient->state = GkClientFailed;
         memReset(pctxt);
	 ast_mutex_unlock(&pGkClient->Lock);
         return OO_FAILED;
      }
   }

   /* Populate Source Info */
   if(srcAliases)
   {
      iRet = ooPopulateAliasList(&pGkClient->msgCtxt, srcAliases,
                              &pAdmReq->srcInfo, 0);
      if(OO_OK != iRet)
      {
         OOTRACEERR1("Error:Failed to populate source aliases -ARQ message\n");
         memReset(pctxt);
         pGkClient->state = GkClientFailed;
	 ast_mutex_unlock(&pGkClient->Lock);
         return OO_FAILED;
      }
   }
   
   /* Populate bandwidth*/
   pAdmReq->bandWidth = DEFAULT_BW_REQUEST;
   /* Populate call Reference */
   pAdmReq->callReferenceValue = call->callReference;
   
   /* populate conferenceID */
   memcpy((void*)&pAdmReq->conferenceID, (void*)&call->confIdentifier,
                                         sizeof(H225ConferenceIdentifier));
   /*populate answerCall */
   if(!strcmp(call->callType, "incoming"))
      pAdmReq->answerCall = TRUE;
   else
      pAdmReq->answerCall = FALSE;

   /* Populate CanMapAlias */
   pAdmReq->m.canMapAliasPresent = TRUE;
   pAdmReq->canMapAlias = FALSE;

   /* Populate call identifier */
   pAdmReq->m.callIdentifierPresent = TRUE;
   memcpy((void*)&pAdmReq->callIdentifier, (void*)&call->callIdentifier,
                                             sizeof(H225CallIdentifier));

   /* Populate Gatekeeper Id */
   if (pGkClient->gkId.nchars) {
    pAdmReq->m.gatekeeperIdentifierPresent = TRUE;
    pAdmReq->gatekeeperIdentifier.nchars = pGkClient->gkId.nchars;
    pAdmReq->gatekeeperIdentifier.data = (ASN116BITCHAR*)memAlloc(pctxt,
                                 sizeof(ASN116BITCHAR)*pGkClient->gkId.nchars);
    if(!pAdmReq->gatekeeperIdentifier.data)
    {
      OOTRACEERR1("Error:Failed to allocate memory for GKID of ARQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
    }
    memcpy((void*)pAdmReq->gatekeeperIdentifier.data, 
          (void*)pGkClient->gkId.data, 
          sizeof(ASN116BITCHAR)*pGkClient->gkId.nchars);
   }

   pAdmReq->m.willSupplyUUIEsPresent = 1;
   pAdmReq->willSupplyUUIEs = FALSE;

   /* Create RasCallAdmissionInfo */
   if(!retransmit)
   {
      pCallAdmInfo = (RasCallAdmissionInfo*)memAlloc(&pGkClient->ctxt, 
                                                sizeof(RasCallAdmissionInfo));
      if(!pCallAdmInfo)
      {
         OOTRACEERR1("Error: Failed to allocate memory for new CallAdmission"
                  " Info entry\n");
         memReset(pctxt);
         pGkClient->state = GkClientFailed;
	 ast_mutex_unlock(&pGkClient->Lock);
         return OO_FAILED;
      } 

      pCallAdmInfo->call = call;
      pCallAdmInfo->retries = 0;
      pCallAdmInfo->requestSeqNum = pAdmReq->requestSeqNum;
      dListAppend(&pGkClient->ctxt, &pGkClient->callsPendingList,pCallAdmInfo);
   }
   else{
      for(x=0; x<pGkClient->callsPendingList.count; x++)
      {
         pNode = dListFindByIndex(&pGkClient->callsPendingList, x);
         pCallAdmInfo = (RasCallAdmissionInfo*)pNode->data;
         if(pCallAdmInfo->call->callReference == call->callReference)
         {
            pCallAdmInfo->requestSeqNum = pAdmReq->requestSeqNum;
            break;
         }
      }
   }
   
   iRet = ooGkClientSendMsg(pGkClient, pRasMsg);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error:Failed to send AdmissionRequest message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   OOTRACEINFO3("Admission Request message sent for (%s, %s)\n", 
                 call->callType, call->callToken);
   memReset(pctxt);
    
   /* Add ARQ timer */
   cbData = (ooGkClientTimerCb*) memAlloc
                               (&pGkClient->ctxt, sizeof(ooGkClientTimerCb));
   if(!cbData)
   {
      OOTRACEERR1("Error:Failed to allocate memory for Regisration timer."
                  "\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   cbData->timerType = OO_ARQ_TIMER;
   cbData->pGkClient = pGkClient;
   cbData->pAdmInfo =  pCallAdmInfo;
   if(!ooTimerCreate(&pGkClient->ctxt, &pGkClient->timerList, 
                  &ooGkClientARQTimerExpired, pGkClient->arqTimeout, 
                  cbData, FALSE))
   {
      OOTRACEERR1("Error:Unable to create ARQ timer.\n ");
      memFreePtr(&pGkClient->ctxt, cbData);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }    
   
   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}

/**
 * Manage incoming ACF message.
 */

int ooGkClientHandleAdmissionConfirm
   (ooGkClient *pGkClient, H225AdmissionConfirm *pAdmissionConfirm)
{
   RasCallAdmissionInfo* pCallAdmInfo=NULL;
   unsigned int x, y;
   DListNode *pNode, *pNode1=NULL;
   H225TransportAddress_ipAddress * ipAddress=NULL;
   OOTimer *pTimer = NULL;
   char ip[20];

   /* Search call in pending calls list */
   for(x=0 ; x<pGkClient->callsPendingList.count; x++)
   {
      pNode = dListFindByIndex(&pGkClient->callsPendingList, x);
      pCallAdmInfo = (RasCallAdmissionInfo*) pNode->data;
      if(pCallAdmInfo->requestSeqNum == pAdmissionConfirm->requestSeqNum)
      {
         OOTRACEDBGC3("Found Pending call(%s, %s)\n", 
                      pCallAdmInfo->call->callType, 
                      pCallAdmInfo->call->callToken);
         /* Populate Remote IP */
         if(pAdmissionConfirm->destCallSignalAddress.t != 
                                      T_H225TransportAddress_ipAddress)
         {
            OOTRACEERR1("Error:Destination Call Signal Address provided by"
                        "Gatekeeper is not an IPv4 address\n");
            OOTRACEINFO1("Ignoring ACF, will wait for timeout and retransmit "
                         "ARQ\n");
            return OO_FAILED;
         }
         ipAddress = pAdmissionConfirm->destCallSignalAddress.u.ipAddress;
         
         sprintf(ip, "%d.%d.%d.%d", ipAddress->ip.data[0],
                                    ipAddress->ip.data[1],
                                    ipAddress->ip.data[2],
                                    ipAddress->ip.data[3]);
         if(strcmp(ip, "0.0.0.0")) {
/* fix this when gk client will adopt to work with IPv6 */
	    pCallAdmInfo->call->versionIP = 4;
            strcpy(pCallAdmInfo->call->remoteIP, ip);
	 }
         pCallAdmInfo->call->remotePort = ipAddress->port;
         /* Update call model */
         if(pAdmissionConfirm->callModel.t == T_H225CallModel_direct)
         {
            if(OO_TESTFLAG(pCallAdmInfo->call->flags, OO_M_GKROUTED))
            {
               OOTRACEINFO3("Gatekeeper changed call model from GkRouted to "
                            "direct. (%s, %s)\n", pCallAdmInfo->call->callType,
                            pCallAdmInfo->call->callToken);
               OO_CLRFLAG(pCallAdmInfo->call->flags, OO_M_GKROUTED);
            }
         }
         
         if(pAdmissionConfirm->callModel.t == T_H225CallModel_gatekeeperRouted)
         {
            if(!OO_TESTFLAG(pCallAdmInfo->call->flags, OO_M_GKROUTED))
            {
               OOTRACEINFO3("Gatekeeper changed call model from direct to "
                            "GkRouted. (%s, %s)\n", 
                            pCallAdmInfo->call->callType,
                            pCallAdmInfo->call->callToken);
               OO_SETFLAG(pCallAdmInfo->call->flags, OO_M_GKROUTED);
            }
         }

         /* Delete ARQ timer */
         for(y=0; y<pGkClient->timerList.count; y++)
         {
            pNode1 =  dListFindByIndex(&pGkClient->timerList, y);
            pTimer = (OOTimer*)pNode1->data;
            if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_ARQ_TIMER)
            {
               if(((ooGkClientTimerCb*)pTimer->cbData)->pAdmInfo == 
                                                                 pCallAdmInfo)
               {
                  memFreePtr(&pGkClient->ctxt, pTimer->cbData);
                  ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, 
                                                                       pTimer);
                  OOTRACEDBGA1("Deleted ARQ Timer.\n");
                  break;
               }
            }
         }       
         OOTRACEINFO3("Admission Confirm message received for (%s, %s)\n", 
                       pCallAdmInfo->call->callType, 
                       pCallAdmInfo->call->callToken);

	 pCallAdmInfo->call->callState = OO_CALL_CONNECTING;
         /* ooH323CallAdmitted( pCallAdmInfo->call); */

         dListRemove(&pGkClient->callsPendingList, pNode);
         dListAppend(&pGkClient->ctxt, &pGkClient->callsAdmittedList, 
                                                        pNode->data);
         memFreePtr(&pGkClient->ctxt, pNode);
	 ast_cond_signal(&pCallAdmInfo->call->gkWait);
         return OO_OK;
         break;
      }
      else
      {
         pNode = pNode->next;
      }
   }
   OOTRACEERR1("Error: Failed to process ACF as there is no corresponding "
               "pending call\n");
   return OO_OK;
}


int ooGkClientHandleAdmissionReject
   (ooGkClient *pGkClient, H225AdmissionReject *pAdmissionReject)
{
   RasCallAdmissionInfo* pCallAdmInfo=NULL;
   unsigned int x, y;
   DListNode *pNode=NULL, *pNode1=NULL;
   OOH323CallData *call=NULL;
   OOTimer *pTimer = NULL;

   /* Search call in pending calls list */
   for(x=0 ; x<pGkClient->callsPendingList.count; x++)
   {
      pNode = dListFindByIndex(&pGkClient->callsPendingList, x);
      pCallAdmInfo = (RasCallAdmissionInfo*) pNode->data;
      if(pCallAdmInfo->requestSeqNum == pAdmissionReject->requestSeqNum)
         break;
      pNode = NULL;
      pCallAdmInfo = NULL;
   }

   if(!pCallAdmInfo)
   {
      OOTRACEWARN2("Received admission reject with request number %d can not"
                   " be matched with any pending call.\n", 
                   pAdmissionReject->requestSeqNum);
      return OO_OK;
   }
   else{
      call = pCallAdmInfo->call;
      dListRemove(&pGkClient->callsPendingList, pNode);
      memFreePtr(&pGkClient->ctxt, pCallAdmInfo);
      memFreePtr(&pGkClient->ctxt, pNode);
   }

   /* Delete ARQ timer */
   for(y=0; y<pGkClient->timerList.count; y++)
   {
     pNode1 =  dListFindByIndex(&pGkClient->timerList, y);
     pTimer = (OOTimer*)pNode1->data;
     if(((ooGkClientTimerCb*)pTimer->cbData)->timerType & OO_ARQ_TIMER)
     {
               if(((ooGkClientTimerCb*)pTimer->cbData)->pAdmInfo == 
                                                                 pCallAdmInfo)
               {
                  memFreePtr(&pGkClient->ctxt, pTimer->cbData);
                  ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, 
                                                                       pTimer);
                  OOTRACEDBGA1("Deleted ARQ Timer.\n");
                  break;
               }
     }
   }       
   OOTRACEINFO4("Admission Reject message received with reason code %d for "
                "(%s, %s)\n", pAdmissionReject->rejectReason.t, call->callType,
                 call->callToken);
   
   call->callState = OO_CALL_CLEARED;

   switch(pAdmissionReject->rejectReason.t)
   {
      case T_H225AdmissionRejectReason_calledPartyNotRegistered:
         call->callEndReason = OO_REASON_GK_NOCALLEDUSER;
         break;
      case T_H225AdmissionRejectReason_invalidPermission:
      case T_H225AdmissionRejectReason_requestDenied:
      case T_H225AdmissionRejectReason_undefinedReason:
         call->callEndReason = OO_REASON_GK_CLEARED;
         break;
      case T_H225AdmissionRejectReason_callerNotRegistered:
         call->callEndReason = OO_REASON_GK_NOCALLERUSER;
         break;
      case T_H225AdmissionRejectReason_exceedsCallCapacity:
      case T_H225AdmissionRejectReason_resourceUnavailable:
         call->callEndReason = OO_REASON_GK_NORESOURCES;
         break;
      case T_H225AdmissionRejectReason_noRouteToDestination:
      case T_H225AdmissionRejectReason_unallocatedNumber:
         call->callEndReason = OO_REASON_GK_UNREACHABLE;
         break;
      case T_H225AdmissionRejectReason_routeCallToGatekeeper:
      case T_H225AdmissionRejectReason_invalidEndpointIdentifier:
      case T_H225AdmissionRejectReason_securityDenial:
      case T_H225AdmissionRejectReason_qosControlNotSupported:
      case T_H225AdmissionRejectReason_incompleteAddress:
      case T_H225AdmissionRejectReason_aliasesInconsistent:
      case T_H225AdmissionRejectReason_routeCallToSCN:
      case T_H225AdmissionRejectReason_collectDestination:
      case T_H225AdmissionRejectReason_collectPIN:
      case T_H225AdmissionRejectReason_genericDataReason:
      case T_H225AdmissionRejectReason_neededFeatureNotSupported:
      case T_H225AdmissionRejectReason_securityErrors:
      case T_H225AdmissionRejectReason_securityDHmismatch:
      case T_H225AdmissionRejectReason_extElem1:
         call->callEndReason = OO_REASON_GK_CLEARED;
         break;
   }

   ast_cond_signal(&pCallAdmInfo->call->gkWait);
   return OO_OK;   
}


int ooGkClientSendIRR
   (ooGkClient *pGkClient, OOH323CallData *call)
{
   int iRet = 0;
   H225RasMessage *pRasMsg=NULL;
   OOCTXT* pctxt;
   H225InfoRequestResponse *pIRR=NULL;
   H225TransportAddress_ipAddress *pIpAddressLocal =NULL, *pIpRasAddress,
				   *pLocalAddr, *pRemoteAddr;
   H225TransportAddress *pTransportAddress;
   ooAliases *srcAliases=NULL;
   H225InfoRequestResponse_perCallInfo_element *perCallInfo = NULL;
   pctxt = &pGkClient->msgCtxt;

   ast_mutex_lock(&pGkClient->Lock);

   OOTRACEDBGA3("Building Info Request Resp for call (%s, %s)\n", 
                 call->callType, call->callToken);   
   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   if(!pRasMsg)
   {
      OOTRACEERR3("Error:Memory - ooGkClientSendIRR - "
                  "pRasMsg(%s, %s)\n", call->callType, call->callToken);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pRasMsg->t = T_H225RasMessage_infoRequestResponse;
   pIRR = (H225InfoRequestResponse*) memAlloc(pctxt, 
                                                 sizeof(H225InfoRequestResponse));
   if(!pIRR)
   {
      OOTRACEERR3("Error:Memory - ooGkClientSendIRR - "
                  "pIRR(%s, %s)\n", call->callType, call->callToken);
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memset(pIRR, 0, sizeof(H225InfoRequestResponse));
   pRasMsg->u.infoRequestResponse = pIRR;
   
   /* Populate call signalling addresses */
   pIpAddressLocal = (H225TransportAddress_ipAddress*)memAlloc(pctxt, 
                                     sizeof(H225TransportAddress_ipAddress));
   pTransportAddress = (H225TransportAddress*) memAlloc(pctxt,
                                                 sizeof(H225TransportAddress));
   if(!pIpAddressLocal || !pTransportAddress)
   {
      OOTRACEERR1("Error:Failed to allocate memory for Call Signalling "
                  "Addresses of IRR message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   inet_pton(AF_INET, pGkClient->localRASIP, pIpAddressLocal->ip.data);

   pIpAddressLocal->ip.numocts = 4;
   pIpAddressLocal->port = gH323ep.listenPort;

   pTransportAddress->t = T_H225TransportAddress_ipAddress;
   pTransportAddress->u.ipAddress = pIpAddressLocal;

   dListInit(&pIRR->callSignalAddress);
   dListAppend(pctxt, &pIRR->callSignalAddress,
                                       (void*)pTransportAddress);

   /* Populate seq number */
   pIRR->requestSeqNum = pGkClient->requestSeqNum++;
   if(!pIRR->requestSeqNum)
      pIRR->requestSeqNum = pGkClient->requestSeqNum++;

   pIpRasAddress = (H225TransportAddress_ipAddress*)memAlloc(pctxt, 
                                     sizeof(H225TransportAddress_ipAddress));
   if(!pIpRasAddress)
   {
      OOTRACEERR1("Error: Memory allocation for Ras Address of IRR message "
                  "failed\n");
      memReset(&pGkClient->msgCtxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   pIpRasAddress->ip.numocts = 4;
   pIpRasAddress->port = pGkClient->localRASPort;
   inet_pton(AF_INET, pGkClient->localRASIP, pIpRasAddress->ip.data);

   pIRR->rasAddress.u.ipAddress = pIpRasAddress;
   pIRR->rasAddress.t=T_H225TransportAddress_ipAddress; /* IPv4 address */

   /* Pose as gateway or terminal as per config */
   if(gH323ep.isGateway)
      pIRR->endpointType.m.gatewayPresent = TRUE;
   else
      pIRR->endpointType.m.terminalPresent = TRUE;

   pIRR->endpointType.m.nonStandardDataPresent=FALSE;
   pIRR->endpointType.m.vendorPresent=TRUE;
   ooGkClientFillVendor(pGkClient, &pIRR->endpointType.vendor);

   /* Populate Endpoint Identifier */
   pIRR->endpointIdentifier.nchars = pGkClient->endpointId.nchars;
   pIRR->endpointIdentifier.data = (ASN116BITCHAR*)memAlloc(pctxt,
                           sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);
   if(!pIRR->endpointIdentifier.data)
   {
      OOTRACEERR3("Error:Memory -  ooGkClientSendIRR - "
                  "endpointIdentifier.data(%s, %s)\n", call->callType,
                  call->callToken);
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memcpy((void*)pIRR->endpointIdentifier.data,
          (void*)pGkClient->endpointId.data,
          sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);


   /* Populate call aliases */
   if(call->ourAliases) 
      srcAliases = call->ourAliases;
   else
      srcAliases = gH323ep.aliases; 

   /* Populate Source Info */
   if(srcAliases)
   {
      iRet = ooPopulateAliasList(&pGkClient->msgCtxt, srcAliases,
                             &pIRR->endpointAlias, T_H225AliasAddress_h323_ID);
      if(OO_OK != iRet)
      {
         OOTRACEERR1("Error:Failed to populate source aliases -IRR message\n");
         memReset(pctxt);
         pGkClient->state = GkClientFailed;
	 ast_mutex_unlock(&pGkClient->Lock);
         return OO_FAILED;
      }
   }
   pIRR->m.endpointAliasPresent = TRUE;

   /* Populate need response & unsolicited */
   pIRR->needResponse = FALSE;
   pIRR->m.needResponsePresent = TRUE;
   pIRR->unsolicited = TRUE;
   pIRR->m.unsolicitedPresent = TRUE;
   
   /* Populate perCallInfo */

   pIRR->m.perCallInfoPresent = TRUE;

   perCallInfo = 
    (H225InfoRequestResponse_perCallInfo_element *)memAlloc(pctxt,
     sizeof(H225InfoRequestResponse_perCallInfo_element));
   memset(perCallInfo, 0, sizeof(H225InfoRequestResponse_perCallInfo_element));

   if(!perCallInfo)
   {
      OOTRACEERR3("Error:Memory -  ooGkClientSendIRR - "
                  "perCallInfo for (%s, %s)\n", call->callType,
                  call->callToken);
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   perCallInfo->m.originatorPresent = TRUE;
   perCallInfo->originator = (!strcmp(call->callType, "incoming")) ? FALSE : TRUE;

   pLocalAddr = (H225TransportAddress_ipAddress*)memAlloc(pctxt,
                                     sizeof(H225TransportAddress_ipAddress));
   pRemoteAddr = (H225TransportAddress_ipAddress*) memAlloc(pctxt,
                                     sizeof(H225TransportAddress_ipAddress));
   if(!pLocalAddr || !pRemoteAddr)
   {
      OOTRACEERR1("Error:Failed to allocate memory for Call Signalling "
                  "Addresses of IRR message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pLocalAddr->ip.numocts = 4;
   inet_pton(AF_INET, call->localIP, pLocalAddr->ip.data);
   pLocalAddr->port = (call->pH225Channel->port) ? call->pH225Channel->port : gH323ep.listenPort;

   pRemoteAddr->ip.numocts = 4;
   inet_pton(AF_INET, call->remoteIP, pRemoteAddr->ip.data);
   pRemoteAddr->port = call->remotePort;

   perCallInfo->callSignaling.m.sendAddressPresent = TRUE;
   perCallInfo->callSignaling.sendAddress.t = T_H225TransportAddress_ipAddress; 
   perCallInfo->callSignaling.m.recvAddressPresent = TRUE;
   perCallInfo->callSignaling.recvAddress.t = T_H225TransportAddress_ipAddress; 

   if (!strcmp(call->callType, "incoming")) {
// terminator
     perCallInfo->callSignaling.sendAddress.u.ipAddress = pRemoteAddr;
     perCallInfo->callSignaling.recvAddress.u.ipAddress = pLocalAddr;
   } else {
// originator
     perCallInfo->callSignaling.sendAddress.u.ipAddress = pLocalAddr;
     perCallInfo->callSignaling.recvAddress.u.ipAddress = pRemoteAddr;
   }

   /* Populate call Reference */
   perCallInfo->callReferenceValue = call->callReference;
   /* populate conferenceID */
   memcpy((void*)&perCallInfo->conferenceID, (void*)&call->confIdentifier,
                                         sizeof(H225ConferenceIdentifier));
   /* Populate call identifier */
   perCallInfo->m.callIdentifierPresent = TRUE;
   memcpy((void*)&perCallInfo->callIdentifier, (void*)&call->callIdentifier,
                                             sizeof(H225CallIdentifier));
   /* Populate call type & call model */
   perCallInfo->callType.t = T_H225CallType_pointToPoint;
   /* Add call model to message*/
   if(OO_TESTFLAG(call->flags, OO_M_GKROUTED))
      perCallInfo->callModel.t = T_H225CallModel_gatekeeperRouted;
   else
      perCallInfo->callModel.t = T_H225CallModel_direct;

   /* Populate usage info */
   if (call->alertingTime) {
     perCallInfo->usageInformation.m.alertingTimePresent = TRUE;
     perCallInfo->usageInformation.alertingTime = call->alertingTime;
   }
   if (call->connectTime) {
    perCallInfo->usageInformation.m.connectTimePresent = TRUE;
    perCallInfo->usageInformation.connectTime = call->connectTime;
   }
   perCallInfo->usageInformation.m.endTimePresent = FALSE;
   perCallInfo->m.usageInformationPresent = TRUE;
   
   dListInit(&pIRR->perCallInfo);
   dListAppend(pctxt, &pIRR->perCallInfo,
                                       (void*)perCallInfo);

   iRet = ooGkClientSendMsg(pGkClient, pRasMsg);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error:Failed to send IRR message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   OOTRACEINFO3("IRR message sent for (%s, %s)\n", 
                 call->callType, call->callToken);
   memReset(pctxt);
    
   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}

/**
 * This function is invoked to request call disengage to gatekeeper. 
 * 
 * @param   szCallToken    Call token.     
 *
 * @return  Completion status - 0 on success, -1 on failure
 */

int ooGkClientSendDisengageRequest(ooGkClient *pGkClient, OOH323CallData *call)
{
   int iRet = 0;   
   unsigned int x;
   H225RasMessage *pRasMsg=NULL;
   OOCTXT *pctxt = NULL;
   DListNode *pNode = NULL;
   H225DisengageRequest * pDRQ = NULL;
   RasCallAdmissionInfo* pCallAdmInfo=NULL;
   pctxt = &pGkClient->msgCtxt;

   ast_mutex_lock(&pGkClient->Lock);

   OOTRACEINFO3("Sending disengage Request for  call. (%s, %s)\n",
                 call->callType, call->callToken);

   pRasMsg = (H225RasMessage*)memAlloc(pctxt, sizeof(H225RasMessage));
   if(!pRasMsg)
   {
      OOTRACEERR1("Error: Memory allocation for DRQ RAS message failed\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   pRasMsg->t = T_H225RasMessage_disengageRequest;
   pDRQ = (H225DisengageRequest*) memAlloc(pctxt, 
                                               sizeof(H225DisengageRequest));
   if(!pDRQ)
   {
      OOTRACEERR1("Error: Failed to allocate memory for DRQ message\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

   memset(pDRQ, 0, sizeof(H225DisengageRequest));
   pRasMsg->u.disengageRequest = pDRQ;
   
   pDRQ->requestSeqNum = pGkClient->requestSeqNum++;
   if(!pDRQ->requestSeqNum )
      pDRQ->requestSeqNum = pGkClient->requestSeqNum++;
   
   
   pDRQ->endpointIdentifier.nchars = pGkClient->endpointId.nchars;
   pDRQ->endpointIdentifier.data = (ASN116BITCHAR*)memAlloc(pctxt,
                           sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);
   if(!pDRQ->endpointIdentifier.data)
   {
      OOTRACEERR1("Error: Failed to allocate memory for EndPoint Id in DRQ "
                  "message.\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   memcpy((void*)pDRQ->endpointIdentifier.data, 
                 (void*)pGkClient->endpointId.data, 
                 sizeof(ASN116BITCHAR)*pGkClient->endpointId.nchars);

   memcpy((void*)&pDRQ->conferenceID, (void*)&call->confIdentifier,
                                         sizeof(H225ConferenceIdentifier));

   pDRQ->callReferenceValue = call->callReference;
   
   pDRQ->disengageReason.t = T_H225DisengageReason_normalDrop;

   pDRQ->m.answeredCallPresent = 1;
   if(!strcmp(call->callType, "incoming"))
      pDRQ->answeredCall = 1;
   else
      pDRQ->answeredCall = 0;

   pDRQ->m.callIdentifierPresent = 1;
   memcpy((void*)&pDRQ->callIdentifier, (void*)&call->callIdentifier,
                                             sizeof(H225CallIdentifier));
   if (pGkClient->gkId.nchars) {
    pDRQ->m.gatekeeperIdentifierPresent = 1;
    pDRQ->gatekeeperIdentifier.nchars = pGkClient->gkId.nchars;
    pDRQ->gatekeeperIdentifier.data = (ASN116BITCHAR*)memAlloc
                       (pctxt, pGkClient->gkId.nchars*sizeof(ASN116BITCHAR));
    if(!pDRQ->gatekeeperIdentifier.data)
    {
      OOTRACEERR1("Error:Failed to allocate memory for GKId in DRQ.\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
    }
    memcpy(pDRQ->gatekeeperIdentifier.data, pGkClient->gkId.data, 
                                pGkClient->gkId.nchars*sizeof(ASN116BITCHAR));
   }

   pDRQ->m.terminationCausePresent = 1;
   pDRQ->terminationCause.t = T_H225CallTerminationCause_releaseCompleteCauseIE;
   pDRQ->terminationCause.u.releaseCompleteCauseIE = 
      (H225CallTerminationCause_releaseCompleteCauseIE*)memAlloc(pctxt,
      sizeof(H225CallTerminationCause_releaseCompleteCauseIE));
   if(!pDRQ->terminationCause.u.releaseCompleteCauseIE)
   {
      OOTRACEERR1("Error: Failed to allocate memory for cause ie in DRQ.\n");
      memReset(pctxt);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   pDRQ->terminationCause.u.releaseCompleteCauseIE->numocts = 
                                                         strlen("Call Ended");
   strcpy((char *)pDRQ->terminationCause.u.releaseCompleteCauseIE->data, "Call Ended");

   /* populate usage info */

   /* Populate usage info */
   if (call->alertingTime) {
     pDRQ->usageInformation.m.alertingTimePresent = TRUE;
     pDRQ->usageInformation.alertingTime = call->alertingTime;
   }
   if (call->connectTime) {
    pDRQ->usageInformation.m.connectTimePresent = TRUE;
    pDRQ->usageInformation.connectTime = call->connectTime;
   }
   pDRQ->usageInformation.m.endTimePresent = TRUE;
   if (call->endTime)
    pDRQ->usageInformation.endTime = call->endTime;
   else
    pDRQ->usageInformation.endTime = time(NULL);
   pDRQ->m.usageInformationPresent = TRUE;

   iRet = ooGkClientSendMsg(pGkClient, pRasMsg);
   if(iRet != OO_OK)
   {
      OOTRACEERR1("Error: Failed to send DRQ message\n");
      pGkClient->state = GkClientFailed;
   }
   


   /* Search call in admitted calls list */
   for(x=0 ; x<pGkClient->callsAdmittedList.count ; x++)
   {
      pNode = (DListNode*)dListFindByIndex(&pGkClient->callsAdmittedList, x);
      pCallAdmInfo = (RasCallAdmissionInfo*) pNode->data;
      if(pCallAdmInfo->call->callReference == call->callReference)
      {
         dListRemove( &pGkClient->callsAdmittedList, pNode);
         memFreePtr(&pGkClient->ctxt, pNode->data);
         memFreePtr(&pGkClient->ctxt, pNode);
         break;
      }
   }
   ast_mutex_unlock(&pGkClient->Lock);
   return iRet;
}     

int ooGkClientHandleDisengageConfirm
   (ooGkClient *pGkClient, H225DisengageConfirm *pDCF)
{
   OOTRACEINFO1("Received disengage confirm\n");
   return OO_OK;
}

int ooGkClientRRQTimerExpired(void*pdata)
{
   int ret=0;
   ooGkClientTimerCb *cbData = (ooGkClientTimerCb*)pdata;
   ooGkClient *pGkClient = cbData->pGkClient;
   OOTRACEDBGA1("Gatekeeper client RRQ timer expired.\n");
   
   if(pGkClient->rrqRetries < OO_MAX_RRQ_RETRIES)
   {
      ret = ooGkClientSendRRQ(pGkClient, 0);      
      if(ret != OO_OK)
      {
         OOTRACEERR1("Error:Failed to send RRQ message\n");
         
         return OO_FAILED;
      }
      pGkClient->rrqRetries++;
      memFreePtr(&pGkClient->ctxt, cbData);
      return OO_OK;
   }
   memFreePtr(&pGkClient->ctxt, cbData);
   OOTRACEERR1("Error:Failed to register with gatekeeper\n");
   pGkClient->state = GkClientUnregistered;


/* Create timer to re-register after default timeout */
/* network failure is one of cases here */

   ast_mutex_lock(&pGkClient->Lock);

   cbData = (ooGkClientTimerCb*) memAlloc
				(&pGkClient->ctxt, sizeof(ooGkClientTimerCb));
   if(!cbData)
   {
      OOTRACEERR1("Error:Failed to allocate memory to RRQ timer callback\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }


   cbData->timerType = OO_RRQ_TIMER;
   cbData->pGkClient = pGkClient;
   if(!ooTimerCreate(&pGkClient->ctxt, &pGkClient->timerList,
                     &ooGkClientRRQTimerExpired, pGkClient->regTimeout,
                     cbData, FALSE))
   {
      OOTRACEERR1("Error:Unable to create GRQ timer.\n ");
      memFreePtr(&pGkClient->ctxt, cbData);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }

/* clear rrq count for re-register after regTimeout */
   pGkClient->rrqRetries = 0;

   ast_mutex_unlock(&pGkClient->Lock);

   return OO_FAILED;
}

int ooGkClientGRQTimerExpired(void* pdata)
{
   int ret=0;
   ooGkClientTimerCb *cbData = (ooGkClientTimerCb*)pdata;
   ooGkClient *pGkClient = cbData->pGkClient;

   OOTRACEDBGA1("Gatekeeper client GRQ timer expired.\n");

   memFreePtr(&pGkClient->ctxt, cbData);   

   if(pGkClient->grqRetries < OO_MAX_GRQ_RETRIES)
   {
      ret = ooGkClientSendGRQ(pGkClient);      
      if(ret != OO_OK)
      {
         OOTRACEERR1("Error:Failed to send GRQ message\n");
         pGkClient->state = GkClientFailed;
         return OO_FAILED;
      }
      pGkClient->grqRetries++;
      return OO_OK;
   }

   OOTRACEERR1("Error:Gatekeeper could not be found\n");
   pGkClient->state = GkClientUnregistered;
/* setup timer to re-send grq after timeout */

   ast_mutex_lock(&pGkClient->Lock);
   cbData = (ooGkClientTimerCb*) memAlloc
                               (&pGkClient->ctxt, sizeof(ooGkClientTimerCb));
   if(!cbData)
   {
      OOTRACEERR1("Error:Failed to allocate memory to GRQ timer callback\n");
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
   cbData->timerType = OO_GRQ_TIMER;
   cbData->pGkClient = pGkClient;
   if(!ooTimerCreate(&pGkClient->ctxt, &pGkClient->timerList,
                     &ooGkClientGRQTimerExpired, pGkClient->grqTimeout,
                     cbData, FALSE))
   {
      OOTRACEERR1("Error:Unable to create GRQ timer.\n ");
      memFreePtr(&pGkClient->ctxt, cbData);
      pGkClient->state = GkClientFailed;
      ast_mutex_unlock(&pGkClient->Lock);
      return OO_FAILED;
   }
 
/* clear grq counter */

   pGkClient->grqRetries = 0;
   ast_mutex_unlock(&pGkClient->Lock);

   return OO_FAILED;
}
   
int ooGkClientREGTimerExpired(void *pdata)
{
   int ret=0;
   ooGkClientTimerCb *cbData = (ooGkClientTimerCb*)pdata;
   ooGkClient *pGkClient = cbData->pGkClient;
   OOTRACEDBGA1("Gatekeeper client additive registration timer expired\n");
   memFreePtr(&pGkClient->ctxt, cbData);   
   ret = ooGkClientSendRRQ(pGkClient, TRUE);      
   if(ret != OO_OK)
   {
      OOTRACEERR1("Error:Failed to send Additive RRQ message\n");
      pGkClient->state = GkClientFailed;
      return OO_FAILED;
   }
   return OO_OK;
}

int ooGkClientARQTimerExpired(void* pdata)
{
   int ret=0;
   ooGkClientTimerCb *cbData = (ooGkClientTimerCb*)pdata;
   ooGkClient *pGkClient = cbData->pGkClient;
   RasCallAdmissionInfo *pAdmInfo = cbData->pAdmInfo;

   OOTRACEDBGA1("Gatekeeper client ARQ timer expired.\n");
   memFreePtr(&pGkClient->ctxt, cbData);   

   if(!pAdmInfo)
    return OO_OK;

   if(pAdmInfo->retries < OO_MAX_ARQ_RETRIES)
   {
      ret = ooGkClientSendAdmissionRequest(pGkClient, pAdmInfo->call, TRUE);      
      if(ret != OO_OK)
      {
         OOTRACEERR1("Error:Failed to send ARQ message\n");
         return OO_FAILED;
      }
      pAdmInfo->retries++;
      return OO_OK;
   }

   OOTRACEERR1("Error:Gatekeeper not responding to ARQ\n");
   pGkClient->state = GkClientGkErr;
   return OO_FAILED;
}

int ooGkClientCleanCall(ooGkClient *pGkClient, OOH323CallData *call)
{
   unsigned int x=0;
   DListNode *pNode=NULL;
   OOTimer *pTimer;
   ooGkClientTimerCb *cbData=NULL;
   RasCallAdmissionInfo *pAdmInfo = NULL;

   ast_mutex_lock(&pGkClient->Lock);


   for(x=0; x<pGkClient->callsAdmittedList.count; x++)
   {
      pNode = dListFindByIndex(&pGkClient->callsAdmittedList, x);
      pAdmInfo = (RasCallAdmissionInfo*)pNode->data;
      if(pAdmInfo->call->callReference == call->callReference)
      {
         dListRemove(&pGkClient->callsAdmittedList, pNode);
         memFreePtr(&pGkClient->ctxt, pAdmInfo);
         memFreePtr(&pGkClient->ctxt, pNode);
	 break;
      }
   }


   for(x=0; x<pGkClient->timerList.count; x++)
   {
      pNode = dListFindByIndex(&pGkClient->timerList, x);
      pTimer = (OOTimer*)pNode->data;
      cbData = (ooGkClientTimerCb*)pTimer->cbData;
      if(cbData->timerType & OO_ARQ_TIMER &&
         cbData->pAdmInfo->call->callReference == call->callReference)
      {
         memFreePtr(&pGkClient->ctxt, pTimer->cbData);
         ooTimerDelete(&pGkClient->ctxt, &pGkClient->timerList, pTimer);
         break;
      }
   }

   for(x=0; x<pGkClient->callsPendingList.count; x++)
   {
      pNode = dListFindByIndex(&pGkClient->callsPendingList, x);
      pAdmInfo = (RasCallAdmissionInfo*)pNode->data;
      if(pAdmInfo->call->callReference == call->callReference)
      {
         dListRemove(&pGkClient->callsPendingList, pNode);
         memFreePtr(&pGkClient->ctxt, pAdmInfo);
         memFreePtr(&pGkClient->ctxt, pNode);
	 break;
      }
   }

   ast_mutex_unlock(&pGkClient->Lock);
   return OO_OK;
}

/*
 * TODO: In case of GkErr, if GkMode is DiscoverGatekeeper,
 *       need to cleanup gkrouted calls, and discover another
 *       gatekeeper.
 * Note: This function returns OO_FAILED, when we can not recover from
 *       the failure.
 */
int ooGkClientHandleClientOrGkFailure(ooGkClient *pGkClient)
{
   if(pGkClient->state == GkClientFailed)
   {
      OOTRACEERR1("Error: Internal Failure in GkClient. Closing "
                  "GkClient\n");
      ooGkClientDestroy();
      return OO_FAILED;
   }
   else if(pGkClient->state == GkClientGkErr) {
      OOTRACEERR1("Error: Gatekeeper error. Either Gk not responding or "
                  "Gk sending invalid messages\n");
      if(pGkClient->gkMode == RasUseSpecificGatekeeper)
      {
         OOTRACEERR1("Error: Gatekeeper error detected. Closing GkClient as "
                     "Gk mode is UseSpecifcGatekeeper\n");
         ooGkClientDestroy();
         return OO_FAILED;
      }
      else{
         OOTRACEERR1("Error: Gatekeeper error detected. Closing GkClient. NEED"
                    " to implement recovery by rediscovering another gk\n");
         ooGkClientDestroy();
         return OO_FAILED;
      }
   }

   return OO_FAILED;
}

/**
 * TODO: This fuction might not work properly in case of additive registrations
 * For example we registrered 10 aliases and gatekeeper accepted 8 of them.
 * Now we want to register another two new aliases(not out of those first 10).
 * Gk responds with RCF with empty terminalAlias field thus indicating both 
 * the aliases were accepted. If this function is called, it will even mark
 * the earlier two unregistered aliases as registered. We will have to
 * maintain a separete list of aliases being sent in RRQ for this.
 */
int ooGkClientUpdateRegisteredAliases
   (ooGkClient *pGkClient, H225_SeqOfH225AliasAddress *pAddresses, 
    OOBOOL registered)
{
   int i=0, j, k;
   DListNode* pNode=NULL;
   ooAliases *pAlias=NULL;
   H225AliasAddress *pAliasAddress=NULL;
   H225TransportAddress *pTransportAddrss=NULL;
   char value[MAXFILENAME];
   OOBOOL bAdd = FALSE;

   if(!pAddresses)
   {
     /* All aliases registered/unregistsred */
      pAlias = gH323ep.aliases;
      
      while(pAlias)
      {
         pAlias->registered = registered?TRUE:FALSE;
         pAlias = pAlias->next;
      }
      return OO_OK;
   }

   /* Mark aliases as registered/unregistered*/
   if(pAddresses->count<=0)
      return OO_FAILED;

   for(i=0; i<(int)pAddresses->count; i++)
   {
      pNode = dListFindByIndex (pAddresses, i);
      if(!pNode)
      {
         OOTRACEERR1("Error:Invalid alias list passed to "
                     "ooGkClientUpdateRegisteredAliases\n");
         continue;
      }
      pAliasAddress = (H225AliasAddress*)pNode->data;
      
      if(!pAliasAddress){
         OOTRACEERR1("Error:Invalid alias list passed to "
                     "ooGkClientUpdateRegisteredAliases\n");
         continue;
      }

      switch(pAliasAddress->t)
      {
      case T_H225AliasAddress_dialedDigits:
         pAlias = ooH323GetAliasFromList(gH323ep.aliases, 
                                          T_H225AliasAddress_dialedDigits, 
                                        (char*)pAliasAddress->u.dialedDigits);
         if(pAlias)
         {
            pAlias->registered = registered?TRUE:FALSE;
         }
         else{
            bAdd = registered?TRUE:FALSE;
         }
         break;
      case T_H225AliasAddress_h323_ID:
         for(j=0, k=0; j<(int)pAliasAddress->u.h323_ID.nchars && (k<MAXFILENAME-1); j++)
         {
            if(pAliasAddress->u.h323_ID.data[j] < 256)
            {
               value[k++] = (char) pAliasAddress->u.h323_ID.data[j];
            }
         }
         value[k] = '\0';
         pAlias = ooH323GetAliasFromList(gH323ep.aliases, 
                                         T_H225AliasAddress_h323_ID, 
                                          value);
         if(pAlias)
         {
            pAlias->registered = registered?TRUE:FALSE;
         }
         else{
            bAdd = registered?TRUE:FALSE;
         }
         break;
      case T_H225AliasAddress_url_ID:
         pAlias = ooH323GetAliasFromList(gH323ep.aliases, 
                                         T_H225AliasAddress_url_ID, 
                                       (char*)pAliasAddress->u.url_ID);
         if(pAlias)
         {
            pAlias->registered = registered?TRUE:FALSE;
         }
         else{
            bAdd = registered?TRUE:FALSE;
         }
         break;
      case T_H225AliasAddress_transportID:
         pTransportAddrss = pAliasAddress->u.transportID;
         if(pTransportAddrss->t != T_H225TransportAddress_ipAddress)
         {
            OOTRACEERR1("Error:Alias transportID not IP address\n");
            break;
         }
         
         sprintf(value, "%d.%d.%d.%d:%d", 
                          pTransportAddrss->u.ipAddress->ip.data[0],
                          pTransportAddrss->u.ipAddress->ip.data[1],
                          pTransportAddrss->u.ipAddress->ip.data[2],
                          pTransportAddrss->u.ipAddress->ip.data[3],
                          pTransportAddrss->u.ipAddress->port);

         pAlias = ooH323GetAliasFromList(gH323ep.aliases, 
                                         T_H225AliasAddress_transportID, 
                                         value);
         if(pAlias)
         {
            pAlias->registered = registered?TRUE:FALSE;
         }
         else{
            bAdd = registered?TRUE:FALSE;
         }
         break;
      case T_H225AliasAddress_email_ID:
         pAlias = ooH323GetAliasFromList(gH323ep.aliases, 
                                         T_H225AliasAddress_email_ID, 
                                       (char*) pAliasAddress->u.email_ID);
         if(pAlias)
         {
            pAlias->registered = registered?TRUE:FALSE;
         }
         else{
            bAdd = registered?TRUE:FALSE;
         }
         break;
      default:
         OOTRACEERR1("Error:Unhandled alias type found in registered "
                     "aliases\n");
      }
      if(bAdd)
      {
         pAlias = ooH323AddAliasToList(&gH323ep.aliases, 
                                           &gH323ep.ctxt, pAliasAddress);
         if(pAlias){
            pAlias->registered = registered?TRUE:FALSE;
         }
         else{
            OOTRACEERR2("Warning:Could not add registered alias of "
                        "type %d to list.\n", pAliasAddress->t);
         }
         bAdd = FALSE;
      }
      pAlias = NULL;
   }
   return OO_OK;
}
