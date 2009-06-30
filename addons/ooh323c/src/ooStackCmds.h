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
 * @file ooStackCmds.h 
 * This file contains stack commands which an user application can use to make
 * call, hang call etc. 
 */

#ifndef OO_STACKCMDS_H
#define OO_STACKCMDS_H

#include "ootypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#if defined (MAKE_DLL)
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */



/** 
 * @defgroup stackcmds Stack Control Commands
 * @{
 */

/**
 * This is an enumeration of Stack Command return codes.
 */
typedef enum OOStkCmdStat{
  OO_STKCMD_SUCCESS,
  OO_STKCMD_MEMERR,
  OO_STKCMD_INVALIDPARAM,
  OO_STKCMD_WRITEERR,
  OO_STKCMD_CONNECTIONERR
}OOStkCmdStat;

/**
 * This is an enumeration of stack command codes.
 */
typedef enum OOStackCmdID {
   OO_CMD_NOOP,
   OO_CMD_MAKECALL,          /*!< Make call */
   OO_CMD_ANSCALL,           /*!< Answer call */
   OO_CMD_FWDCALL,           /*!< Forward call */
   OO_CMD_HANGCALL,          /*!< Terminate call */
   OO_CMD_SENDDIGIT,         /*!< Send dtmf */
   OO_CMD_MANUALRINGBACK,    /*!< Send Alerting - ringback */
   OO_CMD_STOPMONITOR        /*!< Stop the event monitor */
   
} OOStackCmdID;

/**
 * This structure is used to queue a stack command for processing in 
 * the event handler loop.
 */
typedef struct OOStackCommand {
   OOStackCmdID type;
   void* param1;
   void* param2;
   void* param3;
} OOStackCommand;

#define ooCommand OOStackCommand;

/**
 * This function is used by an application to place a call.
 * @param dest        Call Destination - IP:port / alias
 * @param callToken   Pointer to a buffer in which callToken will be returned
 * @param bufsiz      Size of the callToken buffer passed.
 * @param opts        These are call specific options and if passed a non-null
 *                    value, will override global endpoint options.
 *
 * @return            Returns OOStkCmdStat value indication success or failure.
 */
EXTERN OOStkCmdStat ooMakeCall 
   (const char* dest, char *callToken, size_t bufsiz, ooCallOptions *opts);



/**
 * This function is used to send a manual ringback message (alerting message)
 * for a call. Effective only when manual-ringback is enabled.
 * @param callToken    Unique token for the call.
 * 
 * @return            Returns OOStkCmdStat value indication success or failure.
 */
EXTERN OOStkCmdStat ooManualRingback(const char *callToken);

/**
 * This function is used to answer a call
 * @param callToken   Unique token for the call
 * 
 * @return            Returns OOStkCmdStat value indication success or failure.
 */
EXTERN OOStkCmdStat ooAnswerCall(const char *callToken);

/**
 * This function is used to forward an existing call to third party.
 * @param callToken   Unique token for the call.
 * @param dest        Address to which the call has to be forwarded. Can be
 *                    IP:PORT or alias.
 *
 * @return            Returns OOStkCmdStat value indication success or failure.
 */
EXTERN OOStkCmdStat ooForwardCall(const char* callToken, char *dest);

/**
 * This function is used by an user application to terminate a call.
 * @param callToken   The uinque token for the call.
 * @param reason      Reason for hanging call.
 *
 * @return            Returns OOStkCmdStat value indication success or failure.
 */
EXTERN OOStkCmdStat ooHangCall(const char* callToken, OOCallClearReason reason);

/**
 * This command function can be used by an user application to send a DTMF 
 * sequence using H.245 UserInputIndication message.
 * @param callToken  Unique token for the call
 * @param alpha      Alphanumeric string reperesenting dtmf sequence
 *
 * @return            Returns OOStkCmdStat value indication success or failure.
 */
EXTERN OOStkCmdStat ooSendDTMFDigit(const char *callToken, const char* alpha);


/**
 * This function is used by the user application to stop stack thread.
 *
 * @return            Returns OOStkCmdStat value indication success or failure.
 */
EXTERN OOStkCmdStat ooStopMonitor(void);


/**
 * This function is used by application to obtain the text description for
 * failure of tsack command.
 * @param stat       Status code returned by stack command api.
 *
 * @return           Text description corresponding to the code.
 */
EXTERN const char* ooGetStkCmdStatusCodeTxt(OOStkCmdStat stat);
/** 
 * @} 
 */

#ifdef __cplusplus
}
#endif

#endif
