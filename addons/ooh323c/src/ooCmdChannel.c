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

#include "ooStackCmds.h"
#include "ootrace.h"
#include "ooq931.h"
#include "ooh245.h"
#include "ooh323ep.h"
#include "oochannels.h"
#include "ooCalls.h"
#include "ooCmdChannel.h"

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

OOSOCKET gCmdChan = 0;
pthread_mutex_t gCmdChanLock;

int ooCreateCmdConnection()
{
   int ret = 0;
   int thePipe[2];

   if ((ret = pipe(thePipe)) == -1) {
      return OO_FAILED;
   }
   pthread_mutex_init(&gCmdChanLock, NULL);

   gH323ep.cmdSock = dup(thePipe[0]);
   close(thePipe[0]);
   gCmdChan = dup(thePipe[1]);
   close(thePipe[1]);
   return OO_OK;
}


int ooCloseCmdConnection()
{
   close(gH323ep.cmdSock);
   gH323ep.cmdSock = 0;
   close(gCmdChan);
   gCmdChan = 0;
   pthread_mutex_destroy(&gCmdChanLock);

   return OO_OK;
}

int ooWriteStackCommand(OOStackCommand *cmd)
{

	pthread_mutex_lock(&gCmdChanLock);
   	if (write(gCmdChan, (char*)cmd, sizeof(OOStackCommand)) == -1) {
		pthread_mutex_unlock(&gCmdChanLock);
		return OO_FAILED;
	}
	pthread_mutex_unlock(&gCmdChanLock);
   
   	return OO_OK;
}


int ooReadAndProcessStackCommand()
{
   OOH323CallData *pCall = NULL;   
   unsigned char buffer[MAXMSGLEN];
   int i, recvLen = 0;
   OOStackCommand cmd;
   memset(&cmd, 0, sizeof(OOStackCommand));
   recvLen = read(gH323ep.cmdSock, buffer, MAXMSGLEN);
   if(recvLen <= 0)
   {
      OOTRACEERR1("Error:Failed to read CMD message\n");
      return OO_FAILED;
   }

   for(i=0; (int)(i+sizeof(OOStackCommand)) <= recvLen; i += sizeof(OOStackCommand))
   {
      memcpy(&cmd, buffer+i, sizeof(OOStackCommand));

      if(cmd.type == OO_CMD_NOOP)
         continue;

      if(gH323ep.gkClient && gH323ep.gkClient->state != GkClientRegistered)
      {
         OOTRACEINFO1("Ignoring stack command as Gk Client is not registered"
                      " yet\n");
      }
      else {
         switch(cmd.type) {
            case OO_CMD_MAKECALL: 
               OOTRACEINFO2("Processing MakeCall command %s\n", 
                                    (char*)cmd.param2);
 
               ooH323MakeCall ((char*)cmd.param1, (char*)cmd.param2, 
                               (ooCallOptions*)cmd.param3);
               break;

            case OO_CMD_MANUALRINGBACK:
               if(OO_TESTFLAG(gH323ep.flags, OO_M_MANUALRINGBACK))
               {
                  pCall = ooFindCallByToken((char*)cmd.param1);
                  if(!pCall) {
                     OOTRACEINFO2("Call \"%s\" does not exist\n",
                                  (char*)cmd.param1);
                     OOTRACEINFO1("Call migth be cleared/closed\n");
                  }
                  else {
                     ooSendAlerting(ooFindCallByToken((char*)cmd.param1));
                     if(OO_TESTFLAG(gH323ep.flags, OO_M_AUTOANSWER)) {
                        ooSendConnect(ooFindCallByToken((char*)cmd.param1));
                     }
                  }
               }
               break;
 
            case OO_CMD_ANSCALL:
               pCall = ooFindCallByToken((char*)cmd.param1);
               if(!pCall) {
                  OOTRACEINFO2("Call \"%s\" does not exist\n",
                               (char*)cmd.param1);
                  OOTRACEINFO1("Call might be cleared/closed\n");
               }
               else {
                  OOTRACEINFO2("Processing Answer Call command for %s\n",
                               (char*)cmd.param1);
                  ooSendConnect(pCall);
               }
               break;

            case OO_CMD_FWDCALL:
               OOTRACEINFO3("Forwarding call %s to %s\n", (char*)cmd.param1,
                                                          (char*)cmd.param2);
               ooH323ForwardCall((char*)cmd.param1, (char*)cmd.param2);
               break;

            case OO_CMD_HANGCALL: 
               OOTRACEINFO2("Processing Hang call command %s\n", 
                             (char*)cmd.param1);
               ooH323HangCall((char*)cmd.param1, 
                              *(OOCallClearReason*)cmd.param2);
               break;
          
            case OO_CMD_SENDDIGIT:
               pCall = ooFindCallByToken((char*)cmd.param1);
               if(!pCall) {
                  OOTRACEERR2("ERROR:Invalid calltoken %s\n",
                              (char*)cmd.param1);
                  break;
               }
               if(pCall->jointDtmfMode & OO_CAP_DTMF_H245_alphanumeric) {
                  ooSendH245UserInputIndication_alphanumeric(
                     pCall, (const char*)cmd.param2);
               }
               else if(pCall->jointDtmfMode & OO_CAP_DTMF_H245_signal) {
                  ooSendH245UserInputIndication_signal(
                     pCall, (const char*)cmd.param2);
               }
               else {
                  ooQ931SendDTMFAsKeyPadIE(pCall, (const char*)cmd.param2);
               }

               break;

            case OO_CMD_STOPMONITOR: 
               OOTRACEINFO1("Processing StopMonitor command\n");
               ooStopMonitorCalls();
               break;

            default: OOTRACEERR1("ERROR:Unknown command\n");
         }
      }
      if(cmd.param1) free(cmd.param1);
      if(cmd.param2) free(cmd.param2);
      if(cmd.param3) free(cmd.param3);
   }


   return OO_OK;
}
   
