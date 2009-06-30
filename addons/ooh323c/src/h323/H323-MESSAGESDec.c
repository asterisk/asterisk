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

#include "ooasn1.h"
#include "H323-MESSAGES.h"
#include "eventHandler.h"

/**************************************************************/
/*                                                            */
/*  ScreeningIndicator                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ScreeningIndicator (OOCTXT* pctxt, H225ScreeningIndicator* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (extbit) {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;

      *pvalue = ui;
   }
   else {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;

      switch (ui) {
         case 0: *pvalue = userProvidedNotScreened; break;
         case 1: *pvalue = userProvidedVerifiedAndPassed; break;
         case 2: *pvalue = userProvidedVerifiedAndFailed; break;
         case 3: *pvalue = networkProvided; break;
         default: return ASN_E_INVENUM;
      }
   }
   invokeUIntValue (pctxt, *pvalue);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  NumberDigits                                              */
/*                                                            */
/**************************************************************/

extern EXTERN const char* gs_H323_MESSAGES_NumberDigits_CharSet;

EXTERN int asn1PD_H225NumberDigits (OOCTXT* pctxt, H225NumberDigits* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 128, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeConstrainedStringEx (pctxt, pvalue, gs_H323_MESSAGES_NumberDigits_CharSet, 4, 4, 7);
   if (stat != ASN_OK) return stat;
   invokeCharStrValue (pctxt, *pvalue);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TBCD_STRING                                               */
/*                                                            */
/**************************************************************/

extern EXTERN const char* gs_H323_MESSAGES_TBCD_STRING_CharSet;

EXTERN int asn1PD_H225TBCD_STRING (OOCTXT* pctxt, H225TBCD_STRING* pvalue)
{
   int stat = ASN_OK;

   stat = decodeConstrainedStringEx (pctxt, pvalue, gs_H323_MESSAGES_TBCD_STRING_CharSet, 4, 4, 7);
   if (stat != ASN_OK) return stat;
   invokeCharStrValue (pctxt, *pvalue);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GloballyUniqueID                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GloballyUniqueID (OOCTXT* pctxt, H225GloballyUniqueID* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 16, 16, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ConferenceIdentifier                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ConferenceIdentifier (OOCTXT* pctxt, H225ConferenceIdentifier* pvalue)
{
   int stat = ASN_OK;

   stat = asn1PD_H225GloballyUniqueID (pctxt, pvalue);
   if (stat != ASN_OK) return stat;

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RequestSeqNum                                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RequestSeqNum (OOCTXT* pctxt, H225RequestSeqNum* pvalue)
{
   int stat = ASN_OK;

   stat = decodeConsUInt16 (pctxt, pvalue, 1U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, *pvalue);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatekeeperIdentifier                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatekeeperIdentifier (OOCTXT* pctxt, H225GatekeeperIdentifier* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 128, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeBMPString (pctxt, pvalue, 0);
   if (stat != ASN_OK) return stat;
   invokeCharStr16BitValue (pctxt, pvalue->nchars, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  BandWidth                                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225BandWidth (OOCTXT* pctxt, H225BandWidth* pvalue)
{
   int stat = ASN_OK;

   stat = decodeConsUnsigned (pctxt, pvalue, 0U, ASN1UINT_MAX);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, *pvalue);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallReferenceValue                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallReferenceValue (OOCTXT* pctxt, H225CallReferenceValue* pvalue)
{
   int stat = ASN_OK;

   stat = decodeConsUInt16 (pctxt, pvalue, 0U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, *pvalue);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  EndpointIdentifier                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225EndpointIdentifier (OOCTXT* pctxt, H225EndpointIdentifier* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 128, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeBMPString (pctxt, pvalue, 0);
   if (stat != ASN_OK) return stat;
   invokeCharStr16BitValue (pctxt, pvalue->nchars, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ProtocolIdentifier                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ProtocolIdentifier (OOCTXT* pctxt, H225ProtocolIdentifier* pvalue)
{
   int stat = ASN_OK;

   stat = decodeObjectIdentifier (pctxt, pvalue);
   if (stat != ASN_OK) return stat;
   invokeOidValue (pctxt, pvalue->numids, pvalue->subid);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TimeToLive                                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TimeToLive (OOCTXT* pctxt, H225TimeToLive* pvalue)
{
   int stat = ASN_OK;

   stat = decodeConsUnsigned (pctxt, pvalue, 1U, ASN1UINT_MAX);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, *pvalue);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H248PackagesDescriptor                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H248PackagesDescriptor (OOCTXT* pctxt, H225H248PackagesDescriptor* pvalue)
{
   int stat = ASN_OK;

   stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)pvalue);
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H248SignalsDescriptor                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H248SignalsDescriptor (OOCTXT* pctxt, H225H248SignalsDescriptor* pvalue)
{
   int stat = ASN_OK;

   stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)pvalue);
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GenericIdentifier                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GenericIdentifier (OOCTXT* pctxt, H225GenericIdentifier* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* standard */
         case 0:
            invokeStartElement (pctxt, "standard", -1);

            /* extension bit */

            DECODEBIT (pctxt, &extbit);

            if (extbit == 0) {
               stat = decodeConsInteger (pctxt, &pvalue->u.standard, 0, 16383);
               if (stat != ASN_OK) return stat;
            }
            else {
               stat = decodeUnconsInteger (pctxt, &pvalue->u.standard);
               if (stat != ASN_OK) return stat;
            }
            invokeIntValue (pctxt, pvalue->u.standard);

            invokeEndElement (pctxt, "standard", -1);

            break;

         /* oid */
         case 1:
            invokeStartElement (pctxt, "oid", -1);

            pvalue->u.oid = ALLOC_ASN1ELEM (pctxt, ASN1OBJID);

            stat = decodeObjectIdentifier (pctxt, pvalue->u.oid);
            if (stat != ASN_OK) return stat;
            invokeOidValue (pctxt, pvalue->u.oid->numids, pvalue->u.oid->subid);

            invokeEndElement (pctxt, "oid", -1);

            break;

         /* nonStandard */
         case 2:
            invokeStartElement (pctxt, "nonStandard", -1);

            pvalue->u.nonStandard = ALLOC_ASN1ELEM (pctxt, H225GloballyUniqueID);

            stat = asn1PD_H225GloballyUniqueID (pctxt, pvalue->u.nonStandard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandard", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipAddress_ip                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipAddress_ip (OOCTXT* pctxt, H225TransportAddress_ipAddress_ip* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 4, 4, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipAddress                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipAddress (OOCTXT* pctxt, H225TransportAddress_ipAddress* pvalue)
{
   int stat = ASN_OK;

   /* decode ip */

   invokeStartElement (pctxt, "ip", -1);

   stat = asn1PD_H225TransportAddress_ipAddress_ip (pctxt, &pvalue->ip);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "ip", -1);

   /* decode port */

   invokeStartElement (pctxt, "port", -1);

   stat = decodeConsUInt16 (pctxt, &pvalue->port, 0U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->port);

   invokeEndElement (pctxt, "port", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipSourceRoute_ip                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipSourceRoute_ip (OOCTXT* pctxt, H225TransportAddress_ipSourceRoute_ip* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 4, 4, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipSourceRoute_route_element              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipSourceRoute_route_element (OOCTXT* pctxt, H225TransportAddress_ipSourceRoute_route_element* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 4, 4, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225TransportAddress_ipSourceRoute_route_element    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225TransportAddress_ipSourceRoute_route_element (OOCTXT* pctxt, H225_SeqOfH225TransportAddress_ipSourceRoute_route_element* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, H225TransportAddress_ipSourceRoute_route_element);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = asn1PD_H225TransportAddress_ipSourceRoute_route_element (pctxt, &pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipSourceRoute_routing                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipSourceRoute_routing (OOCTXT* pctxt, H225TransportAddress_ipSourceRoute_routing* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* strict */
         case 0:
            invokeStartElement (pctxt, "strict", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "strict", -1);

            break;

         /* loose */
         case 1:
            invokeStartElement (pctxt, "loose", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "loose", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipSourceRoute                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipSourceRoute (OOCTXT* pctxt, H225TransportAddress_ipSourceRoute* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode ip */

   invokeStartElement (pctxt, "ip", -1);

   stat = asn1PD_H225TransportAddress_ipSourceRoute_ip (pctxt, &pvalue->ip);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "ip", -1);

   /* decode port */

   invokeStartElement (pctxt, "port", -1);

   stat = decodeConsUInt16 (pctxt, &pvalue->port, 0U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->port);

   invokeEndElement (pctxt, "port", -1);

   /* decode route */

   invokeStartElement (pctxt, "route", -1);

   stat = asn1PD_H225_SeqOfH225TransportAddress_ipSourceRoute_route_element (pctxt, &pvalue->route);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "route", -1);

   /* decode routing */

   invokeStartElement (pctxt, "routing", -1);

   stat = asn1PD_H225TransportAddress_ipSourceRoute_routing (pctxt, &pvalue->routing);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "routing", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipxAddress_node                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipxAddress_node (OOCTXT* pctxt, H225TransportAddress_ipxAddress_node* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 6, 6, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipxAddress_netnum                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipxAddress_netnum (OOCTXT* pctxt, H225TransportAddress_ipxAddress_netnum* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 4, 4, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipxAddress_port                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipxAddress_port (OOCTXT* pctxt, H225TransportAddress_ipxAddress_port* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 2, 2, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ipxAddress                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ipxAddress (OOCTXT* pctxt, H225TransportAddress_ipxAddress* pvalue)
{
   int stat = ASN_OK;

   /* decode node */

   invokeStartElement (pctxt, "node", -1);

   stat = asn1PD_H225TransportAddress_ipxAddress_node (pctxt, &pvalue->node);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "node", -1);

   /* decode netnum */

   invokeStartElement (pctxt, "netnum", -1);

   stat = asn1PD_H225TransportAddress_ipxAddress_netnum (pctxt, &pvalue->netnum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "netnum", -1);

   /* decode port */

   invokeStartElement (pctxt, "port", -1);

   stat = asn1PD_H225TransportAddress_ipxAddress_port (pctxt, &pvalue->port);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "port", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ip6Address_ip                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ip6Address_ip (OOCTXT* pctxt, H225TransportAddress_ip6Address_ip* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 16, 16, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_ip6Address                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_ip6Address (OOCTXT* pctxt, H225TransportAddress_ip6Address* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode ip */

   invokeStartElement (pctxt, "ip", -1);

   stat = asn1PD_H225TransportAddress_ip6Address_ip (pctxt, &pvalue->ip);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "ip", -1);

   /* decode port */

   invokeStartElement (pctxt, "port", -1);

   stat = decodeConsUInt16 (pctxt, &pvalue->port, 0U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->port);

   invokeEndElement (pctxt, "port", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_netBios                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_netBios (OOCTXT* pctxt, H225TransportAddress_netBios* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 16, 16, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress_nsap                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress_nsap (OOCTXT* pctxt, H225TransportAddress_nsap* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 20, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H221NonStandard                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H221NonStandard (OOCTXT* pctxt, H225H221NonStandard* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode t35CountryCode */

   invokeStartElement (pctxt, "t35CountryCode", -1);

   stat = decodeConsUInt8 (pctxt, &pvalue->t35CountryCode, 0U, 255U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->t35CountryCode);

   invokeEndElement (pctxt, "t35CountryCode", -1);

   /* decode t35Extension */

   invokeStartElement (pctxt, "t35Extension", -1);

   stat = decodeConsUInt8 (pctxt, &pvalue->t35Extension, 0U, 255U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->t35Extension);

   invokeEndElement (pctxt, "t35Extension", -1);

   /* decode manufacturerCode */

   invokeStartElement (pctxt, "manufacturerCode", -1);

   stat = decodeConsUInt16 (pctxt, &pvalue->manufacturerCode, 0U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->manufacturerCode);

   invokeEndElement (pctxt, "manufacturerCode", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  NonStandardIdentifier                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225NonStandardIdentifier (OOCTXT* pctxt, H225NonStandardIdentifier* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* object */
         case 0:
            invokeStartElement (pctxt, "object", -1);

            pvalue->u.object = ALLOC_ASN1ELEM (pctxt, ASN1OBJID);

            stat = decodeObjectIdentifier (pctxt, pvalue->u.object);
            if (stat != ASN_OK) return stat;
            invokeOidValue (pctxt, pvalue->u.object->numids, pvalue->u.object->subid);

            invokeEndElement (pctxt, "object", -1);

            break;

         /* h221NonStandard */
         case 1:
            invokeStartElement (pctxt, "h221NonStandard", -1);

            pvalue->u.h221NonStandard = ALLOC_ASN1ELEM (pctxt, H225H221NonStandard);

            stat = asn1PD_H225H221NonStandard (pctxt, pvalue->u.h221NonStandard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "h221NonStandard", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  NonStandardParameter                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225NonStandardParameter (OOCTXT* pctxt, H225NonStandardParameter* pvalue)
{
   int stat = ASN_OK;

   /* decode nonStandardIdentifier */

   invokeStartElement (pctxt, "nonStandardIdentifier", -1);

   stat = asn1PD_H225NonStandardIdentifier (pctxt, &pvalue->nonStandardIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "nonStandardIdentifier", -1);

   /* decode data */

   invokeStartElement (pctxt, "data", -1);

   stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->data);
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->data.numocts, pvalue->data.data);

   invokeEndElement (pctxt, "data", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportAddress                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportAddress (OOCTXT* pctxt, H225TransportAddress* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 6);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* ipAddress */
         case 0:
            invokeStartElement (pctxt, "ipAddress", -1);

            pvalue->u.ipAddress = ALLOC_ASN1ELEM (pctxt, H225TransportAddress_ipAddress);

            stat = asn1PD_H225TransportAddress_ipAddress (pctxt, pvalue->u.ipAddress);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "ipAddress", -1);

            break;

         /* ipSourceRoute */
         case 1:
            invokeStartElement (pctxt, "ipSourceRoute", -1);

            pvalue->u.ipSourceRoute = ALLOC_ASN1ELEM (pctxt, H225TransportAddress_ipSourceRoute);

            stat = asn1PD_H225TransportAddress_ipSourceRoute (pctxt, pvalue->u.ipSourceRoute);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "ipSourceRoute", -1);

            break;

         /* ipxAddress */
         case 2:
            invokeStartElement (pctxt, "ipxAddress", -1);

            pvalue->u.ipxAddress = ALLOC_ASN1ELEM (pctxt, H225TransportAddress_ipxAddress);

            stat = asn1PD_H225TransportAddress_ipxAddress (pctxt, pvalue->u.ipxAddress);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "ipxAddress", -1);

            break;

         /* ip6Address */
         case 3:
            invokeStartElement (pctxt, "ip6Address", -1);

            pvalue->u.ip6Address = ALLOC_ASN1ELEM (pctxt, H225TransportAddress_ip6Address);

            stat = asn1PD_H225TransportAddress_ip6Address (pctxt, pvalue->u.ip6Address);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "ip6Address", -1);

            break;

         /* netBios */
         case 4:
            invokeStartElement (pctxt, "netBios", -1);

            pvalue->u.netBios = ALLOC_ASN1ELEM (pctxt, H225TransportAddress_netBios);

            stat = asn1PD_H225TransportAddress_netBios (pctxt, pvalue->u.netBios);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "netBios", -1);

            break;

         /* nsap */
         case 5:
            invokeStartElement (pctxt, "nsap", -1);

            pvalue->u.nsap = ALLOC_ASN1ELEM (pctxt, H225TransportAddress_nsap);

            stat = asn1PD_H225TransportAddress_nsap (pctxt, pvalue->u.nsap);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nsap", -1);

            break;

         /* nonStandardAddress */
         case 6:
            invokeStartElement (pctxt, "nonStandardAddress", -1);

            pvalue->u.nonStandardAddress = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandardAddress);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandardAddress", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 8;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  PublicTypeOfNumber                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225PublicTypeOfNumber (OOCTXT* pctxt, H225PublicTypeOfNumber* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 5);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* unknown */
         case 0:
            invokeStartElement (pctxt, "unknown", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "unknown", -1);

            break;

         /* internationalNumber */
         case 1:
            invokeStartElement (pctxt, "internationalNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "internationalNumber", -1);

            break;

         /* nationalNumber */
         case 2:
            invokeStartElement (pctxt, "nationalNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "nationalNumber", -1);

            break;

         /* networkSpecificNumber */
         case 3:
            invokeStartElement (pctxt, "networkSpecificNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "networkSpecificNumber", -1);

            break;

         /* subscriberNumber */
         case 4:
            invokeStartElement (pctxt, "subscriberNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "subscriberNumber", -1);

            break;

         /* abbreviatedNumber */
         case 5:
            invokeStartElement (pctxt, "abbreviatedNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "abbreviatedNumber", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 7;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  PublicPartyNumber                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225PublicPartyNumber (OOCTXT* pctxt, H225PublicPartyNumber* pvalue)
{
   int stat = ASN_OK;

   /* decode publicTypeOfNumber */

   invokeStartElement (pctxt, "publicTypeOfNumber", -1);

   stat = asn1PD_H225PublicTypeOfNumber (pctxt, &pvalue->publicTypeOfNumber);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "publicTypeOfNumber", -1);

   /* decode publicNumberDigits */

   invokeStartElement (pctxt, "publicNumberDigits", -1);

   stat = asn1PD_H225NumberDigits (pctxt, &pvalue->publicNumberDigits);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "publicNumberDigits", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  PrivateTypeOfNumber                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225PrivateTypeOfNumber (OOCTXT* pctxt, H225PrivateTypeOfNumber* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 5);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* unknown */
         case 0:
            invokeStartElement (pctxt, "unknown", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "unknown", -1);

            break;

         /* level2RegionalNumber */
         case 1:
            invokeStartElement (pctxt, "level2RegionalNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "level2RegionalNumber", -1);

            break;

         /* level1RegionalNumber */
         case 2:
            invokeStartElement (pctxt, "level1RegionalNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "level1RegionalNumber", -1);

            break;

         /* pISNSpecificNumber */
         case 3:
            invokeStartElement (pctxt, "pISNSpecificNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "pISNSpecificNumber", -1);

            break;

         /* localNumber */
         case 4:
            invokeStartElement (pctxt, "localNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "localNumber", -1);

            break;

         /* abbreviatedNumber */
         case 5:
            invokeStartElement (pctxt, "abbreviatedNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "abbreviatedNumber", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 7;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  PrivatePartyNumber                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225PrivatePartyNumber (OOCTXT* pctxt, H225PrivatePartyNumber* pvalue)
{
   int stat = ASN_OK;

   /* decode privateTypeOfNumber */

   invokeStartElement (pctxt, "privateTypeOfNumber", -1);

   stat = asn1PD_H225PrivateTypeOfNumber (pctxt, &pvalue->privateTypeOfNumber);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "privateTypeOfNumber", -1);

   /* decode privateNumberDigits */

   invokeStartElement (pctxt, "privateNumberDigits", -1);

   stat = asn1PD_H225NumberDigits (pctxt, &pvalue->privateNumberDigits);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "privateNumberDigits", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  PartyNumber                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225PartyNumber (OOCTXT* pctxt, H225PartyNumber* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 4);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* e164Number */
         case 0:
            invokeStartElement (pctxt, "e164Number", -1);

            pvalue->u.e164Number = ALLOC_ASN1ELEM (pctxt, H225PublicPartyNumber);

            stat = asn1PD_H225PublicPartyNumber (pctxt, pvalue->u.e164Number);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "e164Number", -1);

            break;

         /* dataPartyNumber */
         case 1:
            invokeStartElement (pctxt, "dataPartyNumber", -1);

            stat = asn1PD_H225NumberDigits (pctxt, &pvalue->u.dataPartyNumber);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "dataPartyNumber", -1);

            break;

         /* telexPartyNumber */
         case 2:
            invokeStartElement (pctxt, "telexPartyNumber", -1);

            stat = asn1PD_H225NumberDigits (pctxt, &pvalue->u.telexPartyNumber);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "telexPartyNumber", -1);

            break;

         /* privateNumber */
         case 3:
            invokeStartElement (pctxt, "privateNumber", -1);

            pvalue->u.privateNumber = ALLOC_ASN1ELEM (pctxt, H225PrivatePartyNumber);

            stat = asn1PD_H225PrivatePartyNumber (pctxt, pvalue->u.privateNumber);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "privateNumber", -1);

            break;

         /* nationalStandardPartyNumber */
         case 4:
            invokeStartElement (pctxt, "nationalStandardPartyNumber", -1);

            stat = asn1PD_H225NumberDigits (pctxt, &pvalue->u.nationalStandardPartyNumber);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nationalStandardPartyNumber", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 6;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ANSI_41_UIM_system_id                                     */
/*                                                            */
/**************************************************************/

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_system_id_sid_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_system_id_mid_CharSet;

EXTERN int asn1PD_H225ANSI_41_UIM_system_id (OOCTXT* pctxt, H225ANSI_41_UIM_system_id* pvalue)
{
   static Asn1SizeCnst sid_lsize1 = { 0, 1, 4, 0 };
   static Asn1SizeCnst mid_lsize1 = { 0, 1, 4, 0 };
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* sid */
         case 0:
            invokeStartElement (pctxt, "sid", -1);

            addSizeConstraint (pctxt, &sid_lsize1);

            stat = decodeConstrainedStringEx (pctxt, &pvalue->u.sid, gs_H323_MESSAGES_ANSI_41_UIM_system_id_sid_CharSet, 4, 4, 7);
            if (stat != ASN_OK) return stat;
            invokeCharStrValue (pctxt, pvalue->u.sid);

            invokeEndElement (pctxt, "sid", -1);

            break;

         /* mid */
         case 1:
            invokeStartElement (pctxt, "mid", -1);

            addSizeConstraint (pctxt, &mid_lsize1);

            stat = decodeConstrainedStringEx (pctxt, &pvalue->u.mid, gs_H323_MESSAGES_ANSI_41_UIM_system_id_mid_CharSet, 4, 4, 7);
            if (stat != ASN_OK) return stat;
            invokeCharStrValue (pctxt, pvalue->u.mid);

            invokeEndElement (pctxt, "mid", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ANSI_41_UIM_systemMyTypeCode                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ANSI_41_UIM_systemMyTypeCode (OOCTXT* pctxt, H225ANSI_41_UIM_systemMyTypeCode* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 1, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ANSI_41_UIM_systemAccessType                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ANSI_41_UIM_systemAccessType (OOCTXT* pctxt, H225ANSI_41_UIM_systemAccessType* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 1, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ANSI_41_UIM_qualificationInformationCode                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ANSI_41_UIM_qualificationInformationCode (OOCTXT* pctxt, H225ANSI_41_UIM_qualificationInformationCode* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 1, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ANSI_41_UIM                                               */
/*                                                            */
/**************************************************************/

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_imsi_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_min_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_mdn_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_msisdn_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_esn_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_mscid_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_sesn_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_ANSI_41_UIM_soc_CharSet;

EXTERN int asn1PD_H225ANSI_41_UIM (OOCTXT* pctxt, H225ANSI_41_UIM* pvalue)
{
   static Asn1SizeCnst imsi_lsize1 = { 0, 3, 16, 0 };
   static Asn1SizeCnst min_lsize1 = { 0, 3, 16, 0 };
   static Asn1SizeCnst mdn_lsize1 = { 0, 3, 16, 0 };
   static Asn1SizeCnst msisdn_lsize1 = { 0, 3, 16, 0 };
   static Asn1SizeCnst esn_lsize1 = { 0, 16, 16, 0 };
   static Asn1SizeCnst mscid_lsize1 = { 0, 3, 16, 0 };
   static Asn1SizeCnst sesn_lsize1 = { 0, 16, 16, 0 };
   static Asn1SizeCnst soc_lsize1 = { 0, 3, 16, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.imsiPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.minPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.mdnPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.msisdnPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.esnPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.mscidPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.systemMyTypeCodePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.systemAccessTypePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.qualificationInformationCodePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.sesnPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.socPresent = optbit;

   /* decode imsi */

   if (pvalue->m.imsiPresent) {
      invokeStartElement (pctxt, "imsi", -1);

      addSizeConstraint (pctxt, &imsi_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->imsi, gs_H323_MESSAGES_ANSI_41_UIM_imsi_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->imsi);

      invokeEndElement (pctxt, "imsi", -1);
   }

   /* decode min */

   if (pvalue->m.minPresent) {
      invokeStartElement (pctxt, "min", -1);

      addSizeConstraint (pctxt, &min_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->min, gs_H323_MESSAGES_ANSI_41_UIM_min_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->min);

      invokeEndElement (pctxt, "min", -1);
   }

   /* decode mdn */

   if (pvalue->m.mdnPresent) {
      invokeStartElement (pctxt, "mdn", -1);

      addSizeConstraint (pctxt, &mdn_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->mdn, gs_H323_MESSAGES_ANSI_41_UIM_mdn_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->mdn);

      invokeEndElement (pctxt, "mdn", -1);
   }

   /* decode msisdn */

   if (pvalue->m.msisdnPresent) {
      invokeStartElement (pctxt, "msisdn", -1);

      addSizeConstraint (pctxt, &msisdn_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->msisdn, gs_H323_MESSAGES_ANSI_41_UIM_msisdn_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->msisdn);

      invokeEndElement (pctxt, "msisdn", -1);
   }

   /* decode esn */

   if (pvalue->m.esnPresent) {
      invokeStartElement (pctxt, "esn", -1);

      addSizeConstraint (pctxt, &esn_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->esn, gs_H323_MESSAGES_ANSI_41_UIM_esn_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->esn);

      invokeEndElement (pctxt, "esn", -1);
   }

   /* decode mscid */

   if (pvalue->m.mscidPresent) {
      invokeStartElement (pctxt, "mscid", -1);

      addSizeConstraint (pctxt, &mscid_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->mscid, gs_H323_MESSAGES_ANSI_41_UIM_mscid_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->mscid);

      invokeEndElement (pctxt, "mscid", -1);
   }

   /* decode system_id */

   invokeStartElement (pctxt, "system_id", -1);

   stat = asn1PD_H225ANSI_41_UIM_system_id (pctxt, &pvalue->system_id);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "system_id", -1);

   /* decode systemMyTypeCode */

   if (pvalue->m.systemMyTypeCodePresent) {
      invokeStartElement (pctxt, "systemMyTypeCode", -1);

      stat = asn1PD_H225ANSI_41_UIM_systemMyTypeCode (pctxt, &pvalue->systemMyTypeCode);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "systemMyTypeCode", -1);
   }

   /* decode systemAccessType */

   if (pvalue->m.systemAccessTypePresent) {
      invokeStartElement (pctxt, "systemAccessType", -1);

      stat = asn1PD_H225ANSI_41_UIM_systemAccessType (pctxt, &pvalue->systemAccessType);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "systemAccessType", -1);
   }

   /* decode qualificationInformationCode */

   if (pvalue->m.qualificationInformationCodePresent) {
      invokeStartElement (pctxt, "qualificationInformationCode", -1);

      stat = asn1PD_H225ANSI_41_UIM_qualificationInformationCode (pctxt, &pvalue->qualificationInformationCode);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "qualificationInformationCode", -1);
   }

   /* decode sesn */

   if (pvalue->m.sesnPresent) {
      invokeStartElement (pctxt, "sesn", -1);

      addSizeConstraint (pctxt, &sesn_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->sesn, gs_H323_MESSAGES_ANSI_41_UIM_sesn_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->sesn);

      invokeEndElement (pctxt, "sesn", -1);
   }

   /* decode soc */

   if (pvalue->m.socPresent) {
      invokeStartElement (pctxt, "soc", -1);

      addSizeConstraint (pctxt, &soc_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->soc, gs_H323_MESSAGES_ANSI_41_UIM_soc_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->soc);

      invokeEndElement (pctxt, "soc", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GSM_UIM_tmsi                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GSM_UIM_tmsi (OOCTXT* pctxt, H225GSM_UIM_tmsi* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 4, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GSM_UIM                                                   */
/*                                                            */
/**************************************************************/

extern EXTERN const char* gs_H323_MESSAGES_GSM_UIM_imsi_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_GSM_UIM_msisdn_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_GSM_UIM_imei_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_GSM_UIM_hplmn_CharSet;

extern EXTERN const char* gs_H323_MESSAGES_GSM_UIM_vplmn_CharSet;

EXTERN int asn1PD_H225GSM_UIM (OOCTXT* pctxt, H225GSM_UIM* pvalue)
{
   static Asn1SizeCnst imsi_lsize1 = { 0, 3, 16, 0 };
   static Asn1SizeCnst msisdn_lsize1 = { 0, 3, 16, 0 };
   static Asn1SizeCnst imei_lsize1 = { 0, 15, 16, 0 };
   static Asn1SizeCnst hplmn_lsize1 = { 0, 1, 4, 0 };
   static Asn1SizeCnst vplmn_lsize1 = { 0, 1, 4, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.imsiPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tmsiPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.msisdnPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.imeiPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.hplmnPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.vplmnPresent = optbit;

   /* decode imsi */

   if (pvalue->m.imsiPresent) {
      invokeStartElement (pctxt, "imsi", -1);

      addSizeConstraint (pctxt, &imsi_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->imsi, gs_H323_MESSAGES_GSM_UIM_imsi_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->imsi);

      invokeEndElement (pctxt, "imsi", -1);
   }

   /* decode tmsi */

   if (pvalue->m.tmsiPresent) {
      invokeStartElement (pctxt, "tmsi", -1);

      stat = asn1PD_H225GSM_UIM_tmsi (pctxt, &pvalue->tmsi);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tmsi", -1);
   }

   /* decode msisdn */

   if (pvalue->m.msisdnPresent) {
      invokeStartElement (pctxt, "msisdn", -1);

      addSizeConstraint (pctxt, &msisdn_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->msisdn, gs_H323_MESSAGES_GSM_UIM_msisdn_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->msisdn);

      invokeEndElement (pctxt, "msisdn", -1);
   }

   /* decode imei */

   if (pvalue->m.imeiPresent) {
      invokeStartElement (pctxt, "imei", -1);

      addSizeConstraint (pctxt, &imei_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->imei, gs_H323_MESSAGES_GSM_UIM_imei_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->imei);

      invokeEndElement (pctxt, "imei", -1);
   }

   /* decode hplmn */

   if (pvalue->m.hplmnPresent) {
      invokeStartElement (pctxt, "hplmn", -1);

      addSizeConstraint (pctxt, &hplmn_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->hplmn, gs_H323_MESSAGES_GSM_UIM_hplmn_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->hplmn);

      invokeEndElement (pctxt, "hplmn", -1);
   }

   /* decode vplmn */

   if (pvalue->m.vplmnPresent) {
      invokeStartElement (pctxt, "vplmn", -1);

      addSizeConstraint (pctxt, &vplmn_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->vplmn, gs_H323_MESSAGES_GSM_UIM_vplmn_CharSet, 4, 4, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->vplmn);

      invokeEndElement (pctxt, "vplmn", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  MobileUIM                                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225MobileUIM (OOCTXT* pctxt, H225MobileUIM* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* ansi_41_uim */
         case 0:
            invokeStartElement (pctxt, "ansi_41_uim", -1);

            pvalue->u.ansi_41_uim = ALLOC_ASN1ELEM (pctxt, H225ANSI_41_UIM);

            stat = asn1PD_H225ANSI_41_UIM (pctxt, pvalue->u.ansi_41_uim);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "ansi_41_uim", -1);

            break;

         /* gsm_uim */
         case 1:
            invokeStartElement (pctxt, "gsm_uim", -1);

            pvalue->u.gsm_uim = ALLOC_ASN1ELEM (pctxt, H225GSM_UIM);

            stat = asn1PD_H225GSM_UIM (pctxt, pvalue->u.gsm_uim);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "gsm_uim", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AliasAddress                                              */
/*                                                            */
/**************************************************************/

extern EXTERN const char* gs_H323_MESSAGES_AliasAddress_dialedDigits_CharSet;

EXTERN int asn1PD_H225AliasAddress (OOCTXT* pctxt, H225AliasAddress* pvalue)
{
   static Asn1SizeCnst dialedDigits_lsize1 = { 0, 1, 128, 0 };
   static Asn1SizeCnst h323_ID_lsize1 = { 0, 1, 256, 0 };
   static Asn1SizeCnst url_ID_lsize1 = { 0, 1, 512, 0 };
   static Asn1SizeCnst email_ID_lsize1 = { 0, 1, 512, 0 };
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* dialedDigits */
         case 0:
            invokeStartElement (pctxt, "dialedDigits", -1);

            addSizeConstraint (pctxt, &dialedDigits_lsize1);

            stat = decodeConstrainedStringEx (pctxt, &pvalue->u.dialedDigits, gs_H323_MESSAGES_AliasAddress_dialedDigits_CharSet, 4, 4, 7);
            if (stat != ASN_OK) return stat;
            invokeCharStrValue (pctxt, pvalue->u.dialedDigits);

            invokeEndElement (pctxt, "dialedDigits", -1);

            break;

         /* h323_ID */
         case 1:
            invokeStartElement (pctxt, "h323_ID", -1);

            addSizeConstraint (pctxt, &h323_ID_lsize1);

            stat = decodeBMPString (pctxt, &pvalue->u.h323_ID, 0);
            if (stat != ASN_OK) return stat;
            invokeCharStr16BitValue (pctxt, pvalue->u.h323_ID.nchars, pvalue->u.h323_ID.data);

            invokeEndElement (pctxt, "h323_ID", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* url_ID */
         case 3:
            invokeStartElement (pctxt, "url_ID", -1);

            addSizeConstraint (pctxt, &url_ID_lsize1);

            stat = decodeConstrainedStringEx (pctxt, &pvalue->u.url_ID, 0, 8, 7, 7);
            if (stat != ASN_OK) return stat;
            invokeCharStrValue (pctxt, pvalue->u.url_ID);

            invokeEndElement (pctxt, "url_ID", -1);

            break;

         /* transportID */
         case 4:
            invokeStartElement (pctxt, "transportID", -1);

            pvalue->u.transportID = ALLOC_ASN1ELEM (pctxt, H225TransportAddress);

            stat = asn1PD_H225TransportAddress (pctxt, pvalue->u.transportID);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "transportID", -1);

            break;

         /* email_ID */
         case 5:
            invokeStartElement (pctxt, "email_ID", -1);

            addSizeConstraint (pctxt, &email_ID_lsize1);

            stat = decodeConstrainedStringEx (pctxt, &pvalue->u.email_ID, 0, 8, 7, 7);
            if (stat != ASN_OK) return stat;
            invokeCharStrValue (pctxt, pvalue->u.email_ID);

            invokeEndElement (pctxt, "email_ID", -1);

            break;

         /* partyNumber */
         case 6:
            invokeStartElement (pctxt, "partyNumber", -1);

            pvalue->u.partyNumber = ALLOC_ASN1ELEM (pctxt, H225PartyNumber);

            stat = asn1PD_H225PartyNumber (pctxt, pvalue->u.partyNumber);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "partyNumber", -1);

            break;

         /* mobileUIM */
         case 7:
            invokeStartElement (pctxt, "mobileUIM", -1);

            pvalue->u.mobileUIM = ALLOC_ASN1ELEM (pctxt, H225MobileUIM);

            stat = asn1PD_H225MobileUIM (pctxt, pvalue->u.mobileUIM);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "mobileUIM", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Content_compound                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Content_compound (OOCTXT* pctxt, H225Content_compound* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 512, 0 };
   int stat = ASN_OK;
   H225EnumeratedParameter* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;

   /* decode length determinant */

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeLength (pctxt, &count);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   dListInit (pvalue);

   for (xx1 = 0; xx1 < count; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225EnumeratedParameter);

      stat = asn1PD_H225EnumeratedParameter (pctxt, (H225EnumeratedParameter*)pdata);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

      dListAppendNode (pctxt, pvalue, pdata);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Content_nested                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Content_nested (OOCTXT* pctxt, H225Content_nested* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 16, 0 };
   int stat = ASN_OK;
   H225GenericData* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;

   /* decode length determinant */

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeLength (pctxt, &count);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   dListInit (pvalue);

   for (xx1 = 0; xx1 < count; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225GenericData);

      stat = asn1PD_H225GenericData (pctxt, (H225GenericData*)pdata);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

      dListAppendNode (pctxt, pvalue, pdata);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Content                                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Content (OOCTXT* pctxt, H225Content* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 11);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* raw */
         case 0:
            invokeStartElement (pctxt, "raw", -1);

            pvalue->u.raw = ALLOC_ASN1ELEM (pctxt, ASN1DynOctStr);

            stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)pvalue->u.raw);
            if (stat != ASN_OK) return stat;
            invokeOctStrValue (pctxt, pvalue->u.raw->numocts, pvalue->u.raw->data);

            invokeEndElement (pctxt, "raw", -1);

            break;

         /* text */
         case 1:
            invokeStartElement (pctxt, "text", -1);

            stat = decodeConstrainedStringEx (pctxt, &pvalue->u.text, 0, 8, 7, 7);
            if (stat != ASN_OK) return stat;
            invokeCharStrValue (pctxt, pvalue->u.text);

            invokeEndElement (pctxt, "text", -1);

            break;

         /* unicode */
         case 2:
            invokeStartElement (pctxt, "unicode", -1);

            stat = decodeBMPString (pctxt, &pvalue->u.unicode, 0);
            if (stat != ASN_OK) return stat;
            invokeCharStr16BitValue (pctxt, pvalue->u.unicode.nchars, pvalue->u.unicode.data);

            invokeEndElement (pctxt, "unicode", -1);

            break;

         /* bool_ */
         case 3:
            invokeStartElement (pctxt, "bool_", -1);

            stat = DECODEBIT (pctxt, &pvalue->u.bool_);
            if (stat != ASN_OK) return stat;
            invokeBoolValue (pctxt, pvalue->u.bool_);

            invokeEndElement (pctxt, "bool_", -1);

            break;

         /* number8 */
         case 4:
            invokeStartElement (pctxt, "number8", -1);

            stat = decodeConsUInt8 (pctxt, &pvalue->u.number8, 0U, 255U);
            if (stat != ASN_OK) return stat;
            invokeUIntValue (pctxt, pvalue->u.number8);

            invokeEndElement (pctxt, "number8", -1);

            break;

         /* number16 */
         case 5:
            invokeStartElement (pctxt, "number16", -1);

            stat = decodeConsUInt16 (pctxt, &pvalue->u.number16, 0U, 65535U);
            if (stat != ASN_OK) return stat;
            invokeUIntValue (pctxt, pvalue->u.number16);

            invokeEndElement (pctxt, "number16", -1);

            break;

         /* number32 */
         case 6:
            invokeStartElement (pctxt, "number32", -1);

            stat = decodeConsUnsigned (pctxt, &pvalue->u.number32, 0U, ASN1UINT_MAX);
            if (stat != ASN_OK) return stat;
            invokeUIntValue (pctxt, pvalue->u.number32);

            invokeEndElement (pctxt, "number32", -1);

            break;

         /* id */
         case 7:
            invokeStartElement (pctxt, "id", -1);

            pvalue->u.id = ALLOC_ASN1ELEM (pctxt, H225GenericIdentifier);

            stat = asn1PD_H225GenericIdentifier (pctxt, pvalue->u.id);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "id", -1);

            break;

         /* alias */
         case 8:
            invokeStartElement (pctxt, "alias", -1);

            pvalue->u.alias = ALLOC_ASN1ELEM (pctxt, H225AliasAddress);

            stat = asn1PD_H225AliasAddress (pctxt, pvalue->u.alias);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "alias", -1);

            break;

         /* transport */
         case 9:
            invokeStartElement (pctxt, "transport", -1);

            pvalue->u.transport = ALLOC_ASN1ELEM (pctxt, H225TransportAddress);

            stat = asn1PD_H225TransportAddress (pctxt, pvalue->u.transport);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "transport", -1);

            break;

         /* compound */
         case 10:
            invokeStartElement (pctxt, "compound", -1);

            pvalue->u.compound = ALLOC_ASN1ELEM (pctxt, H225Content_compound);

            stat = asn1PD_H225Content_compound (pctxt, pvalue->u.compound);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "compound", -1);

            break;

         /* nested */
         case 11:
            invokeStartElement (pctxt, "nested", -1);

            pvalue->u.nested = ALLOC_ASN1ELEM (pctxt, H225Content_nested);

            stat = asn1PD_H225Content_nested (pctxt, pvalue->u.nested);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nested", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 13;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  EnumeratedParameter                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225EnumeratedParameter (OOCTXT* pctxt, H225EnumeratedParameter* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.contentPresent = optbit;

   /* decode id */

   invokeStartElement (pctxt, "id", -1);

   stat = asn1PD_H225GenericIdentifier (pctxt, &pvalue->id);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "id", -1);

   /* decode content */

   if (pvalue->m.contentPresent) {
      invokeStartElement (pctxt, "content", -1);

      stat = asn1PD_H225Content (pctxt, &pvalue->content);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "content", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GenericData_parameters                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GenericData_parameters (OOCTXT* pctxt, H225GenericData_parameters* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 512, 0 };
   int stat = ASN_OK;
   H225EnumeratedParameter* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;

   /* decode length determinant */

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeLength (pctxt, &count);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   dListInit (pvalue);

   for (xx1 = 0; xx1 < count; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225EnumeratedParameter);

      stat = asn1PD_H225EnumeratedParameter (pctxt, pdata);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

      dListAppendNode (pctxt, pvalue, pdata);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GenericData                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GenericData (OOCTXT* pctxt, H225GenericData* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.parametersPresent = optbit;

   /* decode id */

   invokeStartElement (pctxt, "id", -1);

   stat = asn1PD_H225GenericIdentifier (pctxt, &pvalue->id);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "id", -1);

   /* decode parameters */

   if (pvalue->m.parametersPresent) {
      invokeStartElement (pctxt, "parameters", -1);

      stat = asn1PD_H225GenericData_parameters (pctxt, &pvalue->parameters);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "parameters", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  FeatureDescriptor                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225FeatureDescriptor (OOCTXT* pctxt, H225FeatureDescriptor* pvalue)
{
   int stat = ASN_OK;

   stat = asn1PD_H225GenericData (pctxt, pvalue);
   if (stat != ASN_OK) return stat;

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  FastStartToken                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225FastStartToken (OOCTXT* pctxt, H225FastStartToken* pvalue)
{
   int stat = ASN_OK;

   stat = asn1PD_H235ClearToken (pctxt, pvalue);
   if (stat != ASN_OK) return stat;

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  EncodedFastStartToken                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225EncodedFastStartToken (OOCTXT* pctxt, H225EncodedFastStartToken* pvalue)
{
   int stat = ASN_OK;

   stat = decodeOpenType (pctxt, &pvalue->data, &pvalue->numocts);
   if (stat != ASN_OK) return stat;
   invokeOpenTypeValue
      (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UserInformation_user_data_user_information           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UserInformation_user_data_user_information (OOCTXT* pctxt, H225H323_UserInformation_user_data_user_information* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 131, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  EndpointType_set                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225EndpointType_set (OOCTXT* pctxt, H225EndpointType_set* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 32, 32, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeBitString (pctxt,
                        &pvalue->numbits,
                        pvalue->data,
                        sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;

   invokeBitStrValue (pctxt, pvalue->numbits, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  VendorIdentifier_productId                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225VendorIdentifier_productId (OOCTXT* pctxt, H225VendorIdentifier_productId* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 256, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  VendorIdentifier_versionId                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225VendorIdentifier_versionId (OOCTXT* pctxt, H225VendorIdentifier_versionId* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 1, 256, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CicInfo_cic_element                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CicInfo_cic_element (OOCTXT* pctxt, H225CicInfo_cic_element* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 2, 4, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CicInfo_pointCode                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CicInfo_pointCode (OOCTXT* pctxt, H225CicInfo_pointCode* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 2, 5, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CarrierInfo_carrierIdentificationCode                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CarrierInfo_carrierIdentificationCode (OOCTXT* pctxt, H225CarrierInfo_carrierIdentificationCode* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 3, 4, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallTerminationCause_releaseCompleteCauseIE               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallTerminationCause_releaseCompleteCauseIE (OOCTXT* pctxt, H225CallTerminationCause_releaseCompleteCauseIE* pvalue)
{
   static Asn1SizeCnst lsize1 = { 0, 2, 32, 0 };
   int stat = ASN_OK;

   addSizeConstraint (pctxt, &lsize1);

   stat = decodeOctetString (pctxt,
                          &pvalue->numocts,
                          pvalue->data,
                          sizeof(pvalue->data));
   if (stat != ASN_OK) return stat;
   invokeOctStrValue (pctxt, pvalue->numocts, pvalue->data);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225AliasAddress                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225AliasAddress (OOCTXT* pctxt, H225_SeqOfH225AliasAddress* pvalue)
{
   int stat = ASN_OK;
   H225AliasAddress* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225AliasAddress);

         stat = asn1PD_H225AliasAddress (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  VendorIdentifier                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225VendorIdentifier (OOCTXT* pctxt, H225VendorIdentifier* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.productIdPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.versionIdPresent = optbit;

   /* decode vendor */

   invokeStartElement (pctxt, "vendor", -1);

   stat = asn1PD_H225H221NonStandard (pctxt, &pvalue->vendor);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "vendor", -1);

   /* decode productId */

   if (pvalue->m.productIdPresent) {
      invokeStartElement (pctxt, "productId", -1);

      stat = asn1PD_H225VendorIdentifier_productId (pctxt, &pvalue->productId);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "productId", -1);
   }

   /* decode versionId */

   if (pvalue->m.versionIdPresent) {
      invokeStartElement (pctxt, "versionId", -1);

      stat = asn1PD_H225VendorIdentifier_versionId (pctxt, &pvalue->versionId);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "versionId", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 1 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.enterpriseNumberPresent = 1;

                     invokeStartElement (pctxt, "enterpriseNumber", -1);

                     stat = decodeObjectIdentifier (pctxt, &pvalue->enterpriseNumber);
                     if (stat != ASN_OK) return stat;
                     invokeOidValue (pctxt, pvalue->enterpriseNumber.numids, pvalue->enterpriseNumber.subid);

                     invokeEndElement (pctxt, "enterpriseNumber", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatekeeperInfo                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatekeeperInfo (OOCTXT* pctxt, H225GatekeeperInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  DataRate                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225DataRate (OOCTXT* pctxt, H225DataRate* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.channelMultiplierPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode channelRate */

   invokeStartElement (pctxt, "channelRate", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->channelRate);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "channelRate", -1);

   /* decode channelMultiplier */

   if (pvalue->m.channelMultiplierPresent) {
      invokeStartElement (pctxt, "channelMultiplier", -1);

      stat = decodeConsUInt16 (pctxt, &pvalue->channelMultiplier, 1U, 256U);
      if (stat != ASN_OK) return stat;
      invokeUIntValue (pctxt, pvalue->channelMultiplier);

      invokeEndElement (pctxt, "channelMultiplier", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225DataRate                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225DataRate (OOCTXT* pctxt, H225_SeqOfH225DataRate* pvalue)
{
   int stat = ASN_OK;
   H225DataRate* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225DataRate);

         stat = asn1PD_H225DataRate (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SupportedPrefix                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SupportedPrefix (OOCTXT* pctxt, H225SupportedPrefix* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode prefix */

   invokeStartElement (pctxt, "prefix", -1);

   stat = asn1PD_H225AliasAddress (pctxt, &pvalue->prefix);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "prefix", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225SupportedPrefix                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225SupportedPrefix (OOCTXT* pctxt, H225_SeqOfH225SupportedPrefix* pvalue)
{
   int stat = ASN_OK;
   H225SupportedPrefix* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225SupportedPrefix);

         stat = asn1PD_H225SupportedPrefix (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H310Caps                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H310Caps (OOCTXT* pctxt, H225H310Caps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H320Caps                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H320Caps (OOCTXT* pctxt, H225H320Caps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H321Caps                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H321Caps (OOCTXT* pctxt, H225H321Caps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H322Caps                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H322Caps (OOCTXT* pctxt, H225H322Caps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323Caps                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323Caps (OOCTXT* pctxt, H225H323Caps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H324Caps                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H324Caps (OOCTXT* pctxt, H225H324Caps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  VoiceCaps                                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225VoiceCaps (OOCTXT* pctxt, H225VoiceCaps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  T120OnlyCaps                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225T120OnlyCaps (OOCTXT* pctxt, H225T120OnlyCaps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.dataRatesSupportedPresent = 1;

                     invokeStartElement (pctxt, "dataRatesSupported", -1);

                     stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "dataRatesSupported", -1);
                     break;

                  case 1:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  NonStandardProtocol                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225NonStandardProtocol (OOCTXT* pctxt, H225NonStandardProtocol* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.dataRatesSupportedPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode dataRatesSupported */

   if (pvalue->m.dataRatesSupportedPresent) {
      invokeStartElement (pctxt, "dataRatesSupported", -1);

      stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "dataRatesSupported", -1);
   }

   /* decode supportedPrefixes */

   invokeStartElement (pctxt, "supportedPrefixes", -1);

   stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "supportedPrefixes", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  T38FaxAnnexbOnlyCaps                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225T38FaxAnnexbOnlyCaps (OOCTXT* pctxt, H225T38FaxAnnexbOnlyCaps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.dataRatesSupportedPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode dataRatesSupported */

   if (pvalue->m.dataRatesSupportedPresent) {
      invokeStartElement (pctxt, "dataRatesSupported", -1);

      stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "dataRatesSupported", -1);
   }

   /* decode supportedPrefixes */

   invokeStartElement (pctxt, "supportedPrefixes", -1);

   stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "supportedPrefixes", -1);

   /* decode t38FaxProtocol */

   invokeStartElement (pctxt, "t38FaxProtocol", -1);

   stat = asn1PD_H245DataProtocolCapability (pctxt, &pvalue->t38FaxProtocol);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "t38FaxProtocol", -1);

   /* decode t38FaxProfile */

   invokeStartElement (pctxt, "t38FaxProfile", -1);

   stat = asn1PD_H245T38FaxProfile (pctxt, &pvalue->t38FaxProfile);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "t38FaxProfile", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SIPCaps                                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SIPCaps (OOCTXT* pctxt, H225SIPCaps* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.dataRatesSupportedPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.supportedPrefixesPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode dataRatesSupported */

   if (pvalue->m.dataRatesSupportedPresent) {
      invokeStartElement (pctxt, "dataRatesSupported", -1);

      stat = asn1PD_H225_SeqOfH225DataRate (pctxt, &pvalue->dataRatesSupported);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "dataRatesSupported", -1);
   }

   /* decode supportedPrefixes */

   if (pvalue->m.supportedPrefixesPresent) {
      invokeStartElement (pctxt, "supportedPrefixes", -1);

      stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "supportedPrefixes", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SupportedProtocols                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SupportedProtocols (OOCTXT* pctxt, H225SupportedProtocols* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 8);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* nonStandardData */
         case 0:
            invokeStartElement (pctxt, "nonStandardData", -1);

            pvalue->u.nonStandardData = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandardData);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandardData", -1);

            break;

         /* h310 */
         case 1:
            invokeStartElement (pctxt, "h310", -1);

            pvalue->u.h310 = ALLOC_ASN1ELEM (pctxt, H225H310Caps);

            stat = asn1PD_H225H310Caps (pctxt, pvalue->u.h310);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "h310", -1);

            break;

         /* h320 */
         case 2:
            invokeStartElement (pctxt, "h320", -1);

            pvalue->u.h320 = ALLOC_ASN1ELEM (pctxt, H225H320Caps);

            stat = asn1PD_H225H320Caps (pctxt, pvalue->u.h320);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "h320", -1);

            break;

         /* h321 */
         case 3:
            invokeStartElement (pctxt, "h321", -1);

            pvalue->u.h321 = ALLOC_ASN1ELEM (pctxt, H225H321Caps);

            stat = asn1PD_H225H321Caps (pctxt, pvalue->u.h321);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "h321", -1);

            break;

         /* h322 */
         case 4:
            invokeStartElement (pctxt, "h322", -1);

            pvalue->u.h322 = ALLOC_ASN1ELEM (pctxt, H225H322Caps);

            stat = asn1PD_H225H322Caps (pctxt, pvalue->u.h322);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "h322", -1);

            break;

         /* h323 */
         case 5:
            invokeStartElement (pctxt, "h323", -1);

            pvalue->u.h323 = ALLOC_ASN1ELEM (pctxt, H225H323Caps);

            stat = asn1PD_H225H323Caps (pctxt, pvalue->u.h323);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "h323", -1);

            break;

         /* h324 */
         case 6:
            invokeStartElement (pctxt, "h324", -1);

            pvalue->u.h324 = ALLOC_ASN1ELEM (pctxt, H225H324Caps);

            stat = asn1PD_H225H324Caps (pctxt, pvalue->u.h324);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "h324", -1);

            break;

         /* voice */
         case 7:
            invokeStartElement (pctxt, "voice", -1);

            pvalue->u.voice = ALLOC_ASN1ELEM (pctxt, H225VoiceCaps);

            stat = asn1PD_H225VoiceCaps (pctxt, pvalue->u.voice);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "voice", -1);

            break;

         /* t120_only */
         case 8:
            invokeStartElement (pctxt, "t120_only", -1);

            pvalue->u.t120_only = ALLOC_ASN1ELEM (pctxt, H225T120OnlyCaps);

            stat = asn1PD_H225T120OnlyCaps (pctxt, pvalue->u.t120_only);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "t120_only", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 10;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* nonStandardProtocol */
         case 10:
            invokeStartElement (pctxt, "nonStandardProtocol", -1);

            pvalue->u.nonStandardProtocol = ALLOC_ASN1ELEM (pctxt, H225NonStandardProtocol);

            stat = asn1PD_H225NonStandardProtocol (pctxt, pvalue->u.nonStandardProtocol);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandardProtocol", -1);

            break;

         /* t38FaxAnnexbOnly */
         case 11:
            invokeStartElement (pctxt, "t38FaxAnnexbOnly", -1);

            pvalue->u.t38FaxAnnexbOnly = ALLOC_ASN1ELEM (pctxt, H225T38FaxAnnexbOnlyCaps);

            stat = asn1PD_H225T38FaxAnnexbOnlyCaps (pctxt, pvalue->u.t38FaxAnnexbOnly);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "t38FaxAnnexbOnly", -1);

            break;

         /* sip */
         case 12:
            invokeStartElement (pctxt, "sip", -1);

            pvalue->u.sip = ALLOC_ASN1ELEM (pctxt, H225SIPCaps);

            stat = asn1PD_H225SIPCaps (pctxt, pvalue->u.sip);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "sip", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225SupportedProtocols                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225SupportedProtocols (OOCTXT* pctxt, H225_SeqOfH225SupportedProtocols* pvalue)
{
   int stat = ASN_OK;
   H225SupportedProtocols* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225SupportedProtocols);

         stat = asn1PD_H225SupportedProtocols (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatewayInfo                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatewayInfo (OOCTXT* pctxt, H225GatewayInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.protocolPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode protocol */

   if (pvalue->m.protocolPresent) {
      invokeStartElement (pctxt, "protocol", -1);

      stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->protocol);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "protocol", -1);
   }

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  McuInfo                                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225McuInfo (OOCTXT* pctxt, H225McuInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 1 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.protocolPresent = 1;

                     invokeStartElement (pctxt, "protocol", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->protocol);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "protocol", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TerminalInfo                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TerminalInfo (OOCTXT* pctxt, H225TerminalInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TunnelledProtocolAlternateIdentifier                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TunnelledProtocolAlternateIdentifier (OOCTXT* pctxt, H225TunnelledProtocolAlternateIdentifier* pvalue)
{
   static Asn1SizeCnst protocolType_lsize1 = { 0, 1, 64, 0 };
   static Asn1SizeCnst protocolVariant_lsize1 = { 0, 1, 64, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.protocolVariantPresent = optbit;

   /* decode protocolType */

   invokeStartElement (pctxt, "protocolType", -1);

   addSizeConstraint (pctxt, &protocolType_lsize1);

   stat = decodeConstrainedStringEx (pctxt, &pvalue->protocolType, 0, 8, 7, 7);
   if (stat != ASN_OK) return stat;
   invokeCharStrValue (pctxt, pvalue->protocolType);

   invokeEndElement (pctxt, "protocolType", -1);

   /* decode protocolVariant */

   if (pvalue->m.protocolVariantPresent) {
      invokeStartElement (pctxt, "protocolVariant", -1);

      addSizeConstraint (pctxt, &protocolVariant_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->protocolVariant, 0, 8, 7, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->protocolVariant);

      invokeEndElement (pctxt, "protocolVariant", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TunnelledProtocol_id                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TunnelledProtocol_id (OOCTXT* pctxt, H225TunnelledProtocol_id* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* tunnelledProtocolObjectID */
         case 0:
            invokeStartElement (pctxt, "tunnelledProtocolObjectID", -1);

            pvalue->u.tunnelledProtocolObjectID = ALLOC_ASN1ELEM (pctxt, ASN1OBJID);

            stat = decodeObjectIdentifier (pctxt, pvalue->u.tunnelledProtocolObjectID);
            if (stat != ASN_OK) return stat;
            invokeOidValue (pctxt, pvalue->u.tunnelledProtocolObjectID->numids, pvalue->u.tunnelledProtocolObjectID->subid);

            invokeEndElement (pctxt, "tunnelledProtocolObjectID", -1);

            break;

         /* tunnelledProtocolAlternateID */
         case 1:
            invokeStartElement (pctxt, "tunnelledProtocolAlternateID", -1);

            pvalue->u.tunnelledProtocolAlternateID = ALLOC_ASN1ELEM (pctxt, H225TunnelledProtocolAlternateIdentifier);

            stat = asn1PD_H225TunnelledProtocolAlternateIdentifier (pctxt, pvalue->u.tunnelledProtocolAlternateID);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "tunnelledProtocolAlternateID", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TunnelledProtocol                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TunnelledProtocol (OOCTXT* pctxt, H225TunnelledProtocol* pvalue)
{
   static Asn1SizeCnst subIdentifier_lsize1 = { 0, 1, 64, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.subIdentifierPresent = optbit;

   /* decode id */

   invokeStartElement (pctxt, "id", -1);

   stat = asn1PD_H225TunnelledProtocol_id (pctxt, &pvalue->id);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "id", -1);

   /* decode subIdentifier */

   if (pvalue->m.subIdentifierPresent) {
      invokeStartElement (pctxt, "subIdentifier", -1);

      addSizeConstraint (pctxt, &subIdentifier_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->subIdentifier, 0, 8, 7, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->subIdentifier);

      invokeEndElement (pctxt, "subIdentifier", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225TunnelledProtocol                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225TunnelledProtocol (OOCTXT* pctxt, H225_SeqOfH225TunnelledProtocol* pvalue)
{
   int stat = ASN_OK;
   H225TunnelledProtocol* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225TunnelledProtocol);

         stat = asn1PD_H225TunnelledProtocol (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  EndpointType                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225EndpointType (OOCTXT* pctxt, H225EndpointType* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.vendorPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatewayPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.mcuPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.terminalPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode vendor */

   if (pvalue->m.vendorPresent) {
      invokeStartElement (pctxt, "vendor", -1);

      stat = asn1PD_H225VendorIdentifier (pctxt, &pvalue->vendor);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "vendor", -1);
   }

   /* decode gatekeeper */

   if (pvalue->m.gatekeeperPresent) {
      invokeStartElement (pctxt, "gatekeeper", -1);

      stat = asn1PD_H225GatekeeperInfo (pctxt, &pvalue->gatekeeper);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeper", -1);
   }

   /* decode gateway */

   if (pvalue->m.gatewayPresent) {
      invokeStartElement (pctxt, "gateway", -1);

      stat = asn1PD_H225GatewayInfo (pctxt, &pvalue->gateway);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gateway", -1);
   }

   /* decode mcu */

   if (pvalue->m.mcuPresent) {
      invokeStartElement (pctxt, "mcu", -1);

      stat = asn1PD_H225McuInfo (pctxt, &pvalue->mcu);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "mcu", -1);
   }

   /* decode terminal */

   if (pvalue->m.terminalPresent) {
      invokeStartElement (pctxt, "terminal", -1);

      stat = asn1PD_H225TerminalInfo (pctxt, &pvalue->terminal);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "terminal", -1);
   }

   /* decode mc */

   invokeStartElement (pctxt, "mc", -1);

   stat = DECODEBIT (pctxt, &pvalue->mc);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->mc);

   invokeEndElement (pctxt, "mc", -1);

   /* decode undefinedNode */

   invokeStartElement (pctxt, "undefinedNode", -1);

   stat = DECODEBIT (pctxt, &pvalue->undefinedNode);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->undefinedNode);

   invokeEndElement (pctxt, "undefinedNode", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.setPresent = 1;

                     invokeStartElement (pctxt, "set", -1);

                     stat = asn1PD_H225EndpointType_set (pctxt, &pvalue->set);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "set", -1);
                     break;

                  case 1:
                     pvalue->m.supportedTunnelledProtocolsPresent = 1;

                     invokeStartElement (pctxt, "supportedTunnelledProtocols", -1);

                     stat = asn1PD_H225_SeqOfH225TunnelledProtocol (pctxt, &pvalue->supportedTunnelledProtocols);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedTunnelledProtocols", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225CallReferenceValue                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225CallReferenceValue (OOCTXT* pctxt, H225_SeqOfH225CallReferenceValue* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, H225CallReferenceValue);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = asn1PD_H225CallReferenceValue (pctxt, &pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Setup_UUIE_conferenceGoal                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Setup_UUIE_conferenceGoal (OOCTXT* pctxt, H225Setup_UUIE_conferenceGoal* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* create */
         case 0:
            invokeStartElement (pctxt, "create", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "create", -1);

            break;

         /* join */
         case 1:
            invokeStartElement (pctxt, "join", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "join", -1);

            break;

         /* invite */
         case 2:
            invokeStartElement (pctxt, "invite", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invite", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* capability_negotiation */
         case 4:
            invokeStartElement (pctxt, "capability_negotiation", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "capability_negotiation", -1);

            break;

         /* callIndependentSupplementaryService */
         case 5:
            invokeStartElement (pctxt, "callIndependentSupplementaryService", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "callIndependentSupplementaryService", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Q954Details                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Q954Details (OOCTXT* pctxt, H225Q954Details* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode conferenceCalling */

   invokeStartElement (pctxt, "conferenceCalling", -1);

   stat = DECODEBIT (pctxt, &pvalue->conferenceCalling);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->conferenceCalling);

   invokeEndElement (pctxt, "conferenceCalling", -1);

   /* decode threePartyService */

   invokeStartElement (pctxt, "threePartyService", -1);

   stat = DECODEBIT (pctxt, &pvalue->threePartyService);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->threePartyService);

   invokeEndElement (pctxt, "threePartyService", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  QseriesOptions                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225QseriesOptions (OOCTXT* pctxt, H225QseriesOptions* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode q932Full */

   invokeStartElement (pctxt, "q932Full", -1);

   stat = DECODEBIT (pctxt, &pvalue->q932Full);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->q932Full);

   invokeEndElement (pctxt, "q932Full", -1);

   /* decode q951Full */

   invokeStartElement (pctxt, "q951Full", -1);

   stat = DECODEBIT (pctxt, &pvalue->q951Full);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->q951Full);

   invokeEndElement (pctxt, "q951Full", -1);

   /* decode q952Full */

   invokeStartElement (pctxt, "q952Full", -1);

   stat = DECODEBIT (pctxt, &pvalue->q952Full);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->q952Full);

   invokeEndElement (pctxt, "q952Full", -1);

   /* decode q953Full */

   invokeStartElement (pctxt, "q953Full", -1);

   stat = DECODEBIT (pctxt, &pvalue->q953Full);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->q953Full);

   invokeEndElement (pctxt, "q953Full", -1);

   /* decode q955Full */

   invokeStartElement (pctxt, "q955Full", -1);

   stat = DECODEBIT (pctxt, &pvalue->q955Full);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->q955Full);

   invokeEndElement (pctxt, "q955Full", -1);

   /* decode q956Full */

   invokeStartElement (pctxt, "q956Full", -1);

   stat = DECODEBIT (pctxt, &pvalue->q956Full);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->q956Full);

   invokeEndElement (pctxt, "q956Full", -1);

   /* decode q957Full */

   invokeStartElement (pctxt, "q957Full", -1);

   stat = DECODEBIT (pctxt, &pvalue->q957Full);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->q957Full);

   invokeEndElement (pctxt, "q957Full", -1);

   /* decode q954Info */

   invokeStartElement (pctxt, "q954Info", -1);

   stat = asn1PD_H225Q954Details (pctxt, &pvalue->q954Info);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "q954Info", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallType                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallType (OOCTXT* pctxt, H225CallType* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* pointToPoint */
         case 0:
            invokeStartElement (pctxt, "pointToPoint", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "pointToPoint", -1);

            break;

         /* oneToN */
         case 1:
            invokeStartElement (pctxt, "oneToN", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "oneToN", -1);

            break;

         /* nToOne */
         case 2:
            invokeStartElement (pctxt, "nToOne", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "nToOne", -1);

            break;

         /* nToN */
         case 3:
            invokeStartElement (pctxt, "nToN", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "nToN", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallIdentifier                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallIdentifier (OOCTXT* pctxt, H225CallIdentifier* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode guid */

   invokeStartElement (pctxt, "guid", -1);

   stat = asn1PD_H225GloballyUniqueID (pctxt, &pvalue->guid);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "guid", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SecurityServiceMode                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SecurityServiceMode (OOCTXT* pctxt, H225SecurityServiceMode* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* nonStandard */
         case 0:
            invokeStartElement (pctxt, "nonStandard", -1);

            pvalue->u.nonStandard = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandard", -1);

            break;

         /* none */
         case 1:
            invokeStartElement (pctxt, "none", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "none", -1);

            break;

         /* default_ */
         case 2:
            invokeStartElement (pctxt, "default_", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "default_", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SecurityCapabilities                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SecurityCapabilities (OOCTXT* pctxt, H225SecurityCapabilities* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardPresent = optbit;

   /* decode nonStandard */

   if (pvalue->m.nonStandardPresent) {
      invokeStartElement (pctxt, "nonStandard", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandard);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandard", -1);
   }

   /* decode encryption */

   invokeStartElement (pctxt, "encryption", -1);

   stat = asn1PD_H225SecurityServiceMode (pctxt, &pvalue->encryption);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "encryption", -1);

   /* decode authenticaton */

   invokeStartElement (pctxt, "authenticaton", -1);

   stat = asn1PD_H225SecurityServiceMode (pctxt, &pvalue->authenticaton);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "authenticaton", -1);

   /* decode integrity */

   invokeStartElement (pctxt, "integrity", -1);

   stat = asn1PD_H225SecurityServiceMode (pctxt, &pvalue->integrity);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "integrity", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H245Security                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H245Security (OOCTXT* pctxt, H225H245Security* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* nonStandard */
         case 0:
            invokeStartElement (pctxt, "nonStandard", -1);

            pvalue->u.nonStandard = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandard", -1);

            break;

         /* noSecurity */
         case 1:
            invokeStartElement (pctxt, "noSecurity", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "noSecurity", -1);

            break;

         /* tls */
         case 2:
            invokeStartElement (pctxt, "tls", -1);

            pvalue->u.tls = ALLOC_ASN1ELEM (pctxt, H225SecurityCapabilities);

            stat = asn1PD_H225SecurityCapabilities (pctxt, pvalue->u.tls);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "tls", -1);

            break;

         /* ipsec */
         case 3:
            invokeStartElement (pctxt, "ipsec", -1);

            pvalue->u.ipsec = ALLOC_ASN1ELEM (pctxt, H225SecurityCapabilities);

            stat = asn1PD_H225SecurityCapabilities (pctxt, pvalue->u.ipsec);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "ipsec", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225H245Security                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225H245Security (OOCTXT* pctxt, H225_SeqOfH225H245Security* pvalue)
{
   int stat = ASN_OK;
   H225H245Security* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225H245Security);

         stat = asn1PD_H225H245Security (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225ClearToken                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225ClearToken (OOCTXT* pctxt, H225_SeqOfH225ClearToken* pvalue)
{
   int stat = ASN_OK;
   H235ClearToken* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H235ClearToken);

         stat = asn1PD_H235ClearToken (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CryptoH323Token_cryptoEPPwdHash                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CryptoH323Token_cryptoEPPwdHash (OOCTXT* pctxt, H225CryptoH323Token_cryptoEPPwdHash* pvalue)
{
   int stat = ASN_OK;

   /* decode alias */

   invokeStartElement (pctxt, "alias", -1);

   stat = asn1PD_H225AliasAddress (pctxt, &pvalue->alias);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "alias", -1);

   /* decode timeStamp */

   invokeStartElement (pctxt, "timeStamp", -1);

   stat = asn1PD_H235TimeStamp (pctxt, &pvalue->timeStamp);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "timeStamp", -1);

   /* decode token */

   invokeStartElement (pctxt, "token", -1);

   stat = asn1PD_H235HASHED (pctxt, &pvalue->token);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "token", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CryptoH323Token_cryptoGKPwdHash                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CryptoH323Token_cryptoGKPwdHash (OOCTXT* pctxt, H225CryptoH323Token_cryptoGKPwdHash* pvalue)
{
   int stat = ASN_OK;

   /* decode gatekeeperId */

   invokeStartElement (pctxt, "gatekeeperId", -1);

   stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperId);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "gatekeeperId", -1);

   /* decode timeStamp */

   invokeStartElement (pctxt, "timeStamp", -1);

   stat = asn1PD_H235TimeStamp (pctxt, &pvalue->timeStamp);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "timeStamp", -1);

   /* decode token */

   invokeStartElement (pctxt, "token", -1);

   stat = asn1PD_H235HASHED (pctxt, &pvalue->token);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "token", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CryptoH323Token_cryptoEPCert                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CryptoH323Token_cryptoEPCert (OOCTXT* pctxt, H225CryptoH323Token_cryptoEPCert* pvalue)
{
   int stat = ASN_OK;

   /* decode toBeSigned */

   invokeStartElement (pctxt, "toBeSigned", -1);

   stat = asn1PD_H235EncodedPwdCertToken (pctxt, &pvalue->toBeSigned);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "toBeSigned", -1);

   /* decode algorithmOID */

   invokeStartElement (pctxt, "algorithmOID", -1);

   stat = decodeObjectIdentifier (pctxt, &pvalue->algorithmOID);
   if (stat != ASN_OK) return stat;
   invokeOidValue (pctxt, pvalue->algorithmOID.numids, pvalue->algorithmOID.subid);

   invokeEndElement (pctxt, "algorithmOID", -1);

   /* decode paramS */

   invokeStartElement (pctxt, "paramS", -1);

   stat = asn1PD_H235Params (pctxt, &pvalue->paramS);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "paramS", -1);

   /* decode signature */

   invokeStartElement (pctxt, "signature", -1);

   stat = decodeDynBitString (pctxt, (ASN1DynBitStr*)&pvalue->signature);
   if (stat != ASN_OK) return stat;

   invokeBitStrValue (pctxt, pvalue->signature.numbits, pvalue->signature.data);

   invokeEndElement (pctxt, "signature", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CryptoH323Token_cryptoGKCert                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CryptoH323Token_cryptoGKCert (OOCTXT* pctxt, H225CryptoH323Token_cryptoGKCert* pvalue)
{
   int stat = ASN_OK;

   /* decode toBeSigned */

   invokeStartElement (pctxt, "toBeSigned", -1);

   stat = asn1PD_H235EncodedPwdCertToken (pctxt, &pvalue->toBeSigned);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "toBeSigned", -1);

   /* decode algorithmOID */

   invokeStartElement (pctxt, "algorithmOID", -1);

   stat = decodeObjectIdentifier (pctxt, &pvalue->algorithmOID);
   if (stat != ASN_OK) return stat;
   invokeOidValue (pctxt, pvalue->algorithmOID.numids, pvalue->algorithmOID.subid);

   invokeEndElement (pctxt, "algorithmOID", -1);

   /* decode paramS */

   invokeStartElement (pctxt, "paramS", -1);

   stat = asn1PD_H235Params (pctxt, &pvalue->paramS);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "paramS", -1);

   /* decode signature */

   invokeStartElement (pctxt, "signature", -1);

   stat = decodeDynBitString (pctxt, (ASN1DynBitStr*)&pvalue->signature);
   if (stat != ASN_OK) return stat;

   invokeBitStrValue (pctxt, pvalue->signature.numbits, pvalue->signature.data);

   invokeEndElement (pctxt, "signature", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CryptoH323Token_cryptoFastStart                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CryptoH323Token_cryptoFastStart (OOCTXT* pctxt, H225CryptoH323Token_cryptoFastStart* pvalue)
{
   int stat = ASN_OK;

   /* decode toBeSigned */

   invokeStartElement (pctxt, "toBeSigned", -1);

   stat = asn1PD_H225EncodedFastStartToken (pctxt, &pvalue->toBeSigned);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "toBeSigned", -1);

   /* decode algorithmOID */

   invokeStartElement (pctxt, "algorithmOID", -1);

   stat = decodeObjectIdentifier (pctxt, &pvalue->algorithmOID);
   if (stat != ASN_OK) return stat;
   invokeOidValue (pctxt, pvalue->algorithmOID.numids, pvalue->algorithmOID.subid);

   invokeEndElement (pctxt, "algorithmOID", -1);

   /* decode paramS */

   invokeStartElement (pctxt, "paramS", -1);

   stat = asn1PD_H235Params (pctxt, &pvalue->paramS);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "paramS", -1);

   /* decode signature */

   invokeStartElement (pctxt, "signature", -1);

   stat = decodeDynBitString (pctxt, (ASN1DynBitStr*)&pvalue->signature);
   if (stat != ASN_OK) return stat;

   invokeBitStrValue (pctxt, pvalue->signature.numbits, pvalue->signature.data);

   invokeEndElement (pctxt, "signature", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CryptoH323Token                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CryptoH323Token (OOCTXT* pctxt, H225CryptoH323Token* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 7);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* cryptoEPPwdHash */
         case 0:
            invokeStartElement (pctxt, "cryptoEPPwdHash", -1);

            pvalue->u.cryptoEPPwdHash = ALLOC_ASN1ELEM (pctxt, H225CryptoH323Token_cryptoEPPwdHash);

            stat = asn1PD_H225CryptoH323Token_cryptoEPPwdHash (pctxt, pvalue->u.cryptoEPPwdHash);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "cryptoEPPwdHash", -1);

            break;

         /* cryptoGKPwdHash */
         case 1:
            invokeStartElement (pctxt, "cryptoGKPwdHash", -1);

            pvalue->u.cryptoGKPwdHash = ALLOC_ASN1ELEM (pctxt, H225CryptoH323Token_cryptoGKPwdHash);

            stat = asn1PD_H225CryptoH323Token_cryptoGKPwdHash (pctxt, pvalue->u.cryptoGKPwdHash);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "cryptoGKPwdHash", -1);

            break;

         /* cryptoEPPwdEncr */
         case 2:
            invokeStartElement (pctxt, "cryptoEPPwdEncr", -1);

            pvalue->u.cryptoEPPwdEncr = ALLOC_ASN1ELEM (pctxt, H235ENCRYPTED);

            stat = asn1PD_H235ENCRYPTED (pctxt, pvalue->u.cryptoEPPwdEncr);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "cryptoEPPwdEncr", -1);

            break;

         /* cryptoGKPwdEncr */
         case 3:
            invokeStartElement (pctxt, "cryptoGKPwdEncr", -1);

            pvalue->u.cryptoGKPwdEncr = ALLOC_ASN1ELEM (pctxt, H235ENCRYPTED);

            stat = asn1PD_H235ENCRYPTED (pctxt, pvalue->u.cryptoGKPwdEncr);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "cryptoGKPwdEncr", -1);

            break;

         /* cryptoEPCert */
         case 4:
            invokeStartElement (pctxt, "cryptoEPCert", -1);

            pvalue->u.cryptoEPCert = ALLOC_ASN1ELEM (pctxt, H225CryptoH323Token_cryptoEPCert);

            stat = asn1PD_H225CryptoH323Token_cryptoEPCert (pctxt, pvalue->u.cryptoEPCert);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "cryptoEPCert", -1);

            break;

         /* cryptoGKCert */
         case 5:
            invokeStartElement (pctxt, "cryptoGKCert", -1);

            pvalue->u.cryptoGKCert = ALLOC_ASN1ELEM (pctxt, H225CryptoH323Token_cryptoGKCert);

            stat = asn1PD_H225CryptoH323Token_cryptoGKCert (pctxt, pvalue->u.cryptoGKCert);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "cryptoGKCert", -1);

            break;

         /* cryptoFastStart */
         case 6:
            invokeStartElement (pctxt, "cryptoFastStart", -1);

            pvalue->u.cryptoFastStart = ALLOC_ASN1ELEM (pctxt, H225CryptoH323Token_cryptoFastStart);

            stat = asn1PD_H225CryptoH323Token_cryptoFastStart (pctxt, pvalue->u.cryptoFastStart);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "cryptoFastStart", -1);

            break;

         /* nestedcryptoToken */
         case 7:
            invokeStartElement (pctxt, "nestedcryptoToken", -1);

            pvalue->u.nestedcryptoToken = ALLOC_ASN1ELEM (pctxt, H235CryptoToken);

            stat = asn1PD_H235CryptoToken (pctxt, pvalue->u.nestedcryptoToken);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nestedcryptoToken", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 9;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225CryptoH323Token                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225CryptoH323Token (OOCTXT* pctxt, H225_SeqOfH225CryptoH323Token* pvalue)
{
   int stat = ASN_OK;
   H225CryptoH323Token* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225CryptoH323Token);

         stat = asn1PD_H225CryptoH323Token (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Setup_UUIE_fastStart                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Setup_UUIE_fastStart (OOCTXT* pctxt, H225Setup_UUIE_fastStart* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ScnConnectionType                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ScnConnectionType (OOCTXT* pctxt, H225ScnConnectionType* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 6);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* unknown */
         case 0:
            invokeStartElement (pctxt, "unknown", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "unknown", -1);

            break;

         /* bChannel */
         case 1:
            invokeStartElement (pctxt, "bChannel", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "bChannel", -1);

            break;

         /* hybrid2x64 */
         case 2:
            invokeStartElement (pctxt, "hybrid2x64", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "hybrid2x64", -1);

            break;

         /* hybrid384 */
         case 3:
            invokeStartElement (pctxt, "hybrid384", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "hybrid384", -1);

            break;

         /* hybrid1536 */
         case 4:
            invokeStartElement (pctxt, "hybrid1536", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "hybrid1536", -1);

            break;

         /* hybrid1920 */
         case 5:
            invokeStartElement (pctxt, "hybrid1920", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "hybrid1920", -1);

            break;

         /* multirate */
         case 6:
            invokeStartElement (pctxt, "multirate", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "multirate", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 8;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ScnConnectionAggregation                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ScnConnectionAggregation (OOCTXT* pctxt, H225ScnConnectionAggregation* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 5);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* auto_ */
         case 0:
            invokeStartElement (pctxt, "auto_", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "auto_", -1);

            break;

         /* none */
         case 1:
            invokeStartElement (pctxt, "none", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "none", -1);

            break;

         /* h221 */
         case 2:
            invokeStartElement (pctxt, "h221", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "h221", -1);

            break;

         /* bonded_mode1 */
         case 3:
            invokeStartElement (pctxt, "bonded_mode1", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "bonded_mode1", -1);

            break;

         /* bonded_mode2 */
         case 4:
            invokeStartElement (pctxt, "bonded_mode2", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "bonded_mode2", -1);

            break;

         /* bonded_mode3 */
         case 5:
            invokeStartElement (pctxt, "bonded_mode3", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "bonded_mode3", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 7;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Setup_UUIE_connectionParameters                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Setup_UUIE_connectionParameters (OOCTXT* pctxt, H225Setup_UUIE_connectionParameters* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode connectionType */

   invokeStartElement (pctxt, "connectionType", -1);

   stat = asn1PD_H225ScnConnectionType (pctxt, &pvalue->connectionType);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "connectionType", -1);

   /* decode numberOfScnConnections */

   invokeStartElement (pctxt, "numberOfScnConnections", -1);

   stat = decodeConsUInt16 (pctxt, &pvalue->numberOfScnConnections, 0U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->numberOfScnConnections);

   invokeEndElement (pctxt, "numberOfScnConnections", -1);

   /* decode connectionAggregation */

   invokeStartElement (pctxt, "connectionAggregation", -1);

   stat = asn1PD_H225ScnConnectionAggregation (pctxt, &pvalue->connectionAggregation);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "connectionAggregation", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Setup_UUIE_language                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Setup_UUIE_language (OOCTXT* pctxt, H225Setup_UUIE_language* pvalue)
{
   static Asn1SizeCnst element_lsize1 = { 0, 1, 32, 0 };
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1IA5String);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      addSizeConstraint (pctxt, &element_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->elem[xx1], 0, 8, 7, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->elem[xx1]);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  PresentationIndicator                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225PresentationIndicator (OOCTXT* pctxt, H225PresentationIndicator* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* presentationAllowed */
         case 0:
            invokeStartElement (pctxt, "presentationAllowed", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "presentationAllowed", -1);

            break;

         /* presentationRestricted */
         case 1:
            invokeStartElement (pctxt, "presentationRestricted", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "presentationRestricted", -1);

            break;

         /* addressNotAvailable */
         case 2:
            invokeStartElement (pctxt, "addressNotAvailable", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "addressNotAvailable", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallCreditServiceControl_billingMode                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallCreditServiceControl_billingMode (OOCTXT* pctxt, H225CallCreditServiceControl_billingMode* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* credit */
         case 0:
            invokeStartElement (pctxt, "credit", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "credit", -1);

            break;

         /* debit */
         case 1:
            invokeStartElement (pctxt, "debit", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "debit", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallCreditServiceControl_callStartingPoint                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallCreditServiceControl_callStartingPoint (OOCTXT* pctxt, H225CallCreditServiceControl_callStartingPoint* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* alerting */
         case 0:
            invokeStartElement (pctxt, "alerting", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "alerting", -1);

            break;

         /* connect */
         case 1:
            invokeStartElement (pctxt, "connect", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "connect", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallCreditServiceControl                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallCreditServiceControl (OOCTXT* pctxt, H225CallCreditServiceControl* pvalue)
{
   static Asn1SizeCnst amountString_lsize1 = { 0, 1, 512, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.amountStringPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.billingModePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callDurationLimitPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.enforceCallDurationLimitPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callStartingPointPresent = optbit;

   /* decode amountString */

   if (pvalue->m.amountStringPresent) {
      invokeStartElement (pctxt, "amountString", -1);

      addSizeConstraint (pctxt, &amountString_lsize1);

      stat = decodeBMPString (pctxt, &pvalue->amountString, 0);
      if (stat != ASN_OK) return stat;
      invokeCharStr16BitValue (pctxt, pvalue->amountString.nchars, pvalue->amountString.data);

      invokeEndElement (pctxt, "amountString", -1);
   }

   /* decode billingMode */

   if (pvalue->m.billingModePresent) {
      invokeStartElement (pctxt, "billingMode", -1);

      stat = asn1PD_H225CallCreditServiceControl_billingMode (pctxt, &pvalue->billingMode);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "billingMode", -1);
   }

   /* decode callDurationLimit */

   if (pvalue->m.callDurationLimitPresent) {
      invokeStartElement (pctxt, "callDurationLimit", -1);

      stat = decodeConsUnsigned (pctxt, &pvalue->callDurationLimit, 1U, ASN1UINT_MAX);
      if (stat != ASN_OK) return stat;
      invokeUIntValue (pctxt, pvalue->callDurationLimit);

      invokeEndElement (pctxt, "callDurationLimit", -1);
   }

   /* decode enforceCallDurationLimit */

   if (pvalue->m.enforceCallDurationLimitPresent) {
      invokeStartElement (pctxt, "enforceCallDurationLimit", -1);

      stat = DECODEBIT (pctxt, &pvalue->enforceCallDurationLimit);
      if (stat != ASN_OK) return stat;
      invokeBoolValue (pctxt, pvalue->enforceCallDurationLimit);

      invokeEndElement (pctxt, "enforceCallDurationLimit", -1);
   }

   /* decode callStartingPoint */

   if (pvalue->m.callStartingPointPresent) {
      invokeStartElement (pctxt, "callStartingPoint", -1);

      stat = asn1PD_H225CallCreditServiceControl_callStartingPoint (pctxt, &pvalue->callStartingPoint);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callStartingPoint", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ServiceControlDescriptor                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ServiceControlDescriptor (OOCTXT* pctxt, H225ServiceControlDescriptor* pvalue)
{
   static Asn1SizeCnst url_lsize1 = { 0, 0, 512, 0 };
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* url */
         case 0:
            invokeStartElement (pctxt, "url", -1);

            addSizeConstraint (pctxt, &url_lsize1);

            stat = decodeConstrainedStringEx (pctxt, &pvalue->u.url, 0, 8, 7, 7);
            if (stat != ASN_OK) return stat;
            invokeCharStrValue (pctxt, pvalue->u.url);

            invokeEndElement (pctxt, "url", -1);

            break;

         /* signal */
         case 1:
            invokeStartElement (pctxt, "signal", -1);

            pvalue->u.signal = ALLOC_ASN1ELEM (pctxt, H225H248SignalsDescriptor);

            stat = asn1PD_H225H248SignalsDescriptor (pctxt, pvalue->u.signal);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "signal", -1);

            break;

         /* nonStandard */
         case 2:
            invokeStartElement (pctxt, "nonStandard", -1);

            pvalue->u.nonStandard = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandard", -1);

            break;

         /* callCreditServiceControl */
         case 3:
            invokeStartElement (pctxt, "callCreditServiceControl", -1);

            pvalue->u.callCreditServiceControl = ALLOC_ASN1ELEM (pctxt, H225CallCreditServiceControl);

            stat = asn1PD_H225CallCreditServiceControl (pctxt, pvalue->u.callCreditServiceControl);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "callCreditServiceControl", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ServiceControlSession_reason                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ServiceControlSession_reason (OOCTXT* pctxt, H225ServiceControlSession_reason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* open */
         case 0:
            invokeStartElement (pctxt, "open", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "open", -1);

            break;

         /* refresh */
         case 1:
            invokeStartElement (pctxt, "refresh", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "refresh", -1);

            break;

         /* close */
         case 2:
            invokeStartElement (pctxt, "close", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "close", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ServiceControlSession                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ServiceControlSession (OOCTXT* pctxt, H225ServiceControlSession* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.contentsPresent = optbit;

   /* decode sessionId */

   invokeStartElement (pctxt, "sessionId", -1);

   stat = decodeConsUInt8 (pctxt, &pvalue->sessionId, 0U, 255U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->sessionId);

   invokeEndElement (pctxt, "sessionId", -1);

   /* decode contents */

   if (pvalue->m.contentsPresent) {
      invokeStartElement (pctxt, "contents", -1);

      stat = asn1PD_H225ServiceControlDescriptor (pctxt, &pvalue->contents);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "contents", -1);
   }

   /* decode reason */

   invokeStartElement (pctxt, "reason", -1);

   stat = asn1PD_H225ServiceControlSession_reason (pctxt, &pvalue->reason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "reason", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225ServiceControlSession                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225ServiceControlSession (OOCTXT* pctxt, H225_SeqOfH225ServiceControlSession* pvalue)
{
   int stat = ASN_OK;
   H225ServiceControlSession* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225ServiceControlSession);

         stat = asn1PD_H225ServiceControlSession (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CarrierInfo                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CarrierInfo (OOCTXT* pctxt, H225CarrierInfo* pvalue)
{
   static Asn1SizeCnst carrierName_lsize1 = { 0, 1, 128, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.carrierIdentificationCodePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.carrierNamePresent = optbit;

   /* decode carrierIdentificationCode */

   if (pvalue->m.carrierIdentificationCodePresent) {
      invokeStartElement (pctxt, "carrierIdentificationCode", -1);

      stat = asn1PD_H225CarrierInfo_carrierIdentificationCode (pctxt, &pvalue->carrierIdentificationCode);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "carrierIdentificationCode", -1);
   }

   /* decode carrierName */

   if (pvalue->m.carrierNamePresent) {
      invokeStartElement (pctxt, "carrierName", -1);

      addSizeConstraint (pctxt, &carrierName_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->carrierName, 0, 8, 7, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->carrierName);

      invokeEndElement (pctxt, "carrierName", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallsAvailable                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallsAvailable (OOCTXT* pctxt, H225CallsAvailable* pvalue)
{
   static Asn1SizeCnst group_lsize1 = { 0, 1, 128, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.groupPresent = optbit;

   /* decode calls */

   invokeStartElement (pctxt, "calls", -1);

   stat = decodeConsUnsigned (pctxt, &pvalue->calls, 0U, ASN1UINT_MAX);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->calls);

   invokeEndElement (pctxt, "calls", -1);

   /* decode group */

   if (pvalue->m.groupPresent) {
      invokeStartElement (pctxt, "group", -1);

      addSizeConstraint (pctxt, &group_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->group, 0, 8, 7, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->group);

      invokeEndElement (pctxt, "group", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 1 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.carrierPresent = 1;

                     invokeStartElement (pctxt, "carrier", -1);

                     stat = asn1PD_H225CarrierInfo (pctxt, &pvalue->carrier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "carrier", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225CallsAvailable                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225CallsAvailable (OOCTXT* pctxt, H225_SeqOfH225CallsAvailable* pvalue)
{
   int stat = ASN_OK;
   H225CallsAvailable* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225CallsAvailable);

         stat = asn1PD_H225CallsAvailable (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallCapacityInfo                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallCapacityInfo (OOCTXT* pctxt, H225CallCapacityInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.voiceGwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h310GwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h320GwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h321GwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h322GwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h323GwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h324GwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.t120OnlyGwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.t38FaxAnnexbOnlyGwCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.terminalCallsAvailablePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.mcuCallsAvailablePresent = optbit;

   /* decode voiceGwCallsAvailable */

   if (pvalue->m.voiceGwCallsAvailablePresent) {
      invokeStartElement (pctxt, "voiceGwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->voiceGwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "voiceGwCallsAvailable", -1);
   }

   /* decode h310GwCallsAvailable */

   if (pvalue->m.h310GwCallsAvailablePresent) {
      invokeStartElement (pctxt, "h310GwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->h310GwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h310GwCallsAvailable", -1);
   }

   /* decode h320GwCallsAvailable */

   if (pvalue->m.h320GwCallsAvailablePresent) {
      invokeStartElement (pctxt, "h320GwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->h320GwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h320GwCallsAvailable", -1);
   }

   /* decode h321GwCallsAvailable */

   if (pvalue->m.h321GwCallsAvailablePresent) {
      invokeStartElement (pctxt, "h321GwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->h321GwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h321GwCallsAvailable", -1);
   }

   /* decode h322GwCallsAvailable */

   if (pvalue->m.h322GwCallsAvailablePresent) {
      invokeStartElement (pctxt, "h322GwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->h322GwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h322GwCallsAvailable", -1);
   }

   /* decode h323GwCallsAvailable */

   if (pvalue->m.h323GwCallsAvailablePresent) {
      invokeStartElement (pctxt, "h323GwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->h323GwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h323GwCallsAvailable", -1);
   }

   /* decode h324GwCallsAvailable */

   if (pvalue->m.h324GwCallsAvailablePresent) {
      invokeStartElement (pctxt, "h324GwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->h324GwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h324GwCallsAvailable", -1);
   }

   /* decode t120OnlyGwCallsAvailable */

   if (pvalue->m.t120OnlyGwCallsAvailablePresent) {
      invokeStartElement (pctxt, "t120OnlyGwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->t120OnlyGwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "t120OnlyGwCallsAvailable", -1);
   }

   /* decode t38FaxAnnexbOnlyGwCallsAvailable */

   if (pvalue->m.t38FaxAnnexbOnlyGwCallsAvailablePresent) {
      invokeStartElement (pctxt, "t38FaxAnnexbOnlyGwCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->t38FaxAnnexbOnlyGwCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "t38FaxAnnexbOnlyGwCallsAvailable", -1);
   }

   /* decode terminalCallsAvailable */

   if (pvalue->m.terminalCallsAvailablePresent) {
      invokeStartElement (pctxt, "terminalCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->terminalCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "terminalCallsAvailable", -1);
   }

   /* decode mcuCallsAvailable */

   if (pvalue->m.mcuCallsAvailablePresent) {
      invokeStartElement (pctxt, "mcuCallsAvailable", -1);

      stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->mcuCallsAvailable);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "mcuCallsAvailable", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 1 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.sipGwCallsAvailablePresent = 1;

                     invokeStartElement (pctxt, "sipGwCallsAvailable", -1);

                     stat = asn1PD_H225_SeqOfH225CallsAvailable (pctxt, &pvalue->sipGwCallsAvailable);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "sipGwCallsAvailable", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallCapacity                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallCapacity (OOCTXT* pctxt, H225CallCapacity* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.maximumCallCapacityPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.currentCallCapacityPresent = optbit;

   /* decode maximumCallCapacity */

   if (pvalue->m.maximumCallCapacityPresent) {
      invokeStartElement (pctxt, "maximumCallCapacity", -1);

      stat = asn1PD_H225CallCapacityInfo (pctxt, &pvalue->maximumCallCapacity);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "maximumCallCapacity", -1);
   }

   /* decode currentCallCapacity */

   if (pvalue->m.currentCallCapacityPresent) {
      invokeStartElement (pctxt, "currentCallCapacity", -1);

      stat = asn1PD_H225CallCapacityInfo (pctxt, &pvalue->currentCallCapacity);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "currentCallCapacity", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225CicInfo_cic_element                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225CicInfo_cic_element (OOCTXT* pctxt, H225_SeqOfH225CicInfo_cic_element* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, H225CicInfo_cic_element);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = asn1PD_H225CicInfo_cic_element (pctxt, &pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CicInfo                                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CicInfo (OOCTXT* pctxt, H225CicInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode cic */

   invokeStartElement (pctxt, "cic", -1);

   stat = asn1PD_H225_SeqOfH225CicInfo_cic_element (pctxt, &pvalue->cic);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "cic", -1);

   /* decode pointCode */

   invokeStartElement (pctxt, "pointCode", -1);

   stat = asn1PD_H225CicInfo_pointCode (pctxt, &pvalue->pointCode);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "pointCode", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GroupID_member                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GroupID_member (OOCTXT* pctxt, H225GroupID_member* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1USINT);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeConsUInt16 (pctxt, &pvalue->elem[xx1], 0U, 65535U);
      if (stat != ASN_OK) return stat;
      invokeUIntValue (pctxt, pvalue->elem[xx1]);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GroupID                                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GroupID (OOCTXT* pctxt, H225GroupID* pvalue)
{
   static Asn1SizeCnst group_lsize1 = { 0, 1, 128, 0 };
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.memberPresent = optbit;

   /* decode member */

   if (pvalue->m.memberPresent) {
      invokeStartElement (pctxt, "member", -1);

      stat = asn1PD_H225GroupID_member (pctxt, &pvalue->member);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "member", -1);
   }

   /* decode group */

   invokeStartElement (pctxt, "group", -1);

   addSizeConstraint (pctxt, &group_lsize1);

   stat = decodeConstrainedStringEx (pctxt, &pvalue->group, 0, 8, 7, 7);
   if (stat != ASN_OK) return stat;
   invokeCharStrValue (pctxt, pvalue->group);

   invokeEndElement (pctxt, "group", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CircuitIdentifier                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CircuitIdentifier (OOCTXT* pctxt, H225CircuitIdentifier* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cicPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.groupPresent = optbit;

   /* decode cic */

   if (pvalue->m.cicPresent) {
      invokeStartElement (pctxt, "cic", -1);

      stat = asn1PD_H225CicInfo (pctxt, &pvalue->cic);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cic", -1);
   }

   /* decode group */

   if (pvalue->m.groupPresent) {
      invokeStartElement (pctxt, "group", -1);

      stat = asn1PD_H225GroupID (pctxt, &pvalue->group);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "group", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 1 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.carrierPresent = 1;

                     invokeStartElement (pctxt, "carrier", -1);

                     stat = asn1PD_H225CarrierInfo (pctxt, &pvalue->carrier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "carrier", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225GenericData                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225GenericData (OOCTXT* pctxt, H225_SeqOfH225GenericData* pvalue)
{
   int stat = ASN_OK;
   H225GenericData* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225GenericData);

         stat = asn1PD_H225GenericData (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CircuitInfo                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CircuitInfo (OOCTXT* pctxt, H225CircuitInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.sourceCircuitIDPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destinationCircuitIDPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.genericDataPresent = optbit;

   /* decode sourceCircuitID */

   if (pvalue->m.sourceCircuitIDPresent) {
      invokeStartElement (pctxt, "sourceCircuitID", -1);

      stat = asn1PD_H225CircuitIdentifier (pctxt, &pvalue->sourceCircuitID);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "sourceCircuitID", -1);
   }

   /* decode destinationCircuitID */

   if (pvalue->m.destinationCircuitIDPresent) {
      invokeStartElement (pctxt, "destinationCircuitID", -1);

      stat = asn1PD_H225CircuitIdentifier (pctxt, &pvalue->destinationCircuitID);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destinationCircuitID", -1);
   }

   /* decode genericData */

   if (pvalue->m.genericDataPresent) {
      invokeStartElement (pctxt, "genericData", -1);

      stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "genericData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225FeatureDescriptor                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225FeatureDescriptor (OOCTXT* pctxt, H225_SeqOfH225FeatureDescriptor* pvalue)
{
   int stat = ASN_OK;
   H225FeatureDescriptor* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225FeatureDescriptor);

         stat = asn1PD_H225FeatureDescriptor (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Setup_UUIE_parallelH245Control                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Setup_UUIE_parallelH245Control (OOCTXT* pctxt, H225Setup_UUIE_parallelH245Control* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ExtendedAliasAddress                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ExtendedAliasAddress (OOCTXT* pctxt, H225ExtendedAliasAddress* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.presentationIndicatorPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.screeningIndicatorPresent = optbit;

   /* decode address */

   invokeStartElement (pctxt, "address", -1);

   stat = asn1PD_H225AliasAddress (pctxt, &pvalue->address);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "address", -1);

   /* decode presentationIndicator */

   if (pvalue->m.presentationIndicatorPresent) {
      invokeStartElement (pctxt, "presentationIndicator", -1);

      stat = asn1PD_H225PresentationIndicator (pctxt, &pvalue->presentationIndicator);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "presentationIndicator", -1);
   }

   /* decode screeningIndicator */

   if (pvalue->m.screeningIndicatorPresent) {
      invokeStartElement (pctxt, "screeningIndicator", -1);

      stat = asn1PD_H225ScreeningIndicator (pctxt, &pvalue->screeningIndicator);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "screeningIndicator", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225ExtendedAliasAddress                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225ExtendedAliasAddress (OOCTXT* pctxt, H225_SeqOfH225ExtendedAliasAddress* pvalue)
{
   int stat = ASN_OK;
   H225ExtendedAliasAddress* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225ExtendedAliasAddress);

         stat = asn1PD_H225ExtendedAliasAddress (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Setup_UUIE                                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Setup_UUIE (OOCTXT* pctxt, H225Setup_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h245AddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.sourceAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destinationAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destCallSignalAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destExtraCallInfoPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destExtraCRVPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callServicesPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode h245Address */

   if (pvalue->m.h245AddressPresent) {
      invokeStartElement (pctxt, "h245Address", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->h245Address);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h245Address", -1);
   }

   /* decode sourceAddress */

   if (pvalue->m.sourceAddressPresent) {
      invokeStartElement (pctxt, "sourceAddress", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->sourceAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "sourceAddress", -1);
   }

   /* decode sourceInfo */

   invokeStartElement (pctxt, "sourceInfo", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->sourceInfo);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "sourceInfo", -1);

   /* decode destinationAddress */

   if (pvalue->m.destinationAddressPresent) {
      invokeStartElement (pctxt, "destinationAddress", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destinationAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destinationAddress", -1);
   }

   /* decode destCallSignalAddress */

   if (pvalue->m.destCallSignalAddressPresent) {
      invokeStartElement (pctxt, "destCallSignalAddress", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->destCallSignalAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destCallSignalAddress", -1);
   }

   /* decode destExtraCallInfo */

   if (pvalue->m.destExtraCallInfoPresent) {
      invokeStartElement (pctxt, "destExtraCallInfo", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destExtraCallInfo);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destExtraCallInfo", -1);
   }

   /* decode destExtraCRV */

   if (pvalue->m.destExtraCRVPresent) {
      invokeStartElement (pctxt, "destExtraCRV", -1);

      stat = asn1PD_H225_SeqOfH225CallReferenceValue (pctxt, &pvalue->destExtraCRV);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destExtraCRV", -1);
   }

   /* decode activeMC */

   invokeStartElement (pctxt, "activeMC", -1);

   stat = DECODEBIT (pctxt, &pvalue->activeMC);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->activeMC);

   invokeEndElement (pctxt, "activeMC", -1);

   /* decode conferenceID */

   invokeStartElement (pctxt, "conferenceID", -1);

   stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceID", -1);

   /* decode conferenceGoal */

   invokeStartElement (pctxt, "conferenceGoal", -1);

   stat = asn1PD_H225Setup_UUIE_conferenceGoal (pctxt, &pvalue->conferenceGoal);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceGoal", -1);

   /* decode callServices */

   if (pvalue->m.callServicesPresent) {
      invokeStartElement (pctxt, "callServices", -1);

      stat = asn1PD_H225QseriesOptions (pctxt, &pvalue->callServices);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callServices", -1);
   }

   /* decode callType */

   invokeStartElement (pctxt, "callType", -1);

   stat = asn1PD_H225CallType (pctxt, &pvalue->callType);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callType", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 27 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.sourceCallSignalAddressPresent = 1;

                     invokeStartElement (pctxt, "sourceCallSignalAddress", -1);

                     stat = asn1PD_H225TransportAddress (pctxt, &pvalue->sourceCallSignalAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "sourceCallSignalAddress", -1);
                     break;

                  case 1:
                     pvalue->m.remoteExtensionAddressPresent = 1;

                     invokeStartElement (pctxt, "remoteExtensionAddress", -1);

                     stat = asn1PD_H225AliasAddress (pctxt, &pvalue->remoteExtensionAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "remoteExtensionAddress", -1);
                     break;

                  case 2:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 3:
                     pvalue->m.h245SecurityCapabilityPresent = 1;

                     invokeStartElement (pctxt, "h245SecurityCapability", -1);

                     stat = asn1PD_H225_SeqOfH225H245Security (pctxt, &pvalue->h245SecurityCapability);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h245SecurityCapability", -1);
                     break;

                  case 4:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 5:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 6:
                     pvalue->m.fastStartPresent = 1;

                     invokeStartElement (pctxt, "fastStart", -1);

                     stat = asn1PD_H225Setup_UUIE_fastStart (pctxt, &pvalue->fastStart);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "fastStart", -1);
                     break;

                  case 7:
                     pvalue->m.mediaWaitForConnectPresent = 1;

                     invokeStartElement (pctxt, "mediaWaitForConnect", -1);

                     stat = DECODEBIT (pctxt, &pvalue->mediaWaitForConnect);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->mediaWaitForConnect);

                     invokeEndElement (pctxt, "mediaWaitForConnect", -1);
                     break;

                  case 8:
                     pvalue->m.canOverlapSendPresent = 1;

                     invokeStartElement (pctxt, "canOverlapSend", -1);

                     stat = DECODEBIT (pctxt, &pvalue->canOverlapSend);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->canOverlapSend);

                     invokeEndElement (pctxt, "canOverlapSend", -1);
                     break;

                  case 9:
                     pvalue->m.endpointIdentifierPresent = 1;

                     invokeStartElement (pctxt, "endpointIdentifier", -1);

                     stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "endpointIdentifier", -1);
                     break;

                  case 10:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 11:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 12:
                     pvalue->m.connectionParametersPresent = 1;

                     invokeStartElement (pctxt, "connectionParameters", -1);

                     stat = asn1PD_H225Setup_UUIE_connectionParameters (pctxt, &pvalue->connectionParameters);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "connectionParameters", -1);
                     break;

                  case 13:
                     pvalue->m.languagePresent = 1;

                     invokeStartElement (pctxt, "language", -1);

                     stat = asn1PD_H225Setup_UUIE_language (pctxt, &pvalue->language);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "language", -1);
                     break;

                  case 14:
                     pvalue->m.presentationIndicatorPresent = 1;

                     invokeStartElement (pctxt, "presentationIndicator", -1);

                     stat = asn1PD_H225PresentationIndicator (pctxt, &pvalue->presentationIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "presentationIndicator", -1);
                     break;

                  case 15:
                     pvalue->m.screeningIndicatorPresent = 1;

                     invokeStartElement (pctxt, "screeningIndicator", -1);

                     stat = asn1PD_H225ScreeningIndicator (pctxt, &pvalue->screeningIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "screeningIndicator", -1);
                     break;

                  case 16:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 17:
                     pvalue->m.symmetricOperationRequiredPresent = 1;

                     invokeStartElement (pctxt, "symmetricOperationRequired", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "symmetricOperationRequired", -1);
                     break;

                  case 18:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 19:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 20:
                     pvalue->m.desiredProtocolsPresent = 1;

                     invokeStartElement (pctxt, "desiredProtocols", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->desiredProtocols);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "desiredProtocols", -1);
                     break;

                  case 21:
                     pvalue->m.neededFeaturesPresent = 1;

                     invokeStartElement (pctxt, "neededFeatures", -1);

                     stat = asn1PD_H225_SeqOfH225FeatureDescriptor (pctxt, &pvalue->neededFeatures);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "neededFeatures", -1);
                     break;

                  case 22:
                     pvalue->m.desiredFeaturesPresent = 1;

                     invokeStartElement (pctxt, "desiredFeatures", -1);

                     stat = asn1PD_H225_SeqOfH225FeatureDescriptor (pctxt, &pvalue->desiredFeatures);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "desiredFeatures", -1);
                     break;

                  case 23:
                     pvalue->m.supportedFeaturesPresent = 1;

                     invokeStartElement (pctxt, "supportedFeatures", -1);

                     stat = asn1PD_H225_SeqOfH225FeatureDescriptor (pctxt, &pvalue->supportedFeatures);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedFeatures", -1);
                     break;

                  case 24:
                     pvalue->m.parallelH245ControlPresent = 1;

                     invokeStartElement (pctxt, "parallelH245Control", -1);

                     stat = asn1PD_H225Setup_UUIE_parallelH245Control (pctxt, &pvalue->parallelH245Control);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "parallelH245Control", -1);
                     break;

                  case 25:
                     pvalue->m.additionalSourceAddressesPresent = 1;

                     invokeStartElement (pctxt, "additionalSourceAddresses", -1);

                     stat = asn1PD_H225_SeqOfH225ExtendedAliasAddress (pctxt, &pvalue->additionalSourceAddresses);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "additionalSourceAddresses", -1);
                     break;

                  case 26:
                     pvalue->m.hopCountPresent = 1;

                     invokeStartElement (pctxt, "hopCount", -1);

                     stat = decodeConsUInt8 (pctxt, &pvalue->hopCount, 1U, 31U);
                     if (stat != ASN_OK) return stat;
                     invokeUIntValue (pctxt, pvalue->hopCount);

                     invokeEndElement (pctxt, "hopCount", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallProceeding_UUIE_fastStart                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallProceeding_UUIE_fastStart (OOCTXT* pctxt, H225CallProceeding_UUIE_fastStart* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  FeatureSet                                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225FeatureSet (OOCTXT* pctxt, H225FeatureSet* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.neededFeaturesPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.desiredFeaturesPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.supportedFeaturesPresent = optbit;

   /* decode replacementFeatureSet */

   invokeStartElement (pctxt, "replacementFeatureSet", -1);

   stat = DECODEBIT (pctxt, &pvalue->replacementFeatureSet);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->replacementFeatureSet);

   invokeEndElement (pctxt, "replacementFeatureSet", -1);

   /* decode neededFeatures */

   if (pvalue->m.neededFeaturesPresent) {
      invokeStartElement (pctxt, "neededFeatures", -1);

      stat = asn1PD_H225_SeqOfH225FeatureDescriptor (pctxt, &pvalue->neededFeatures);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "neededFeatures", -1);
   }

   /* decode desiredFeatures */

   if (pvalue->m.desiredFeaturesPresent) {
      invokeStartElement (pctxt, "desiredFeatures", -1);

      stat = asn1PD_H225_SeqOfH225FeatureDescriptor (pctxt, &pvalue->desiredFeatures);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "desiredFeatures", -1);
   }

   /* decode supportedFeatures */

   if (pvalue->m.supportedFeaturesPresent) {
      invokeStartElement (pctxt, "supportedFeatures", -1);

      stat = asn1PD_H225_SeqOfH225FeatureDescriptor (pctxt, &pvalue->supportedFeatures);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "supportedFeatures", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallProceeding_UUIE                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallProceeding_UUIE (OOCTXT* pctxt, H225CallProceeding_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h245AddressPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode destinationInfo */

   invokeStartElement (pctxt, "destinationInfo", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->destinationInfo);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "destinationInfo", -1);

   /* decode h245Address */

   if (pvalue->m.h245AddressPresent) {
      invokeStartElement (pctxt, "h245Address", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->h245Address);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h245Address", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 9 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.h245SecurityModePresent = 1;

                     invokeStartElement (pctxt, "h245SecurityMode", -1);

                     stat = asn1PD_H225H245Security (pctxt, &pvalue->h245SecurityMode);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h245SecurityMode", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.fastStartPresent = 1;

                     invokeStartElement (pctxt, "fastStart", -1);

                     stat = asn1PD_H225CallProceeding_UUIE_fastStart (pctxt, &pvalue->fastStart);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "fastStart", -1);
                     break;

                  case 5:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 6:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 7:
                     pvalue->m.fastConnectRefusedPresent = 1;

                     invokeStartElement (pctxt, "fastConnectRefused", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "fastConnectRefused", -1);
                     break;

                  case 8:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Connect_UUIE_fastStart                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Connect_UUIE_fastStart (OOCTXT* pctxt, H225Connect_UUIE_fastStart* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Connect_UUIE_language                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Connect_UUIE_language (OOCTXT* pctxt, H225Connect_UUIE_language* pvalue)
{
   static Asn1SizeCnst element_lsize1 = { 0, 1, 32, 0 };
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1IA5String);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      addSizeConstraint (pctxt, &element_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->elem[xx1], 0, 8, 7, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->elem[xx1]);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Connect_UUIE                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Connect_UUIE (OOCTXT* pctxt, H225Connect_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h245AddressPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode h245Address */

   if (pvalue->m.h245AddressPresent) {
      invokeStartElement (pctxt, "h245Address", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->h245Address);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h245Address", -1);
   }

   /* decode destinationInfo */

   invokeStartElement (pctxt, "destinationInfo", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->destinationInfo);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "destinationInfo", -1);

   /* decode conferenceID */

   invokeStartElement (pctxt, "conferenceID", -1);

   stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceID", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 15 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.h245SecurityModePresent = 1;

                     invokeStartElement (pctxt, "h245SecurityMode", -1);

                     stat = asn1PD_H225H245Security (pctxt, &pvalue->h245SecurityMode);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h245SecurityMode", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.fastStartPresent = 1;

                     invokeStartElement (pctxt, "fastStart", -1);

                     stat = asn1PD_H225Connect_UUIE_fastStart (pctxt, &pvalue->fastStart);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "fastStart", -1);
                     break;

                  case 5:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 6:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 7:
                     pvalue->m.languagePresent = 1;

                     invokeStartElement (pctxt, "language", -1);

                     stat = asn1PD_H225Connect_UUIE_language (pctxt, &pvalue->language);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "language", -1);
                     break;

                  case 8:
                     pvalue->m.connectedAddressPresent = 1;

                     invokeStartElement (pctxt, "connectedAddress", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->connectedAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "connectedAddress", -1);
                     break;

                  case 9:
                     pvalue->m.presentationIndicatorPresent = 1;

                     invokeStartElement (pctxt, "presentationIndicator", -1);

                     stat = asn1PD_H225PresentationIndicator (pctxt, &pvalue->presentationIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "presentationIndicator", -1);
                     break;

                  case 10:
                     pvalue->m.screeningIndicatorPresent = 1;

                     invokeStartElement (pctxt, "screeningIndicator", -1);

                     stat = asn1PD_H225ScreeningIndicator (pctxt, &pvalue->screeningIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "screeningIndicator", -1);
                     break;

                  case 11:
                     pvalue->m.fastConnectRefusedPresent = 1;

                     invokeStartElement (pctxt, "fastConnectRefused", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "fastConnectRefused", -1);
                     break;

                  case 12:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 13:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 14:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Alerting_UUIE_fastStart                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Alerting_UUIE_fastStart (OOCTXT* pctxt, H225Alerting_UUIE_fastStart* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Alerting_UUIE                                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Alerting_UUIE (OOCTXT* pctxt, H225Alerting_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h245AddressPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode destinationInfo */

   invokeStartElement (pctxt, "destinationInfo", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->destinationInfo);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "destinationInfo", -1);

   /* decode h245Address */

   if (pvalue->m.h245AddressPresent) {
      invokeStartElement (pctxt, "h245Address", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->h245Address);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h245Address", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 14 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.h245SecurityModePresent = 1;

                     invokeStartElement (pctxt, "h245SecurityMode", -1);

                     stat = asn1PD_H225H245Security (pctxt, &pvalue->h245SecurityMode);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h245SecurityMode", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.fastStartPresent = 1;

                     invokeStartElement (pctxt, "fastStart", -1);

                     stat = asn1PD_H225Alerting_UUIE_fastStart (pctxt, &pvalue->fastStart);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "fastStart", -1);
                     break;

                  case 5:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 6:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 7:
                     pvalue->m.alertingAddressPresent = 1;

                     invokeStartElement (pctxt, "alertingAddress", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->alertingAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alertingAddress", -1);
                     break;

                  case 8:
                     pvalue->m.presentationIndicatorPresent = 1;

                     invokeStartElement (pctxt, "presentationIndicator", -1);

                     stat = asn1PD_H225PresentationIndicator (pctxt, &pvalue->presentationIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "presentationIndicator", -1);
                     break;

                  case 9:
                     pvalue->m.screeningIndicatorPresent = 1;

                     invokeStartElement (pctxt, "screeningIndicator", -1);

                     stat = asn1PD_H225ScreeningIndicator (pctxt, &pvalue->screeningIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "screeningIndicator", -1);
                     break;

                  case 10:
                     pvalue->m.fastConnectRefusedPresent = 1;

                     invokeStartElement (pctxt, "fastConnectRefused", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "fastConnectRefused", -1);
                     break;

                  case 11:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 12:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 13:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Information_UUIE_fastStart                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Information_UUIE_fastStart (OOCTXT* pctxt, H225Information_UUIE_fastStart* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Information_UUIE                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Information_UUIE (OOCTXT* pctxt, H225Information_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 6 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.fastStartPresent = 1;

                     invokeStartElement (pctxt, "fastStart", -1);

                     stat = asn1PD_H225Information_UUIE_fastStart (pctxt, &pvalue->fastStart);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "fastStart", -1);
                     break;

                  case 4:
                     pvalue->m.fastConnectRefusedPresent = 1;

                     invokeStartElement (pctxt, "fastConnectRefused", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "fastConnectRefused", -1);
                     break;

                  case 5:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SecurityErrors                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SecurityErrors (OOCTXT* pctxt, H225SecurityErrors* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 15);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* securityWrongSyncTime */
         case 0:
            invokeStartElement (pctxt, "securityWrongSyncTime", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongSyncTime", -1);

            break;

         /* securityReplay */
         case 1:
            invokeStartElement (pctxt, "securityReplay", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityReplay", -1);

            break;

         /* securityWrongGeneralID */
         case 2:
            invokeStartElement (pctxt, "securityWrongGeneralID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongGeneralID", -1);

            break;

         /* securityWrongSendersID */
         case 3:
            invokeStartElement (pctxt, "securityWrongSendersID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongSendersID", -1);

            break;

         /* securityIntegrityFailed */
         case 4:
            invokeStartElement (pctxt, "securityIntegrityFailed", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityIntegrityFailed", -1);

            break;

         /* securityWrongOID */
         case 5:
            invokeStartElement (pctxt, "securityWrongOID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongOID", -1);

            break;

         /* securityDHmismatch */
         case 6:
            invokeStartElement (pctxt, "securityDHmismatch", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDHmismatch", -1);

            break;

         /* securityCertificateExpired */
         case 7:
            invokeStartElement (pctxt, "securityCertificateExpired", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityCertificateExpired", -1);

            break;

         /* securityCertificateDateInvalid */
         case 8:
            invokeStartElement (pctxt, "securityCertificateDateInvalid", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityCertificateDateInvalid", -1);

            break;

         /* securityCertificateRevoked */
         case 9:
            invokeStartElement (pctxt, "securityCertificateRevoked", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityCertificateRevoked", -1);

            break;

         /* securityCertificateNotReadable */
         case 10:
            invokeStartElement (pctxt, "securityCertificateNotReadable", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityCertificateNotReadable", -1);

            break;

         /* securityCertificateSignatureInvalid */
         case 11:
            invokeStartElement (pctxt, "securityCertificateSignatureInvalid", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityCertificateSignatureInvalid", -1);

            break;

         /* securityCertificateMissing */
         case 12:
            invokeStartElement (pctxt, "securityCertificateMissing", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityCertificateMissing", -1);

            break;

         /* securityCertificateIncomplete */
         case 13:
            invokeStartElement (pctxt, "securityCertificateIncomplete", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityCertificateIncomplete", -1);

            break;

         /* securityUnsupportedCertificateAlgOID */
         case 14:
            invokeStartElement (pctxt, "securityUnsupportedCertificateAlgOID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityUnsupportedCertificateAlgOID", -1);

            break;

         /* securityUnknownCA */
         case 15:
            invokeStartElement (pctxt, "securityUnknownCA", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityUnknownCA", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 17;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ReleaseCompleteReason                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ReleaseCompleteReason (OOCTXT* pctxt, H225ReleaseCompleteReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 11);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* noBandwidth */
         case 0:
            invokeStartElement (pctxt, "noBandwidth", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "noBandwidth", -1);

            break;

         /* gatekeeperResources */
         case 1:
            invokeStartElement (pctxt, "gatekeeperResources", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "gatekeeperResources", -1);

            break;

         /* unreachableDestination */
         case 2:
            invokeStartElement (pctxt, "unreachableDestination", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "unreachableDestination", -1);

            break;

         /* destinationRejection */
         case 3:
            invokeStartElement (pctxt, "destinationRejection", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "destinationRejection", -1);

            break;

         /* invalidRevision */
         case 4:
            invokeStartElement (pctxt, "invalidRevision", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidRevision", -1);

            break;

         /* noPermission */
         case 5:
            invokeStartElement (pctxt, "noPermission", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "noPermission", -1);

            break;

         /* unreachableGatekeeper */
         case 6:
            invokeStartElement (pctxt, "unreachableGatekeeper", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "unreachableGatekeeper", -1);

            break;

         /* gatewayResources */
         case 7:
            invokeStartElement (pctxt, "gatewayResources", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "gatewayResources", -1);

            break;

         /* badFormatAddress */
         case 8:
            invokeStartElement (pctxt, "badFormatAddress", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "badFormatAddress", -1);

            break;

         /* adaptiveBusy */
         case 9:
            invokeStartElement (pctxt, "adaptiveBusy", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "adaptiveBusy", -1);

            break;

         /* inConf */
         case 10:
            invokeStartElement (pctxt, "inConf", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "inConf", -1);

            break;

         /* undefinedReason */
         case 11:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 13;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* facilityCallDeflection */
         case 13:
            invokeStartElement (pctxt, "facilityCallDeflection", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "facilityCallDeflection", -1);

            break;

         /* securityDenied */
         case 14:
            invokeStartElement (pctxt, "securityDenied", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenied", -1);

            break;

         /* calledPartyNotRegistered */
         case 15:
            invokeStartElement (pctxt, "calledPartyNotRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "calledPartyNotRegistered", -1);

            break;

         /* callerNotRegistered */
         case 16:
            invokeStartElement (pctxt, "callerNotRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "callerNotRegistered", -1);

            break;

         /* newConnectionNeeded */
         case 17:
            invokeStartElement (pctxt, "newConnectionNeeded", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "newConnectionNeeded", -1);

            break;

         /* nonStandardReason */
         case 18:
            invokeStartElement (pctxt, "nonStandardReason", -1);

            pvalue->u.nonStandardReason = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandardReason);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandardReason", -1);

            break;

         /* replaceWithConferenceInvite */
         case 19:
            invokeStartElement (pctxt, "replaceWithConferenceInvite", -1);

            pvalue->u.replaceWithConferenceInvite = ALLOC_ASN1ELEM (pctxt, H225ConferenceIdentifier);

            stat = asn1PD_H225ConferenceIdentifier (pctxt, pvalue->u.replaceWithConferenceInvite);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "replaceWithConferenceInvite", -1);

            break;

         /* genericDataReason */
         case 20:
            invokeStartElement (pctxt, "genericDataReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "genericDataReason", -1);

            break;

         /* neededFeatureNotSupported */
         case 21:
            invokeStartElement (pctxt, "neededFeatureNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "neededFeatureNotSupported", -1);

            break;

         /* tunnelledSignallingRejected */
         case 22:
            invokeStartElement (pctxt, "tunnelledSignallingRejected", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "tunnelledSignallingRejected", -1);

            break;

         /* invalidCID */
         case 23:
            invokeStartElement (pctxt, "invalidCID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidCID", -1);

            break;

         /* securityError */
         case 24:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors);

            stat = asn1PD_H225SecurityErrors (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         /* hopCountExceeded */
         case 25:
            invokeStartElement (pctxt, "hopCountExceeded", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "hopCountExceeded", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ReleaseComplete_UUIE                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ReleaseComplete_UUIE (OOCTXT* pctxt, H225ReleaseComplete_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.reasonPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode reason */

   if (pvalue->m.reasonPresent) {
      invokeStartElement (pctxt, "reason", -1);

      stat = asn1PD_H225ReleaseCompleteReason (pctxt, &pvalue->reason);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "reason", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 9 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.busyAddressPresent = 1;

                     invokeStartElement (pctxt, "busyAddress", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->busyAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "busyAddress", -1);
                     break;

                  case 4:
                     pvalue->m.presentationIndicatorPresent = 1;

                     invokeStartElement (pctxt, "presentationIndicator", -1);

                     stat = asn1PD_H225PresentationIndicator (pctxt, &pvalue->presentationIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "presentationIndicator", -1);
                     break;

                  case 5:
                     pvalue->m.screeningIndicatorPresent = 1;

                     invokeStartElement (pctxt, "screeningIndicator", -1);

                     stat = asn1PD_H225ScreeningIndicator (pctxt, &pvalue->screeningIndicator);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "screeningIndicator", -1);
                     break;

                  case 6:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 7:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 8:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  FacilityReason                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225FacilityReason (OOCTXT* pctxt, H225FacilityReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* routeCallToGatekeeper */
         case 0:
            invokeStartElement (pctxt, "routeCallToGatekeeper", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "routeCallToGatekeeper", -1);

            break;

         /* callForwarded */
         case 1:
            invokeStartElement (pctxt, "callForwarded", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "callForwarded", -1);

            break;

         /* routeCallToMC */
         case 2:
            invokeStartElement (pctxt, "routeCallToMC", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "routeCallToMC", -1);

            break;

         /* undefinedReason */
         case 3:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* conferenceListChoice */
         case 5:
            invokeStartElement (pctxt, "conferenceListChoice", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "conferenceListChoice", -1);

            break;

         /* startH245 */
         case 6:
            invokeStartElement (pctxt, "startH245", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "startH245", -1);

            break;

         /* noH245 */
         case 7:
            invokeStartElement (pctxt, "noH245", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "noH245", -1);

            break;

         /* newTokens */
         case 8:
            invokeStartElement (pctxt, "newTokens", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "newTokens", -1);

            break;

         /* featureSetUpdate */
         case 9:
            invokeStartElement (pctxt, "featureSetUpdate", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "featureSetUpdate", -1);

            break;

         /* forwardedElements */
         case 10:
            invokeStartElement (pctxt, "forwardedElements", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "forwardedElements", -1);

            break;

         /* transportedInformation */
         case 11:
            invokeStartElement (pctxt, "transportedInformation", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "transportedInformation", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ConferenceList                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ConferenceList (OOCTXT* pctxt, H225ConferenceList* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.conferenceIDPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.conferenceAliasPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode conferenceID */

   if (pvalue->m.conferenceIDPresent) {
      invokeStartElement (pctxt, "conferenceID", -1);

      stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "conferenceID", -1);
   }

   /* decode conferenceAlias */

   if (pvalue->m.conferenceAliasPresent) {
      invokeStartElement (pctxt, "conferenceAlias", -1);

      stat = asn1PD_H225AliasAddress (pctxt, &pvalue->conferenceAlias);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "conferenceAlias", -1);
   }

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225ConferenceList                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225ConferenceList (OOCTXT* pctxt, H225_SeqOfH225ConferenceList* pvalue)
{
   int stat = ASN_OK;
   H225ConferenceList* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225ConferenceList);

         stat = asn1PD_H225ConferenceList (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Facility_UUIE_fastStart                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Facility_UUIE_fastStart (OOCTXT* pctxt, H225Facility_UUIE_fastStart* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Facility_UUIE                                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Facility_UUIE (OOCTXT* pctxt, H225Facility_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.alternativeAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.alternativeAliasAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.conferenceIDPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode alternativeAddress */

   if (pvalue->m.alternativeAddressPresent) {
      invokeStartElement (pctxt, "alternativeAddress", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->alternativeAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "alternativeAddress", -1);
   }

   /* decode alternativeAliasAddress */

   if (pvalue->m.alternativeAliasAddressPresent) {
      invokeStartElement (pctxt, "alternativeAliasAddress", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->alternativeAliasAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "alternativeAliasAddress", -1);
   }

   /* decode conferenceID */

   if (pvalue->m.conferenceIDPresent) {
      invokeStartElement (pctxt, "conferenceID", -1);

      stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "conferenceID", -1);
   }

   /* decode reason */

   invokeStartElement (pctxt, "reason", -1);

   stat = asn1PD_H225FacilityReason (pctxt, &pvalue->reason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "reason", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 16 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.destExtraCallInfoPresent = 1;

                     invokeStartElement (pctxt, "destExtraCallInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destExtraCallInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destExtraCallInfo", -1);
                     break;

                  case 2:
                     pvalue->m.remoteExtensionAddressPresent = 1;

                     invokeStartElement (pctxt, "remoteExtensionAddress", -1);

                     stat = asn1PD_H225AliasAddress (pctxt, &pvalue->remoteExtensionAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "remoteExtensionAddress", -1);
                     break;

                  case 3:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 4:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 5:
                     pvalue->m.conferencesPresent = 1;

                     invokeStartElement (pctxt, "conferences", -1);

                     stat = asn1PD_H225_SeqOfH225ConferenceList (pctxt, &pvalue->conferences);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "conferences", -1);
                     break;

                  case 6:
                     pvalue->m.h245AddressPresent = 1;

                     invokeStartElement (pctxt, "h245Address", -1);

                     stat = asn1PD_H225TransportAddress (pctxt, &pvalue->h245Address);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h245Address", -1);
                     break;

                  case 7:
                     pvalue->m.fastStartPresent = 1;

                     invokeStartElement (pctxt, "fastStart", -1);

                     stat = asn1PD_H225Facility_UUIE_fastStart (pctxt, &pvalue->fastStart);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "fastStart", -1);
                     break;

                  case 8:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 9:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 10:
                     pvalue->m.fastConnectRefusedPresent = 1;

                     invokeStartElement (pctxt, "fastConnectRefused", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "fastConnectRefused", -1);
                     break;

                  case 11:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 12:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 13:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 14:
                     pvalue->m.destinationInfoPresent = 1;

                     invokeStartElement (pctxt, "destinationInfo", -1);

                     stat = asn1PD_H225EndpointType (pctxt, &pvalue->destinationInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destinationInfo", -1);
                     break;

                  case 15:
                     pvalue->m.h245SecurityModePresent = 1;

                     invokeStartElement (pctxt, "h245SecurityMode", -1);

                     stat = asn1PD_H225H245Security (pctxt, &pvalue->h245SecurityMode);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h245SecurityMode", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Progress_UUIE_fastStart                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Progress_UUIE_fastStart (OOCTXT* pctxt, H225Progress_UUIE_fastStart* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Progress_UUIE                                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Progress_UUIE (OOCTXT* pctxt, H225Progress_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h245AddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h245SecurityModePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.fastStartPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode destinationInfo */

   invokeStartElement (pctxt, "destinationInfo", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->destinationInfo);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "destinationInfo", -1);

   /* decode h245Address */

   if (pvalue->m.h245AddressPresent) {
      invokeStartElement (pctxt, "h245Address", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->h245Address);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h245Address", -1);
   }

   /* decode callIdentifier */

   invokeStartElement (pctxt, "callIdentifier", -1);

   stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callIdentifier", -1);

   /* decode h245SecurityMode */

   if (pvalue->m.h245SecurityModePresent) {
      invokeStartElement (pctxt, "h245SecurityMode", -1);

      stat = asn1PD_H225H245Security (pctxt, &pvalue->h245SecurityMode);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "h245SecurityMode", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode fastStart */

   if (pvalue->m.fastStartPresent) {
      invokeStartElement (pctxt, "fastStart", -1);

      stat = asn1PD_H225Progress_UUIE_fastStart (pctxt, &pvalue->fastStart);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "fastStart", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 3 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 1:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 2:
                     pvalue->m.fastConnectRefusedPresent = 1;

                     invokeStartElement (pctxt, "fastConnectRefused", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "fastConnectRefused", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Status_UUIE                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Status_UUIE (OOCTXT* pctxt, H225Status_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode callIdentifier */

   invokeStartElement (pctxt, "callIdentifier", -1);

   stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callIdentifier", -1);

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  StatusInquiry_UUIE                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225StatusInquiry_UUIE (OOCTXT* pctxt, H225StatusInquiry_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode callIdentifier */

   invokeStartElement (pctxt, "callIdentifier", -1);

   stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callIdentifier", -1);

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SetupAcknowledge_UUIE                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SetupAcknowledge_UUIE (OOCTXT* pctxt, H225SetupAcknowledge_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode callIdentifier */

   invokeStartElement (pctxt, "callIdentifier", -1);

   stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callIdentifier", -1);

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Notify_UUIE                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Notify_UUIE (OOCTXT* pctxt, H225Notify_UUIE* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode callIdentifier */

   invokeStartElement (pctxt, "callIdentifier", -1);

   stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callIdentifier", -1);

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UU_PDU_h323_message_body                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UU_PDU_h323_message_body (OOCTXT* pctxt, H225H323_UU_PDU_h323_message_body* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 6);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* setup */
         case 0:
            invokeStartElement (pctxt, "setup", -1);

            pvalue->u.setup = ALLOC_ASN1ELEM (pctxt, H225Setup_UUIE);

            stat = asn1PD_H225Setup_UUIE (pctxt, pvalue->u.setup);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "setup", -1);

            break;

         /* callProceeding */
         case 1:
            invokeStartElement (pctxt, "callProceeding", -1);

            pvalue->u.callProceeding = ALLOC_ASN1ELEM (pctxt, H225CallProceeding_UUIE);

            stat = asn1PD_H225CallProceeding_UUIE (pctxt, pvalue->u.callProceeding);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "callProceeding", -1);

            break;

         /* connect */
         case 2:
            invokeStartElement (pctxt, "connect", -1);

            pvalue->u.connect = ALLOC_ASN1ELEM (pctxt, H225Connect_UUIE);

            stat = asn1PD_H225Connect_UUIE (pctxt, pvalue->u.connect);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "connect", -1);

            break;

         /* alerting */
         case 3:
            invokeStartElement (pctxt, "alerting", -1);

            pvalue->u.alerting = ALLOC_ASN1ELEM (pctxt, H225Alerting_UUIE);

            stat = asn1PD_H225Alerting_UUIE (pctxt, pvalue->u.alerting);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "alerting", -1);

            break;

         /* information */
         case 4:
            invokeStartElement (pctxt, "information", -1);

            pvalue->u.information = ALLOC_ASN1ELEM (pctxt, H225Information_UUIE);

            stat = asn1PD_H225Information_UUIE (pctxt, pvalue->u.information);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "information", -1);

            break;

         /* releaseComplete */
         case 5:
            invokeStartElement (pctxt, "releaseComplete", -1);

            pvalue->u.releaseComplete = ALLOC_ASN1ELEM (pctxt, H225ReleaseComplete_UUIE);

            stat = asn1PD_H225ReleaseComplete_UUIE (pctxt, pvalue->u.releaseComplete);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "releaseComplete", -1);

            break;

         /* facility */
         case 6:
            invokeStartElement (pctxt, "facility", -1);

            pvalue->u.facility = ALLOC_ASN1ELEM (pctxt, H225Facility_UUIE);

            stat = asn1PD_H225Facility_UUIE (pctxt, pvalue->u.facility);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "facility", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 8;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* progress */
         case 8:
            invokeStartElement (pctxt, "progress", -1);

            pvalue->u.progress = ALLOC_ASN1ELEM (pctxt, H225Progress_UUIE);

            stat = asn1PD_H225Progress_UUIE (pctxt, pvalue->u.progress);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "progress", -1);

            break;

         /* empty */
         case 9:
            invokeStartElement (pctxt, "empty", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "empty", -1);

            break;

         /* status */
         case 10:
            invokeStartElement (pctxt, "status", -1);

            pvalue->u.status = ALLOC_ASN1ELEM (pctxt, H225Status_UUIE);

            stat = asn1PD_H225Status_UUIE (pctxt, pvalue->u.status);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "status", -1);

            break;

         /* statusInquiry */
         case 11:
            invokeStartElement (pctxt, "statusInquiry", -1);

            pvalue->u.statusInquiry = ALLOC_ASN1ELEM (pctxt, H225StatusInquiry_UUIE);

            stat = asn1PD_H225StatusInquiry_UUIE (pctxt, pvalue->u.statusInquiry);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "statusInquiry", -1);

            break;

         /* setupAcknowledge */
         case 12:
            invokeStartElement (pctxt, "setupAcknowledge", -1);

            pvalue->u.setupAcknowledge = ALLOC_ASN1ELEM (pctxt, H225SetupAcknowledge_UUIE);

            stat = asn1PD_H225SetupAcknowledge_UUIE (pctxt, pvalue->u.setupAcknowledge);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "setupAcknowledge", -1);

            break;

         /* notify */
         case 13:
            invokeStartElement (pctxt, "notify", -1);

            pvalue->u.notify = ALLOC_ASN1ELEM (pctxt, H225Notify_UUIE);

            stat = asn1PD_H225Notify_UUIE (pctxt, pvalue->u.notify);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "notify", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UU_PDU_h4501SupplementaryService                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UU_PDU_h4501SupplementaryService (OOCTXT* pctxt, H225H323_UU_PDU_h4501SupplementaryService* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UU_PDU_h245Control                                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UU_PDU_h245Control (OOCTXT* pctxt, H225H323_UU_PDU_h245Control* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225NonStandardParameter                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225NonStandardParameter (OOCTXT* pctxt, H225_SeqOfH225NonStandardParameter* pvalue)
{
   int stat = ASN_OK;
   H225NonStandardParameter* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225NonStandardParameter);

         stat = asn1PD_H225NonStandardParameter (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallLinkage                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallLinkage (OOCTXT* pctxt, H225CallLinkage* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.globalCallIdPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.threadIdPresent = optbit;

   /* decode globalCallId */

   if (pvalue->m.globalCallIdPresent) {
      invokeStartElement (pctxt, "globalCallId", -1);

      stat = asn1PD_H225GloballyUniqueID (pctxt, &pvalue->globalCallId);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "globalCallId", -1);
   }

   /* decode threadId */

   if (pvalue->m.threadIdPresent) {
      invokeStartElement (pctxt, "threadId", -1);

      stat = asn1PD_H225GloballyUniqueID (pctxt, &pvalue->threadId);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "threadId", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UU_PDU_tunnelledSignallingMessage_messageContent     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UU_PDU_tunnelledSignallingMessage_messageContent (OOCTXT* pctxt, H225H323_UU_PDU_tunnelledSignallingMessage_messageContent* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1DynOctStr);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->elem[xx1].numocts, pvalue->elem[xx1].data);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UU_PDU_tunnelledSignallingMessage                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UU_PDU_tunnelledSignallingMessage (OOCTXT* pctxt, H225H323_UU_PDU_tunnelledSignallingMessage* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tunnellingRequiredPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode tunnelledProtocolID */

   invokeStartElement (pctxt, "tunnelledProtocolID", -1);

   stat = asn1PD_H225TunnelledProtocol (pctxt, &pvalue->tunnelledProtocolID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "tunnelledProtocolID", -1);

   /* decode messageContent */

   invokeStartElement (pctxt, "messageContent", -1);

   stat = asn1PD_H225H323_UU_PDU_tunnelledSignallingMessage_messageContent (pctxt, &pvalue->messageContent);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "messageContent", -1);

   /* decode tunnellingRequired */

   if (pvalue->m.tunnellingRequiredPresent) {
      invokeStartElement (pctxt, "tunnellingRequired", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "tunnellingRequired", -1);
   }

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  StimulusControl                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225StimulusControl (OOCTXT* pctxt, H225StimulusControl* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.isTextPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.h248MessagePresent = optbit;

   /* decode nonStandard */

   if (pvalue->m.nonStandardPresent) {
      invokeStartElement (pctxt, "nonStandard", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandard);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandard", -1);
   }

   /* decode isText */

   if (pvalue->m.isTextPresent) {
      invokeStartElement (pctxt, "isText", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "isText", -1);
   }

   /* decode h248Message */

   if (pvalue->m.h248MessagePresent) {
      invokeStartElement (pctxt, "h248Message", -1);

      stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->h248Message);
      if (stat != ASN_OK) return stat;
      invokeOctStrValue (pctxt, pvalue->h248Message.numocts, pvalue->h248Message.data);

      invokeEndElement (pctxt, "h248Message", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UU_PDU                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UU_PDU (OOCTXT* pctxt, H225H323_UU_PDU* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode h323_message_body */

   invokeStartElement (pctxt, "h323_message_body", -1);

   stat = asn1PD_H225H323_UU_PDU_h323_message_body (pctxt, &pvalue->h323_message_body);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "h323_message_body", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 9 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.h4501SupplementaryServicePresent = 1;

                     invokeStartElement (pctxt, "h4501SupplementaryService", -1);

                     stat = asn1PD_H225H323_UU_PDU_h4501SupplementaryService (pctxt, &pvalue->h4501SupplementaryService);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h4501SupplementaryService", -1);
                     break;

                  case 1:
                     pvalue->m.h245TunnelingPresent = 1;

                     invokeStartElement (pctxt, "h245Tunneling", -1);

                     stat = DECODEBIT (pctxt, &pvalue->h245Tunneling);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->h245Tunneling);

                     invokeEndElement (pctxt, "h245Tunneling", -1);
                     break;

                  case 2:
                     pvalue->m.h245ControlPresent = 1;

                     invokeStartElement (pctxt, "h245Control", -1);

                     stat = asn1PD_H225H323_UU_PDU_h245Control (pctxt, &pvalue->h245Control);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "h245Control", -1);
                     break;

                  case 3:
                     pvalue->m.nonStandardControlPresent = 1;

                     invokeStartElement (pctxt, "nonStandardControl", -1);

                     stat = asn1PD_H225_SeqOfH225NonStandardParameter (pctxt, &pvalue->nonStandardControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "nonStandardControl", -1);
                     break;

                  case 4:
                     pvalue->m.callLinkagePresent = 1;

                     invokeStartElement (pctxt, "callLinkage", -1);

                     stat = asn1PD_H225CallLinkage (pctxt, &pvalue->callLinkage);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callLinkage", -1);
                     break;

                  case 5:
                     pvalue->m.tunnelledSignallingMessagePresent = 1;

                     invokeStartElement (pctxt, "tunnelledSignallingMessage", -1);

                     stat = asn1PD_H225H323_UU_PDU_tunnelledSignallingMessage (pctxt, &pvalue->tunnelledSignallingMessage);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tunnelledSignallingMessage", -1);
                     break;

                  case 6:
                     pvalue->m.provisionalRespToH245TunnelingPresent = 1;

                     invokeStartElement (pctxt, "provisionalRespToH245Tunneling", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "provisionalRespToH245Tunneling", -1);
                     break;

                  case 7:
                     pvalue->m.stimulusControlPresent = 1;

                     invokeStartElement (pctxt, "stimulusControl", -1);

                     stat = asn1PD_H225StimulusControl (pctxt, &pvalue->stimulusControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "stimulusControl", -1);
                     break;

                  case 8:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UserInformation_user_data                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UserInformation_user_data (OOCTXT* pctxt, H225H323_UserInformation_user_data* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode protocol_discriminator */

   invokeStartElement (pctxt, "protocol_discriminator", -1);

   stat = decodeConsUInt8 (pctxt, &pvalue->protocol_discriminator, 0U, 255U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->protocol_discriminator);

   invokeEndElement (pctxt, "protocol_discriminator", -1);

   /* decode user_information */

   invokeStartElement (pctxt, "user_information", -1);

   stat = asn1PD_H225H323_UserInformation_user_data_user_information (pctxt, &pvalue->user_information);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "user_information", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  H323_UserInformation                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225H323_UserInformation (OOCTXT* pctxt, H225H323_UserInformation* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.user_dataPresent = optbit;

   /* decode h323_uu_pdu */

   invokeStartElement (pctxt, "h323_uu_pdu", -1);

   stat = asn1PD_H225H323_UU_PDU (pctxt, &pvalue->h323_uu_pdu);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "h323_uu_pdu", -1);

   /* decode user_data */

   if (pvalue->m.user_dataPresent) {
      invokeStartElement (pctxt, "user_data", -1);

      stat = asn1PD_H225H323_UserInformation_user_data (pctxt, &pvalue->user_data);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "user_data", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AddressPattern_range                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AddressPattern_range (OOCTXT* pctxt, H225AddressPattern_range* pvalue)
{
   int stat = ASN_OK;

   /* decode startOfRange */

   invokeStartElement (pctxt, "startOfRange", -1);

   stat = asn1PD_H225PartyNumber (pctxt, &pvalue->startOfRange);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "startOfRange", -1);

   /* decode endOfRange */

   invokeStartElement (pctxt, "endOfRange", -1);

   stat = asn1PD_H225PartyNumber (pctxt, &pvalue->endOfRange);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endOfRange", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AddressPattern                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AddressPattern (OOCTXT* pctxt, H225AddressPattern* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* wildcard */
         case 0:
            invokeStartElement (pctxt, "wildcard", -1);

            pvalue->u.wildcard = ALLOC_ASN1ELEM (pctxt, H225AliasAddress);

            stat = asn1PD_H225AliasAddress (pctxt, pvalue->u.wildcard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "wildcard", -1);

            break;

         /* range */
         case 1:
            invokeStartElement (pctxt, "range", -1);

            pvalue->u.range = ALLOC_ASN1ELEM (pctxt, H225AddressPattern_range);

            stat = asn1PD_H225AddressPattern_range (pctxt, pvalue->u.range);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "range", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225TransportAddress                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225TransportAddress (OOCTXT* pctxt, H225_SeqOfH225TransportAddress* pvalue)
{
   int stat = ASN_OK;
   H225TransportAddress* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225TransportAddress);

         stat = asn1PD_H225TransportAddress (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AlternateTransportAddresses                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AlternateTransportAddresses (OOCTXT* pctxt, H225AlternateTransportAddresses* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.annexEPresent = optbit;

   /* decode annexE */

   if (pvalue->m.annexEPresent) {
      invokeStartElement (pctxt, "annexE", -1);

      stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->annexE);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "annexE", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 1 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.sctpPresent = 1;

                     invokeStartElement (pctxt, "sctp", -1);

                     stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->sctp);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "sctp", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  Endpoint                                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225Endpoint (OOCTXT* pctxt, H225Endpoint* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.aliasAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callSignalAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.rasAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endpointTypePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.priorityPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.remoteExtensionAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destExtraCallInfoPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode aliasAddress */

   if (pvalue->m.aliasAddressPresent) {
      invokeStartElement (pctxt, "aliasAddress", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->aliasAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "aliasAddress", -1);
   }

   /* decode callSignalAddress */

   if (pvalue->m.callSignalAddressPresent) {
      invokeStartElement (pctxt, "callSignalAddress", -1);

      stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->callSignalAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callSignalAddress", -1);
   }

   /* decode rasAddress */

   if (pvalue->m.rasAddressPresent) {
      invokeStartElement (pctxt, "rasAddress", -1);

      stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->rasAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "rasAddress", -1);
   }

   /* decode endpointType */

   if (pvalue->m.endpointTypePresent) {
      invokeStartElement (pctxt, "endpointType", -1);

      stat = asn1PD_H225EndpointType (pctxt, &pvalue->endpointType);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endpointType", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode priority */

   if (pvalue->m.priorityPresent) {
      invokeStartElement (pctxt, "priority", -1);

      stat = decodeConsUInt8 (pctxt, &pvalue->priority, 0U, 127U);
      if (stat != ASN_OK) return stat;
      invokeUIntValue (pctxt, pvalue->priority);

      invokeEndElement (pctxt, "priority", -1);
   }

   /* decode remoteExtensionAddress */

   if (pvalue->m.remoteExtensionAddressPresent) {
      invokeStartElement (pctxt, "remoteExtensionAddress", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->remoteExtensionAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "remoteExtensionAddress", -1);
   }

   /* decode destExtraCallInfo */

   if (pvalue->m.destExtraCallInfoPresent) {
      invokeStartElement (pctxt, "destExtraCallInfo", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destExtraCallInfo);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destExtraCallInfo", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 3 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.alternateTransportAddressesPresent = 1;

                     invokeStartElement (pctxt, "alternateTransportAddresses", -1);

                     stat = asn1PD_H225AlternateTransportAddresses (pctxt, &pvalue->alternateTransportAddresses);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateTransportAddresses", -1);
                     break;

                  case 1:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 2:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UseSpecifiedTransport                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UseSpecifiedTransport (OOCTXT* pctxt, H225UseSpecifiedTransport* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* tcp */
         case 0:
            invokeStartElement (pctxt, "tcp", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "tcp", -1);

            break;

         /* annexE */
         case 1:
            invokeStartElement (pctxt, "annexE", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "annexE", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* sctp */
         case 3:
            invokeStartElement (pctxt, "sctp", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "sctp", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AlternateGK                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AlternateGK (OOCTXT* pctxt, H225AlternateGK* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperIdentifierPresent = optbit;

   /* decode rasAddress */

   invokeStartElement (pctxt, "rasAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->rasAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rasAddress", -1);

   /* decode gatekeeperIdentifier */

   if (pvalue->m.gatekeeperIdentifierPresent) {
      invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

      stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
   }

   /* decode needToRegister */

   invokeStartElement (pctxt, "needToRegister", -1);

   stat = DECODEBIT (pctxt, &pvalue->needToRegister);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->needToRegister);

   invokeEndElement (pctxt, "needToRegister", -1);

   /* decode priority */

   invokeStartElement (pctxt, "priority", -1);

   stat = decodeConsUInt8 (pctxt, &pvalue->priority, 0U, 127U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->priority);

   invokeEndElement (pctxt, "priority", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225AlternateGK                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225AlternateGK (OOCTXT* pctxt, H225_SeqOfH225AlternateGK* pvalue)
{
   int stat = ASN_OK;
   H225AlternateGK* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225AlternateGK);

         stat = asn1PD_H225AlternateGK (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AltGKInfo                                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AltGKInfo (OOCTXT* pctxt, H225AltGKInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode alternateGatekeeper */

   invokeStartElement (pctxt, "alternateGatekeeper", -1);

   stat = asn1PD_H225_SeqOfH225AlternateGK (pctxt, &pvalue->alternateGatekeeper);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "alternateGatekeeper", -1);

   /* decode altGKisPermanent */

   invokeStartElement (pctxt, "altGKisPermanent", -1);

   stat = DECODEBIT (pctxt, &pvalue->altGKisPermanent);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->altGKisPermanent);

   invokeEndElement (pctxt, "altGKisPermanent", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  SecurityErrors2                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225SecurityErrors2 (OOCTXT* pctxt, H225SecurityErrors2* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 5);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* securityWrongSyncTime */
         case 0:
            invokeStartElement (pctxt, "securityWrongSyncTime", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongSyncTime", -1);

            break;

         /* securityReplay */
         case 1:
            invokeStartElement (pctxt, "securityReplay", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityReplay", -1);

            break;

         /* securityWrongGeneralID */
         case 2:
            invokeStartElement (pctxt, "securityWrongGeneralID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongGeneralID", -1);

            break;

         /* securityWrongSendersID */
         case 3:
            invokeStartElement (pctxt, "securityWrongSendersID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongSendersID", -1);

            break;

         /* securityIntegrityFailed */
         case 4:
            invokeStartElement (pctxt, "securityIntegrityFailed", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityIntegrityFailed", -1);

            break;

         /* securityWrongOID */
         case 5:
            invokeStartElement (pctxt, "securityWrongOID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityWrongOID", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 7;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  EncryptIntAlg                                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225EncryptIntAlg (OOCTXT* pctxt, H225EncryptIntAlg* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* nonStandard */
         case 0:
            invokeStartElement (pctxt, "nonStandard", -1);

            pvalue->u.nonStandard = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandard", -1);

            break;

         /* isoAlgorithm */
         case 1:
            invokeStartElement (pctxt, "isoAlgorithm", -1);

            pvalue->u.isoAlgorithm = ALLOC_ASN1ELEM (pctxt, ASN1OBJID);

            stat = decodeObjectIdentifier (pctxt, pvalue->u.isoAlgorithm);
            if (stat != ASN_OK) return stat;
            invokeOidValue (pctxt, pvalue->u.isoAlgorithm->numids, pvalue->u.isoAlgorithm->subid);

            invokeEndElement (pctxt, "isoAlgorithm", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  NonIsoIntegrityMechanism                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225NonIsoIntegrityMechanism (OOCTXT* pctxt, H225NonIsoIntegrityMechanism* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* hMAC_MD5 */
         case 0:
            invokeStartElement (pctxt, "hMAC_MD5", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "hMAC_MD5", -1);

            break;

         /* hMAC_iso10118_2_s */
         case 1:
            invokeStartElement (pctxt, "hMAC_iso10118_2_s", -1);

            pvalue->u.hMAC_iso10118_2_s = ALLOC_ASN1ELEM (pctxt, H225EncryptIntAlg);

            stat = asn1PD_H225EncryptIntAlg (pctxt, pvalue->u.hMAC_iso10118_2_s);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "hMAC_iso10118_2_s", -1);

            break;

         /* hMAC_iso10118_2_l */
         case 2:
            invokeStartElement (pctxt, "hMAC_iso10118_2_l", -1);

            pvalue->u.hMAC_iso10118_2_l = ALLOC_ASN1ELEM (pctxt, H225EncryptIntAlg);

            stat = asn1PD_H225EncryptIntAlg (pctxt, pvalue->u.hMAC_iso10118_2_l);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "hMAC_iso10118_2_l", -1);

            break;

         /* hMAC_iso10118_3 */
         case 3:
            invokeStartElement (pctxt, "hMAC_iso10118_3", -1);

            pvalue->u.hMAC_iso10118_3 = ALLOC_ASN1ELEM (pctxt, ASN1OBJID);

            stat = decodeObjectIdentifier (pctxt, pvalue->u.hMAC_iso10118_3);
            if (stat != ASN_OK) return stat;
            invokeOidValue (pctxt, pvalue->u.hMAC_iso10118_3->numids, pvalue->u.hMAC_iso10118_3->subid);

            invokeEndElement (pctxt, "hMAC_iso10118_3", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  IntegrityMechanism                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225IntegrityMechanism (OOCTXT* pctxt, H225IntegrityMechanism* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* nonStandard */
         case 0:
            invokeStartElement (pctxt, "nonStandard", -1);

            pvalue->u.nonStandard = ALLOC_ASN1ELEM (pctxt, H225NonStandardParameter);

            stat = asn1PD_H225NonStandardParameter (pctxt, pvalue->u.nonStandard);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandard", -1);

            break;

         /* digSig */
         case 1:
            invokeStartElement (pctxt, "digSig", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "digSig", -1);

            break;

         /* iso9797 */
         case 2:
            invokeStartElement (pctxt, "iso9797", -1);

            pvalue->u.iso9797 = ALLOC_ASN1ELEM (pctxt, ASN1OBJID);

            stat = decodeObjectIdentifier (pctxt, pvalue->u.iso9797);
            if (stat != ASN_OK) return stat;
            invokeOidValue (pctxt, pvalue->u.iso9797->numids, pvalue->u.iso9797->subid);

            invokeEndElement (pctxt, "iso9797", -1);

            break;

         /* nonIsoIM */
         case 3:
            invokeStartElement (pctxt, "nonIsoIM", -1);

            pvalue->u.nonIsoIM = ALLOC_ASN1ELEM (pctxt, H225NonIsoIntegrityMechanism);

            stat = asn1PD_H225NonIsoIntegrityMechanism (pctxt, pvalue->u.nonIsoIM);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonIsoIM", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ICV                                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ICV (OOCTXT* pctxt, H225ICV* pvalue)
{
   int stat = ASN_OK;

   /* decode algorithmOID */

   invokeStartElement (pctxt, "algorithmOID", -1);

   stat = decodeObjectIdentifier (pctxt, &pvalue->algorithmOID);
   if (stat != ASN_OK) return stat;
   invokeOidValue (pctxt, pvalue->algorithmOID.numids, pvalue->algorithmOID.subid);

   invokeEndElement (pctxt, "algorithmOID", -1);

   /* decode icv */

   invokeStartElement (pctxt, "icv", -1);

   stat = decodeDynBitString (pctxt, (ASN1DynBitStr*)&pvalue->icv);
   if (stat != ASN_OK) return stat;

   invokeBitStrValue (pctxt, pvalue->icv.numbits, pvalue->icv.data);

   invokeEndElement (pctxt, "icv", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CapacityReportingCapability                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CapacityReportingCapability (OOCTXT* pctxt, H225CapacityReportingCapability* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode canReportCallCapacity */

   invokeStartElement (pctxt, "canReportCallCapacity", -1);

   stat = DECODEBIT (pctxt, &pvalue->canReportCallCapacity);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->canReportCallCapacity);

   invokeEndElement (pctxt, "canReportCallCapacity", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CapacityReportingSpecification_when                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CapacityReportingSpecification_when (OOCTXT* pctxt, H225CapacityReportingSpecification_when* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callStartPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callEndPresent = optbit;

   /* decode callStart */

   if (pvalue->m.callStartPresent) {
      invokeStartElement (pctxt, "callStart", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "callStart", -1);
   }

   /* decode callEnd */

   if (pvalue->m.callEndPresent) {
      invokeStartElement (pctxt, "callEnd", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "callEnd", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CapacityReportingSpecification                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CapacityReportingSpecification (OOCTXT* pctxt, H225CapacityReportingSpecification* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode when */

   invokeStartElement (pctxt, "when", -1);

   stat = asn1PD_H225CapacityReportingSpecification_when (pctxt, &pvalue->when);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "when", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RasUsageInfoTypes                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RasUsageInfoTypes (OOCTXT* pctxt, H225RasUsageInfoTypes* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.startTimePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endTimePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.terminationCausePresent = optbit;

   /* decode nonStandardUsageTypes */

   invokeStartElement (pctxt, "nonStandardUsageTypes", -1);

   stat = asn1PD_H225_SeqOfH225NonStandardParameter (pctxt, &pvalue->nonStandardUsageTypes);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "nonStandardUsageTypes", -1);

   /* decode startTime */

   if (pvalue->m.startTimePresent) {
      invokeStartElement (pctxt, "startTime", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "startTime", -1);
   }

   /* decode endTime */

   if (pvalue->m.endTimePresent) {
      invokeStartElement (pctxt, "endTime", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "endTime", -1);
   }

   /* decode terminationCause */

   if (pvalue->m.terminationCausePresent) {
      invokeStartElement (pctxt, "terminationCause", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "terminationCause", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RasUsageSpecification_when                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RasUsageSpecification_when (OOCTXT* pctxt, H225RasUsageSpecification_when* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.startPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.inIrrPresent = optbit;

   /* decode start */

   if (pvalue->m.startPresent) {
      invokeStartElement (pctxt, "start", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "start", -1);
   }

   /* decode end */

   if (pvalue->m.endPresent) {
      invokeStartElement (pctxt, "end", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "end", -1);
   }

   /* decode inIrr */

   if (pvalue->m.inIrrPresent) {
      invokeStartElement (pctxt, "inIrr", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "inIrr", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RasUsageSpecification_callStartingPoint                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RasUsageSpecification_callStartingPoint (OOCTXT* pctxt, H225RasUsageSpecification_callStartingPoint* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.alertingPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.connectPresent = optbit;

   /* decode alerting */

   if (pvalue->m.alertingPresent) {
      invokeStartElement (pctxt, "alerting", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "alerting", -1);
   }

   /* decode connect */

   if (pvalue->m.connectPresent) {
      invokeStartElement (pctxt, "connect", -1);

      /* NULL */
      invokeNullValue (pctxt);

      invokeEndElement (pctxt, "connect", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RasUsageSpecification                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RasUsageSpecification (OOCTXT* pctxt, H225RasUsageSpecification* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callStartingPointPresent = optbit;

   /* decode when */

   invokeStartElement (pctxt, "when", -1);

   stat = asn1PD_H225RasUsageSpecification_when (pctxt, &pvalue->when);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "when", -1);

   /* decode callStartingPoint */

   if (pvalue->m.callStartingPointPresent) {
      invokeStartElement (pctxt, "callStartingPoint", -1);

      stat = asn1PD_H225RasUsageSpecification_callStartingPoint (pctxt, &pvalue->callStartingPoint);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callStartingPoint", -1);
   }

   /* decode required */

   invokeStartElement (pctxt, "required", -1);

   stat = asn1PD_H225RasUsageInfoTypes (pctxt, &pvalue->required);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "required", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RasUsageInformation                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RasUsageInformation (OOCTXT* pctxt, H225RasUsageInformation* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.alertingTimePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.connectTimePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endTimePresent = optbit;

   /* decode nonStandardUsageFields */

   invokeStartElement (pctxt, "nonStandardUsageFields", -1);

   stat = asn1PD_H225_SeqOfH225NonStandardParameter (pctxt, &pvalue->nonStandardUsageFields);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "nonStandardUsageFields", -1);

   /* decode alertingTime */

   if (pvalue->m.alertingTimePresent) {
      invokeStartElement (pctxt, "alertingTime", -1);

      stat = asn1PD_H235TimeStamp (pctxt, &pvalue->alertingTime);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "alertingTime", -1);
   }

   /* decode connectTime */

   if (pvalue->m.connectTimePresent) {
      invokeStartElement (pctxt, "connectTime", -1);

      stat = asn1PD_H235TimeStamp (pctxt, &pvalue->connectTime);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "connectTime", -1);
   }

   /* decode endTime */

   if (pvalue->m.endTimePresent) {
      invokeStartElement (pctxt, "endTime", -1);

      stat = asn1PD_H235TimeStamp (pctxt, &pvalue->endTime);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endTime", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallTerminationCause                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallTerminationCause (OOCTXT* pctxt, H225CallTerminationCause* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* releaseCompleteReason */
         case 0:
            invokeStartElement (pctxt, "releaseCompleteReason", -1);

            pvalue->u.releaseCompleteReason = ALLOC_ASN1ELEM (pctxt, H225ReleaseCompleteReason);

            stat = asn1PD_H225ReleaseCompleteReason (pctxt, pvalue->u.releaseCompleteReason);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "releaseCompleteReason", -1);

            break;

         /* releaseCompleteCauseIE */
         case 1:
            invokeStartElement (pctxt, "releaseCompleteCauseIE", -1);

            pvalue->u.releaseCompleteCauseIE = ALLOC_ASN1ELEM (pctxt, H225CallTerminationCause_releaseCompleteCauseIE);

            stat = asn1PD_H225CallTerminationCause_releaseCompleteCauseIE (pctxt, pvalue->u.releaseCompleteCauseIE);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "releaseCompleteCauseIE", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportChannelInfo                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportChannelInfo (OOCTXT* pctxt, H225TransportChannelInfo* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.sendAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.recvAddressPresent = optbit;

   /* decode sendAddress */

   if (pvalue->m.sendAddressPresent) {
      invokeStartElement (pctxt, "sendAddress", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->sendAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "sendAddress", -1);
   }

   /* decode recvAddress */

   if (pvalue->m.recvAddressPresent) {
      invokeStartElement (pctxt, "recvAddress", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->recvAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "recvAddress", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  BandwidthDetails                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225BandwidthDetails (OOCTXT* pctxt, H225BandwidthDetails* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode sender */

   invokeStartElement (pctxt, "sender", -1);

   stat = DECODEBIT (pctxt, &pvalue->sender);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->sender);

   invokeEndElement (pctxt, "sender", -1);

   /* decode multicast */

   invokeStartElement (pctxt, "multicast", -1);

   stat = DECODEBIT (pctxt, &pvalue->multicast);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->multicast);

   invokeEndElement (pctxt, "multicast", -1);

   /* decode bandwidth */

   invokeStartElement (pctxt, "bandwidth", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandwidth);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "bandwidth", -1);

   /* decode rtcpAddresses */

   invokeStartElement (pctxt, "rtcpAddresses", -1);

   stat = asn1PD_H225TransportChannelInfo (pctxt, &pvalue->rtcpAddresses);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rtcpAddresses", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallCreditCapability                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallCreditCapability (OOCTXT* pctxt, H225CallCreditCapability* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.canDisplayAmountStringPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.canEnforceDurationLimitPresent = optbit;

   /* decode canDisplayAmountString */

   if (pvalue->m.canDisplayAmountStringPresent) {
      invokeStartElement (pctxt, "canDisplayAmountString", -1);

      stat = DECODEBIT (pctxt, &pvalue->canDisplayAmountString);
      if (stat != ASN_OK) return stat;
      invokeBoolValue (pctxt, pvalue->canDisplayAmountString);

      invokeEndElement (pctxt, "canDisplayAmountString", -1);
   }

   /* decode canEnforceDurationLimit */

   if (pvalue->m.canEnforceDurationLimitPresent) {
      invokeStartElement (pctxt, "canEnforceDurationLimit", -1);

      stat = DECODEBIT (pctxt, &pvalue->canEnforceDurationLimit);
      if (stat != ASN_OK) return stat;
      invokeBoolValue (pctxt, pvalue->canEnforceDurationLimit);

      invokeEndElement (pctxt, "canEnforceDurationLimit", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RTPSession_associatedSessionIds                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RTPSession_associatedSessionIds (OOCTXT* pctxt, H225RTPSession_associatedSessionIds* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1UINT8);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeConsUInt8 (pctxt, &pvalue->elem[xx1], 1U, 255U);
      if (stat != ASN_OK) return stat;
      invokeUIntValue (pctxt, pvalue->elem[xx1]);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RTPSession                                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RTPSession (OOCTXT* pctxt, H225RTPSession* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   /* decode rtpAddress */

   invokeStartElement (pctxt, "rtpAddress", -1);

   stat = asn1PD_H225TransportChannelInfo (pctxt, &pvalue->rtpAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rtpAddress", -1);

   /* decode rtcpAddress */

   invokeStartElement (pctxt, "rtcpAddress", -1);

   stat = asn1PD_H225TransportChannelInfo (pctxt, &pvalue->rtcpAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rtcpAddress", -1);

   /* decode cname */

   invokeStartElement (pctxt, "cname", -1);

   stat = decodeConstrainedStringEx (pctxt, &pvalue->cname, 0, 8, 7, 7);
   if (stat != ASN_OK) return stat;
   invokeCharStrValue (pctxt, pvalue->cname);

   invokeEndElement (pctxt, "cname", -1);

   /* decode ssrc */

   invokeStartElement (pctxt, "ssrc", -1);

   stat = decodeConsUnsigned (pctxt, &pvalue->ssrc, 1U, ASN1UINT_MAX);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->ssrc);

   invokeEndElement (pctxt, "ssrc", -1);

   /* decode sessionId */

   invokeStartElement (pctxt, "sessionId", -1);

   stat = decodeConsUInt8 (pctxt, &pvalue->sessionId, 1U, 255U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->sessionId);

   invokeEndElement (pctxt, "sessionId", -1);

   /* decode associatedSessionIds */

   invokeStartElement (pctxt, "associatedSessionIds", -1);

   stat = asn1PD_H225RTPSession_associatedSessionIds (pctxt, &pvalue->associatedSessionIds);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "associatedSessionIds", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.multicastPresent = 1;

                     invokeStartElement (pctxt, "multicast", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "multicast", -1);
                     break;

                  case 1:
                     pvalue->m.bandwidthPresent = 1;

                     invokeStartElement (pctxt, "bandwidth", -1);

                     stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandwidth);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "bandwidth", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225Endpoint                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225Endpoint (OOCTXT* pctxt, H225_SeqOfH225Endpoint* pvalue)
{
   int stat = ASN_OK;
   H225Endpoint* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225Endpoint);

         stat = asn1PD_H225Endpoint (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225AuthenticationMechanism                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225AuthenticationMechanism (OOCTXT* pctxt, H225_SeqOfH225AuthenticationMechanism* pvalue)
{
   int stat = ASN_OK;
   H235AuthenticationMechanism* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H235AuthenticationMechanism);

         stat = asn1PD_H235AuthenticationMechanism (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatekeeperRequest_algorithmOIDs                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatekeeperRequest_algorithmOIDs (OOCTXT* pctxt, H225GatekeeperRequest_algorithmOIDs* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1OBJID);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = decodeObjectIdentifier (pctxt, &pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeOidValue (pctxt, pvalue->elem[xx1].numids, pvalue->elem[xx1].subid);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225IntegrityMechanism                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225IntegrityMechanism (OOCTXT* pctxt, H225_SeqOfH225IntegrityMechanism* pvalue)
{
   int stat = ASN_OK;
   H225IntegrityMechanism* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225IntegrityMechanism);

         stat = asn1PD_H225IntegrityMechanism (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatekeeperRequest                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatekeeperRequest (OOCTXT* pctxt, H225GatekeeperRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperIdentifierPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callServicesPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endpointAliasPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode rasAddress */

   invokeStartElement (pctxt, "rasAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->rasAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rasAddress", -1);

   /* decode endpointType */

   invokeStartElement (pctxt, "endpointType", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->endpointType);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointType", -1);

   /* decode gatekeeperIdentifier */

   if (pvalue->m.gatekeeperIdentifierPresent) {
      invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

      stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
   }

   /* decode callServices */

   if (pvalue->m.callServicesPresent) {
      invokeStartElement (pctxt, "callServices", -1);

      stat = asn1PD_H225QseriesOptions (pctxt, &pvalue->callServices);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callServices", -1);
   }

   /* decode endpointAlias */

   if (pvalue->m.endpointAliasPresent) {
      invokeStartElement (pctxt, "endpointAlias", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->endpointAlias);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endpointAlias", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 10 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.alternateEndpointsPresent = 1;

                     invokeStartElement (pctxt, "alternateEndpoints", -1);

                     stat = asn1PD_H225_SeqOfH225Endpoint (pctxt, &pvalue->alternateEndpoints);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateEndpoints", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.authenticationCapabilityPresent = 1;

                     invokeStartElement (pctxt, "authenticationCapability", -1);

                     stat = asn1PD_H225_SeqOfH225AuthenticationMechanism (pctxt, &pvalue->authenticationCapability);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "authenticationCapability", -1);
                     break;

                  case 4:
                     pvalue->m.algorithmOIDsPresent = 1;

                     invokeStartElement (pctxt, "algorithmOIDs", -1);

                     stat = asn1PD_H225GatekeeperRequest_algorithmOIDs (pctxt, &pvalue->algorithmOIDs);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "algorithmOIDs", -1);
                     break;

                  case 5:
                     pvalue->m.integrityPresent = 1;

                     invokeStartElement (pctxt, "integrity", -1);

                     stat = asn1PD_H225_SeqOfH225IntegrityMechanism (pctxt, &pvalue->integrity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrity", -1);
                     break;

                  case 6:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 7:
                     pvalue->m.supportsAltGKPresent = 1;

                     invokeStartElement (pctxt, "supportsAltGK", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "supportsAltGK", -1);
                     break;

                  case 8:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 9:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatekeeperConfirm                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatekeeperConfirm (OOCTXT* pctxt, H225GatekeeperConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperIdentifierPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode gatekeeperIdentifier */

   if (pvalue->m.gatekeeperIdentifierPresent) {
      invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

      stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
   }

   /* decode rasAddress */

   invokeStartElement (pctxt, "rasAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->rasAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rasAddress", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 9 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.alternateGatekeeperPresent = 1;

                     invokeStartElement (pctxt, "alternateGatekeeper", -1);

                     stat = asn1PD_H225_SeqOfH225AlternateGK (pctxt, &pvalue->alternateGatekeeper);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateGatekeeper", -1);
                     break;

                  case 1:
                     pvalue->m.authenticationModePresent = 1;

                     invokeStartElement (pctxt, "authenticationMode", -1);

                     stat = asn1PD_H235AuthenticationMechanism (pctxt, &pvalue->authenticationMode);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "authenticationMode", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.algorithmOIDPresent = 1;

                     invokeStartElement (pctxt, "algorithmOID", -1);

                     stat = decodeObjectIdentifier (pctxt, &pvalue->algorithmOID);
                     if (stat != ASN_OK) return stat;
                     invokeOidValue (pctxt, pvalue->algorithmOID.numids, pvalue->algorithmOID.subid);

                     invokeEndElement (pctxt, "algorithmOID", -1);
                     break;

                  case 5:
                     pvalue->m.integrityPresent = 1;

                     invokeStartElement (pctxt, "integrity", -1);

                     stat = asn1PD_H225_SeqOfH225IntegrityMechanism (pctxt, &pvalue->integrity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrity", -1);
                     break;

                  case 6:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 7:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 8:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatekeeperRejectReason                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatekeeperRejectReason (OOCTXT* pctxt, H225GatekeeperRejectReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* resourceUnavailable */
         case 0:
            invokeStartElement (pctxt, "resourceUnavailable", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "resourceUnavailable", -1);

            break;

         /* terminalExcluded */
         case 1:
            invokeStartElement (pctxt, "terminalExcluded", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "terminalExcluded", -1);

            break;

         /* invalidRevision */
         case 2:
            invokeStartElement (pctxt, "invalidRevision", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidRevision", -1);

            break;

         /* undefinedReason */
         case 3:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* securityDenial */
         case 5:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* genericDataReason */
         case 6:
            invokeStartElement (pctxt, "genericDataReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "genericDataReason", -1);

            break;

         /* neededFeatureNotSupported */
         case 7:
            invokeStartElement (pctxt, "neededFeatureNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "neededFeatureNotSupported", -1);

            break;

         /* securityError */
         case 8:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors);

            stat = asn1PD_H225SecurityErrors (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  GatekeeperReject                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225GatekeeperReject (OOCTXT* pctxt, H225GatekeeperReject* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperIdentifierPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode gatekeeperIdentifier */

   if (pvalue->m.gatekeeperIdentifierPresent) {
      invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

      stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
   }

   /* decode rejectReason */

   invokeStartElement (pctxt, "rejectReason", -1);

   stat = asn1PD_H225GatekeeperRejectReason (pctxt, &pvalue->rejectReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rejectReason", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 6 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.altGKInfoPresent = 1;

                     invokeStartElement (pctxt, "altGKInfo", -1);

                     stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "altGKInfo", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 4:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 5:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225AddressPattern                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225AddressPattern (OOCTXT* pctxt, H225_SeqOfH225AddressPattern* pvalue)
{
   int stat = ASN_OK;
   H225AddressPattern* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225AddressPattern);

         stat = asn1PD_H225AddressPattern (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225H248PackagesDescriptor                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225H248PackagesDescriptor (OOCTXT* pctxt, H225_SeqOfH225H248PackagesDescriptor* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, H225H248PackagesDescriptor);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = asn1PD_H225H248PackagesDescriptor (pctxt, &pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RegistrationRequest                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RegistrationRequest (OOCTXT* pctxt, H225RegistrationRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.terminalAliasPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperIdentifierPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode discoveryComplete */

   invokeStartElement (pctxt, "discoveryComplete", -1);

   stat = DECODEBIT (pctxt, &pvalue->discoveryComplete);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->discoveryComplete);

   invokeEndElement (pctxt, "discoveryComplete", -1);

   /* decode callSignalAddress */

   invokeStartElement (pctxt, "callSignalAddress", -1);

   stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->callSignalAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callSignalAddress", -1);

   /* decode rasAddress */

   invokeStartElement (pctxt, "rasAddress", -1);

   stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->rasAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rasAddress", -1);

   /* decode terminalType */

   invokeStartElement (pctxt, "terminalType", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->terminalType);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "terminalType", -1);

   /* decode terminalAlias */

   if (pvalue->m.terminalAliasPresent) {
      invokeStartElement (pctxt, "terminalAlias", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->terminalAlias);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "terminalAlias", -1);
   }

   /* decode gatekeeperIdentifier */

   if (pvalue->m.gatekeeperIdentifierPresent) {
      invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

      stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
   }

   /* decode endpointVendor */

   invokeStartElement (pctxt, "endpointVendor", -1);

   stat = asn1PD_H225VendorIdentifier (pctxt, &pvalue->endpointVendor);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointVendor", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 23 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.alternateEndpointsPresent = 1;

                     invokeStartElement (pctxt, "alternateEndpoints", -1);

                     stat = asn1PD_H225_SeqOfH225Endpoint (pctxt, &pvalue->alternateEndpoints);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateEndpoints", -1);
                     break;

                  case 1:
                     pvalue->m.timeToLivePresent = 1;

                     invokeStartElement (pctxt, "timeToLive", -1);

                     stat = asn1PD_H225TimeToLive (pctxt, &pvalue->timeToLive);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "timeToLive", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 5:
                     pvalue->m.keepAlivePresent = 1;

                     invokeStartElement (pctxt, "keepAlive", -1);

                     stat = DECODEBIT (pctxt, &pvalue->keepAlive);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->keepAlive);

                     invokeEndElement (pctxt, "keepAlive", -1);
                     break;

                  case 6:
                     pvalue->m.endpointIdentifierPresent = 1;

                     invokeStartElement (pctxt, "endpointIdentifier", -1);

                     stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "endpointIdentifier", -1);
                     break;

                  case 7:
                     pvalue->m.willSupplyUUIEsPresent = 1;

                     invokeStartElement (pctxt, "willSupplyUUIEs", -1);

                     stat = DECODEBIT (pctxt, &pvalue->willSupplyUUIEs);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->willSupplyUUIEs);

                     invokeEndElement (pctxt, "willSupplyUUIEs", -1);
                     break;

                  case 8:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 9:
                     pvalue->m.alternateTransportAddressesPresent = 1;

                     invokeStartElement (pctxt, "alternateTransportAddresses", -1);

                     stat = asn1PD_H225AlternateTransportAddresses (pctxt, &pvalue->alternateTransportAddresses);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateTransportAddresses", -1);
                     break;

                  case 10:
                     pvalue->m.additiveRegistrationPresent = 1;

                     invokeStartElement (pctxt, "additiveRegistration", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "additiveRegistration", -1);
                     break;

                  case 11:
                     pvalue->m.terminalAliasPatternPresent = 1;

                     invokeStartElement (pctxt, "terminalAliasPattern", -1);

                     stat = asn1PD_H225_SeqOfH225AddressPattern (pctxt, &pvalue->terminalAliasPattern);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "terminalAliasPattern", -1);
                     break;

                  case 12:
                     pvalue->m.supportsAltGKPresent = 1;

                     invokeStartElement (pctxt, "supportsAltGK", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "supportsAltGK", -1);
                     break;

                  case 13:
                     pvalue->m.usageReportingCapabilityPresent = 1;

                     invokeStartElement (pctxt, "usageReportingCapability", -1);

                     stat = asn1PD_H225RasUsageInfoTypes (pctxt, &pvalue->usageReportingCapability);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageReportingCapability", -1);
                     break;

                  case 14:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 15:
                     pvalue->m.supportedH248PackagesPresent = 1;

                     invokeStartElement (pctxt, "supportedH248Packages", -1);

                     stat = asn1PD_H225_SeqOfH225H248PackagesDescriptor (pctxt, &pvalue->supportedH248Packages);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedH248Packages", -1);
                     break;

                  case 16:
                     pvalue->m.callCreditCapabilityPresent = 1;

                     invokeStartElement (pctxt, "callCreditCapability", -1);

                     stat = asn1PD_H225CallCreditCapability (pctxt, &pvalue->callCreditCapability);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callCreditCapability", -1);
                     break;

                  case 17:
                     pvalue->m.capacityReportingCapabilityPresent = 1;

                     invokeStartElement (pctxt, "capacityReportingCapability", -1);

                     stat = asn1PD_H225CapacityReportingCapability (pctxt, &pvalue->capacityReportingCapability);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacityReportingCapability", -1);
                     break;

                  case 18:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 19:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 20:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  case 21:
                     pvalue->m.restartPresent = 1;

                     invokeStartElement (pctxt, "restart", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "restart", -1);
                     break;

                  case 22:
                     pvalue->m.supportsACFSequencesPresent = 1;

                     invokeStartElement (pctxt, "supportsACFSequences", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "supportsACFSequences", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RegistrationConfirm_preGrantedARQ                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RegistrationConfirm_preGrantedARQ (OOCTXT* pctxt, H225RegistrationConfirm_preGrantedARQ* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   /* decode makeCall */

   invokeStartElement (pctxt, "makeCall", -1);

   stat = DECODEBIT (pctxt, &pvalue->makeCall);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->makeCall);

   invokeEndElement (pctxt, "makeCall", -1);

   /* decode useGKCallSignalAddressToMakeCall */

   invokeStartElement (pctxt, "useGKCallSignalAddressToMakeCall", -1);

   stat = DECODEBIT (pctxt, &pvalue->useGKCallSignalAddressToMakeCall);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->useGKCallSignalAddressToMakeCall);

   invokeEndElement (pctxt, "useGKCallSignalAddressToMakeCall", -1);

   /* decode answerCall */

   invokeStartElement (pctxt, "answerCall", -1);

   stat = DECODEBIT (pctxt, &pvalue->answerCall);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->answerCall);

   invokeEndElement (pctxt, "answerCall", -1);

   /* decode useGKCallSignalAddressToAnswer */

   invokeStartElement (pctxt, "useGKCallSignalAddressToAnswer", -1);

   stat = DECODEBIT (pctxt, &pvalue->useGKCallSignalAddressToAnswer);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->useGKCallSignalAddressToAnswer);

   invokeEndElement (pctxt, "useGKCallSignalAddressToAnswer", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 4 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.irrFrequencyInCallPresent = 1;

                     invokeStartElement (pctxt, "irrFrequencyInCall", -1);

                     stat = decodeConsUInt16 (pctxt, &pvalue->irrFrequencyInCall, 1U, 65535U);
                     if (stat != ASN_OK) return stat;
                     invokeUIntValue (pctxt, pvalue->irrFrequencyInCall);

                     invokeEndElement (pctxt, "irrFrequencyInCall", -1);
                     break;

                  case 1:
                     pvalue->m.totalBandwidthRestrictionPresent = 1;

                     invokeStartElement (pctxt, "totalBandwidthRestriction", -1);

                     stat = asn1PD_H225BandWidth (pctxt, &pvalue->totalBandwidthRestriction);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "totalBandwidthRestriction", -1);
                     break;

                  case 2:
                     pvalue->m.alternateTransportAddressesPresent = 1;

                     invokeStartElement (pctxt, "alternateTransportAddresses", -1);

                     stat = asn1PD_H225AlternateTransportAddresses (pctxt, &pvalue->alternateTransportAddresses);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateTransportAddresses", -1);
                     break;

                  case 3:
                     pvalue->m.useSpecifiedTransportPresent = 1;

                     invokeStartElement (pctxt, "useSpecifiedTransport", -1);

                     stat = asn1PD_H225UseSpecifiedTransport (pctxt, &pvalue->useSpecifiedTransport);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "useSpecifiedTransport", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225RasUsageSpecification                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225RasUsageSpecification (OOCTXT* pctxt, H225_SeqOfH225RasUsageSpecification* pvalue)
{
   int stat = ASN_OK;
   H225RasUsageSpecification* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225RasUsageSpecification);

         stat = asn1PD_H225RasUsageSpecification (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RegistrationConfirm                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RegistrationConfirm (OOCTXT* pctxt, H225RegistrationConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.terminalAliasPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperIdentifierPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode callSignalAddress */

   invokeStartElement (pctxt, "callSignalAddress", -1);

   stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->callSignalAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callSignalAddress", -1);

   /* decode terminalAlias */

   if (pvalue->m.terminalAliasPresent) {
      invokeStartElement (pctxt, "terminalAlias", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->terminalAlias);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "terminalAlias", -1);
   }

   /* decode gatekeeperIdentifier */

   if (pvalue->m.gatekeeperIdentifierPresent) {
      invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

      stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
   }

   /* decode endpointIdentifier */

   invokeStartElement (pctxt, "endpointIdentifier", -1);

   stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointIdentifier", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 17 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.alternateGatekeeperPresent = 1;

                     invokeStartElement (pctxt, "alternateGatekeeper", -1);

                     stat = asn1PD_H225_SeqOfH225AlternateGK (pctxt, &pvalue->alternateGatekeeper);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateGatekeeper", -1);
                     break;

                  case 1:
                     pvalue->m.timeToLivePresent = 1;

                     invokeStartElement (pctxt, "timeToLive", -1);

                     stat = asn1PD_H225TimeToLive (pctxt, &pvalue->timeToLive);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "timeToLive", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 5:
                     pvalue->m.willRespondToIRRPresent = 1;

                     invokeStartElement (pctxt, "willRespondToIRR", -1);

                     stat = DECODEBIT (pctxt, &pvalue->willRespondToIRR);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->willRespondToIRR);

                     invokeEndElement (pctxt, "willRespondToIRR", -1);
                     break;

                  case 6:
                     pvalue->m.preGrantedARQPresent = 1;

                     invokeStartElement (pctxt, "preGrantedARQ", -1);

                     stat = asn1PD_H225RegistrationConfirm_preGrantedARQ (pctxt, &pvalue->preGrantedARQ);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "preGrantedARQ", -1);
                     break;

                  case 7:
                     pvalue->m.maintainConnectionPresent = 1;

                     invokeStartElement (pctxt, "maintainConnection", -1);

                     stat = DECODEBIT (pctxt, &pvalue->maintainConnection);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->maintainConnection);

                     invokeEndElement (pctxt, "maintainConnection", -1);
                     break;

                  case 8:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 9:
                     pvalue->m.supportsAdditiveRegistrationPresent = 1;

                     invokeStartElement (pctxt, "supportsAdditiveRegistration", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "supportsAdditiveRegistration", -1);
                     break;

                  case 10:
                     pvalue->m.terminalAliasPatternPresent = 1;

                     invokeStartElement (pctxt, "terminalAliasPattern", -1);

                     stat = asn1PD_H225_SeqOfH225AddressPattern (pctxt, &pvalue->terminalAliasPattern);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "terminalAliasPattern", -1);
                     break;

                  case 11:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  case 12:
                     pvalue->m.usageSpecPresent = 1;

                     invokeStartElement (pctxt, "usageSpec", -1);

                     stat = asn1PD_H225_SeqOfH225RasUsageSpecification (pctxt, &pvalue->usageSpec);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageSpec", -1);
                     break;

                  case 13:
                     pvalue->m.featureServerAliasPresent = 1;

                     invokeStartElement (pctxt, "featureServerAlias", -1);

                     stat = asn1PD_H225AliasAddress (pctxt, &pvalue->featureServerAlias);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureServerAlias", -1);
                     break;

                  case 14:
                     pvalue->m.capacityReportingSpecPresent = 1;

                     invokeStartElement (pctxt, "capacityReportingSpec", -1);

                     stat = asn1PD_H225CapacityReportingSpecification (pctxt, &pvalue->capacityReportingSpec);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacityReportingSpec", -1);
                     break;

                  case 15:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 16:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RegistrationRejectReason_invalidTerminalAliases           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RegistrationRejectReason_invalidTerminalAliases (OOCTXT* pctxt, H225RegistrationRejectReason_invalidTerminalAliases* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.terminalAliasPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.terminalAliasPatternPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.supportedPrefixesPresent = optbit;

   /* decode terminalAlias */

   if (pvalue->m.terminalAliasPresent) {
      invokeStartElement (pctxt, "terminalAlias", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->terminalAlias);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "terminalAlias", -1);
   }

   /* decode terminalAliasPattern */

   if (pvalue->m.terminalAliasPatternPresent) {
      invokeStartElement (pctxt, "terminalAliasPattern", -1);

      stat = asn1PD_H225_SeqOfH225AddressPattern (pctxt, &pvalue->terminalAliasPattern);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "terminalAliasPattern", -1);
   }

   /* decode supportedPrefixes */

   if (pvalue->m.supportedPrefixesPresent) {
      invokeStartElement (pctxt, "supportedPrefixes", -1);

      stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "supportedPrefixes", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RegistrationRejectReason                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RegistrationRejectReason (OOCTXT* pctxt, H225RegistrationRejectReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 7);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* discoveryRequired */
         case 0:
            invokeStartElement (pctxt, "discoveryRequired", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "discoveryRequired", -1);

            break;

         /* invalidRevision */
         case 1:
            invokeStartElement (pctxt, "invalidRevision", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidRevision", -1);

            break;

         /* invalidCallSignalAddress */
         case 2:
            invokeStartElement (pctxt, "invalidCallSignalAddress", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidCallSignalAddress", -1);

            break;

         /* invalidRASAddress */
         case 3:
            invokeStartElement (pctxt, "invalidRASAddress", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidRASAddress", -1);

            break;

         /* duplicateAlias */
         case 4:
            invokeStartElement (pctxt, "duplicateAlias", -1);

            pvalue->u.duplicateAlias = ALLOC_ASN1ELEM (pctxt, H225_SeqOfH225AliasAddress);

            stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, pvalue->u.duplicateAlias);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "duplicateAlias", -1);

            break;

         /* invalidTerminalType */
         case 5:
            invokeStartElement (pctxt, "invalidTerminalType", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidTerminalType", -1);

            break;

         /* undefinedReason */
         case 6:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         /* transportNotSupported */
         case 7:
            invokeStartElement (pctxt, "transportNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "transportNotSupported", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 9;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* transportQOSNotSupported */
         case 9:
            invokeStartElement (pctxt, "transportQOSNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "transportQOSNotSupported", -1);

            break;

         /* resourceUnavailable */
         case 10:
            invokeStartElement (pctxt, "resourceUnavailable", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "resourceUnavailable", -1);

            break;

         /* invalidAlias */
         case 11:
            invokeStartElement (pctxt, "invalidAlias", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidAlias", -1);

            break;

         /* securityDenial */
         case 12:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* fullRegistrationRequired */
         case 13:
            invokeStartElement (pctxt, "fullRegistrationRequired", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "fullRegistrationRequired", -1);

            break;

         /* additiveRegistrationNotSupported */
         case 14:
            invokeStartElement (pctxt, "additiveRegistrationNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "additiveRegistrationNotSupported", -1);

            break;

         /* invalidTerminalAliases */
         case 15:
            invokeStartElement (pctxt, "invalidTerminalAliases", -1);

            pvalue->u.invalidTerminalAliases = ALLOC_ASN1ELEM (pctxt, H225RegistrationRejectReason_invalidTerminalAliases);

            stat = asn1PD_H225RegistrationRejectReason_invalidTerminalAliases (pctxt, pvalue->u.invalidTerminalAliases);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "invalidTerminalAliases", -1);

            break;

         /* genericDataReason */
         case 16:
            invokeStartElement (pctxt, "genericDataReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "genericDataReason", -1);

            break;

         /* neededFeatureNotSupported */
         case 17:
            invokeStartElement (pctxt, "neededFeatureNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "neededFeatureNotSupported", -1);

            break;

         /* securityError */
         case 18:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors);

            stat = asn1PD_H225SecurityErrors (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RegistrationReject                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RegistrationReject (OOCTXT* pctxt, H225RegistrationReject* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.gatekeeperIdentifierPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode rejectReason */

   invokeStartElement (pctxt, "rejectReason", -1);

   stat = asn1PD_H225RegistrationRejectReason (pctxt, &pvalue->rejectReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rejectReason", -1);

   /* decode gatekeeperIdentifier */

   if (pvalue->m.gatekeeperIdentifierPresent) {
      invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

      stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 6 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.altGKInfoPresent = 1;

                     invokeStartElement (pctxt, "altGKInfo", -1);

                     stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "altGKInfo", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 4:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 5:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UnregRequestReason                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UnregRequestReason (OOCTXT* pctxt, H225UnregRequestReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* reregistrationRequired */
         case 0:
            invokeStartElement (pctxt, "reregistrationRequired", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "reregistrationRequired", -1);

            break;

         /* ttlExpired */
         case 1:
            invokeStartElement (pctxt, "ttlExpired", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "ttlExpired", -1);

            break;

         /* securityDenial */
         case 2:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* undefinedReason */
         case 3:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* maintenance */
         case 5:
            invokeStartElement (pctxt, "maintenance", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "maintenance", -1);

            break;

         /* securityError */
         case 6:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors2);

            stat = asn1PD_H225SecurityErrors2 (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UnregistrationRequest                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UnregistrationRequest (OOCTXT* pctxt, H225UnregistrationRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endpointAliasPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endpointIdentifierPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode callSignalAddress */

   invokeStartElement (pctxt, "callSignalAddress", -1);

   stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->callSignalAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callSignalAddress", -1);

   /* decode endpointAlias */

   if (pvalue->m.endpointAliasPresent) {
      invokeStartElement (pctxt, "endpointAlias", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->endpointAlias);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endpointAlias", -1);
   }

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode endpointIdentifier */

   if (pvalue->m.endpointIdentifierPresent) {
      invokeStartElement (pctxt, "endpointIdentifier", -1);

      stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endpointIdentifier", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 10 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.alternateEndpointsPresent = 1;

                     invokeStartElement (pctxt, "alternateEndpoints", -1);

                     stat = asn1PD_H225_SeqOfH225Endpoint (pctxt, &pvalue->alternateEndpoints);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateEndpoints", -1);
                     break;

                  case 1:
                     pvalue->m.gatekeeperIdentifierPresent = 1;

                     invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

                     stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 5:
                     pvalue->m.reasonPresent = 1;

                     invokeStartElement (pctxt, "reason", -1);

                     stat = asn1PD_H225UnregRequestReason (pctxt, &pvalue->reason);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "reason", -1);
                     break;

                  case 6:
                     pvalue->m.endpointAliasPatternPresent = 1;

                     invokeStartElement (pctxt, "endpointAliasPattern", -1);

                     stat = asn1PD_H225_SeqOfH225AddressPattern (pctxt, &pvalue->endpointAliasPattern);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "endpointAliasPattern", -1);
                     break;

                  case 7:
                     pvalue->m.supportedPrefixesPresent = 1;

                     invokeStartElement (pctxt, "supportedPrefixes", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedPrefix (pctxt, &pvalue->supportedPrefixes);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedPrefixes", -1);
                     break;

                  case 8:
                     pvalue->m.alternateGatekeeperPresent = 1;

                     invokeStartElement (pctxt, "alternateGatekeeper", -1);

                     stat = asn1PD_H225_SeqOfH225AlternateGK (pctxt, &pvalue->alternateGatekeeper);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateGatekeeper", -1);
                     break;

                  case 9:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UnregistrationConfirm                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UnregistrationConfirm (OOCTXT* pctxt, H225UnregistrationConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 4 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 1:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 2:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 3:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UnregRejectReason                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UnregRejectReason (OOCTXT* pctxt, H225UnregRejectReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* notCurrentlyRegistered */
         case 0:
            invokeStartElement (pctxt, "notCurrentlyRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "notCurrentlyRegistered", -1);

            break;

         /* callInProgress */
         case 1:
            invokeStartElement (pctxt, "callInProgress", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "callInProgress", -1);

            break;

         /* undefinedReason */
         case 2:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* permissionDenied */
         case 4:
            invokeStartElement (pctxt, "permissionDenied", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "permissionDenied", -1);

            break;

         /* securityDenial */
         case 5:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* securityError */
         case 6:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors2);

            stat = asn1PD_H225SecurityErrors2 (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UnregistrationReject                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UnregistrationReject (OOCTXT* pctxt, H225UnregistrationReject* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode rejectReason */

   invokeStartElement (pctxt, "rejectReason", -1);

   stat = asn1PD_H225UnregRejectReason (pctxt, &pvalue->rejectReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rejectReason", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 5 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.altGKInfoPresent = 1;

                     invokeStartElement (pctxt, "altGKInfo", -1);

                     stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "altGKInfo", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 4:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  CallModel                                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225CallModel (OOCTXT* pctxt, H225CallModel* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* direct */
         case 0:
            invokeStartElement (pctxt, "direct", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "direct", -1);

            break;

         /* gatekeeperRouted */
         case 1:
            invokeStartElement (pctxt, "gatekeeperRouted", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "gatekeeperRouted", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  TransportQOS                                              */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225TransportQOS (OOCTXT* pctxt, H225TransportQOS* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* endpointControlled */
         case 0:
            invokeStartElement (pctxt, "endpointControlled", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "endpointControlled", -1);

            break;

         /* gatekeeperControlled */
         case 1:
            invokeStartElement (pctxt, "gatekeeperControlled", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "gatekeeperControlled", -1);

            break;

         /* noControl */
         case 2:
            invokeStartElement (pctxt, "noControl", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "noControl", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AdmissionRequest                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AdmissionRequest (OOCTXT* pctxt, H225AdmissionRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callModelPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destinationInfoPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destCallSignalAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.destExtraCallInfoPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.srcCallSignalAddressPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callServicesPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode callType */

   invokeStartElement (pctxt, "callType", -1);

   stat = asn1PD_H225CallType (pctxt, &pvalue->callType);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callType", -1);

   /* decode callModel */

   if (pvalue->m.callModelPresent) {
      invokeStartElement (pctxt, "callModel", -1);

      stat = asn1PD_H225CallModel (pctxt, &pvalue->callModel);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callModel", -1);
   }

   /* decode endpointIdentifier */

   invokeStartElement (pctxt, "endpointIdentifier", -1);

   stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointIdentifier", -1);

   /* decode destinationInfo */

   if (pvalue->m.destinationInfoPresent) {
      invokeStartElement (pctxt, "destinationInfo", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destinationInfo);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destinationInfo", -1);
   }

   /* decode destCallSignalAddress */

   if (pvalue->m.destCallSignalAddressPresent) {
      invokeStartElement (pctxt, "destCallSignalAddress", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->destCallSignalAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destCallSignalAddress", -1);
   }

   /* decode destExtraCallInfo */

   if (pvalue->m.destExtraCallInfoPresent) {
      invokeStartElement (pctxt, "destExtraCallInfo", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destExtraCallInfo);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "destExtraCallInfo", -1);
   }

   /* decode srcInfo */

   invokeStartElement (pctxt, "srcInfo", -1);

   stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->srcInfo);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "srcInfo", -1);

   /* decode srcCallSignalAddress */

   if (pvalue->m.srcCallSignalAddressPresent) {
      invokeStartElement (pctxt, "srcCallSignalAddress", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->srcCallSignalAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "srcCallSignalAddress", -1);
   }

   /* decode bandWidth */

   invokeStartElement (pctxt, "bandWidth", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandWidth);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "bandWidth", -1);

   /* decode callReferenceValue */

   invokeStartElement (pctxt, "callReferenceValue", -1);

   stat = asn1PD_H225CallReferenceValue (pctxt, &pvalue->callReferenceValue);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callReferenceValue", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode callServices */

   if (pvalue->m.callServicesPresent) {
      invokeStartElement (pctxt, "callServices", -1);

      stat = asn1PD_H225QseriesOptions (pctxt, &pvalue->callServices);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callServices", -1);
   }

   /* decode conferenceID */

   invokeStartElement (pctxt, "conferenceID", -1);

   stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceID", -1);

   /* decode activeMC */

   invokeStartElement (pctxt, "activeMC", -1);

   stat = DECODEBIT (pctxt, &pvalue->activeMC);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->activeMC);

   invokeEndElement (pctxt, "activeMC", -1);

   /* decode answerCall */

   invokeStartElement (pctxt, "answerCall", -1);

   stat = DECODEBIT (pctxt, &pvalue->answerCall);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->answerCall);

   invokeEndElement (pctxt, "answerCall", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 19 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.canMapAliasPresent = 1;

                     invokeStartElement (pctxt, "canMapAlias", -1);

                     stat = DECODEBIT (pctxt, &pvalue->canMapAlias);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->canMapAlias);

                     invokeEndElement (pctxt, "canMapAlias", -1);
                     break;

                  case 1:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 2:
                     pvalue->m.srcAlternativesPresent = 1;

                     invokeStartElement (pctxt, "srcAlternatives", -1);

                     stat = asn1PD_H225_SeqOfH225Endpoint (pctxt, &pvalue->srcAlternatives);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "srcAlternatives", -1);
                     break;

                  case 3:
                     pvalue->m.destAlternativesPresent = 1;

                     invokeStartElement (pctxt, "destAlternatives", -1);

                     stat = asn1PD_H225_SeqOfH225Endpoint (pctxt, &pvalue->destAlternatives);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destAlternatives", -1);
                     break;

                  case 4:
                     pvalue->m.gatekeeperIdentifierPresent = 1;

                     invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

                     stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
                     break;

                  case 5:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 6:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 7:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 8:
                     pvalue->m.transportQOSPresent = 1;

                     invokeStartElement (pctxt, "transportQOS", -1);

                     stat = asn1PD_H225TransportQOS (pctxt, &pvalue->transportQOS);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "transportQOS", -1);
                     break;

                  case 9:
                     pvalue->m.willSupplyUUIEsPresent = 1;

                     invokeStartElement (pctxt, "willSupplyUUIEs", -1);

                     stat = DECODEBIT (pctxt, &pvalue->willSupplyUUIEs);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->willSupplyUUIEs);

                     invokeEndElement (pctxt, "willSupplyUUIEs", -1);
                     break;

                  case 10:
                     pvalue->m.callLinkagePresent = 1;

                     invokeStartElement (pctxt, "callLinkage", -1);

                     stat = asn1PD_H225CallLinkage (pctxt, &pvalue->callLinkage);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callLinkage", -1);
                     break;

                  case 11:
                     pvalue->m.gatewayDataRatePresent = 1;

                     invokeStartElement (pctxt, "gatewayDataRate", -1);

                     stat = asn1PD_H225DataRate (pctxt, &pvalue->gatewayDataRate);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "gatewayDataRate", -1);
                     break;

                  case 12:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 13:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 14:
                     pvalue->m.desiredProtocolsPresent = 1;

                     invokeStartElement (pctxt, "desiredProtocols", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->desiredProtocols);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "desiredProtocols", -1);
                     break;

                  case 15:
                     pvalue->m.desiredTunnelledProtocolPresent = 1;

                     invokeStartElement (pctxt, "desiredTunnelledProtocol", -1);

                     stat = asn1PD_H225TunnelledProtocol (pctxt, &pvalue->desiredTunnelledProtocol);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "desiredTunnelledProtocol", -1);
                     break;

                  case 16:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 17:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  case 18:
                     pvalue->m.canMapSrcAliasPresent = 1;

                     invokeStartElement (pctxt, "canMapSrcAlias", -1);

                     stat = DECODEBIT (pctxt, &pvalue->canMapSrcAlias);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->canMapSrcAlias);

                     invokeEndElement (pctxt, "canMapSrcAlias", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UUIEsRequested                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UUIEsRequested (OOCTXT* pctxt, H225UUIEsRequested* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   /* decode setup */

   invokeStartElement (pctxt, "setup", -1);

   stat = DECODEBIT (pctxt, &pvalue->setup);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->setup);

   invokeEndElement (pctxt, "setup", -1);

   /* decode callProceeding */

   invokeStartElement (pctxt, "callProceeding", -1);

   stat = DECODEBIT (pctxt, &pvalue->callProceeding);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->callProceeding);

   invokeEndElement (pctxt, "callProceeding", -1);

   /* decode connect */

   invokeStartElement (pctxt, "connect", -1);

   stat = DECODEBIT (pctxt, &pvalue->connect);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->connect);

   invokeEndElement (pctxt, "connect", -1);

   /* decode alerting */

   invokeStartElement (pctxt, "alerting", -1);

   stat = DECODEBIT (pctxt, &pvalue->alerting);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->alerting);

   invokeEndElement (pctxt, "alerting", -1);

   /* decode information */

   invokeStartElement (pctxt, "information", -1);

   stat = DECODEBIT (pctxt, &pvalue->information);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->information);

   invokeEndElement (pctxt, "information", -1);

   /* decode releaseComplete */

   invokeStartElement (pctxt, "releaseComplete", -1);

   stat = DECODEBIT (pctxt, &pvalue->releaseComplete);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->releaseComplete);

   invokeEndElement (pctxt, "releaseComplete", -1);

   /* decode facility */

   invokeStartElement (pctxt, "facility", -1);

   stat = DECODEBIT (pctxt, &pvalue->facility);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->facility);

   invokeEndElement (pctxt, "facility", -1);

   /* decode progress */

   invokeStartElement (pctxt, "progress", -1);

   stat = DECODEBIT (pctxt, &pvalue->progress);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->progress);

   invokeEndElement (pctxt, "progress", -1);

   /* decode empty */

   invokeStartElement (pctxt, "empty", -1);

   stat = DECODEBIT (pctxt, &pvalue->empty);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->empty);

   invokeEndElement (pctxt, "empty", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 4 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.statusPresent = 1;

                     invokeStartElement (pctxt, "status", -1);

                     stat = DECODEBIT (pctxt, &pvalue->status);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->status);

                     invokeEndElement (pctxt, "status", -1);
                     break;

                  case 1:
                     pvalue->m.statusInquiryPresent = 1;

                     invokeStartElement (pctxt, "statusInquiry", -1);

                     stat = DECODEBIT (pctxt, &pvalue->statusInquiry);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->statusInquiry);

                     invokeEndElement (pctxt, "statusInquiry", -1);
                     break;

                  case 2:
                     pvalue->m.setupAcknowledgePresent = 1;

                     invokeStartElement (pctxt, "setupAcknowledge", -1);

                     stat = DECODEBIT (pctxt, &pvalue->setupAcknowledge);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->setupAcknowledge);

                     invokeEndElement (pctxt, "setupAcknowledge", -1);
                     break;

                  case 3:
                     pvalue->m.notifyPresent = 1;

                     invokeStartElement (pctxt, "notify", -1);

                     stat = DECODEBIT (pctxt, &pvalue->notify);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->notify);

                     invokeEndElement (pctxt, "notify", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AdmissionConfirm_language                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AdmissionConfirm_language (OOCTXT* pctxt, H225AdmissionConfirm_language* pvalue)
{
   static Asn1SizeCnst element_lsize1 = { 0, 1, 32, 0 };
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, ASN1IA5String);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      addSizeConstraint (pctxt, &element_lsize1);

      stat = decodeConstrainedStringEx (pctxt, &pvalue->elem[xx1], 0, 8, 7, 7);
      if (stat != ASN_OK) return stat;
      invokeCharStrValue (pctxt, pvalue->elem[xx1]);
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AdmissionConfirm                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AdmissionConfirm (OOCTXT* pctxt, H225AdmissionConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.irrFrequencyPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode bandWidth */

   invokeStartElement (pctxt, "bandWidth", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandWidth);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "bandWidth", -1);

   /* decode callModel */

   invokeStartElement (pctxt, "callModel", -1);

   stat = asn1PD_H225CallModel (pctxt, &pvalue->callModel);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callModel", -1);

   /* decode destCallSignalAddress */

   invokeStartElement (pctxt, "destCallSignalAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->destCallSignalAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "destCallSignalAddress", -1);

   /* decode irrFrequency */

   if (pvalue->m.irrFrequencyPresent) {
      invokeStartElement (pctxt, "irrFrequency", -1);

      stat = decodeConsUInt16 (pctxt, &pvalue->irrFrequency, 1U, 65535U);
      if (stat != ASN_OK) return stat;
      invokeUIntValue (pctxt, pvalue->irrFrequency);

      invokeEndElement (pctxt, "irrFrequency", -1);
   }

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 22 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.destinationInfoPresent = 1;

                     invokeStartElement (pctxt, "destinationInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destinationInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destinationInfo", -1);
                     break;

                  case 1:
                     pvalue->m.destExtraCallInfoPresent = 1;

                     invokeStartElement (pctxt, "destExtraCallInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destExtraCallInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destExtraCallInfo", -1);
                     break;

                  case 2:
                     pvalue->m.destinationTypePresent = 1;

                     invokeStartElement (pctxt, "destinationType", -1);

                     stat = asn1PD_H225EndpointType (pctxt, &pvalue->destinationType);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destinationType", -1);
                     break;

                  case 3:
                     pvalue->m.remoteExtensionAddressPresent = 1;

                     invokeStartElement (pctxt, "remoteExtensionAddress", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->remoteExtensionAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "remoteExtensionAddress", -1);
                     break;

                  case 4:
                     pvalue->m.alternateEndpointsPresent = 1;

                     invokeStartElement (pctxt, "alternateEndpoints", -1);

                     stat = asn1PD_H225_SeqOfH225Endpoint (pctxt, &pvalue->alternateEndpoints);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateEndpoints", -1);
                     break;

                  case 5:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 6:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 7:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 8:
                     pvalue->m.transportQOSPresent = 1;

                     invokeStartElement (pctxt, "transportQOS", -1);

                     stat = asn1PD_H225TransportQOS (pctxt, &pvalue->transportQOS);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "transportQOS", -1);
                     break;

                  case 9:
                     pvalue->m.willRespondToIRRPresent = 1;

                     invokeStartElement (pctxt, "willRespondToIRR", -1);

                     stat = DECODEBIT (pctxt, &pvalue->willRespondToIRR);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->willRespondToIRR);

                     invokeEndElement (pctxt, "willRespondToIRR", -1);
                     break;

                  case 10:
                     pvalue->m.uuiesRequestedPresent = 1;

                     invokeStartElement (pctxt, "uuiesRequested", -1);

                     stat = asn1PD_H225UUIEsRequested (pctxt, &pvalue->uuiesRequested);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "uuiesRequested", -1);
                     break;

                  case 11:
                     pvalue->m.languagePresent = 1;

                     invokeStartElement (pctxt, "language", -1);

                     stat = asn1PD_H225AdmissionConfirm_language (pctxt, &pvalue->language);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "language", -1);
                     break;

                  case 12:
                     pvalue->m.alternateTransportAddressesPresent = 1;

                     invokeStartElement (pctxt, "alternateTransportAddresses", -1);

                     stat = asn1PD_H225AlternateTransportAddresses (pctxt, &pvalue->alternateTransportAddresses);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateTransportAddresses", -1);
                     break;

                  case 13:
                     pvalue->m.useSpecifiedTransportPresent = 1;

                     invokeStartElement (pctxt, "useSpecifiedTransport", -1);

                     stat = asn1PD_H225UseSpecifiedTransport (pctxt, &pvalue->useSpecifiedTransport);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "useSpecifiedTransport", -1);
                     break;

                  case 14:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 15:
                     pvalue->m.usageSpecPresent = 1;

                     invokeStartElement (pctxt, "usageSpec", -1);

                     stat = asn1PD_H225_SeqOfH225RasUsageSpecification (pctxt, &pvalue->usageSpec);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageSpec", -1);
                     break;

                  case 16:
                     pvalue->m.supportedProtocolsPresent = 1;

                     invokeStartElement (pctxt, "supportedProtocols", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->supportedProtocols);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedProtocols", -1);
                     break;

                  case 17:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 18:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 19:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 20:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  case 21:
                     pvalue->m.modifiedSrcInfoPresent = 1;

                     invokeStartElement (pctxt, "modifiedSrcInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->modifiedSrcInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "modifiedSrcInfo", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225PartyNumber                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225PartyNumber (OOCTXT* pctxt, H225_SeqOfH225PartyNumber* pvalue)
{
   int stat = ASN_OK;
   H225PartyNumber* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225PartyNumber);

         stat = asn1PD_H225PartyNumber (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AdmissionRejectReason                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AdmissionRejectReason (OOCTXT* pctxt, H225AdmissionRejectReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 7);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* calledPartyNotRegistered */
         case 0:
            invokeStartElement (pctxt, "calledPartyNotRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "calledPartyNotRegistered", -1);

            break;

         /* invalidPermission */
         case 1:
            invokeStartElement (pctxt, "invalidPermission", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidPermission", -1);

            break;

         /* requestDenied */
         case 2:
            invokeStartElement (pctxt, "requestDenied", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "requestDenied", -1);

            break;

         /* undefinedReason */
         case 3:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         /* callerNotRegistered */
         case 4:
            invokeStartElement (pctxt, "callerNotRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "callerNotRegistered", -1);

            break;

         /* routeCallToGatekeeper */
         case 5:
            invokeStartElement (pctxt, "routeCallToGatekeeper", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "routeCallToGatekeeper", -1);

            break;

         /* invalidEndpointIdentifier */
         case 6:
            invokeStartElement (pctxt, "invalidEndpointIdentifier", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidEndpointIdentifier", -1);

            break;

         /* resourceUnavailable */
         case 7:
            invokeStartElement (pctxt, "resourceUnavailable", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "resourceUnavailable", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 9;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* securityDenial */
         case 9:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* qosControlNotSupported */
         case 10:
            invokeStartElement (pctxt, "qosControlNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "qosControlNotSupported", -1);

            break;

         /* incompleteAddress */
         case 11:
            invokeStartElement (pctxt, "incompleteAddress", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "incompleteAddress", -1);

            break;

         /* aliasesInconsistent */
         case 12:
            invokeStartElement (pctxt, "aliasesInconsistent", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "aliasesInconsistent", -1);

            break;

         /* routeCallToSCN */
         case 13:
            invokeStartElement (pctxt, "routeCallToSCN", -1);

            pvalue->u.routeCallToSCN = ALLOC_ASN1ELEM (pctxt, H225_SeqOfH225PartyNumber);

            stat = asn1PD_H225_SeqOfH225PartyNumber (pctxt, pvalue->u.routeCallToSCN);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "routeCallToSCN", -1);

            break;

         /* exceedsCallCapacity */
         case 14:
            invokeStartElement (pctxt, "exceedsCallCapacity", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "exceedsCallCapacity", -1);

            break;

         /* collectDestination */
         case 15:
            invokeStartElement (pctxt, "collectDestination", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "collectDestination", -1);

            break;

         /* collectPIN */
         case 16:
            invokeStartElement (pctxt, "collectPIN", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "collectPIN", -1);

            break;

         /* genericDataReason */
         case 17:
            invokeStartElement (pctxt, "genericDataReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "genericDataReason", -1);

            break;

         /* neededFeatureNotSupported */
         case 18:
            invokeStartElement (pctxt, "neededFeatureNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "neededFeatureNotSupported", -1);

            break;

         /* securityErrors */
         case 19:
            invokeStartElement (pctxt, "securityErrors", -1);

            pvalue->u.securityErrors = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors2);

            stat = asn1PD_H225SecurityErrors2 (pctxt, pvalue->u.securityErrors);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityErrors", -1);

            break;

         /* securityDHmismatch */
         case 20:
            invokeStartElement (pctxt, "securityDHmismatch", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDHmismatch", -1);

            break;

         /* noRouteToDestination */
         case 21:
            invokeStartElement (pctxt, "noRouteToDestination", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "noRouteToDestination", -1);

            break;

         /* unallocatedNumber */
         case 22:
            invokeStartElement (pctxt, "unallocatedNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "unallocatedNumber", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  AdmissionReject                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225AdmissionReject (OOCTXT* pctxt, H225AdmissionReject* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode rejectReason */

   invokeStartElement (pctxt, "rejectReason", -1);

   stat = asn1PD_H225AdmissionRejectReason (pctxt, &pvalue->rejectReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rejectReason", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 8 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.altGKInfoPresent = 1;

                     invokeStartElement (pctxt, "altGKInfo", -1);

                     stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "altGKInfo", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.callSignalAddressPresent = 1;

                     invokeStartElement (pctxt, "callSignalAddress", -1);

                     stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->callSignalAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callSignalAddress", -1);
                     break;

                  case 4:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 5:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 6:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 7:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225BandwidthDetails                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225BandwidthDetails (OOCTXT* pctxt, H225_SeqOfH225BandwidthDetails* pvalue)
{
   int stat = ASN_OK;
   H225BandwidthDetails* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225BandwidthDetails);

         stat = asn1PD_H225BandwidthDetails (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  BandwidthRequest                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225BandwidthRequest (OOCTXT* pctxt, H225BandwidthRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callTypePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode endpointIdentifier */

   invokeStartElement (pctxt, "endpointIdentifier", -1);

   stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointIdentifier", -1);

   /* decode conferenceID */

   invokeStartElement (pctxt, "conferenceID", -1);

   stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceID", -1);

   /* decode callReferenceValue */

   invokeStartElement (pctxt, "callReferenceValue", -1);

   stat = asn1PD_H225CallReferenceValue (pctxt, &pvalue->callReferenceValue);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callReferenceValue", -1);

   /* decode callType */

   if (pvalue->m.callTypePresent) {
      invokeStartElement (pctxt, "callType", -1);

      stat = asn1PD_H225CallType (pctxt, &pvalue->callType);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callType", -1);
   }

   /* decode bandWidth */

   invokeStartElement (pctxt, "bandWidth", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandWidth);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "bandWidth", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 11 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.gatekeeperIdentifierPresent = 1;

                     invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

                     stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 5:
                     pvalue->m.answeredCallPresent = 1;

                     invokeStartElement (pctxt, "answeredCall", -1);

                     stat = DECODEBIT (pctxt, &pvalue->answeredCall);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->answeredCall);

                     invokeEndElement (pctxt, "answeredCall", -1);
                     break;

                  case 6:
                     pvalue->m.callLinkagePresent = 1;

                     invokeStartElement (pctxt, "callLinkage", -1);

                     stat = asn1PD_H225CallLinkage (pctxt, &pvalue->callLinkage);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callLinkage", -1);
                     break;

                  case 7:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 8:
                     pvalue->m.usageInformationPresent = 1;

                     invokeStartElement (pctxt, "usageInformation", -1);

                     stat = asn1PD_H225RasUsageInformation (pctxt, &pvalue->usageInformation);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageInformation", -1);
                     break;

                  case 9:
                     pvalue->m.bandwidthDetailsPresent = 1;

                     invokeStartElement (pctxt, "bandwidthDetails", -1);

                     stat = asn1PD_H225_SeqOfH225BandwidthDetails (pctxt, &pvalue->bandwidthDetails);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "bandwidthDetails", -1);
                     break;

                  case 10:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  BandwidthConfirm                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225BandwidthConfirm (OOCTXT* pctxt, H225BandwidthConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode bandWidth */

   invokeStartElement (pctxt, "bandWidth", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandWidth);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "bandWidth", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 5 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 1:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 2:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 3:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 4:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  BandRejectReason                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225BandRejectReason (OOCTXT* pctxt, H225BandRejectReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 5);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* notBound */
         case 0:
            invokeStartElement (pctxt, "notBound", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "notBound", -1);

            break;

         /* invalidConferenceID */
         case 1:
            invokeStartElement (pctxt, "invalidConferenceID", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidConferenceID", -1);

            break;

         /* invalidPermission */
         case 2:
            invokeStartElement (pctxt, "invalidPermission", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidPermission", -1);

            break;

         /* insufficientResources */
         case 3:
            invokeStartElement (pctxt, "insufficientResources", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "insufficientResources", -1);

            break;

         /* invalidRevision */
         case 4:
            invokeStartElement (pctxt, "invalidRevision", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidRevision", -1);

            break;

         /* undefinedReason */
         case 5:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 7;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* securityDenial */
         case 7:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* securityError */
         case 8:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors2);

            stat = asn1PD_H225SecurityErrors2 (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  BandwidthReject                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225BandwidthReject (OOCTXT* pctxt, H225BandwidthReject* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode rejectReason */

   invokeStartElement (pctxt, "rejectReason", -1);

   stat = asn1PD_H225BandRejectReason (pctxt, &pvalue->rejectReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rejectReason", -1);

   /* decode allowedBandWidth */

   invokeStartElement (pctxt, "allowedBandWidth", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->allowedBandWidth);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "allowedBandWidth", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 5 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.altGKInfoPresent = 1;

                     invokeStartElement (pctxt, "altGKInfo", -1);

                     stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "altGKInfo", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 4:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  DisengageReason                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225DisengageReason (OOCTXT* pctxt, H225DisengageReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* forcedDrop */
         case 0:
            invokeStartElement (pctxt, "forcedDrop", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "forcedDrop", -1);

            break;

         /* normalDrop */
         case 1:
            invokeStartElement (pctxt, "normalDrop", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "normalDrop", -1);

            break;

         /* undefinedReason */
         case 2:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  DisengageRequest                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225DisengageRequest (OOCTXT* pctxt, H225DisengageRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode endpointIdentifier */

   invokeStartElement (pctxt, "endpointIdentifier", -1);

   stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointIdentifier", -1);

   /* decode conferenceID */

   invokeStartElement (pctxt, "conferenceID", -1);

   stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceID", -1);

   /* decode callReferenceValue */

   invokeStartElement (pctxt, "callReferenceValue", -1);

   stat = asn1PD_H225CallReferenceValue (pctxt, &pvalue->callReferenceValue);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callReferenceValue", -1);

   /* decode disengageReason */

   invokeStartElement (pctxt, "disengageReason", -1);

   stat = asn1PD_H225DisengageReason (pctxt, &pvalue->disengageReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "disengageReason", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 13 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.gatekeeperIdentifierPresent = 1;

                     invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

                     stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
                     break;

                  case 2:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 3:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 4:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 5:
                     pvalue->m.answeredCallPresent = 1;

                     invokeStartElement (pctxt, "answeredCall", -1);

                     stat = DECODEBIT (pctxt, &pvalue->answeredCall);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->answeredCall);

                     invokeEndElement (pctxt, "answeredCall", -1);
                     break;

                  case 6:
                     pvalue->m.callLinkagePresent = 1;

                     invokeStartElement (pctxt, "callLinkage", -1);

                     stat = asn1PD_H225CallLinkage (pctxt, &pvalue->callLinkage);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callLinkage", -1);
                     break;

                  case 7:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 8:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 9:
                     pvalue->m.usageInformationPresent = 1;

                     invokeStartElement (pctxt, "usageInformation", -1);

                     stat = asn1PD_H225RasUsageInformation (pctxt, &pvalue->usageInformation);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageInformation", -1);
                     break;

                  case 10:
                     pvalue->m.terminationCausePresent = 1;

                     invokeStartElement (pctxt, "terminationCause", -1);

                     stat = asn1PD_H225CallTerminationCause (pctxt, &pvalue->terminationCause);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "terminationCause", -1);
                     break;

                  case 11:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 12:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  DisengageConfirm                                          */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225DisengageConfirm (OOCTXT* pctxt, H225DisengageConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 7 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 1:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 2:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 3:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 4:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 5:
                     pvalue->m.usageInformationPresent = 1;

                     invokeStartElement (pctxt, "usageInformation", -1);

                     stat = asn1PD_H225RasUsageInformation (pctxt, &pvalue->usageInformation);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageInformation", -1);
                     break;

                  case 6:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  DisengageRejectReason                                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225DisengageRejectReason (OOCTXT* pctxt, H225DisengageRejectReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 1);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* notRegistered */
         case 0:
            invokeStartElement (pctxt, "notRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "notRegistered", -1);

            break;

         /* requestToDropOther */
         case 1:
            invokeStartElement (pctxt, "requestToDropOther", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "requestToDropOther", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 3;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* securityDenial */
         case 3:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* securityError */
         case 4:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors2);

            stat = asn1PD_H225SecurityErrors2 (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  DisengageReject                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225DisengageReject (OOCTXT* pctxt, H225DisengageReject* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode rejectReason */

   invokeStartElement (pctxt, "rejectReason", -1);

   stat = asn1PD_H225DisengageRejectReason (pctxt, &pvalue->rejectReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rejectReason", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 5 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.altGKInfoPresent = 1;

                     invokeStartElement (pctxt, "altGKInfo", -1);

                     stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "altGKInfo", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 4:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  LocationRequest                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225LocationRequest (OOCTXT* pctxt, H225LocationRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endpointIdentifierPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode endpointIdentifier */

   if (pvalue->m.endpointIdentifierPresent) {
      invokeStartElement (pctxt, "endpointIdentifier", -1);

      stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endpointIdentifier", -1);
   }

   /* decode destinationInfo */

   invokeStartElement (pctxt, "destinationInfo", -1);

   stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destinationInfo);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "destinationInfo", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode replyAddress */

   invokeStartElement (pctxt, "replyAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->replyAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "replyAddress", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 16 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.sourceInfoPresent = 1;

                     invokeStartElement (pctxt, "sourceInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->sourceInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "sourceInfo", -1);
                     break;

                  case 1:
                     pvalue->m.canMapAliasPresent = 1;

                     invokeStartElement (pctxt, "canMapAlias", -1);

                     stat = DECODEBIT (pctxt, &pvalue->canMapAlias);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->canMapAlias);

                     invokeEndElement (pctxt, "canMapAlias", -1);
                     break;

                  case 2:
                     pvalue->m.gatekeeperIdentifierPresent = 1;

                     invokeStartElement (pctxt, "gatekeeperIdentifier", -1);

                     stat = asn1PD_H225GatekeeperIdentifier (pctxt, &pvalue->gatekeeperIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "gatekeeperIdentifier", -1);
                     break;

                  case 3:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 4:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 5:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 6:
                     pvalue->m.desiredProtocolsPresent = 1;

                     invokeStartElement (pctxt, "desiredProtocols", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->desiredProtocols);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "desiredProtocols", -1);
                     break;

                  case 7:
                     pvalue->m.desiredTunnelledProtocolPresent = 1;

                     invokeStartElement (pctxt, "desiredTunnelledProtocol", -1);

                     stat = asn1PD_H225TunnelledProtocol (pctxt, &pvalue->desiredTunnelledProtocol);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "desiredTunnelledProtocol", -1);
                     break;

                  case 8:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 9:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  case 10:
                     pvalue->m.hopCountPresent = 1;

                     invokeStartElement (pctxt, "hopCount", -1);

                     stat = decodeConsUInt8 (pctxt, &pvalue->hopCount, 1U, 255U);
                     if (stat != ASN_OK) return stat;
                     invokeUIntValue (pctxt, pvalue->hopCount);

                     invokeEndElement (pctxt, "hopCount", -1);
                     break;

                  case 11:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 12:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 13:
                     pvalue->m.bandWidthPresent = 1;

                     invokeStartElement (pctxt, "bandWidth", -1);

                     stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandWidth);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "bandWidth", -1);
                     break;

                  case 14:
                     pvalue->m.sourceEndpointInfoPresent = 1;

                     invokeStartElement (pctxt, "sourceEndpointInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->sourceEndpointInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "sourceEndpointInfo", -1);
                     break;

                  case 15:
                     pvalue->m.canMapSrcAliasPresent = 1;

                     invokeStartElement (pctxt, "canMapSrcAlias", -1);

                     stat = DECODEBIT (pctxt, &pvalue->canMapSrcAlias);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->canMapSrcAlias);

                     invokeEndElement (pctxt, "canMapSrcAlias", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  LocationConfirm                                           */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225LocationConfirm (OOCTXT* pctxt, H225LocationConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode callSignalAddress */

   invokeStartElement (pctxt, "callSignalAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->callSignalAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callSignalAddress", -1);

   /* decode rasAddress */

   invokeStartElement (pctxt, "rasAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->rasAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rasAddress", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 17 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.destinationInfoPresent = 1;

                     invokeStartElement (pctxt, "destinationInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destinationInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destinationInfo", -1);
                     break;

                  case 1:
                     pvalue->m.destExtraCallInfoPresent = 1;

                     invokeStartElement (pctxt, "destExtraCallInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->destExtraCallInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destExtraCallInfo", -1);
                     break;

                  case 2:
                     pvalue->m.destinationTypePresent = 1;

                     invokeStartElement (pctxt, "destinationType", -1);

                     stat = asn1PD_H225EndpointType (pctxt, &pvalue->destinationType);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "destinationType", -1);
                     break;

                  case 3:
                     pvalue->m.remoteExtensionAddressPresent = 1;

                     invokeStartElement (pctxt, "remoteExtensionAddress", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->remoteExtensionAddress);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "remoteExtensionAddress", -1);
                     break;

                  case 4:
                     pvalue->m.alternateEndpointsPresent = 1;

                     invokeStartElement (pctxt, "alternateEndpoints", -1);

                     stat = asn1PD_H225_SeqOfH225Endpoint (pctxt, &pvalue->alternateEndpoints);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateEndpoints", -1);
                     break;

                  case 5:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 6:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 7:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 8:
                     pvalue->m.alternateTransportAddressesPresent = 1;

                     invokeStartElement (pctxt, "alternateTransportAddresses", -1);

                     stat = asn1PD_H225AlternateTransportAddresses (pctxt, &pvalue->alternateTransportAddresses);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "alternateTransportAddresses", -1);
                     break;

                  case 9:
                     pvalue->m.supportedProtocolsPresent = 1;

                     invokeStartElement (pctxt, "supportedProtocols", -1);

                     stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->supportedProtocols);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "supportedProtocols", -1);
                     break;

                  case 10:
                     pvalue->m.multipleCallsPresent = 1;

                     invokeStartElement (pctxt, "multipleCalls", -1);

                     stat = DECODEBIT (pctxt, &pvalue->multipleCalls);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->multipleCalls);

                     invokeEndElement (pctxt, "multipleCalls", -1);
                     break;

                  case 11:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 12:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  case 13:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  case 14:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  case 15:
                     pvalue->m.modifiedSrcInfoPresent = 1;

                     invokeStartElement (pctxt, "modifiedSrcInfo", -1);

                     stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->modifiedSrcInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "modifiedSrcInfo", -1);
                     break;

                  case 16:
                     pvalue->m.bandWidthPresent = 1;

                     invokeStartElement (pctxt, "bandWidth", -1);

                     stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandWidth);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "bandWidth", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  LocationRejectReason                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225LocationRejectReason (OOCTXT* pctxt, H225LocationRejectReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* notRegistered */
         case 0:
            invokeStartElement (pctxt, "notRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "notRegistered", -1);

            break;

         /* invalidPermission */
         case 1:
            invokeStartElement (pctxt, "invalidPermission", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidPermission", -1);

            break;

         /* requestDenied */
         case 2:
            invokeStartElement (pctxt, "requestDenied", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "requestDenied", -1);

            break;

         /* undefinedReason */
         case 3:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* securityDenial */
         case 5:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* aliasesInconsistent */
         case 6:
            invokeStartElement (pctxt, "aliasesInconsistent", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "aliasesInconsistent", -1);

            break;

         /* routeCalltoSCN */
         case 7:
            invokeStartElement (pctxt, "routeCalltoSCN", -1);

            pvalue->u.routeCalltoSCN = ALLOC_ASN1ELEM (pctxt, H225_SeqOfH225PartyNumber);

            stat = asn1PD_H225_SeqOfH225PartyNumber (pctxt, pvalue->u.routeCalltoSCN);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "routeCalltoSCN", -1);

            break;

         /* resourceUnavailable */
         case 8:
            invokeStartElement (pctxt, "resourceUnavailable", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "resourceUnavailable", -1);

            break;

         /* genericDataReason */
         case 9:
            invokeStartElement (pctxt, "genericDataReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "genericDataReason", -1);

            break;

         /* neededFeatureNotSupported */
         case 10:
            invokeStartElement (pctxt, "neededFeatureNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "neededFeatureNotSupported", -1);

            break;

         /* hopCountExceeded */
         case 11:
            invokeStartElement (pctxt, "hopCountExceeded", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "hopCountExceeded", -1);

            break;

         /* incompleteAddress */
         case 12:
            invokeStartElement (pctxt, "incompleteAddress", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "incompleteAddress", -1);

            break;

         /* securityError */
         case 13:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors2);

            stat = asn1PD_H225SecurityErrors2 (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         /* securityDHmismatch */
         case 14:
            invokeStartElement (pctxt, "securityDHmismatch", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDHmismatch", -1);

            break;

         /* noRouteToDestination */
         case 15:
            invokeStartElement (pctxt, "noRouteToDestination", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "noRouteToDestination", -1);

            break;

         /* unallocatedNumber */
         case 16:
            invokeStartElement (pctxt, "unallocatedNumber", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "unallocatedNumber", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  LocationReject                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225LocationReject (OOCTXT* pctxt, H225LocationReject* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode rejectReason */

   invokeStartElement (pctxt, "rejectReason", -1);

   stat = asn1PD_H225LocationRejectReason (pctxt, &pvalue->rejectReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rejectReason", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 7 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.altGKInfoPresent = 1;

                     invokeStartElement (pctxt, "altGKInfo", -1);

                     stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "altGKInfo", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 4:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 5:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  case 6:
                     pvalue->m.serviceControlPresent = 1;

                     invokeStartElement (pctxt, "serviceControl", -1);

                     stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "serviceControl", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequest                                               */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequest (OOCTXT* pctxt, H225InfoRequest* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.replyAddressPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode callReferenceValue */

   invokeStartElement (pctxt, "callReferenceValue", -1);

   stat = asn1PD_H225CallReferenceValue (pctxt, &pvalue->callReferenceValue);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callReferenceValue", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode replyAddress */

   if (pvalue->m.replyAddressPresent) {
      invokeStartElement (pctxt, "replyAddress", -1);

      stat = asn1PD_H225TransportAddress (pctxt, &pvalue->replyAddress);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "replyAddress", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 11 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 4:
                     pvalue->m.uuiesRequestedPresent = 1;

                     invokeStartElement (pctxt, "uuiesRequested", -1);

                     stat = asn1PD_H225UUIEsRequested (pctxt, &pvalue->uuiesRequested);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "uuiesRequested", -1);
                     break;

                  case 5:
                     pvalue->m.callLinkagePresent = 1;

                     invokeStartElement (pctxt, "callLinkage", -1);

                     stat = asn1PD_H225CallLinkage (pctxt, &pvalue->callLinkage);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callLinkage", -1);
                     break;

                  case 6:
                     pvalue->m.usageInfoRequestedPresent = 1;

                     invokeStartElement (pctxt, "usageInfoRequested", -1);

                     stat = asn1PD_H225RasUsageInfoTypes (pctxt, &pvalue->usageInfoRequested);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageInfoRequested", -1);
                     break;

                  case 7:
                     pvalue->m.segmentedResponseSupportedPresent = 1;

                     invokeStartElement (pctxt, "segmentedResponseSupported", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "segmentedResponseSupported", -1);
                     break;

                  case 8:
                     pvalue->m.nextSegmentRequestedPresent = 1;

                     invokeStartElement (pctxt, "nextSegmentRequested", -1);

                     stat = decodeConsUInt16 (pctxt, &pvalue->nextSegmentRequested, 0U, 65535U);
                     if (stat != ASN_OK) return stat;
                     invokeUIntValue (pctxt, pvalue->nextSegmentRequested);

                     invokeEndElement (pctxt, "nextSegmentRequested", -1);
                     break;

                  case 9:
                     pvalue->m.capacityInfoRequestedPresent = 1;

                     invokeStartElement (pctxt, "capacityInfoRequested", -1);

                     /* NULL */
                     invokeNullValue (pctxt);

                     invokeEndElement (pctxt, "capacityInfoRequested", -1);
                     break;

                  case 10:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225RTPSession                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225RTPSession (OOCTXT* pctxt, H225_SeqOfH225RTPSession* pvalue)
{
   int stat = ASN_OK;
   H225RTPSession* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225RTPSession);

         stat = asn1PD_H225RTPSession (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225TransportChannelInfo                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225TransportChannelInfo (OOCTXT* pctxt, H225_SeqOfH225TransportChannelInfo* pvalue)
{
   int stat = ASN_OK;
   H225TransportChannelInfo* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225TransportChannelInfo);

         stat = asn1PD_H225TransportChannelInfo (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225ConferenceIdentifier                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225ConferenceIdentifier (OOCTXT* pctxt, H225_SeqOfH225ConferenceIdentifier* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT xx1;

   /* decode length determinant */

   stat = decodeLength (pctxt, &pvalue->n);
   if (stat != ASN_OK) return stat;

   /* decode elements */

   ALLOC_ASN1ARRAY (pctxt, pvalue, H225ConferenceIdentifier);

   for (xx1 = 0; xx1 < pvalue->n; xx1++) {
      invokeStartElement (pctxt, "elem", xx1);

      stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->elem[xx1]);
      if (stat != ASN_OK) return stat;
      invokeEndElement (pctxt, "elem", xx1);

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequestResponse_perCallInfo_element_pdu_element       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequestResponse_perCallInfo_element_pdu_element (OOCTXT* pctxt, H225InfoRequestResponse_perCallInfo_element_pdu_element* pvalue)
{
   int stat = ASN_OK;

   /* decode h323pdu */

   invokeStartElement (pctxt, "h323pdu", -1);

   stat = asn1PD_H225H323_UU_PDU (pctxt, &pvalue->h323pdu);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "h323pdu", -1);

   /* decode sent */

   invokeStartElement (pctxt, "sent", -1);

   stat = DECODEBIT (pctxt, &pvalue->sent);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->sent);

   invokeEndElement (pctxt, "sent", -1);

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225InfoRequestResponse_perCallInfo_element_pdu_el  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225InfoRequestResponse_perCallInfo_element_pdu_element (OOCTXT* pctxt, H225_SeqOfH225InfoRequestResponse_perCallInfo_element_pdu_element* pvalue)
{
   int stat = ASN_OK;
   H225InfoRequestResponse_perCallInfo_element_pdu_element* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225InfoRequestResponse_perCallInfo_element_pdu_element);

         stat = asn1PD_H225InfoRequestResponse_perCallInfo_element_pdu_element (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequestResponse_perCallInfo_element                   */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequestResponse_perCallInfo_element (OOCTXT* pctxt, H225InfoRequestResponse_perCallInfo_element* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.originatorPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.audioPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.videoPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.dataPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode callReferenceValue */

   invokeStartElement (pctxt, "callReferenceValue", -1);

   stat = asn1PD_H225CallReferenceValue (pctxt, &pvalue->callReferenceValue);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callReferenceValue", -1);

   /* decode conferenceID */

   invokeStartElement (pctxt, "conferenceID", -1);

   stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceID", -1);

   /* decode originator */

   if (pvalue->m.originatorPresent) {
      invokeStartElement (pctxt, "originator", -1);

      stat = DECODEBIT (pctxt, &pvalue->originator);
      if (stat != ASN_OK) return stat;
      invokeBoolValue (pctxt, pvalue->originator);

      invokeEndElement (pctxt, "originator", -1);
   }

   /* decode audio */

   if (pvalue->m.audioPresent) {
      invokeStartElement (pctxt, "audio", -1);

      stat = asn1PD_H225_SeqOfH225RTPSession (pctxt, &pvalue->audio);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "audio", -1);
   }

   /* decode video */

   if (pvalue->m.videoPresent) {
      invokeStartElement (pctxt, "video", -1);

      stat = asn1PD_H225_SeqOfH225RTPSession (pctxt, &pvalue->video);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "video", -1);
   }

   /* decode data */

   if (pvalue->m.dataPresent) {
      invokeStartElement (pctxt, "data", -1);

      stat = asn1PD_H225_SeqOfH225TransportChannelInfo (pctxt, &pvalue->data);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "data", -1);
   }

   /* decode h245 */

   invokeStartElement (pctxt, "h245", -1);

   stat = asn1PD_H225TransportChannelInfo (pctxt, &pvalue->h245);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "h245", -1);

   /* decode callSignaling */

   invokeStartElement (pctxt, "callSignaling", -1);

   stat = asn1PD_H225TransportChannelInfo (pctxt, &pvalue->callSignaling);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callSignaling", -1);

   /* decode callType */

   invokeStartElement (pctxt, "callType", -1);

   stat = asn1PD_H225CallType (pctxt, &pvalue->callType);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callType", -1);

   /* decode bandWidth */

   invokeStartElement (pctxt, "bandWidth", -1);

   stat = asn1PD_H225BandWidth (pctxt, &pvalue->bandWidth);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "bandWidth", -1);

   /* decode callModel */

   invokeStartElement (pctxt, "callModel", -1);

   stat = asn1PD_H225CallModel (pctxt, &pvalue->callModel);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callModel", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 8 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.callIdentifierPresent = 1;

                     invokeStartElement (pctxt, "callIdentifier", -1);

                     stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callIdentifier", -1);
                     break;

                  case 1:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 2:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 3:
                     pvalue->m.substituteConfIDsPresent = 1;

                     invokeStartElement (pctxt, "substituteConfIDs", -1);

                     stat = asn1PD_H225_SeqOfH225ConferenceIdentifier (pctxt, &pvalue->substituteConfIDs);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "substituteConfIDs", -1);
                     break;

                  case 4:
                     pvalue->m.pduPresent = 1;

                     invokeStartElement (pctxt, "pdu", -1);

                     stat = asn1PD_H225_SeqOfH225InfoRequestResponse_perCallInfo_element_pdu_element (pctxt, &pvalue->pdu);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "pdu", -1);
                     break;

                  case 5:
                     pvalue->m.callLinkagePresent = 1;

                     invokeStartElement (pctxt, "callLinkage", -1);

                     stat = asn1PD_H225CallLinkage (pctxt, &pvalue->callLinkage);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "callLinkage", -1);
                     break;

                  case 6:
                     pvalue->m.usageInformationPresent = 1;

                     invokeStartElement (pctxt, "usageInformation", -1);

                     stat = asn1PD_H225RasUsageInformation (pctxt, &pvalue->usageInformation);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "usageInformation", -1);
                     break;

                  case 7:
                     pvalue->m.circuitInfoPresent = 1;

                     invokeStartElement (pctxt, "circuitInfo", -1);

                     stat = asn1PD_H225CircuitInfo (pctxt, &pvalue->circuitInfo);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "circuitInfo", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225InfoRequestResponse_perCallInfo_element         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225InfoRequestResponse_perCallInfo_element (OOCTXT* pctxt, H225_SeqOfH225InfoRequestResponse_perCallInfo_element* pvalue)
{
   int stat = ASN_OK;
   H225InfoRequestResponse_perCallInfo_element* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225InfoRequestResponse_perCallInfo_element);

         stat = asn1PD_H225InfoRequestResponse_perCallInfo_element (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequestResponseStatus                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequestResponseStatus (OOCTXT* pctxt, H225InfoRequestResponseStatus* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 3);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* complete */
         case 0:
            invokeStartElement (pctxt, "complete", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "complete", -1);

            break;

         /* incomplete */
         case 1:
            invokeStartElement (pctxt, "incomplete", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "incomplete", -1);

            break;

         /* segment */
         case 2:
            invokeStartElement (pctxt, "segment", -1);

            stat = decodeConsUInt16 (pctxt, &pvalue->u.segment, 0U, 65535U);
            if (stat != ASN_OK) return stat;
            invokeUIntValue (pctxt, pvalue->u.segment);

            invokeEndElement (pctxt, "segment", -1);

            break;

         /* invalidCall */
         case 3:
            invokeStartElement (pctxt, "invalidCall", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "invalidCall", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 5;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequestResponse                                       */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequestResponse (OOCTXT* pctxt, H225InfoRequestResponse* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endpointAliasPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.perCallInfoPresent = optbit;

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode endpointType */

   invokeStartElement (pctxt, "endpointType", -1);

   stat = asn1PD_H225EndpointType (pctxt, &pvalue->endpointType);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointType", -1);

   /* decode endpointIdentifier */

   invokeStartElement (pctxt, "endpointIdentifier", -1);

   stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointIdentifier", -1);

   /* decode rasAddress */

   invokeStartElement (pctxt, "rasAddress", -1);

   stat = asn1PD_H225TransportAddress (pctxt, &pvalue->rasAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "rasAddress", -1);

   /* decode callSignalAddress */

   invokeStartElement (pctxt, "callSignalAddress", -1);

   stat = asn1PD_H225_SeqOfH225TransportAddress (pctxt, &pvalue->callSignalAddress);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callSignalAddress", -1);

   /* decode endpointAlias */

   if (pvalue->m.endpointAliasPresent) {
      invokeStartElement (pctxt, "endpointAlias", -1);

      stat = asn1PD_H225_SeqOfH225AliasAddress (pctxt, &pvalue->endpointAlias);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endpointAlias", -1);
   }

   /* decode perCallInfo */

   if (pvalue->m.perCallInfoPresent) {
      invokeStartElement (pctxt, "perCallInfo", -1);

      stat = asn1PD_H225_SeqOfH225InfoRequestResponse_perCallInfo_element (pctxt, &pvalue->perCallInfo);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "perCallInfo", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 8 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 1:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 2:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 3:
                     pvalue->m.needResponsePresent = 1;

                     invokeStartElement (pctxt, "needResponse", -1);

                     stat = DECODEBIT (pctxt, &pvalue->needResponse);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->needResponse);

                     invokeEndElement (pctxt, "needResponse", -1);
                     break;

                  case 4:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 5:
                     pvalue->m.irrStatusPresent = 1;

                     invokeStartElement (pctxt, "irrStatus", -1);

                     stat = asn1PD_H225InfoRequestResponseStatus (pctxt, &pvalue->irrStatus);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "irrStatus", -1);
                     break;

                  case 6:
                     pvalue->m.unsolicitedPresent = 1;

                     invokeStartElement (pctxt, "unsolicited", -1);

                     stat = DECODEBIT (pctxt, &pvalue->unsolicited);
                     if (stat != ASN_OK) return stat;
                     invokeBoolValue (pctxt, pvalue->unsolicited);

                     invokeEndElement (pctxt, "unsolicited", -1);
                     break;

                  case 7:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  NonStandardMessage                                        */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225NonStandardMessage (OOCTXT* pctxt, H225NonStandardMessage* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode nonStandardData */

   invokeStartElement (pctxt, "nonStandardData", -1);

   stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "nonStandardData", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 5 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 1:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 2:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 3:
                     pvalue->m.featureSetPresent = 1;

                     invokeStartElement (pctxt, "featureSet", -1);

                     stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "featureSet", -1);
                     break;

                  case 4:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  UnknownMessageResponse                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225UnknownMessageResponse (OOCTXT* pctxt, H225UnknownMessageResponse* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 4 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.tokensPresent = 1;

                     invokeStartElement (pctxt, "tokens", -1);

                     stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "tokens", -1);
                     break;

                  case 1:
                     pvalue->m.cryptoTokensPresent = 1;

                     invokeStartElement (pctxt, "cryptoTokens", -1);

                     stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "cryptoTokens", -1);
                     break;

                  case 2:
                     pvalue->m.integrityCheckValuePresent = 1;

                     invokeStartElement (pctxt, "integrityCheckValue", -1);

                     stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "integrityCheckValue", -1);
                     break;

                  case 3:
                     pvalue->m.messageNotUnderstoodPresent = 1;

                     invokeStartElement (pctxt, "messageNotUnderstood", -1);

                     stat = decodeDynOctetString (pctxt, (ASN1DynOctStr*)&pvalue->messageNotUnderstood);
                     if (stat != ASN_OK) return stat;
                     invokeOctStrValue (pctxt, pvalue->messageNotUnderstood.numocts, pvalue->messageNotUnderstood.data);

                     invokeEndElement (pctxt, "messageNotUnderstood", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RequestInProgress                                         */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RequestInProgress (OOCTXT* pctxt, H225RequestInProgress* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.integrityCheckValuePresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode integrityCheckValue */

   if (pvalue->m.integrityCheckValuePresent) {
      invokeStartElement (pctxt, "integrityCheckValue", -1);

      stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "integrityCheckValue", -1);
   }

   /* decode delay */

   invokeStartElement (pctxt, "delay", -1);

   stat = decodeConsUInt16 (pctxt, &pvalue->delay, 1U, 65535U);
   if (stat != ASN_OK) return stat;
   invokeUIntValue (pctxt, pvalue->delay);

   invokeEndElement (pctxt, "delay", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ResourcesAvailableIndicate                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ResourcesAvailableIndicate (OOCTXT* pctxt, H225ResourcesAvailableIndicate* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.integrityCheckValuePresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode endpointIdentifier */

   invokeStartElement (pctxt, "endpointIdentifier", -1);

   stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "endpointIdentifier", -1);

   /* decode protocols */

   invokeStartElement (pctxt, "protocols", -1);

   stat = asn1PD_H225_SeqOfH225SupportedProtocols (pctxt, &pvalue->protocols);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocols", -1);

   /* decode almostOutOfResources */

   invokeStartElement (pctxt, "almostOutOfResources", -1);

   stat = DECODEBIT (pctxt, &pvalue->almostOutOfResources);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->almostOutOfResources);

   invokeEndElement (pctxt, "almostOutOfResources", -1);

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode integrityCheckValue */

   if (pvalue->m.integrityCheckValuePresent) {
      invokeStartElement (pctxt, "integrityCheckValue", -1);

      stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "integrityCheckValue", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 2 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.capacityPresent = 1;

                     invokeStartElement (pctxt, "capacity", -1);

                     stat = asn1PD_H225CallCapacity (pctxt, &pvalue->capacity);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "capacity", -1);
                     break;

                  case 1:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ResourcesAvailableConfirm                                 */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ResourcesAvailableConfirm (OOCTXT* pctxt, H225ResourcesAvailableConfirm* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   OOCTXT lctxt2;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.integrityCheckValuePresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode protocolIdentifier */

   invokeStartElement (pctxt, "protocolIdentifier", -1);

   stat = asn1PD_H225ProtocolIdentifier (pctxt, &pvalue->protocolIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "protocolIdentifier", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode integrityCheckValue */

   if (pvalue->m.integrityCheckValuePresent) {
      invokeStartElement (pctxt, "integrityCheckValue", -1);

      stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "integrityCheckValue", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            if (i < 1 && openType.numocts > 0) {  /* known element */
               copyContext (&lctxt2, pctxt);
               initContextBuffer (pctxt, openType.data, openType.numocts);

               switch (i) {
                  case 0:
                     pvalue->m.genericDataPresent = 1;

                     invokeStartElement (pctxt, "genericData", -1);

                     stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
                     if (stat != ASN_OK) return stat;

                     invokeEndElement (pctxt, "genericData", -1);
                     break;

                  default:
                     pctxt->buffer.byteIndex += openType.numocts;
               }
               copyContext (pctxt, &lctxt2);
            }
            else {  /* unknown element */
               pctxt->buffer.byteIndex += openType.numocts;
            }
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequestAck                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequestAck (OOCTXT* pctxt, H225InfoRequestAck* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.integrityCheckValuePresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode integrityCheckValue */

   if (pvalue->m.integrityCheckValuePresent) {
      invokeStartElement (pctxt, "integrityCheckValue", -1);

      stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "integrityCheckValue", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequestNakReason                                      */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequestNakReason (OOCTXT* pctxt, H225InfoRequestNakReason* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 2);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* notRegistered */
         case 0:
            invokeStartElement (pctxt, "notRegistered", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "notRegistered", -1);

            break;

         /* securityDenial */
         case 1:
            invokeStartElement (pctxt, "securityDenial", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "securityDenial", -1);

            break;

         /* undefinedReason */
         case 2:
            invokeStartElement (pctxt, "undefinedReason", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "undefinedReason", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 4;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* securityError */
         case 4:
            invokeStartElement (pctxt, "securityError", -1);

            pvalue->u.securityError = ALLOC_ASN1ELEM (pctxt, H225SecurityErrors2);

            stat = asn1PD_H225SecurityErrors2 (pctxt, pvalue->u.securityError);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "securityError", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  InfoRequestNak                                            */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225InfoRequestNak (OOCTXT* pctxt, H225InfoRequestNak* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.altGKInfoPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.integrityCheckValuePresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode nakReason */

   invokeStartElement (pctxt, "nakReason", -1);

   stat = asn1PD_H225InfoRequestNakReason (pctxt, &pvalue->nakReason);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "nakReason", -1);

   /* decode altGKInfo */

   if (pvalue->m.altGKInfoPresent) {
      invokeStartElement (pctxt, "altGKInfo", -1);

      stat = asn1PD_H225AltGKInfo (pctxt, &pvalue->altGKInfo);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "altGKInfo", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode integrityCheckValue */

   if (pvalue->m.integrityCheckValuePresent) {
      invokeStartElement (pctxt, "integrityCheckValue", -1);

      stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "integrityCheckValue", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ServiceControlIndication_callSpecific                     */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ServiceControlIndication_callSpecific (OOCTXT* pctxt, H225ServiceControlIndication_callSpecific* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* decode callIdentifier */

   invokeStartElement (pctxt, "callIdentifier", -1);

   stat = asn1PD_H225CallIdentifier (pctxt, &pvalue->callIdentifier);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "callIdentifier", -1);

   /* decode conferenceID */

   invokeStartElement (pctxt, "conferenceID", -1);

   stat = asn1PD_H225ConferenceIdentifier (pctxt, &pvalue->conferenceID);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "conferenceID", -1);

   /* decode answeredCall */

   invokeStartElement (pctxt, "answeredCall", -1);

   stat = DECODEBIT (pctxt, &pvalue->answeredCall);
   if (stat != ASN_OK) return stat;
   invokeBoolValue (pctxt, pvalue->answeredCall);

   invokeEndElement (pctxt, "answeredCall", -1);

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ServiceControlIndication                                  */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ServiceControlIndication (OOCTXT* pctxt, H225ServiceControlIndication* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.endpointIdentifierPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.callSpecificPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.integrityCheckValuePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.featureSetPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.genericDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode serviceControl */

   invokeStartElement (pctxt, "serviceControl", -1);

   stat = asn1PD_H225_SeqOfH225ServiceControlSession (pctxt, &pvalue->serviceControl);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "serviceControl", -1);

   /* decode endpointIdentifier */

   if (pvalue->m.endpointIdentifierPresent) {
      invokeStartElement (pctxt, "endpointIdentifier", -1);

      stat = asn1PD_H225EndpointIdentifier (pctxt, &pvalue->endpointIdentifier);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "endpointIdentifier", -1);
   }

   /* decode callSpecific */

   if (pvalue->m.callSpecificPresent) {
      invokeStartElement (pctxt, "callSpecific", -1);

      stat = asn1PD_H225ServiceControlIndication_callSpecific (pctxt, &pvalue->callSpecific);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "callSpecific", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode integrityCheckValue */

   if (pvalue->m.integrityCheckValuePresent) {
      invokeStartElement (pctxt, "integrityCheckValue", -1);

      stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "integrityCheckValue", -1);
   }

   /* decode featureSet */

   if (pvalue->m.featureSetPresent) {
      invokeStartElement (pctxt, "featureSet", -1);

      stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "featureSet", -1);
   }

   /* decode genericData */

   if (pvalue->m.genericDataPresent) {
      invokeStartElement (pctxt, "genericData", -1);

      stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "genericData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ServiceControlResponse_result                             */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ServiceControlResponse_result (OOCTXT* pctxt, H225ServiceControlResponse_result* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 4);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* started */
         case 0:
            invokeStartElement (pctxt, "started", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "started", -1);

            break;

         /* failed */
         case 1:
            invokeStartElement (pctxt, "failed", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "failed", -1);

            break;

         /* stopped */
         case 2:
            invokeStartElement (pctxt, "stopped", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "stopped", -1);

            break;

         /* notAvailable */
         case 3:
            invokeStartElement (pctxt, "notAvailable", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "notAvailable", -1);

            break;

         /* neededFeatureNotSupported */
         case 4:
            invokeStartElement (pctxt, "neededFeatureNotSupported", -1);

            /* NULL */
            invokeNullValue (pctxt);

            invokeEndElement (pctxt, "neededFeatureNotSupported", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 6;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  ServiceControlResponse                                    */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225ServiceControlResponse (OOCTXT* pctxt, H225ServiceControlResponse* pvalue)
{
   int stat = ASN_OK;
   OOCTXT lctxt;
   ASN1OpenType openType;
   ASN1UINT bitcnt;
   ASN1UINT i;
   ASN1BOOL optbit;
   ASN1BOOL extbit;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   /* optional bits */

   memset (&pvalue->m, 0, sizeof(pvalue->m));

   DECODEBIT (pctxt, &optbit);
   pvalue->m.resultPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.nonStandardDataPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.tokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.cryptoTokensPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.integrityCheckValuePresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.featureSetPresent = optbit;

   DECODEBIT (pctxt, &optbit);
   pvalue->m.genericDataPresent = optbit;

   /* decode requestSeqNum */

   invokeStartElement (pctxt, "requestSeqNum", -1);

   stat = asn1PD_H225RequestSeqNum (pctxt, &pvalue->requestSeqNum);
   if (stat != ASN_OK) return stat;

   invokeEndElement (pctxt, "requestSeqNum", -1);

   /* decode result */

   if (pvalue->m.resultPresent) {
      invokeStartElement (pctxt, "result", -1);

      stat = asn1PD_H225ServiceControlResponse_result (pctxt, &pvalue->result);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "result", -1);
   }

   /* decode nonStandardData */

   if (pvalue->m.nonStandardDataPresent) {
      invokeStartElement (pctxt, "nonStandardData", -1);

      stat = asn1PD_H225NonStandardParameter (pctxt, &pvalue->nonStandardData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "nonStandardData", -1);
   }

   /* decode tokens */

   if (pvalue->m.tokensPresent) {
      invokeStartElement (pctxt, "tokens", -1);

      stat = asn1PD_H225_SeqOfH225ClearToken (pctxt, &pvalue->tokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "tokens", -1);
   }

   /* decode cryptoTokens */

   if (pvalue->m.cryptoTokensPresent) {
      invokeStartElement (pctxt, "cryptoTokens", -1);

      stat = asn1PD_H225_SeqOfH225CryptoH323Token (pctxt, &pvalue->cryptoTokens);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "cryptoTokens", -1);
   }

   /* decode integrityCheckValue */

   if (pvalue->m.integrityCheckValuePresent) {
      invokeStartElement (pctxt, "integrityCheckValue", -1);

      stat = asn1PD_H225ICV (pctxt, &pvalue->integrityCheckValue);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "integrityCheckValue", -1);
   }

   /* decode featureSet */

   if (pvalue->m.featureSetPresent) {
      invokeStartElement (pctxt, "featureSet", -1);

      stat = asn1PD_H225FeatureSet (pctxt, &pvalue->featureSet);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "featureSet", -1);
   }

   /* decode genericData */

   if (pvalue->m.genericDataPresent) {
      invokeStartElement (pctxt, "genericData", -1);

      stat = asn1PD_H225_SeqOfH225GenericData (pctxt, &pvalue->genericData);
      if (stat != ASN_OK) return stat;

      invokeEndElement (pctxt, "genericData", -1);
   }

   if (extbit) {

      /* decode extension optional bits length */

      stat = decodeSmallNonNegWholeNumber (pctxt, &bitcnt);
      if (stat != ASN_OK) return stat;

      bitcnt += 1;

      ZEROCONTEXT (&lctxt);
      stat = setPERBufferUsingCtxt (&lctxt, pctxt);
      if (stat != ASN_OK) return stat;

      stat = moveBitCursor (pctxt, bitcnt);
      if (stat != ASN_OK) return stat;

      for (i = 0; i < bitcnt; i++) {
         DECODEBIT (&lctxt, &optbit);

         if (optbit) {
            stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
            if (stat != ASN_OK) return stat;

            pctxt->buffer.byteIndex += openType.numocts;
         }
      }
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  _SeqOfH225AdmissionConfirm                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225_SeqOfH225AdmissionConfirm (OOCTXT* pctxt, H225_SeqOfH225AdmissionConfirm* pvalue)
{
   int stat = ASN_OK;
   H225AdmissionConfirm* pdata;
   ASN1UINT count = 0;
   ASN1UINT xx1;
   int lstat;

   dListInit (pvalue);

   for (;;) {
      /* decode length determinant */

      lstat = decodeLength (pctxt, &count);
      if (lstat != ASN_OK && lstat != ASN_OK_FRAG) {
         return lstat;
      }

      /* decode elements */

      for (xx1 = 0; xx1 < count; xx1++) {
         invokeStartElement (pctxt, "elem", xx1);

         pdata = ALLOC_ASN1ELEMDNODE (pctxt, H225AdmissionConfirm);

         stat = asn1PD_H225AdmissionConfirm (pctxt, pdata);
         if (stat != ASN_OK) return stat;
         invokeEndElement (pctxt, "elem", xx1);

         dListAppendNode (pctxt, pvalue, pdata);
      }

      if(lstat == ASN_OK) break;
   }

   return (stat);
}

/**************************************************************/
/*                                                            */
/*  RasMessage                                                */
/*                                                            */
/**************************************************************/

EXTERN int asn1PD_H225RasMessage (OOCTXT* pctxt, H225RasMessage* pvalue)
{
   int stat = ASN_OK;
   ASN1UINT ui;
   ASN1OpenType openType;
   ASN1BOOL extbit;
   OOCTXT lctxt;

   /* extension bit */

   DECODEBIT (pctxt, &extbit);

   if (!extbit) {
      stat = decodeConsUnsigned (pctxt, &ui, 0, 24);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 1;

      switch (ui) {
         /* gatekeeperRequest */
         case 0:
            invokeStartElement (pctxt, "gatekeeperRequest", -1);

            pvalue->u.gatekeeperRequest = ALLOC_ASN1ELEM (pctxt, H225GatekeeperRequest);

            stat = asn1PD_H225GatekeeperRequest (pctxt, pvalue->u.gatekeeperRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "gatekeeperRequest", -1);

            break;

         /* gatekeeperConfirm */
         case 1:
            invokeStartElement (pctxt, "gatekeeperConfirm", -1);

            pvalue->u.gatekeeperConfirm = ALLOC_ASN1ELEM (pctxt, H225GatekeeperConfirm);

            stat = asn1PD_H225GatekeeperConfirm (pctxt, pvalue->u.gatekeeperConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "gatekeeperConfirm", -1);

            break;

         /* gatekeeperReject */
         case 2:
            invokeStartElement (pctxt, "gatekeeperReject", -1);

            pvalue->u.gatekeeperReject = ALLOC_ASN1ELEM (pctxt, H225GatekeeperReject);

            stat = asn1PD_H225GatekeeperReject (pctxt, pvalue->u.gatekeeperReject);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "gatekeeperReject", -1);

            break;

         /* registrationRequest */
         case 3:
            invokeStartElement (pctxt, "registrationRequest", -1);

            pvalue->u.registrationRequest = ALLOC_ASN1ELEM (pctxt, H225RegistrationRequest);

            stat = asn1PD_H225RegistrationRequest (pctxt, pvalue->u.registrationRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "registrationRequest", -1);

            break;

         /* registrationConfirm */
         case 4:
            invokeStartElement (pctxt, "registrationConfirm", -1);

            pvalue->u.registrationConfirm = ALLOC_ASN1ELEM (pctxt, H225RegistrationConfirm);

            stat = asn1PD_H225RegistrationConfirm (pctxt, pvalue->u.registrationConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "registrationConfirm", -1);

            break;

         /* registrationReject */
         case 5:
            invokeStartElement (pctxt, "registrationReject", -1);

            pvalue->u.registrationReject = ALLOC_ASN1ELEM (pctxt, H225RegistrationReject);

            stat = asn1PD_H225RegistrationReject (pctxt, pvalue->u.registrationReject);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "registrationReject", -1);

            break;

         /* unregistrationRequest */
         case 6:
            invokeStartElement (pctxt, "unregistrationRequest", -1);

            pvalue->u.unregistrationRequest = ALLOC_ASN1ELEM (pctxt, H225UnregistrationRequest);

            stat = asn1PD_H225UnregistrationRequest (pctxt, pvalue->u.unregistrationRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "unregistrationRequest", -1);

            break;

         /* unregistrationConfirm */
         case 7:
            invokeStartElement (pctxt, "unregistrationConfirm", -1);

            pvalue->u.unregistrationConfirm = ALLOC_ASN1ELEM (pctxt, H225UnregistrationConfirm);

            stat = asn1PD_H225UnregistrationConfirm (pctxt, pvalue->u.unregistrationConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "unregistrationConfirm", -1);

            break;

         /* unregistrationReject */
         case 8:
            invokeStartElement (pctxt, "unregistrationReject", -1);

            pvalue->u.unregistrationReject = ALLOC_ASN1ELEM (pctxt, H225UnregistrationReject);

            stat = asn1PD_H225UnregistrationReject (pctxt, pvalue->u.unregistrationReject);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "unregistrationReject", -1);

            break;

         /* admissionRequest */
         case 9:
            invokeStartElement (pctxt, "admissionRequest", -1);

            pvalue->u.admissionRequest = ALLOC_ASN1ELEM (pctxt, H225AdmissionRequest);

            stat = asn1PD_H225AdmissionRequest (pctxt, pvalue->u.admissionRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "admissionRequest", -1);

            break;

         /* admissionConfirm */
         case 10:
            invokeStartElement (pctxt, "admissionConfirm", -1);

            pvalue->u.admissionConfirm = ALLOC_ASN1ELEM (pctxt, H225AdmissionConfirm);

            stat = asn1PD_H225AdmissionConfirm (pctxt, pvalue->u.admissionConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "admissionConfirm", -1);

            break;

         /* admissionReject */
         case 11:
            invokeStartElement (pctxt, "admissionReject", -1);

            pvalue->u.admissionReject = ALLOC_ASN1ELEM (pctxt, H225AdmissionReject);

            stat = asn1PD_H225AdmissionReject (pctxt, pvalue->u.admissionReject);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "admissionReject", -1);

            break;

         /* bandwidthRequest */
         case 12:
            invokeStartElement (pctxt, "bandwidthRequest", -1);

            pvalue->u.bandwidthRequest = ALLOC_ASN1ELEM (pctxt, H225BandwidthRequest);

            stat = asn1PD_H225BandwidthRequest (pctxt, pvalue->u.bandwidthRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "bandwidthRequest", -1);

            break;

         /* bandwidthConfirm */
         case 13:
            invokeStartElement (pctxt, "bandwidthConfirm", -1);

            pvalue->u.bandwidthConfirm = ALLOC_ASN1ELEM (pctxt, H225BandwidthConfirm);

            stat = asn1PD_H225BandwidthConfirm (pctxt, pvalue->u.bandwidthConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "bandwidthConfirm", -1);

            break;

         /* bandwidthReject */
         case 14:
            invokeStartElement (pctxt, "bandwidthReject", -1);

            pvalue->u.bandwidthReject = ALLOC_ASN1ELEM (pctxt, H225BandwidthReject);

            stat = asn1PD_H225BandwidthReject (pctxt, pvalue->u.bandwidthReject);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "bandwidthReject", -1);

            break;

         /* disengageRequest */
         case 15:
            invokeStartElement (pctxt, "disengageRequest", -1);

            pvalue->u.disengageRequest = ALLOC_ASN1ELEM (pctxt, H225DisengageRequest);

            stat = asn1PD_H225DisengageRequest (pctxt, pvalue->u.disengageRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "disengageRequest", -1);

            break;

         /* disengageConfirm */
         case 16:
            invokeStartElement (pctxt, "disengageConfirm", -1);

            pvalue->u.disengageConfirm = ALLOC_ASN1ELEM (pctxt, H225DisengageConfirm);

            stat = asn1PD_H225DisengageConfirm (pctxt, pvalue->u.disengageConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "disengageConfirm", -1);

            break;

         /* disengageReject */
         case 17:
            invokeStartElement (pctxt, "disengageReject", -1);

            pvalue->u.disengageReject = ALLOC_ASN1ELEM (pctxt, H225DisengageReject);

            stat = asn1PD_H225DisengageReject (pctxt, pvalue->u.disengageReject);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "disengageReject", -1);

            break;

         /* locationRequest */
         case 18:
            invokeStartElement (pctxt, "locationRequest", -1);

            pvalue->u.locationRequest = ALLOC_ASN1ELEM (pctxt, H225LocationRequest);

            stat = asn1PD_H225LocationRequest (pctxt, pvalue->u.locationRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "locationRequest", -1);

            break;

         /* locationConfirm */
         case 19:
            invokeStartElement (pctxt, "locationConfirm", -1);

            pvalue->u.locationConfirm = ALLOC_ASN1ELEM (pctxt, H225LocationConfirm);

            stat = asn1PD_H225LocationConfirm (pctxt, pvalue->u.locationConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "locationConfirm", -1);

            break;

         /* locationReject */
         case 20:
            invokeStartElement (pctxt, "locationReject", -1);

            pvalue->u.locationReject = ALLOC_ASN1ELEM (pctxt, H225LocationReject);

            stat = asn1PD_H225LocationReject (pctxt, pvalue->u.locationReject);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "locationReject", -1);

            break;

         /* infoRequest */
         case 21:
            invokeStartElement (pctxt, "infoRequest", -1);

            pvalue->u.infoRequest = ALLOC_ASN1ELEM (pctxt, H225InfoRequest);

            stat = asn1PD_H225InfoRequest (pctxt, pvalue->u.infoRequest);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "infoRequest", -1);

            break;

         /* infoRequestResponse */
         case 22:
            invokeStartElement (pctxt, "infoRequestResponse", -1);

            pvalue->u.infoRequestResponse = ALLOC_ASN1ELEM (pctxt, H225InfoRequestResponse);

            stat = asn1PD_H225InfoRequestResponse (pctxt, pvalue->u.infoRequestResponse);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "infoRequestResponse", -1);

            break;

         /* nonStandardMessage */
         case 23:
            invokeStartElement (pctxt, "nonStandardMessage", -1);

            pvalue->u.nonStandardMessage = ALLOC_ASN1ELEM (pctxt, H225NonStandardMessage);

            stat = asn1PD_H225NonStandardMessage (pctxt, pvalue->u.nonStandardMessage);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "nonStandardMessage", -1);

            break;

         /* unknownMessageResponse */
         case 24:
            invokeStartElement (pctxt, "unknownMessageResponse", -1);

            pvalue->u.unknownMessageResponse = ALLOC_ASN1ELEM (pctxt, H225UnknownMessageResponse);

            stat = asn1PD_H225UnknownMessageResponse (pctxt, pvalue->u.unknownMessageResponse);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "unknownMessageResponse", -1);

            break;

         default:
            return ASN_E_INVOPT;
      }
   }
   else {
      stat = decodeSmallNonNegWholeNumber (pctxt, &ui);
      if (stat != ASN_OK) return stat;
      else pvalue->t = ui + 26;

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return stat;

      stat = decodeOpenType (pctxt, &openType.data, &openType.numocts);
      if (stat != ASN_OK) return stat;

      copyContext (&lctxt, pctxt);
      initContextBuffer (pctxt, openType.data, openType.numocts);

      switch (pvalue->t) {
         /* requestInProgress */
         case 26:
            invokeStartElement (pctxt, "requestInProgress", -1);

            pvalue->u.requestInProgress = ALLOC_ASN1ELEM (pctxt, H225RequestInProgress);

            stat = asn1PD_H225RequestInProgress (pctxt, pvalue->u.requestInProgress);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "requestInProgress", -1);

            break;

         /* resourcesAvailableIndicate */
         case 27:
            invokeStartElement (pctxt, "resourcesAvailableIndicate", -1);

            pvalue->u.resourcesAvailableIndicate = ALLOC_ASN1ELEM (pctxt, H225ResourcesAvailableIndicate);

            stat = asn1PD_H225ResourcesAvailableIndicate (pctxt, pvalue->u.resourcesAvailableIndicate);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "resourcesAvailableIndicate", -1);

            break;

         /* resourcesAvailableConfirm */
         case 28:
            invokeStartElement (pctxt, "resourcesAvailableConfirm", -1);

            pvalue->u.resourcesAvailableConfirm = ALLOC_ASN1ELEM (pctxt, H225ResourcesAvailableConfirm);

            stat = asn1PD_H225ResourcesAvailableConfirm (pctxt, pvalue->u.resourcesAvailableConfirm);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "resourcesAvailableConfirm", -1);

            break;

         /* infoRequestAck */
         case 29:
            invokeStartElement (pctxt, "infoRequestAck", -1);

            pvalue->u.infoRequestAck = ALLOC_ASN1ELEM (pctxt, H225InfoRequestAck);

            stat = asn1PD_H225InfoRequestAck (pctxt, pvalue->u.infoRequestAck);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "infoRequestAck", -1);

            break;

         /* infoRequestNak */
         case 30:
            invokeStartElement (pctxt, "infoRequestNak", -1);

            pvalue->u.infoRequestNak = ALLOC_ASN1ELEM (pctxt, H225InfoRequestNak);

            stat = asn1PD_H225InfoRequestNak (pctxt, pvalue->u.infoRequestNak);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "infoRequestNak", -1);

            break;

         /* serviceControlIndication */
         case 31:
            invokeStartElement (pctxt, "serviceControlIndication", -1);

            pvalue->u.serviceControlIndication = ALLOC_ASN1ELEM (pctxt, H225ServiceControlIndication);

            stat = asn1PD_H225ServiceControlIndication (pctxt, pvalue->u.serviceControlIndication);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "serviceControlIndication", -1);

            break;

         /* serviceControlResponse */
         case 32:
            invokeStartElement (pctxt, "serviceControlResponse", -1);

            pvalue->u.serviceControlResponse = ALLOC_ASN1ELEM (pctxt, H225ServiceControlResponse);

            stat = asn1PD_H225ServiceControlResponse (pctxt, pvalue->u.serviceControlResponse);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "serviceControlResponse", -1);

            break;

         /* admissionConfirmSequence */
         case 33:
            invokeStartElement (pctxt, "admissionConfirmSequence", -1);

            pvalue->u.admissionConfirmSequence = ALLOC_ASN1ELEM (pctxt, H225_SeqOfH225AdmissionConfirm);

            stat = asn1PD_H225_SeqOfH225AdmissionConfirm (pctxt, pvalue->u.admissionConfirmSequence);
            if (stat != ASN_OK) return stat;

            invokeEndElement (pctxt, "admissionConfirmSequence", -1);

            break;

         default:;
      }

      copyContext (pctxt, &lctxt);
   }

   return (stat);
}

