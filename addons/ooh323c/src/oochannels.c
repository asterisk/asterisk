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
#include "asterisk/poll-compat.h"
#include "asterisk/config.h"
#include "asterisk/netsock2.h"

#include "ooports.h"
#include "oochannels.h"
#include "ootrace.h"
#include "ooq931.h"
#include "ooh245.h"
#include "ooh323.h"
#include "ooCalls.h"
#include "printHandler.h"
#include "ooGkClient.h"
#include "stdio.h"
#include "ooTimer.h"
#include "ooh323ep.h"
#include "ooStackCmds.h"
#include "ooCmdChannel.h"
#include "ooSocket.h"
#include "ootypes.h"


/** Global endpoint structure */
extern OOH323EndPoint gH323ep;
extern ast_mutex_t callListLock;
extern ast_mutex_t monitorLock;

extern DList g_TimerList;

static OOBOOL gMonitor = FALSE;

int ooSetCmdFDSETs(struct pollfd *pfds, int *nfds);
int ooProcessCmdFDSETsAndTimers
   (struct pollfd *pfds, int nfds, struct timeval *pToMin);
int ooSetFDSETs(struct pollfd *pfds, int *nfds);
int ooProcessFDSETsAndTimers
   (struct pollfd* pfds, int nfds, struct timeval *pToMin);
int ooProcessCallFDSETsAndTimers
   (OOH323CallData *call, struct pollfd* pfds, int nfds, struct timeval *pToMin);
int ooSetCallFDSETs(OOH323CallData* call, struct pollfd* pfds, int *nfds);



int ooCreateH245Listener(OOH323CallData *call)
{
   int ret=0;
   OOSOCKET channelSocket=0;
   OOTRACEINFO1("Creating H245 listener\n");
   if((ret=ooSocketCreate (&channelSocket, call->versionIP))!=ASN_OK)
   {
      OOTRACEERR3("ERROR: Failed to create socket for H245 listener "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   ret = ooBindPort (OOTCP, channelSocket, call->localIP);
   if(ret == OO_FAILED)
   {
      OOTRACEERR3("Error:Unable to bind to a TCP port - H245 listener creation"
                  " (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   call->h245listenport = (int*) memAlloc(call->pctxt, sizeof(int));
   *(call->h245listenport) = ret;
   call->h245listener = (OOSOCKET*)memAlloc(call->pctxt, sizeof(OOSOCKET));
   *(call->h245listener) = channelSocket;
   ret = ooSocketListen(*(call->h245listener), 4096);
   if(ret != ASN_OK)
   {
      OOTRACEERR3("Error:Unable to listen on H.245 socket (%s, %s)\n",
                   call->callType, call->callToken);
      return OO_FAILED;
   }

   OOTRACEINFO4("H245 listener creation - successful(port %d) (%s, %s)\n",
                *(call->h245listenport),call->callType, call->callToken);
   return OO_OK;
}

int ooCreateH245Connection(OOH323CallData *call)
{
   int ret=0;
   OOSOCKET channelSocket=0;
   ooTimerCallback *cbData=NULL;

   OOTRACEINFO1("Creating H245 Connection\n");
   if((ret=ooSocketCreate (&channelSocket, call->versionIP))!=ASN_OK)
   {
      OOTRACEERR3("ERROR:Failed to create socket for H245 connection "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   else
   {
      if (0 == call->pH245Channel) {
         call->pH245Channel =
            (OOH323Channel*) memAllocZ (call->pctxt, sizeof(OOH323Channel));
      }

      /*
         bind socket to a port before connecting. Thus avoiding
         implicit bind done by a connect call.
      */
      ret = ooBindPort(OOTCP, channelSocket, call->localIP);
      if(ret == OO_FAILED)
      {
         OOTRACEERR3("Error:Unable to bind to a TCP port - h245 connection "
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      call->pH245Channel->port = ret;
      OOTRACEDBGC4("Local H.245 port is %d (%s, %s)\n",
                   call->pH245Channel->port,
                   call->callType, call->callToken);
      OOTRACEINFO5("Trying to connect to remote endpoint to setup H245 "
                   "connection %s:%d(%s, %s)\n", call->remoteIP,
                    call->remoteH245Port, call->callType, call->callToken);

      if((ret=ooSocketConnect(channelSocket, call->remoteIP,
                              call->remoteH245Port))==ASN_OK)
      {
         call->pH245Channel->sock = channelSocket;
         call->h245SessionState = OO_H245SESSION_ACTIVE;

         OOTRACEINFO3("H245 connection creation successful (%s, %s)\n",
                      call->callType, call->callToken);

         /*Start terminal capability exchange and master slave determination */
         ret = ooSendTermCapMsg(call);
         if(ret != OO_OK)
         {
            OOTRACEERR3("ERROR:Sending Terminal capability message (%s, %s)\n",
                         call->callType, call->callToken);
            return ret;
         }
      }
      else
      {
         if(call->h245ConnectionAttempts >= 3)
         {
            OOTRACEERR3("Error:Failed to setup an H245 connection with remote "
                        "destination. (%s, %s)\n", call->callType,
                         call->callToken);
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_TRANSPORTFAILURE;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         else{
            OOTRACEWARN4("Warn:Failed to connect to remote destination for "
                         "H245 connection - will retry after %d seconds"
                         "(%s, %s)\n", DEFAULT_H245CONNECTION_RETRYTIMEOUT,
                         call->callType, call->callToken);

            cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                     sizeof(ooTimerCallback));
            if(!cbData)
            {
               OOTRACEERR3("Error:Unable to allocate memory for timer "
                           "callback.(%s, %s)\n", call->callType,
                            call->callToken);
               return OO_FAILED;
            }
            cbData->call = call;
            cbData->timerType = OO_H245CONNECT_TIMER;
            if(!ooTimerCreate(call->pctxt, &call->timerList,
                              &ooCallH245ConnectionRetryTimerExpired,
                              DEFAULT_H245CONNECTION_RETRYTIMEOUT, cbData,
                              FALSE))
            {
               OOTRACEERR3("Error:Unable to create H245 connection retry timer"
                           "(%s, %s)\n", call->callType, call->callToken);
               memFreePtr(call->pctxt, cbData);
               return OO_FAILED;
            }
            return OO_OK;
         }
      }
   }
   return OO_OK;
}

int ooSendH225Msg(OOH323CallData *call, Q931Message *msg)
{
   int iRet=0;
   ASN1OCTET * encodebuf;
   if(!call)
      return OO_FAILED;

   encodebuf = (ASN1OCTET*) memAlloc (call->pctxt, MAXMSGLEN);
   if(!encodebuf)
   {
      OOTRACEERR3("Error:Failed to allocate memory for encoding H225 "
                  "message(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   iRet = ooEncodeH225Message(call, msg, (char *)encodebuf, MAXMSGLEN);
   if(iRet != OO_OK)
   {
      OOTRACEERR3("Error:Failed to encode H225 message. (%s, %s)\n",
                  call->callType, call->callToken);
      memFreePtr (call->pctxt, encodebuf);
      return OO_FAILED;
   }

   /* If high priority messages, send immediately.*/
   if(encodebuf[0] == OOReleaseComplete ||
      (encodebuf[0]==OOFacility && encodebuf[1]==OOEndSessionCommand))
   {
      dListFreeAll(call->pctxt, &call->pH225Channel->outQueue);
      dListAppend (call->pctxt, &call->pH225Channel->outQueue, encodebuf);
      // ooSendMsg(call, OOQ931MSG);
   }
   else{
      dListAppend (call->pctxt, &call->pH225Channel->outQueue, encodebuf);

      OOTRACEDBGC4("Queued H225 messages %d. (%s, %s)\n",
                                     call->pH225Channel->outQueue.count,
                                     call->callType, call->callToken);
   }
   return OO_OK;
}

int ooCreateH225Connection(OOH323CallData *call)
{
   int ret=0, i;
   OOSOCKET channelSocket=0;
   for (i=0;i<3;i++) {
   if((ret=ooSocketCreate (&channelSocket, call->versionIP))!=ASN_OK)
   {
      OOTRACEERR3("Failed to create socket for transmit H2250 channel (%s, %s)"
                  "\n", call->callType, call->callToken);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callState = OO_CALL_CLEAR;
         call->callEndReason = OO_REASON_TRANSPORTFAILURE;
      }
      return OO_FAILED;
   }
   else
   {
      /*
         bind socket to a port before connecting. Thus avoiding
         implicit bind done by a connect call. Avoided on windows as
         windows sockets have problem in reusing the addresses even after
         setting SO_REUSEADDR, hence in windows we just allow os to bind
         to any random port.
      */
#ifndef _WIN32
      ret = ooBindPort(OOTCP,channelSocket, call->localIP);
#else
      ret = ooBindOSAllocatedPort(channelSocket, call->localIP);
#endif

      if(ret == OO_FAILED)
      {
         OOTRACEERR3("Error:Unable to bind to a TCP port (%s, %s)\n",
         call->callType, call->callToken);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callState = OO_CALL_CLEAR;
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
         }
         return OO_FAILED;
      }

      if (0 == call->pH225Channel) {
         call->pH225Channel =
            (OOH323Channel*) memAllocZ (call->pctxt, sizeof(OOH323Channel));
      }
      call->pH225Channel->port = ret;

      OOTRACEINFO6("Trying to connect to remote endpoint(%s:%d) (IPv%d) to setup "
                   "H2250 channel (%s, %s)\n", call->remoteIP,
                   call->remotePort, call->versionIP, call->callType, call->callToken);

      if((ret=ooSocketConnect(channelSocket, call->remoteIP,
                              call->remotePort))==ASN_OK)
      {
         call->pH225Channel->sock = channelSocket;

         OOTRACEINFO3("H2250 transmitter channel creation - successful "
                      "(%s, %s)\n", call->callType, call->callToken);

         /* If multihomed, get ip from socket */
         if(!strcmp(call->localIP, "0.0.0.0") || !strcmp(call->localIP, "::"))
         {
            OOTRACEDBGA3("Determining IP address for outgoing call in "
                         "multihomed mode. (%s, %s)\n", call->callType,
                          call->callToken);
            ret = ooSocketGetIpAndPort(channelSocket, call->localIP, 2+8*4+7,
                                       &call->pH225Channel->port, NULL);
            if(ret != ASN_OK)
            {
               OOTRACEERR3("ERROR:Failed to retrieve local ip and port from "
                           "socket for multihomed mode.(%s, %s)\n",
                            call->callType, call->callToken);
               if(call->callState < OO_CALL_CLEAR)
               {  /* transport failure */
                  call->callState = OO_CALL_CLEAR;
                  call->callEndReason = OO_REASON_TRANSPORTFAILURE;
               }
               return OO_FAILED;
            }
            OOTRACEDBGA4("Using local ip %s for outgoing call(multihomedMode)."
                         " (%s, %s)\n", call->localIP, call->callType,
                         call->callToken);
         }
         return OO_OK;
      }
      else
      {
         OOTRACEERR5("ERROR:Failed to connect to remote destination for "
                    "transmit H2250 channel(%s, %s, %d, %s)\n",call->callType,
                     call->callToken, channelSocket, call->localIP);
	 close(channelSocket);

         if(call->callState < OO_CALL_CLEAR)
         {  /* No one is listening at remote end */
            call->callState = OO_CALL_CLEAR;
            call->callEndReason = OO_REASON_NOUSER;
         }
         if (i>=2) return OO_FAILED; else continue;
      }

      return OO_FAILED;
    }
  }
  return OO_FAILED;
}

int ooCloseH225Connection (OOH323CallData *call)
{
   if (0 != call->pH225Channel)
   {
      if(call->pH225Channel->sock != 0)
         ooSocketClose (call->pH225Channel->sock);
      if (call->pH225Channel->outQueue.count > 0)
      {
         dListFreeAll (call->pctxt, &(call->pH225Channel->outQueue));
      }
      memFreePtr (call->pctxt, call->pH225Channel);
      call->pH225Channel = NULL;
   }
   return OO_OK;
}

int ooCreateH323Listener()
{
   int ret=0;
   OOSOCKET channelSocket=0;
   OOIPADDR ipaddrs;

    /* Create socket */
   ret = ast_parse_arg(gH323ep.signallingIP, PARSE_ADDR, &ipaddrs);
   if((ret=ooSocketCreate (&channelSocket, ast_sockaddr_is_ipv6(&ipaddrs) ? 6 : 4))
										!=ASN_OK)
   {
      OOTRACEERR1("Failed to create socket for H323 Listener\n");
      return OO_FAILED;
   }
   if((ret=ooSocketBind (channelSocket, ipaddrs,
                         gH323ep.listenPort))==ASN_OK)
   {
      gH323ep.listener = (OOSOCKET*)memAlloc(&gH323ep.ctxt,sizeof(OOSOCKET));
      *(gH323ep.listener) = channelSocket;

      ooSocketListen(channelSocket,2048); /*listen on socket*/
      OOTRACEINFO1("H323 listener creation - successful\n");
      return OO_OK;
   }
   else
   {
      OOTRACEERR1("ERROR:Failed to create H323 listener\n");
      return OO_FAILED;
   }
}



int ooAcceptH225Connection()
{
   OOH323CallData * call;
   int ret;
   char callToken[20];
   char remoteIP[2+8*4+7];
   OOSOCKET h225Channel=0;

   memset(remoteIP, 0, sizeof(remoteIP));
   ret = ooSocketAccept (*(gH323ep.listener), &h225Channel,
                         remoteIP, NULL);
   if(ret != ASN_OK)
   {
      OOTRACEERR1("Error:Accepting h225 connection\n");
      return OO_FAILED;
   }
   ooGenerateCallToken(callToken, sizeof(callToken));

   call = ooCreateCall("incoming", callToken);
   if(!call)
   {
      OOTRACEERR1("ERROR:Failed to create an incoming call\n");
      return OO_FAILED;
   }

   ast_mutex_lock(&call->Lock);
   call->pH225Channel = (OOH323Channel*)
      memAllocZ (call->pctxt, sizeof(OOH323Channel));

   call->pH225Channel->sock = h225Channel;

   /* If multihomed, get ip from socket */
   if(!strcmp(call->localIP, "0.0.0.0") || !strcmp(call->localIP,"::"))
   {
      OOTRACEDBGA3("Determining IP address for incoming call in multihomed "
                   "mode (%s, %s)\n", call->callType, call->callToken);

   }
   ret = ooSocketGetIpAndPort(h225Channel, call->localIP, 2+8*4+7,
                                       &call->pH225Channel->port, &call->versionIP);
   if(ret != ASN_OK)
   {
      OOTRACEERR3("Error:Failed to retrieve local ip and port from "
                  "socket for multihomed mode.(%s, %s)\n",
                   call->callType, call->callToken);
      if(call->callState < OO_CALL_CLEAR)
      {  /* transport failure */
         call->callState = OO_CALL_CLEAR;
         call->callEndReason = OO_REASON_TRANSPORTFAILURE;
      }
      ast_mutex_unlock(&call->Lock);
      return OO_FAILED;
   }
   OOTRACEDBGA5("Using Local IP address %s (IPv%d) for incoming call "
                "(%s, %s)\n", call->localIP, call->versionIP, call->callType,
                 call->callToken);

   if (remoteIP[0]) {
	memcpy(call->remoteIP, remoteIP, strlen(remoteIP) + 1);
   }

   ast_mutex_unlock(&call->Lock);
   return OO_OK;
}

int ooAcceptH245Connection(OOH323CallData *call)
{
   int ret;
   OOSOCKET h245Channel=0;
   ret = ooSocketAccept (*(call->h245listener), &h245Channel,
                         NULL, NULL);
   if(ret != ASN_OK)
   {
      OOTRACEERR1("Error:Accepting h245 connection\n");
      return OO_FAILED;
   }

   if (0 == call->pH245Channel) {
      call->pH245Channel =
         (OOH323Channel*) memAllocZ (call->pctxt, sizeof(OOH323Channel));
   }
   call->pH245Channel->sock = h245Channel;
   call->h245SessionState = OO_H245SESSION_ACTIVE;


   OOTRACEINFO3("H.245 connection established (%s, %s)\n",
                call->callType, call->callToken);

   return OO_OK;
}


int ooSetCmdFDSETs(struct pollfd *pfds, int *nfds)
{
   if(gH323ep.cmdSock)
   {
      pfds[*nfds].fd = gH323ep.cmdSock;
      pfds[*nfds].events = POLLIN;
      (*nfds)++;
   }

   return OO_OK;

}

int ooProcessCmdFDSETsAndTimers
   (struct pollfd *pfds, int nfds, struct timeval *pToMin)
{
   if(gH323ep.cmdSock) {
      if(ooPDRead(pfds, nfds, gH323ep.cmdSock)) {
         if(ooReadAndProcessStackCommand() != OO_OK) {
			 /* ooReadAndProcessStackCommand prints an error message */
			 return OO_FAILED;
		 }
      }
   }

   return OO_OK;

}


int ooSetFDSETs(struct pollfd *pfds, int *nfds)
{
   if(gH323ep.gkClient && gH323ep.gkClient->rasSocket != 0)
   {
      pfds[*nfds].fd = gH323ep.gkClient->rasSocket;
      pfds[*nfds].events = POLLIN;
      (*nfds)++;
   }
   if(gH323ep.listener)
   {
      pfds[*nfds].fd = *gH323ep.listener;
      pfds[*nfds].events = POLLIN;
      (*nfds)++;
   }

   return OO_OK;

}

int ooSetCallFDSETs(OOH323CallData* call, struct pollfd* pfds, int *nfds)
{

   if(call) {

    if(call->cmdSock && call->callState < OO_CALL_CLEAR) {
      pfds[*nfds].fd = call->cmdSock;
      pfds[*nfds].events = POLLIN;
      (*nfds)++;
    }

    if (0 != call->pH225Channel && 0 != call->pH225Channel->sock) {
      pfds[*nfds].fd = call->pH225Channel->sock;
      pfds[*nfds].events = POLLIN;

      if (call->pH225Channel->outQueue.count > 0 ||
       (OO_TESTFLAG (call->flags, OO_M_TUNNELING) &&
         0 != call->pH245Channel &&
         call->pH245Channel->outQueue.count>0))
       pfds[*nfds].events |= POLLOUT;
      (*nfds)++;

    }

    if (0 != call->pH245Channel &&  call->pH245Channel->sock != 0) {
       pfds[*nfds].fd = call->pH245Channel->sock;
       pfds[*nfds].events = POLLIN;
       if (call->pH245Channel->outQueue.count>0)
        pfds[*nfds].events |= POLLOUT;
       (*nfds)++;
      }
      else if(call->h245listener) {
       OOTRACEINFO3("H.245 Listerner socket being monitored "
        "(%s, %s)\n", call->callType, call->callToken);
       pfds[*nfds].fd = *(call->h245listener);
       pfds[*nfds].events = POLLIN;
       (*nfds)++;
      }


   }

   return OO_OK;
}

int ooProcessFDSETsAndTimers
   (struct pollfd* pfds, int nfds, struct timeval *pToMin)
{
   struct timeval toNext;

   /* Process gatekeeper client timers */
   if(gH323ep.gkClient)
   {
      ooTimerFireExpired(&gH323ep.gkClient->ctxt,
                         &gH323ep.gkClient->timerList);
      if(ooTimerNextTimeout(&gH323ep.gkClient->timerList, &toNext))
      {
         if(ooCompareTimeouts(pToMin, &toNext)>0)
         {
            pToMin->tv_sec = toNext.tv_sec;
            pToMin->tv_usec = toNext.tv_usec;
         }
      }
      if(gH323ep.gkClient->state == GkClientFailed ||
         gH323ep.gkClient->state == GkClientGkErr)
      {
         ooGkClientHandleClientOrGkFailure(gH323ep.gkClient);
      }
   }

   /* Manage ready descriptors after select */

   if(0 != gH323ep.gkClient && 0 != gH323ep.gkClient->rasSocket)
   {
      if(ooPDRead(pfds, nfds, gH323ep.gkClient->rasSocket) )
      {
         ooGkClientReceive(gH323ep.gkClient);
         if(gH323ep.gkClient->state == GkClientFailed ||
            gH323ep.gkClient->state == GkClientGkErr) {
            ooGkClientHandleClientOrGkFailure(gH323ep.gkClient);
         }
      }
   }

   if(gH323ep.listener)
   {
      if(ooPDRead(pfds, nfds, *(gH323ep.listener)))
      {
         OOTRACEDBGA1("New connection at H225 receiver\n");
         ooAcceptH225Connection();
      }
   }


   return OO_OK;

}
int ooProcessCallFDSETsAndTimers
   (OOH323CallData *call, struct pollfd* pfds, int nfds, struct timeval *pToMin)
{
   struct timeval toNext;

   if(call)
   {

    if(call->cmdSock) {
      if(ooPDRead(pfds, nfds, call->cmdSock)) {
	 ast_mutex_lock(&call->Lock);
         if(ooReadAndProcessCallStackCommand(call) != OO_OK) {
			 /* ooReadAndProcessStackCommand prints an error message */
	 		 ast_mutex_unlock(&call->Lock);
			 return OO_FAILED;
		 }
	 ast_mutex_unlock(&call->Lock);
      }
    }

    ooTimerFireExpired(call->pctxt, &call->timerList);
    if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
    {
     if(ooPDRead(pfds, nfds, call->pH225Channel->sock))
     {
      if(ooH2250Receive(call) != OO_OK)
      {
       OOTRACEERR3("ERROR:Failed ooH2250Receive - Clearing call "
        "(%s, %s)\n", call->callType, call->callToken);
       if(call->callState < OO_CALL_CLEAR)
       {
        if (!call->callEndReason) call->callEndReason = OO_REASON_INVALIDMESSAGE;
        call->callState = OO_CALL_CLEAR;
       }
      }
     }
    }

    if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
     if(ooPDRead(pfds, nfds, call->pH245Channel->sock))
      ooH245Receive(call);

    if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
    {
     if(ooPDWrite(pfds, nfds, call->pH245Channel->sock)) {
      if (call->pH245Channel->outQueue.count>0) {
       if (ooSendMsg(call, OOH245MSG) != OO_OK)
	OOTRACEERR1("Error in sending h245 message\n");
      }
     }
    }
    else if(call->h245listener)
    {
     if(ooPDRead(pfds, nfds, *(call->h245listener)))
     {
      OOTRACEDBGC3("Incoming H.245 connection (%s, %s)\n",
                    call->callType, call->callToken);
      ooAcceptH245Connection(call);
     }
    }

    if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
    {
     if(ooPDWrite(pfds, nfds, call->pH225Channel->sock))
     {
      if (call->pH225Channel->outQueue.count>0)
      {
       OOTRACEDBGC3("Sending H225 message (%s, %s)\n",
                        call->callType, call->callToken);
       if (ooSendMsg(call, OOQ931MSG) != OO_OK)
	OOTRACEERR1("Error in sending h225 message\n");
      }
      if(call->pH245Channel &&
         call->pH245Channel->outQueue.count>0 &&
        OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
        OOTRACEDBGC3("H245 message needs to be tunneled. "
                          "(%s, %s)\n", call->callType,
                               call->callToken);
        if (ooSendMsg(call, OOH245MSG) != OO_OK)
	  OOTRACEERR1("Error in sending h245 message\n");
       }
      }
     }

     if(ooTimerNextTimeout(&call->timerList, &toNext))
     {
      if(ooCompareTimeouts(pToMin, &toNext) > 0)
      {
       pToMin->tv_sec = toNext.tv_sec;
       pToMin->tv_usec = toNext.tv_usec;
      }
     }

     if(call->callState >= OO_CALL_CLEAR && call->callState < OO_CALL_CLEARED) {
	    ast_mutex_lock(&call->Lock);
            ooEndCall(call);
	    ast_mutex_unlock(&call->Lock);
     } else if(call->callState == OO_CALL_CLEARED) {
	    ast_mutex_lock(&call->Lock);
            ooEndCall(call);
	    ast_mutex_unlock(&call->Lock);
     }
     if(call->callState >= OO_CALL_CLEARED)
		ooStopMonitorCallChannels(call);
   }

   return OO_OK;

}

int ooMonitorCmdChannels()
{
   int ret=0, nfds=0;
   struct timeval toMin;
   struct pollfd pfds[1];

   gMonitor = TRUE;

   toMin.tv_sec = 3;
   toMin.tv_usec = 0;

   while(1)
   {
      nfds = 0;
      ooSetCmdFDSETs(pfds, &nfds);

      if(!gMonitor) {
         OOTRACEINFO1("Ending Monitor thread\n");
         break;
      }


      if(nfds == 0)
#ifdef _WIN32
         Sleep(10);
#else
      {
         toMin.tv_sec = 0;
         toMin.tv_usec = 10000;
         ooSocketPoll(pfds, nfds, toMin.tv_usec / 1000);
      }
#endif
      else
         ret = ooSocketPoll(pfds, nfds, toMin.tv_sec * 1000 + toMin.tv_usec / 1000);

      if(ret == -1)
      {

         OOTRACEERR1("Error in poll ...exiting\n");
         exit(-1);
	 continue;
      }

      toMin.tv_sec = 2;	 /* 2 sec */
      toMin.tv_usec = 100000; /* 100ms*/

      ast_mutex_lock(&monitorLock);
      if(ooProcessCmdFDSETsAndTimers(pfds, nfds, &toMin) != OO_OK)
      {
         /* ooStopMonitorCalls(); */
         ast_mutex_unlock(&monitorLock);
         continue;
      }
      ast_mutex_unlock(&monitorLock);

   }/* while(1)*/
   return OO_OK;
}

int ooMonitorChannels()
{
   int ret=0, nfds=0;
   struct timeval toMin, toNext;
   struct pollfd pfds[2];

   gMonitor = TRUE;

   toMin.tv_sec = 3;
   toMin.tv_usec = 0;
   ooH323EpPrintConfig();

   if(gH323ep.gkClient) {
      ooGkClientPrintConfig(gH323ep.gkClient);
      if(OO_OK != ooGkClientStart(gH323ep.gkClient))
      {
         OOTRACEERR1("Error:Failed to start Gatekeeper client\n");
	 // not need more, now it can be restarted correctly
         // ooGkClientDestroy();
      }
   }

   while(1)
   {
      nfds = 0;
      ooSetFDSETs(pfds, &nfds);

      if(!gMonitor) {
         OOTRACEINFO1("Ending Monitor thread\n");
         break;
      }


      if(nfds == 0)
#ifdef _WIN32
         Sleep(10);
#else
      {
         toMin.tv_sec = 0;
         toMin.tv_usec = 10000;
         ooSocketPoll(pfds, nfds, toMin.tv_usec / 1000);
      }
#endif
      else
         ret = ooSocketPoll(pfds, nfds, toMin.tv_sec * 1000 + toMin.tv_usec / 1000);

      if(ret == -1)
      {

         OOTRACEERR1("Error in poll ...exiting\n");
         exit(-1);
      }

      toMin.tv_sec = 2; /* 2 sec */
      toMin.tv_usec = 100000; /* 100ms*/
      /*This is for test application. Not part of actual stack */

      ast_mutex_lock(&monitorLock);
      ooTimerFireExpired(&gH323ep.ctxt, &g_TimerList);
      if(ooTimerNextTimeout(&g_TimerList, &toNext))
      {
         if(ooCompareTimeouts(&toMin, &toNext)>0)
         {
            toMin.tv_sec = toNext.tv_sec;
            toMin.tv_usec = toNext.tv_usec;
         }
      }

      if(ooProcessFDSETsAndTimers(pfds, nfds, &toMin) != OO_OK)
      {
         ast_mutex_unlock(&monitorLock);
         ooStopMonitorCalls();
         continue;
      }

      ast_mutex_unlock(&monitorLock);
   }/* while(1)*/
   return OO_OK;
}
int ooMonitorCallChannels(OOH323CallData *call)
{
   int ret=0, nfds=0, zeroloops = 0;
#define MAX_ZERO_LOOP 1020
   struct timeval toMin;
   struct pollfd pfds[5];

   OOCTXT* pctxt;

   call->Monitor = TRUE;

   toMin.tv_sec = 3;
   toMin.tv_usec = 0;

   while(1)
   {
      if(!call->Monitor) {
         OOTRACEINFO1("Ending Call Monitor thread\n");
         break;
      }

      nfds = 0;
      ooSetCallFDSETs(call, pfds, &nfds);


      if(nfds == 0)
#ifdef _WIN32
         Sleep(10);
#else
      {
	 if (zeroloops++ > MAX_ZERO_LOOP) {
		ooCleanCall(call);
		ooStopMonitorCallChannels(call);
		break;
	 }
         toMin.tv_sec = 0;
         toMin.tv_usec = 10000;
         ooSocketPoll(pfds, nfds, toMin.tv_usec / 1000);
      }
#endif
      else
         ret = ooSocketPoll(pfds, nfds, toMin.tv_sec * 1000 + toMin.tv_usec / 1000);

      if(ret == -1)
      {

        OOTRACEERR2("Error in poll %d ...exiting\n", errno);
        call->callEndReason = OO_REASON_INVALIDMESSAGE;
       	call->callState = OO_CALL_CLEARED;
	ooCleanCall(call);
	ooStopMonitorCallChannels(call);
	break;

      }

      toMin.tv_sec = 2; /* 2 sec */
      toMin.tv_usec = 100000; /* 100ms*/
      /*This is for test application. Not part of actual stack */

      if(ooProcessCallFDSETsAndTimers(call, pfds, nfds, &toMin) != OO_OK)
      {
         ooStopMonitorCallChannels(call);
         continue;
      }

   }/* while(1)*/

   if (call->cmdSock)
    ooCloseCallCmdConnection(call);

   ast_mutex_lock(&call->Lock);
   ast_mutex_unlock(&call->Lock);
   ast_mutex_destroy(&call->Lock);
   ast_mutex_destroy(&call->GkLock);
   ast_cond_destroy(&call->gkWait);
   pctxt = call->pctxt;
   freeContext(pctxt);
   ast_free(pctxt);

   return OO_OK;
}

int ooH2250Receive(OOH323CallData *call)
{
   int  recvLen=0, total=0, ret=0;
   ASN1OCTET message[MAXMSGLEN], message1[MAXMSGLEN];
   int len;
   Q931Message *pmsg;
   OOCTXT *pctxt = call->msgctxt;

   struct timeval timeout;


   pmsg = (Q931Message*)memAlloc(pctxt, sizeof(Q931Message));
   if(!pmsg)
   {
      OOTRACEERR3("ERROR:Failed to allocate memory for incoming H.2250 message"
                  " (%s, %s)\n", call->callType, call->callToken);
      /* memReset(&gH323ep.msgctxt); */
      memReset(call->pctxt);
      return OO_FAILED;
   }
   memset(pmsg, 0, sizeof(Q931Message));
   /* First read just TPKT header which is four bytes */
   recvLen = ooSocketRecv (call->pH225Channel->sock, message, 4);
   if(recvLen <= 0)
   {
      if(recvLen == 0)
         OOTRACEWARN3("Warn:RemoteEndpoint closed connection (%s, %s)\n",
                      call->callType, call->callToken);
      else
         OOTRACEERR3("Error:Transport failure while reading Q931 "
                     "message (%s, %s)\n", call->callType, call->callToken);

      ooCloseH225Connection(call);
      if(call->callState < OO_CALL_CLEARED)
      {
         if(call->callState < OO_CALL_CLEAR)
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
         call->callState = OO_CALL_CLEARED;

      }
      ooFreeQ931Message(pctxt, pmsg);
      return OO_OK;
   }
   OOTRACEDBGC3("Receiving H.2250 message (%s, %s)\n",
                call->callType, call->callToken);
   /* Since we are working with TCP, need to determine the
      message boundary. Has to be done at channel level, as channels
      know the message formats and can determine boundaries
   */
   if(recvLen != 4)
   {
      OOTRACEERR4("Error: Reading TPKT header for H225 message "
                  "recvLen= %d (%s, %s)\n", recvLen, call->callType,
                  call->callToken);
      ooFreeQ931Message(pctxt, pmsg);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }


   len = message[2];
   len = len<<8;
   len = len | message[3];
   /* Remaining message length is length - tpkt length */
   len = len - 4;

   if(len > MAXMSGLEN - 4)
   {
      OOTRACEERR4("Error: Invalid TPKT header for H225 message "
                  "Len = %d (%s, %s)\n", len, call->callType,
                  call->callToken);
      ooCloseH225Connection(call);
      ooFreeQ931Message(pctxt, pmsg);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   /* Now read actual Q931 message body. We should make sure that we
      receive complete message as indicated by len. If we don't then there
      is something wrong. The loop below receives message, then checks whether
      complete message is received. If not received, then uses select to peek
      for remaining bytes of the message. If message is not received in 3
      seconds, then we have a problem. Report an error and exit.
   */
   while(total < len)
   {
      struct pollfd pfds;

      pfds.fd = call->pH225Channel->sock;
      pfds.events = POLLIN;
      timeout.tv_sec = 3;
      timeout.tv_usec = 0;
      ret = ooSocketPoll(&pfds, 1,  timeout.tv_sec * 1000);
      if(ret == -1)
      {
         OOTRACEERR3("Error in select while receiving H.2250 message - "
                     "clearing call (%s, %s)\n", call->callType,
                     call->callToken);
         ooFreeQ931Message(pctxt, pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }

      /* exit If remaining part of the message is not received in 3 seconds */

      if(!ooPDRead(&pfds, 1, call->pH225Channel->sock))
      {
         OOTRACEERR3("Error: Incomplete H.2250 message received - clearing "
                     "call (%s, %s)\n", call->callType, call->callToken);
         ooFreeQ931Message(pctxt, pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_INVALIDMESSAGE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }

      recvLen = ooSocketRecv (call->pH225Channel->sock, message1, len-total);
      if (recvLen == 0) {
         OOTRACEERR3("Error in read while receiving H.2250 message - "
                     "clearing call (%s, %s)\n", call->callType,
                     call->callToken);
         ooFreeQ931Message(pctxt, pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      memcpy(message+total, message1, recvLen);
      total = total + recvLen;

   }

   OOTRACEDBGC3("Received Q.931 message: (%s, %s)\n",
                call->callType, call->callToken);

   initializePrintHandler(&printHandler, "Received H.2250 Message");
   setEventHandler (pctxt, &printHandler);
   setPERBuffer (pctxt, message, len, TRUE);
   ret = ooQ931Decode (call, pmsg, len, message, 1);
   if(ret != OO_OK) {
      OOTRACEERR3("Error:Failed to decode received H.2250 message. (%s, %s)\n",
                   call->callType, call->callToken);
   }
   OOTRACEDBGC3("Decoded Q931 message (%s, %s)\n", call->callType,
                call->callToken);
   finishPrint();
   removeEventHandler(pctxt);
   if(ret == OO_OK) {
      ret = ooHandleH2250Message(call, pmsg);
   }
   return ret;
}



int ooH245Receive(OOH323CallData *call)
{
   int  recvLen, ret, len, total=0;
   ASN1OCTET message[MAXMSGLEN], message1[MAXMSGLEN];
   ASN1BOOL aligned = TRUE;
   H245Message *pmsg;
   /* OOCTXT *pctxt = &gH323ep.msgctxt; */
   OOCTXT *pctxt = call->pctxt;
   struct timeval timeout;

   pmsg = (H245Message*)memAlloc(pctxt, sizeof(H245Message));

   /* First read just TPKT header which is four bytes */
   recvLen = ooSocketRecv (call->pH245Channel->sock, message, 4);
   /* Since we are working with TCP, need to determine the
      message boundary. Has to be done at channel level, as channels
      know the message formats and can determine boundaries
   */
   if(recvLen<=0 && call->h245SessionState != OO_H245SESSION_PAUSED)
   {
      if(recvLen == 0)
         OOTRACEINFO3("Closing H.245 channels as remote end point closed H.245"
                    " connection (%s, %s)\n", call->callType, call->callToken);
      else
         OOTRACEERR3("Error: Transport failure while trying to receive H245"
                     " message (%s, %s)\n", call->callType, call->callToken);

      ooCloseH245Connection(call);
      ooFreeH245Message(call, pmsg);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_TRANSPORTFAILURE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }
   if(call->h245SessionState == OO_H245SESSION_PAUSED)
   {
      ooLogicalChannel *temp;

      OOTRACEINFO3("Call Paused, closing logical channels"
                    " (%s, %s)\n", call->callType, call->callToken);

      temp = call->logicalChans;
      while(temp)
      {
         if(temp->state == OO_LOGICALCHAN_ESTABLISHED)
         {
            /* Sending closelogicalchannel only for outgoing channels*/
            if(!strcmp(temp->dir, "transmit"))
            {
               ooSendCloseLogicalChannel(call, temp);
            }
         }
         temp = temp->next;
      }
      call->masterSlaveState = OO_MasterSlave_Idle;
      call->callState = OO_CALL_PAUSED;
      call->localTermCapState = OO_LocalTermCapExchange_Idle;
      call->remoteTermCapState = OO_RemoteTermCapExchange_Idle;
      call->h245SessionState = OO_H245SESSION_IDLE;
      call->logicalChans = NULL;
   }
   OOTRACEDBGC1("Receiving H245 message\n");
   if(recvLen != 4)
   {
      OOTRACEERR3("Error: Reading TPKT header for H245 message (%s, %s)\n",
                  call->callType, call->callToken);
      ooFreeH245Message(call, pmsg);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   len = message[2];
   len = len<<8;
   len = (len | message[3]);
   /* Remaining message length is length - tpkt length */
   len = len - 4;
   /* Now read actual H245 message body. We should make sure that we
      receive complete message as indicated by len. If we don't then there
      is something wrong. The loop below receives message, then checks whether
      complete message is received. If not received, then uses select to peek
      for remaining bytes of the message. If message is not received in 3
      seconds, then we have a problem. Report an error and exit.
   */

   if(len > MAXMSGLEN - 4)
   {
      OOTRACEERR4("Error: Invalid TPKT header length %d for H245 message (%s, %s)\n",
                  len, call->callType, call->callToken);
      ooFreeH245Message(call, pmsg);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_INVALIDMESSAGE;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   while(total < len)
   {
      struct pollfd pfds;
      recvLen = ooSocketRecv (call->pH245Channel->sock, message1, len-total);
      memcpy(message+total, message1, recvLen);
      total = total + recvLen;
      if(total == len) break; /* Complete message is received */

      pfds.fd = call->pH245Channel->sock;
      pfds.events = POLLIN;
      timeout.tv_sec = 3;
      timeout.tv_usec = 0;
      ret = ooSocketPoll(&pfds, 1,  timeout.tv_sec * 1000);
      if(ret == -1)
      {
         OOTRACEERR3("Error in select...H245 Receive-Clearing call (%s, %s)\n",
                     call->callType, call->callToken);
         ooFreeH245Message(call, pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      /* If remaining part of the message is not received in 3 seconds
         exit */
      if(!ooPDRead(&pfds, 1, call->pH245Channel->sock))
      {
         OOTRACEERR3("Error: Incomplete h245 message received (%s, %s)\n",
                     call->callType, call->callToken);
         ooFreeH245Message(call, pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
   }

   OOTRACEDBGC3("Complete H245 message received (%s, %s)\n",
                 call->callType, call->callToken);
   setPERBuffer(pctxt, message, recvLen, aligned);
   initializePrintHandler(&printHandler, "Received H.245 Message");

   /* Set event handler */
   setEventHandler (pctxt, &printHandler);

   ret = asn1PD_H245MultimediaSystemControlMessage(pctxt, &(pmsg->h245Msg));
   if(ret != ASN_OK)
   {
      OOTRACEERR3("Error decoding H245 message (%s, %s)\n",
                  call->callType, call->callToken);
      ooFreeH245Message(call, pmsg);
      return OO_FAILED;
   }
   finishPrint();
   removeEventHandler(pctxt);
   ooHandleH245Message(call, pmsg);
   return OO_OK;
}

/* Generic Send Message functionality. Based on type of message to be sent,
   it calls the corresponding function to retrieve the message buffer and
   then transmits on the associated channel
   Interpreting msgptr:
      Q931 messages except facility
                             1st octet - msgType, next 4 octets - tpkt header,
                             followed by encoded msg
      Q931 message facility
                             1st octect - OOFacility, 2nd octet - tunneled msg
                             type(in case no tunneled msg - OOFacility),
                             3rd and 4th octet - associated logical channel
                             of the tunneled msg(0 when no channel is
                             associated. ex. in case of MSD, TCS), next
                             4 octets - tpkt header, followed by encoded
                             message.

      H.245 messages no tunneling
                             1st octet - msg type, next two octets - logical
                             channel number(0, when no channel is associated),
                             next two octets - total length of the message
                            (including tpkt header)

      H.245 messages - tunneling.
                             1st octet - msg type, next two octets - logical
                             channel number(0, when no channel is associated),
                             next two octets - total length of the message.
                             Note, no tpkt header is present in this case.

*/
int ooSendMsg(OOH323CallData *call, int type)
{

   int len=0, ret=0, msgType=0, tunneledMsgType=0, logicalChannelNo = 0;
   DListNode * p_msgNode=NULL;
   ASN1OCTET *msgptr, *msgToSend=NULL;



   if(call->callState == OO_CALL_CLEARED)
   {
      OOTRACEDBGA3("Warning:Call marked for cleanup. Can not send message."
                   "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   if(type == OOQ931MSG)
   {
      if(call->pH225Channel->outQueue.count == 0)
      {
         OOTRACEWARN3("WARN:No H.2250 message to send. (%s, %s)\n",
                      call->callType, call->callToken);
         return OO_FAILED;
      }

      OOTRACEDBGA3("Sending Q931 message (%s, %s)\n", call->callType,
                                                      call->callToken);
      p_msgNode = call->pH225Channel->outQueue.head;
      msgptr = (ASN1OCTET*) p_msgNode->data;
      msgType = msgptr[0];

      if(msgType == OOFacility)
      {
         tunneledMsgType = msgptr[1];
         logicalChannelNo = msgptr[2];
         logicalChannelNo = logicalChannelNo << 8;
         logicalChannelNo = (logicalChannelNo | msgptr[3]);
         len = msgptr[6];
         len = len<<8;
         len = (len | msgptr[7]);
         msgToSend = msgptr+4;
      }
      else {
         len = msgptr[3];
         len = len<<8;
         len = (len | msgptr[4]);
         msgToSend = msgptr+1;
      }

      /* Remove the message from rtdlist pH225Channel->outQueue */
      dListRemove(&(call->pH225Channel->outQueue), p_msgNode);
      if(p_msgNode)
         memFreePtr(call->pctxt, p_msgNode);

      /*TODO: This is not required ideally. We will see for some time and if
         we don't face any problems we will delete this code */
#if 0
      /* Check whether connection with remote is alright */
      if(!ooChannelsIsConnectionOK(call, call->pH225Channel->sock))
      {
         OOTRACEERR3("Error:Transport failure for signalling channel. "
                     "Abandoning message send and marking call for cleanup.(%s"
                     "'%s)\n", call->callType, call->callToken);
         if(call->callState < OO_CALL_CLEAR)
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
         call->callState = OO_CALL_CLEARED;
         return OO_OK;
      }
#endif
      /* Send message out via TCP */
      ret = ooSocketSend(call->pH225Channel->sock, msgToSend, len);
      if(ret == ASN_OK)
      {
         memFreePtr (call->pctxt, msgptr);
         OOTRACEDBGC3("H2250/Q931 Message sent successfully (%s, %s)\n",
                      call->callType, call->callToken);
         ooOnSendMsg(call, msgType, tunneledMsgType, logicalChannelNo);
         return OO_OK;
      }
      else{
         OOTRACEERR3("H2250Q931 Message send failed (%s, %s)\n",
                     call->callType, call->callToken);
         memFreePtr (call->pctxt, msgptr);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         } else if (call->callState == OO_CALL_CLEAR)
	    call->callState = OO_CALL_CLEAR_RELEASESENT;
         return OO_FAILED;
      }
   }/* end of type==OOQ931MSG */
   if(type == OOH245MSG)
   {
      if(call->pH245Channel->outQueue.count == 0)
      {
         OOTRACEWARN3("WARN:No H.245 message to send. (%s, %s)\n",
                                 call->callType, call->callToken);
         return OO_FAILED;
      }
      OOTRACEDBGA3("Sending H245 message (%s, %s)\n", call->callType,
                                                      call->callToken);
      p_msgNode = call->pH245Channel->outQueue.head;
      msgptr = (ASN1OCTET*) p_msgNode->data;
      msgType = msgptr[0];

      logicalChannelNo = msgptr[1];
      logicalChannelNo = logicalChannelNo << 8;
      logicalChannelNo = (logicalChannelNo | msgptr[2]);

      len = msgptr[3];
      len = len<<8;
      len = (len | msgptr[4]);
      /* Remove the message from queue */
      dListRemove(&(call->pH245Channel->outQueue), p_msgNode);
      if(p_msgNode)
         memFreePtr(call->pctxt, p_msgNode);

      /* Send message out */
      if (0 == call->pH245Channel && !OO_TESTFLAG(call->flags, OO_M_TUNNELING))
      {
         OOTRACEWARN3("Neither H.245 channel nor tunneling active "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr (call->pctxt, msgptr);
         /*ooCloseH245Session(call);*/
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_OK;
      }

      if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
      {
         OOTRACEDBGC4("Sending %s H245 message over H.245 channel. "
                      "(%s, %s)\n", ooGetMsgTypeText(msgType),
                      call->callType, call->callToken);

         ret = ooSocketSend(call->pH245Channel->sock, msgptr+5, len);
         if(ret == ASN_OK)
         {
            memFreePtr (call->pctxt, msgptr);
            OOTRACEDBGA3("H245 Message sent successfully (%s, %s)\n",
                          call->callType, call->callToken);
            ooOnSendMsg(call, msgType, tunneledMsgType, logicalChannelNo);
            return OO_OK;
         }
         else{
            memFreePtr (call->pctxt, msgptr);
            OOTRACEERR3("ERROR:H245 Message send failed (%s, %s)\n",
                        call->callType, call->callToken);
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_TRANSPORTFAILURE;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
      }
      else if(OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
         OOTRACEDBGC4("Sending %s H245 message as a tunneled message."
                      "(%s, %s)\n", ooGetMsgTypeText(msgType),
                      call->callType, call->callToken);

         ret = ooSendAsTunneledMessage
                  (call, msgptr+5,len,msgType, logicalChannelNo);

         if(ret != OO_OK)
         {
            memFreePtr (call->pctxt, msgptr);
            OOTRACEERR3("ERROR:Failed to tunnel H.245 message (%s, %s)\n",
                         call->callType, call->callToken);
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_INVALIDMESSAGE;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         memFreePtr (call->pctxt, msgptr);
         return OO_OK;
      }
   }
   /* Need to add support for other messages such as T38 etc */
   OOTRACEWARN3("ERROR:Unknown message type - message not Sent (%s, %s)\n",
                call->callType, call->callToken);
   return OO_FAILED;
}

int ooCloseH245Connection(OOH323CallData *call)
{
   OOTRACEINFO3("Closing H.245 connection (%s, %s)\n", call->callType,
                call->callToken);

   if (0 != call->pH245Channel)
   {
      if(0 != call->pH245Channel->sock)
         ooSocketClose (call->pH245Channel->sock);
      if (call->pH245Channel->outQueue.count > 0)
         dListFreeAll(call->pctxt, &(call->pH245Channel->outQueue));
      memFreePtr (call->pctxt, call->pH245Channel);
      call->pH245Channel = NULL;
      OOTRACEDBGC3("Closed H245 connection. (%s, %s)\n", call->callType,
                                                       call->callToken);
   }
   call->h245SessionState = OO_H245SESSION_CLOSED;

   return OO_OK;
}

int ooCloseH245Listener(OOH323CallData *call)
{
   OOTRACEINFO3("Closing H.245 Listener (%s, %s)\n", call->callType,
                call->callToken);
   if(call->h245listener)
   {
      ooSocketClose(*(call->h245listener));
      memFreePtr(call->pctxt, call->h245listener);
      call->h245listener = NULL;
   }
   return OO_OK;
}

int ooOnSendMsg
   (OOH323CallData *call, int msgType, int tunneledMsgType, int associatedChan)
{
   ooTimerCallback *cbData=NULL;
   switch(msgType)
   {
   case OOSetup:
      OOTRACEINFO3("Sent Message - Setup (%s, %s)\n", call->callType,
                    call->callToken);
      /* Start call establishment timer */
      cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_CALLESTB_TIMER;
      if(!ooTimerCreate(call->pctxt, &call->timerList, &ooCallEstbTimerExpired,
                        gH323ep.callEstablishmentTimeout, cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create call establishment timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr(call->pctxt, cbData);
         return OO_FAILED;
      }

      /* if(gH323ep.h323Callbacks.onOutgoingCall)
         gH323ep.h323Callbacks.onOutgoingCall(call); */
      break;
   case OOCallProceeding:
      OOTRACEINFO3("Sent Message - CallProceeding (%s, %s)\n", call->callType,
                    call->callToken);
      break;
   case OOAlert:
      OOTRACEINFO3("Sent Message - Alerting (%s, %s) \n", call->callType,
                    call->callToken);
      /* if(gH323ep.h323Callbacks.onAlerting && call->callState < OO_CALL_CLEAR)
         gH323ep.h323Callbacks.onAlerting(call); */
      break;
   case OOStatus:
      OOTRACEINFO3("Sent Message - Status (%s, %s) \n", call->callType,
                    call->callToken);
      break;
   case OOConnect:
      OOTRACEINFO3("Sent Message - Connect (%s, %s)\n", call->callType,
                    call->callToken);
      if(gH323ep.h323Callbacks.onCallEstablished)
         gH323ep.h323Callbacks.onCallEstablished(call);
      break;
   case OOReleaseComplete:
      OOTRACEINFO3("Sent Message - ReleaseComplete (%s, %s)\n", call->callType,
                    call->callToken);

      if(call->callState == OO_CALL_CLEAR_RELEASERECVD)
         call->callState = OO_CALL_CLEARED;
      else{
         call->callState = OO_CALL_CLEAR_RELEASESENT;
         if(gH323ep.gkClient && !OO_TESTFLAG(call->flags, OO_M_DISABLEGK) &&
            gH323ep.gkClient->state == GkClientRegistered){
            OOTRACEDBGA3("Sending DRQ after sending ReleaseComplete."
                         "(%s, %s)\n",   call->callType, call->callToken);

	    call->endTime = (H235TimeStamp) time(NULL);
            ooGkClientSendDisengageRequest(gH323ep.gkClient, call);
         }
      }

      if(call->callState == OO_CALL_CLEAR_RELEASESENT &&
         call->h245SessionState == OO_H245SESSION_IDLE)
      {
         cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                  sizeof(ooTimerCallback));
         if(!cbData)
         {
            OOTRACEERR3("Error:Unable to allocate memory for timer callback "
                        "data.(%s, %s)\n", call->callType, call->callToken);
            return OO_FAILED;
         }
         cbData->call = call;
         cbData->timerType = OO_SESSION_TIMER;
         cbData->channelNumber = 0;
         if(!ooTimerCreate(call->pctxt, &call->timerList,
             &ooSessionTimerExpired, gH323ep.sessionTimeout, cbData, FALSE))
         {
            OOTRACEERR3("Error:Unable to create EndSession timer- "
                        "ReleaseComplete.(%s, %s)\n", call->callType,
                        call->callToken);
            memFreePtr(call->pctxt, cbData);
            return OO_FAILED;
         }
      }

      if(call->h245SessionState == OO_H245SESSION_CLOSED)
      {
         call->callState = OO_CALL_CLEARED;
      }
      break;

   case OOFacility:
      if(tunneledMsgType == OOFacility)
      {
         OOTRACEINFO3("Sent Message - Facility. (%s, %s)\n",
                      call->callType, call->callToken);
      }
      else{
         OOTRACEINFO4("Sent Message - Facility(%s) (%s, %s)\n",
                      ooGetMsgTypeText(tunneledMsgType),
                      call->callType, call->callToken);

         ooOnSendMsg(call, tunneledMsgType, 0, associatedChan);
      }
      break;

   case OOMasterSlaveDetermination:
     if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
        OOTRACEINFO3("Tunneled Message - MasterSlaveDetermination (%s, %s)\n",
                      call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - MasterSlaveDetermination (%s, %s)\n",
                       call->callType, call->callToken);
       /* Start MSD timer */
      cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_MSD_TIMER;
      if(!ooTimerCreate(call->pctxt, &call->timerList, &ooMSDTimerExpired,
                        gH323ep.msdTimeout, cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create MSD timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr(call->pctxt, cbData);
         return OO_FAILED;
      }

      break;
   case OOMasterSlaveAck:
     if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - MasterSlaveDeterminationAck (%s, %s)"
                      "\n",  call->callType, call->callToken);
     else
        OOTRACEINFO3("Sent Message - MasterSlaveDeterminationAck (%s, %s)\n",
                    call->callType, call->callToken);
      break;
   case OOMasterSlaveReject:
     if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
        OOTRACEINFO3("Tunneled Message - MasterSlaveDeterminationReject "
                     "(%s, %s)\n", call->callType, call->callToken);
     else
        OOTRACEINFO3("Sent Message - MasterSlaveDeterminationReject(%s, %s)\n",
                    call->callType, call->callToken);
      break;
   case OOMasterSlaveRelease:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - MasterSlaveDeterminationRelease "
                      "(%s, %s)\n", call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - MasterSlaveDeterminationRelease "
                      "(%s, %s)\n", call->callType, call->callToken);
      break;
   case OOTerminalCapabilitySet:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING)) {
         /* If session isn't marked active yet, do it. possible in case of
            tunneling */
         if(call->h245SessionState == OO_H245SESSION_IDLE ||
            call->h245SessionState == OO_H245SESSION_PAUSED) {
            call->h245SessionState = OO_H245SESSION_ACTIVE;
         }
         OOTRACEINFO3("Tunneled Message - TerminalCapabilitySet (%s, %s)\n",
                       call->callType, call->callToken);
      }
      else {
         OOTRACEINFO3("Sent Message - TerminalCapabilitySet (%s, %s)\n",
                       call->callType, call->callToken);
      }
      /* Start TCS timer */
      cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_TCS_TIMER;
      if(!ooTimerCreate(call->pctxt, &call->timerList, &ooTCSTimerExpired,
                        gH323ep.tcsTimeout, cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create TCS timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr(call->pctxt, cbData);
         return OO_FAILED;
      }
      break;


   case OOTerminalCapabilitySetAck:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - TerminalCapabilitySetAck (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - TerminalCapabilitySetAck (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OOTerminalCapabilitySetReject:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - TerminalCapabilitySetReject "
                      "(%s, %s)\n",  call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - TerminalCapabilitySetReject (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OOOpenLogicalChannel:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO4("Tunneled Message - OpenLogicalChannel(%d). (%s, %s)\n",
                       associatedChan, call->callType, call->callToken);
      else
         OOTRACEINFO4("Sent Message - OpenLogicalChannel(%d). (%s, %s)\n",
                       associatedChan, call->callType, call->callToken);
      /* Start LogicalChannel timer */
      cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_OLC_TIMER;
      cbData->channelNumber = associatedChan;
      if(!ooTimerCreate(call->pctxt, &call->timerList,
          &ooOpenLogicalChannelTimerExpired, gH323ep.logicalChannelTimeout,
          cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create OpenLogicalChannel timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr(call->pctxt, cbData);
         return OO_FAILED;
      }

      break;
   case OOOpenLogicalChannelAck:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO4("Tunneled Message - OpenLogicalChannelAck(%d) (%s,%s)\n",
                       associatedChan, call->callType, call->callToken);
      else
         OOTRACEINFO4("Sent Message - OpenLogicalChannelAck(%d) (%s, %s)\n",
                       associatedChan, call->callType, call->callToken);
      break;
   case OOOpenLogicalChannelReject:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO4("Tunneled Message - OpenLogicalChannelReject(%d)"
                      "(%s, %s)\n", associatedChan, call->callType,
                      call->callToken);
      else
         OOTRACEINFO4("Sent Message - OpenLogicalChannelReject(%d) (%s, %s)\n",
                       associatedChan, call->callType, call->callToken);
      break;
   case OOEndSessionCommand:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - EndSessionCommand(%s, %s)\n",
                                             call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - EndSessionCommand (%s, %s)\n",
                                           call->callType, call->callToken);
      if((call->h245SessionState == OO_H245SESSION_ACTIVE))
      {
         /* Start EndSession timer */
         call->h245SessionState = OO_H245SESSION_ENDSENT;
         cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                  sizeof(ooTimerCallback));
         if(!cbData)
         {
            OOTRACEERR3("Error:Unable to allocate memory for timer callback "
                        "data.(%s, %s)\n", call->callType, call->callToken);
            return OO_FAILED;
         }
         cbData->call = call;
         cbData->timerType = OO_SESSION_TIMER;
         cbData->channelNumber = 0;
         if(!ooTimerCreate(call->pctxt, &call->timerList,
             &ooSessionTimerExpired, gH323ep.sessionTimeout, cbData, FALSE))
         {
            OOTRACEERR3("Error:Unable to create EndSession timer. "
                        "(%s, %s)\n", call->callType, call->callToken);
            memFreePtr(call->pctxt, cbData);
            return OO_FAILED;
         }
      }
      else{
         ooCloseH245Connection(call);
	 if(call->callState < OO_CALL_CLEAR)
	    call->callState = OO_CALL_CLEAR;
      }
      break;
   case OOCloseLogicalChannel:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - CloseLogicalChannel (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - CloseLogicalChannel (%s, %s)\n",
                       call->callType, call->callToken);
      /* Start LogicalChannel timer */
      cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_CLC_TIMER;
      cbData->channelNumber = associatedChan;
      if(!ooTimerCreate(call->pctxt, &call->timerList,
          &ooCloseLogicalChannelTimerExpired, gH323ep.logicalChannelTimeout,
          cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create CloseLogicalChannel timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr(call->pctxt, cbData);
         return OO_FAILED;
      }

      break;
   case OOCloseLogicalChannelAck:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - CloseLogicalChannelAck (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - CloseLogicalChannelAck (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OORequestChannelClose:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - RequestChannelClose (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - RequestChannelClose (%s, %s)\n",
                       call->callType, call->callToken);
      /* Start RequestChannelClose timer */
      cbData = (ooTimerCallback*) memAlloc(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_RCC_TIMER;
      cbData->channelNumber = associatedChan;
      if(!ooTimerCreate(call->pctxt, &call->timerList,
          &ooRequestChannelCloseTimerExpired, gH323ep.logicalChannelTimeout,
          cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create RequestChannelClose timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr(call->pctxt, cbData);
         return OO_FAILED;
      }
      break;
   case OORequestChannelCloseAck:
      if(OO_TESTFLAG (call->flags, OO_M_TUNNELING))
         OOTRACEINFO3("Tunneled Message - RequestChannelCloseAck (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - RequestChannelCloseAck (%s, %s)\n",
                       call->callType, call->callToken);
      break;

   default:
     ;
   }
   return OO_OK;
}

void ooStopMonitorCallChannels(OOH323CallData * call) {
	if (call->Monitor)
	 call->Monitor = FALSE;
	/* if (call->cmdSock)
	 ooCloseCallCmdConnection(call); */
}

int ooStopMonitorCalls()
{
   OOH323CallData * call;
   if(gMonitor)
   {
      OOTRACEINFO1("Doing ooStopMonitorCalls\n");
      if(gH323ep.cmdSock)
      {
         ooCloseCmdConnection();
      }

      if(gH323ep.callList)
      {
         OOTRACEWARN1("Warn:Abruptly ending calls as stack going down\n");
         call = gH323ep.callList;
         while(call)
         {
            OOTRACEWARN3("Clearing call (%s, %s)\n", call->callType,
                          call->callToken);
            call->callEndReason = OO_REASON_LOCAL_CLEARED;
            ooCleanCall(call);
            call = NULL;
            call = gH323ep.callList;
         }
         gH323ep.callList = NULL;
      }
      OOTRACEINFO1("Stopping listener for incoming calls\n");
      if(gH323ep.listener)
      {
         ooSocketClose(*(gH323ep.listener));
         memFreePtr(&gH323ep.ctxt, gH323ep.listener);
         gH323ep.listener = NULL;
      }

      gMonitor = FALSE;
      OOTRACEINFO1("Done ooStopMonitorCalls\n");
   }
   return OO_OK;
}

OOBOOL ooChannelsIsConnectionOK(OOH323CallData *call, OOSOCKET sock)
{
   struct timeval to = { .tv_usec = 500 };
   struct pollfd pfds = { .fd = sock, .events = POLLIN };
   int ret = 0;

   ret = ast_poll2(&pfds, 1, &to);

   if(ret == -1)
   {
      OOTRACEERR3("Error in select ...broken pipe check(%s, %s)\n",
                   call->callType, call->callToken );
      return FALSE;
   }

   if (pfds.events & POLLIN) {
      char buf[2];
      if(ooSocketRecvPeek(sock, (ASN1OCTET*) buf, 2) == 0)
      {
         OOTRACEWARN3("Broken pipe detected. (%s, %s)", call->callType,
                       call->callToken);
         if(call->callState < OO_CALL_CLEAR)
            call->callEndReason = OO_REASON_TRANSPORTFAILURE;
         call->callState = OO_CALL_CLEARED;
         return FALSE;
      }
   }
   return TRUE;
}
