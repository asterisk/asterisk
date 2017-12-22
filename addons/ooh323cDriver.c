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

#include "asterisk.h"
#include "asterisk/lock.h"

#include "asterisk/pbx.h"
#include "asterisk/logger.h"

#undef AST_BACKGROUND_STACKSIZE
#define AST_BACKGROUND_STACKSIZE 768 * 1024

#define SEC_TO_HOLD_THREAD 24

extern struct ast_module *myself;
extern OOBOOL gH323Debug;
extern OOH323EndPoint gH323ep;
/* ooh323c stack thread. */
static pthread_t ooh323c_thread = AST_PTHREADT_NULL;
static pthread_t ooh323cmd_thread = AST_PTHREADT_NULL;
static int grxframes = 240;

static int gtxframes = 20;

static struct callthread {
	ast_mutex_t		lock;
	int			thePipe[2];
	OOBOOL			inUse;
	ooCallData*		call;
	struct callthread	*next, *prev;
} *callThreads = NULL;

AST_MUTEX_DEFINE_STATIC(callThreadsLock);


int ooh323c_start_receive_channel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_start_transmit_channel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_stop_receive_channel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_stop_transmit_channel(ooCallData *call, ooLogicalChannel *pChannel);

int ooh323c_start_receive_datachannel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_start_transmit_datachannel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_stop_receive_datachannel(ooCallData *call, ooLogicalChannel *pChannel);
int ooh323c_stop_transmit_datachannel(ooCallData *call, ooLogicalChannel *pChannel);

void* ooh323c_stack_thread(void* dummy);
void* ooh323c_cmd_thread(void* dummy);
void* ooh323c_call_thread(void* dummy);
int ooh323c_set_aliases(ooAliases * aliases);

void* ooh323c_stack_thread(void* dummy)
{

  ooMonitorChannels();
  return dummy;
}

void* ooh323c_cmd_thread(void* dummy)
{

  ooMonitorCmdChannels();
  return dummy;
}

void* ooh323c_call_thread(void* dummy)
{
 struct callthread* mycthread = (struct callthread *)dummy;
 struct pollfd pfds[1];
 char c;
 int res = 0;

 do {

 	ooMonitorCallChannels((ooCallData*)mycthread->call);
	mycthread->call = NULL;
	mycthread->prev = NULL;
	mycthread->inUse = FALSE;

	ast_mutex_lock(&callThreadsLock);
	mycthread->next = callThreads;
	callThreads = mycthread;
	if (mycthread->next) mycthread->next->prev = mycthread;
	ast_mutex_unlock(&callThreadsLock);

	pfds[0].fd = mycthread->thePipe[0];
	pfds[0].events = POLLIN;
	ooSocketPoll(pfds, 1, SEC_TO_HOLD_THREAD * 1000);
	if (ooPDRead(pfds, 1, mycthread->thePipe[0]))
		res = read(mycthread->thePipe[0], &c, 1);

 	ast_mutex_lock(&callThreadsLock);
	ast_mutex_lock(&mycthread->lock);
 	if (mycthread->prev)
		mycthread->prev->next = mycthread->next;
 	else
		callThreads = mycthread->next;
 	if (mycthread->next)
		mycthread->next->prev = mycthread->prev;
	ast_mutex_unlock(&mycthread->lock);
 	ast_mutex_unlock(&callThreadsLock);

 } while (mycthread->call != NULL && res >= 0);


 ast_mutex_destroy(&mycthread->lock);

 close(mycthread->thePipe[0]);
 close(mycthread->thePipe[1]);
 ast_free(mycthread);
 ast_module_unref(myself);
 ast_update_use_count();
 return NULL;
}

int ooh323c_start_call_thread(ooCallData *call) {
 char c = 'c';
 struct callthread *cur = callThreads;

 ast_mutex_lock(&callThreadsLock);
 while (cur != NULL && (cur->inUse || ast_mutex_trylock(&cur->lock))) {
	cur = cur->next;
 }
 ast_mutex_unlock(&callThreadsLock);

 if (cur != NULL) {
   if (cur->inUse || write(cur->thePipe[1], &c, 1) < 0) {
	ast_mutex_unlock(&cur->lock);
	cur = NULL;
   }
 }

/* make new thread */
 if (cur == NULL) {
	if (!(cur = ast_calloc(1, sizeof(struct callthread)))) {
		ast_log(LOG_ERROR, "Unable to allocate thread structure for call %s\n",
							call->callToken);
		return -1;
	}

	ast_module_ref(myself);
	if ((socketpair(PF_LOCAL, SOCK_STREAM, 0, cur->thePipe)) == -1) {
		ast_log(LOG_ERROR, "Can't create thread pipe for call %s\n", call->callToken);
		ast_free(cur);
		return -1;
	}
	cur->inUse = TRUE;
	cur->call = call;

	ast_mutex_init(&cur->lock);

	if (gH323Debug)
		ast_debug(1,"new call thread created for call %s\n", call->callToken);

	if(ast_pthread_create_detached_background(&call->callThread, NULL, ooh323c_call_thread, cur) < 0)
 	{
  		ast_log(LOG_ERROR, "Unable to start ooh323c call thread for call %s\n",
					call->callToken);
		ast_mutex_destroy(&cur->lock);
		close(cur->thePipe[0]);
		close(cur->thePipe[1]);
		ast_free(cur);
  		return -1;
 	}

 } else {
	if (gH323Debug)
		ast_debug(1,"using existing call thread for call %s\n", call->callToken);
	cur->inUse = TRUE;
	cur->call = call;
	ast_mutex_unlock(&cur->lock);

 }
 return 0;
}


int ooh323c_stop_call_thread(ooCallData *call) {
 if (call->callThread != AST_PTHREADT_NULL) {
  ooStopMonitorCallChannels(call);
 }
 return 0;
}

int ooh323c_start_stack_thread()
{
   if(ast_pthread_create_background(&ooh323c_thread, NULL, ooh323c_stack_thread, NULL) < 0)
   {
      ast_log(LOG_ERROR, "Unable to start ooh323c thread.\n");
      return -1;
   }
   if(ast_pthread_create_background(&ooh323cmd_thread, NULL, ooh323c_cmd_thread, NULL) < 0)
   {
      ast_log(LOG_ERROR, "Unable to start ooh323cmd thread.\n");
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
      pthread_join(ooh323cmd_thread, NULL);
      ooh323cmd_thread =  AST_PTHREADT_NULL;
   }
   return 0;
}

int ooh323c_set_capability
   (struct ast_format_cap *cap, int dtmf, int dtmfcodec)
{
   int ret = 0, x;
   if (gH323Debug) {
     ast_verb(0, "\tAdding capabilities to H323 endpoint\n");
   }

   for(x=0; x<ast_format_cap_count(cap); x++)
   {
    struct ast_format *format = ast_format_cap_get_format(cap, x);
      if(ast_format_cmp(format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g711 ulaw capability to H323 endpoint\n");
	 }
         ret= ooH323EpAddG711Capability(OO_G711ULAW64K, gtxframes, grxframes,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
      }
      if(ast_format_cmp(format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g711 alaw capability to H323 endpoint\n");
	 }
         ret= ooH323EpAddG711Capability(OO_G711ALAW64K, gtxframes, grxframes,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
      }

      if(ast_format_cmp(format, ast_format_g729) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
	    ast_verb(0, "\tAdding g729A capability to H323 endpoint\n");
	 }
         ret = ooH323EpAddG729Capability(OO_G729A, 2, 24,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

         if (gH323Debug) {
            ast_verb(0, "\tAdding g729 capability to H323 endpoint\n");
	 }
         ret |= ooH323EpAddG729Capability(OO_G729, 2, 24,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
         if (gH323Debug) {
            ast_verb(0, "\tAdding g729b capability to H323 endpoint\n");
	 }
         ret |= ooH323EpAddG729Capability(OO_G729B, 2, 24,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
      }

      if(ast_format_cmp(format, ast_format_g723) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g7231 capability to H323 endpoint\n");
	 }
         ret = ooH323EpAddG7231Capability(OO_G7231, 1, 1, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_g726) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g726 capability to H323 endpoint\n");
	 }
         ret = ooH323EpAddG726Capability(OO_G726, gtxframes, grxframes, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_g726_aal2) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g726aal2 capability to H323 endpoint\n");
	 }
         ret = ooH323EpAddG726Capability(OO_G726AAL2, gtxframes, grxframes, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_h263) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding h263 capability to H323 endpoint\n");
	 }
         ret = ooH323EpAddH263VideoCapability(OO_H263VIDEO, 1, 0, 0, 0, 0, 320*1024,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_gsm) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding gsm capability to H323 endpoint\n");
	 }
         ret = ooH323EpAddGSMCapability(OO_GSMFULLRATE, 4, FALSE, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_speex) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding speex capability to H323 endpoint\n");
	 }
         ret = ooH323EpAddSpeexCapability(OO_SPEEX, 4, 4, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

    ao2_ref(format, -1);
   }

   if(dtmf & H323_DTMF_CISCO)
      ret |= ooH323EpEnableDTMFCISCO(0);
   if(dtmf & H323_DTMF_RFC2833)
      ret |= ooH323EpEnableDTMFRFC2833(0);
   else if(dtmf & H323_DTMF_H245ALPHANUMERIC)
      ret |= ooH323EpEnableDTMFH245Alphanumeric();
   else if(dtmf & H323_DTMF_H245SIGNAL)
      ret |= ooH323EpEnableDTMFH245Signal();

   return ret;
}

int ooh323c_set_capability_for_call
   (ooCallData *call, struct ast_format_cap *cap, int dtmf, int dtmfcodec,
		 int t38support, int g729onlyA)
{
   int ret = 0, x, txframes;
   if (gH323Debug) {
     ast_verb(0, "\tAdding capabilities to call(%s, %s)\n", call->callType,
                                                            call->callToken);
   }
   if(dtmf & H323_DTMF_CISCO || 1)
      ret |= ooCallEnableDTMFCISCO(call,dtmfcodec);
   if(dtmf & H323_DTMF_RFC2833 || 1)
      ret |= ooCallEnableDTMFRFC2833(call,dtmfcodec);
   if(dtmf & H323_DTMF_H245ALPHANUMERIC || 1)
      ret |= ooCallEnableDTMFH245Alphanumeric(call);
   if(dtmf & H323_DTMF_H245SIGNAL || 1)
      ret |= ooCallEnableDTMFH245Signal(call);

   if (t38support)
   	ooCapabilityAddT38Capability(call, OO_T38, OORXANDTX,
					&ooh323c_start_receive_datachannel,
					&ooh323c_start_transmit_datachannel,
					&ooh323c_stop_receive_datachannel,
					&ooh323c_stop_transmit_datachannel,
					0);

   for(x=0; x<ast_format_cap_count(cap); x++)
   {
    struct ast_format *format = ast_format_cap_get_format(cap, x);
      if(ast_format_cmp(format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g711 ulaw capability to call(%s, %s)\n",
                                              call->callType, call->callToken);
	 }
	 txframes = ast_format_cap_get_format_framing(cap, format);
         ret= ooCallAddG711Capability(call, OO_G711ULAW64K, txframes,
                                      txframes, OORXANDTX,
                                      &ooh323c_start_receive_channel,
                                      &ooh323c_start_transmit_channel,
                                      &ooh323c_stop_receive_channel,
                                      &ooh323c_stop_transmit_channel);
      }
      if(ast_format_cmp(format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g711 alaw capability to call(%s, %s)\n",
                                            call->callType, call->callToken);
	 }
         txframes = ast_format_cap_get_format_framing(cap, format);
         ret= ooCallAddG711Capability(call, OO_G711ALAW64K, txframes,
                                     txframes, OORXANDTX,
                                     &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
      }

      if(ast_format_cmp(format, ast_format_g726) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g726 capability to call (%s, %s)\n",
                                           call->callType, call->callToken);
	 }
	 txframes = ast_format_cap_get_format_framing(cap, format);
         ret = ooCallAddG726Capability(call, OO_G726, txframes, grxframes, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_g726_aal2) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g726aal2 capability to call (%s, %s)\n",
                                           call->callType, call->callToken);
	 }
	 txframes = ast_format_cap_get_format_framing(cap, format);
         ret = ooCallAddG726Capability(call, OO_G726AAL2, txframes, grxframes, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_g729) == AST_FORMAT_CMP_EQUAL)
      {

         txframes = (ast_format_cap_get_format_framing(cap, format))/10;
         if (gH323Debug) {
            ast_verb(0, "\tAdding g729A capability to call(%s, %s)\n",
                                            call->callType, call->callToken);
	 }
         ret= ooCallAddG729Capability(call, OO_G729A, txframes, txframes,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
	 if (g729onlyA)
		continue;
         if (gH323Debug) {
            ast_verb(0, "\tAdding g729 capability to call(%s, %s)\n",
                                            call->callType, call->callToken);
	 }
         ret|= ooCallAddG729Capability(call, OO_G729, txframes, txframes,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
         if (gH323Debug) {
            ast_verb(0, "\tAdding g729B capability to call(%s, %s)\n",
                                            call->callType, call->callToken);
	 }
         ret|= ooCallAddG729Capability(call, OO_G729B, txframes, txframes,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_g723) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding g7231 capability to call (%s, %s)\n",
                                           call->callType, call->callToken);
	 }
         ret = ooCallAddG7231Capability(call, OO_G7231, 1, 1, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_h263) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding h263 capability to call (%s, %s)\n",
                                           call->callType, call->callToken);
	 }
         ret = ooCallAddH263VideoCapability(call, OO_H263VIDEO, 1, 0, 0, 0, 0, 320*1024,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);

      }

      if(ast_format_cmp(format, ast_format_gsm) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding gsm capability to call(%s, %s)\n",
                                             call->callType, call->callToken);
	 }
         ret = ooCallAddGSMCapability(call, OO_GSMFULLRATE, 4, FALSE, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
      }

      if(ast_format_cmp(format, ast_format_speex) == AST_FORMAT_CMP_EQUAL)
      {
         if (gH323Debug) {
            ast_verb(0, "\tAdding Speex capability to call(%s, %s)\n",
                                             call->callType, call->callToken);
	 }
         ret = ooCallAddSpeexCapability(call, OO_SPEEX, 4, 4, FALSE,
                                     OORXANDTX, &ooh323c_start_receive_channel,
                                     &ooh323c_start_transmit_channel,
                                     &ooh323c_stop_receive_channel,
                                     &ooh323c_stop_transmit_channel);
      }

      ao2_ref(format, -1);
   }
   return ret;
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
   struct ast_format *tmpfmt = NULL;
   tmpfmt = convertH323CapToAsteriskCap(pChannel->chanCap->cap);
   if(tmpfmt) {
      /* ooh323_set_read_format(call, fmt); */
   }else{
     ast_log(LOG_ERROR, "Invalid capability type for receive channel %s\n",
                                                          call->callToken);
     return -1;
   }
   return 1;
}

int ooh323c_start_transmit_channel(ooCallData *call, ooLogicalChannel *pChannel)
{
   struct ast_format *tmpfmt = NULL;
   tmpfmt = convertH323CapToAsteriskCap(pChannel->chanCap->cap);

   if (tmpfmt) {
    if ((ast_format_cmp(tmpfmt, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) ||
      (ast_format_cmp(tmpfmt, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL)) {
  	ooh323_set_write_format(call, tmpfmt, ((OOCapParams *)(pChannel->chanCap->params))->txframes);
    } else if (ast_format_cmp(tmpfmt, ast_format_g729) == AST_FORMAT_CMP_EQUAL) {
	 ooh323_set_write_format(call, tmpfmt, ((OOCapParams *)(pChannel->chanCap->params))->txframes*10);
  } else {
	ooh323_set_write_format(call, tmpfmt, 0);
      }
   }else{
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


int ooh323c_start_receive_datachannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   return 1;
}

int ooh323c_start_transmit_datachannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   setup_udptl_connection(call, pChannel->remoteIP, pChannel->remoteMediaPort);
   return 1;
}

int ooh323c_stop_receive_datachannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   return 1;
}

int ooh323c_stop_transmit_datachannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   close_udptl_connection(call);
   return 1;
}

struct ast_format *convertH323CapToAsteriskCap(int cap)
{
   switch(cap)
   {
      case OO_G711ULAW64K:
         return ast_format_ulaw;
      case OO_G711ALAW64K:
         return ast_format_alaw;
      case OO_GSMFULLRATE:
         return ast_format_gsm;
      case OO_SPEEX:
         return ast_format_speex;
      case OO_G729:
         return ast_format_g729;
      case OO_G729A:
         return ast_format_g729;
      case OO_G729B:
         return ast_format_g729;
      case OO_G7231:
         return ast_format_g723;
      case OO_G726:
         return ast_format_g726;
      case OO_G726AAL2:
         return ast_format_g726_aal2;
      case OO_H263VIDEO:
         return ast_format_h263;
      default:
         ast_debug(1, "Cap %d is not supported by driver yet\n", cap);
         return NULL;
   }

   return NULL;
}
