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
 * @file ooh323.h 
 * This file contains functions to support H.225 messages. 
 */
#ifndef _OOH323HDR_H_
#define _OOH323HDR_H_

#include "ooasn1.h"
#include "ootypes.h"
#include "ootrace.h"
#include "ooq931.h"
#include "MULTIMEDIA-SYSTEM-CONTROL.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#ifdef MAKE_DLL
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */

struct OOH323CallData;

/**
 * @addtogroup q931
 * @{
 */
/**
 * This function is used to process a received SETUP message.
 * @param call     Pointer to call for which SETUP message is received.
 * @param q931Msg  Pointer to the received SETUP message.
 *
 * @return         OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedSetup
(struct OOH323CallData *call, Q931Message *q931Msg);

/**
 * This function is used to process a received CONNECT message.
 * It creates H.245 negotiation channel, and starts TCS and MSD
 * procedures.
 * @param call      Pointer to call for which CONNECT message is received.
 * @param q931Msg   Pointer to the received q931Msg
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedSignalConnect
(struct OOH323CallData* call, Q931Message *q931Msg);

/**
 * This function is used to handle received H.2250 messages. It
 * calls helper functions based on the type of message received.
 * @param call       Pointer to the call for which a H.2250 message is received
 * @param q931Msg    Pointer to the received q931Msg
 *
 * @return           OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooHandleH2250Message
(struct OOH323CallData *call, Q931Message *q931Msg);

/**
 * This function is used to process a received Facility message.
 * @param call       Handle to the call for which message has been received.
 * @param pQ931Msg   Pointer the the received Facility message.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooOnReceivedFacility
(struct OOH323CallData *call, Q931Message * pQ931Msg);

/**
 * This function is used to process tunneled H245 messages
 * @param call       Handle to the call
 * @param pH323UUPdu Pointer to the pdu containing tunneled messages.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooHandleTunneledH245Messages(struct OOH323CallData *call, 
                                        H225H323_UU_PDU * pH323UUPdu);

/**
 * This is a helper function used to handle an startH245 Facility message.
 * @param call       Handle to the call
 * @param facility   Pointer to the facility message.
 */
EXTERN int ooHandleStartH245FacilityMessage(struct OOH323CallData *call, 
                                            H225Facility_UUIE *facility);

/**
 * This function is used to retrieve the aliases from Sequence of alias
 * addresses.
 * @param call       Handle to the call.Null when retrieving registered
 *                   aliases.
 * @param pAddresses Pointer to the sequence of alias addresses.
 * @param aliasList  Handle to alias list to be populated with retrieved 
 *                   aliases.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323RetrieveAliases
   (struct OOH323CallData *call, H225_SeqOfH225AliasAddress *pAddresses, 
    OOAliases **aliasList);

/**
 * This is a helper function used to populate alias list using aliases.
 * @param pctxt      Pointer to OOCTXT structure which will be used for memory
 *                   allocations.
 * @param pAliases   Pointer to aliases to be used for populating list.
 * @param pAliasList Pointer to alias list to be populated.
 *
 * @return           OO_OK, on success. OO_FAILED, otherwise. 
 */
EXTERN int ooPopulateAliasList(OOCTXT *pctxt, OOAliases *pAliases,
                               H225_SeqOfH225AliasAddress *pAliasList); 

/**
 * This function is used to search a particular alias in the alias list. The
 * search can be on the basis of alias type or value or both.
 * @param aliasList Handle to the alias list to be searched.
 * @param type      Type of the alias, if search has to consider type as 
 *                  criterion, otherwise 0.
 * @param value     Value of the alias, if the search has to consider value as
 *                  criterion, NULL otherwise.
 */
EXTERN OOAliases* ooH323GetAliasFromList
                               (OOAliases *aliasList, int type, char *value);

/**
 * This function is used to add a new alias to alias list.
 * @param pAliasList    Pointer to Alias list.
 * @param pctxt         Pointer to OOCTXT structure to be used for memory 
 *                      allocation.
 * @param pAliasAddress New alias address to be added.
 *
 * @return              Handle to newly added alias or NULL in case of failure.
 */
EXTERN OOAliases* ooH323AddAliasToList
(OOAliases **pAliasList, OOCTXT *pctxt, H225AliasAddress *pAliasAddress);

/**
 * This function is used to retrieve ip address and port number from an
 * H225TransportAddress structure.
 * @param call          Handle to the associated call.
 * @param h225Address   Pointer to H225TransportAddress structure
 * @param ip            Pointer to the buffer in which dotted ip address will
 *                      be returned.
 * @param port          Pointer to the integer in which port value will be 
 *                      returned.
 *
 * @return              OO_OK, on success; OO_FAILED, on failure.
 */
int ooH323GetIpPortFromH225TransportAddress(struct OOH323CallData *call, 
   H225TransportAddress *h225Address, char *ip, int *port);
/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif

