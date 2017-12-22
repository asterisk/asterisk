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
#include "ooh323ep.h"
#include "ooCalls.h"
#include "ooCmdChannel.h"

extern OOSOCKET gCmdChan;
extern ast_mutex_t newCallLock;

static int counter = 1;

int ooGenerateOutgoingCallToken (char *callToken, size_t size)
{
   char aCallToken[200];
   int  ret = 0;

   ast_mutex_lock(&newCallLock);
   sprintf (aCallToken, "ooh323c_o_%d", counter++);

   if (counter > OO_MAX_CALL_TOKEN)
      counter = 1;
   ast_mutex_unlock(&newCallLock);

   if ((strlen(aCallToken)+1) < size)
      strcpy (callToken, aCallToken);
   else {
      ret = OO_FAILED;
   }

   return ret;
}

int isRunning(char *callToken) {
  OOH323CallData *call;

  if((call = ooFindCallByToken(callToken)))
   if (call->Monitor)
    return 1;
  return 0;
}

OOStkCmdStat ooMakeCall
   (const char* dest, char* callToken, size_t bufsiz, ooCallOptions *opts)
{
   OOStackCommand cmd;

   if(!callToken)
      return OO_STKCMD_INVALIDPARAM;


   /* Generate call token*/
   if (ooGenerateOutgoingCallToken (callToken, bufsiz) != OO_OK){
      return OO_STKCMD_INVALIDPARAM;
   }

   if(gCmdChan == 0)
   {
      if(ooCreateCmdConnection() != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_MAKECALL;
   cmd.param1 = ast_malloc(strlen(dest)+1);
   if(!cmd.param1)
   {
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, dest);


   cmd.param2 = ast_malloc(strlen(callToken)+1);
   if(!cmd.param2)
   {
      ast_free(cmd.param1);
      return OO_STKCMD_MEMERR;
   }

   strcpy((char*)cmd.param2, callToken);

   if(!opts)
   {
      cmd.param3 = 0;
   }
   else {
      cmd.param3 = ast_malloc(sizeof(ooCallOptions));
      if(!cmd.param3)
      {
         ast_free(cmd.param1);
         ast_free(cmd.param2);
         return OO_STKCMD_MEMERR;
      }
      memcpy((void*)cmd.param3, opts, sizeof(ooCallOptions));
   }

   if(ooWriteStackCommand(&cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      if(cmd.param3) ast_free(cmd.param3);
      return OO_STKCMD_WRITEERR;
   }

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooRunCall
   (const char* dest, char* callToken, size_t bufsiz, ooCallOptions *opts)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   if(!callToken)
      return OO_STKCMD_INVALIDPARAM;


   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_MAKECALL;
   cmd.param1 = ast_malloc(strlen(dest)+1);
   if(!cmd.param1)
   {
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, dest);
   cmd.plen1 = strlen(dest);


   cmd.param2 = ast_malloc(strlen(callToken)+1);
   if(!cmd.param2)
   {
      ast_free(cmd.param1);
      return OO_STKCMD_MEMERR;
   }

   strcpy((char*)cmd.param2, callToken);
   cmd.plen2 = strlen(callToken);

   if(!opts)
   {
      cmd.param3 = 0;
   }
   else {
      cmd.param3 = ast_malloc(sizeof(ooCallOptions));
      if(!cmd.param3)
      {
         ast_free(cmd.param1);
         ast_free(cmd.param2);
         return OO_STKCMD_MEMERR;
      }
      memcpy((void*)cmd.param3, opts, sizeof(ooCallOptions));
      cmd.plen3 = sizeof(ooCallOptions);
   }

   if(ooWriteCallStackCommand(call, &cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      if(cmd.param3) ast_free(cmd.param3);
      return OO_STKCMD_WRITEERR;
   }


   ast_free(cmd.param1);
   ast_free(cmd.param2);
   if(cmd.param3) ast_free(cmd.param3);

   return OO_STKCMD_SUCCESS;
}


OOStkCmdStat ooManualRingback(const char *callToken)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_MANUALRINGBACK;
   cmd.param1 = ast_malloc(strlen(callToken)+1);
   if(!cmd.param1)
   {
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);

   if(ooWriteCallStackCommand(call,&cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      return OO_STKCMD_WRITEERR;
   }

   ast_free(cmd.param1);

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooManualProgress(const char *callToken)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   if (call->h225version < 4)
      return OO_STKCMD_SUCCESS;

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_MANUALPROGRESS;
   cmd.param1 = ast_malloc(strlen(callToken)+1);
   if(!cmd.param1)
   {
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);

   if(ooWriteCallStackCommand(call, &cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      return OO_STKCMD_WRITEERR;
   }

   ast_free(cmd.param1);

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooAnswerCall(const char *callToken)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_ANSCALL;

   cmd.param1 = ast_malloc(strlen(callToken)+1);
   if(!cmd.param1)
   {
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);

   if(ooWriteCallStackCommand(call, &cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      return OO_STKCMD_WRITEERR;
   }

   ast_free(cmd.param1);

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooForwardCall(const char* callToken, char *dest)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken || !dest)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }
   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_FWDCALL;

   cmd.param1 = ast_malloc(strlen(callToken)+1);
   cmd.param2 = ast_malloc(strlen(dest)+1);
   if(!cmd.param1 || !cmd.param2)
   {
      if(cmd.param1)   ast_free(cmd.param1);  /* Release memory */
      if(cmd.param2)   ast_free(cmd.param2);
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);
   strcpy((char*)cmd.param2, dest);
   cmd.plen2 = strlen(dest);

   if(ooWriteCallStackCommand(call, &cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      return OO_STKCMD_WRITEERR;
   }
   ast_free(cmd.param1);
   ast_free(cmd.param2);

   return OO_STKCMD_SUCCESS;
}


OOStkCmdStat ooHangCall(const char* callToken, OOCallClearReason reason, int q931cause)
{
   OOStackCommand cmd;

   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_HANGCALL;
   cmd.param1 = ast_malloc(strlen(callToken)+1);
   cmd.param2 = ast_malloc(sizeof(OOCallClearReason));
   cmd.param3 = ast_malloc(sizeof(int));
   if(!cmd.param1 || !cmd.param2 || !cmd.param3)
   {
      if(cmd.param1)   ast_free(cmd.param1); /* Release memory */
      if(cmd.param2)   ast_free(cmd.param2);
      if(cmd.param3)   ast_free(cmd.param3);
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);
   *((OOCallClearReason*)cmd.param2) = reason;
   cmd.plen2 = sizeof(OOCallClearReason);
   *(int *)cmd.param3 = q931cause;
   cmd.plen3 = sizeof(int);

   if(ooWriteCallStackCommand(call, &cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      ast_free(cmd.param3);
      return OO_STKCMD_WRITEERR;
   }
   ast_free(cmd.param1);
   ast_free(cmd.param2);
   ast_free(cmd.param3);

   return OO_STKCMD_SUCCESS;
}


OOStkCmdStat ooStopMonitor()
{
   OOStackCommand cmd;

   if(gCmdChan == 0)
   {
      if(ooCreateCmdConnection() != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_STOPMONITOR;

   if(ooWriteStackCommand(&cmd) != OO_OK)
      return OO_STKCMD_WRITEERR;

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooSendDTMFDigit(const char *callToken, const char* dtmf)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_SENDDIGIT;

   cmd.param1 = ast_malloc(strlen(callToken)+1);
   cmd.param2 = ast_malloc(strlen(dtmf)+1);
   if(!cmd.param1 || !cmd.param2)
   {
      if(cmd.param1)   ast_free(cmd.param1); /* Release memory */
      if(cmd.param2)   ast_free(cmd.param2);
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);
   strcpy((char*)cmd.param2, dtmf);
   cmd.plen2 = strlen(dtmf);

   if(ooWriteCallStackCommand(call,&cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      return OO_STKCMD_WRITEERR;
   }
   ast_free(cmd.param1);
   ast_free(cmd.param2);

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooSetANI(const char *callToken, const char* ani)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_SETANI;

   cmd.param1 = ast_malloc(strlen(callToken)+1);
   cmd.param2 = ast_malloc(strlen(ani)+1);
   if(!cmd.param1 || !cmd.param2)
   {
      if(cmd.param1)   ast_free(cmd.param1); /* Release memory */
      if(cmd.param2)   ast_free(cmd.param2);
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);
   strcpy((char*)cmd.param2, ani);
   cmd.plen2 = strlen(ani);

   if(ooWriteCallStackCommand(call,&cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      return OO_STKCMD_WRITEERR;
   }
   ast_free(cmd.param1);
   ast_free(cmd.param2);

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooUpdateLogChannels(const char *callToken, const char* localIP, int port)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if (!callToken) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if (!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if (localIP == NULL) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if (call->CmdChan == 0) {
      if (ooCreateCallCmdConnection(call) != OO_OK) {
         return OO_STKCMD_CONNECTIONERR;
      }
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_UPDLC;

   cmd.param1 = ast_malloc(strlen(callToken) + 1);
   cmd.param2 = ast_malloc(strlen(localIP) + 1);
   cmd.param3 = ast_malloc(sizeof(int) + 1);
   if (!cmd.param1 || !cmd.param2 || !cmd.param3) {
      if (cmd.param1) {
	ast_free(cmd.param1); /* Release memory */
      }
      if (cmd.param2) {
	ast_free(cmd.param2);
      }
      if (cmd.param3) {
	ast_free(cmd.param3);
      }
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);
   strcpy((char*)cmd.param2, localIP);
   cmd.plen2 = strlen(localIP);
   *((int *)cmd.param3) = port;
   cmd.plen3 = sizeof(int) + 1;

   if (ooWriteCallStackCommand(call, &cmd) != OO_OK) {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      ast_free(cmd.param3);
      return OO_STKCMD_WRITEERR;
   }

   ast_free(cmd.param1);
   ast_free(cmd.param2);
   ast_free(cmd.param3);

   return OO_STKCMD_SUCCESS;
}

OOStkCmdStat ooRequestChangeMode(const char *callToken, int isT38Mode)
{
   OOStackCommand cmd;
   OOH323CallData *call;

   if(!callToken)
   {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(!(call = ooFindCallByToken(callToken))) {
      return OO_STKCMD_INVALIDPARAM;
   }

   if(call->CmdChan == 0)
   {
      if(ooCreateCallCmdConnection(call) != OO_OK)
         return OO_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(OOStackCommand));
   cmd.type = OO_CMD_REQMODE;

   cmd.param1 = ast_malloc(strlen(callToken)+1);
   cmd.param2 = ast_malloc(sizeof(int));
   if(!cmd.param1 || !cmd.param2)
   {
      ast_free(cmd.param1); /* Release memory */
      ast_free(cmd.param2);
      return OO_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   cmd.plen1 = strlen(callToken);
   *((int *) cmd.param2) = isT38Mode;
   cmd.plen2 = sizeof(int);

   if(ooWriteCallStackCommand(call,&cmd) != OO_OK)
   {
      ast_free(cmd.param1);
      ast_free(cmd.param2);
      return OO_STKCMD_WRITEERR;
   }
   ast_free(cmd.param1);
   ast_free(cmd.param2);

   return OO_STKCMD_SUCCESS;
}

const char* ooGetStkCmdStatusCodeTxt(OOStkCmdStat stat)
{
   switch(stat)
   {
      case OO_STKCMD_SUCCESS:
         return "Stack command - successfully issued";

      case OO_STKCMD_MEMERR:
         return "Stack command - Memory allocation error";

      case OO_STKCMD_INVALIDPARAM:
         return "Stack command - Invalid parameter";

      case OO_STKCMD_WRITEERR:
         return "Stack command - write error";

      case OO_STKCMD_CONNECTIONERR:
         return "Stack command - Failed to create command channel";

      default:
         return "Invalid status code";
   }
}
