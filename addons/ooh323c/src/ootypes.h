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
/*! \mainpage
 * <H1>ooH323c Stack Functions</H1>
 *
 * The <B>Objective Open H.323 for C (ooH323c)</B> protocol stack is an
 * open source applications program interface (API) for building H.323 based
 * applications.   The stack implements Q.931/H.225 call signaling procedures,
 * H.245 logical channel operations, and Registration, Admission, and Status
 * (RAS) messaging for Gatekeeper communications.
 *
 * The categories of user functions provided are as follows:
 * <UL>
 * <LI>Stack command functions.  These are high level functions used to
 * initiate common H.323 telephony operations (for example, to make a
 * call).</LI>
 * <LI>Gatekeeper functions.  These are high level functions for
 * managing communications with a gatekeeper using RAS messages.</LI>
 * <LI>H.323 endpoint management functions.  These are function for
 * used for managing the global H.323 endpoint.</LI>
 * <LI>Call management functions.  These are functions used to manage
 * active calls within the stack.</LI>
 * <LI>Capability management functions.  These functions are used for
 * negotiating capabilities between two different terminals.</LI>
 * <LI>H.225 and H.245 message handling functions.  Functions for
 * creating and handling H.323 standard ASN.1 messages.</LI>
 * <LI>Q.931 functions.  Functions for the execution of various
 * standard Q.931 operations.</LI>
 * <LI>TCP/IP and UDP socket communication functions.  Low-level
 * functions for writing data to and receiving data from sockets.</LI>
 * </UL>
 */
/**
 * @file ootypes.h
 * This file contains definitions of common constants and data structures.
 */
#ifndef _OOTYPES_H_
#define _OOTYPES_H_

#include "ooSocket.h"
#include "MULTIMEDIA-SYSTEM-CONTROL.h"
#include "H323-MESSAGES.h"
#include "ooasn1.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



#define OOH323C_VERSION "v0.8.3m"

#ifndef EXTERN
#ifdef MAKE_DLL
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */

/**
 * @defgroup ootypes Common type and constant definitions.
 * @{
 */
/* Function return codes */
#define OO_FAILED       -1
#define OO_OK           0

/* TODO: States for both local and remote initiation should be maintained
   separately */
/**
 * States defined for master/slave determination procedure.
 */
typedef enum OOMasterSlaveState {
   OO_MasterSlave_Idle,
   OO_MasterSlave_DetermineSent,
   OO_MasterSlave_AckReceived,
   OO_MasterSlave_Master,
   OO_MasterSlave_Slave
} OOMasterSlaveState;

typedef enum OOMSAckStatus {
  OO_msAck_localSent,
  OO_msAck_remoteReceived
} OOMSAckStatus;

/**
 * States defined for the capability exchange procedure.
 */
typedef enum {
   OO_LocalTermCapExchange_Idle,
   OO_LocalTermCapSetSent,
   OO_LocalTermCapSetAckRecvd,
   OO_RemoteTermCapExchange_Idle,
   OO_RemoteTermCapSetRecvd,
   OO_RemoteTermCapSetAckSent
} OOCapExchangeState;

/**
 * Call clear reason codes.
 */
typedef enum OOCallClearReason {
   OO_REASON_UNKNOWN=0,
   OO_REASON_INVALIDMESSAGE,
   OO_REASON_TRANSPORTFAILURE,
   OO_REASON_NOROUTE,
   OO_REASON_NOUSER,
   OO_REASON_NOBW,
   OO_REASON_GK_NOCALLEDUSER,
   OO_REASON_GK_NOCALLERUSER,
   OO_REASON_GK_NORESOURCES,
   OO_REASON_GK_UNREACHABLE,
   OO_REASON_GK_CLEARED,
   OO_REASON_NOCOMMON_CAPABILITIES,
   OO_REASON_REMOTE_FWDED,
   OO_REASON_LOCAL_FWDED,
   OO_REASON_REMOTE_CLEARED,
   OO_REASON_LOCAL_CLEARED,
   OO_REASON_REMOTE_BUSY,
   OO_REASON_LOCAL_BUSY,
   OO_REASON_REMOTE_NOANSWER,
   OO_REASON_LOCAL_NOTANSWERED,
   OO_REASON_REMOTE_REJECTED,
   OO_REASON_LOCAL_REJECTED,
   OO_REASON_REMOTE_CONGESTED,
   OO_REASON_LOCAL_CONGESTED
} OOCallClearReason;

/** Terminal type of the endpoint. Default is 60. */
#define OOTERMTYPE 60

/** Maximum length of an IP address (xxx.xxx.xxx.xxx). */
#define MAX_IP_LENGTH 2+8*4+7

/** Maximum length of a log file message */
#define MAXLOGMSGLEN 2048

/** Number of times to retry a failed operation. */
#define DEFAULT_MAX_RETRIES 3

/**
   Various message types for H225 and H245 messages
*/
#define OO_MSGTYPE_MIN                     0x65
#define OOQ931MSG                          0x65
#define OOH245MSG                          0x66
#define OOSetup                            0x67
#define OOCallProceeding                   0x68
#define OOAlert                            0x69
#define OOConnect                          0x6a
#define OOReleaseComplete                  0x6b
#define OOFacility                         0x6c
#define OOInformationMessage               0x6d
#define OOMasterSlaveDetermination         0x6e
#define OOMasterSlaveAck                   0x6f
#define OOMasterSlaveReject                0x70
#define OOMasterSlaveRelease               0x71
#define OOTerminalCapabilitySet            0x72
#define OOTerminalCapabilitySetAck         0x73
#define OOTerminalCapabilitySetReject      0x74
#define OOTerminalCapabilitySetRelease     0x75
#define OOOpenLogicalChannel               0x76
#define OOOpenLogicalChannelAck            0x77
#define OOOpenLogicalChannelReject         0x78
#define OOOpenLogicalChannelRelease        0x79
#define OOOpenLogicalChannelConfirm        0x7a
#define OOCloseLogicalChannel              0x7b
#define OOCloseLogicalChannelAck           0x7c
#define OORequestChannelClose              0x7d
#define OORequestChannelCloseAck           0x7e
#define OORequestChannelCloseReject        0x7f
#define OORequestChannelCloseRelease       0x80
#define OOEndSessionCommand                0x81
#define OOUserInputIndication              0x82
#define OORequestModeAck		   0x83
#define OORequestModeReject		   0x84
#define OORequestMode			   0x85
#define OORequestDelayResponse		   0x86
#define OORequestDelayRequest		   0x87
#define OOStatus			   0x88

#define OO_MSGTYPE_MAX                     0x88

/* Timer types */
#define OO_CALLESTB_TIMER  (1<<0)
#define OO_MSD_TIMER       (1<<1)
#define OO_TCS_TIMER       (1<<2)
#define OO_OLC_TIMER       (1<<3)
#define OO_CLC_TIMER       (1<<4)
#define OO_RCC_TIMER       (1<<5)
#define OO_SESSION_TIMER   (1<<6)
#define OO_H245CONNECT_TIMER (1<<7)
#define OO_RTD_TIMER	   (1<<8)

/** Maximum length for received messages */
#define MAXMSGLEN 4096

/** Maximum length of a filename */
#define MAXFILENAME 256

/**
 * Endpoint call modes. The call mode of the endpoint dictates what type
 * of channels are created for the calls placed by the endpoint or received
 * by the endpoint.
 */
typedef enum OOCallMode {
   OO_CALLMODE_AUDIOCALL,   /*!< Audio call */
   OO_CALLMODE_AUDIORX,     /*!< Audio call - receive only */
   OO_CALLMODE_AUDIOTX,     /*!< Audio call - transmit only */
   OO_CALLMODE_VIDEOCALL,   /*!< Video call */
   OO_CALLMODE_FAX          /*!< Fax transmission */
} OOCallMode;

/*
 * Flag macros - these operate on bit mask flags using mask values
 */
/** This macro sets a flag within a bit mask */
#define OO_SETFLAG(flags,mask) (flags |= (ASN1UINT)mask)

/** This macro clears a flag within a bit mask */
#define OO_CLRFLAG(flags,mask) (flags &= ~(ASN1UINT)mask)

/** This macro tests a flag with a bit mask */
#define OO_TESTFLAG(flags,mask) (((ASN1UINT)flags & (ASN1UINT)mask) != 0)



/*TODO: Should add caller-id, callername etc. So that they can be changed per
  call basis*/
/**
 * This structure defines options that can be set at the level of an
 * individual call. They override options set in the H.323 endpoint
 * structure.
 */
typedef struct ooCallOptions {
   OOBOOL fastStart;    /*!< Use FastStart signaling */
   OOBOOL tunneling;    /*!< Use H.245 tunneling */
   OOBOOL disableGk;    /*!< Disable use of gatekeeper */
   OOCallMode callMode; /*!< Type of channel to setup with FastStart */
   int transfercap;	/* q931 cap */
}ooCallOptions;


struct OOH323CallData;

typedef struct ooTimerCallback{
   struct OOH323CallData* call;
   ASN1UINT    timerType;
   ASN1UINT    channelNumber;
} ooTimerCallback;

/**
 * @}
 */
#endif
