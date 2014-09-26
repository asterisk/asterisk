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
#include "asterisk/utils.h"
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
ast_mutex_t gCmdChanLock;

int ooCreateCmdConnection()
{
   int ret = 0;
   int thePipe[2];

   if ((ret = pipe(thePipe)) == -1) {
      return OO_FAILED;
   }
   ast_mutex_init(&gCmdChanLock);

   gH323ep.cmdSock = dup(thePipe[0]);
   close(thePipe[0]);
   gCmdChan = dup(thePipe[1]);
   close(thePipe[1]);
   return OO_OK;
}

int ooCreateCallCmdConnection(OOH323CallData* call)
{
   int ret = 0;
   int thePipe[2];

    OOTRACEINFO2("INFO: create cmd connect for call: %lx\n", call);

   call->CmdChanLock = ast_calloc(1, sizeof(ast_mutex_t));
   ast_mutex_init(call->CmdChanLock);


   if ((ret = socketpair(PF_LOCAL, SOCK_STREAM, 0, thePipe)) == -1) {
      ast_mutex_destroy(call->CmdChanLock);
      ast_free(call->CmdChanLock);
      call->CmdChanLock = NULL;
      return OO_FAILED;
   }
   ast_mutex_lock(call->CmdChanLock);
   call->cmdSock = thePipe[0];
   call->CmdChan = thePipe[1];
   ast_mutex_unlock(call->CmdChanLock);
   return OO_OK;
}


int ooCloseCmdConnection()
{
   close(gH323ep.cmdSock);
   gH323ep.cmdSock = 0;
   close(gCmdChan);
   gCmdChan = 0;
   ast_mutex_destroy(&gCmdChanLock);

   return OO_OK;
}
int ooCloseCallCmdConnection(OOH323CallData* call)
{
   ast_mutex_lock(call->CmdChanLock);
   close(call->cmdSock);
   call->cmdSock = 0;
   close(call->CmdChan);
   call->CmdChan = 0;
   ast_mutex_unlock(call->CmdChanLock);
   ast_mutex_destroy(call->CmdChanLock);
   ast_free(call->CmdChanLock);
   call->CmdChanLock = NULL;

   return OO_OK;
}

int ooWriteStackCommand(OOStackCommand *cmd)
{

	ast_mutex_lock(&gCmdChanLock);
   	if (write(gCmdChan, (char*)cmd, sizeof(OOStackCommand)) == -1) {
		ast_mutex_unlock(&gCmdChanLock);
		return OO_FAILED;
	}
	ast_mutex_unlock(&gCmdChanLock);
   
   	return OO_OK;
}
int ooWriteCallStackCommand(OOH323CallData* call, OOStackCommand *cmd)
{
	unsigned char buffer[MAXMSGLEN];
	unsigned char* bPoint;

	memcpy(buffer, cmd,  sizeof(OOStackCommand));
	bPoint = buffer +  sizeof(OOStackCommand);
	if (cmd->param1 && cmd->plen1 > 0) {
		if (bPoint + cmd->plen1 >= buffer + MAXMSGLEN)
			return OO_FAILED;
		memcpy(bPoint, cmd->param1, cmd->plen1);
		bPoint += cmd->plen1;
	}
	if (cmd->param2 && cmd->plen2 > 0) {
		if (bPoint + cmd->plen2 >= buffer + MAXMSGLEN)
			return OO_FAILED;
		memcpy(bPoint, cmd->param2, cmd->plen2);
		bPoint += cmd->plen2;
	}
	if (cmd->param3 && cmd->plen3 > 0) {
		if (bPoint + cmd->plen3 >= buffer + MAXMSGLEN)
			return OO_FAILED;
		memcpy(bPoint, cmd->param3, cmd->plen3);
		bPoint += cmd->plen3;
	}

	ast_mutex_lock(call->CmdChanLock);
   	if (write(call->CmdChan, buffer, bPoint - buffer) == -1) {
		ast_mutex_unlock(call->CmdChanLock);
		return OO_FAILED;
	}
	ast_mutex_unlock(call->CmdChanLock);
   
   	return OO_OK;
}


int ooReadAndProcessStackCommand()
{
   OOH323CallData *pCall = NULL;   
   unsigned char buffer[MAXMSGLEN];
   int i, recvLen = 0;
   OOStackCommand cmd;
   memset(&cmd, 0, sizeof(OOStackCommand));
   ast_mutex_lock(&gCmdChanLock);
   recvLen = read(gH323ep.cmdSock, buffer, MAXMSGLEN);
   ast_mutex_unlock(&gCmdChanLock);
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

      else {
         switch(cmd.type) {
            case OO_CMD_MAKECALL: 
               OOTRACEINFO2("Processing MakeCall command %s\n", 
                                    (char*)cmd.param2);
 
               ooH323NewCall ((char*)cmd.param2);
               break;

            case OO_CMD_MANUALPROGRESS:
                pCall = ooFindCallByToken((char*)cmd.param1);
                if(!pCall) {
                   OOTRACEINFO2("Call \"%s\" does not exist\n",
                                (char*)cmd.param1);
                   OOTRACEINFO1("Call migth be cleared/closed\n");
                }
                else {
                     ooSendProgress(ooFindCallByToken((char*)cmd.param1));
                }
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
               OOTRACEINFO3("Processing Hang call command %s with q931 cause %d\n", 
                             (char*)cmd.param1, *(int *) cmd.param3);
               ooH323HangCall((char*)cmd.param1, 
                              *(OOCallClearReason*)cmd.param2, *(int *) cmd.param3);
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
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      ast_free(cmd.param3);
   }


   return OO_OK;
}
int ooReadAndProcessCallStackCommand(OOH323CallData* call)
{
   unsigned char buffer[MAXMSGLEN];
   unsigned char *bPoint;
   int recvLen = 0;
   OOStackCommand cmd;
   memset(&cmd, 0, sizeof(OOStackCommand));
   if (call->CmdChanLock) {
    ast_mutex_lock(call->CmdChanLock);
    recvLen = read(call->cmdSock, buffer, MAXMSGLEN);
    ast_mutex_unlock(call->CmdChanLock);
   } else {
    recvLen = read(call->cmdSock, buffer, MAXMSGLEN);
   }
   if(recvLen <= 0)
   {
      OOTRACEERR1("Error:Failed to read CMD message\n");
      return OO_FAILED;
   }

   bPoint = buffer;
   while (bPoint  < buffer + recvLen - sizeof(OOStackCommand)) {

      memcpy(&cmd, bPoint, sizeof(OOStackCommand));
      bPoint +=  sizeof(OOStackCommand);

      if (cmd.plen1 > 0) {
	cmd.param1 = ast_malloc(cmd.plen1 + 1);
	if (!cmd.param1) 
		return OO_FAILED;
	memset(cmd.param1, 0, cmd.plen1 + 1);
	memcpy(cmd.param1, bPoint, cmd.plen1);
	bPoint += cmd.plen1;
      }

      if (cmd.plen2 > 0) {
	cmd.param2 = ast_malloc(cmd.plen2 + 1);
	if (!cmd.param2) 
		return OO_FAILED;
	memset(cmd.param2, 0, cmd.plen2 + 1);
	memcpy(cmd.param2, bPoint, cmd.plen2);
	bPoint += cmd.plen2;
      }

      if (cmd.plen3 > 0) {
	cmd.param3 = ast_malloc(cmd.plen3 + 1);
	if (!cmd.param3) 
		return OO_FAILED;
	memset(cmd.param3, 0, cmd.plen3 + 1);
	memcpy(cmd.param3, bPoint, cmd.plen3);
	bPoint += cmd.plen3;
      }

      if(cmd.type == OO_CMD_NOOP)
         continue;

      else {
         switch(cmd.type) {
            case OO_CMD_MAKECALL: 
               OOTRACEINFO2("Processing MakeCall command %s\n", 
                                    (char*)cmd.param2);
 
               ooH323MakeCall ((char*)cmd.param1, (char*)cmd.param2, 
                               (ooCallOptions*)cmd.param3);
               break;

            case OO_CMD_MANUALPROGRESS:
               ooSendProgress(call);
               break;

            case OO_CMD_MANUALRINGBACK:
               if(OO_TESTFLAG(gH323ep.flags, OO_M_MANUALRINGBACK))
               {
                 ooSendAlerting(call);
                 if(OO_TESTFLAG(gH323ep.flags, OO_M_AUTOANSWER)) {
                   ooSendConnect(call);
                 }
               }
               break;
 
            case OO_CMD_ANSCALL:
                  ooSendConnect(call);
               break;

            case OO_CMD_FWDCALL:
               OOTRACEINFO3("Forwarding call %s to %s\n", (char*)cmd.param1,
                                                          (char*)cmd.param2);
               ooH323ForwardCall((char*)cmd.param1, (char*)cmd.param2);
               break;

            case OO_CMD_HANGCALL: 
               OOTRACEINFO2("Processing Hang call command %s with q931 cause %d\n", 
                             (char*)cmd.param1);
               ooH323HangCall((char*)cmd.param1, 
                              *(OOCallClearReason*)cmd.param2, *(int *) cmd.param3);
               break;
          
            case OO_CMD_SENDDIGIT:
               if(call->jointDtmfMode & OO_CAP_DTMF_H245_alphanumeric) {
                  ooSendH245UserInputIndication_alphanumeric(
                     call, (const char*)cmd.param2);
               }
               else if(call->jointDtmfMode & OO_CAP_DTMF_H245_signal) {
                  ooSendH245UserInputIndication_signal(
                     call, (const char*)cmd.param2);
               }
               else {
                  ooQ931SendDTMFAsKeyPadIE(call, (const char*)cmd.param2);
               }

               break;

	    case OO_CMD_REQMODE:
	       OOTRACEINFO3("Processing RequestMode command %s, requested mode is %d\n",
				(char *)cmd.param1, *(int *)cmd.param2);
	       ooSendRequestMode(call, *(int *)cmd.param2);
	       break;

	    case OO_CMD_SETANI:
		OOTRACEINFO3("Processing SetANI command %s, ani is %s\n",
				(char *)cmd.param1, (char *)cmd.param2);
   		if(cmd.param2) {
     			strncpy(call->ourCallerId, cmd.param2, sizeof(call->ourCallerId)-1);
     			call->ourCallerId[sizeof(call->ourCallerId)-1] = '\0';
   		}
		break;

	    case OO_CMD_UPDLC:
		OOTRACEINFO4("Processing UpdLC command %s, localIP is %s, port is %d\n",
				(char *)cmd.param1, (char *)cmd.param2, *(int *)cmd.param3);
		if (cmd.param2) {
			ooUpdateAllLogicalChannels(call, (char *)cmd.param2, *(int *)cmd.param3);
		}
		break;

            default: OOTRACEERR1("ERROR:Unknown command\n");
         }
      }
      if (cmd.param1) {
	ast_free(cmd.param1);
      }
      if (cmd.param2) {
	ast_free(cmd.param2);
      }
      if (cmd.param3) {
	ast_free(cmd.param3);
      }
   }


   return OO_OK;
}
   
