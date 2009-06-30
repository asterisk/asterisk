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

#include "ooh323cDriver.h"

#include <asterisk/pbx.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>

extern OOBOOL gH323Debug;
/* ooh323c stack thread. */
static pthread_t ooh323c_thread = AST_PTHREADT_NULL;
static int grxframes = 240;

static int gtxframes = 20;

int ooh323c_start_receive_channel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_start_transmit_channel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_stop_receive_channel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_stop_transmit_channel(ooCallData *call, ooLogicalChannel *pChannel);

void* ooh323c_stack_thread(void* dummy)
{

  ooMonitorChannels();
  return dummy;
}

int ooh323c_start_stack_thread()
{
   if(ast_pthread_create(&ooh323c_thread, NULL, ooh323c_stack_thread, NULL) < 0)
   {
      ast_log(LOG_ERROR, "Unable to start ooh323c thread.\n");
      return -1;
   }
   return 0;
}

int ooh323c_stop_stack_thread(void)
{
   if(ooh323c_thread !=  AST_PTHREADT_NULL)
   {
      ooStopMonitor();
      pthread_join(ooh323c_thread, NULL);
      ooh323c_thread =  AST_PTHREADT_NULL;
   }
   return 0;
}

int ooh323c_set_capability
   (struct ast_codec_pref *prefs, int capability, int dtmf)
{
   int ret, x, format=0;
   if(gH323Debug)
     ast_verbose("\tAdding capabilities to H323 endpoint\n");
   
   for(x=0; 0 != (format=ast_codec_pref_index(prefs, x)); x++)
   {
      if(format & AST_FORMAT_ULAW)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g711 ulaw capability to H323 endpoint\n");
         ret= ooH323EpAddG711Capability(OO_G711ULAW64K, gtxframes, grxframes, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);
      }
      if(format & AST_FORMAT_ALAW)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g711 alaw capability to H323 endpoint\n");
         ret= ooH323EpAddG711Capability(OO_G711ALAW64K, gtxframes, grxframes, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);
      }

      if(format & AST_FORMAT_G729A)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g729A capability to H323 endpoint\n");
         ret = ooH323EpAddG729Capability(OO_G729A, 2, 24, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);

         if(gH323Debug)
            ast_verbose("\tAdding g729 capability to H323 endpoint\n");
         ret |= ooH323EpAddG729Capability(OO_G729, 2, 24, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);
      }

      if(format & AST_FORMAT_G723_1)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g7231 capability to H323 endpoint\n");
         ret = ooH323EpAddG7231Capability(OO_G7231, 4, 7, FALSE, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);

      }

      if(format & AST_FORMAT_H263)
      {
         if(gH323Debug)
            ast_verbose("\tAdding h263 capability to H323 endpoint\n");
         ret = ooH323EpAddH263VideoCapability(OO_H263VIDEO, 1, 0, 0, 0, 0, 320*1024, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);

      }

      if(format & AST_FORMAT_GSM)
      {
         if(gH323Debug)
            ast_verbose("\tAdding gsm capability to H323 endpoint\n");
         ret = ooH323EpAddGSMCapability(OO_GSMFULLRATE, 4, FALSE, FALSE, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);

      }
      
   }
   
   if(dtmf & H323_DTMF_RFC2833)
      ret |= ooH323EpEnableDTMFRFC2833(0);
   else if(dtmf & H323_DTMF_H245ALPHANUMERIC)
      ret |= ooH323EpEnableDTMFH245Alphanumeric();
   else if(dtmf & H323_DTMF_H245SIGNAL)
      ret |= ooH323EpEnableDTMFH245Signal();

   return ret;
}

int ooh323c_set_capability_for_call
   (ooCallData *call, struct ast_codec_pref *prefs, int capability, int dtmf)
{
   int ret, x, txframes;
   int format=0;
   if(gH323Debug)
     ast_verbose("\tAdding capabilities to call(%s, %s)\n", call->callType, 
                                                            call->callToken);
   if(dtmf & H323_DTMF_RFC2833)
      ret |= ooCallEnableDTMFRFC2833(call,0);
   else if(dtmf & H323_DTMF_H245ALPHANUMERIC)
      ret |= ooCallEnableDTMFH245Alphanumeric(call);
   else if(dtmf & H323_DTMF_H245SIGNAL)
      ret |= ooCallEnableDTMFH245Signal(call);


   for(x=0; 0 !=(format=ast_codec_pref_index(prefs, x)); x++)
   {
      if(format & AST_FORMAT_ULAW)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g711 ulaw capability to call(%s, %s)\n", 
                                              call->callType, call->callToken);
	 txframes = prefs->framing[x];
         ret= ooCallAddG711Capability(call, OO_G711ULAW64K, txframes, 
                                      grxframes, OORXANDTX, 
                                      &ooh323c_start_receive_channel,
                                      &ooh323c_start_transmit_channel,
                                      &ooh323c_stop_receive_channel, 
                                      &ooh323c_stop_transmit_channel);
      }
      if(format & AST_FORMAT_ALAW)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g711 alaw capability to call(%s, %s)\n",
                                            call->callType, call->callToken);
         txframes = prefs->framing[x];
         ret= ooCallAddG711Capability(call, OO_G711ALAW64K, txframes, 
                                     grxframes, OORXANDTX, 
                                     &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);
      }

      if(format & AST_FORMAT_G729A)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g729A capability to call(%s, %s)\n",
                                            call->callType, call->callToken);
         txframes = (prefs->framing[x])/10;
         ret= ooCallAddG729Capability(call, OO_G729A, txframes, 24, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);

         if(gH323Debug)
            ast_verbose("\tAdding g729 capability to call(%s, %s)\n",
                                            call->callType, call->callToken);
         ret|= ooCallAddG729Capability(call, OO_G729, txframes, 24, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);
      }

      if(format & AST_FORMAT_G723_1)
      {
         if(gH323Debug)
            ast_verbose("\tAdding g7231 capability to call (%s, %s)\n",
                                           call->callType, call->callToken);
         ret = ooCallAddG7231Capability(call, OO_G7231, 4, 7, FALSE, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);

      }

      if(format & AST_FORMAT_H263)
      {
         if(gH323Debug)
            ast_verbose("\tAdding h263 capability to call (%s, %s)\n",
                                           call->callType, call->callToken);
         ret = ooCallAddH263VideoCapability(call, OO_H263VIDEO, 1, 0, 0, 0, 0, 320*1024, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);

      }

      if(format & AST_FORMAT_GSM)
      {
         if(gH323Debug)
            ast_verbose("\tAdding gsm capability to call(%s, %s)\n", 
                                             call->callType, call->callToken);
         ret = ooCallAddGSMCapability(call, OO_GSMFULLRATE, 4, FALSE, FALSE, 
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel, 
                                     &ooh323c_stop_transmit_channel);
      }
   }
}

int ooh323c_set_aliases(ooAliases * aliases)
{
   ooAliases *cur = aliases;
   while(cur)
   {
      switch(cur->type)
      { 
      case T_H225AliasAddress_dialedDigits:
         ooH323EpAddAliasDialedDigits(cur->value);
         break;
      case T_H225AliasAddress_h323_ID:
         ooH323EpAddAliasH323ID(cur->value);
         break;
      case T_H225AliasAddress_url_ID:
         ooH323EpAddAliasURLID(cur->value);
         break;
      case T_H225AliasAddress_email_ID:
         ooH323EpAddAliasEmailID(cur->value);
         break;
      default:
         ast_debug(1, "Ignoring unknown alias type\n");
      }
      cur = cur->next;
   }
   return 1;
}
   
int ooh323c_start_receive_channel(ooCallData *call, ooLogicalChannel *pChannel)
{
   int fmt=-1;
   fmt = convertH323CapToAsteriskCap(pChannel->chanCap->cap);
   if(fmt>0)
      ooh323_set_read_format(call, fmt);
   else{
     ast_log(LOG_ERROR, "Invalid capability type for receive channel %s\n",
                                                          call->callToken);
     return -1;
   }
   return 1;
}

int ooh323c_start_transmit_channel(ooCallData *call, ooLogicalChannel *pChannel)
{
   int fmt=-1;
   fmt = convertH323CapToAsteriskCap(pChannel->chanCap->cap);
   if(fmt>0)
      ooh323_set_write_format(call, fmt);
   else{
      ast_log(LOG_ERROR, "Invalid capability type for receive channel %s\n",
                                                          call->callToken);
      return -1;
   }
   setup_rtp_connection(call, pChannel->remoteIP, pChannel->remoteMediaPort);
    return 1;
}

int ooh323c_stop_receive_channel(ooCallData *call, ooLogicalChannel *pChannel)
{
   return 1;
}

int ooh323c_stop_transmit_channel(ooCallData *call, ooLogicalChannel *pChannel)
{
   close_rtp_connection(call);
   return 1;
}

int convertH323CapToAsteriskCap(int cap)
{

   switch(cap)
   {
      case OO_G711ULAW64K:
         return AST_FORMAT_ULAW;
      case OO_G711ALAW64K:
         return AST_FORMAT_ALAW;
      case OO_GSMFULLRATE:
         return AST_FORMAT_GSM;
      case OO_G729:
         return AST_FORMAT_G729A;
      case OO_G729A:
         return AST_FORMAT_G729A;
      case OO_G7231:
         return AST_FORMAT_G723_1;
      case OO_H263VIDEO:
         return AST_FORMAT_H263;
      default:
         ast_debug(1, "Cap %d is not supported by driver yet\n", cap);
         return -1;
   }

   return -1;
}

 
